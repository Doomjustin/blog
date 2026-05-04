#ifndef BLOG_ASYNC_WRITE_SEQUENCE_AWAITER_H
#define BLOG_ASYNC_WRITE_SEQUENCE_AWAITER_H

#include <cstddef>

#include <liburing.h>

#include <buffer.h>
#include <single_operation.h>

namespace async {

/**
 * @brief Awaiter that performs a single gather-write (`writev`) through io_uring.
 *
 * This awaiter copies only iovec descriptors (not payload bytes) from the
 * caller-provided buffer sequence, submits one writev-style request, and
 * resumes the suspended coroutine when completion arrives.
 *
 * @tparam Buffer  Buffer sequence type satisfying `sequence_buffer`.
 */
template<sequence_buffer Buffer>
class WriteSequenceAwaiter: public SingleOperation<WriteSequenceAwaiter<Buffer>, std::size_t> {
public:
    using context_type = SingleOperation<WriteSequenceAwaiter<Buffer>, std::size_t>::context_type;

    /**
     * @brief Build iovec metadata from a sequence of immutable buffers.
     *
     * @param context Context used to submit and complete the operation.
     * @param socket  Connected socket file descriptor.
     * @param buffers Source buffer sequence written in order.
     * @pre All underlying buffer memory referenced by `buffers` must remain
     *      valid until the coroutine resumes.
     */
    WriteSequenceAwaiter(context_type& context, int socket, const Buffer& buffers)
      : SingleOperation<WriteSequenceAwaiter<Buffer>, std::size_t>{ context },
        socket_{ socket }
    {
        iov_.reserve(std::ranges::size(buffers));

        for (const auto& chunk : buffers) {
            auto data = buffer(chunk);

            iov_.push_back(::iovec{
                .iov_base = const_cast<std::byte*>(data.data()),
                .iov_len = data.size()
            });
        }
    }

    ~WriteSequenceAwaiter() = default;

    /** @brief Fill SQE as a writev request using cached iovec descriptors. */
    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_writev(sqe, socket_, iov_.data(), iov_.size(), 0);
    }

    /** @brief Store successful CQE result (called by base class). */
    void set_result(int result, std::uint32_t /*flags*/) noexcept
    {
        bytes_written_ = static_cast<std::size_t>(result);
    }

    /** @brief Return stored bytes-written count to the coroutine (called by base class). */
    auto result() noexcept -> std::size_t
    {
        return bytes_written_;
    }

private:
    int socket_;

    std::vector<::iovec> iov_;
    std::size_t bytes_written_{ 0 };
};

} // namespace async

#endif // BLOG_ASYNC_WRITE_SEQUENCE_AWAITER_H