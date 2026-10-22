module;

#include <CLI/CLI.hpp>
#include <cstddef>
#include <filesystem>
#include <print>
#include <string>
#include <system_error>

#include <nuxe/macros.h>

export module nuxe.cli.extract;

import nuxe.cli.diagnostics;
import nuxe.cli.header;
import nuxe.cli.options;
import nuxe.extract.plan;
import nuxe.extract.planner;
import nuxe.extract.worker;
import nuxe.format.bhd5;

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
            load_ds2_header(header_path, key_path, jobs, diagnostics);
        if (!archive)
            return 1;
        if (bucket >= archive->buckets.size()) {
            diagnostics.report("bucket index is out of range",
                               "use a bucket index from inspect", "--bucket");
            return 1;
        }
        const let &entries = archive->buckets[bucket].files;
        if (file >= entries.size()) {
            diagnostics.report(
                "file index is out of range",
                "use a file index within the selected bucket from inspect",
                "--file");
            return 1;
        }

        let error = std::error_code{};
        const let size = std::filesystem::file_size(data_path, error);
        if (error) {
            diagnostics.report("could not inspect data archive",
                               error.message(), data_path);
            return 1;
        }
        const let plan =
            nuxe::plan_file(entries[file], data_path, size, output_path);
        if (!plan) {
            diagnostics.report(plan.error(), output_path);
            return 1;
        }
        const let &destination = plan->entries[0].output_path;
        for (const let &input : {header_path, key_path}) {
            if (input.empty())
                continue;
            const let same =
                std::filesystem::equivalent(input, destination, error);
            if (same) {
                diagnostics.report("output would overwrite an input file", {},
                                   output_path);
                return 1;
            }
            if (error && error != std::errc::no_such_file_or_directory) {
                diagnostics.report("could not check output destination",
                                   error.message(), output_path);
                return 1;
            }
        }
        let worker = nuxe::prepare_worker(*plan, buffer_size);
        if (!worker) {
            diagnostics.report(worker.error(), output_path);
            return 1;
        }
        for (const let &directory : plan->directories) {
            std::filesystem::create_directories(directory, error);
            if (error) {
                diagnostics.report("could not create output directory",
                                   error.message(), directory.string());
                return 1;
            }
        }
        const let result =
            nuxe::run_group(*plan, plan->groups[0], *worker,
                            replace ? nuxe::overwrite_policy::replace
                                    : nuxe::overwrite_policy::fail);
        if (!result) {
            diagnostics.report(result.error(), output_path);
            return 1;
        }
        std::println("extracted {} bytes to {}", worker->progress.bytes_written,
                     plan->entries[0].output_path.string());
        return 0;
    }

    fn attach(CLI::App &app, int &exit_code, const diagnostics &diagnostics)
        -> void {
        let *command = app.add_subcommand(
            "extract", "extract one unencrypted DS2 payload");
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
