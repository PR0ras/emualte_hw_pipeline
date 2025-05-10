#ifndef __HW_PIPELINE_H__
#define __HW_PIPELINE_H__

#include <taskflow/taskflow.hpp>
#include <taskflow/algorithm/pipeline.hpp>
#include <string>
#include <unistd.h>

#define my_msleep(ms) std::this_thread::sleep_for(std::chrono::milliseconds(ms));

void init_hardware_pool();
void submit_to_hardware(size_t hw_id, const std::string& node_name, int exec_time_ms);

void tutorial_multi_pipeline();

#endif // __HW_PIPELINE_H__
