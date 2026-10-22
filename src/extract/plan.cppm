module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

export module nuxe.extract.plan;

import nuxe.format.bhd5.metadata;

export namespace nuxe {

enum class overwrite_policy { fail, replace };

/// Index into the plan's immutable registry of distinct AES keys.
struct key_id {
    std::size_t value;
};

/// One validated output file and its payload location in the source archive.
struct extraction_entry {
    std::uint64_t path_hash;
    std::uint64_t data_offset;
    std::uint64_t data_length;
    std::filesystem::path output_path;
    std::optional<key_id> key;
    std::vector<bhd5_range> encrypted_ranges;
};

/// Adjacent whole-file jobs bounded by both file count and byte count.
struct extraction_group {
    std::size_t first_entry;
    std::size_t entry_count;
    std::uint64_t byte_count;
};

/// An immutable execution input with entries sorted by data offset.
struct extraction_plan {
    std::filesystem::path data_path;
    std::vector<extraction_entry> entries;
    std::vector<extraction_group> groups;
    std::vector<std::array<std::byte, 16>> keys;
    std::vector<std::filesystem::path> directories;
};

} // namespace nuxe
