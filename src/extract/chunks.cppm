module;

#include <cstddef>
#include <cstdint>
#include <nuxe/macros.h>
#include <optional>

export module nuxe.extract.chunks;

import nuxe.error;
import nuxe.extract.plan;

export namespace nuxe {

struct payload_chunk {
    std::uint64_t file_offset;
    std::size_t size;
};

// an empty optional means EOF
fn next_payload_chunk(const extraction_entry &entry, std::uint64_t file_offset,
                      std::size_t buffer_bytes)
    -> result<std::optional<payload_chunk>>;

} // namespace nuxe
