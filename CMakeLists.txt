cmake_minimum_required(VERSION 3.20)

project(clxcpp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra)
endif()

set(CLXCPP_SOURCES
  src/core.cpp
  src/tiff.cpp
  src/png.cpp
  src/export.cpp
)

set(MINIZ_SOURCES
  third_party/miniz.c
  third_party/miniz_tdef.c
  third_party/miniz_tinfl.c
)

add_library(clxcpp STATIC ${CLXCPP_SOURCES} ${MINIZ_SOURCES})
target_include_directories(clxcpp PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_include_directories(clxcpp PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

add_executable(clx src/main.cpp)
target_link_libraries(clx PRIVATE clxcpp)

enable_testing()

option(CLXCPP_BUILD_TESTS "Build the test suite" ON)
if(CLXCPP_BUILD_TESTS)
  add_subdirectory(tests)
endif()
