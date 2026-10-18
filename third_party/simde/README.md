# SIMDe (private, vendored dependency)

Unmodified header subset of [SIMDe](https://github.com/simd-everywhere/simde),
release **v0.8.2**, commit `71fd833d9666141edcd1d3c109a80e228303d8d7`.
The upstream MIT license is in `COPYING`; individual notices remain in headers.

Only `simde/x86/sse.h` and its recursively quoted includes are included.
Standard library and architecture headers are supplied by the compiler.
No code generation, network access, or system SIMDe package is needed to build.
This dependency is private and does not appear in the installed public header.

To update, check out the desired upstream release, copy `simde/x86/sse.h` and
all transitive quoted includes preserving their paths, and copy `COPYING`.
Review upstream changes and repeat the decoder CPU/CER benchmarks, including
a build with `-DSIMDE_NO_NATIVE` and one with
`-DSIMDE_NO_NATIVE -DSIMDE_NO_VECTOR`.

libmorsedec uses the prefixed SIMDe API without native aliases or global ISA
flags. SIMDe selects the backend at compile time; it does not add runtime CPU
dispatch. Architecture compiler flags remain the packager's choice.
