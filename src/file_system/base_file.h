#ifndef BLOG_FILE_SYSTEM_BASE_FILE_H
#define BLOG_FILE_SYSTEM_BASE_FILE_H

#include <cstdint>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>

#include <async/async.h>
#include <file_system/close_awaiter.h>
#include <file_system/fsync_awaiter.h>

namespace fs {
class BaseFile {
public:
    using context_type = async::IOContext;

    enum class how: std::uint8_t {
        seek_set = SEEK_SET,
        seek_cur = SEEK_CUR,
        seek_end = SEEK_END
    };

    enum class permission: std::uint16_t {
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

    enum class flag: std::uint32_t {
        // access mode (mutually exclusive)
        read_only = O_RDONLY,
        write_only = O_WRONLY,
        read_write = O_RDWR,
        // creation / open behaviour
        create = O_CREAT,
        exclusive = O_EXCL,
        truncate = O_TRUNC,
        // write behaviour
        append = O_APPEND,
        sync_all_on_write = O_SYNC,
        data_sync = O_DSYNC,
        // fd flags
        close_on_exec = O_CLOEXEC,
        non_blocking = O_NONBLOCK,
        // io_uring / Linux extras
        direct = O_DIRECT,
        no_follow = O_NOFOLLOW,
        no_atime = O_NOATIME,
        path_only = O_PATH,
    };

    BaseFile(const BaseFile&) = delete;
    auto operator=(const BaseFile&) -> BaseFile& = delete;

    BaseFile(BaseFile&& other) noexcept;
    auto operator=(BaseFile&& other) noexcept -> BaseFile&;

    ~BaseFile();

    void close();

    auto release() noexcept -> int
    {
        return std::exchange(fd_, invalid_fd);
    }

    auto async_close() noexcept -> CloseAwaiter
    {
        return { *context_, std::exchange(fd_, invalid_fd) };
    }

    [[nodiscard]]
    auto context() const noexcept -> context_type& { return *context_; }

    [[nodiscard]]
    constexpr auto native_handle() const noexcept -> int { return fd_; }

protected:
    static constexpr int invalid_fd = -1;

    context_type* context_;
    int fd_ = invalid_fd;

    BaseFile(context_type& context, int fd) noexcept;

    [[nodiscard]]
    constexpr auto is_open() const noexcept -> bool 
    { 
        return fd_ != invalid_fd; 
    }
};


constexpr auto operator|(BaseFile::permission a, BaseFile::permission b) -> BaseFile::permission
{
    return static_cast<BaseFile::permission>(std::to_underlying(a) | std::to_underlying(b));
}

constexpr auto operator&(BaseFile::permission a, BaseFile::permission b) -> BaseFile::permission
{
    return static_cast<BaseFile::permission>(std::to_underlying(a) & std::to_underlying(b));
}

constexpr auto operator~(BaseFile::permission a) -> BaseFile::permission
{
    return static_cast<BaseFile::permission>(~std::to_underlying(a));
}

constexpr auto operator|=(BaseFile::permission& a, BaseFile::permission b) -> BaseFile::permission&
{
    a = a | b;
    return a;
}

constexpr auto operator&=(BaseFile::permission& a, BaseFile::permission b) -> BaseFile::permission&
{
    a = a & b;
    return a;
}

constexpr auto operator|(BaseFile::flag a, BaseFile::flag b) -> BaseFile::flag
{
    return static_cast<BaseFile::flag>(std::to_underlying(a) | std::to_underlying(b));
}

constexpr auto operator&(BaseFile::flag a, BaseFile::flag b) -> BaseFile::flag
{
    return static_cast<BaseFile::flag>(std::to_underlying(a) & std::to_underlying(b));
}

constexpr auto operator~(BaseFile::flag a) -> BaseFile::flag
{
    return static_cast<BaseFile::flag>(~std::to_underlying(a));
}

constexpr auto operator|=(BaseFile::flag& a, BaseFile::flag b) -> BaseFile::flag&
{
    a = a | b;
    return a;
}

constexpr auto operator&=(BaseFile::flag& a, BaseFile::flag b) -> BaseFile::flag&
{
    a = a & b;
    return a;
}

} // namespace fs


#endif // BLOG_FILE_SYSTEM_BASE_FILE_H