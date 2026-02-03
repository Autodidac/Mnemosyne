cmake_minimum_required(VERSION 3.28)

project(Mnemosyne LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(mylib STATIC)

target_sources(mylib
  PRIVATE
    src/mylib.cpp
    src/core.log.cpp
    src/core.time.cpp
    src/core.error.cpp
    src/core.format.cpp
    src/core.string.cpp
    src/core.id.cpp
    src/core.math.cpp
    src/core.env.cpp
    src/core.path.cpp
    src/core.assert.cpp
    src/runtime.cpp
    src/core.memory.cpp
    src/core.memory.store.cpp
    src/core.memory.index.cpp
    src/core.memory.stage.cpp
    src/lib_main.cpp
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
      modules/mylib.ixx
      modules/core.log.ixx
      modules/core.time.ixx
      modules/core.error.ixx
      modules/core.format.ixx
      modules/core.string.ixx
      modules/core.id.ixx
      modules/core.math.ixx
      modules/core.env.ixx
      modules/core.path.ixx
      modules/core.assert.ixx
      modules/runtime.ixx
      modules/core.memory.ixx
      modules/core.memory.store.ixx
      modules/core.memory.index.ixx
      modules/core.memory.stage.ixx
)

target_include_directories(mylib
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(mylib PUBLIC cxx_std_23)

if (MSVC)
  target_compile_options(mylib PRIVATE /W4 /WX /EHsc)
endif()

add_executable(app src/app.cpp)

target_include_directories(app
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(app PRIVATE mylib)

target_compile_features(app PRIVATE cxx_std_23)

if (MSVC)
  target_compile_options(app PRIVATE /W4 /WX /EHsc)
endif()

option(MNEMOSYNE_BUILD_TESTS "Build tests.exe" OFF)

if (MNEMOSYNE_BUILD_TESTS)
  add_executable(tests src/tests_main.cpp)
  target_include_directories(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(tests PRIVATE mylib)
  target_compile_features(tests PRIVATE cxx_std_23)
  if (MSVC)
    target_compile_options(tests PRIVATE /W4 /WX /EHsc)
  endif()
endif()
