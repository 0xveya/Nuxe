module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
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

fn build_groups(std::span<const extraction_entry> entries, group_limits limits)
    -> result<std::vector<extraction_group>> {
    if (limits.max_files == 0 || limits.max_bytes == 0)
        return std::unexpected(extraction_error::invalid_group);

    let groups = std::vector<extraction_group>{};
    groups.reserve(entries.size());

    let first = std::size_t{};
    while (first < entries.size()) {
        let count = std::size_t{};
        let bytes = std::uint64_t{};
        while (count < entries.size() - first && count < limits.max_files) {
            const let length = entries[first + count].data_length;
            if (count != 0 && (bytes >= limits.max_bytes ||
                               length > limits.max_bytes - bytes))
                break;
            bytes += length;
            ++count;
        }
        groups.emplace_back(extraction_group{
            .first_entry = first,
            .entry_count = count,
            .byte_count = bytes,
        });
        first += count;
    }
    return groups;
}

struct extraction_request {
    std::reference_wrapper<const bhd5_file> file;
    std::filesystem::path output_path;
};

fn prepare_entry(const bhd5_file &file, const std::filesystem::path &data_path,
                 std::uint64_t data_size, std::filesystem::path output_path)
    -> result<extraction_entry> {
    if (file.data_offset > data_size ||
        file.data_length > data_size - file.data_offset)
        return std::unexpected(extraction_error::invalid_source_range);

    output_path = output_path.lexically_normal();
    if (output_path.empty() || output_path.filename().empty() ||
        output_path.filename() == "." || output_path.filename() == ".." ||
        output_path == data_path)
        return std::unexpected(extraction_error::invalid_destination);

    let entry = extraction_entry{
        .path_hash = file.path_hash,
        .data_offset = file.data_offset,
        .data_length = file.data_length,
        .output_path = std::move(output_path),
        .key = {},
        .encrypted_ranges = {},
    };
    if (file.encryption) {
        let ranges =
            prepare_encrypted_ranges(file.encryption->ranges, file.data_length);
        if (!ranges)
            return std::unexpected(ranges.error());
        entry.encrypted_ranges = std::move(*ranges);
    }
    return entry;
}

fn plan_files(std::span<const extraction_request> requests,
              std::filesystem::path data_path, std::uint64_t data_size,
              group_limits limits) -> result<extraction_plan> {
    if (limits.max_files == 0 || limits.max_bytes == 0)
        return std::unexpected(extraction_error::invalid_group);

    let plan = extraction_plan{};
    plan.data_path = data_path.lexically_normal();
    plan.entries.resize(requests.size());
    plan.keys.reserve(requests.size());
    plan.directories.reserve(requests.size());
    let destinations = std::vector<std::filesystem::path>{};
    destinations.reserve(requests.size());
    let keys = std::map<std::array<std::byte, 16>, key_id>{};

    for (std::size_t index = 0; index < requests.size(); ++index) {
        const let &request = requests[index];
        const let &file = request.file.get();
        let entry =
            prepare_entry(file, plan.data_path, data_size, request.output_path);
        if (!entry)
            return std::unexpected(entry.error());
        if (!entry->encrypted_ranges.empty()) {
            const let[position, inserted] = keys.try_emplace(
                file.encryption->key, key_id{plan.keys.size()});
            if (inserted)
                plan.keys.emplace_back(file.encryption->key);
            entry->key = position->second;
        }
        destinations.emplace_back(entry->output_path);
        const let parent = entry->output_path.parent_path();
        plan.directories.emplace_back(
            parent.empty() ? std::filesystem::path{"."} : parent);
        plan.entries[index] = std::move(*entry);
    }

    std::ranges::sort(destinations);
    if (std::ranges::adjacent_find(destinations) != destinations.end())
        return std::unexpected(extraction_error::invalid_destination);
    for (const let &destination : destinations) {
        let parent = destination.parent_path();
        while (!parent.empty()) {
            if (std::ranges::binary_search(destinations, parent))
                return std::unexpected(extraction_error::invalid_destination);
            const let next = parent.parent_path();
            if (next == parent)
                break;
            parent = next;
        }
    }
    std::ranges::sort(plan.directories);
    const let redundant = std::ranges::unique(plan.directories);
    plan.directories.erase(redundant.begin(), redundant.end());

    std::ranges::stable_sort(plan.entries, {}, &extraction_entry::data_offset);
    let groups = build_groups(plan.entries, limits);
    if (!groups)
        return std::unexpected(groups.error());
    plan.groups = std::move(*groups);
    return plan;
}

fn plan_file(const bhd5_file &file, std::filesystem::path data_path,
             std::uint64_t data_size, std::filesystem::path output_path)
    -> result<extraction_plan> {
    const let request =
        extraction_request{std::cref(file), std::move(output_path)};
    return plan_files(
        std::span{&request, 1}, std::move(data_path), data_size,
        group_limits{1, std::numeric_limits<std::uint64_t>::max()});
}

} // namespace nuxe
