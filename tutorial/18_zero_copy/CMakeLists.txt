add_executable(tutorial.18_zero_copy main.cpp)
target_link_libraries(tutorial.18_zero_copy blog)
target_compile_features(tutorial.18_zero_copy PRIVATE cxx_std_23)
