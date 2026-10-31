module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <stop_token>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nuxe/macros.h>

export module nuxe.archive.ds2;

export import nuxe.error;
export import nuxe.format.bhd5;
export import nuxe.extract.plan;
import nuxe.crypto.header;
import nuxe.io.file;
import nuxe.io.payload;
import nuxe.extract.planner;
import nuxe.extract.worker;

export namespace nuxe {

fn load_ds2_header(const std::filesystem::path &path,
                   const std::filesystem::path &key_path = {},
                   std::size_t jobs = 1, std::stop_token cancellation = {})
    -> result<bhd5> {
    if (cancellation.stop_requested())
        return std::unexpected(operation_cancelled{});
    if (jobs == 0)
        return std::unexpected(extraction_error::invalid_worker_count);
    let input = read_file(path);
    if (!input)
        return std::unexpected(input.error());
    let &bytes = *input;
    constexpr let magic = std::array{std::byte{'B'}, std::byte{'H'},
                                     std::byte{'D'}, std::byte{'5'}};
    const let decrypted = bytes.size() >= magic.size() &&
                          std::equal(magic.begin(), magic.end(), bytes.begin());
    if (!decrypted) {
        if (key_path.empty()) {
            let actual = std::array<std::byte, 4>{};
            std::copy_n(bytes.begin(), std::min(bytes.size(), actual.size()),
                        actual.begin());
            return std::unexpected(invalid_magic{actual});
        }
        const let pem = read_file(key_path);
        if (!pem)
            return std::unexpected(pem.error());
        const let pem_text =
            pem->empty()
                ? std::string_view{}
                : std::string_view{reinterpret_cast<const char *>(pem->data()),
                                   pem->size()};
        const let key = header_key::parse(pem_text);
        if (!key)
            return std::unexpected(key.error());
        const let layout = key->layout(bytes.size());
        if (!layout)
            return std::unexpected(layout.error());
        if (layout->block_count == 0)
            return std::unexpected(invalid_header{0});
        constexpr std::size_t blocks_per_batch = 64;
        const let batch_count =
            1 + (layout->block_count - 1) / blocks_per_batch;
        const let worker_count = std::min(jobs, batch_count);
        let workers = std::vector<header_worker>{};
        workers.reserve(worker_count);
        for (std::size_t index = 0; index < worker_count; ++index) {
            let worker = header_worker::create(*key);
            if (!worker)
                return std::unexpected(worker.error());
            workers.emplace_back(std::move(*worker));
        }
        let plaintext = std::vector<std::byte>(layout->output_bytes);
        const let decrypted_header = decrypt_header(
            bytes, plaintext, workers, blocks_per_batch, cancellation);
        if (!decrypted_header)
            return std::unexpected(decrypted_header.error());
        bytes = std::move(plaintext);
    }
    if (cancellation.stop_requested())
        return std::unexpected(operation_cancelled{});
    return parse_bhd5<bhd5_format::dark_souls2>(bytes);
}

struct file_extraction_options {
    std::size_t buffer_size = 64 * 1024;
    overwrite_policy overwrite = overwrite_policy::fail;
};

// Load once for browsing; each extraction owns its worker and output file.
class ds2_archive {
  public:
    static fn open(const std::filesystem::path &header_path,
                   const std::filesystem::path &data_path,
                   const std::filesystem::path &key_path = {},
                   std::size_t jobs = 1, std::stop_token cancellation = {})
        -> result<ds2_archive> {
        let header = load_ds2_header(header_path, key_path, jobs, cancellation);
        if (!header)
            return std::unexpected(header.error());
        const let data = open_archive(data_path);
        if (!data)
            return std::unexpected(data.error());
        return ds2_archive{std::move(*header), header_path, data_path,
                           key_path};
    }

    fn header() const noexcept -> const bhd5 & { return header_; }

    fn extract_file(std::size_t bucket, std::size_t file,
                    const std::filesystem::path &output_path,
                    file_extraction_options options = {},
                    std::stop_token cancellation = {}) const
        -> result<std::uint64_t> {
        if (cancellation.stop_requested())
            return std::unexpected(operation_cancelled{});
        if (bucket >= header_.buckets.size())
            return std::unexpected(extraction_error::invalid_bucket_index);
        const let &entries = header_.buckets[bucket].files;
        if (file >= entries.size())
            return std::unexpected(extraction_error::invalid_file_index);
        const let input = open_archive(data_path_);
        if (!input)
            return std::unexpected(input.error());
        const let plan =
            plan_file(entries[file], data_path_, input->size(), output_path);
        if (!plan)
            return std::unexpected(plan.error());
        let identities = std::vector<file_identity>{};
        for (const let *source : {&header_path_, &key_path_}) {
            if (source->empty())
                continue;
            const let protected_file = open_archive(*source);
            if (!protected_file)
                return std::unexpected(protected_file.error());
            identities.push_back(
                {protected_file->info().st_dev, protected_file->info().st_ino});
        }
        let worker = prepare_worker(*plan, options.buffer_size,
                                    copy_mode::buffered, identities);
        if (!worker)
            return std::unexpected(worker.error());
        for (const let &directory : plan->directories) {
            let error = std::error_code{};
            std::filesystem::create_directories(directory, error);
            if (error)
                return std::unexpected(
                    io_error{io_operation::open, directory, error});
        }
        const let extracted = run_group(*plan, plan->groups[0], *worker,
                                        options.overwrite, cancellation);
        if (!extracted)
            return std::unexpected(extracted.error());
        return worker->progress.bytes_written;
    }

  private:
    ds2_archive(bhd5 header, std::filesystem::path header_path,
                std::filesystem::path data_path, std::filesystem::path key_path)
        : header_(std::move(header)), header_path_(std::move(header_path)),
          data_path_(std::move(data_path)), key_path_(std::move(key_path)) {}

    bhd5 header_;
    std::filesystem::path header_path_;
    std::filesystem::path data_path_;
    std::filesystem::path key_path_;
};

} // namespace nuxe
