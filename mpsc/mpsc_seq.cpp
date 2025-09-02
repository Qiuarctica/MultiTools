#include "../spsc/spsc.h"
#include "../utils/test_suit.h"
#include "spsc_based_mpsc.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <sys/types.h>
#include <thread>
#include <unordered_map>
#include <vector>

class SeqMpsc {
public:
  struct SeqData {
    uint64_t seq = 0;
    uint64_t payload = 0;
  };

private:
  // private functions
  void push_to_spsc_(SeqData &data, uint64_t seq) {
    while (!spsc_queues_[seq % num_consumers_].push(data)) {
      std::this_thread::yield();
    }
  }
  // while wait for ns ns
  void wait_for_ns(uint64_t ns) {
    auto start = std::chrono::high_resolution_clock::now();
    while (true) {
      auto now = std::chrono::high_resolution_clock::now();
      auto elapsed =
          std::chrono::duration_cast<std::chrono::nanoseconds>(now - start)
              .count();
      if (elapsed >= ns) {
        break;
      }
      std::this_thread::yield();
    }
  }

  void produce_thread() {
    uint64_t global_seq = 0;
    while (!stop_.load()) {
      SeqData data;
      data.seq = global_seq++;
      data.payload = rand();
      push_to_spsc_(data, global_seq);
      wait_for_ns(50);
    }
  }

  void consume_thread(size_t consumer_id) {
    while (!stop_.load()) {
      SeqData data;
      if (spsc_queues_[consumer_id].pop(data)) {
        // 模拟处理时间导致乱序
        wait_for_ns(rand() % 400 + 100);
        data.payload ^= 0xdeadbeef;
        while (!queue_.push(data)) {
          std::this_thread::yield();
        }
      } else {
        std::this_thread::yield();
      }
    }
  }
  // make mpsc sequencial
  // 单独开一个线程用来从MPSC中取出数据并排序放到一个对外的SPSC中，把自己包装成排序的MPSC
  class Reorderer {
  private:
    struct Slot {
      bool valid = false;
      SeqData data;
    };
    std::array<Slot, 1024> fast_buffer_{};
    std::unordered_map<uint64_t, SeqData> overflow_buffer_{};
    uint64_t next_expected_seq_ = 0;
    SPSCQueue<SeqData, 4096> output_queue_;

    MPSCQueue<SeqData, 1024> &source_queue_;
    std::atomic<bool> stop_{false};
    std::thread worker_thread_;

    // debug info
    std::atomic<uint64_t> processed_count_{0};
    std::atomic<uint64_t> direct_hit_count_{0};
    std::atomic<uint64_t> l1_cached_count_{0};
    std::atomic<uint64_t> l2_cached_count_{0};
    std::atomic<uint64_t> max_disordered_count_{0};

    void reorder_work() {
      SeqData data;
      while (!stop_.load(std::memory_order_relaxed)) {
        if (source_queue_.pop(data)) {
          processed_count_++;
          process_data(data);
          try_output_ready_data();
        } else {
          std::this_thread::yield();
        }
      }
    }

    // 处理接收到的数据包，进行排序
    void process_data(const SeqData &data) {
      if (data.seq == next_expected_seq_) {
        // 正好是期望的序列号，直接输出
        direct_hit_count_++;
        output_data(data);
        next_expected_seq_++;
        try_output_ready_data();
        return;
      }

      if (data.seq < next_expected_seq_) {
        // 过期数据，直接丢弃
        return;
      }

      // data.seq > next_expected_seq_，需要缓存
      size_t slot_idx = data.seq % fast_buffer_.size();
      auto &slot = fast_buffer_[slot_idx];

      max_disordered_count_ =
          std::max(max_disordered_count_.load(), data.seq - next_expected_seq_);

      if (!slot.valid) {
        slot.valid = true;
        slot.data = data;
      } else if (slot.data.seq != data.seq) {
        // 槽位冲突，选择更接近期望序列号的数据
        uint64_t old_distance = slot.data.seq - next_expected_seq_;
        uint64_t new_distance = data.seq - next_expected_seq_;

        if (new_distance < old_distance) {
          overflow_buffer_[slot.data.seq] = slot.data;
          slot.data = data;
        } else {
          overflow_buffer_[data.seq] = data;
        }
      }
    }

    void output_data(const SeqData &data) {
      while (!output_queue_.push(data) &&
             !stop_.load(std::memory_order_relaxed)) {
        std::this_thread::yield();
      }
    }

    void try_output_ready_data() {
      // 尝试输出有序数据
      while (true) {
        bool found = false;
        // 检查一级缓存
        size_t slot_idx = next_expected_seq_ % fast_buffer_.size();
        auto &slot = fast_buffer_[slot_idx];
        if (slot.valid && slot.data.seq == next_expected_seq_) {
          output_data(slot.data);
          slot.valid = false;
          next_expected_seq_++;
          l1_cached_count_++;
          found = true;
        } else {
          // 检查二级缓存
          auto it = overflow_buffer_.find(next_expected_seq_);
          if (it != overflow_buffer_.end()) {
            output_data(it->second);
            overflow_buffer_.erase(it);
            next_expected_seq_++;
            l2_cached_count_++;
            found = true;
          }
        }

        if (!found) {
          break;
        }
      }
    }

