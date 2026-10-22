module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <system_error>
#include <variant>

export module nuxe.error;

export namespace nuxe {

enum class io_operation { open, stat, read, map };

/// A filesystem failure with its original system error.
struct io_error {
    io_operation operation;
    std::filesystem::path path;
    std::error_code cause;
};

/// A read that needs more bytes than remain in the input.
struct truncated_input {
    std::size_t offset;
    std::size_t requested;
    std::size_t available;
};

/// An archive offset outside the input's bounds.
struct invalid_offset {
    std::uint64_t offset;
    std::uint64_t input_size;
};

/// An unexpected four-byte archive signature.
struct invalid_magic {
    std::array<std::byte, 4> actual;
};

/// An invalid value in an archive header or metadata record.
struct invalid_header {
    std::size_t offset;
};

using error = std::variant<io_error, truncated_input, invalid_offset,
                           invalid_magic, invalid_header>;

/// A value or a typed failure, including operations with no return value.
template <class T> using result = std::expected<T, error>;

} // namespace nuxe
