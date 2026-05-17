#include "random_access_file.h"

namespace fs {

auto RandomAccessFile::open(const std::string& path, flag flags, std::filesystem::perms perms)
    -> int
{
    auto res = ::open(path.data(), std::to_underlying(flags), std::to_underlying(perms));

    if (res == -1)
        throw_system_error("failed to open file '{}'", path);

    return res;
}

} // namespace fs