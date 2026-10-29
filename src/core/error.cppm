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

enum class io_operation {
    open,
    stat,
    read,
    map,
    seek,
    write,
    copy,
    truncate,
    close
};

enum class crypto_operation {
    import_header_key,
    calculate_header_layout,
    prepare_header_worker,
    decrypt_header,
    prepare_file_cipher,
    decrypt_file_ranges,
};

// zero means OpenSSL supplied no error code
struct crypto_error {
    crypto_operation operation;
    unsigned long code;
};

struct operation_cancelled {};

struct worker_launch_error {
    std::error_code cause;
};

struct io_error {
    io_operation operation;
    std::filesystem::path path;
    std::error_code cause;
};

struct truncated_input {
    std::size_t offset;
    std::size_t requested;
    std::size_t available;
};

struct invalid_offset {
    std::uint64_t offset;
    std::uint64_t input_size;
};

struct invalid_magic {
    std::array<std::byte, 4> actual;
};

struct invalid_header {
    std::size_t offset;
};

enum class extraction_error {
    encrypted_payload,
    invalid_encryption_range,
    invalid_source_range,
    invalid_destination,
    invalid_worker_buffer,
    invalid_group,
};

using error =
    std::variant<io_error, truncated_input, invalid_offset, invalid_magic,
                 invalid_header, crypto_error, operation_cancelled,
                 worker_launch_error, extraction_error>;

template <class T> using result = std::expected<T, error>;

} // namespace nuxe
