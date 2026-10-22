module;

#include <CLI/CLI.hpp>
#include <cstddef>
#include <cstdint>
#include <format>
#include <print>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <nuxe/macros.h>

export module nuxe.cli.inspect;

import nuxe.cli.diagnostics;
import nuxe.cli.header;
import nuxe.cli.options;
import nuxe.format.bhd5;
import nlohmann.json;

namespace {

fn hex_bytes(std::span<const std::byte> bytes) -> std::string {
    let text = std::string{};
    text.reserve(bytes.size() * 2);
    for (const let byte : bytes)
        text += std::format("{:02x}", std::to_integer<unsigned>(byte));
    return text;
}

fn ranges_json(std::span<const nuxe::bhd5_range> ranges) -> nlohmann::json {
    let output = nlohmann::json(nlohmann::json::array_t(ranges.size()));
    for (std::size_t index = 0; index < ranges.size(); ++index) {
        const let &range = ranges[index];
        output[index] = {{"start", range.start}, {"end", range.end}};
    }
    return output;
}

fn inspect_json(const nuxe::bhd5 &archive) -> nlohmann::json {
    let buckets =
        nlohmann::json(nlohmann::json::array_t(archive.buckets.size()));
    let file_count = std::uint64_t{};
    for (std::size_t index = 0; index < archive.buckets.size(); ++index) {
        const let &bucket = archive.buckets[index];
        let files =
            nlohmann::json(nlohmann::json::array_t(bucket.files.size()));
        for (std::size_t file_index = 0; file_index < bucket.files.size();
             ++file_index) {
            const let &entry = bucket.files[file_index];
            let hash = nlohmann::json(nullptr);
            if (entry.hash) {
                hash = {
                    {"value", hex_bytes(entry.hash->value)},
                    {"ranges", ranges_json(entry.hash->ranges)},
                };
            }
            let encryption = nlohmann::json(nullptr);
            if (entry.encryption) {
                encryption = {
                    {"key", hex_bytes(entry.encryption->key)},
                    {"ranges", ranges_json(entry.encryption->ranges)},
                };
            }
            files[file_index] = {
                {"index", file_index},
                {"path_hash", std::format("{:016x}", entry.path_hash)},
                {"data_length", entry.data_length},
                {"data_offset", entry.data_offset},
                {"hash_offset", entry.hash_offset},
                {"encryption_offset", entry.encryption_offset},
                {"hash", std::move(hash)},
                {"encryption", std::move(encryption)},
            };
        }
        buckets[index] = {
            {"index", index},
            {"file_count", bucket.files.size()},
            {"files", std::move(files)},
        };
        file_count += static_cast<std::uint64_t>(bucket.files.size());
    }

    return nlohmann::json{
        {"format", "DarkSouls2"},
        {"little_endian", archive.little_endian},
        {"unk05", archive.unk05},
        {"bucket_count", archive.buckets.size()},
        {"data_salt", archive.data_salt},
        {"file_count", file_count},
        {"buckets", std::move(buckets)},
    };
}

fn inspect_ds2(const std::string &path, const std::string &key_path,
               std::size_t jobs, const nuxe::cli::diagnostics &diagnostics)
    -> int {
    const let archive =
        nuxe::cli::load_ds2_header(path, key_path, jobs, diagnostics);
    if (!archive)
        return 1;
    std::println("{}", inspect_json(*archive).dump(2));
    return 0;
}

} // namespace

export namespace nuxe::cli {

struct inspect_command {
    std::string path;
    std::string key_path;
    std::size_t jobs = 1;

    fn attach(CLI::App &app, int &exit_code,
              const nuxe::cli::diagnostics &diagnostics) -> void {
        let *command = app.add_subcommand(
            "inspect", "inspect a Dark Souls 2 BHD5 header as JSON");
        command
            ->add_option("path", path,
                         "path to an encrypted or decrypted DS2 BHD5 file")
            ->required();
        command->add_option("--key", key_path,
                            "matching RSA public key PEM for encrypted input");
        command->add_option("--jobs", jobs, "maximum RSA workers (default: 1)")
            ->check(size_option());
        command->callback([this, &exit_code, &diagnostics] {
            exit_code = inspect_ds2(path, key_path, jobs, diagnostics);
        });
    }
};

} // namespace nuxe::cli
