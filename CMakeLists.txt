cmake_minimum_required(VERSION 3.10)
project(C_re C)

set(CMAKE_C_STANDARD 99)

add_executable(C_re main.c)
add_executable(hello write-a-C-interpreter/hello_world.c)