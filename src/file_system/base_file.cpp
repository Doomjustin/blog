#include "base_file.h"

#include <unistd.h>

#include <common/common.h>

namespace fs {

BaseFile::BaseFile(async::IOContext& context, int fd) noexcept
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

BaseFile::~BaseFile()
{
    close();
}
void BaseFile::close()
{
    if (!is_open()) return;

    ::close(fd_);
    fd_ = invalid_fd;
}

} // namespace fs