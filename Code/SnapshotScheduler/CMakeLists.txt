cmake_minimum_required(VERSION 3.15)

add_library(snapshot_scheduler STATIC
    src/snapshot_scheduler.cpp
)

target_include_directories(snapshot_scheduler
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(snapshot_scheduler PUBLIC cxx_std_17)

add_library(SnapshotScheduler::snapshot_scheduler ALIAS snapshot_scheduler)
