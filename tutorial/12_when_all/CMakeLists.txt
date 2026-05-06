add_executable(tutorial.12_when_all main.cpp)
target_link_libraries(tutorial.12_when_all blog)
target_compile_features(tutorial.12_when_all PRIVATE cxx_std_23)
