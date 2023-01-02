cmake_minimum_required(VERSION 3.22)
project(Radio C)

set(CMAKE_C_STANDARD 99)

add_executable(Radio Client.c Server.c)
