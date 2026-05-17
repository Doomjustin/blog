#ifndef BLOG_FILE_SYSTEM_FILE_H
#define BLOG_FILE_SYSTEM_FILE_H

#include <cstdint>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>

#include <async/async.h>

namespace fs {

class BasicFile {
protected:
    static constexpr int invalid_fd = -1;

    int fd_ = invalid_fd;

    BasicFile(int fd) noexcept
      : fd_{ fd }
    {}

    [[nodiscard]]
    constexpr auto is_open() const noexcept -> bool
    {
        return fd_ != invalid_fd;
    }

public:
    enum class permission : std::uint16_t {
        none = 0,
        owner_read = S_IRUSR,
        owner_write = S_IWUSR,
        owner_exec = S_IXUSR,
        owner_all = S_IRWXU,
        group_read = S_IRGRP,
        group_write = S_IWGRP,
        group_exec = S_IXGRP,
        group_all = S_IRWXG,
        others_read = S_IROTH,
        others_write = S_IWOTH,
        others_exec = S_IXOTH,
        others_all = S_IRWXO,
        // common shorthand combinations
        rw_r_r = owner_read | owner_write | group_read | others_read,
        rwxr_xr_x = owner_all | group_read | group_exec | others_read | others_exec
    };

    enum class flag : std::uint32_t {
        read_only = O_RDONLY,
        write_only = O_WRONLY,
        read_write = O_RDWR,
        create = O_CREAT,
        exclusive = O_EXCL,
        truncate = O_TRUNC,
        append = O_APPEND,
        sync_all_on_write = O_SYNC,
        data_sync = O_DSYNC,
        close_on_exec = O_CLOEXEC,
        non_blocking = O_NONBLOCK,
        // io_uring / Linux extras
        direct = O_DIRECT,
        no_follow = O_NOFOLLOW,
        no_atime = O_NOATIME,
        path_only = O_PATH,
    };

    virtual ~BasicFile()
    {
        close();
    }

    BasicFile(const BasicFile&) = delete;
    auto operator=(const BasicFile&) -> BasicFile& = delete;

    BasicFile(BasicFile&& other) noexcept;
    auto operator=(BasicFile&& other) noexcept -> BasicFile&;

    void close();

    auto release() noexcept -> int
    {
        return std::exchange(fd_, invalid_fd);
    }

    auto async_close() noexcept -> async::CloseAwaiter
    {
        return async::CloseAwaiter{ std::exchange(fd_, invalid_fd) };
    }

    [[nodiscard]]
    constexpr auto native_handle() const noexcept -> int
    {
        return fd_;
    }
};

constexpr auto operator|(BasicFile::permission a, BasicFile::permission b) -> BasicFile::permission
{
    return static_cast<BasicFile::permission>(std::to_underlying(a) | std::to_underlying(b));
}

constexpr auto operator&(BasicFile::permission a, BasicFile::permission b) -> BasicFile::permission
{
    return static_cast<BasicFile::permission>(std::to_underlying(a) & std::to_underlying(b));
}

constexpr auto operator~(BasicFile::permission a) -> BasicFile::permission
{
    return static_cast<BasicFile::permission>(~std::to_underlying(a));
}

constexpr auto operator|=(BasicFile::permission& a, BasicFile::permission b)
    -> BasicFile::permission&
{
    a = a | b;
    return a;
}

constexpr auto operator&=(BasicFile::permission& a, BasicFile::permission b)
    -> BasicFile::permission&
{
    a = a & b;
    return a;
}

constexpr auto operator|(BasicFile::flag a, BasicFile::flag b) -> BasicFile::flag
{
    return static_cast<BasicFile::flag>(std::to_underlying(a) | std::to_underlying(b));
}

constexpr auto operator&(BasicFile::flag a, BasicFile::flag b) -> BasicFile::flag
{
    return static_cast<BasicFile::flag>(std::to_underlying(a) & std::to_underlying(b));
}

constexpr auto operator~(BasicFile::flag a) -> BasicFile::flag
{
    return static_cast<BasicFile::flag>(~std::to_underlying(a));
}

constexpr auto operator|=(BasicFile::flag& a, BasicFile::flag b) -> BasicFile::flag&
{
    a = a | b;
    return a;
}

constexpr auto operator&=(BasicFile::flag& a, BasicFile::flag b) -> BasicFile::flag&
{
    a = a & b;
    return a;
}

} // namespace fs

#endif // BLOG_FILE_SYSTEM_FILE_H