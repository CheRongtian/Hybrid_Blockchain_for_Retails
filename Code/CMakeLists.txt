cmake_minimum_required(VERSION 3.15)
project(BlockchainStructure LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MERKLE_TREE_BUILD_CLI "Build the standalone MerkleTree CLI" ON)

add_subdirectory(MerkleTree)
add_subdirectory(Server)
