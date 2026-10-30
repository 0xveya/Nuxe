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

class payload_cursor {
  public:
    explicit payload_cursor(const extraction_entry &entry,
                            std::uint64_t offset = 0) noexcept
        : entry_(entry), offset_(offset) {}

    // an empty optional means EOF
    fn next(std::size_t buffer_bytes) -> result<std::optional<payload_chunk>> {
        if (buffer_bytes == 0)
            return std::unexpected(extraction_error::invalid_worker_buffer);
        if (offset_ > entry_.data_length)
            return std::unexpected(extraction_error::invalid_source_range);
        if (offset_ == entry_.data_length)
            return std::optional<payload_chunk>{};

        while (range_index_ < entry_.encrypted_ranges.size() &&
               offset_ >= static_cast<std::uint64_t>(
                              entry_.encrypted_ranges[range_index_].end))
            ++range_index_;

        let end = entry_.data_length;
        let encrypted = false;
        if (range_index_ < entry_.encrypted_ranges.size()) {
            const let &range = entry_.encrypted_ranges[range_index_];
            const let start = static_cast<std::uint64_t>(range.start);
            if (offset_ < start) {
                end = start;
            } else {
                if ((offset_ - start) % 16 != 0)
                    return std::unexpected(
                        extraction_error::invalid_source_range);
                end = static_cast<std::uint64_t>(range.end);
                encrypted = true;
            }
        }
        let count = static_cast<std::size_t>(
            std::min(end - offset_, static_cast<std::uint64_t>(buffer_bytes)));
        if (encrypted)
            count -= count % 16;
        if (count == 0)
            return std::unexpected(extraction_error::invalid_worker_buffer);
        const let chunk = payload_chunk{offset_, count, encrypted};
        offset_ += count;
        return std::optional{chunk};
    }

  private:
    const extraction_entry &entry_;
    std::uint64_t offset_{};
    std::size_t range_index_{};
};

fn next_payload_chunk(const extraction_entry &entry, std::uint64_t file_offset,
                      std::size_t buffer_bytes)
    -> result<std::optional<payload_chunk>> {
    return payload_cursor{entry, file_offset}.next(buffer_bytes);
}

} // namespace nuxe
