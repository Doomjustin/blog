function(add_example name)
    add_executable(example.${name} ${name}.cpp)
    target_link_libraries(example.${name} PRIVATE blog::blog)
    target_compile_features(example.${name} PRIVATE cxx_std_23)
endfunction()

add_example(hello_world)
add_example(echo_server)