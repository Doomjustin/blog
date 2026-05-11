#ifndef BLOG_FILE_SYSTEM_FALLOCATE_AWAITER_H
#define BLOG_FILE_SYSTEM_FALLOCATE_AWAITER_H

#include <cstdint>
#include <utility>

#include <liburing.h>

#include <async/async.h>
#include <linux/falloc.h>

namespace fs {

/**
 * @brief Flags controlling the behaviour of `async_fallocate`.
 *
 * These mirror the `FALLOC_FL_*` constants from `<linux/falloc.h>`.
 * Flags may be combined with `operator|`; the default (`none`) performs
 * standard space pre-allocation and may extend the file size.
 */
enum class fallocate_mode: int {
    none           = 0,
    keep_size      = FALLOC_FL_KEEP_SIZE,
    punch_hole     = FALLOC_FL_PUNCH_HOLE,
    collapse_range = FALLOC_FL_COLLAPSE_RANGE,
    zero_range     = FALLOC_FL_ZERO_RANGE,
    insert_range   = FALLOC_FL_INSERT_RANGE,
    unshare_range  = FALLOC_FL_UNSHARE_RANGE,
};

constexpr auto operator|(fallocate_mode a, fallocate_mode b) noexcept -> fallocate_mode
{
    return static_cast<fallocate_mode>(std::to_underlying(a) | std::to_underlying(b));
}

constexpr auto operator&(fallocate_mode a, fallocate_mode b) noexcept -> fallocate_mode
{
    return static_cast<fallocate_mode>(std::to_underlying(a) & std::to_underlying(b));
}

constexpr auto operator~(fallocate_mode a) noexcept -> fallocate_mode
{
    return static_cast<fallocate_mode>(~std::to_underlying(a));
}

constexpr auto operator|=(fallocate_mode& a, fallocate_mode b) noexcept -> fallocate_mode&
{
    a = a | b;
    return a;
}

constexpr auto operator&=(fallocate_mode& a, fallocate_mode b) noexcept -> fallocate_mode&
{
    a = a & b;
    return a;
}

/**
 * @brief Suspend until a `fallocate` operation completes via io_uring.
 *
 * Uses `IORING_OP_FALLOCATE` to manipulate the disk space allocation for a
 * file without performing any data I/O.  The most common use cases are:
 *
 *  - Pre-allocating contiguous extents before a large sequential write
 *    (reduces fragmentation and avoids incremental allocation stalls).
 *  - Punching holes to reclaim space from a sparse file
 *    (`mode = fallocate_mode::punch_hole | fallocate_mode::keep_size`).
 *
 * On success the coroutine resumes with `std::expected<void, std::error_code>`.
 */
class FallocateAwaiter: public async::SingleOperation<FallocateAwaiter, void> {
public:
    /**
     * @param context IOContext that owns the io_uring ring.
     * @param fd      File descriptor to operate on.
     * @param mode    Allocation mode flags; use `fallocate_mode::none` for
     *                standard pre-allocation.
     * @param offset  Byte offset at which the operation starts.
     * @param len     Number of bytes affected by the operation.
     */
    FallocateAwaiter(
        context_type& context,
        int fd,
        fallocate_mode mode,
        std::uint64_t offset,
        std::uint64_t len
    ) noexcept
      : async::SingleOperation<FallocateAwaiter, void>{ context },
        fd_{ fd },
        mode_{ mode },
        offset_{ offset },
        len_{ len }
    {}

    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_fallocate(
            sqe, fd_, std::to_underlying(mode_),
            static_cast<off_t>(offset_),
            static_cast<off_t>(len_)
        );
    }

private:
    int fd_;
    fallocate_mode mode_;
    std::uint64_t offset_;
    std::uint64_t len_;
};

} // namespace fs

#endif // BLOG_FILE_SYSTEM_FALLOCATE_AWAITER_H
