cmake_minimum_required(VERSION 3.15)

project(MerkleTreeNTree LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE AND EXISTS "/opt/homebrew/opt/openssl@3")
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/openssl@3")
endif()

find_package(OpenSSL REQUIRED COMPONENTS Crypto)

add_library(ntree_merkle STATIC src/NTreeMerkle.cpp)
target_include_directories(ntree_merkle PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(ntree_merkle PUBLIC OpenSSL::Crypto)

add_executable(merkle_ntree src/main.cpp)
target_link_libraries(merkle_ntree PRIVATE ntree_merkle)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(ntree_merkle PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(merkle_ntree PRIVATE -Wall -Wextra -Wpedantic)
endif()
