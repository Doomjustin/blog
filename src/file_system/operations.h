#ifndef BLOG_FILE_SYSTEM_OPERATIONS_H
#define BLOG_FILE_SYSTEM_OPERATIONS_H

#include <span>

#include <file_system/read_all_awaiter.h>
#include <file_system/stream_file.h>
#include <file_system/write_all_awaiter.h>

namespace fs {

/**
 * @brief Read from `file` until `buffer` is full or EOF is reached.
 *
 * Retries automatically on partial reads. Returns the number of bytes
 * actually read; a value smaller than `buffer.size()` means EOF was
 * reached before the buffer was filled. Never returns an error for EOF.
 *
 * @param file   Open stream file to read from.
 * @param buffer Destination span; must remain valid until the coroutine resumes.
 */
[[nodiscard]]
inline auto read_all(StreamFile& file, std::span<std::byte> buffer) -> ReadAllAwaiter
{
    return { file.context(), file.native_handle(), buffer };
}

/**
 * @brief Write the entire `buffer` to `file`, retrying on partial writes.
 *
 * Returns the total bytes written on success, or an error code if the
 * underlying write operation fails.
 *
 * @param file   Open stream file to write to.
 * @param buffer Source span; must remain valid until the coroutine resumes.
 */
[[nodiscard]]
inline auto write_all(StreamFile& file, std::span<const std::byte> buffer) -> WriteAllAwaiter
{
    return { file.context(), file.native_handle(), buffer };
}

} // namespace fs

#endif // BLOG_FILE_SYSTEM_OPERATIONS_H
