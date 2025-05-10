#include <taskflow/taskflow.hpp>
#include <taskflow/algorithm/pipeline.hpp>
#include "trace_categories.h"
#include <string>
#include <thread>
#include <unistd.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>
#include <atomic>
#include <memory>

#include "hw_pipeline.h"

enum class HWType : uint8_t {
  DSP0,
  DSP1,
  DSP2,
  NPU,
  NUM,
};

std::unordered_map<HWType, std::string> hw_type_name = {
  {HWType::DSP0, "DSP0"},
  {HWType::DSP1, "DSP1"},
  {HWType::DSP2, "DSP2"},
  {HWType::NPU, "NPU"},
};

// 硬件模拟器：每个硬件独占一个线程，支持任务排队
class HardwareSimulator {
public:
    struct TaskInfo {
        std::function<void()> task;
        std::string node_name;
        int exec_time_ms;
        size_t task_id;
    };

    HardwareSimulator(std::string  name)
        : name_(std::move(name)), stop_flag_(false), task_counter_(0) {
        worker_ = std::thread([this]() { this->thread_func(); });
    }

    ~HardwareSimulator() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            stop_flag_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    // 投递任务到硬件队列
    void submit(std::function<void()> task, const std::string& node_name, int exec_time_ms) {
        size_t task_id = next_task_id();
        TRACE_EVENT_INSTANT("rendering", "Hardware_Submit",
                           "hardware", name_,
                           "node", node_name,
                           "task_id", task_id);

        {
            std::lock_guard<std::mutex> lock(mtx_);
            tasks_.push({std::move(task), node_name, exec_time_ms, task_id});
        }
        cv_.notify_one();
    }

    // 获取硬件名
    std::string name() const { return name_; }

private:
    size_t next_task_id() {
        return task_counter_++;
    }

    void thread_func() {
        char thread_name[32];
        snprintf(thread_name, sizeof(thread_name), "HW_%s", name_.c_str());


        // Give a custom name for the traced thread.
        perfetto::ThreadTrack thread_track = perfetto::ThreadTrack::Current();
        perfetto::protos::gen::TrackDescriptor desc = thread_track.Serialize();
        desc.mutable_thread()->set_thread_name(name_);
        perfetto::TrackEvent::SetTrackDescriptor(thread_track, desc);

        while (true) {
            TaskInfo task_info;
            {
                std::unique_lock<std::mutex> lock(mtx_);
                cv_.wait(lock, [this]() { return stop_flag_ || !tasks_.empty(); });

                if (stop_flag_ && tasks_.empty()) break;

                task_info = std::move(tasks_.front());
                tasks_.pop();
            }

            // 记录硬件执行任务的开始
            TRACE_EVENT_BEGIN("rendering", perfetto::DynamicString("HW_Task_" + task_info.node_name),
                             "node", task_info.node_name,
                             "hardware", name_,
                             "task_id", task_info.task_id);

            // 执行任务
            my_msleep(task_info.exec_time_ms);  // 模拟硬件执行时间
            printf("[hardware %s] executing for node: %s (time: %d ms)\n",
                   name_.c_str(), task_info.node_name.c_str(), task_info.exec_time_ms);

            // 执行实际回调
            task_info.task();

            // 记录硬件执行任务的结束
            TRACE_EVENT_END("rendering");

            TRACE_EVENT_INSTANT("rendering", "HW_Task_Complete",
                               "node", task_info.node_name,
                               "hardware", name_,
                               "task_id", task_info.task_id);
        }
    }

    std::string name_;
    std::queue<TaskInfo> tasks_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::thread worker_;
    bool stop_flag_;
    std::atomic<size_t> task_counter_;
};

// 全局硬件池
std::vector<std::unique_ptr<HardwareSimulator>> g_hardware_pool;

// 初始化硬件池
void init_hardware_pool() {
    TRACE_EVENT("rendering", "初始化硬件池", "size", static_cast<size_t>(HWType::NUM));
    g_hardware_pool.clear();
    for (size_t i = 0; i < static_cast<size_t>(HWType::NUM); ++i) {
        auto hw_iterator = hw_type_name.find(static_cast<HWType>(i));
        if (hw_iterator != hw_type_name.end()) {
            g_hardware_pool.emplace_back(std::make_unique<HardwareSimulator>(hw_iterator->second));
        } else {
            fprintf(stderr, "ERROR: HWType value missing in hw_type_name map\n");
            return;
        }
    }
}

// DAG节点异步请求硬件，并等待任务完成
void submit_to_hardware(size_t hw_id, const std::string& node_name, int exec_time_ms) {
    TRACE_EVENT("rendering", perfetto::DynamicString("DAG_" + node_name),
               "node", node_name,
               "hardware_id", hw_id,
               "execution_time", exec_time_ms);

    // 使用 shared_ptr 包装 promise，使 lambda 可复制
    auto done_promise = std::make_shared<std::promise<void>>();
    std::future<void> done_future = done_promise->get_future();

    g_hardware_pool[hw_id]->submit(
        [promise_ptr = done_promise, node_name]() {
            // 当硬件任务完成时，设置 promise 的值
            promise_ptr->set_value();
        },
        node_name,
        exec_time_ms
    );

    done_future.wait(); // DAG节点同步等待硬件任务完成
}
