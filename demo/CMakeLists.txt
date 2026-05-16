add_subdirectory(src)

function(add_blog_executable name)
    add_executable(demo.${name} ${name}.cpp)
    target_link_libraries(demo.${name} PRIVATE blog::demo)
    target_compile_features(demo.${name} PUBLIC cxx_std_23)
endfunction()


add_blog_executable(io_context_v1)
add_blog_executable(io_context_v2)
add_blog_executable(timeout_v1)
add_blog_executable(socket_v1)
add_blog_executable(tcp_ip)
add_blog_executable(benchmark)

add_executable(demo.benchmark_asio benchmark_asio.cpp)
target_compile_definitions(demo.benchmark_asio 
    PRIVATE
        ASIO_HAS_IO_URING
)
target_link_libraries(demo.benchmark_asio 
    PRIVATE 
        asio::asio
        spdlog::spdlog
        PkgConfig::liburing
        PkgConfig::jemalloc
)

add_executable(demo.benchmark_asio_no_uring benchmark_asio.cpp)
target_link_libraries(demo.benchmark_asio_no_uring
    PRIVATE 
        asio::asio
        spdlog::spdlog
        PkgConfig::jemalloc
)
