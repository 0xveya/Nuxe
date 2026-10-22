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

Inspect a decrypted DS2 header:

```nu
mise exec -- xmake run nuxe inspect reference/output/LqMapEbl.decrypted.bhd
```

The BHD5 header, archive, and metadata readers live in separate native modules in
`src/format`. The binary reader consumes memory spans and performs no file I/O.

Crypto and extraction APIs are stubs; their declared methods need definitions
before use. The intended execution model uses immutable plans, allocations
prepared before execution, atomic batch dispatch, and private worker buffers and
crypto state. The next step is OpenSSL RSA header decryption, followed by bounded
buffered extraction. Config, binder lookup, mapped I/O, and patch modules remain
scaffolds.

The original [JKAnderson/Nuxe](https://github.com/JKAnderson/Nuxe) checkout is in
`old_code/`, which is ignored by Git. It remains available in the fork's history.
