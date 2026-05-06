add_executable(tutorial.14_all_any main.cpp)
target_link_libraries(tutorial.14_all_any blog)
target_compile_features(tutorial.14_all_any PRIVATE cxx_std_23)
