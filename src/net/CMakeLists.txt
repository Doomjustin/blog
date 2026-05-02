file(GLOB_RECURSE BLOG_NET_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.h")
file(GLOB_RECURSE BLOG_NET_IMPLEMENT_SOURCE CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.cpp")
file(GLOB_RECURSE BLOG_NET_TEST_SOURCE CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.test.cpp")

list(FILTER BLOG_NET_IMPLEMENT_SOURCE EXCLUDE REGEX "\\.test\\.cpp$")

add_library(blog_net ${BLOG_NET_IMPLEMENT_SOURCE})
add_library(blog::net ALIAS blog_net)

target_compile_features(blog_net PUBLIC cxx_std_23)
target_include_directories(blog_net PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(blog_net
    PUBLIC
        blog::common
        blog::async
        PkgConfig::liburing
)