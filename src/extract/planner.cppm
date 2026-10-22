module;

#include <cstdint>
#include <filesystem>
#include <nuxe/macros.h>

export module nuxe.extract.planner;

import nuxe.error;
import nuxe.extract.plan;
import nuxe.format.bhd5;

export namespace nuxe {

/// Validates one entry and destination without opening or writing output files.
fn plan_file(const bhd5_file &file, std::filesystem::path data_path,
             std::uint64_t data_size, std::filesystem::path output_path)
    -> result<extraction_plan>;

} // namespace nuxe
