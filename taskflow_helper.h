#pragma once

#include <memory>

#include <queue>
#include <taskflow/taskflow.hpp>
#include <taskflow/algorithm/pipeline.hpp>

inline std::queue<std::shared_ptr<tf::Executor>> g_executor_array;

inline std::shared_ptr<tf::Executor> create_executor()
{
    auto executor = std::make_shared<tf::Executor>();
    g_executor_array.push(executor);
    return executor;
}

inline void wait_all_executors()
{
    while(!g_executor_array.empty()) {
        auto executor = g_executor_array.front();
        g_executor_array.pop();
        executor->wait_for_all();
    }
}
