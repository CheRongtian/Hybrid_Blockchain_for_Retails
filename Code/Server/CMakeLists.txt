cmake_minimum_required(VERSION 3.10)
project(StandaloneHttpServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(SQLite3 REQUIRED)

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
    control_server
    server.cpp
    db_utils.cpp
    log_utils.cpp
)

target_link_libraries(
    control_server
    PRIVATE
    MerkleTree::merkle_tree
    ${SQLite3_LIBRARIES}
)

target_include_directories(control_server PRIVATE ${SQLite3_INCLUDE_DIRS})

target_compile_definitions(
    control_server
    PRIVATE
    CONTROL_DEFAULT_STATIC_DIR="${CMAKE_CURRENT_BINARY_DIR}/control_static"
    CONTROL_DEFAULT_DATABASE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../Database/supply_chain.db"
)

add_custom_command(
    TARGET control_server
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/control_static
            ${CMAKE_CURRENT_BINARY_DIR}/control_static
)

add_executable(user_server user_server.cpp)

target_compile_definitions(
    user_server
    PRIVATE USER_DEFAULT_STATIC_DIR="${CMAKE_CURRENT_BINARY_DIR}/user_static"
)

add_custom_command(
    TARGET user_server
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/static
            ${CMAKE_CURRENT_BINARY_DIR}/user_static
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(control_server PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(user_server PRIVATE -Wall -Wextra -Wpedantic)
endif()
