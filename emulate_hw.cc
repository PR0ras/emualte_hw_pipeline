#include "common_helper.h"
#include "trace_categories.h"
#include <perfetto.h>

#include <iostream>
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
#include <future>

using perfetto::DynamicString;
using perfetto::NamedTrack;
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

// 硬件模拟器：每个硬件独占一个线程，支持任务排队
class HardwareSimulator {
public:
    struct TaskInfo {
        function<void()> task;
        string node_name;
        double exec_time_ms;
        size_t task_id;
        shared_ptr<promise<void>> ready_promise;  // 准备就绪信号
        shared_ptr<future<void>> start_signal;    // 开始执行信号
    };

    static perfetto::Track& HardwareSimulatorTrack() {
        static int64_t pid = 10001;
        static auto hw_proc_track = perfetto::ProcessTrack::Global(pid);
        static bool track_desc_set = false;
        if (!track_desc_set) {
            auto proc_desc = hw_proc_track.Serialize();
            proc_desc.mutable_process()->set_process_name("Hardware Simulator");
            proc_desc.mutable_process()->set_pid(pid);
            perfetto::TrackEvent::SetTrackDescriptor(hw_proc_track, proc_desc);
            track_desc_set = true;
        }
        return hw_proc_track;
    }

    HardwareSimulator(string  name)
        : name_(std::move(name)), stop_flag_(false), task_counter_(0){

        auto tid_ = static_cast<uint64_t>(std::hash<string>{}(name_));

        thread_track_ = new perfetto::Track(tid_, HardwareSimulatorTrack());

        auto proc_desc = HardwareSimulatorTrack().Serialize();
        auto desc = thread_track_->Serialize();
        desc.mutable_thread()->set_thread_name(name_);
        desc.mutable_thread()->set_tid(tid_);
        desc.mutable_thread()->set_pid(proc_desc.process().pid());
        perfetto::TrackEvent::SetTrackDescriptor(*thread_track_, desc);

        worker_ = std::thread([this]() { this->thread_func(); });
    }

    ~HardwareSimulator() {
        {
            lock_guard<mutex> lock(mtx_);
            stop_flag_ = true;
            delete thread_track_;
            thread_track_ = nullptr;
        }
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    // 投递任务到硬件队列
    void submit(function<void()> task, const string& node_name, double exec_time_ms) {
        size_t task_id = next_task_id();

        {
            lock_guard<mutex> lock(mtx_);
            tasks_.push({std::move(task), node_name, exec_time_ms, task_id, nullptr, nullptr});
        }
        cv_.notify_one();
    }

    // 投递需要同步的任务
    void submit_sync(function<void()> task, const string& node_name, double exec_time_ms,
                    shared_ptr<promise<void>> ready_promise, shared_ptr<future<void>> start_signal) {
        size_t task_id = next_task_id();

        {
            lock_guard<mutex> lock(mtx_);
            tasks_.push({std::move(task), node_name, exec_time_ms, task_id, ready_promise, start_signal});
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
        while (true) {
            TaskInfo task_info;
            {
                unique_lock<mutex> lock(mtx_);
                cv_.wait(lock, [this]() { return stop_flag_ || !tasks_.empty(); });

                if (stop_flag_ && tasks_.empty()) break;

                task_info = std::move(tasks_.front());
                tasks_.pop();
            }

            // 如果是同步任务，先通知准备就绪，然后等待开始信号
            if (task_info.ready_promise && task_info.start_signal) {
                task_info.ready_promise->set_value();  // 通知已准备就绪
                task_info.start_signal->wait();        // 等待开始信号
            }

            TRACE_EVENT_BEGIN("rendering", DynamicString(task_info.node_name), *thread_track_,
                             "node", task_info.node_name,
                             "hardware", name_,
                             "task_id", task_info.task_id);


            my_msleep(task_info.exec_time_ms);
            printf("[hardware %s] executing for node: %s (time: %f ms)\n",
                   name_.c_str(), task_info.node_name.c_str(), task_info.exec_time_ms);

            // 执行实际回调
            task_info.task();

            // 记录硬件执行任务的结束
            TRACE_EVENT_END("rendering", *thread_track_);
        }
    }

    string name_;
    queue<TaskInfo> tasks_;
    mutex mtx_;
    condition_variable cv_;
    thread worker_;
    bool stop_flag_;
    atomic<size_t> task_counter_;

    perfetto::Track *thread_track_;
};

unordered_map<string, unique_ptr<HardwareSimulator>> g_hardware_pool;

void submit_to_hardware(const vector<string>& hw_names, const string& node_name, double exec_time_ms) {
    TRACE_EVENT_BEGIN("rendering", DynamicString(node_name), NamedTrack(DynamicString(node_name)),
               "node", node_name,
               "hardware_count", hw_names.size(),
               "execution_time", exec_time_ms);

    vector<future<void>> futures;

    if (hw_names.size() == 1) {
        // 单硬件情况，直接执行
        const auto& hw_name = hw_names[0];
        if (g_hardware_pool.find(hw_name) == g_hardware_pool.end()) {
            g_hardware_pool.emplace(hw_name, std::make_unique<HardwareSimulator>(hw_name));
        }

        auto done_promise = std::make_shared<promise<void>>();
        futures.push_back(done_promise->get_future());

        g_hardware_pool[hw_name]->submit(
            [promise_ptr = done_promise, node_name]() {
                promise_ptr->set_value();
            },
            node_name,
            exec_time_ms
        );
    } else {
        // 多硬件情况，需要同步执行
        vector<shared_ptr<promise<void>>> ready_promises;
        vector<shared_ptr<future<void>>> ready_futures;
        auto start_promise = std::make_shared<promise<void>>();
        auto start_future = std::make_shared<future<void>>(start_promise->get_future());

        for (const auto& hw_name : hw_names) {
            if (g_hardware_pool.find(hw_name) == g_hardware_pool.end()) {
                g_hardware_pool.emplace(hw_name, std::make_unique<HardwareSimulator>(hw_name));
            }

            auto done_promise = std::make_shared<promise<void>>();
            futures.push_back(done_promise->get_future());

            auto ready_promise = std::make_shared<promise<void>>();
            ready_promises.push_back(ready_promise);
            ready_futures.push_back(std::make_shared<future<void>>(ready_promise->get_future()));

            g_hardware_pool[hw_name]->submit_sync(
                [promise_ptr = done_promise, node_name]() {
                    promise_ptr->set_value();
                },
                node_name,
                exec_time_ms,
                ready_promise,
                start_future
            );
        }

        // 等待所有硬件准备就绪
        for (auto& ready_future : ready_futures) {
            ready_future->wait();
        }

        // 所有硬件准备就绪后，发送开始信号
        start_promise->set_value();
    }

    // 等待所有硬件任务完成
    for (auto& future : futures) {
        future.wait();
    }

    TRACE_EVENT_END("rendering", NamedTrack(DynamicString(node_name)));
}
