module;

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include <nuxe/macros.h>

export module nuxe.cli.header;

import nuxe.cli.diagnostics;
import nuxe.archive.ds2;

export namespace nuxe::cli {

fn load_ds2_header(const std::string &path, const std::string &key_path,
                   std::size_t jobs, const diagnostics &diagnostics)
    -> std::optional<nuxe::bhd5> {
    let archive = nuxe::load_ds2_header(path, key_path, jobs);
    if (!archive) {
        diagnostics.report(archive.error(), path);
        return std::nullopt;
    }
    return std::move(*archive);
}

} // namespace nuxe::cli
