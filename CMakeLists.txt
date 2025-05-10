cmake_minimum_required(VERSION 3.5.0)

project(CGraph VERSION 3.0.0)

set(CMAKE_CXX_STANDARD 20)

# include(${CMAKE_CURRENT_SOURCE_DIR}/../CGraph/cmake/CGraph-env-include.cmake)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/perfetto_sdk
    ${CMAKE_CURRENT_SOURCE_DIR}/../taskflow
)

add_executable(emulate_graph
    # $<TARGET_OBJECTS:CGraph>
    ${CMAKE_CURRENT_SOURCE_DIR}/perfetto_sdk/perfetto.cc
    trace_categories.cc
    main.cc
)
