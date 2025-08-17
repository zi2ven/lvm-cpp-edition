cmake_minimum_required(VERSION 3.31)
project(lvm_cpp_edition)

set(CMAKE_CXX_STANDARD 20)
add_subdirectory(argparse)

add_executable(lvm_cpp_edition main.cpp
        vm.cpp
        vm.h
        memory.h
        memory.cpp
        module.h
        module.cpp
        bytecode.h
        exception.h
        exception.cpp
        bytecode.cpp)
