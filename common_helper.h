#pragma once
#include <chrono>
#include <thread>

#define my_msleep(ms) std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>(ms * 1000)));
