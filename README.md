# nuxe

C++23 module scaffold for a Linux archive extractor. Uses Clang and Xmake,
matching the local C++ projects. Xmake must be available on PATH.

```sh
mise trust
mise run dev
mise run run
mise run build
mise run compiledb
mise run format
```

The executable currently prints `Hello, world!`. `include/nuxe/macros.h` and
`nuxe.match` are copied from the local shitnet project. The other modules are empty:

- `core`: errors
- `io`: mapped files and bounded binary reads
- `format`: BHD5
- `config`: game configuration
- `binder`: path hashes and dictionaries
- `crypto`: header and file decryption
- `extract`: plans, planning, execution, chunks, and workers
- `patch`: patch and restore

The original [JKAnderson/Nuxe](https://github.com/JKAnderson/Nuxe) checkout is in
`old_code/`, which is ignored by Git. It remains available in the fork's history.
Parsing, extraction, crypto, and threading are left for later implementation.
