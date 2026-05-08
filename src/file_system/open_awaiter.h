#ifndef BLOG_FILE_SYSTEM_OPEN_AWAITER_H
#define BLOG_FILE_SYSTEM_OPEN_AWAITER_H

#include <string>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>

#include <liburing.h>

#include <async/async.h>
#include <file_system/base_file.h>

namespace fs {

/**
 * @brief Suspend until an `openat` completes via io_uring.
 *
 * Submits one `io_uring_prep_openat(AT_FDCWD, ...)` SQE and resumes the
 * coroutine with the new file descriptor on success, or an error code on
 * failure.  The path string is stored by value so it remains valid until
 * the CQE arrives.
 */
class OpenAwaiter: public async::SingleOperation<OpenAwaiter, int> {
public:
    OpenAwaiter(context_type& context, std::string path,
                BaseFile::flag flags, BaseFile::mode permissions)
      : async::SingleOperation<OpenAwaiter, int>{ context },
        path_{ std::move(path) },
        flags_{ static_cast<int>(std::to_underlying(flags)) },
        mode_{ static_cast<::mode_t>(std::to_underlying(permissions)) }
    {}

    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_openat(sqe, AT_FDCWD, path_.c_str(), flags_, mode_);
    }

    void set_result(int result, std::uint32_t /*flags*/) noexcept
    {
        fd_ = result;
    }

    auto result() noexcept -> int
    {
        return fd_;
    }

private:
    std::string path_;
    int flags_;
    ::mode_t mode_;
    int fd_{ -1 };
};

} // namespace fs

#endif // BLOG_FILE_SYSTEM_OPEN_AWAITER_H
