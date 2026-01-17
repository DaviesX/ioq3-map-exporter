cmake_minimum_required(VERSION 3.16)
project(ioq3_map_exporter)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_IWYU "Enable Include What You Use" ON)
if(ENABLE_IWYU)
  find_program(IWYU_PATH NAMES include-what-you-use iwyu)
  if(IWYU_PATH)
    message(STATUS "IWYU found: ${IWYU_PATH}")
    set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE ${IWYU_PATH})
  else()
    message(WARNING "include-what-you-use not found, but ENABLE_IWYU is ON")
  endif()
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(
  $<$<CONFIG:Release>:-O3>
  $<$<CONFIG:Release>:-flto=auto>
  $<$<CONFIG:Release>:-march=native>
)
add_link_options(
  $<$<CONFIG:Release>:-flto=auto>
  $<$<CONFIG:Release>:-O3>
)

# Dependencies
find_package(Eigen3 3.3 REQUIRED NO_MODULE)
find_package(ZLIB REQUIRED)
find_package(nlohmann_json 3.2.0 REQUIRED)
find_package(glog REQUIRED)
find_package(gflags REQUIRED)
find_package(GTest REQUIRED)

find_package(PkgConfig REQUIRED)
# This searches for minizip.pc installed by libminizip-dev
pkg_check_modules(MINIZIP REQUIRED IMPORTED_TARGET minizip)

# tinygltf is header-only.
find_path(TINYGLTF_INCLUDE_DIR NAMES tiny_gltf.h)
if(NOT TINYGLTF_INCLUDE_DIR)
  message(FATAL_ERROR "tinygltf not found")
endif()
add_library(tinygltf INTERFACE)

# Library
add_library(ioq3_map SHARED
    src/archives.cpp
    src/bsp.cpp
)

target_link_libraries(ioq3_map PUBLIC
    Eigen3::Eigen
    ZLIB::ZLIB
    nlohmann_json::nlohmann_json
    glog
    gflags
    ${MINIZIP_LIBRARIES}
)

target_include_directories(ioq3_map PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${MINIZIP_INCLUDE_DIRS}
    ${TINYGLTF_INCLUDE_DIR}
)

# Main Executable
add_executable(ioq3_map_exporter src/main.cpp)
target_link_libraries(ioq3_map_exporter PRIVATE ioq3_map)

# Tests
enable_testing()
add_executable(ioq3_map_exporter_test
    src/archives_test.cpp
    src/bsp_test.cpp
)
target_link_libraries(ioq3_map_exporter_test PRIVATE
    ioq3_map
    GTest::gtest_main
)
