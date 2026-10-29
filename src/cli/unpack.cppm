module;

#include <CLI/CLI.hpp>
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <linux/ioprio.h>
#include <print>
#include <span>
#include <string>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include <nuxe/macros.h>

export module nuxe.cli.unpack;

import nuxe.cli.diagnostics;
import nuxe.cli.archives;
import nuxe.cli.header;
import nuxe.cli.options;
import nuxe.extract.plan;
import nuxe.extract.planner;
import nuxe.extract.worker;
import nuxe.extract.executor;

export namespace nuxe::cli {

struct unpack_command {
    std::string input_path;
    std::string data_override;
    std::string output_root;
    std::string key_override;
    std::string copying = "auto";
    std::size_t jobs = 0;
    std::size_t buffer_size = 1024 * 1024;
    bool replace = false;
    bool eat_all_cpu = false;

    fn run(const diagnostics &diagnostics) const -> int {
        const let available = std::max(1U, std::thread::hardware_concurrency());
        const let worker_limit =
            jobs == 0 ? static_cast<std::size_t>(available) : jobs;
        if (!eat_all_cpu) {
            errno = 0;
            const let priority = ::getpriority(PRIO_PROCESS, 0);
            if ((priority == -1 && errno != 0) ||
                (priority < 10 && ::setpriority(PRIO_PROCESS, 0, 10) < 0)) {
                diagnostics.report(
                    "could not lower CPU priority",
                    std::error_code{errno, std::generic_category()}.message());
                return 1;
            }
            if (::syscall(SYS_ioprio_set, IOPRIO_WHO_PROCESS, 0,
                          IOPRIO_PRIO_VALUE(IOPRIO_CLASS_IDLE, 0)) < 0) {
                diagnostics.report(
                    "could not lower I/O priority",
                    std::error_code{errno, std::generic_category()}.message());
                return 1;
            }
        }
        let error = std::error_code{};
        const let directory = std::filesystem::is_directory(input_path, error);
        if (error && error != std::errc::no_such_file_or_directory) {
            diagnostics.report("could not inspect input", error.message(),
                               input_path);
            return 1;
        }
        if (directory) {
            if (!data_override.empty() || !key_override.empty()) {
                diagnostics.report("directory input discovers data archives "
                                   "and keys automatically",
                                   "omit the data argument and --key",
                                   input_path);
                return 1;
            }
            const let archives = discover_archives(input_path, diagnostics);
            if (!archives)
                return 1;
            for (const let &paths : *archives) {
                const let destination =
                    std::filesystem::path{output_root} / paths.header.stem();
                if (run_archive(paths, destination, worker_limit, diagnostics,
                                *archives) != 0)
                    return 1;
            }
            return 0;
        }
        let paths = paths_for_header(input_path);
        if (!data_override.empty())
            paths.data = data_override;
        if (!key_override.empty())
            paths.key = key_override;
        return run_archive(paths, output_root, worker_limit, diagnostics,
                           std::span{&paths, 1});
    }

