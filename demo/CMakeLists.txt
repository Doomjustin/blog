function(add_blog_executable name)
    add_executable(blog.${name} ${name}.cpp)
    target_link_libraries(blog.${name} PRIVATE blog::demo)
    target_compile_features(blog.${name} PUBLIC cxx_std_23)
endfunction()


add_blog_executable(io_context_v1)
add_blog_executable(io_context_v2)
add_blog_executable(timeout_v1)
add_blog_executable(socket_v1)
add_blog_executable(tcp_ip)
add_blog_executable(benchmark)
add_blog_executable(stream_io)

add_executable(blog.demo demo.cpp)
target_link_libraries(blog.demo PRIVATE blog::blog)
target_compile_features(blog.demo PUBLIC cxx_std_23)

add_executable(blog.benchmark2 benchmark2.cpp)
target_link_libraries(blog.benchmark2 PRIVATE blog::blog)
target_compile_features(blog.benchmark2 PUBLIC cxx_std_23)


add_executable(blog.benchmark_asio benchmark_asio.cpp)
target_compile_definitions(blog.benchmark_asio 
    PRIVATE
        ASIO_HAS_IO_URING
)
target_link_libraries(blog.benchmark_asio 
    PRIVATE 
        asio::asio
        spdlog::spdlog
        PkgConfig::liburing
        PkgConfig::jemalloc
)

add_executable(blog.benchmark_asio_no_uring benchmark_asio.cpp)
target_link_libraries(blog.benchmark_asio_no_uring
    PRIVATE 
        asio::asio
        spdlog::spdlog
        PkgConfig::jemalloc
)
