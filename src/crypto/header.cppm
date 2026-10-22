module;

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <expected>
#include <functional>
#include <limits>
#include <memory>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <span>
#include <stop_token>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <nuxe/macros.h>

export module nuxe.crypto.header;

import nuxe.error;

export namespace nuxe {

struct header_layout {
    std::size_t input_block_bytes;
    std::size_t output_block_bytes;
    std::size_t block_count;
    std::size_t output_bytes;
};

class header_key {
  public:
    static fn parse(std::string_view pem) -> result<header_key>;
    fn layout(std::size_t encrypted_bytes) const -> result<header_layout>;
    header_key(header_key &&) noexcept;
    fn operator=(header_key &&) noexcept -> header_key &;
    ~header_key();

    header_key(const header_key &) = delete;
    fn operator=(const header_key &)->header_key & = delete;

  private:
    friend class header_worker;
    struct state;
    std::unique_ptr<state> state_;
    explicit header_key(std::unique_ptr<state> state) noexcept;
};

class header_worker {
  public:
    static fn create(const header_key &key) -> result<header_worker>;
    header_worker(header_worker &&) noexcept;
    fn operator=(header_worker &&) noexcept -> header_worker &;
    ~header_worker();

    header_worker(const header_worker &) = delete;
    fn operator=(const header_worker &)->header_worker & = delete;

    fn decrypt_blocks(std::span<const std::byte> encrypted,
                      std::span<std::byte> plaintext) -> result<void>;

  private:
    struct state;
    std::unique_ptr<state> state_;
    explicit header_worker(std::unique_ptr<state> state) noexcept;

    friend fn decrypt_header(std::span<const std::byte> encrypted,
                             std::span<std::byte> plaintext,
                             std::span<header_worker> workers,
                             std::size_t blocks_per_batch,
                             std::stop_token cancellation) -> result<void>;
};

fn decrypt_header(std::span<const std::byte> encrypted,
                  std::span<std::byte> plaintext,
                  std::span<header_worker> workers,
                  std::size_t blocks_per_batch,
                  std::stop_token cancellation = {}) -> result<void>;

} // namespace nuxe

namespace nuxe {
using public_key_ptr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using big_number_ptr = std::unique_ptr<BIGNUM, decltype(&BN_free)>;
using number_context_ptr = std::unique_ptr<BN_CTX, decltype(&BN_CTX_free)>;
using montgomery_ptr =
    std::unique_ptr<BN_MONT_CTX, decltype(&BN_MONT_CTX_free)>;

struct header_key::state {
    public_key_ptr key;
};

struct header_worker::state {
    big_number_ptr modulus{nullptr, BN_free};
    big_number_ptr exponent{nullptr, BN_free};
    big_number_ptr input{nullptr, BN_free};
    big_number_ptr output{nullptr, BN_free};
    number_context_ptr context{nullptr, BN_CTX_free};
    montgomery_ptr montgomery{nullptr, BN_MONT_CTX_free};

    std::size_t input_block_bytes{};
    std::size_t output_block_bytes{};
};

header_worker::header_worker(std::unique_ptr<state> state) noexcept
    : state_(std::move(state)) {}

header_worker::header_worker(header_worker &&) noexcept = default;

fn header_worker::operator=(header_worker &&) noexcept
    -> header_worker & = default;

header_worker::~header_worker() = default;

header_key::header_key(std::unique_ptr<state> state) noexcept
    : state_(std::move(state)) {}

header_key::header_key(header_key &&) noexcept = default;
fn header_key::operator=(header_key &&) noexcept -> header_key & = default;
header_key::~header_key() = default;

fn header_key::parse(std::string_view pem) -> result<header_key> {
    ERR_clear_error();

    const let failure = [] {
        return std::unexpected(crypto_error{
            crypto_operation::import_header_key,
            ERR_get_error(),
        });
    };

    if (pem.empty() ||
        pem.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return failure();

    const std::unique_ptr<BIO, decltype(&BIO_free)> input{
        BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())),
        BIO_free,
    };
    if (!input)
        return failure();

