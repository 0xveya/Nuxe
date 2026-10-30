module;

#include <algorithm>
#include <cstddef>
#include <expected>
#include <limits>
#include <memory>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <span>
#include <utility>

#include <nuxe/macros.h>

export module nuxe.crypto.file;

import nuxe.error;

export namespace nuxe {

// AES-128-ECB without padding
class file_cipher {
  public:
    static fn create(std::span<const std::byte, 16> key) -> result<file_cipher>;
    file_cipher(file_cipher &&) noexcept;
    fn operator=(file_cipher &&) noexcept -> file_cipher &;
    ~file_cipher();

    file_cipher(const file_cipher &) = delete;
    fn operator=(const file_cipher &)->file_cipher & = delete;

    fn decrypt_blocks(std::span<std::byte> blocks) -> result<void>;
    fn rekey(std::span<const std::byte, 16> key) -> result<void>;

  private:
    struct state;
    std::unique_ptr<state> state_;
    explicit file_cipher(std::unique_ptr<state> state) noexcept;
};

} // namespace nuxe

namespace nuxe {

struct file_cipher::state {
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> context{
        EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free};
};

file_cipher::file_cipher(std::unique_ptr<state> state) noexcept
    : state_(std::move(state)) {}

file_cipher::file_cipher(file_cipher &&) noexcept = default;

fn file_cipher::operator=(file_cipher &&) noexcept -> file_cipher & = default;

file_cipher::~file_cipher() = default;

fn file_cipher::create(std::span<const std::byte, 16> key)
    -> result<file_cipher> {
    ERR_clear_error();

    const let failure = [] {
        return std::unexpected(crypto_error{
            crypto_operation::prepare_file_cipher,
            ERR_get_error(),
        });
    };

    let prepared = std::make_unique<state>();
    if (!prepared->context)
        return failure();

    if (EVP_DecryptInit_ex2(prepared->context.get(), EVP_aes_128_ecb(),
                            reinterpret_cast<const unsigned char *>(key.data()),
                            nullptr, nullptr) != 1)
        return failure();

    if (EVP_CIPHER_CTX_set_padding(prepared->context.get(), 0) != 1)
        return failure();

    return file_cipher{std::move(prepared)};
}

fn file_cipher::rekey(std::span<const std::byte, 16> key) -> result<void> {
    ERR_clear_error();

    const let failure = [] {
        return std::unexpected(crypto_error{
            crypto_operation::prepare_file_cipher,
            ERR_get_error(),
        });
    };

    if (!state_)
        return failure();
    if (EVP_DecryptInit_ex2(state_->context.get(), nullptr,
                            reinterpret_cast<const unsigned char *>(key.data()),
                            nullptr, nullptr) != 1)
        return failure();
    if (EVP_CIPHER_CTX_set_padding(state_->context.get(), 0) != 1)
        return failure();
    return {};
}

fn file_cipher::decrypt_blocks(std::span<std::byte> blocks) -> result<void> {
    ERR_clear_error();

    const let failure = [] {
        return std::unexpected(crypto_error{
            crypto_operation::decrypt_file_ranges,
            ERR_get_error(),
        });
    };

    constexpr std::size_t block_size = 16;

    if (!state_ || blocks.size() % block_size != 0)
        return failure();

    constexpr let max_batch =
        static_cast<std::size_t>(std::numeric_limits<int>::max()) / block_size *
        block_size;

    let offset = std::size_t{};
    while (offset < blocks.size()) {
        const let count = std::min(max_batch, blocks.size() - offset);
        let *data = reinterpret_cast<unsigned char *>(blocks.data() + offset);
        let written = int{};

        if (EVP_DecryptUpdate(state_->context.get(), data, &written, data,
                              static_cast<int>(count)) != 1)
            return failure();

        if (written != static_cast<int>(count))
            return failure();

        offset += count;
    }

    return {};
}

} // namespace nuxe
