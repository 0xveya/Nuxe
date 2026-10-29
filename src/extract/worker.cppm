module;

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <stop_token>
#include <utility>
#include <vector>

#include <nuxe/macros.h>

export module nuxe.extract.worker;

import nuxe.error;
import nuxe.crypto.file;
import nuxe.extract.chunks;
import nuxe.io.payload;
import nuxe.extract.plan;

export namespace nuxe {

// progress is not synchronized for concurrent reads
struct worker_progress {
    std::uint64_t bytes_written{};
    std::size_t files_completed{};
};

struct extraction_worker {
    std::vector<std::byte> buffer;
    worker_progress progress;
    std::vector<file_cipher> ciphers;
};

fn prepare_worker(const extraction_plan &plan, std::size_t buffer_bytes)
    -> result<extraction_worker> {
    if (buffer_bytes == 0 || buffer_bytes > std::vector<std::byte>{}.max_size())
        return std::unexpected(extraction_error::invalid_worker_buffer);
    for (const let &entry : plan.entries) {
        if (entry.key && entry.key->value >= plan.keys.size())
            return std::unexpected(extraction_error::invalid_group);
        if (!entry.encrypted_ranges.empty() &&
            (!entry.key || buffer_bytes < 16))
            return std::unexpected(extraction_error::invalid_worker_buffer);
    }
    let worker = extraction_worker{
        .buffer = std::vector<std::byte>(buffer_bytes),
        .progress = {},
        .ciphers = {},
    };
    worker.ciphers.reserve(plan.keys.size());
    for (const let &key : plan.keys) {
        let cipher = file_cipher::create(key);
        if (!cipher)
            return std::unexpected(cipher.error());
        worker.ciphers.emplace_back(std::move(*cipher));
    }
    return worker;
}

fn run_group(const extraction_plan &plan, const extraction_group &group,
             extraction_worker &worker, overwrite_policy overwrite,
             std::stop_token cancellation = {}) -> result<void> {
    if (worker.buffer.empty())
        return std::unexpected(extraction_error::invalid_worker_buffer);
    if (group.first_entry > plan.entries.size() ||
        group.entry_count > plan.entries.size() - group.first_entry)
        return std::unexpected(extraction_error::invalid_group);
    const let entries =
        std::span{plan.entries}.subspan(group.first_entry, group.entry_count);
    for (const let &entry : entries) {
        if (entry.key && entry.key->value >= worker.ciphers.size())
            return std::unexpected(extraction_error::invalid_group);
        if (!entry.encrypted_ranges.empty() &&
            (!entry.key || worker.buffer.size() < 16))
            return std::unexpected(extraction_error::invalid_worker_buffer);
    }
    if (cancellation.stop_requested())
        return std::unexpected(operation_cancelled{});
    if (entries.empty())
        return {};
    let input = open_archive(plan.data_path);
    if (!input)
        return std::unexpected(input.error());
    for (const let &entry : entries) {
        if (entry.data_offset > input->size() ||
            entry.data_length > input->size() - entry.data_offset)
            return std::unexpected(extraction_error::invalid_source_range);
    }
    for (const let &entry : entries) {
        if (cancellation.stop_requested())
            return std::unexpected(operation_cancelled{});
        let output = open_output(*input, entry.output_path, overwrite);
        if (!output)
            return std::unexpected(output.error());
        if (entry.encrypted_ranges.empty()) {
            const let copied = copy_plain_payload(
                *input, *output, entry.data_offset, entry.data_length,
                worker.buffer, cancellation);
            if (!copied)
                return std::unexpected(copied.error());
        } else {
            let offset = std::uint64_t{};
            while (offset < entry.data_length) {
                const let next =
                    next_payload_chunk(entry, offset, worker.buffer.size());
                if (!next)
                    return std::unexpected(next.error());
                const let chunk = **next;
                const let copied = copy_payload(
                    *input, *output, entry.data_offset + offset, chunk.size,
                    std::span{worker.buffer}.first(chunk.size), cancellation,
                    [&](std::span<std::byte> bytes) -> result<void> {
                        if (chunk.encrypted)
                            return worker.ciphers[entry.key->value]
                                .decrypt_blocks(bytes);
                        return {};
                    });
                if (!copied)
                    return std::unexpected(copied.error());
                offset += chunk.size;
            }
        }
        const let finished = output->finish();
        if (!finished)
            return std::unexpected(finished.error());
        worker.progress.bytes_written += entry.data_length;
        ++worker.progress.files_completed;
    }
    return {};
}

} // namespace nuxe
