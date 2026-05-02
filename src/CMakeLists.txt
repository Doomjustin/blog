add_subdirectory(common)
add_subdirectory(async)
add_subdirectory(net)


add_library(blog INTERFACE blog.h)
add_library(blog::blog ALIAS blog)

target_include_directories(blog INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(blog
    INTERFACE 
        blog::common
        blog::async
        blog::net
)