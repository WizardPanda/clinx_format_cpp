# clxcpp — project notes

`clxcpp` is a dependency-free C++17 library + CLI for parsing Clinx
chemiluminescence instrument `.clx` files (metadata + 16-bit
bright-field/fluorescence images), ported from the Python `clxparser` project.

## Commands

- Configure + build (MSVC, Ninja):
  `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`
  `cmake --build build`
- Run tests: `ctest --test-dir build --output-on-failure`
- Run the CLI: `build\clx.exe info <file.clx>` (or `extract`, `preview`)
- Cross-check parity against the reference Python implementation:
  `python -m clxparser info <file.clx>` (run from the sibling `clinx_format` tree)

## Documentation

- Docs live in `docs/`. Every document has an English (`docs/<name>.md`) and a
  Chinese (`docs/<name>.zh-CN.md`) version that must stay in sync.
- When editing a doc, update BOTH language versions (and cross-links) in the
  same commit; keep section structure and values identical, translating only
  prose.
- If a doc changes and the other language was not updated, flag it explicitly.
- The format spec is shared with the Python project; keep it in sync.

## Conventions

- Stdlib-only C++17. No external dependencies except the vendored single-file
  `third_party/miniz.h` (public domain), used for PNG deflate + CRC32. Do not
  add dependencies without an explicit reason.
- Little-endian throughout. See `include/clxcpp/clx.hpp` and `docs/` for the
  full format spec.
- Keep the pixel-identity guarantees: exported TIFFs and 16-bit PNGs must carry
  exactly the raw 16-bit pixels from the `.clx` capture, byte-identical to the
  instrument's exported TIFF (verified in `tests/`). The TIFF *container* is
  intentionally a simple standard layout — do not reintroduce byte-level
  replication of the vendor's writer.
- The 8-bit preview path must match the Python implementation's numpy-based
  percentile scaling byte-for-byte.
- Commit each feature separately with a clear message.

## Golden files

`tests/data/` holds the two real instrument samples (`.clx` + their exported
`.tif` files) copied from the `clxparser` project. They are the source of truth
for pixel-identity checks.
