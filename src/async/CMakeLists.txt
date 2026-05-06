file(GLOB BLOG_ASYNC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.h")
file(GLOB BLOG_ASYNC_IMPLEMENT_SOURCE CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.cpp")
file(GLOB BLOG_ASYNC_TEST_SOURCE CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.test.cpp")

list(FILTER BLOG_ASYNC_IMPLEMENT_SOURCE EXCLUDE REGEX "\\.test\\.cpp$")

add_library(blog_async ${BLOG_ASYNC_IMPLEMENT_SOURCE})
add_library(blog::async ALIAS blog_async)

target_compile_features(blog_async PUBLIC cxx_std_23)
target_include_directories(blog_async PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(blog_async
    PUBLIC
        blog::common
        PkgConfig::liburing
)

if (blog_BUILD_TESTS AND BLOG_ASYNC_TEST_SOURCE)
    include(Catch)
    add_executable(tests.async.unit ${BLOG_ASYNC_TEST_SOURCE})
    target_link_libraries(tests.async.unit
        PRIVATE
            blog::async
            Catch2::Catch2WithMain
    )
    catch_discover_tests(tests.async.unit)
endif()