#ifndef BLOG_FILE_SYSTEM_STREAM_FILE_H
#define BLOG_FILE_SYSTEM_STREAM_FILE_H

#include <unistd.h>

#include <common/common.h>

#include "basic_file.h"
#include "read_awaiter.h"
#include "write_awaiter.h"

namespace fs {

class StreamFile : public BasicFile {
private:
    static auto open(const std::string& path, flag flags, permission perms) -> int;

    bool owned_{ false };

public:
    StreamFile(const std::string& path, flag flags)
      : BasicFile{ open(path, flags, permission::none) }
    {}

    StreamFile(const std::string& path, flag flags, permission perms)
      : BasicFile{ open(path, flags, perms) }
    {}

    StreamFile(int fd, bool owned = false)
      : BasicFile{ fd }
      , owned_{ owned }
    {}

    ~StreamFile()
    {
        if (!owned_)
            release();
    }

    StreamFile(StreamFile&&) = default;
    auto operator=(StreamFile&&) -> StreamFile& = default;

    [[nodiscard]]
    auto async_read(std::span<std::byte> buffer) -> ReadAwaiter
    {
        return { native_handle(), buffer };
    }

    [[nodiscard]]
    auto async_write(std::span<const std::byte> buffer) -> WriteAwaiter
    {
        return { native_handle(), buffer };
    }
};

auto stdin() -> StreamFile
{
    return StreamFile{ STDIN_FILENO };
}

auto stdout() -> StreamFile
{
    return StreamFile{ STDOUT_FILENO };
}

auto stderr() -> StreamFile
{
    return StreamFile{ STDERR_FILENO };
}

} // namespace fs

#endif // BLOG_FILE_SYSTEM_STREAM_FILE_H