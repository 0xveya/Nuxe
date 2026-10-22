module;

#include <cstddef>
#include <memory>
#include <nuxe/macros.h>
#include <span>

export module nuxe.crypto.file;

import nuxe.error;

export namespace nuxe {

/// Owns reusable AES-128-ECB state for one key on one worker.
class file_cipher {
  public:
    static fn create(std::span<const std::byte, 16> key) -> result<file_cipher>;
    file_cipher(file_cipher &&) noexcept;
    fn operator=(file_cipher &&) noexcept -> file_cipher &;
    ~file_cipher();

    file_cipher(const file_cipher &) = delete;
    fn operator=(const file_cipher &)->file_cipher & = delete;

    /// Decrypts validated complete AES blocks using prepared private state.
    fn decrypt_blocks(std::span<std::byte> blocks) -> result<void>;

  private:
    struct state;
    std::unique_ptr<state> state_;
    explicit file_cipher(std::unique_ptr<state> state) noexcept;
};

} // namespace nuxe
