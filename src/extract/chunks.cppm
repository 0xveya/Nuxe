module;

#include <cstddef>
#include <cstdint>
#include <nuxe/macros.h>
#include <optional>

export module nuxe.extract.chunks;

import nuxe.error;
import nuxe.extract.plan;

export namespace nuxe {

/// A bounded payload slice whose encrypted portions contain complete AES
/// blocks.
struct payload_chunk {
    std::uint64_t file_offset;
    std::size_t size;
};

/// Selects the next slice; an empty optional means the payload is complete.
fn next_payload_chunk(const extraction_entry &entry, std::uint64_t file_offset,
                      std::size_t buffer_bytes)
    -> result<std::optional<payload_chunk>>;

} // namespace nuxe