    fn run_archive(const archive_paths &paths,
                   const std::filesystem::path &destination,
                   std::size_t worker_limit, const diagnostics &diagnostics,
                   std::span<const archive_paths> inputs) const -> int {
        const let header_path = paths.header.string();
        const let data_path = paths.data.string();
        const let key_path = paths.key.string();
        const let output_path = destination.string();
        const let archive =
            load_ds2_header(header_path, key_path, worker_limit, diagnostics);
        if (!archive)
            return 1;

        let error = std::error_code{};
        const let size = std::filesystem::file_size(data_path, error);
        if (error) {
            diagnostics.report("could not inspect data archive",
                               error.message(), data_path);
            return 1;
        }
        const let root = std::filesystem::path{output_path}.lexically_normal();
        if (root.empty()) {
            diagnostics.report("invalid output directory", {}, output_path);
            return 1;
        }

        let count = std::size_t{};
        for (const let &bucket : archive->buckets)
            count += bucket.files.size();
        let requests = std::vector<nuxe::extraction_request>{};
        requests.reserve(count);
        for (std::size_t bucket = 0; bucket < archive->buckets.size();
             ++bucket) {
            const let &files = archive->buckets[bucket].files;
            for (std::size_t file = 0; file < files.size(); ++file) {
                requests.emplace_back(nuxe::extraction_request{
                    std::cref(files[file]),
                    root / std::format("{}/{}.bin", bucket, file),
                });
            }
        }
        const let plan =
            nuxe::plan_files(requests, data_path, size,
                             nuxe::group_limits{64, 64ULL * 1024 * 1024});
        if (!plan) {
            diagnostics.report(plan.error(), output_path);
            return 1;
        }

        for (const let &entry : plan->entries) {
            for (const let &source : inputs) {
                for (const let *input :
                     {&source.header, &source.data, &source.key}) {
                    if (input->empty())
                        continue;
                    const let same = std::filesystem::equivalent(
                        *input, entry.output_path, error);
                    if (same) {
                        diagnostics.report(
                            "output would overwrite an input file", {},
                            entry.output_path.string());
                        return 1;
                    }
                    if (error &&
                        error != std::errc::no_such_file_or_directory) {
                        diagnostics.report("could not check output destination",
                                           error.message(),
                                           entry.output_path.string());
                        return 1;
                    }
                }
            }
        }

        const let worker_count = std::min(worker_limit, plan->groups.size());
        let workers = std::vector<nuxe::extraction_worker>{};
        workers.reserve(worker_count);
        for (std::size_t index = 0; index < worker_count; ++index) {
            let worker = nuxe::prepare_worker(*plan, buffer_size,
                                              copying == "buffered"
                                                  ? nuxe::copy_mode::buffered
                                                  : nuxe::copy_mode::automatic);
            if (!worker) {
                diagnostics.report(worker.error(), output_path);
                return 1;
            }
            workers.emplace_back(std::move(*worker));
        }
        for (const let &directory : plan->directories) {
            std::filesystem::create_directories(directory, error);
            if (error) {
                diagnostics.report("could not create output directory",
                                   error.message(), directory.string());
                return 1;
            }
        }
        const let outcome =
            nuxe::extract_buffered(*plan, workers,
                                   replace ? nuxe::overwrite_policy::replace
                                           : nuxe::overwrite_policy::fail);
        if (!outcome) {
            diagnostics.report(outcome.error(), output_path);
            return 1;
        }

        let files = std::size_t{};
        let bytes = std::uint64_t{};
        for (const let &worker : workers) {
            files += worker.progress.files_completed;
            bytes += worker.progress.bytes_written;
        }
        std::println("unpacked {} files ({} bytes) to {}", files, bytes,
                     root.string());
        return 0;
    }

    fn attach(CLI::App &app, int &exit_code, const diagnostics &diagnostics)
        -> void {
        let *command = app.add_subcommand(
            "unpack",
            "unpack DS2 archives from a game directory or BHD5 header");
        command
            ->add_option("input", input_path,
                         "game directory or encrypted/decrypted BHD5 header")
            ->required();
        command->add_option("data", data_override,
                            "BDT archive (inferred when omitted)");
        command
            ->add_option("-o,--output", output_root,
                         "output directory; creates parent directories")
            ->required();
        command->add_option(
            "--key", key_override,
            "matching RSA public key PEM for encrypted headers");
        command
            ->add_option(
                "--jobs", jobs,
                "maximum workers (default: all available hardware threads)")
            ->check(size_option());
        command->add_flag("--eat-all-cpu", eat_all_cpu,
                          "keep inherited CPU and I/O priority instead of "
                          "background priority");
        command
            ->add_option("--buffer-size", buffer_size,
                         "fixed buffer per worker in bytes (default: 1048576)")
            ->check(size_option());
        command
            ->add_option("--copy-mode", copying,
                         "plain-file copying: auto tries kernel copy "
                         "(default), buffered forces the baseline")
            ->check(CLI::IsMember({"auto", "buffered"}));
        command->add_flag(
            "--replace", replace,
            "replace existing outputs; failures may leave them partial");
        command->callback(
            [this, &exit_code, &diagnostics] { exit_code = run(diagnostics); });
    }
};

} // namespace nuxe::cli
