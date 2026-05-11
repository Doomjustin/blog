#include "stream_file.h"

#include <utility>

#include <fcntl.h>

#include <async/async.h>
#include <common/common.h>

namespace fs {
StreamFile::StreamFile(async::IOContext& context, const std::string& path, flag flags)
  : BaseFile{ context, open(path, flags) }
{}

StreamFile::StreamFile(async::IOContext& context, const std::string& path, flag flags, permission perms)
  : BaseFile{ context, open(path, flags, perms) }
{}

StreamFile::StreamFile(const std::string& path, flag flags) 
  : StreamFile{ async::this_coroutine::context(), path, flags }
{}

StreamFile::StreamFile(const std::string& path, flag flags, permission perms) 
  : StreamFile{ async::this_coroutine::context(), path, flags, perms }
{}

auto StreamFile::open(const std::string& path, flag flags, permission permissions) -> int 
{
    auto res = ::open(path.data(), std::to_underlying(flags), std::to_underlying(permissions));
    if (res == -1)
        throw_system_error("Failed to open file: {}", path);

    return res;    
}

} // namespace fs