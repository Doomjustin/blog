#include "stream_file.h"

#include <utility>

#include <common/common.h>

namespace fs {

auto StreamFile::open(const std::string& path, flag flags, permission perms) -> int
{
    auto res = ::open(path.data(), std::to_underlying(flags), std::to_underlying(perms));

    if (res == -1)
        throw_system_error("failed to open file '{}'", path);

    return res;
}

} // namespace fs