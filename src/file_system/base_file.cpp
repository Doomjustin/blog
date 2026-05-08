#include "base_file.h"

#include <unistd.h>

#include <common/common.h>

namespace fs {

BaseFile::BaseFile(async::IOContext& context)
  : context_{ &context }
{}

BaseFile::BaseFile(const std::string& path, flag flags, async::IOContext& context)
  : context_{ &context }
{
    open(path, flags);
}

BaseFile::~BaseFile()
{
    close();
}

void BaseFile::open(const std::string& path, flag flags)
{
    fd_ = ::open(path.data(), std::to_underlying(flags));
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