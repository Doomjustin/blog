#ifndef BLOG_FILE_SYSTEM_ASYNC_OPEN_H
#define BLOG_FILE_SYSTEM_ASYNC_OPEN_H

#include <string>
#include <system_error>

#include <async/async.h>
#include <file_system/base_file.h>
#include <file_system/open_awaiter.h>

namespace fs {

/**
 * @brief Asynchronously open a file and construct a `FileType` object.
 *
 * Submits an `openat(AT_FDCWD, ...)` via io_uring and suspends the calling
 * coroutine until the file is open.  On success a fully constructed
 * `FileType` (e.g. `StreamFile`, `RandomAccessFile`) is returned.  On
 * failure a `std::system_error` is thrown.
 *
 * @tparam FileType  Concrete file type to construct.  Must be derived from
 *                   `BaseFile` and provide a constructor of the form
 *                   `FileType(int fd, async::IOContext&)`.
 *
 * @param context     The I/O context that drives this operation.
 * @param path        Path to the file.
 * @param flags       Open flags (e.g. `BaseFile::flag::read_only`).
 * @param permissions File-creation mode bits; ignored when `create` is not
 *                    set in `flags`.  Defaults to 0644.
 *
 * @throws std::system_error  If the kernel returns an error.
 */
template<typename FileType>
auto async_open(async::IOContext& context,
                const std::string& path,
                BaseFile::flag flags,
                BaseFile::permission permissions = BaseFile::permission::rw_r_r)
    -> async::Task<FileType>
{
    auto result = co_await OpenAwaiter{ context, path, flags, permissions };

    if (!result)
        throw std::system_error(result.error());

    co_return FileType{ *result, context };
}

} // namespace fs

#endif // BLOG_FILE_SYSTEM_ASYNC_OPEN_H
