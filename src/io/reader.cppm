module;

#include <nuxe/macros.h>

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <expected>
#include <span>
#include <type_traits>

export module nuxe.io.reader;

import nuxe.error;

export namespace nuxe {

class reader {
  public:
    explicit reader(std::span<const std::byte> data,
                    std::endian endian = std::endian::little) noexcept
        : data_{data}, endian_{endian} {}

    [[nodiscard]]
    fn position() const noexcept -> std::size_t {
        return position_;
    }

    [[nodiscard]]
    fn read_bytes(std::size_t count) -> result<std::span<const std::byte>> {
        const let available = data_.size() - position_;

        if (count > available) {
            return std::unexpected(
                truncated_input{position_, count, available});
        }

        const let bytes = data_.subspan(position_, count);
        position_ += count;
        return bytes;
    }

    [[nodiscard]]
    fn seek(std::size_t offset) -> result<void> {
        if (offset > data_.size()) {
            return std::unexpected(invalid_offset{offset, data_.size()});
        }

        position_ = offset;
        return {};
    }

    fn set_endian(std::endian endian) noexcept -> void { endian_ = endian; }

    template <std::integral T>
        requires(!std::same_as<T, bool>)
    [[nodiscard]]
    fn read() -> result<T> {
        return read_bytes(sizeof(T)).transform(
            [this](std::span<const std::byte> bytes) -> T {
                using bits_type = std::make_unsigned_t<T>;

                bits_type bits{};
                std::memcpy(&bits, bytes.data(), sizeof(bits));

                if (endian_ != std::endian::native) {
                    bits = std::byteswap(bits);
                }

                return std::bit_cast<T>(bits);
            });
    }

  private:
    std::span<const std::byte> data_;
    std::size_t position_{};
    std::endian endian_;
};

} // namespace nuxe
