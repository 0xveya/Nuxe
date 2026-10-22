module;

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <nuxe/macros.h>
#include <span>
#include <string>

export module nuxe.format.bhd5.header;

import nuxe.error;
import nuxe.io.reader;

export namespace nuxe {

enum class bhd5_format {
    dark_souls,
    dark_souls_remastered,
    dark_souls2,
    dark_souls3,
    elden_ring,
};

struct bhd5_header {
    bool little_endian;
    bool unk05;
    std::int32_t bucket_count;
    std::int64_t buckets_offset;
    std::string data_salt;
};

template <bhd5_format Format>
fn parse_bhd5_header(std::span<const std::byte> bytes) -> result<bhd5_header> {
    // i dont worry abt optmizations yet in future i shoudbt be spamming read
    // syscall as much tho but first i want sum to work future veya can worry
    // about makeing it not dogshit slow
    let input = reader(bytes);
    let header = bhd5_header{};

    const let magic = input.read_bytes(4);
    if (!magic)
        return std::unexpected(magic.error());

    const let actual = std::array{
        (*magic)[0],
        (*magic)[1],
        (*magic)[2],
        (*magic)[3],
    };
    const let expected = std::array{
        std::byte{'B'},
        std::byte{'H'},
        std::byte{'D'},
        std::byte{'5'},
    };
    if (actual != expected)
        return std::unexpected(invalid_magic{actual});

    const let endian = input.read<std::uint8_t>();
    if (!endian)
        return std::unexpected(endian.error());

    if (*endian != 0 && *endian != 0xff)
        return std::unexpected(invalid_header{4});

    header.little_endian = *endian != 0;
    input.set_endian(header.little_endian ? std::endian::little
                                          : std::endian::big);

    const let flag = input.read<std::uint8_t>();
    if (!flag)
        return std::unexpected(flag.error());

    if (*flag > 1)
        return std::unexpected(invalid_header{5});

    header.unk05 = *flag != 0;

    const let reserved = input.read<std::uint16_t>();
    if (!reserved)
        return std::unexpected(reserved.error());
    if (*reserved != 0)
        return std::unexpected(invalid_header{6});

    const let version = input.read<std::int32_t>();
    if (!version)
        return std::unexpected(version.error());
    if (*version != 1)
        return std::unexpected(invalid_header{8});

    const let ignored = input.read<std::int32_t>();
    if (!ignored)
        return std::unexpected(ignored.error());

    const let count = input.read<std::int32_t>();
    if (!count)
        return std::unexpected(count.error());
    if (*count < 0)
        return std::unexpected(invalid_header{16});
    header.bucket_count = *count;

    if constexpr (Format == bhd5_format::dark_souls_remastered) {
        const let padding = input.read<std::int32_t>();
        if (!padding)
            return std::unexpected(padding.error());
        if (*padding != 0)
            return std::unexpected(invalid_header{20});

        const let offset = input.read<std::int64_t>();
        if (!offset)
            return std::unexpected(offset.error());
        if (*offset < 0)
            return std::unexpected(invalid_header{24});
        header.buckets_offset = *offset;
    } else {
        const let offset = input.read<std::int32_t>();
        if (!offset)
            return std::unexpected(offset.error());
        if (*offset < 0)
            return std::unexpected(invalid_header{20});
        header.buckets_offset = *offset;
    }

    const let buckets_offset =
        static_cast<std::uint64_t>(header.buckets_offset);
    if (buckets_offset > bytes.size())
        return std::unexpected(invalid_offset{buckets_offset, bytes.size()});

    if constexpr (Format == bhd5_format::dark_souls2 ||
                  Format == bhd5_format::dark_souls3 ||
                  Format == bhd5_format::elden_ring) {
        const let length_offset = input.position();
        const let length = input.read<std::int32_t>();
        if (!length)
            return std::unexpected(length.error());
        if (*length < 0)
            return std::unexpected(invalid_header{length_offset});

        const let salt = input.read_bytes(static_cast<std::size_t>(*length));
        if (!salt)
            return std::unexpected(salt.error());

        header.data_salt.assign(reinterpret_cast<const char *>(salt->data()),
                                salt->size());
    }

    return header;
}

} // namespace nuxe
