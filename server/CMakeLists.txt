link_directories(
   ${base_lib_path}
   ${json_lib_path}
)

include_directories(
   ../venues
   ../jsoncpp/include
   ../
)

MESSAGE(${json_lib_path})

add_executable(bse_server.out bse_server.cpp ../venues/common.cpp)
target_link_libraries(bse_server.out ${base_lib} ${json_lib} pthread)
