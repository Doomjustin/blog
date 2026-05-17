#include "basic_file.h"

namespace fs {

BasicFile::BasicFile(BasicFile&& other) noexcept
  : fd_{ std::exchange(other.fd_, invalid_fd) }
{}

auto BasicFile::operator=(BasicFile&& other) noexcept -> BasicFile&
{
    if (this != &other)
        close();

    return *this;
}

void BasicFile::close()
{
    if (is_open()) {
        ::close(fd_);
        release();
    }
}

} // namespace fs