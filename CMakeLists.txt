cmake_minimum_required(VERSION 3.5)
project(Shell_Implementation)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES
    builtins.cpp
    builtins.h
    CMakeLists.txt
    README.md
    shell.cpp )

add_executable(Shell_Implementation ${SOURCE_FILES})