# SIMDe decoder optimization

Branch: `perf/simd-exploration`, compared with the float implementation at
`fa428c6`. Public API and shared-library ABI 2 are unchanged.

## Implementation

- SIMDe 128-bit maximum reduction for WFST survivor selection, followed by a
  first-match scan preserving expansion-order ties. A contiguous 400-float score
  bank caches beam eligibility and follows selection and diversity penalties.
- Audio and I/Q windowing over two contiguous ring spans, using four-lane
  multiplication and I/Q interleaving. Unaligned loads and scalar tails cover
  arbitrary ring offsets; no sample is read beyond its span.
- No approximate math, fast-math, score reassociation, global ISA flags, or new
  streaming allocations. FFTW retains responsibility for FFT vectorization.
- Private, unmodified [SIMDe v0.8.2 headers](../../third_party/simde/README.md),
  commit `71fd833d9666141edcd1d3c109a80e228303d8d7`, vendored with MIT notices.
  Fresh builds need no additional package, network access or code generation.

All rank scans remain bounded by 400 candidates and 100 survivors. The extra
score bank costs 1,600 bytes per channel: 407,552 bytes inline, **414,984 bytes**
including 618 word states and both index slots, leaving 109,304 bytes below the
512 KiB limit on this 64-bit build.

## CPU cost

Linux x86-64, Intel i7-1165G7, GCC 15.2.0, FFTW 3.3.10, release `-O3`,
`b_ndebug=true`, parallel backend enabled. Each timed decoder uses one processing
thread, pinned to CPU 2. Five alternating-order rounds, nine repetitions per
round, **45 CPU samples per input/build** (900 measurements total). No CER jobs
or compilation ran during CPU timing. The baseline was rebuilt previously from
an immutable snapshot identical to `fa428c6` core sources.

Preloaded 8 kHz PCM processing, acquisition/replay and four seconds of silence
are timed. Construction, teardown, file I/O and explicit EOF flush are excluded.
Compare the freshly paired measurements here, rather than historical host times.

| Input | Float baseline | SIMDe native | No native intrinsics | No native/vector extensions | Native CPU reduction |
| --- | ---: | ---: | ---: | ---: | ---: |
| 20 WPM, +3 dB | 129.756 ms | 115.593 ms | 120.813 ms | 124.842 ms | 10.9% |
| 12 WPM, clean | 124.389 ms | 111.025 ms | 114.592 ms | 118.571 ms | 10.7% |
| 40 WPM, −3 dB | 44.052 ms | 38.252 ms | 40.263 ms | 42.601 ms | 13.2% |
| Overlapping carrier | 81.400 ms | 73.613 ms | 75.658 ms | 77.209 ms | 9.6% |
| 35 WPM, −5 dB, headroom | 102.234 ms | 88.089 ms | 94.066 ms | 102.111 ms | 13.8% |

Native SIMDe reduces median streaming CPU by **9.6%–13.8%**, running
317–470× real time on these inputs. These are whole-decoder results, including
the ring-span and cached-eligibility changes. The fallback controls isolate
SIMDe backend choices within the new algorithm, rather than every code change.

`portable` sets `SIMDE_NO_NATIVE`; `scalar` additionally sets `SIMDE_NO_VECTOR`.
The compiler may still auto-vectorize ordinary C loops. SIMDe chooses its
backend at compile time; this implementation adds no runtime CPU dispatch.
These measurements cover x86-64, not ARM hardware performance.

[All CPU samples, input/binary/source hashes and build options](simde-speed.json).
Reproduction commands are in [benchmarks/README.md](../../benchmarks/README.md#simde-optimization-measurements).

## CER comparison

Compared with the archived float reports on identical SHA-256-checked WAVs and
references. All five datasets pass both the exact rational mean and pooled CER
gates. Every normalized decoded string is identical to the float baseline.

| Dataset | Cases | Mean CER, both builds | Errors / reference characters, both builds | Changed texts | Gate |
| --- | ---: | ---: | ---: | ---: | --- |
| QSO, 15–40 WPM | 936 | 1.5714% | 657 / 42,120 | 0 | PASS |
| Vail primary grid | 464 | 0.9662% | 235 / 18,940 | 0 | PASS |
| Additional grid | 120 | 2.8265% | 157 / 4,560 | 0 | PASS |
| All-speed SNR | 8,736 | 37.2435% | 145,309 / 393,120 | 0 | PASS |
| Refined SNR | 6,912 | 14.1976% | 43,692 / 311,040 | 0 | PASS |

This replays **15,728 distinct synthetic WAVs**, covering 17,168 report rows;
the SNR reports overlap by 1,440 cases. There are no per-case text or error
changes. Existing SNR/CER thresholds therefore remain unchanged.

Both forced fallback builds separately replay all **1,520 primary synthetic
cases**, with identical text and error counts. Each fallback also passes the
three recorded QSOs. Native and both fallback builds preserve the exact
chronological recording text and **13/170 errors** (2/57, 2/40, 9/73).

These are development corpora, including Vail cwgen audio and three recorded
QSOs. Fallback replay on x86 verifies those code paths; it does not establish
results on another CPU architecture.

[Summary and artifact hashes](simde.json),
[native QSO cases](simde-qso.json.gz),
[Vail grid](simde-grid.json.gz),
[additional grid](simde-additional.json.gz),
[all-speed SNR](simde-snr-full.json.gz),
[refined SNR](simde-snr-refined.json.gz),
[recorded QSOs](simde-native-recordings.json).
Fallback case reports use `simde-portable-*` and `simde-scalar-*` filenames.

## Build and memory checks

- GCC 15.2 release shared build, including tools, with `-Dwerror=true`.
- GCC forced portable and scalar SIMDe fallback libraries, also with warnings
  treated as errors.
- Clang 21.1 static build with the parallel backend disabled and warnings
  treated as errors.
- GCC AddressSanitizer + UndefinedBehaviorSanitizer build. Existing preloaded
  I/Q benchmark: four carriers at 8 kHz and 48 kHz; no sanitizer reports. These
  concurrent verification runs are not used as CPU performance evidence.
- Sanitized audio replay with chunks of 1, 86 and 777 samples: text matches the
  float baseline, no sanitizer reports.
- Formatting check and channel payload measurement pass. Vendored headers and
  license match the pinned upstream bytes.

Accuracy evidence comes from the archived audio/CER benchmarks and
representative streaming replays.
