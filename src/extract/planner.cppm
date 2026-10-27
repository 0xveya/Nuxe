module;

#include <algorithm>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <utility>
#include <vector>

#include <nuxe/macros.h>

export module nuxe.extract.planner;

import nuxe.error;
import nuxe.extract.plan;
import nuxe.format.bhd5;

export namespace nuxe {

fn prepare_encrypted_ranges(std::span<const bhd5_range> ranges,
                            std::uint64_t data_length)
    -> result<std::vector<bhd5_range>> {
    let prepared = std::vector<bhd5_range>{};
    prepared.reserve(ranges.size());

    for (const let &range : ranges) {
        if (range.start == -1 || range.end == -1 || range.start == range.end)
            continue;

        if (range.start < 0 || range.end < range.start)
            return std::unexpected(extraction_error::invalid_encryption_range);

        const let start = static_cast<std::uint64_t>(range.start);
        const let end = static_cast<std::uint64_t>(range.end);

        if (end > data_length || (end - start) % 16 != 0)
            return std::unexpected(extraction_error::invalid_encryption_range);

        prepared.emplace_back(range);
    }

    std::ranges::sort(prepared, {}, &bhd5_range::start);

    for (std::size_t index = 1; index < prepared.size(); ++index) {
        if (prepared[index].start < prepared[index - 1].end)
            return std::unexpected(extraction_error::invalid_encryption_range);
    }

    return prepared;
}

fn plan_file(const bhd5_file &file, std::filesystem::path data_path,
             std::uint64_t data_size, std::filesystem::path output_path)
    -> result<extraction_plan> {
    if (file.data_offset > data_size ||
        file.data_length > data_size - file.data_offset)
        return std::unexpected(extraction_error::invalid_source_range);

    output_path = output_path.lexically_normal();
    if (output_path.empty() || output_path.filename().empty() ||
        output_path.filename() == "." || output_path.filename() == ".." ||
        output_path == data_path.lexically_normal())
        return std::unexpected(extraction_error::invalid_destination);

    let plan = extraction_plan{};
    plan.data_path = std::move(data_path);
    plan.entries.resize(1);
    plan.groups.resize(1);
    plan.directories.resize(1);

    const let parent = output_path.parent_path();
    plan.directories[0] = parent.empty() ? std::filesystem::path{"."} : parent;
    plan.entries[0] = extraction_entry{
        .path_hash = file.path_hash,
        .data_offset = file.data_offset,
        .data_length = file.data_length,
        .output_path = std::move(output_path),
        .key = {},
        .encrypted_ranges = {},
    };
    plan.groups[0] = extraction_group{
        .first_entry = 0,
        .entry_count = 1,
        .byte_count = file.data_length,
    };
    if (file.encryption) {
        let ranges =
            prepare_encrypted_ranges(file.encryption->ranges, file.data_length);
        if (!ranges)
            return std::unexpected(ranges.error());
        if (!ranges->empty()) {
            plan.keys.resize(1);
            plan.keys[0] = file.encryption->key;
            plan.entries[0].key = key_id{0};
            plan.entries[0].encrypted_ranges = std::move(*ranges);
        }
    }
    return plan;
}

} // namespace nuxe
