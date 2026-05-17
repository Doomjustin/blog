file(GLOB BLOG_FILE_SYSTEM_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.h")
file(GLOB BLOG_FILE_SYSTEM_IMPLEMENT_SOURCE CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.cpp")
file(GLOB BLOG_FILE_SYSTEM_TEST_SOURCE CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.test.cpp")

list(FILTER BLOG_FILE_SYSTEM_IMPLEMENT_SOURCE EXCLUDE REGEX "\\.test\\.cpp$")

add_library(blog_file_system ${BLOG_FILE_SYSTEM_IMPLEMENT_SOURCE})
add_library(blog::file_system ALIAS blog_file_system)

target_compile_features(blog_file_system PUBLIC cxx_std_23)
target_include_directories(blog_file_system PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)
target_link_libraries(blog_file_system
    PUBLIC
        blog::common
        blog::async
        PkgConfig::liburing
)

if (blog_BUILD_TESTS AND BLOG_FILE_SYSTEM_TEST_SOURCE)
    include(Catch)
    add_executable(tests.file_system ${BLOG_FILE_SYSTEM_TEST_SOURCE})
    target_link_libraries(tests.file_system
        PRIVATE
            blog::file_system
            Catch2::Catch2WithMain
    )
    catch_discover_tests(tests.file_system)
endif()