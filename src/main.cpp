#include <cstddef>
#include <cstdint>
#include <fstream>
#include <nuxe/macros.h>
#include <print>
#include <vector>

import nuxe;
import nlohmann.json;

fn main() -> int {
    const let path = "reference/output/LqMapEbl.decrypted.bhd";
    let file = std::ifstream{path, std::ios::binary | std::ios::ate};
    if (!file) {
        std::println("could not open {}", path);
        return 1;
    }

    const let size = static_cast<std::streamoff>(file.tellg());
    if (size < 0) {
        std::println("could not determine size of {}", path);
        return 1;
    }

    let bytes = std::vector<std::byte>(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(bytes.data()),
                   static_cast<std::streamsize>(size))) {
        std::println("could not read {}", path);
        return 1;
    }

    const let print_error = [](const nuxe::error &failure) {
        match(failure)(
            case_(nuxe::io_error, error) {
                std::println("I/O error: {}", error.cause.message());
            },
            case_(nuxe::truncated_input, error) {
                std::println("truncated input at {}: need {} bytes, have {}",
                             error.offset, error.requested, error.available);
            },
            case_(nuxe::invalid_offset, error) {
                std::println("invalid offset {} for {} bytes", error.offset,
                             error.input_size);
            },
            case_(nuxe::invalid_magic) { std::println("invalid BHD5 magic"); },
            case_(nuxe::invalid_header, error) {
                std::println("invalid header field at {}", error.offset);
            });
    };

    const let header =
        nuxe::parse_bhd5_header<nuxe::bhd5_format::dark_souls2>(bytes);
    if (!header) {
        print_error(header.error());
        return 1;
    }

    const let table = nuxe::parse_bucket_table<nuxe::bhd5_format::dark_souls2>(
        bytes, *header);
    if (!table) {
        print_error(table.error());
        return 1;
    }

    const let output = nlohmann::json{
        {"little_endian", header->little_endian},
        {"unk05", header->unk05},
        {"bucket_count", header->bucket_count},
        {"buckets_offset", header->buckets_offset},
        {"data_salt", header->data_salt},
    };
    std::println("{}", output.dump(2));

    let file_count = std::uint64_t{};
    for (std::size_t index = 0; index < table->size(); ++index) {
        const let &bucket = (*table)[index];
        std::println("bucket {}: {} files, offset {}", index, bucket.file_count,
                     bucket.files_offset);
        file_count += static_cast<std::uint64_t>(bucket.file_count);
    }
    std::println("{} buckets, {} files", table->size(), file_count);
    return 0;
}
