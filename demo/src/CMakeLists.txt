file(GLOB BLOG_DEMO_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.h")
file(GLOB BLOG_DEMO_IMPLEMENT_SOURCE CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.cpp")


add_library(demo ${BLOG_DEMO_IMPLEMENT_SOURCE})

add_library(blog::demo ALIAS demo)

target_compile_features(demo PUBLIC cxx_std_23)

target_include_directories(demo PUBLIC ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(demo 
    PUBLIC 
        spdlog::spdlog
        Microsoft.GSL::GSL
        PkgConfig::liburing
        PkgConfig::jemalloc
)