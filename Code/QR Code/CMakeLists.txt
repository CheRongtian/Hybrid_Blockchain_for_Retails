cmake_minimum_required(VERSION 3.15)

project(qr C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(qr
    qr.c
    qrcodegen.c
    TinyPngOut.c
)

target_compile_options(qr PRIVATE
    -Wall
    -Wextra
    -g
)

add_custom_target(run
    COMMAND $<TARGET_FILE:qr>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS qr
    USES_TERMINAL
)