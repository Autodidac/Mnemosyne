/**************************************************************
 *   Epoch Engine - Modern Renderer Skeleton (2026)
 *   License: MIT (adapt as needed)
 **************************************************************/
cmake_minimum_required(VERSION 3.29)
project(mylib LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(mylib STATIC)

target_include_directories(mylib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Gather existing sources (keep deterministic list by globbing in configure step)
file(GLOB_RECURSE EPOCH_LIB_CPP CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)
file(GLOB_RECURSE EPOCH_LIB_IXX CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/*.ixx
)

list(FILTER EPOCH_LIB_CPP EXCLUDE REGEX "tests_main\\.cpp$")

target_sources(mylib PRIVATE ${EPOCH_LIB_CPP})

# C++ modules support varies; prefer FILE_SET when supported.
# If your toolchain supports it, switch to:
# target_sources(mylib PUBLIC FILE_SET CXX_MODULES FILES ${EPOCH_LIB_IXX})
target_sources(mylib PRIVATE ${EPOCH_LIB_IXX})

if (MSVC)
    target_compile_options(mylib PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
    target_compile_options(mylib PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_subdirectory(examples/AppExample)
add_subdirectory(examples/YourStaticLib)

add_executable(mylib_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tests_main.cpp
)
target_link_libraries(mylib_tests PRIVATE mylib)

enable_testing()
add_test(NAME mylib_tests COMMAND mylib_tests)


# AI HTTP (Windows)
if (WIN32)
  target_link_libraries(mylib PRIVATE winhttp)
endif()
