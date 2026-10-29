# nuxe

A Linux-focused fork of [JKAnderson/Nuxe](https://github.com/JKAnderson/Nuxe),
rewritten in C++23 with native modules and Xmake. Mostly a side project because
I'm bored and want to make archive I/O fast.

Currently supports Dark Souls II, including Scholar of the First Sin: inspecting
BHD5 headers, extracting individual files, and unpacking BHD/BDT archive pairs.
Extracted files use bucket/file indices rather than original filenames. Other
games, filename dictionaries, and executable patching aren't implemented yet.

## Build

Requires Linux, [mise](https://mise.jdx.dev/), and [Xmake](https://xmake.io/).
Xmake resolves the C++ dependencies.

```nu
mise trust
mise install
mise run build
```

Use `mise run dev` for a debug build, `mise run format` for formatting, and
`mise run compiledb` to regenerate clangd's compilation database.

## Usage

Examples use Nushell. Set `game` to your installed game's directory:

```nu
let game = "/path/to/Dark Souls II Scholar of the First Sin/Game"
let header = ($game | path join "LqMapEbl.bhd")
let data = ($game | path join "LqMapEbl.bdt")
let key = ($game | path join "LqMapKeyCode.pem")
let out = ($env.HOME | path join "ds2-unpacked/LqMap")

^build/linux/x86_64/release/nuxe inspect $header --key $key
^build/linux/x86_64/release/nuxe extract $header $data --key $key --bucket 0 --file 0 --output ./payload.bin
^build/linux/x86_64/release/nuxe unpack $header $data --key $key --output $out
```

Decrypted headers don't need `--key`. Unpacking writes
`<output>/<bucket>/<file>.bin`; use a separate output directory for each archive.
Inputs are read only, and existing outputs fail unless you pass `--replace`.
A failed replacement can leave a partial file.

Unpacking uses all available hardware threads at background CPU and I/O priority.
Set `--jobs` to limit workers, or `--eat-all-cpu` to keep normal inherited
priority. Each worker uses a fixed buffer, configurable with `--buffer-size`.
Plain files try `copy_file_range()` with buffered fallback; encrypted files use
the buffered AES path. Completion does not imply an fsync to storage.

Run `nuxe <command> --help` for all options.

## Profiling

The project pins Samply through mise. With the system `perf` tool installed:

```nu
let profile_out = ($env.HOME | path join $"nuxe-profile-(random uuid)")
mise run profile -- unpack $header $data --key $key --output $profile_out
```

This builds optimized code with debug symbols and frame pointers, records CPU
stacks, and opens Firefox Profiler in your browser. Leave the terminal running;
Ctrl-C stops the local server. `mise run profile` reopens the latest recording.
Profiles stay local under ignored `.cache/profiles/` unless you upload them.
Unpacked output files remain in the directory you chose.

Samples show CPU activity rather than exact function wall times. Compare real
disk workloads separately from warm-cache or RAM-backed runs.

The original C# source is kept in ignored `old_code/` for reference.
