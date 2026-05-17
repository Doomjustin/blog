#ifndef BLOG_FILE_SYSTEM_SIZE_AWAITER_H
#define BLOG_FILE_SYSTEM_SIZE_AWAITER_H

#include <chrono>
#include <cstdint>
#include <filesystem>

#include <fcntl.h>
#include <liburing.h>
#include <linux/stat.h>

#include <async/async.h>

namespace fs {

enum class FileType : std::uint8_t {
    unknown,
    regular,
    directory,
    symlink,
    block_device,
    character_device,
    fifo,
    socket
};

struct Status {
    std::uint64_t size;
    std::chrono::file_clock::time_point modify_at;
    std::chrono::file_clock::time_point access_at;
    std::chrono::file_clock::time_point create_at;
    std::filesystem::perms permissions;
    FileType type;
    std::uint64_t inode;
    std::uint64_t nlink;
    std::uint64_t uid;
    std::uint64_t gid;
};

auto type_cast(std::uint32_t type) -> FileType
{
    if ((type & STATX_TYPE) == 0)
        return FileType::unknown;

    if (S_ISREG(type))
        return FileType::regular;

    if (S_ISDIR(type))
        return FileType::directory;

    if (S_ISLNK(type))
        return FileType::symlink;

    if (S_ISBLK(type))
        return FileType::block_device;

    if (S_ISCHR(type))
        return FileType::character_device;

    if (S_ISFIFO(type))
        return FileType::fifo;

    if (S_ISSOCK(type))
        return FileType::socket;

    return FileType::unknown;
}

auto time_cast(statx_timestamp timestamp) -> std::chrono::file_clock::time_point
{
    using namespace std::chrono;

    auto sec = seconds{ timestamp.tv_sec };
    auto nsec = nanoseconds{ timestamp.tv_nsec };

    return time_point<file_clock>{ sec + nsec };
}

auto perms_cast(std::uint16_t mode) -> std::filesystem::perms
{
    std::filesystem::perms perms = std::filesystem::perms::none;

    if (mode & S_IRUSR)
        perms |= std::filesystem::perms::owner_read;
    if (mode & S_IWUSR)
        perms |= std::filesystem::perms::owner_write;
    if (mode & S_IXUSR)
        perms |= std::filesystem::perms::owner_exec;

    if (mode & S_IRGRP)
        perms |= std::filesystem::perms::group_read;
    if (mode & S_IWGRP)
        perms |= std::filesystem::perms::group_write;
    if (mode & S_IXGRP)
        perms |= std::filesystem::perms::group_exec;

    if (mode & S_IROTH)
        perms |= std::filesystem::perms::others_read;
    if (mode & S_IWOTH)
        perms |= std::filesystem::perms::others_write;
    if (mode & S_IXOTH)
        perms |= std::filesystem::perms::others_exec;

    return perms;
}

class StatusAwaiter : public async::IOAwaiter<StatusAwaiter, Status> {
private:
    int fd_;
    struct ::statx state_;

public:
    StatusAwaiter(int fd)
      : fd_{ fd }
    {}

    void prepare(::io_uring_sqe* sqe) noexcept
    {
        ::io_uring_prep_statx(sqe, fd_, "", AT_STATX_SYNC_AS_STAT | AT_EMPTY_PATH, STATX_ALL,
                              &state_);
    }

    auto value() noexcept -> Status
    {
        return {
            .size = static_cast<std::uint64_t>(state_.stx_size),
            .modify_at = time_cast(state_.stx_mtime),
            .access_at = time_cast(state_.stx_atime),
            .create_at = time_cast(state_.stx_ctime),
            .permissions = perms_cast(state_.stx_mode),
            .type = type_cast(state_.stx_mode),
            .inode = state_.stx_ino,
            .nlink = state_.stx_nlink,
            .uid = state_.stx_uid,
            .gid = state_.stx_gid,
        };
    }
};

} // namespace fs

#endif // BLOG_FILE_SYSTEM_STATUS_AWAITER_H