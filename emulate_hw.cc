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


using perfetto::DynamicString;
using perfetto::NamedTrack;
using perfetto::ThreadTrack;
using std::function;
using std::string;
using std::queue;
using std::mutex;
using std::lock_guard;
using std::condition_variable;
using std::unique_ptr;
using std::shared_ptr;
using std::atomic;
using std::promise;
using std::future;
using std::unordered_map;
using std::thread;
using std::vector;
using std::unique_lock;

unordered_map<HWType, string> hw_type_name = {
  {HWType::Sensor, "Sensor"},
  {HWType::DSP0, "DSP0"},
  {HWType::DSP1, "DSP1"},
  {HWType::DSP2, "DSP2"},
  {HWType::NPU, "NPU"},
};

// 硬件模拟器：每个硬件独占一个线程，支持任务排队
class HardwareSimulator {
public:
    struct TaskInfo {
        function<void()> task;
        string node_name;
        double exec_time_ms;
        size_t task_id;
    };

    HardwareSimulator(std::string  name)
        : name_(std::move(name)), stop_flag_(false), task_counter_(0) {
        worker_ = std::thread([this]() { this->thread_func(); });
    }

    ~HardwareSimulator() {
        {
            lock_guard<mutex> lock(mtx_);
            stop_flag_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    // 投递任务到硬件队列
    void submit(function<void()> task, const string& node_name, double exec_time_ms) {
        size_t task_id = next_task_id();
        // TRACE_EVENT_INSTANT("rendering", "Hardware_Submit",
        //                    "hardware", name_,
        //                    "node", node_name,
        //                    "task_id", task_id);

        {
            lock_guard<mutex> lock(mtx_);
            tasks_.push({std::move(task), node_name, exec_time_ms, task_id});
        }
        cv_.notify_one();
    }

    // 获取硬件名
    [[nodiscard]] string name() const { return name_; }

private:
    size_t next_task_id() {
        return task_counter_++;
    }

    void thread_func() {
        // 全局模拟进程track（pid=10001，可自定义）
        static auto hw_proc_track = perfetto::ProcessTrack::Global(100001);
        static bool track_desc_set = false;
        if (!track_desc_set) {
            auto proc_desc = hw_proc_track.Serialize();
            proc_desc.mutable_process()->set_process_name("HardwareSimProcess");
            perfetto::TrackEvent::SetTrackDescriptor(hw_proc_track, proc_desc);
            track_desc_set = true;
        }
        // 每个硬件线程唯一tid（用hash(name_)），track name为硬件名，归属模拟进程
        auto tid = static_cast<uint64_t>(std::hash<std::string>{}(name_));
        auto thread_track = perfetto::ThreadTrack::Global(tid);
        auto desc = thread_track.Serialize();
        desc.mutable_thread()->set_thread_name(name_);
        desc.set_parent_uuid(hw_proc_track.uuid); // 归属模拟进程
        perfetto::TrackEvent::SetTrackDescriptor(thread_track, desc);

        while (true) {
            TaskInfo task_info;
            {
                unique_lock<mutex> lock(mtx_);
                cv_.wait(lock, [this]() { return stop_flag_ || !tasks_.empty(); });

                if (stop_flag_ && tasks_.empty()) break;

                task_info = std::move(tasks_.front());
                tasks_.pop();
            }

            // 记录硬件执行任务的开始，指定track为该硬件线程
            TRACE_EVENT_BEGIN("rendering", DynamicString(task_info.node_name), thread_track,
                             "node", task_info.node_name,
                             "hardware", name_,
                             "task_id", task_info.task_id);

            // 执行任务
            my_msleep(task_info.exec_time_ms);  // 模拟硬件执行时间
            printf("[hardware %s] executing for node: %s (time: %f ms)\n",
                   name_.c_str(), task_info.node_name.c_str(), task_info.exec_time_ms);

            // 执行实际回调
            task_info.task();

            // 记录硬件执行任务的结束
            TRACE_EVENT_END("rendering", thread_track);
        }
    }

    string name_;
    queue<TaskInfo> tasks_;
    mutex mtx_;
    condition_variable cv_;
    thread worker_;
    bool stop_flag_;
    atomic<size_t> task_counter_;
};

// 全局硬件池，key为string
unordered_map<string, unique_ptr<HardwareSimulator>> g_hardware_pool;

// DAG节点异步请求硬件，并等待任务完成
void submit_to_hardware(const std::string& hw_name, const std::string& node_name, double exec_time_ms) {
    // TRACE_EVENT("rendering", DynamicString(node_name),
    //              "node", node_name,
    //              "hardware", hw_name,
    //              "execution_time", exec_time_ms);
    TRACE_EVENT_BEGIN("rendering", DynamicString(node_name), NamedTrack(DynamicString(node_name)),
               "node", node_name,
               "hardware_name", hw_name,
               "execution_time", exec_time_ms);

    // 如果硬件线程不存在则自动创建
    if (g_hardware_pool.find(hw_name) == g_hardware_pool.end()) {
        g_hardware_pool.emplace(hw_name, std::make_unique<HardwareSimulator>(hw_name));
    }

    // 使用 shared_ptr 包装 promise，使 lambda 可复制
    auto done_promise = std::make_shared<promise<void>>();
    future<void> done_future = done_promise->get_future();

    g_hardware_pool[hw_name]->submit(
        [promise_ptr = done_promise, node_name]() {
            // 当硬件任务完成时，设置 promise 的值
            promise_ptr->set_value();
        },
        node_name,
        exec_time_ms
    );

    done_future.wait(); // DAG节点同步等待硬件任务完成
    TRACE_EVENT_END("rendering", NamedTrack(DynamicString(node_name)));
}
