module;

#include <array>
#include <format>
#include <openssl/err.h>
#include <print>
#include <string>
#include <string_view>

#include <nuxe/macros.h>

export module nuxe.cli.diagnostics;

import nuxe.error;
import nuxe.match;

namespace {
fn crypto_summary(nuxe::crypto_operation operation) -> std::string_view {
    switch (operation) {
    case nuxe::crypto_operation::import_header_key:
        return "could not import RSA public key";
    case nuxe::crypto_operation::calculate_header_layout:
        return "encrypted header length is not a whole number of RSA blocks";
    case nuxe::crypto_operation::prepare_header_worker:
        return "could not prepare RSA worker";
    case nuxe::crypto_operation::decrypt_header:
        return "could not decrypt header";
    case nuxe::crypto_operation::prepare_file_cipher:
        return "could not prepare AES cipher";
    case nuxe::crypto_operation::decrypt_file_ranges:
        return "could not decrypt file ranges";
    }
    return "crypto operation failed";
}
} // namespace

export namespace nuxe::cli {

struct diagnostics {
    fn report(std::string_view summary, std::string_view detail = {},
              std::string_view argument = {}) const -> void {
        std::print(stderr, "nuxe: error: ");
        if (!argument.empty())
            std::print(stderr, "'{}': ", argument);
        std::print(stderr, "{}", summary);
        if (!detail.empty())
            std::print(stderr, ": {}", detail);
        std::println(stderr, "");
    }

    fn report(const nuxe::error &failure, std::string_view argument = {}) const
        -> void {
        match(failure)(
            case_(nuxe::extraction_error, error) {
                switch (error) {
                case nuxe::extraction_error::invalid_worker_count:
                    report("worker count must be nonzero", {}, argument);
                    break;
                case nuxe::extraction_error::invalid_bucket_index:
                    report("bucket index is out of range",
                           "use a bucket index from inspect", argument);
                    break;
                case nuxe::extraction_error::invalid_file_index:
                    report("file index is out of range",
                           "use a file index within the selected bucket from "
                           "inspect",
                           argument);
                    break;
                case nuxe::extraction_error::encrypted_payload:
                    report("encrypted payload extraction is not implemented",
                           {}, argument);
                    break;
                case nuxe::extraction_error::invalid_encryption_range:
                    report("invalid or overlapping AES payload ranges", {},
                           argument);
                    break;
                case nuxe::extraction_error::invalid_source_range:
                    report("payload range is outside the data archive", {},
                           argument);
                    break;
                case nuxe::extraction_error::invalid_destination:
                    report("invalid payload destination", {}, argument);
                    break;
                case nuxe::extraction_error::invalid_worker_buffer:
                    report("worker buffer must be nonzero and at least 16 "
                           "bytes for encrypted payloads",
                           {}, argument);
                    break;
                case nuxe::extraction_error::invalid_group:
                    report("invalid extraction group", {}, argument);
                    break;
                }
            },
            case_(nuxe::operation_cancelled) {
                report("operation cancelled", {}, argument);
            },
            case_(nuxe::worker_launch_error, error) {
                report("could not start worker", error.cause.message(),
                       argument);
            },
            case_(nuxe::crypto_error, error) {
                const let aes =
                    error.operation ==
                        nuxe::crypto_operation::prepare_file_cipher ||
                    error.operation ==
                        nuxe::crypto_operation::decrypt_file_ranges;
                let help = std::string{
                    aes ? "check the payload AES key and block ranges"
                        : "use the matching DS2 RSA public key PEM"};
                if (error.code != 0) {
                    let detail = std::array<char, 256>{};
                    ERR_error_string_n(error.code, detail.data(),
                                       detail.size());
                    help += std::format("; {}", detail.data());
                }
                report(crypto_summary(error.operation), help, argument);
            },
            case_(nuxe::io_error, error) {
                const let summary = [&]() -> std::string_view {
                    switch (error.operation) {
                    case nuxe::io_operation::open:
                        return "could not open file";
                    case nuxe::io_operation::stat:
                        return "could not inspect file";
                    case nuxe::io_operation::read:
                        return "could not read file";
                    case nuxe::io_operation::map:
                        return "could not map file";
                    case nuxe::io_operation::seek:
                        return "could not seek file";
                    case nuxe::io_operation::write:
                        return "could not write file";
                    case nuxe::io_operation::copy:
                        return "could not copy file";
                    case nuxe::io_operation::truncate:
                        return "could not truncate file";
                    case nuxe::io_operation::close:
                        return "could not close file";
                    }
                    return "file operation failed";
                }();
                report(summary, error.cause.message(), error.path.string());
            },
            case_(nuxe::truncated_input, error) {
                report("truncated BHD5 input",
                       std::format("at byte {}: need {} bytes, have {}",
                                   error.offset, error.requested,
                                   error.available),
                       argument);
            },
            case_(nuxe::invalid_offset, error) {
                report("BHD5 offset is outside the input",
                       std::format("offset {} exceeds the {}-byte input",
                                   error.offset, error.input_size),
                       argument);
            },
            case_(nuxe::invalid_magic) {
                report("invalid BHD5 signature",
                       "check the input header and its matching PEM key",
                       argument);
            },
            case_(nuxe::invalid_header, error) {
                report("invalid BHD5 header or metadata",
                       std::format("invalid field at byte {}", error.offset),
                       argument);
            });
    }
};

} // namespace nuxe::cli
