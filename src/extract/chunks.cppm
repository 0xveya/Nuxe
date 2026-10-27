module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <nuxe/macros.h>
#include <optional>

export module nuxe.extract.chunks;

import nuxe.error;
import nuxe.extract.plan;

export namespace nuxe {

struct payload_chunk {
    std::uint64_t file_offset;
    std::size_t size;
    bool encrypted;
};

// an empty optional means EOF
fn next_payload_chunk(const extraction_entry &entry, std::uint64_t file_offset,
                      std::size_t buffer_bytes)
    -> result<std::optional<payload_chunk>> {
    if (buffer_bytes == 0)
        return std::unexpected(extraction_error::invalid_worker_buffer);
    if (file_offset > entry.data_length)
        return std::unexpected(extraction_error::invalid_source_range);
    if (file_offset == entry.data_length)
        return std::optional<payload_chunk>{};

    let end = entry.data_length;
    let encrypted = false;
    for (const let &range : entry.encrypted_ranges) {
        const let start = static_cast<std::uint64_t>(range.start);
        const let finish = static_cast<std::uint64_t>(range.end);
        if (file_offset >= finish)
            continue;
        if (file_offset < start) {
            end = start;
        } else {
            if ((file_offset - start) % 16 != 0)
                return std::unexpected(extraction_error::invalid_source_range);
            end = finish;
            encrypted = true;
        }
        break;
    }
    let count = static_cast<std::size_t>(
        std::min(end - file_offset, static_cast<std::uint64_t>(buffer_bytes)));
    if (encrypted)
        count -= count % 16;
    if (count == 0)
        return std::unexpected(extraction_error::invalid_worker_buffer);
    return std::optional{payload_chunk{file_offset, count, encrypted}};
}

} // namespace nuxe
