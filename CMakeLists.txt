cmake_minimum_required(VERSION 3.10)
project(cos_log C)

set(CMAKE_C_STANDARD 99)

add_subdirectory(src)

option(DO_LOG_FUNCTION_NAME "enable printing a function name in logging" OFF)
option(DO_LOG_CURRENT_TIME "enable printing a current time in logging" ON)
