function(add_example name)
    add_executable(example.${name} ${name}.cpp)
    target_link_libraries(example.${name} PRIVATE blog::blog)
    target_compile_features(example.${name} PRIVATE cxx_std_23)
endfunction()


add_subdirectory(01_hello)
add_subdirectory(02_echo)
add_subdirectory(03_chat)
add_subdirectory(04_all_any)