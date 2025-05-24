#pragma once

#include <memory>
#include <fstream>

#include <queue>
#include <taskflow/taskflow.hpp>
#include <taskflow/algorithm/pipeline.hpp>

inline std::queue<std::shared_ptr<tf::Executor>> g_executor_array;
inline std::queue<std::shared_ptr<tf::Taskflow>> g_taskflow_array;

inline std::shared_ptr<tf::Executor> create_executor()
{
    auto executor = std::make_shared<tf::Executor>();
    g_executor_array.emplace(executor);
    return executor;
}

inline std::shared_ptr<tf::Taskflow> create_taskflow()
{
    auto taskflow = std::make_shared<tf::Taskflow>();
    g_taskflow_array.emplace(taskflow);
    return taskflow;
}

inline void wait_all_executors()
{
    while(!g_executor_array.empty()) {
        auto executor = g_executor_array.front();
        g_executor_array.pop();
        executor->wait_for_all();
    }
}

 inline void dump_taskflow(tf::Taskflow& taskflow, const std::string& filename) {
    std::ofstream taskflow_dumpfile(filename);
    taskflow.dump(taskflow_dumpfile);
 }
