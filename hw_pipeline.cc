#include <memory>
#include <taskflow/taskflow.hpp>
#include <taskflow/algorithm/pipeline.hpp>
#include <string>
#include <unistd.h>
#include <utility>
#include <perfetto.h>

#include "hw_pipeline.h"
#include "taskflow_helper.h"
#include "common_helper.h"

const size_t num_lines = 128;

void make_still_process_taskflow(tf::Taskflow& taskflow)
{
    auto taskA = taskflow.emplace(
        [](){ submit_to_hardware("NPU0", "post process1", 20); },
        [](){ submit_to_hardware("DSP0", "post process2", 50); }
    );
}

void make_liveview_process_taskflow(tf::Taskflow& taskflow)
{
    auto taskA = taskflow.emplace(
        [](){ submit_to_hardware("NPU1", "ROI", 1); },
        [](){ submit_to_hardware("ISP0", "HWPD", 3); }
    );
}

auto make_liveview_pipeline()
{
    auto liveview_process_executor = create_executor();
    auto liveview_process_tf = create_taskflow();

    make_liveview_process_taskflow(*liveview_process_tf);
    auto *liveview_pipeline = new tf::Pipeline(num_lines,
        tf::Pipe{tf::PipeType::SERIAL, [](tf::Pipeflow& pipeflow) {
            if(pipeflow.token() == 6) {
                pipeflow.stop();
                return;
            }
            if(pipeflow.token() == 0) {
                my_msleep(10);
                return;
            }
            submit_to_hardware("Sensor", "Liveview", 10);
        }},
        tf::Pipe{tf::PipeType::PARALLEL, [taskflow=liveview_process_tf, executor=liveview_process_executor](tf::Pipeflow& pipeflow) {
             executor->run(*taskflow);
        }}
    );

    return liveview_pipeline;
}

void make_coninue_capture_taskflow(tf::Taskflow& main_taskflow) {

    auto still_process_executor = create_executor();
    auto still_process_tf = create_taskflow();

    make_still_process_taskflow(*still_process_tf);

    auto liveview_pipeline = make_liveview_pipeline();

    auto [still_frame_task, still_process_task] = main_taskflow.emplace(
        [liveview_pipeline](){ submit_to_hardware("Sensor", "Sensor", 110); liveview_pipeline->reset(); },
        [taskflow=still_process_tf, executor=still_process_executor](){ executor->run(*taskflow); }
    );
    still_frame_task.name("still_frame");
    still_process_task.name("still_process");

    tf::Task liveview_task = main_taskflow.composed_of(*liveview_pipeline)
    .name("liveview pipeline");

    still_frame_task.precede(still_process_task, liveview_task);
}

void tutorial_multi_pipeline()
{
    tf::Taskflow taskflow("taskflow processing pipeline");
    tf::Executor executor;
    const size_t num_pipes = 5;

    std::array<tf::Taskflow, num_pipes> taskflow_array;

    make_coninue_capture_taskflow(taskflow_array[0]);

    tf::Pipeline pipeline_obj(num_lines,
        tf::Pipe{tf::PipeType::SERIAL, [&](tf::Pipeflow& pipeflow) {
            if(pipeflow.token() == 5) {
                pipeflow.stop();
                return;
            }
            executor.corun(taskflow_array[pipeflow.pipe()]);
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
    wait_all_executors();
}
