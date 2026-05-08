cmake_minimum_required(VERSION 3.30)

project(blog
    VERSION 0.0.1
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


include(CTest)
include(CMakeDependentOption)

cmake_dependent_option(${PROJECT_NAME}_BUILD_TESTS
    "Enable ${PROJECT_NAME} project tests targets" ON
    "BUILD_TESTING" OFF
)

find_package(spdlog CONFIG REQUIRED)
find_package(Microsoft.GSL CONFIG REQUIRED)
find_package(glaze CONFIG REQUIRED)
find_package(ctre CONFIG REQUIRED)
find_package(asio CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(liburing REQUIRED IMPORTED_TARGET liburing)
pkg_check_modules(jemalloc REQUIRED IMPORTED_TARGET jemalloc)

if (${PROJECT_NAME}_BUILD_TESTS)
    find_package(Catch2 CONFIG REQUIRED)
endif()


add_subdirectory(src)
add_subdirectory(demo)
add_subdirectory(tutorial)
add_subdirectory(examples)