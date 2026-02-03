/**************************************************************
 *   Epoch Engine - Modern Renderer Skeleton (2026)
 *   License: MIT (adapt as needed)
 **************************************************************/
cmake_minimum_required(VERSION 3.29)
project(App LANGUAGES CXX)

add_executable(App src/app.cpp)
target_link_libraries(App PRIVATE mylib)

if (MSVC)
    target_compile_options(App PRIVATE /W4 /permissive-)
else()
    target_compile_options(App PRIVATE -Wall -Wextra -Wpedantic)
endif()
