module;

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <fcntl.h>
#include <filesystem>
#include <limits>
#include <map>
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

namespace nuxe::detail {
fn file_error(nuxe::io_operation operation, const std::filesystem::path &path,
              int code) -> nuxe::io_error {
    return {operation, path, std::error_code{code, std::generic_category()}};
}
} // namespace nuxe::detail

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
                detail::file_error(io_operation::stat, *path_, errno));
        if (!S_ISREG(info_.st_mode) || info_.st_size < 0)
            return std::unexpected(
                detail::file_error(io_operation::stat, *path_, EINVAL));
        return {};
    }

    // completion does not imply an fsync
    fn finish() -> result<void> {
        if (descriptor_ < 0)
            return std::unexpected(
                detail::file_error(io_operation::close, *path_, EBADF));
        const let descriptor = std::exchange(descriptor_, -1);
        if (::close(descriptor) < 0)
            return std::unexpected(
                detail::file_error(io_operation::close, *path_, errno));
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
        return std::unexpected(
            detail::file_error(io_operation::open, path, errno));
    let file = payload_file{descriptor, path};
    const let inspected = file.inspect();
    if (!inspected)
        return std::unexpected(inspected.error());
    return file;
}

fn open_archive(std::filesystem::path &&) -> result<payload_file> = delete;

struct file_identity {
    dev_t device;
    ino_t inode;
};

// parents are created before workers start
fn open_output(const payload_file &input, const std::filesystem::path &path,
               overwrite_policy overwrite,
               std::span<const file_identity> protected_inputs = {})
    -> result<payload_file> {
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
        return std::unexpected(
            detail::file_error(io_operation::open, path, errno));

    let output = payload_file{descriptor, path, created};
    if (created)
        return output;
    const let inspected = output.inspect();
    if (!inspected)
        return std::unexpected(inspected.error());
    if (input.info().st_dev == output.info().st_dev &&
        input.info().st_ino == output.info().st_ino)
        return std::unexpected(extraction_error::invalid_destination);
    for (const let &identity : protected_inputs) {
        if (identity.device == output.info().st_dev &&
            identity.inode == output.info().st_ino)
            return std::unexpected(extraction_error::invalid_destination);
    }
    if (!created) {
        let status = int{};
        do {
            status = ::ftruncate(descriptor, 0);
        } while (status < 0 && errno == EINTR);
        if (status < 0)
            return std::unexpected(
                detail::file_error(io_operation::truncate, path, errno));
    }
    return output;
}

fn open_output(const payload_file &, std::filesystem::path &&, overwrite_policy)
    -> result<payload_file> = delete;

struct copy_statistics {
    std::uint64_t cfr_calls{};
    std::uint64_t cfr_bytes{};
    std::uint64_t cfr_fallbacks{};
    std::uint64_t buffered_bytes{};
    std::uint64_t cfr_ns{};
    std::map<int, std::uint64_t> cfr_errors;
};

struct unchanged_payload {
    fn operator()(std::span<std::byte>) const->result<void> { return {}; }
};

template <class Transform = unchanged_payload>
fn copy_payload(const payload_file &input, const payload_file &output,
                std::uint64_t data_offset, std::uint64_t data_length,
                std::span<std::byte> buffer, std::stop_token cancellation = {},
                Transform transform = {}, copy_statistics *statistics = nullptr)
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
        let available = std::size_t{};
        while (available < count) {
            if (cancellation.stop_requested())
                return std::unexpected(operation_cancelled{});
            const let received =
                ::pread(input.descriptor(), buffer.data() + available,
                        count - available,
                        static_cast<off_t>(data_offset + copied + available));
            if (received < 0) {
                if (errno == EINTR)
                    continue;
                return std::unexpected(detail::file_error(io_operation::read,
                                                          input.path(), errno));
            }
            if (received == 0)
                return std::unexpected(
                    detail::file_error(io_operation::read, input.path(), EIO));
            available += static_cast<std::size_t>(received);
        }
        const let transformed = transform(buffer.first(available));
        if (!transformed)
            return std::unexpected(transformed.error());
        let written = std::size_t{};
        while (written < available) {
            if (cancellation.stop_requested())
                return std::unexpected(operation_cancelled{});
            const let sent =
                ::write(output.descriptor(), buffer.data() + written,
                        available - written);
            if (sent < 0) {
                if (errno == EINTR)
                    continue;
                return std::unexpected(detail::file_error(
                    io_operation::write, output.path(), errno));
            }
            if (sent == 0)
                return std::unexpected(detail::file_error(io_operation::write,
                                                          output.path(), EIO));
            written += static_cast<std::size_t>(sent);
            if (statistics)
                statistics->buffered_bytes += static_cast<std::uint64_t>(sent);
        }
        copied += available;
    }
    return {};
}

fn copy_plain_payload(const payload_file &input, const payload_file &output,
                      std::uint64_t data_offset, std::uint64_t data_length,
                      std::span<std::byte> buffer,
                      std::stop_token cancellation = {},
                      copy_statistics *statistics = nullptr,
                      bool *cfr_unavailable = nullptr) -> result<void> {
    if (buffer.empty())
        return std::unexpected(extraction_error::invalid_worker_buffer);
    if (data_offset > input.size() || data_length > input.size() - data_offset)
        return std::unexpected(extraction_error::invalid_source_range);

    if (cfr_unavailable && *cfr_unavailable)
        return copy_payload(input, output, data_offset, data_length, buffer,
                            cancellation, unchanged_payload{}, statistics);

    constexpr std::uint64_t max_batch = 16 * 1024 * 1024;
    let copied = std::uint64_t{};
    while (copied < data_length) {
        if (cancellation.stop_requested())
            return std::unexpected(operation_cancelled{});
        const let count =
            static_cast<std::size_t>(std::min(data_length - copied, max_batch));
        let source = static_cast<off_t>(data_offset + copied);
        const let started = statistics
                                ? std::chrono::steady_clock::now()
                                : std::chrono::steady_clock::time_point{};
        const let received =
            ::copy_file_range(input.descriptor(), &source, output.descriptor(),
                              nullptr, count, 0);
        const let code = received < 0 ? errno : 0;
        if (statistics) {
            ++statistics->cfr_calls;
            statistics->cfr_ns += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - started)
                    .count());
            if (received > 0)
                statistics->cfr_bytes += static_cast<std::uint64_t>(received);
            if (code != 0)
                ++statistics->cfr_errors[code];
        }
        if (received < 0) {
            if (code == ENOSYS && cfr_unavailable)
                *cfr_unavailable = true;
            if (code == EINTR)
                continue;
            if (code == EXDEV || code == ENOSYS || code == EOPNOTSUPP ||
                code == EINVAL) {
                if (statistics)
                    ++statistics->cfr_fallbacks;
                return copy_payload(input, output, data_offset + copied,
                                    data_length - copied, buffer, cancellation,
                                    unchanged_payload{}, statistics);
            }
            return std::unexpected(
                detail::file_error(io_operation::copy, output.path(), code));
        }
        if (received == 0) {
            if (statistics)
                ++statistics->cfr_fallbacks;
            return copy_payload(input, output, data_offset + copied,
                                data_length - copied, buffer, cancellation,
                                unchanged_payload{}, statistics);
        }
        copied += static_cast<std::uint64_t>(received);
    }
    return {};
}

} // namespace nuxe
