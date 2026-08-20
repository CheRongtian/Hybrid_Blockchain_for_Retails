cmake_minimum_required(VERSION 3.15)

project(snapshot_qr C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(snapshot_qr
    qr.c
    qrcodegen.c
    TinyPngOut.c
)

target_compile_options(snapshot_qr PRIVATE
    -Wall
    -Wextra
    -g
)
