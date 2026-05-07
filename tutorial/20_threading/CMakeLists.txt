add_executable(tutorial.20_threading main.cpp)
target_link_libraries(tutorial.20_threading blog)
target_compile_features(tutorial.20_threading PRIVATE cxx_std_23)
