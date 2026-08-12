cmake_minimum_required(VERSION 3.15)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(SupplyChainSnapshot LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

if(NOT TARGET merkle_tree)
    set(MERKLE_TREE_BUILD_CLI OFF)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/../PrivateChain/MerkleTree
        ${CMAKE_CURRENT_BINARY_DIR}/merkle_tree
    )
endif()

add_library(
    snapshot
    STATIC
    src/snapshot.cpp
    src/snapshot_policy.cpp
)

add_library(Snapshot::snapshot ALIAS snapshot)

target_include_directories(
    snapshot
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(snapshot PUBLIC MerkleTree::merkle_tree)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(snapshot PRIVATE -Wall -Wextra -Wpedantic)
endif()
