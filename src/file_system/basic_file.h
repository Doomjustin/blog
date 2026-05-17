#ifndef BLOG_FILE_SYSTEM_FILE_H
#define BLOG_FILE_SYSTEM_FILE_H

#include <filesystem>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>

#include <async/async.h>

#include "openat_awaiter.h"

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
    enum class flag : int {
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

/// @brief 异步打开文件，返回 OpenatAwaiter。
/// @param[in] path 文件路径。
/// @param[in] flags 打开标志（如只读、只写等）。
/// @param[in] perms 文件权限（仅在创建文件时有效）。
/// @return OpenatAwaiter，co_await 后返回 expected<StreamFile, error_code>。
template<typename FileType>
auto async_open(const std::string& path, BasicFile::flag flags,
                std::filesystem::perms perms = std::filesystem::perms::none)
    -> OpenatAwaiter<FileType>
{
    return OpenatAwaiter<FileType>{ path, std::to_underlying(flags), static_cast<mode_t>(perms) };
}

} // namespace fs

#endif // BLOG_FILE_SYSTEM_FILE_H