file(GLOB_RECURSE BLOG_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.h")
file(GLOB_RECURSE BLOG_IMPLEMENT_SOURCE CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.cpp")
file(GLOB_RECURSE BLOG_TEST_SOURCE CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.test.cpp")

list(FILTER BLOG_IMPLEMENT_SOURCE EXCLUDE REGEX "\\.test\\.cpp$")


add_library(blog ${BLOG_IMPLEMENT_SOURCE})

add_library(blog::blog ALIAS blog)

target_compile_features(blog PUBLIC cxx_std_23)

target_include_directories(blog PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(blog
    PUBLIC 
        spdlog::spdlog
        Microsoft.GSL::GSL
        magic_enum::magic_enum
        PkgConfig::liburing
        PkgConfig::jemalloc
)