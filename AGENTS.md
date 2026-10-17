# Repository Guidelines

## Project Structure & Module Organization

`include/morsedec.h` defines the public C API. `src/` contains the FFTW frontend,
band/channel manager, extraction filters, duration model, element segmentation,
character decoder, word graph and message buffers. Keep internal declarations in
`src/internal.h`; export public functions only with the `md_` prefix.

`tools/` holds command-line utilities using the library; `tools/tui.c` is the
optional ncurses view of `tools/live-common.c` and compiles to no-op stubs when
ncurses is absent. `examples/` contains a minimal consumer. Describe decoder
architecture and implementation tradeoffs in
`docs/design.md`; document public functions in `docs/man/`.

## Build and Development Commands

Install Meson, Ninja and FFTW3 development files; libsndfile enables the CLI.
On Windows, build from an MSYS2 MINGW64 shell using the matching
`mingw-w64-x86_64-gcc`, `-meson`, `-ninja`, `-pkgconf`, `-fftw` and
`-libsndfile` packages. Keep Windows-only code behind `_WIN32` and leave the
Unix code paths unchanged.

- `meson setup build`: configure a development build.
- `meson compile -C build`: build the library, tools and example.
- `meson compile -C build format`: apply clang-format.
- `meson compile -C build format-check`: verify formatting.
- `meson setup build-asan -Db_sanitize=address,undefined -Db_lundef=false`: configure sanitizer checks.

Use `-Dtools=disabled` to build without libsndfile, and `-Dtui=disabled` to
build the stdin tools without ncurses and their `-T` terminal view. Keep
generated files inside ignored `build*` directories. Keep new terminal code in
`tools/`, never in the library: the core must stay free of terminal and audio
device dependencies.

## Coding Style & Naming Conventions

Use C11 and Linux coding style: eight-column tabs, Linux brace placement,
`snake_case` names and concise functions. Apply `.clang-format` before submitting.
Internal helpers use `md_`; constants use uppercase underscores. Add
`SPDX-License-Identifier: GPL-3.0-or-later` to new implementation files.

Keep sample rate and timing configurable. Avoid platform-specific core APIs,
global decoder state and allocation during streaming processing. Document buffer
ownership and errors in the public header.

## Verification Guidelines

No automated test suite is shipped. Build the affected library and tools, verify
formatting, and exercise representative input when changing decoder behavior.
Check chunk boundaries, sample rates, multiple carriers and malformed input for
DSP changes. Use sanitizer builds for memory-sensitive work. Validate manual
pages with `mandoc -T lint docs/man/*.3` when documentation changes.

## Commit & Pull Request Guidelines

The initial workspace had no Git history, so no historical message convention
exists. Use concise imperative subjects, optionally prefixed by module, such as
`frontend: preserve power scaling across sample rates`. Explain the behavior,
linked issue if available, validation commands and design tradeoffs in
pull requests. Include trace/audio reproduction details for decoding fixes;
screenshots are unnecessary for library-only changes.
