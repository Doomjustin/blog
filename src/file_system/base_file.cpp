#include "base_file.h"

#include <unistd.h>

#include <common/common.h>

namespace fs {

BaseFile::BaseFile(async::IOContext& context)
  : context_{ &context }
{}

BaseFile::BaseFile(int fd, async::IOContext& context) noexcept
  : context_{ &context },
    fd_{ fd }
{}

BaseFile::BaseFile(BaseFile&& other) noexcept
  : context_{ other.context_ },
    fd_{ std::exchange(other.fd_, invalid_fd) }
{}

auto BaseFile::operator=(BaseFile&& other) noexcept -> BaseFile&
{
    if (this != &other) {
        close();
        context_ = other.context_;
        fd_      = std::exchange(other.fd_, invalid_fd);
    }
    return *this;
}

BaseFile::BaseFile(const std::string& path, flag flags, mode permissions, async::IOContext& context)
  : context_{ &context }
{
    open(path, flags, permissions);
}

BaseFile::~BaseFile()
{
    close();
}

void BaseFile::open(const std::string& path, flag flags, mode permissions)
{
    fd_ = ::open(path.data(), std::to_underlying(flags), std::to_underlying(permissions));
    if (fd_ == invalid_fd)
        throw_system_error("Failed to open file '{}'", path);
}

void BaseFile::close()
{
    if (!is_open()) return;

    ::close(fd_);
    fd_ = invalid_fd;
}

} // namespace fs