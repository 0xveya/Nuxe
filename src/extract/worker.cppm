module;

#include <cstddef>
#include <cstdint>
#include <nuxe/macros.h>
#include <stop_token>
#include <vector>

export module nuxe.extract.worker;

import nuxe.crypto.file;
import nuxe.error;
import nuxe.extract.plan;

export namespace nuxe {

/// Worker-local state, read by the coordinator only after joining.
struct worker_progress {
    std::uint64_t bytes_written{};
    std::size_t files_completed{};
};

/// Prepared private storage and ciphers; container sizes stay fixed during
/// work.
struct extraction_worker {
    std::vector<std::byte> buffer;
    std::vector<file_cipher> ciphers;
    worker_progress progress;
};

/// Allocates the fixed buffer and prepares ciphers in plan key-ID order.
fn prepare_worker(const extraction_plan &plan, std::size_t buffer_bytes)
    -> result<extraction_worker>;

/// Owns each output file until complete; reads input at explicit offsets.
fn run_group(const extraction_plan &plan, const extraction_group &group,
             extraction_worker &worker, overwrite_policy overwrite,
             std::stop_token cancellation = {}) -> result<void>;

} // namespace nuxe
