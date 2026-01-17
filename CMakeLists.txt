cmake_minimum_required(VERSION 3.16)
project(ioq3_map_exporter)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dependencies
find_package(Eigen3 3.3 REQUIRED NO_MODULE)
find_package(ZLIB REQUIRED)
find_package(nlohmann_json 3.2.0 REQUIRED)
find_package(glog REQUIRED)
find_package(gflags REQUIRED)
find_package(GTest REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MINIZIP REQUIRED minizip)

# tinygltf is header-only.
find_path(TINYGLTF_INCLUDE_DIR tiny_gltf.h)
if (NOT TINYGLTF_INCLUDE_DIR)
    message(FATAL_ERROR "tiny_gltf.h not found")
endif()

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
