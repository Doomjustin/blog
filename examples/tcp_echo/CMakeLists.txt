add_executable(example.tcp_echo.client echo_client.cpp)
target_link_libraries(example.tcp_echo.client PRIVATE blog::blog)

add_executable(example.tcp_echo.server echo_server.cpp)
target_link_libraries(example.tcp_echo.server PRIVATE blog::blog)
