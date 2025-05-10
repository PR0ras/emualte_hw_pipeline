#include <taskflow/taskflow.hpp>
#include <taskflow/algorithm/pipeline.hpp>
#include <string>
#include <unistd.h>
#include <utility>

#include "trace_categories.h"
#include "hw_pipeline.h"

// 1st-stage function
void f1(const std::string& node) {
  TRACE_EVENT("rendering", perfetto::DynamicString("f1" +node + " run"));
  printf("f1(%s)\n", node.c_str());
}

// 2nd-stage function
void f2(const std::string& node) {
  TRACE_EVENT("rendering", perfetto::DynamicString("f2" +node + " run"));
  printf("f2(%s)\n", node.c_str());
}

// 3rd-stage function
void f3(const std::string& node) {
  TRACE_EVENT("rendering", perfetto::DynamicString("f3" +node + " run"));
  printf("f3(%s)\n", node.c_str());
}

// taskflow on the first pipe
void make_taskflow1(tf::Taskflow& taskflow) {
    auto [taskA1, taskB1, taskC1, taskD1] = taskflow.emplace(
        [](){ submit_to_hardware(0, "A1", 120); },
        [](){ submit_to_hardware(0, "B1", 80); },
        [](){ submit_to_hardware(0, "C1", 100); },
        [](){ submit_to_hardware(0, "D1", 150); }
    );
    taskA1.precede(taskB1, taskC1);
    taskD1.succeed(taskB1, taskC1);
}

// taskflow on the second pipe
void make_taskflow2(tf::Taskflow& taskflow) {
    auto [taskA2, taskB2, taskC2, taskD2] = taskflow.emplace(
        [](){ submit_to_hardware(1, "A2", 90); },
        [](){ submit_to_hardware(1, "B2", 110); },
        [](){ submit_to_hardware(1, "C2", 70); },
        [](){ submit_to_hardware(1, "D2", 130); }
    );
    taskflow.linearize({taskA2, taskB2, taskC2, taskD2});
}

// taskflow on the third pipe
void make_taskflow3(tf::Taskflow& taskflow) {
    auto [taskA3, taskB3, taskC3, taskD3] = taskflow.emplace(
        [](){ submit_to_hardware(2, "A3", 60); },
        [](){ submit_to_hardware(2, "B3", 140); },
        [](){ submit_to_hardware(2, "C3", 100); },
        [](){ submit_to_hardware(2, "D3", 120); }
    );
    taskA3.precede(taskB3, taskC3, taskD3);
}

void tutorial_multi_pipeline()
{
    tf::Taskflow taskflow("taskflow processing pipeline");
    tf::Executor executor;

    const size_t num_lines = 2;
    const size_t num_pipes = 3;

    // define the taskflow storage
    std::array<tf::Taskflow, num_pipes> taskflow_array;

    // create three different taskflows for the three pipes
    make_taskflow1(taskflow_array[0]);
    make_taskflow2(taskflow_array[1]);
    make_taskflow3(taskflow_array[2]);

    // the pipeline consists of three serial pipes
    // and up to two concurrent scheduling tokens
    tf::Pipeline pipeline_obj(num_lines,
        tf::Pipe{tf::PipeType::SERIAL, [&](tf::Pipeflow& pipeflow) {
            if(pipeflow.token() == 5) {
                pipeflow.stop();
                return;
            }
            printf("begin token %zu\n", pipeflow.token());
            TRACE_EVENT_INSTANT("rendering", "token start", "token", pipeflow.token());
            executor.corun(taskflow_array[pipeflow.pipe()]);
        }},
        tf::Pipe{tf::PipeType::SERIAL, [&](tf::Pipeflow& pipeflow) {
            executor.corun(taskflow_array[pipeflow.pipe()]);
        }},
        tf::Pipe{tf::PipeType::SERIAL, [&](tf::Pipeflow& pipeflow) {
            executor.corun(taskflow_array[pipeflow.pipe()]);
        }}
    );

    // build the pipeline graph using composition
    tf::Task init = taskflow.emplace([](){ std::cout << "ready\n"; })
                            .name("starting pipeline");
    tf::Task task = taskflow.composed_of(pipeline_obj)
                            .name("pipeline");
    tf::Task stop = taskflow.emplace([](){ std::cout << "stopped\n"; })
                            .name("pipeline stopped");

    // create task dependency
    init.precede(task);
    task.precede(stop);

    // run the pipeline
    executor.run(taskflow).wait();
}
