# nuxe

C++23 Linux archive extractor in development. Uses native modules, Clang and Xmake,
matching the local C++ projects. Xmake must be available on PATH.

```nu
mise trust
mise run dev
mise run build
mise run compiledb
mise run format
```

CLI11 2.7.2 is supplied by xrepo as a header-only dependency. `nuxe --help`
prints usage; unknown arguments produce a diagnostic and nonzero exit code.
Command adapters live in `src/cli`; `src/main.cpp` registers them and handles
process-level failures. `include/nuxe/macros.h` and `nuxe.match` follow the local
shitnet conventions.

All command errors use the shared `nuxe.cli.diagnostics` module and print plain
`nuxe: error: ...` messages to stderr, including the relevant path and cause.
CLI11 handles argument parsing. Help and JSON go to stdout.

Inspect a decrypted DS2 header:

```nu
mise exec -- xmake run nuxe inspect reference/output/LqMapEbl.decrypted.bhd
```

The BHD5 header, archive, and metadata readers live in separate native modules in
`src/format`. The binary reader consumes memory spans and performs no file I/O.

Inspect an encrypted DS2 header with its matching public key:

```nu
let game = "/mnt/data/SteamLibrary/steamapps/common/Dark Souls II Scholar of the First Sin/Game"
^build/linux/x86_64/release/nuxe inspect ($game | path join "LqMapEbl.bhd") --key ($game | path join "LqMapKeyCode.pem") --jobs 4
```

`inspect` recognizes decrypted input by its `BHD5` signature. Encrypted input
requires `--key`. `--jobs` is a positive maximum worker count and defaults to 1;
small headers use fewer workers. JSON goes to stdout and failures go to stderr
with a nonzero exit status. The command reads inputs without changing them.
The current format support is DS2, including Scholar of the First Sin.

Compare with the local Coremats reference after `mise run build`:

```nu
let game = "/mnt/data/SteamLibrary/steamapps/common/Dark Souls II Scholar of the First Sin/Game"
let out = (mktemp -d | str trim)
let header = ($game | path join "LqMapEbl.bhd")
let key = ($game | path join "LqMapKeyCode.pem")
^build/linux/x86_64/release/nuxe inspect $header --key $key --jobs 4 | save ($out | path join "nuxe.json")
dotnet run --project reference -- $header DarkSouls2 ($out | path join "coremats") $key
let comparable = 'del(.buckets[].files[].hash_offset, .buckets[].files[].encryption_offset, .buckets[].files[].unpadded_data_length)'
jq -S $comparable ($out | path join "nuxe.json") | save ($out | path join "nuxe.sorted.json")
jq -S $comparable ($out | path join "coremats/LqMapEbl.json") | save ($out | path join "coremats.sorted.json")
diff -u ($out | path join "coremats.sorted.json") ($out | path join "nuxe.sorted.json")
```

`diff` prints nothing when the metadata matches. Normalization removes Nuxe's
metadata-record offsets, which Coremats does not emit, and Coremats' unpadded
length field, which is zero for these DS2 records. Bucket/file order and all
shared metadata remain part of the comparison. The ignored `reference` project requires .NET 10.

RSA header decryption uses OpenSSL 3, private worker state, and atomic batch
dispatch into a final-sized plaintext buffer. File reads live in `src/io/file.cppm`.
Single-file unencrypted planning and buffered copying are available as library
APIs in `src/extract`; payload I/O ownership lives in `src/io/payload.cppm`.
`prepare_worker` allocates the entire buffer before copying. `run_group` reuses
that buffer without growing containers; output parent directories must already
exist. Paths in the immutable plan must remain alive until I/O owners are closed.
Newly created partial outputs are removed on failure on a best-effort basis.
With replacement enabled, an existing output can be left partial after failure.
Completion checks close errors but does not promise an fsync to storage.

Extract one unencrypted payload using zero-based indices from `inspect`:

```nu
let game = "/mnt/data/SteamLibrary/steamapps/common/Dark Souls II Scholar of the First Sin/Game"
let out = ((mktemp -d | str trim) | path join "payload.bin")
^build/linux/x86_64/release/nuxe extract ($game | path join "LqMapEbl.bhd") ($game | path join "LqMapEbl.bdt") --key ($game | path join "LqMapKeyCode.pem") --bucket 0 --file 0 --output $out --buffer-size 4096
ls $out
```

`--output` names one file, and the command creates missing parent directories.
Existing outputs fail unless `--replace` is supplied. `--buffer-size` is a positive
byte count, defaults to 1 MiB, and is allocated before copying. `--jobs` only
controls RSA header decryption; payload copying currently uses one worker.
Encrypted payload entries are rejected before creating output directories.
Decrypted headers work without `--key`. Run `nuxe extract --help` for the options.

AES payload decryption and the parallel extraction executor remain stubs; config, binder lookup, mapped I/O, and patch
modules remain scaffolds.

The original [JKAnderson/Nuxe](https://github.com/JKAnderson/Nuxe) checkout is in
`old_code/`, which is ignored by Git. It remains available in the fork's history.
