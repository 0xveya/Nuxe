module;

#include <CLI/CLI.hpp>
#include <cstddef>
#include <print>
#include <string>

#include <nuxe/macros.h>

export module nuxe.cli.extract;

import nuxe.cli.diagnostics;
import nuxe.cli.options;
import nuxe.archive.ds2;

export namespace nuxe::cli {

struct extract_command {
    std::string header_path;
    std::string data_path;
    std::string output_path;
    std::string key_path;
    std::size_t bucket = 0;
    std::size_t file = 0;
    std::size_t jobs = 1;
    std::size_t buffer_size = 1024 * 1024;
    bool replace = false;

    fn run(const diagnostics &diagnostics) const -> int {
        const let archive =
            nuxe::ds2_archive::open(header_path, data_path, key_path, jobs);
        if (!archive) {
            diagnostics.report(archive.error(), header_path);
            return 1;
        }
        const let result = archive->extract_file(
            bucket, file, output_path,
            {.buffer_size = buffer_size,
             .overwrite = replace ? nuxe::overwrite_policy::replace
                                  : nuxe::overwrite_policy::fail});
        if (!result) {
            diagnostics.report(result.error(), output_path);
            return 1;
        }
        std::println("extracted {} bytes to {}", *result, output_path);
        return 0;
    }

    fn attach(CLI::App &app, int &exit_code, const diagnostics &diagnostics)
        -> void {
        let *command = app.add_subcommand(
            "extract", "extract and decrypt one DS2 payload");
        command
            ->add_option("header", header_path,
                         "encrypted or decrypted BHD5 header")
            ->required();
        command->add_option("data", data_path, "matching BDT data archive")
            ->required();
        command
            ->add_option("--bucket", bucket,
                         "zero-based bucket index from inspect")
            ->required()
            ->check(size_option(true));
        command
            ->add_option("--file", file,
                         "zero-based file index within the bucket")
            ->required()
            ->check(size_option(true));
        command
            ->add_option("-o,--output", output_path,
                         "destination file; creates parent directories")
            ->required();
        command->add_option(
            "--key", key_path,
            "matching RSA public key PEM for encrypted headers");
        command
            ->add_option("--jobs", jobs,
                         "maximum RSA header workers (default: 1)")
            ->check(size_option());
        command
            ->add_option("--buffer-size", buffer_size,
                         "fixed copy buffer in bytes (default: 1048576)")
            ->check(size_option());
        command->add_flag(
            "--replace", replace,
            "replace an existing output; failures may leave it partial");
        command->callback(
            [this, &exit_code, &diagnostics] { exit_code = run(diagnostics); });
    }
};

} // namespace nuxe::cli
