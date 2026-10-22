module;

#include <cstdint>
#include <expected>
#include <filesystem>
#include <utility>

#include <nuxe/macros.h>

export module nuxe.extract.planner;

import nuxe.error;
import nuxe.extract.plan;
import nuxe.format.bhd5;

export namespace nuxe {

fn plan_file(const bhd5_file &file, std::filesystem::path data_path,
             std::uint64_t data_size, std::filesystem::path output_path)
    -> result<extraction_plan> {
    if (file.encryption)
        return std::unexpected(extraction_error::encrypted_payload);

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
    return plan;
}

} // namespace nuxe
