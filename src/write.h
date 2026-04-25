#ifndef BLOG_WRITE_H
#define BLOG_WRITE_H

#include "write_all_awaiter.h"

template<typename Socket>
auto write(Socket& socket, std::span<const std::byte> view)
{
    return WriteAllAwaiter{ socket.context(), socket.native_handle(), view };
}

#endif // BLOG_WRITE_H