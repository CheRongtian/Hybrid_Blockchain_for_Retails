cmake_minimum_required(VERSION 3.15)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(MerkleTreeProject LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

if(APPLE AND EXISTS "/opt/homebrew/opt/openssl@3")
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/openssl@3")
endif()

find_package(OpenSSL REQUIRED COMPONENTS Crypto)

add_library(
    merkle_tree
    STATIC
    MerkleTree_Core.cpp
    MerkleTree_Build.cpp
    MerkleTree_Proof.cpp
    MerkleTree_Utils.cpp
)

add_library(MerkleTree::merkle_tree ALIAS merkle_tree)

target_include_directories(
    merkle_tree
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(merkle_tree PUBLIC OpenSSL::Crypto)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(merkle_tree PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(MERKLE_TREE_BUILD_CLI "Build the standalone MerkleTree CLI" ON)

if(MERKLE_TREE_BUILD_CLI)
    add_executable(merkle_cli Main.cpp)
    target_link_libraries(merkle_cli PRIVATE MerkleTree::merkle_tree)

    set(MERKLE_INPUT_COPY "${CMAKE_CURRENT_BINARY_DIR}/inp.txt")
    add_custom_command(
        OUTPUT "${MERKLE_INPUT_COPY}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/inp.txt"
                "${MERKLE_INPUT_COPY}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/inp.txt"
        COMMENT "Synchronizing MerkleTree CLI input"
        VERBATIM
    )
    add_custom_target(merkle_cli_input DEPENDS "${MERKLE_INPUT_COPY}")
    add_dependencies(merkle_cli merkle_cli_input)
endif()
