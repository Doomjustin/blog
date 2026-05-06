add_executable(tutorial.13_when_any main.cpp)
target_link_libraries(tutorial.13_when_any blog)
target_compile_features(tutorial.13_when_any PRIVATE cxx_std_23)
