/**************************************************************
 *   Epoch Engine - Modern Renderer Skeleton (2026)
 *   License: MIT (adapt as needed)
 **************************************************************/
cmake_minimum_required(VERSION 3.29)
project(AppExample LANGUAGES CXX)

add_executable(AppExample src/app.cpp)
target_link_libraries(AppExample PRIVATE mylib)

if (MSVC)
    target_compile_options(AppExample PRIVATE /W4 /permissive-)
else()
    target_compile_options(AppExample PRIVATE -Wall -Wextra -Wpedantic)
endif()
