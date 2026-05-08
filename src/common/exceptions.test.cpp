#include <common/exceptions.h>

#include <cerrno>
#include <system_error>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("throw_system_error: throws std::system_error with explicit error code", "[exceptions]")
{
    REQUIRE_THROWS_AS(throw_system_error(EINVAL, "bad arg {}", 42), std::system_error);

    try {
        throw_system_error(EINVAL, "bad arg {}", 42);
    }
    catch (const std::system_error& e) {
        REQUIRE(e.code().value() == EINVAL);
        REQUIRE(e.code().category() == std::generic_category());
        REQUIRE(std::string{ e.what() }.contains("bad arg 42"));
    }
}

TEST_CASE("throw_system_error: uses current errno", "[exceptions]")
{
    errno = ENOENT;
    REQUIRE_THROWS_AS(throw_system_error("file not found"), std::system_error);

    try {
        errno = ENOENT;
        throw_system_error("file not found");
    }
    catch (const std::system_error& e) {
        REQUIRE(e.code().value() == ENOENT);
    }
}

TEST_CASE("unexpected_system_error: wraps errno", "[exceptions]")
{
    errno = EACCES;
    auto result = unexpected_system_error();
    REQUIRE(result.error().value() == EACCES);
}

TEST_CASE("unexpected_system_error: wraps std::errc", "[exceptions]")
{
    auto result = unexpected_system_error(std::errc::timed_out);
    REQUIRE(result.error() == std::make_error_code(std::errc::timed_out));
}

TEST_CASE("unexpected_system_error: wraps explicit int error", "[exceptions]")
{
    auto result = unexpected_system_error(EPERM);
    REQUIRE(result.error().value() == EPERM);
}
