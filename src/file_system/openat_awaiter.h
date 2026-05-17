#ifndef BLOG_FILE_SYSTEM_OPENAT_AWAITER_H
#define BLOG_FILE_SYSTEM_OPENAT_AWAITER_H

#include <async/async.h>

namespace fs {

/// @brief 基于 io_uring 的 openat awaiter，异步打开文件并返回 StreamFile。
/// @tparam File 文件类型，需满足 BasicFile（如 StreamFile）。且能通过 File{ fd } 构造函数接管 fd。
template<typename File>
class OpenatAwaiter : public async::IOAwaiter<OpenatAwaiter<File>, File> {
private:
    std::string file_name_;
    int flags_;
    mode_t mode_{ 0 };

public:
    OpenatAwaiter(std::string file_name, int flags, mode_t mode = 0)
      : file_name_{ std::move(file_name) }
      , flags_{ flags }
      , mode_{ mode }
    {}

    /// @brief 准备 io_uring SQE，提交 openat 操作。
    void prepare(::io_uring_sqe* sqe) const noexcept
    {
        ::io_uring_prep_openat(sqe, AT_FDCWD, file_name_.c_str(), flags_, mode_);
    }

    auto value() noexcept -> File
    {
        return File{ static_cast<int>(this->result) };
    }
};

} // namespace fs

#endif // BLOG_FILE_SYSTEM_OPENAT_AWAITER_H
