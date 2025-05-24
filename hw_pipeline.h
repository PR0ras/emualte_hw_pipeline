#ifndef __HW_PIPELINE_H__
#define __HW_PIPELINE_H__

#include <taskflow/taskflow.hpp>
#include <taskflow/algorithm/pipeline.hpp>
#include <string>
#include <unistd.h>

#define my_msleep(ms) std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>(ms * 1000)));

void submit_to_hardware(const std::string& hw_name, const std::string& node_name, double exec_time_ms);
void tutorial_multi_pipeline();

#endif // __HW_PIPELINE_H__
