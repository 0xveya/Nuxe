module;

#include <cstddef>
#include <memory>
#include <nuxe/macros.h>
#include <span>
#include <stop_token>
#include <string_view>

export module nuxe.crypto.header;

import nuxe.error;

export namespace nuxe {

/// Validated sizes used to allocate final plaintext before starting workers.
struct header_layout {
    std::size_t input_block_bytes;
    std::size_t output_block_bytes;
    std::size_t block_count;
    std::size_t output_bytes;
};

/// Owns an imported and validated RSA public key.
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

/// Owns reusable RSA arithmetic state for one worker, independent of the key
/// owner.
class header_worker {
  public:
    static fn create(const header_key &key) -> result<header_worker>;
    header_worker(header_worker &&) noexcept;
    fn operator=(header_worker &&) noexcept -> header_worker &;
    ~header_worker();

    header_worker(const header_worker &) = delete;
    fn operator=(const header_worker &)->header_worker & = delete;

    /// Processes a batch into its exclusive final output span without resizing.
    fn decrypt_blocks(std::span<const std::byte> encrypted,
                      std::span<std::byte> plaintext) -> result<void>;

  private:
    struct state;
    std::unique_ptr<state> state_;
    explicit header_worker(std::unique_ptr<state> state) noexcept;
};

/// Claims RSA batches atomically and joins workers before returning plaintext.
fn decrypt_header(std::span<const std::byte> encrypted,
                  std::span<std::byte> plaintext,
                  std::span<header_worker> workers,
                  std::size_t blocks_per_batch,
                  std::stop_token cancellation = {}) -> result<void>;

} // namespace nuxe
