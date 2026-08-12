cmake_minimum_required(VERSION 3.10)
project(StandaloneHttpServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

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
    thread_pool.cpp
    auth_utils.cpp
    block_merkle.cpp
    db_utils.cpp
    log_utils.cpp
    ../MemoryPool/mempool.cpp
    ../ConMemPool/concurrency_mempool.cpp
)

target_link_libraries(
    control_server
    PRIVATE
    MerkleTree::merkle_tree
    ${SQLite3_LIBRARIES}
    Threads::Threads
)

target_include_directories(control_server PRIVATE ${SQLite3_INCLUDE_DIRS})

target_compile_definitions(
    control_server
    PRIVATE
    CONTROL_DEFAULT_STATIC_DIR="${CMAKE_CURRENT_BINARY_DIR}/control_static"
    CONTROL_DEFAULT_DATABASE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../Database/supply_chain.db"
)

add_executable(user_server user_server.cpp)

target_compile_definitions(
    user_server
    PRIVATE USER_DEFAULT_STATIC_DIR="${CMAKE_CURRENT_BINARY_DIR}/user_static"
)

# Static resources are build targets so source changes and deleted files are
# synchronized whenever the corresponding server target is built.
function(add_static_sync sync_target consumer_target source_dir output_dir description)
    add_custom_target(
        ${sync_target}
        ALL
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${output_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${output_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${source_dir}" "${output_dir}"
        COMMENT "Synchronizing ${description} static resources"
        VERBATIM
    )
    add_dependencies(${consumer_target} ${sync_target})
endfunction()

add_static_sync(
    sync_control_static
    control_server
    "${CMAKE_CURRENT_SOURCE_DIR}/control_static"
    "${CMAKE_CURRENT_BINARY_DIR}/control_static"
    "control server"
)

add_static_sync(
    sync_user_static
    user_server
    "${CMAKE_CURRENT_SOURCE_DIR}/static"
    "${CMAKE_CURRENT_BINARY_DIR}/user_static"
    "user server"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(control_server PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(user_server PRIVATE -Wall -Wextra -Wpedantic)
endif()
