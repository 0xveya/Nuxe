module;

#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include <nuxe/macros.h>
#include <vector>

export module nuxe.format.bhd5;

import nuxe.error;
import nuxe.io.reader;

export import nuxe.format.bhd5.header;
export import nuxe.format.bhd5.metadata;

export namespace nuxe {

struct bhd5_file {
    std::uint64_t path_hash;
    std::uint32_t data_length;
    std::uint64_t data_offset;
    std::uint64_t hash_offset;
    std::uint64_t encryption_offset;
    std::optional<bhd5_file_hash> hash;
    std::optional<bhd5_file_encryption> encryption;
};

struct bhd5_bucket {
    std::vector<bhd5_file> files;
};

struct bhd5 {
    bhd5_format format;
    bool little_endian;
    bool unk05;
    std::string data_salt;
    std::vector<bhd5_bucket> buckets;
};

struct bhd5_bucket_record {
    std::int32_t file_count;
    std::int64_t files_offset;
};

template <bhd5_format Format>
fn read_path_hash(reader &input) -> result<std::uint64_t> {
    if constexpr (Format == bhd5_format::elden_ring) {
        return input.read<std::uint64_t>();
    } else {
        return input.read<std::uint32_t>().transform(
            [](std::uint32_t hash) -> std::uint64_t { return hash; });
    }
}

template <bhd5_format Format>
fn read_bucket_record(reader &input) -> result<bhd5_bucket_record> {
    const let record_offset = input.position();
    let record = bhd5_bucket_record{};

    const let count = input.read<std::int32_t>();
    if (!count)
        return std::unexpected(count.error());
    if (*count < 0)
        return std::unexpected(invalid_header{record_offset});

    record.file_count = *count;

    if constexpr (Format == bhd5_format::dark_souls_remastered) {
        const let marker = input.read<std::int32_t>();
        if (!marker)
            return std::unexpected(marker.error());
        if (*marker != 1)
            return std::unexpected(invalid_header{record_offset + 4});

        const let offset = input.read<std::int64_t>();
        if (!offset)
            return std::unexpected(offset.error());
        if (*offset < 0)
            return std::unexpected(invalid_header{record_offset + 8});

        record.files_offset = *offset;
    } else {
        const let offset = input.read<std::int32_t>();
        if (!offset)
            return std::unexpected(offset.error());
        if (*offset < 0)
            return std::unexpected(invalid_header{record_offset + 4});

        record.files_offset = *offset;
    }

    return record;
}

template <bhd5_format Format>
fn parse_bucket_table(std::span<const std::byte> bytes,
                      const bhd5_header &header)
    -> result<std::vector<bhd5_bucket_record>> {
    if (header.bucket_count < 0)
        return std::unexpected(invalid_header{16});

    constexpr let offset_field =
        Format == bhd5_format::dark_souls_remastered ? 24u : 20u;

    if (header.buckets_offset < 0)
        return std::unexpected(invalid_header{offset_field});

    const let table_offset = static_cast<std::uint64_t>(header.buckets_offset);

    if (table_offset > bytes.size())
        return std::unexpected(invalid_offset{table_offset, bytes.size()});

    let input = reader{
        bytes,
        header.little_endian ? std::endian::little : std::endian::big,
    };

    const let seek = input.seek(static_cast<std::size_t>(table_offset));
    if (!seek)
        return std::unexpected(seek.error());

    constexpr std::size_t record_size =
        Format == bhd5_format::dark_souls_remastered ? 16 : 8;

    const let count = static_cast<std::size_t>(header.bucket_count);
    const let available = bytes.size() - input.position();

    // validat before reserving memory from a file-provided count
    if (count > available / record_size)
        return std::unexpected(invalid_header{16});

    let records = std::vector<bhd5_bucket_record>(count);

    for (std::size_t index = 0; index < count; ++index) {
        const let record = read_bucket_record<Format>(input);
        if (!record)
            return std::unexpected(record.error());

        const let files_offset =
            static_cast<std::uint64_t>(record->files_offset);

        if (files_offset > bytes.size())
            return std::unexpected(invalid_offset{files_offset, bytes.size()});

        records[index] = *record;
    }

    return records;
}

template <bhd5_format Format>
fn read_file_entry(reader &input) -> result<bhd5_file> {
    static_assert(Format == bhd5_format::dark_souls2,
                  "file entry parsing currently supports DS2");

    const let start = input.position();
    let file = bhd5_file{};

    const let hash = read_path_hash<Format>(input);
    if (!hash)
        return std::unexpected(hash.error());
    file.path_hash = *hash;

    const let length = input.read<std::int32_t>();
    if (!length)
        return std::unexpected(length.error());
    if (*length < 0)
        return std::unexpected(invalid_header{start + 4});
    file.data_length = static_cast<std::uint32_t>(*length);

    const let offset = input.read<std::int64_t>();
    if (!offset)
        return std::unexpected(offset.error());
    if (*offset < 0)
        return std::unexpected(invalid_header{start + 8});
    file.data_offset = static_cast<std::uint64_t>(*offset);

    const let hash_offset = input.read<std::int64_t>();
    if (!hash_offset)
        return std::unexpected(hash_offset.error());
    if (*hash_offset < 0)
        return std::unexpected(invalid_header{start + 16});
    file.hash_offset = static_cast<std::uint64_t>(*hash_offset);

    const let encryption_offset = input.read<std::int64_t>();
    if (!encryption_offset)
        return std::unexpected(encryption_offset.error());
    if (*encryption_offset < 0)
        return std::unexpected(invalid_header{start + 24});
    file.encryption_offset = static_cast<std::uint64_t>(*encryption_offset);

    return file;
}

template <bhd5_format Format>
fn parse_bucket_files(std::span<const std::byte> bytes,
                      const bhd5_header &header,
                      const bhd5_bucket_record &record) -> result<bhd5_bucket> {
    static_assert(Format == bhd5_format::dark_souls2,
                  "file entry parsing currently supports DS2");

    let input = reader{
        bytes,
        header.little_endian ? std::endian::little : std::endian::big,
    };

    const let seek = input.seek(static_cast<std::size_t>(record.files_offset));
    if (!seek)
        return std::unexpected(seek.error());

    constexpr std::size_t entry_size = 32;
    const let count = static_cast<std::size_t>(record.file_count);
    const let available = bytes.size() - input.position();

    if (count > available / entry_size)
        return std::unexpected(invalid_header{input.position()});

    let bucket = bhd5_bucket{};
    bucket.files.resize(count);

    for (std::size_t index = 0; index < count; ++index) {
        let file = read_file_entry<Format>(input);
        if (!file)
            return std::unexpected(file.error());

        if (file->hash_offset > bytes.size())
            return std::unexpected(
                invalid_offset{file->hash_offset, bytes.size()});

        if (file->encryption_offset > bytes.size())
            return std::unexpected(
                invalid_offset{file->encryption_offset, bytes.size()});

        if (file->hash_offset != 0) {
            let hash = read_file_hash(
                bytes,
                header.little_endian ? std::endian::little : std::endian::big,
                static_cast<std::size_t>(file->hash_offset));
            if (!hash)
                return std::unexpected(hash.error());
            file->hash = std::move(*hash);
        }
        if (file->encryption_offset != 0) {
            let encryption = read_file_encryption(
                bytes,
                header.little_endian ? std::endian::little : std::endian::big,
                static_cast<std::size_t>(file->encryption_offset));

            if (!encryption)
                return std::unexpected(encryption.error());

            file->encryption = std::move(*encryption);
        }

        bucket.files[index] = std::move(*file);
    }

    return bucket;
}

template <bhd5_format Format>
fn parse_bhd5(std::span<const std::byte> bytes) -> result<bhd5> {
    static_assert(Format == bhd5_format::dark_souls2,
                  "file entry parsing currently supports DS2");

    let header = parse_bhd5_header<Format>(bytes);
    if (!header)
        return std::unexpected(header.error());

    const let table = parse_bucket_table<Format>(bytes, *header);
    if (!table)
        return std::unexpected(table.error());

    let archive = bhd5{
        Format,
        header->little_endian,
        header->unk05,
        std::move(header->data_salt),
        {},
    };
    archive.buckets.resize(table->size());

    for (std::size_t index = 0; index < table->size(); ++index) {
        let bucket =
            parse_bucket_files<Format>(bytes, *header, (*table)[index]);
        if (!bucket)
            return std::unexpected(bucket.error());

        archive.buckets[index] = std::move(*bucket);
    }

    return archive;
}

} // namespace nuxe
