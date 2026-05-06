add_executable(example.graceful_shutdown_server main.cpp)
target_link_libraries(example.graceful_shutdown_server PRIVATE blog::blog)
