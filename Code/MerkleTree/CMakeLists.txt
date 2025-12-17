cmake_minimum_required(VERSION 3.15)
project(MerkleTreeProject)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OPENSSL_ROOT_DIR "/opt/homebrew/opt/openssl@3")
set(OPENSSL_INCLUDE_DIR "/opt/homebrew/opt/openssl@3/include")

find_package(OpenSSL REQUIRED)

add_executable(MerkleTree 
    main.cpp
    MerkleTree_Core.cpp
    MerkleTree_Build.cpp
    MerkleTree_Proof.cpp
    MerkleTree_Utils.cpp
)

target_include_directories(MerkleTree PRIVATE ${OPENSSL_INCLUDE_DIR})

target_link_libraries(MerkleTree PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
)
