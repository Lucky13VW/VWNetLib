link_directories(
   ${base_lib_path}
)

include_directories(
   ../venues
   ../
)

add_executable(test.out test.cpp )
target_link_libraries(test.out ${base_lib} pthread)

add_executable(multi_test.out multi_test.cpp ../venues/common.cpp)
target_link_libraries(multi_test.out  pthread)
