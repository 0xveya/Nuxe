module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nuxe/macros.h>

export module nuxe.cli.header;

import nuxe.cli.diagnostics;
import nuxe.crypto.header;
import nuxe.io.file;
import nuxe.format.bhd5;

export namespace nuxe::cli {

fn load_ds2_header(const std::string &path, const std::string &key_path,
                   std::size_t jobs, const diagnostics &diagnostics)
    -> std::optional<nuxe::bhd5> {
    let input = nuxe::read_file(path);
    if (!input) {
        diagnostics.report(input.error(), path);
        return std::nullopt;
    }
    let &bytes = *input;
    constexpr let magic = std::array{
        std::byte{'B'},
        std::byte{'H'},
        std::byte{'D'},
        std::byte{'5'},
    };
    const let decrypted = bytes.size() >= magic.size() &&
                          std::equal(magic.begin(), magic.end(), bytes.begin());
    if (!decrypted) {
        if (key_path.empty()) {
            diagnostics.report(
                "input has no BHD5 signature",
                "encrypted DS2 headers require --key <matching.pem>", path);
            return std::nullopt;
        }
        const let pem = nuxe::read_file(key_path);
        if (!pem) {
            diagnostics.report(pem.error(), key_path);
            return std::nullopt;
        }
        const let pem_text =
            pem->empty()
                ? std::string_view{}
                : std::string_view{reinterpret_cast<const char *>(pem->data()),
                                   pem->size()};
        const let key = nuxe::header_key::parse(pem_text);
        if (!key) {
            diagnostics.report(key.error(), key_path);
            return std::nullopt;
        }
        const let layout = key->layout(bytes.size());
        if (!layout) {
            diagnostics.report(layout.error(), path);
            return std::nullopt;
        }
        if (layout->block_count == 0) {
            diagnostics.report("header file is empty",
                               "supply a DS2 BHD5 header", path);
            return std::nullopt;
        }
        constexpr std::size_t blocks_per_batch = 64;
        const let batch_count =
            1 + (layout->block_count - 1) / blocks_per_batch;
        const let worker_count = std::min(jobs, batch_count);
        let workers = std::vector<nuxe::header_worker>{};
        workers.reserve(worker_count);
        for (std::size_t index = 0; index < worker_count; ++index) {
            let worker = nuxe::header_worker::create(*key);
            if (!worker) {
                diagnostics.report(worker.error(), key_path);
                return std::nullopt;
            }
            workers.emplace_back(std::move(*worker));
        }
        let plaintext = std::vector<std::byte>(layout->output_bytes);
        const let result =
            nuxe::decrypt_header(bytes, plaintext, workers, blocks_per_batch);
        if (!result) {
            diagnostics.report(result.error(), path);
            return std::nullopt;
        }
        bytes = std::move(plaintext);
    }

    let archive = nuxe::parse_bhd5<nuxe::bhd5_format::dark_souls2>(bytes);
    if (!archive) {
        diagnostics.report(archive.error(), path);
        return std::nullopt;
    }
    return std::move(*archive);
}

} // namespace nuxe::cli
