#ifndef BLOG_FILE_SYSTEM_RANDOM_ACCESS_FILE_H
#define BLOG_FILE_SYSTEM_RANDOM_ACCESS_FILE_H

#include <filesystem>

#include "allocate_awaiter.h"
#include "basic_file.h"
#include "file_sync_awaiter.h"
#include "read_awaiter.h"
#include "status_awaiter.h"
#include "truncate_awaiter.h"
#include "write_awaiter.h"

namespace fs {

class RandomAccessFile : public BasicFile {
private:
    static auto open(const std::string& path, flag flags, std::filesystem::perms perms) -> int;

public:
    RandomAccessFile(const std::string& path, flag flags,
                     std::filesystem::perms perms = std::filesystem::perms::none)
      : BasicFile{ open(path, flags, perms) }
    {}

    explicit RandomAccessFile(int fd)
      : BasicFile{ fd }
    {}

    auto async_read_some(std::uint64_t offset, std::span<std::byte> buffer) -> ReadAwaiter
    {
        return { native_handle(), buffer, offset };
    }

    auto async_write_some(std::uint64_t offset, std::span<const std::byte> buffer) -> WriteAwaiter
    {
        return { native_handle(), buffer, offset };
    }

    auto async_status() -> StatusAwaiter
    {
        return { native_handle() };
    }

    auto async_truncate(std::uint64_t new_size) -> TruncateAwaiter
    {
        return { native_handle(), new_size };
    }

    auto async_fsync() -> FileSyncAwaiter
    {
        return { native_handle() };
    }

    auto async_fdatasync() -> FileSyncAwaiter
    {
        return { native_handle(), IORING_FSYNC_DATASYNC };
    }

    auto async_allocate(std::uint64_t offset, std::uint64_t size) -> AllocateAwaiter
    {
        return { native_handle(), offset, size };
    }
};

} // namespace fs

#endif // BLOG_FILE_SYSTEM_RANDOM_ACCESS_FILE_H