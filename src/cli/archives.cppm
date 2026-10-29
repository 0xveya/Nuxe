module;

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <nuxe/macros.h>

export module nuxe.cli.archives;

import nuxe.cli.diagnostics;

export namespace nuxe::cli {

struct archive_paths {
    std::filesystem::path header;
    std::filesystem::path data;
    std::filesystem::path key;
};

fn paths_for_header(std::filesystem::path header) -> archive_paths {
    let data = header;
    data.replace_extension(".bdt");
    const let stem = header.stem().string();
    let key =
        header.parent_path() /
        (stem.ends_with("Ebl") ? stem.substr(0, stem.size() - 3) + "KeyCode.pem"
                               : stem + ".pem");
    return {std::move(header), std::move(data), std::move(key)};
}

fn discover_archives(const std::filesystem::path &directory,
                     const diagnostics &diagnostics)
    -> std::optional<std::vector<archive_paths>> {
    let error = std::error_code{};
    let iterator = std::filesystem::directory_iterator{directory, error};
    let archives = std::vector<archive_paths>{};
    while (!error && iterator != std::filesystem::directory_iterator{}) {
        const let &entry = *iterator;
        if (entry.path().extension() == ".bhd" &&
            entry.is_regular_file(error)) {
            let paths = paths_for_header(entry.path());
            if (!std::filesystem::is_regular_file(paths.data, error)) {
                diagnostics.report(
                    "matching BDT archive is missing or not a regular file",
                    error ? error.message() : std::string{},
                    paths.data.string());
                return std::nullopt;
            }
            archives.emplace_back(std::move(paths));
        }
        if (!error)
            iterator.increment(error);
    }
    if (error) {
        diagnostics.report("could not scan archive directory", error.message(),
                           directory.string());
        return std::nullopt;
    }
    if (archives.empty()) {
        diagnostics.report("no BHD5 archives found", {}, directory.string());
        return std::nullopt;
    }
    std::ranges::sort(archives, {}, &archive_paths::header);
    return archives;
}

} // namespace nuxe::cli
