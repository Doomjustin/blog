file(GLOB BLOG_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.h")
file(GLOB BLOG_IMPLEMENT_SOURCE CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.cpp")
file(GLOB BLOG_TEST_SOURCE CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.test.cpp")

list(FILTER BLOG_IMPLEMENT_SOURCE EXCLUDE REGEX "\\.test\\.cpp$")


add_library(blog ${BLOG_IMPLEMENT_SOURCE})

add_library(blog::blog ALIAS blog)

target_compile_features(blog PUBLIC cxx_std_23)

target_include_directories(blog PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(blog 
    PUBLIC 
        spdlog::spdlog
        Microsoft.GSL::GSL
        PkgConfig::liburing
        PkgConfig::jemalloc
)

# if(BUILD_TESTING AND ${PROJECT_NAME}_BUILD_TESTS)
#     add_executable(blog.test ${BLOG_TEST_SOURCE})

#     target_link_libraries(blog.test
#         PRIVATE
#             Catch2::Catch2WithMain
#     )

#     target_compile_features(blog.test PRIVATE cxx_std_23)

#     add_test(NAME blog.test COMMAND blog.test)
# endif()