#include "stream_file.h"

#include <utility>

#include <fcntl.h>
#include <unistd.h>

#include <common/common.h>

namespace fs {

auto std_input() -> StreamFile
{
    return StreamFile{ STDIN_FILENO, false };
}

auto std_output() -> StreamFile
{
    return StreamFile{ STDOUT_FILENO, false };
}

auto std_error() -> StreamFile
{
    return StreamFile{ STDERR_FILENO, false };
}

auto pipe() -> std::pair<StreamFile, StreamFile>
{
    std::array<int, 2> fds;
    if (::pipe2(fds.data(), O_CLOEXEC) == -1)
        throw_system_error("failed to create pipe");

    return { StreamFile{ fds[0] }, StreamFile{ fds[1] } };
}

} // namespace fs