#ifndef BLOG_FILE_SYSTEM_RANDOM_ACCESS_FILE_H
#define BLOG_FILE_SYSTEM_RANDOM_ACCESS_FILE_H

#include <cstdint>
#include <span>

#include <file_system/base_file.h>
#include <file_system/read_at_awaiter.h>
#include <file_system/read_awaiter.h>
#include <file_system/write_at_awaiter.h>
#include <file_system/write_awaiter.h>

namespace fs {

/**
 * @brief File type for positional I/O without tracking a shared file offset.
 *
 * Each read and write carries an explicit byte offset, mirroring `pread(2)`
 * and `pwrite(2)` semantics. The file's current position is never consulted
 * or modified, which makes it safe for multiple coroutines to issue
 * concurrent I/O to disjoint regions of the same file.
 */
class RandomAccessFile : public BaseFile {
public:
    using BaseFile::BaseFile;

    /**
     * @brief Suspend until a read using the file's current position completes.
     *
     * @param buffer Writable span to receive the data.
     * @return Awaiter yielding bytes read, or an error code on failure.
     *         A result of 0 indicates EOF.
     */
    [[nodiscard]]
    auto async_read(std::span<std::byte> buffer) -> ReadAwaiter
    {
        return { context(), native_handle(), buffer };
    }

    /**
     * @brief Suspend until a write using the file's current position completes.
     *
     * @param buffer Read-only span of data to write.
     * @return Awaiter yielding bytes written, or an error code on failure.
     */
    [[nodiscard]]
    auto async_write(std::span<const std::byte> buffer) -> WriteAwaiter
    {
        return { context(), native_handle(), buffer };
    }

    /**
     * @brief Suspend until a read at the given byte offset completes.
     *
     * @param offset Byte offset from the start of the file.
     * @param buffer Writable span to receive the data.
     * @return Awaiter yielding bytes read, or an error code on failure.
     *         A result of 0 indicates EOF at `offset`.
     */
    [[nodiscard]]
    auto async_read(std::uint64_t offset, std::span<std::byte> buffer) -> ReadAtAwaiter
    {
        return { context(), native_handle(), offset, buffer };
    }

    /**
     * @brief Suspend until a write at the given byte offset completes.
     *
     * @param offset Byte offset from the start of the file.
     * @param buffer Read-only span of data to write.
     * @return Awaiter yielding bytes written, or an error code on failure.
     */
    [[nodiscard]]
    auto async_write(std::uint64_t offset, std::span<const std::byte> buffer) -> WriteAtAwaiter
    {
        return { context(), native_handle(), offset, buffer };
    }
};

} // namespace fs

#endif // BLOG_FILE_SYSTEM_RANDOM_ACCESS_FILE_H
