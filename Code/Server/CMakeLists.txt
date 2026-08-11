cmake_minimum_required(VERSION 3.10)
project(StandaloneHttpServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Find SQLite3
find_package(SQLite3 REQUIRED)

# Include current directory for header files
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(
    server
    server.cpp
    db_utils.cpp
    log_utils.cpp
)

# Link SQLite3
target_link_libraries(server PRIVATE SQLite::SQLite3)

target_compile_definitions(
    server
    PRIVATE SERVER_DEFAULT_STATIC_DIR="${CMAKE_CURRENT_BINARY_DIR}/static"
)

file(
    COPY ${CMAKE_CURRENT_SOURCE_DIR}/static/
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/static
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(server PRIVATE -Wall -Wextra -Wpedantic)
endif()
