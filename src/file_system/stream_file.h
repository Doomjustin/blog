#ifndef BLOG_FILE_SYSTEM_STREAM_FILE_H
#define BLOG_FILE_SYSTEM_STREAM_FILE_H

#include <cstdint>
#include <expected>
#include <span>
#include <system_error>
#include <utility>

#include <unistd.h>

#include <async/async.h>
#include <file_system/base_file.h>
#include <file_system/read_awaiter.h>
#include <file_system/write_awaiter.h>

namespace fs {

class StreamFile : public BaseFile {
public:
    using BaseFile::BaseFile;

    [[nodiscard]]
    auto async_read(std::span<std::byte> buffer) -> ReadAwaiter
    {
        return { context(), native_handle(), buffer };
    }

    [[nodiscard]]
    auto async_write(std::span<const std::byte> buffer) -> WriteAwaiter
    {
        return { context(), native_handle(), buffer };
    }

    auto seek(std::int64_t offset, how whence) -> std::expected<std::int64_t, std::error_code>
    {
        auto result = ::lseek(native_handle(), offset, std::to_underlying(whence));
        if (result == -1)
            return unexpected_system_error(errno);
        return result;
    }
};

} // namespace fs

#endif // BLOG_FILE_SYSTEM_STREAM_FILE_H