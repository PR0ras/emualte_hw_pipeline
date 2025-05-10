#include <memory>
#include <taskflow/taskflow.hpp>
#include <taskflow/algorithm/pipeline.hpp>
#include <string>
#include <unistd.h>
#include <utility>

#include "trace_categories.h"
#include "hw_pipeline.h"

const size_t num_lines = 128;

void make_still_process_taskflow(tf::Taskflow& taskflow)
{
    auto taskA = taskflow.emplace(
        [](){ submit_to_hardware("NPU0", "NPU0", 20); },
        [](){ submit_to_hardware("DSP0", "DSP0", 50); }
    );
}

void make_liveview_process_taskflow(tf::Taskflow& taskflow)
{
    auto taskA = taskflow.emplace(
        [](){ submit_to_hardware("NPU1", "NPU1", 1); },
        [](){ submit_to_hardware("ISP0", "ISP0", 3); }
    );
}

void make_taskflow1(tf::Taskflow& taskflow) {

    auto still_process_executor = std::make_shared<tf::Executor>();
    auto still_process_tf = std::make_shared<tf::Taskflow>();

    auto liveview_process_executor = std::make_shared<tf::Executor>();
    auto liveview_process_tf = std::make_shared<tf::Taskflow>();

    make_liveview_process_taskflow(*liveview_process_tf);

    make_still_process_taskflow(*still_process_tf);

    auto liveview_pipeline = new tf::Pipeline(num_lines,
        tf::Pipe{tf::PipeType::SERIAL, [](tf::Pipeflow& pipeflow) {
            if(pipeflow.token() == 6) {
                pipeflow.stop();
                return;
            }
            if(pipeflow.token() == 0) {
                my_msleep(10);
                return;
            }
            submit_to_hardware("Sensor", "Sensor", 10);
        }},
        tf::Pipe{tf::PipeType::PARALLEL, [taskflow=liveview_process_tf, executor=liveview_process_executor](tf::Pipeflow& pipeflow) {
             executor->run(*taskflow).wait();
        }}
    );

    auto [still_frame_task, still_process_task] = taskflow.emplace(
        [liveview_pipeline](){ submit_to_hardware("Sensor", "Sensor", 110); liveview_pipeline->reset(); },
        [taskflow=still_process_tf, executor=still_process_executor](){ executor->run(*taskflow); }
    );
    still_frame_task.name("still_frame");
    still_process_task.name("still_process");

    tf::Task liveview_task = taskflow.composed_of(*liveview_pipeline)
    .name("liveview pipeline");

    still_frame_task.precede(still_process_task, liveview_task);
}

void make_taskflow_spec(tf::Taskflow& taskflow, const std::string& hw_name,
                        const std::string& node_name, int exec_time_ms) {
    auto taskA = taskflow.emplace(
        [hw_name, node_name, exec_time_ms](){ submit_to_hardware(hw_name, node_name, exec_time_ms); }
    );
}

void tutorial_multi_pipeline()
{
    tf::Taskflow taskflow("taskflow processing pipeline");
    tf::Executor executor;
    tf::Executor executor_inner;
    const size_t num_pipes = 5;

    std::array<tf::Taskflow, num_pipes> taskflow_array;

    make_taskflow1(taskflow_array[0]);

    tf::Pipeline pipeline_obj(num_lines,
        tf::Pipe{tf::PipeType::SERIAL, [&](tf::Pipeflow& pipeflow) {
            if(pipeflow.token() == 5) {
                pipeflow.stop();
                return;
            }
            executor_inner.run(taskflow_array[pipeflow.pipe()]).wait();
        }}
    );

    tf::Task init = taskflow.emplace([](){ std::cout << "ready\n"; })
                            .name("starting pipeline");
    tf::Task task = taskflow.composed_of(pipeline_obj)
                            .name("pipeline");
    tf::Task stop = taskflow.emplace([](){ std::cout << "stopped\n"; })
                            .name("pipeline stopped");

    init.precede(task);
    task.precede(stop);

    executor.run(taskflow).wait();
}