    public_key_ptr key{
        PEM_read_bio_PUBKEY(input.get(), nullptr, nullptr, nullptr),
        EVP_PKEY_free,
    };
    if (!key)
        return failure();

    if (EVP_PKEY_is_a(key.get(), "RSA") != 1)
        return failure();

    const std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> context{
        EVP_PKEY_CTX_new_from_pkey(nullptr, key.get(), nullptr),
        EVP_PKEY_CTX_free,
    };
    if (!context || EVP_PKEY_public_check(context.get()) != 1)
        return failure();

    return header_key{
        std::make_unique<state>(std::move(key)),
    };
}

fn header_key::layout(std::size_t encrypted_bytes) const
    -> result<header_layout> {
    const let failure = [] {
        return std::unexpected(crypto_error{
            crypto_operation::calculate_header_layout,
            0,
        });
    };

    if (!state_)
        return failure();

    const int key_bytes = EVP_PKEY_get_size(state_->key.get());
    if (key_bytes <= 1)
        return failure();

    const let input_block_bytes = static_cast<std::size_t>(key_bytes);
    const let output_block_bytes = input_block_bytes - 1;

    if (encrypted_bytes % input_block_bytes != 0)
        return failure();

    const let block_count = encrypted_bytes / input_block_bytes;

    if (block_count >
        std::numeric_limits<std::size_t>::max() / output_block_bytes)
        return failure();

    return header_layout{
        .input_block_bytes = input_block_bytes,
        .output_block_bytes = output_block_bytes,
        .block_count = block_count,
        .output_bytes = block_count * output_block_bytes,
    };
}

fn header_worker::create(const header_key &key) -> result<header_worker> {
    ERR_clear_error();

    const let failure = [] {
        return std::unexpected(crypto_error{
            crypto_operation::prepare_header_worker,
            ERR_get_error(),
        });
    };

    const let layout = key.layout(0);
    if (!layout)
        return std::unexpected(layout.error());

    let prepared = std::make_unique<state>();

    BIGNUM *modulus = nullptr;
    const int got_modulus = EVP_PKEY_get_bn_param(
        key.state_->key.get(), OSSL_PKEY_PARAM_RSA_N, &modulus);
    prepared->modulus.reset(modulus);
    if (got_modulus != 1)
        return failure();

    BIGNUM *exponent = nullptr;
    const int got_exponent = EVP_PKEY_get_bn_param(
        key.state_->key.get(), OSSL_PKEY_PARAM_RSA_E, &exponent);
    prepared->exponent.reset(exponent);
    if (got_exponent != 1)
        return failure();

    prepared->input.reset(BN_new());
    prepared->output.reset(BN_new());
    prepared->context.reset(BN_CTX_new());
    prepared->montgomery.reset(BN_MONT_CTX_new());
    if (!prepared->input || !prepared->output || !prepared->context ||
        !prepared->montgomery)
        return failure();

    if (BN_MONT_CTX_set(prepared->montgomery.get(), prepared->modulus.get(),
                        prepared->context.get()) != 1)
        return failure();

    prepared->input_block_bytes = layout->input_block_bytes;
    prepared->output_block_bytes = layout->output_block_bytes;
    return header_worker{std::move(prepared)};
}

fn header_worker::decrypt_blocks(std::span<const std::byte> encrypted,
                                 std::span<std::byte> plaintext)
    -> result<void> {
    ERR_clear_error();

    const let failure = [] {
        return std::unexpected(crypto_error{
            crypto_operation::decrypt_header,
            ERR_get_error(),
        });
    };

    if (!state_)
        return failure();

    let &worker = *state_;
    const let input_bytes = worker.input_block_bytes;
    const let output_bytes = worker.output_block_bytes;

    if (encrypted.size() % input_bytes != 0 ||
        plaintext.size() % output_bytes != 0)
        return failure();

    const let block_count = encrypted.size() / input_bytes;
    if (plaintext.size() / output_bytes != block_count)
        return failure();

    for (std::size_t index = 0; index < block_count; ++index) {
        const let input = encrypted.subspan(index * input_bytes, input_bytes);
        const let output =
            plaintext.subspan(index * output_bytes, output_bytes);

        if (!BN_bin2bn(reinterpret_cast<const unsigned char *>(input.data()),
                       static_cast<int>(input_bytes), worker.input.get()))
            return failure();

        if (BN_mod_exp_mont(worker.output.get(), worker.input.get(),
                            worker.exponent.get(), worker.modulus.get(),
                            worker.context.get(), worker.montgomery.get()) != 1)
            return failure();

        if (BN_bn2binpad(worker.output.get(),
                         reinterpret_cast<unsigned char *>(output.data()),
                         static_cast<int>(output_bytes)) !=
            static_cast<int>(output_bytes))
            return failure();
    }

    return {};
}

