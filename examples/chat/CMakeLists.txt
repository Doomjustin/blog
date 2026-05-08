add_executable(chat.server server.cpp)
target_link_libraries(chat.server PRIVATE blog)

add_executable(chat.client client.cpp)
target_link_libraries(chat.client PRIVATE blog)
