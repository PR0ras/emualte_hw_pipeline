cmake_minimum_required(VERSION 3.20)

project(EmulateGraph VERSION 3.0.0)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/perfetto_sdk
    ${CMAKE_CURRENT_SOURCE_DIR}/../taskflow
)

add_executable(emulate_graph
    ${CMAKE_CURRENT_SOURCE_DIR}/perfetto_sdk/perfetto.cc
    emulate_hw.cc
    hw_pipeline.cc
    trace_categories.cc
    main.cc
)
