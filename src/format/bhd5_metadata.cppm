module;

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <nuxe/macros.h>
#include <span>
#include <vector>

export module nuxe.format.bhd5.metadata;

import nuxe.error;
import nuxe.io.reader;

export namespace nuxe {

struct bhd5_range {
    std::int64_t start;
    std::int64_t end;
};

struct bhd5_file_hash {
    std::array<std::byte, 32> value;
    std::vector<bhd5_range> ranges;
};

struct bhd5_file_encryption {
    std::array<std::byte, 16> key;
    std::vector<bhd5_range> ranges;
};

inline fn read_range(reader &input) -> result<bhd5_range> {
    const let start = input.read<std::int64_t>();
    if (!start)
        return std::unexpected(start.error());

    const let end = input.read<std::int64_t>();
    if (!end)
        return std::unexpected(end.error());

    return bhd5_range{*start, *end};
}

inline fn read_file_hash(std::span<const std::byte> bytes, std::endian endian,
                         std::size_t offset = 0) -> result<bhd5_file_hash> {
    let input = reader{
        bytes,
        endian,
    };
    const let seek = input.seek(offset);
    if (!seek)
        return std::unexpected(seek.error());

    let hash = bhd5_file_hash{};

    const let value = input.read_bytes(hash.value.size());
    if (!value)
        return std::unexpected(value.error());

    std::copy(value->begin(), value->end(), hash.value.begin());

    const let count_offset = input.position();
    const let count = input.read<std::int32_t>();
    if (!count)
        return std::unexpected(count.error());
    if (*count < 0)
        return std::unexpected(invalid_header{count_offset});

    const let range_count = static_cast<std::size_t>(*count);
    const let available = bytes.size() - input.position();

    if (range_count > available / 16)
        return std::unexpected(invalid_header{count_offset});

    hash.ranges.resize(range_count);
    for (std::size_t index = 0; index < range_count; ++index) {
        const let range = read_range(input);
        if (!range)
            return std::unexpected(range.error());

        hash.ranges[index] = *range;
    }
    return hash;
}

inline fn read_file_encryption(std::span<const std::byte> bytes,
                               std::endian endian, std::size_t offset)
    -> result<bhd5_file_encryption> {
    let input = reader{bytes, endian};

    const let seek = input.seek(offset);
    if (!seek)
        return std::unexpected(seek.error());

    let encryption = bhd5_file_encryption{};

    const let key = input.read_bytes(encryption.key.size());
    if (!key)
        return std::unexpected(key.error());

    std::copy(key->begin(), key->end(), encryption.key.begin());

    const let count_offset = input.position();
    const let count = input.read<std::int32_t>();
    if (!count)
        return std::unexpected(count.error());
    if (*count < 0)
        return std::unexpected(invalid_header{count_offset});

    const let range_count = static_cast<std::size_t>(*count);
    const let available = bytes.size() - input.position();

    if (range_count > available / 16)
        return std::unexpected(invalid_header{count_offset});

    encryption.ranges.resize(range_count);

    for (std::size_t index = 0; index < range_count; ++index) {
        const let range = read_range(input);
        if (!range)
            return std::unexpected(range.error());

        encryption.ranges[index] = *range;
    }

    return encryption;
}

} // namespace nuxe
