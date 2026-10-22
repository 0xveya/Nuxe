module;

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <fcntl.h>
#include <filesystem>
#include <limits>
#include <span>
#include <stop_token>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <utility>

#include <nuxe/macros.h>

export module nuxe.io.payload;

import nuxe.error;
import nuxe.extract.plan;

namespace {
fn file_error(nuxe::io_operation operation, const std::filesystem::path &path,
              int code) -> nuxe::io_error {
    return {operation, path, std::error_code{code, std::generic_category()}};
}
} // namespace

export namespace nuxe {

// new outputs are removed unless finish succeeds
class payload_file {
  public:
    payload_file(int descriptor, const std::filesystem::path &path,
                 bool remove_on_failure = false) noexcept
        : descriptor_(descriptor), path_(&path),
          remove_on_failure_(remove_on_failure) {}
    payload_file(int, std::filesystem::path &&, bool = false) = delete;
    payload_file(const payload_file &) = delete;
    fn operator=(const payload_file &)->payload_file & = delete;
    payload_file(payload_file &&other) noexcept
        : descriptor_(std::exchange(other.descriptor_, -1)), path_(other.path_),
          info_(other.info_),
          remove_on_failure_(std::exchange(other.remove_on_failure_, false)) {}
    fn operator=(payload_file &&other) noexcept -> payload_file & {
        if (this != &other) {
            cleanup();
            descriptor_ = std::exchange(other.descriptor_, -1);
            path_ = other.path_;
            info_ = other.info_;
            remove_on_failure_ = std::exchange(other.remove_on_failure_, false);
        }
        return *this;
    }
    ~payload_file() { cleanup(); }

    fn descriptor() const noexcept -> int { return descriptor_; }
    fn path() const noexcept -> const std::filesystem::path & { return *path_; }
    fn info() const noexcept -> const struct stat & { return info_; }
    fn size() const noexcept -> std::uint64_t {
        return static_cast<std::uint64_t>(info_.st_size);
    }

    fn inspect() -> result<void> {
        let status = int{};
        do {
            status = ::fstat(descriptor_, &info_);
        } while (status < 0 && errno == EINTR);
        if (status < 0)
            return std::unexpected(
                file_error(io_operation::stat, *path_, errno));
        if (!S_ISREG(info_.st_mode) || info_.st_size < 0)
            return std::unexpected(
                file_error(io_operation::stat, *path_, EINVAL));
        return {};
    }

    // completion does not imply an fsync
    fn finish() -> result<void> {
        if (descriptor_ < 0)
            return std::unexpected(
                file_error(io_operation::close, *path_, EBADF));
        const let descriptor = std::exchange(descriptor_, -1);
        if (::close(descriptor) < 0)
            return std::unexpected(
                file_error(io_operation::close, *path_, errno));
        remove_on_failure_ = false;
        return {};
    }

  private:
    fn cleanup() noexcept -> void {
        if (descriptor_ >= 0)
            ::close(std::exchange(descriptor_, -1));
        if (remove_on_failure_) {
            ::unlink(path_->c_str());
            remove_on_failure_ = false;
        }
    }
    int descriptor_;
    // borrow the plan path to avoid per-file copies
    const std::filesystem::path *path_;
    struct stat info_{};
    bool remove_on_failure_;
};

fn open_archive(const std::filesystem::path &path) -> result<payload_file> {
    let descriptor = int{};
    do {
        descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0)
        return std::unexpected(file_error(io_operation::open, path, errno));
    let file = payload_file{descriptor, path};
    const let inspected = file.inspect();
    if (!inspected)
        return std::unexpected(inspected.error());
    return file;
}

fn open_archive(std::filesystem::path &&) -> result<payload_file> = delete;

// parents are created before workers start
fn open_output(const payload_file &input, const std::filesystem::path &path,
               overwrite_policy overwrite) -> result<payload_file> {
    constexpr int flags = O_WRONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK;
    let descriptor = int{};
    do {
        descriptor = ::open(path.c_str(), flags | O_CREAT | O_EXCL, 0666);
    } while (descriptor < 0 && errno == EINTR);
    const let created = descriptor >= 0;
    if (descriptor < 0 && errno == EEXIST &&
        overwrite == overwrite_policy::replace) {
        do {
            descriptor = ::open(path.c_str(), flags);
        } while (descriptor < 0 && errno == EINTR);
    }
    if (descriptor < 0)
        return std::unexpected(file_error(io_operation::open, path, errno));

    let output = payload_file{descriptor, path, created};
    const let inspected = output.inspect();
    if (!inspected)
        return std::unexpected(inspected.error());
    if (input.info().st_dev == output.info().st_dev &&
        input.info().st_ino == output.info().st_ino)
        return std::unexpected(extraction_error::invalid_destination);
    if (!created) {
        let status = int{};
        do {
            status = ::ftruncate(descriptor, 0);
        } while (status < 0 && errno == EINTR);
        if (status < 0)
            return std::unexpected(
                file_error(io_operation::truncate, path, errno));
    }
    return output;
}

fn open_output(const payload_file &, std::filesystem::path &&, overwrite_policy)
    -> result<payload_file> = delete;

fn copy_payload(const payload_file &input, const payload_file &output,
                std::uint64_t data_offset, std::uint64_t data_length,
                std::span<std::byte> buffer, std::stop_token cancellation = {})
    -> result<void> {
    if (buffer.empty())
        return std::unexpected(extraction_error::invalid_worker_buffer);
    if (data_offset > input.size() || data_length > input.size() - data_offset)
        return std::unexpected(extraction_error::invalid_source_range);

    let copied = std::uint64_t{};
    while (copied < data_length) {
        if (cancellation.stop_requested())
            return std::unexpected(operation_cancelled{});
        const let count = static_cast<std::size_t>(std::min({
            data_length - copied,
            static_cast<std::uint64_t>(buffer.size()),
            static_cast<std::uint64_t>(std::numeric_limits<ssize_t>::max()),
        }));
        const let received = ::pread(input.descriptor(), buffer.data(), count,
                                     static_cast<off_t>(data_offset + copied));
        if (received < 0) {
            if (errno == EINTR)
                continue;
            return std::unexpected(
                file_error(io_operation::read, input.path(), errno));
        }
        if (received == 0)
            return std::unexpected(
                file_error(io_operation::read, input.path(), EIO));

        let written = std::size_t{};
        const let available = static_cast<std::size_t>(received);
        while (written < available) {
            if (cancellation.stop_requested())
                return std::unexpected(operation_cancelled{});
            const let sent =
                ::write(output.descriptor(), buffer.data() + written,
                        available - written);
            if (sent < 0) {
                if (errno == EINTR)
                    continue;
                return std::unexpected(
                    file_error(io_operation::write, output.path(), errno));
            }
            if (sent == 0)
                return std::unexpected(
                    file_error(io_operation::write, output.path(), EIO));
            written += static_cast<std::size_t>(sent);
        }
        copied += static_cast<std::uint64_t>(received);
    }
    return {};
}

} // namespace nuxe
