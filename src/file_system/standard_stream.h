#ifndef BLOG_FILE_SYSTEM_STANDARD_STREAM_H
#define BLOG_FILE_SYSTEM_STANDARD_STREAM_H

#include "read_awaiter.h"
#include "write_awaiter.h"

#include <async/async.h>

namespace fs {

class ReadStream {
public:
    ReadStream(async::IOContext& context, int fd)
      : context_{ &context }, 
        fd_{ fd }
    {}

    auto async_read(std::span<std::byte> buffer) -> ReadAwaiter
    {
        return { *context_, fd_, buffer };
    }

private:
    async::IOContext* context_;
    int fd_;
};


class WriteStream {
public:
    WriteStream(async::IOContext& context, int fd)
      : context_{ &context }, 
        fd_{ fd }
    {}

    auto async_write(std::span<const std::byte> buffer) -> WriteAwaiter
    {
        return { *context_, fd_, buffer };
    }

private:
    async::IOContext* context_;
    int fd_;
};

inline auto async_stdin(async::IOContext& context = async::this_coroutine::context()) -> ReadStream
{
    return { context, STDIN_FILENO };
}

inline auto async_stdout(async::IOContext& context = async::this_coroutine::context()) -> WriteStream
{
    return { context, STDOUT_FILENO };
}

inline auto async_stderr(async::IOContext& context = async::this_coroutine::context()) -> WriteStream
{
    return { context, STDERR_FILENO };
}

} // namespace fs

#endif // BLOG_FILE_SYSTEM_STANDARD_STREAM_H