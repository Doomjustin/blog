#include <file_system/base_file.h>

#include <catch2/catch_test_macros.hpp>

// ---------------------------------------------------------------------------
// BaseFile::flag bitwise operators
// ---------------------------------------------------------------------------

TEST_CASE("BaseFile::flag bitwise operators", "[base_file][flag]")
{
    using flag = fs::BaseFile::flag;

    SECTION("operator| combines flags")
    {
        auto combined = flag::create | flag::write_only;
        auto expected = static_cast<flag>(O_CREAT | O_WRONLY);
        CHECK(combined == expected);
    }

    SECTION("operator& masks flags")
    {
        auto combined = flag::create | flag::write_only | flag::truncate;
        CHECK((combined & flag::truncate) == flag::truncate);
        CHECK((combined & flag::append) == static_cast<flag>(0));
    }

    SECTION("operator~ inverts flags")
    {
        auto all      = flag::create | flag::write_only;
        auto inverted = ~all;
        CHECK((inverted & flag::create)     == static_cast<flag>(0));
        CHECK((inverted & flag::write_only) == static_cast<flag>(0));
    }

    SECTION("operator|= accumulates flags")
    {
        flag f = flag::write_only;
        f |= flag::create;
        f |= flag::truncate;
        CHECK(f == (flag::write_only | flag::create | flag::truncate));
    }

    SECTION("operator&= narrows flags")
    {
        flag f = flag::create | flag::write_only | flag::truncate;
        f &= flag::create | flag::truncate;
        CHECK((f & flag::write_only) == static_cast<flag>(0));
        CHECK((f & flag::create)     == flag::create);
    }
}

// ---------------------------------------------------------------------------
// BaseFile::mode bitwise operators
// ---------------------------------------------------------------------------

TEST_CASE("BaseFile::mode bitwise operators", "[base_file][mode]")
{
    using mode = fs::BaseFile::mode;

    SECTION("operator| combines permission bits")
    {
        auto rw = mode::owner_read | mode::owner_write;
        auto expected = static_cast<mode>(S_IRUSR | S_IWUSR);
        CHECK(rw == expected);
    }

    SECTION("operator& masks permission bits")
    {
        auto rw = mode::owner_read | mode::owner_write;
        CHECK((rw & mode::owner_read)  == mode::owner_read);
        CHECK((rw & mode::owner_exec)  == mode::none);
    }

    SECTION("rw_r_r shorthand equals 0644")
    {
        auto expected = static_cast<mode>(S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        CHECK(mode::rw_r_r == expected);
    }

    SECTION("rwxr_xr_x shorthand equals 0755")
    {
        auto expected = static_cast<mode>(S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
        CHECK(mode::rwxr_xr_x == expected);
    }

    SECTION("operator|= accumulates permission bits")
    {
        mode m = mode::owner_read;
        m |= mode::owner_write;
        CHECK(m == (mode::owner_read | mode::owner_write));
    }

    SECTION("operator&= narrows permission bits")
    {
        mode m = mode::owner_read | mode::owner_write | mode::group_read;
        m &= mode::owner_read | mode::owner_write;
        CHECK((m & mode::group_read) == mode::none);
    }
}
