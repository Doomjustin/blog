#ifndef BLOG_FILE_SYSTEM_BASE_FILE_H
#define BLOG_FILE_SYSTEM_BASE_FILE_H

#include <cstdint>
#include <string>
#include <utility>

#include <fcntl.h>

#include <async/async.h>

namespace fs {

class BaseFile {
public:
    using context_type = async::IOContext;

    enum class how: std::uint8_t {
        seek_set = SEEK_SET,
        seek_cur = SEEK_CUR,
        seek_end = SEEK_END
    };

    enum class flag: std::uint32_t {
        read_only = O_RDONLY,
        write_only = O_WRONLY,
        read_write = O_RDWR,
        append = O_APPEND,
        create = O_CREAT,
        exclusive = O_EXCL,
        truncate = O_TRUNC,
        sync_all_on_write = O_SYNC
    };

    BaseFile(context_type& context = async::this_coroutine::context());

    BaseFile(const std::string& path, flag flags, context_type& context = async::this_coroutine::context());

    ~BaseFile();

    void open(const std::string& path, flag flags);

    void close();

    [[nodiscard]]
    auto context() const noexcept -> context_type& { return *context_; }

    [[nodiscard]]
    auto native_handle() const noexcept -> int { return fd_; }

private:
    static constexpr int invalid_fd = -1;

    context_type* context_;
    int fd_ = invalid_fd;

    [[nodiscard]]
    constexpr auto is_open() const noexcept -> bool 
    { 
        return fd_ != invalid_fd; 
    }
};


constexpr auto operator|(BaseFile::flag a, BaseFile::flag b) -> BaseFile::flag
{
    return static_cast<BaseFile::flag>(std::to_underlying(a) | std::to_underlying(b));
}

constexpr auto operator&(BaseFile::flag a, BaseFile::flag b) -> BaseFile::flag
{
    return static_cast<BaseFile::flag>(std::to_underlying(a) & std::to_underlying(b));
}

constexpr auto operator~(BaseFile::flag a) -> BaseFile::flag
{
    return static_cast<BaseFile::flag>(~std::to_underlying(a));
}

constexpr auto operator|=(BaseFile::flag& a, BaseFile::flag b) -> BaseFile::flag&
{
    a = a | b;
    return a;
}

constexpr auto operator&=(BaseFile::flag& a, BaseFile::flag b) -> BaseFile::flag&
{
    a = a & b;
    return a;
}

} // namespace fs


#endif // BLOG_FILE_SYSTEM_BASE_FILE_H