fn decrypt_header(std::span<const std::byte> encrypted,
                  std::span<std::byte> plaintext,
                  std::span<header_worker> workers,
                  std::size_t blocks_per_batch, std::stop_token cancellation)
    -> result<void> {
    const let invalid = [] {
        return std::unexpected(crypto_error{
            crypto_operation::decrypt_header,
            0,
        });
    };

    if (workers.empty() || blocks_per_batch == 0)
        return invalid();

    const let *first = workers.front().state_.get();
    if (!first)
        return invalid();

    const let input_bytes = first->input_block_bytes;
    const let output_bytes = first->output_block_bytes;
    if (encrypted.size() % input_bytes != 0 ||
        plaintext.size() % output_bytes != 0)
        return invalid();

    const let block_count = encrypted.size() / input_bytes;
    if (plaintext.size() / output_bytes != block_count)
        return invalid();

    for (const let &worker : workers) {
        const let *state = worker.state_.get();
        if (!state || state->input_block_bytes != input_bytes ||
            state->output_block_bytes != output_bytes ||
            BN_cmp(first->modulus.get(), state->modulus.get()) != 0 ||
            BN_cmp(first->exponent.get(), state->exponent.get()) != 0)
            return invalid();
    }

    if (!encrypted.empty()) {
        const let before = std::less<const std::byte *>{};
        if (before(encrypted.data(), plaintext.data() + plaintext.size()) &&
            before(plaintext.data(), encrypted.data() + encrypted.size()))
            return invalid();
    }

    if (cancellation.stop_requested())
        return std::unexpected(operation_cancelled{});
    if (block_count == 0)
        return {};

    const let batch_blocks = std::min(blocks_per_batch, block_count);
    const let batch_count = 1 + (block_count - 1) / batch_blocks;
    const let worker_count = std::min(workers.size(), batch_count);
    let next_batch = std::atomic<std::size_t>{0};
    let failed = std::atomic<bool>{false};

    const let run = [&](header_worker &worker) -> result<void> {
        while (!failed.load(std::memory_order_relaxed)) {
            if (cancellation.stop_requested())
                return std::unexpected(operation_cancelled{});

            const let batch =
                next_batch.fetch_add(1, std::memory_order_relaxed);
            if (batch >= batch_count)
                break;
            const let first_block = batch * batch_blocks;
            const let count = std::min(batch_blocks, block_count - first_block);
            const let decrypted = worker.decrypt_blocks(
                encrypted.subspan(first_block * input_bytes,
                                  count * input_bytes),
                plaintext.subspan(first_block * output_bytes,
                                  count * output_bytes));
            if (!decrypted) {
                failed.store(true, std::memory_order_relaxed);
                return std::unexpected(decrypted.error());
            }
        }
        return {};
    };

    if (worker_count == 1)
        return run(workers.front());

    let results = std::vector<result<void>>(worker_count);
    {
        let threads = std::vector<std::jthread>(worker_count - 1);
        try {
            for (std::size_t index = 1; index < worker_count; ++index) {
                threads[index - 1] = std::jthread(
                    [&, index] { results[index] = run(workers[index]); });
            }
        } catch (const std::system_error &error) {
            failed.store(true, std::memory_order_relaxed);
            return std::unexpected(worker_launch_error{error.code()});
        }
        results[0] = run(workers.front());
    }

    for (const let &result : results) {
        if (!result)
            return std::unexpected(result.error());
    }
    return {};
}

} // namespace nuxe
