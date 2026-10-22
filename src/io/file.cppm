module;

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <fcntl.h>
#include <filesystem>
#include <limits>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <vector>

#include <nuxe/macros.h>

export module nuxe.io.file;

import nuxe.error;

namespace {
struct input_descriptor {
    int value;
    ~input_descriptor() { ::close(value); }
    input_descriptor(const input_descriptor &) = delete;
    fn operator=(const input_descriptor &)->input_descriptor & = delete;
    explicit input_descriptor(int descriptor) : value(descriptor) {}
};
} // namespace

export namespace nuxe {

fn read_file(const std::filesystem::path &path)
    -> result<std::vector<std::byte>> {
    const let failure = [&](io_operation operation, int code) {
        return std::unexpected(io_error{
            operation,
            path,
            std::error_code{code, std::generic_category()},
        });
    };

    let descriptor = int{};
    do {
        descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0)
        return failure(io_operation::open, errno);
    const let file = input_descriptor{descriptor};

    struct stat info{};
    let status = int{};
    do {
        status = ::fstat(file.value, &info);
    } while (status < 0 && errno == EINTR);
    if (status < 0)
        return failure(io_operation::stat, errno);
    if (!S_ISREG(info.st_mode))
        return failure(io_operation::stat,
                       S_ISDIR(info.st_mode) ? EISDIR : EINVAL);

    let bytes = std::vector<std::byte>{};
    if (info.st_size < 0 ||
        static_cast<std::uintmax_t>(info.st_size) > bytes.max_size())
        return failure(io_operation::stat, EFBIG);
    bytes.resize(static_cast<std::size_t>(info.st_size));

    let offset = std::size_t{};
    while (offset < bytes.size()) {
        const let count = std::min(
            bytes.size() - offset,
            static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
        const let received = ::read(file.value, bytes.data() + offset, count);
        if (received < 0) {
            if (errno == EINTR)
                continue;
            return failure(io_operation::read, errno);
        }
        if (received == 0)
            return failure(io_operation::read, EIO);
        offset += static_cast<std::size_t>(received);
    }
    return bytes;
}

} // namespace nuxe
