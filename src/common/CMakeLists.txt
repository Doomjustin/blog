file(GLOB BLOG_COMMON_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.h")
file(GLOB BLOG_COMMON_IMPLEMENT_SOURCE CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.cpp")
file(GLOB BLOG_COMMON_TEST_SOURCE CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.test.cpp")

list(FILTER BLOG_COMMON_IMPLEMENT_SOURCE EXCLUDE REGEX "\\.test\\.cpp$")

add_library(blog_common ${BLOG_COMMON_IMPLEMENT_SOURCE})
add_library(blog::common ALIAS blog_common)

target_compile_features(blog_common PUBLIC cxx_std_23)
target_include_directories(blog_common PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(blog_common
    PUBLIC
        Microsoft.GSL::GSL
        magic_enum::magic_enum
    PRIVATE 
        spdlog::spdlog
)