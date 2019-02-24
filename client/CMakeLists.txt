link_directories(
   ${base_lib_path}
   ${json_lib_path}
)

include_directories(
   ../venues
   ../jsoncpp/include
   ../
)

add_executable(bse_client.out bse_client.cpp ../venues/common.cpp)
target_link_libraries(bse_client.out ${base_lib} ${json_lib} pthread)
