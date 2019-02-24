cmake_minimum_required (VERSION 2.8)

project(epoll_net)
set(base_lib "epoll_net")
set(base_lib_path "./")

set(json_lib "jsoncpp")
if (CMAKE_BUILD_TYPE MATCHES "Debug")
  set(json_lib_path "./jsoncpp/lib/debug")
  #set(json_lib_path "/home/vw/codelab/simulator/jsoncpp/lib/debug")
else()
  set(json_lib_path "./jsoncpp/lib/release")
  #set(json_lib_path "/home/vw/codelab/simulator/jsoncpp/lib/release")
endif()

# cmake -DCMAKE_BUILD_TYPE=Debug ../
set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall")
add_definitions(-std=c++11)

add_library(${base_lib} STATIC
thread_pool.hpp epoll_net.cpp)

#aux_source_directory(. DIR_SRCS)
#add_executable(Demo main.cpp)
add_subdirectory(server)
add_subdirectory(client)
add_subdirectory(test)
