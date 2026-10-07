#include <CLI/CLI.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <format>
#include <fstream>
#include <nuxe/macros.h>
#include <print>
#include <string>
#include <utility>
#include <vector>

import nuxe;
import nlohmann.json;

fn inspect_ds2(const std::string &path) -> int {
    let file = std::ifstream{path, std::ios::binary | std::ios::ate};
    if (!file) {
        std::println(stderr, "could not open {}", path);
        return 1;
    }

    const let size = static_cast<std::streamoff>(file.tellg());
    if (size < 0) {
        std::println(stderr, "could not determine size of {}", path);
        return 1;
    }

    let bytes = std::vector<std::byte>(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(bytes.data()),
                   static_cast<std::streamsize>(size))) {
        std::println(stderr, "could not read {}", path);
        return 1;
    }

    const let print_error = [](const nuxe::error &failure) {
        match(failure)(
            case_(nuxe::io_error, error) {
                std::println(stderr, "I/O error: {}", error.cause.message());
            },
            case_(nuxe::truncated_input, error) {
                std::println(stderr,
                             "truncated input at {}: need {} bytes, have {}",
                             error.offset, error.requested, error.available);
            },
            case_(nuxe::invalid_offset, error) {
                std::println(stderr, "invalid offset {} for {} bytes",
                             error.offset, error.input_size);
            },
            case_(nuxe::invalid_magic) {
                std::println(stderr, "invalid BHD5 magic");
            },
            case_(nuxe::invalid_header, error) {
                std::println(stderr, "invalid header field at {}",
                             error.offset);
            });
    };

    const let archive = nuxe::parse_bhd5<nuxe::bhd5_format::dark_souls2>(bytes);
    if (!archive) {
        print_error(archive.error());
        return 1;
    }

    let buckets = nlohmann::json::array();
    let file_count = std::uint64_t{};
    for (std::size_t index = 0; index < archive->buckets.size(); ++index) {
        const let &bucket = archive->buckets[index];
        let files = nlohmann::json::array();
        for (std::size_t file_index = 0; file_index < bucket.files.size();
             ++file_index) {
            const let &entry = bucket.files[file_index];
            files.push_back({
                {"index", file_index},
                {"path_hash", std::format("{:016x}", entry.path_hash)},
                {"data_length", entry.data_length},
                {"data_offset", entry.data_offset},
                {"hash_offset", entry.hash_offset},
                {"encryption_offset", entry.encryption_offset},
            });
        }
        buckets.push_back({
            {"index", index},
            {"file_count", bucket.files.size()},
            {"files", std::move(files)},
        });
        file_count += static_cast<std::uint64_t>(bucket.files.size());
    }

    const let output = nlohmann::json{
        {"format", "DarkSouls2"},
        {"little_endian", archive->little_endian},
        {"unk05", archive->unk05},
        {"bucket_count", archive->buckets.size()},
        {"data_salt", archive->data_salt},
        {"file_count", file_count},
        {"buckets", std::move(buckets)},
    };
    std::println("{}", output.dump(2));
    return 0;
}

fn main(int argc, char **argv) -> int {
    let app = CLI::App{"FromSoftware archive extractor", "nuxe"};
    app.require_subcommand(1);

    let path = std::string{};
    app.add_subcommand("inspect",
                       "inspect a decrypted Dark Souls 2 BHD5 header")
        ->add_option("path", path, "path to the decrypted BHD5 file")
        ->required();

    try {
        app.parse(argc, argv);
        return inspect_ds2(path);
    } catch (const CLI::ParseError &error) {
        return app.exit(error);
    } catch (const std::exception &error) {
        std::println(stderr, "inspect failed: {}", error.what());
        return 1;
    }
}
