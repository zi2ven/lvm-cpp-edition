cmake_minimum_required(VERSION 3.28)
project(lvm_cpp_edition)

set(CMAKE_CXX_STANDARD 20)

include_directories(argparse/include)

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
