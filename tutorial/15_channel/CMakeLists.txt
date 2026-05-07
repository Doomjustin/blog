add_executable(tutorial.15_channel main.cpp)
target_link_libraries(tutorial.15_channel blog)
target_compile_features(tutorial.15_channel PRIVATE cxx_std_23)
