cmake_minimum_required(VERSION 3.15)
project(SnapshotStorage LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SQLite3 REQUIRED)

add_library(snapshot_storage STATIC
    src/snapshot_storage.cpp
)

add_library(SnapshotStorage::snapshot_storage ALIAS snapshot_storage)

target_include_directories(snapshot_storage
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SQLite3_INCLUDE_DIRS}
)

target_link_libraries(snapshot_storage
    PUBLIC
    ${SQLite3_LIBRARIES}
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(snapshot_storage PRIVATE -Wall -Wextra -Wpedantic)
endif()
