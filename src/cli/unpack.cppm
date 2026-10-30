module;

#include <CLI/CLI.hpp>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <functional>
#include <linux/ioprio.h>
#include <optional>
#include <print>
#include <set>
#include <span>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
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
import nuxe.io.payload;

export namespace nuxe::cli {

fn protected_identities(std::span<const archive_paths> inputs,
                        const diagnostics &diagnostics)
    -> std::optional<std::vector<nuxe::file_identity>> {
    let identities = std::vector<nuxe::file_identity>{};
    for (const let &source : inputs) {
        for (const let *path : {&source.header, &source.data, &source.key}) {
            if (path->empty())
                continue;
            struct stat info{};
            let status = int{};
            do {
                status = ::stat(path->c_str(), &info);
            } while (status < 0 && errno == EINTR);
            if (status < 0) {
                if (errno == ENOENT)
                    continue;
                diagnostics.report(
                    "could not inspect protected input",
                    std::error_code{errno, std::generic_category()}.message(),
                    path->string());
                return std::nullopt;
            }
            identities.push_back({info.st_dev, info.st_ino});
        }
    }
    return identities;
}

struct unpack_statistics {
    double header_seconds{};
    double planning_seconds{};
    double preflight_seconds{};
    double setup_seconds{};
    double directory_seconds{};
    double payload_seconds{};
    std::size_t archives{};
    std::size_t files{};
    std::size_t groups{};
    std::size_t workers{};
    std::set<std::array<std::byte, 16>> keys;
    std::size_t ranges{};
    std::uint64_t plain_bytes{};
    std::uint64_t encrypted_bytes{};
    nuxe::copy_statistics copying;
};

struct unpack_command {
    std::string input_path;
    std::string data_override;
    std::string output_root;
    std::string key_override;
    std::string copying = "buffered";
    std::size_t jobs = 0;
    std::size_t buffer_size = 64 * 1024;
    std::size_t group_files = 64;
    std::size_t group_bytes = 64 * 1024 * 1024;
    std::size_t cfr_min_size = 0;
    bool replace = false;
    bool eat_all_cpu = false;
    bool stats = false;

    fn run(const diagnostics &diagnostics) const -> int {
        const let started = std::chrono::steady_clock::now();
        let statistics = unpack_statistics{};
        const let status = run_extraction(diagnostics, statistics);
        if (stats) {
            const let total = std::chrono::duration<double>(
                                  std::chrono::steady_clock::now() - started)
                                  .count();
            std::println(stderr,
                         "stages (s): header={:.6f} planning={:.6f} "
                         "preflight={:.6f} setup={:.6f} directories={:.6f} "
                         "payload={:.6f} total={:.6f}",
                         statistics.header_seconds, statistics.planning_seconds,
                         statistics.preflight_seconds, statistics.setup_seconds,
                         statistics.directory_seconds,
                         statistics.payload_seconds, total);
            std::println(stderr,
                         "counts: archives={} files={} groups={} workers={} "
                         "plain_bytes={} "
                         "encrypted_bytes={} unique_keys={} ranges={}",
                         statistics.archives, statistics.files,
                         statistics.groups, statistics.workers,
                         statistics.plain_bytes, statistics.encrypted_bytes,
                         statistics.keys.size(), statistics.ranges);
            const let &copying_stats = statistics.copying;
            std::println(stderr,
                         "copy: cfr_calls={} cfr_bytes={} cfr_fallbacks={} "
                         "buffered_bytes={} cfr_ns={}",
                         copying_stats.cfr_calls, copying_stats.cfr_bytes,
                         copying_stats.cfr_fallbacks,
                         copying_stats.buffered_bytes, copying_stats.cfr_ns);
            for (const let &[ code, occurrences ] : copying_stats.cfr_errors)
                std::println(
                    stderr, "copy errno: code={} count={} message={}", code,
                    occurrences,
                    std::error_code{code, std::generic_category()}.message());
        }
        return status;
    }

    fn run_extraction(const diagnostics &diagnostics,
                      unpack_statistics &statistics) const -> int {
        const let available = std::max(1U, std::thread::hardware_concurrency());
        const let worker_limit =
            jobs == 0 ? static_cast<std::size_t>(std::min(available, 2U))
                      : jobs;
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
            const let identities = protected_identities(*archives, diagnostics);
            if (!identities)
                return 1;
            for (const let &paths : *archives) {
                const let destination =
                    std::filesystem::path{output_root} / paths.header.stem();
                if (run_archive(paths, destination, worker_limit, diagnostics,
                                *identities, statistics) != 0)
                    return 1;
            }
            return 0;
        }
        let paths = paths_for_header(input_path);
        if (!data_override.empty())
            paths.data = data_override;
        if (!key_override.empty())
            paths.key = key_override;
        const let identities =
            protected_identities(std::span{&paths, 1}, diagnostics);
        if (!identities)
            return 1;
        return run_archive(paths, output_root, worker_limit, diagnostics,
                           *identities, statistics);
    }

