function(add_blog_executable name)
    add_executable(blog.${name} ${name}.cpp)

    target_link_libraries(blog.${name} PRIVATE blog::blog)

    target_compile_features(blog.${name} PUBLIC cxx_std_23)
endfunction()


add_blog_executable(io_context_v1)
add_blog_executable(io_context_v2)
add_blog_executable(timeout_v1)