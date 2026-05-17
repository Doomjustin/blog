#ifndef BLOG_FILE_SYSTEM_STREAM_FILE_H
#define BLOG_FILE_SYSTEM_STREAM_FILE_H

#include <unistd.h>

#include <common/common.h>

#include "basic_file.h"
#include "openat_awaiter.h"
#include "read_awaiter.h"
#include "write_awaiter.h"

namespace fs {

class StreamFile : public BasicFile {
private:
    bool owned_{ true };

public:
    explicit StreamFile(int fd, bool owned = true)
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

    auto async_read_some(std::span<std::byte> buffer) -> ReadAwaiter
    {
        return { native_handle(), buffer };
    }

    template<std::ranges::contiguous_range T>
    auto async_read_some(T& range) -> ReadAwaiter
    {
        return async_read_some(async::buffer(range));
    }

    auto async_write_some(std::span<const std::byte> buffer) -> WriteAwaiter
    {
        return { native_handle(), buffer };
    }

    template<std::ranges::contiguous_range T>
    auto async_write_some(const T& range) -> WriteAwaiter
    {
        return async_write_some(async::buffer(range));
    }
};

/// @brief 获取标准输入流对应的 StreamFile。
/// @return 包含标准输入流文件描述符的 StreamFile 对象。
auto std_input() -> StreamFile;

/// @brief 获取标准输出流对应的 StreamFile。
/// @return 包含标准输出流文件描述符的 StreamFile 对象。
auto std_output() -> StreamFile;

/// @brief 获取标准错误流对应的 StreamFile。
/// @return 包含标准错误流文件描述符的 StreamFile 对象。
auto std_error() -> StreamFile;

/// @brief 创建匿名管道，返回一对 StreamFile（读端和写端）。
/// @return 包含读端和写端的 StreamFile 对。first 是读端，second 是写端。
auto pipe() -> std::pair<StreamFile, StreamFile>;

} // namespace fs

#endif // BLOG_FILE_SYSTEM_STREAM_FILE_H