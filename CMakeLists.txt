cmake_minimum_required(VERSION 3.24)
project(apollo_g_streamer)

set(CMAKE_CXX_STANDARD 20)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0>=1.4)

file(GLOB_RECURSE src_files CONFIGURE_DEPENDS src/*.cpp)
add_executable(apollo_g_streamer ${src_files})

target_include_directories(apollo_g_streamer PRIVATE ${GST_INCLUDE_DIRS})
target_link_libraries(apollo_g_streamer ${GST_LIBRARIES})