  public:
    explicit Reorderer(MPSCQueue<SeqData, 1024> &source, bool enable_reorder)
        : source_queue_(source) {
      if (!enable_reorder) {
        return;
      }
      worker_thread_ = std::thread([this]() { reorder_work(); });
    }
    ~Reorderer() {
      stop_.store(true, std::memory_order_relaxed);
      if (worker_thread_.joinable()) {
        worker_thread_.join();
      }
    }
    bool get_next(SeqData &data) { return output_queue_.pop(data); }
    void print_debug_info() {
      std::cout << "Processed count: " << processed_count_.load() << std::endl;
      std::cout << "Direct hit count: " << direct_hit_count_.load()
                << std::endl;
      std::cout << "Direct hit rate: "
                << (direct_hit_count_.load() * 100.0 / processed_count_.load())
                << "%" << std::endl;
      std::cout << "L1 cached count: " << l1_cached_count_.load() << std::endl;
      std::cout << "L1 hit rate : "
                << (l1_cached_count_.load() * 100.0 / processed_count_.load())
                << "%" << std::endl;
      std::cout << "L2 cached count: " << l2_cached_count_.load() << std::endl;
      std::cout << "L2 hit rate : "
                << (l2_cached_count_.load() * 100.0 / processed_count_.load())
                << "%" << std::endl;
      if (processed_count_ !=
          direct_hit_count_ + l1_cached_count_ + l2_cached_count_) {
        std::cout << "Warning: processed_count_ != direct_hit_count_ + "
                     "l1_cached_count_ + l2_cached_count_"
                  << std::endl;
      }
      std::cout << "Max disordered count: " << max_disordered_count_.load()
                << std::endl;
    }
  };
  // private members
  MPSCQueue<SeqData, 1024> queue_;
  std::vector<SPSCQueue<SeqData, 1024>> spsc_queues_;
  std::vector<std::thread> threads_;
  static constexpr size_t num_consumers_ = 4;
  std::atomic<bool> stop_ = false;
  Reorderer reorderer_;

public:
  explicit SeqMpsc(bool enable_reorder)
      : spsc_queues_(num_consumers_), reorderer_(queue_, enable_reorder) {
    threads_.emplace_back(&SeqMpsc::produce_thread, this);
    for (size_t i = 0; i < num_consumers_; ++i) {
      threads_.emplace_back(&SeqMpsc::consume_thread, this, i);
    }
  }
  ~SeqMpsc() {
    stop_.store(true, std::memory_order_relaxed);
    for (auto &thread : threads_) {
      if (thread.joinable()) {
        thread.join();
      }
    }
  }

  bool get_next_ordered_data(SeqData &data) {
    return reorderer_.get_next(data);
  }
  // 不在乎是否有序
  bool get_next_data(SeqData &data) { return queue_.pop(data); }
  void print_debug_info() { reorderer_.print_debug_info(); }
};

double test_seq_ordering() {
  PRINT_INFO("开始基于访问次数的丢失检测测试...");
  SeqMpsc pipeline(true);
  SeqMpsc::SeqData data;
  auto start_time = std::chrono::high_resolution_clock::now();

  uint64_t last_seq = -1;
  size_t received_count = 0;
  size_t discontinuity_count = 0;

  while (received_count < 500000) {
    while (!pipeline.get_next_ordered_data(data)) {
      ;
    }
    if (data.seq != last_seq + 1) {
      PRINT_WARNING("检测到序列号不连续: " + std::to_string(last_seq + 1) +
                    " -> " + std::to_string(data.seq));
      discontinuity_count++;
    }
    last_seq = data.seq;
    received_count++;
  }
  auto end_time = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> duration = end_time - start_time;
  PRINT_INFO("基于访问次数的丢失检测测试完成，耗时: " +
             std::to_string(duration.count()) + "秒");
  double throughput = received_count / duration.count();
  PRINT_INFO("传输带宽：" + std::to_string(throughput) + " 数据/s");
  pipeline.print_debug_info();
  return throughput;
}

double test_origin() {
  PRINT_INFO("无排序性能测试...");
  SeqMpsc pipeline(false);
  SeqMpsc::SeqData data;
  auto start_time = std::chrono::high_resolution_clock::now();

  uint64_t last_seq = -1;
  size_t received_count = 0;
  size_t discontinuity_count = 0;

  while (received_count < 500000) {
    while (!pipeline.get_next_data(data)) {
      ;
    }
    if (data.seq != last_seq + 1) {
      discontinuity_count++;
    }
    last_seq = data.seq;
    received_count++;
  }
  auto end_time = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> duration = end_time - start_time;
  PRINT_INFO("无排序测试完成，耗时: " + std::to_string(duration.count()) +
             "秒");
  double throughput = received_count / duration.count();
  PRINT_INFO("传输带宽：" + std::to_string(throughput) + " 数据/s");
  return throughput;
}

int main() {
  // 运行十次取平均
  const int num_iterations = 10;
  double reorder_total = 0;
  double origin_total = 0;

  for (int i = 0; i < num_iterations; ++i) {
    reorder_total += test_seq_ordering();
    origin_total += test_origin();
  }

  auto reorder = reorder_total / num_iterations;
  auto origin = origin_total / num_iterations;
  // 计算性能损失
  double loss = (origin - reorder) / origin * 100;
  PRINT_INFO("性能损失: " + std::to_string(loss) + "%");
}
