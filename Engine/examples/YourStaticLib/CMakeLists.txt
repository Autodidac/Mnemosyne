/**************************************************************
 *   Epoch Engine - Modern Renderer Skeleton (2026)
 *   License: MIT (adapt as needed)
 **************************************************************/
cmake_minimum_required(VERSION 3.29)
project(YourStaticLib LANGUAGES CXX)

add_library(YourStaticLib STATIC
    YourStaticLib.cpp
    pch.cpp
)
target_link_libraries(YourStaticLib PRIVATE mylib)

if (MSVC)
    target_compile_options(YourStaticLib PRIVATE /W4 /permissive-)
else()
    target_compile_options(YourStaticLib PRIVATE -Wall -Wextra -Wpedantic)
endif()