    fn run_archive(const archive_paths &paths,
                   const std::filesystem::path &destination,
                   std::size_t worker_limit, const diagnostics &diagnostics,
                   std::span<const nuxe::file_identity> inputs,
                   unpack_statistics &statistics) const -> int {
        let stage_started = std::chrono::steady_clock::now();
        const let record = [&](double &seconds) {
            if (!stats)
                return;
            const let now = std::chrono::steady_clock::now();
            seconds +=
                std::chrono::duration<double>(now - stage_started).count();
            stage_started = now;
        };
        const let header_path = paths.header.string();
        const let data_path = paths.data.string();
        const let key_path = paths.key.string();
        const let output_path = destination.string();
        const let archive =
            load_ds2_header(header_path, key_path, worker_limit, diagnostics);
        if (!archive)
            return 1;
        record(statistics.header_seconds);

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
                             nuxe::group_limits{group_files, group_bytes});
        if (!plan) {
            diagnostics.report(plan.error(), output_path);
            return 1;
        }
        record(statistics.planning_seconds);

        record(statistics.preflight_seconds);
        const let worker_count = std::min(worker_limit, plan->groups.size());
        statistics.workers = std::max(statistics.workers, worker_count);
        let workers = std::vector<nuxe::extraction_worker>{};
        workers.reserve(worker_count);
        for (std::size_t index = 0; index < worker_count; ++index) {
            let worker = nuxe::prepare_worker(*plan, buffer_size,
                                              copying == "buffered"
                                                  ? nuxe::copy_mode::buffered
                                                  : nuxe::copy_mode::automatic,
                                              inputs, cfr_min_size, stats);
            if (!worker) {
                diagnostics.report(worker.error(), output_path);
                return 1;
            }
            workers.emplace_back(std::move(*worker));
        }
        record(statistics.setup_seconds);
        for (const let &directory : plan->directories) {
            std::filesystem::create_directories(directory, error);
            if (error) {
                diagnostics.report("could not create output directory",
                                   error.message(), directory.string());
                return 1;
            }
        }
        record(statistics.directory_seconds);
        const let outcome =
            nuxe::extract_buffered(*plan, workers,
                                   replace ? nuxe::overwrite_policy::replace
                                           : nuxe::overwrite_policy::fail);
        record(statistics.payload_seconds);
        if (stats) {
            for (const let &worker : workers) {
                const let &copying_stats = worker.statistics;
                statistics.copying.cfr_calls += copying_stats.cfr_calls;
                statistics.copying.cfr_bytes += copying_stats.cfr_bytes;
                statistics.copying.cfr_fallbacks += copying_stats.cfr_fallbacks;
                statistics.copying.buffered_bytes +=
                    copying_stats.buffered_bytes;
                statistics.copying.cfr_ns += copying_stats.cfr_ns;
                for (const let &[ code, occurrences ] :
                     copying_stats.cfr_errors)
                    statistics.copying.cfr_errors[code] += occurrences;
            }
        }
        if (!outcome) {
            diagnostics.report(outcome.error(), output_path);
            return 1;
        }
        if (stats) {
            ++statistics.archives;
            statistics.files += plan->entries.size();
            statistics.groups += plan->groups.size();
            statistics.keys.insert(plan->keys.begin(), plan->keys.end());
            for (const let &entry : plan->entries) {
                statistics.ranges += entry.encrypted_ranges.size();
                let encrypted = std::uint64_t{};
                for (const let &range : entry.encrypted_ranges)
                    encrypted +=
                        static_cast<std::uint64_t>(range.end - range.start);
                statistics.encrypted_bytes += encrypted;
                statistics.plain_bytes += entry.data_length - encrypted;
            }
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
                "maximum workers (default: up to 2 available hardware threads)")
            ->check(size_option());
        command->add_flag("--eat-all-cpu", eat_all_cpu,
                          "keep inherited CPU and I/O priority instead of "
                          "background priority");
        command
            ->add_option("--buffer-size", buffer_size,
                         "fixed buffer per worker in bytes (default: 65536)")
            ->check(size_option());
        command
            ->add_option("--group-files", group_files,
                         "experimental maximum files per group (default: 64)")
            ->check(size_option());
        command
            ->add_option(
                "--group-bytes", group_bytes,
                "experimental maximum bytes per group (default: 67108864)")
            ->check(size_option());
        command
            ->add_option(
                "--cfr-min-size", cfr_min_size,
                "minimum plain-file bytes for auto kernel copying (default: 0)")
            ->check(size_option(true));
        command->add_flag(
            "--stats", stats,
            "print extraction stage timings and counters to stderr");
        command
            ->add_option("--copy-mode", copying,
                         "plain-file copying: buffered (default), auto tries "
                         "kernel copy with fallback")
            ->check(CLI::IsMember({"auto", "buffered"}));
        command->add_flag(
            "--replace", replace,
            "replace existing outputs; failures may leave them partial");
        command->callback(
            [this, &exit_code, &diagnostics] { exit_code = run(diagnostics); });
    }
};

} // namespace nuxe::cli
