module;

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <utility>

#include <nuxe/macros.h>
#include <vector>

export module nuxe.format.bhd5;

import nuxe.error;
import nuxe.io.reader;

export namespace nuxe {

enum class bhd5_format {
    dark_souls,
    dark_souls_remastered,
    dark_souls2,
    dark_souls3,
    elden_ring,
};

struct bhd5_file {
    std::uint64_t path_hash;
    std::uint32_t data_length;
    std::uint64_t data_offset;
    std::uint64_t hash_offset;
    std::uint64_t encryption_offset;
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

struct bhd5_header {
    bool little_endian;
    bool unk05;
    std::int32_t bucket_count;
    std::int64_t buckets_offset;
    std::string data_salt;
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
fn parse_bhd5_header(std::span<const std::byte> bytes) -> result<bhd5_header> {
    // i dont worry abt optmizations yet in future i shoudbt be spamming read
    // syscall as much tho but first i want sum to work future veya can worry
    // about makeing it not dogshit slow
    let input = reader(bytes);
    let header = bhd5_header{};

    const let magic = input.read_bytes(4);
    if (!magic)
        return std::unexpected(magic.error());

    const let actual = std::array{
        (*magic)[0],
        (*magic)[1],
        (*magic)[2],
        (*magic)[3],
    };
    const let expected = std::array{
        std::byte{'B'},
        std::byte{'H'},
        std::byte{'D'},
        std::byte{'5'},
    };
    if (actual != expected)
        return std::unexpected(invalid_magic{actual});

    const let endian = input.read<std::uint8_t>();
    if (!endian)
        return std::unexpected(endian.error());

    if (*endian != 0 && *endian != 0xff)
        return std::unexpected(invalid_header{4});

    header.little_endian = *endian != 0;
    input.set_endian(header.little_endian ? std::endian::little
                                          : std::endian::big);

    const let flag = input.read<std::uint8_t>();
    if (!flag)
        return std::unexpected(flag.error());

    if (*flag > 1)
        return std::unexpected(invalid_header{5});

    header.unk05 = *flag != 0;

    const let reserved = input.read<std::uint16_t>();
    if (!reserved)
        return std::unexpected(reserved.error());
    if (*reserved != 0)
        return std::unexpected(invalid_header{6});

    const let version = input.read<std::int32_t>();
    if (!version)
        return std::unexpected(version.error());
    if (*version != 1)
        return std::unexpected(invalid_header{8});

    const let ignored = input.read<std::int32_t>();
    if (!ignored)
        return std::unexpected(ignored.error());

    const let count = input.read<std::int32_t>();
    if (!count)
        return std::unexpected(count.error());
    if (*count < 0)
        return std::unexpected(invalid_header{16});
    header.bucket_count = *count;

    if constexpr (Format == bhd5_format::dark_souls_remastered) {
        const let padding = input.read<std::int32_t>();
        if (!padding)
            return std::unexpected(padding.error());
        if (*padding != 0)
            return std::unexpected(invalid_header{20});

        const let offset = input.read<std::int64_t>();
        if (!offset)
            return std::unexpected(offset.error());
        if (*offset < 0)
            return std::unexpected(invalid_header{24});
        header.buckets_offset = *offset;
    } else {
        const let offset = input.read<std::int32_t>();
        if (!offset)
            return std::unexpected(offset.error());
        if (*offset < 0)
            return std::unexpected(invalid_header{20});
        header.buckets_offset = *offset;
    }

    const let buckets_offset =
        static_cast<std::uint64_t>(header.buckets_offset);
    if (buckets_offset > bytes.size())
        return std::unexpected(invalid_offset{buckets_offset, bytes.size()});

    if constexpr (Format == bhd5_format::dark_souls2 ||
                  Format == bhd5_format::dark_souls3 ||
                  Format == bhd5_format::elden_ring) {
        const let length_offset = input.position();
        const let length = input.read<std::int32_t>();
        if (!length)
            return std::unexpected(length.error());
        if (*length < 0)
            return std::unexpected(invalid_header{length_offset});

        const let salt = input.read_bytes(static_cast<std::size_t>(*length));
        if (!salt)
            return std::unexpected(salt.error());

        header.data_salt.assign(reinterpret_cast<const char *>(salt->data()),
                                salt->size());
    }

    return header;
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

    let records = std::vector<bhd5_bucket_record>{};
    records.reserve(count);

    for (std::size_t index = 0; index < count; ++index) {
        const let record = read_bucket_record<Format>(input);
        if (!record)
            return std::unexpected(record.error());

        const let files_offset =
            static_cast<std::uint64_t>(record->files_offset);

        if (files_offset > bytes.size())
            return std::unexpected(invalid_offset{files_offset, bytes.size()});

        records.emplace_back(record->file_count, record->files_offset);
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
    bucket.files.reserve(count);

    for (std::size_t index = 0; index < count; ++index) {
        const let file = read_file_entry<Format>(input);
        if (!file)
            return std::unexpected(file.error());

        if (file->hash_offset > bytes.size())
            return std::unexpected(
                invalid_offset{file->hash_offset, bytes.size()});

        if (file->encryption_offset > bytes.size())
            return std::unexpected(
                invalid_offset{file->encryption_offset, bytes.size()});

        bucket.files.push_back(*file);
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
    archive.buckets.reserve(table->size());

    for (const let &record : *table) {
        let bucket = parse_bucket_files<Format>(bytes, *header, record);
        if (!bucket)
            return std::unexpected(bucket.error());

        archive.buckets.push_back(std::move(*bucket));
    }

    return archive;
}

} // namespace nuxe
