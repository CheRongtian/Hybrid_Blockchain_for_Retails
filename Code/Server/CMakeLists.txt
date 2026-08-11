cmake_minimum_required(VERSION 3.10)
project(StandaloneHttpServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT TARGET merkle_tree)
    set(MERKLE_TREE_BUILD_CLI OFF)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/../MerkleTree
        ${CMAKE_CURRENT_BINARY_DIR}/merkle_tree
    )
endif()

# Include current directory for header files
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(
    server
    server.cpp
    log_utils.cpp
)

# Link the standalone server dependencies.
target_link_libraries(
    server
    PRIVATE
    MerkleTree::merkle_tree
)

target_compile_definitions(
    server
    PRIVATE SERVER_DEFAULT_STATIC_DIR="${CMAKE_CURRENT_BINARY_DIR}/static"
)

add_custom_command(
    TARGET server
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/static
            ${CMAKE_CURRENT_BINARY_DIR}/static
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(server PRIVATE -Wall -Wextra -Wpedantic)
endif()
