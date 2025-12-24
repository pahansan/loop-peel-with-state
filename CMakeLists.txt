cmake_minimum_required(VERSION 3.20)
project(LoopPeelWithState LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(LLVM REQUIRED CONFIG)
include(AddLLVM)

message(STATUS "LLVM include dirs: ${LLVM_INCLUDE_DIRS}")
message(STATUS "LLVM version: ${LLVM_PACKAGE_VERSION}")

add_llvm_pass_plugin(LoopPeelWithState
  LoopPeelWithState.cpp
)

target_include_directories(LoopPeelWithState PRIVATE ${LLVM_INCLUDE_DIRS})
