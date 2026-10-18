# Decoder comparison

`compare.py` runs the exact case definitions and CRC32 seeds from a separately
checked out [Vail decoder](https://github.com/TheRealKamisama/vail-decoder).
No Vail source is included here. The comparison used Vail revision
`ffb61d5bd7f010cedeb291e440c1781052c24c48`.

Build both projects and the final-current-channel adapter:

```sh
git clone https://github.com/TheRealKamisama/vail-decoder.git build-vail
git -C build-vail checkout ffb61d5bd7f010cedeb291e440c1781052c24c48
bash build-vail/build.sh
meson setup build-perf --buildtype=release
meson compile -C build-perf
cc -O2 -Iinclude benchmarks/decode.c -Lbuild-perf \
  -Wl,-rpath,'$ORIGIN' -lmorsedec -lsndfile -o build-perf/compare-decode
python3 benchmarks/compare.py --vail build-vail \
  --decoder build-perf/compare-decode --output build-perf/comparison.json
```

The adapter uses the real-audio FFT backend, native mono sample rate, default configuration, one
hop per processing call, four seconds of silence, then `md_flush()`. It prints
only the final current-channel snapshot, matching Vail's comparison policy.
This differs from the chronological snapshot assembly of the archived real
recordings in [docs/benchmarks](../docs/benchmarks/README.md).

The default is 464 cases (116 conditions, four seeds). Each decoder receives the
same generated WAV. The JSON contains references, decoded strings, edit counts,
per-case CER, WAV checksums, Vail revision and adapter/library checksums. CER
includes word spacing after uppercase conversion and collapsing whitespace. The
headline metric is the **mean per-case CER**, matching Vail's report; it is not
pooled edit errors divided by total reference length. Space-free edit counts are
also retained. Process wall times include startup and file I/O and should not be
interpreted as library throughput.

Use `--held-out --seeds 2` for 120 additional synthetic cases with different
messages, speeds, pitches, fading and jitter. These still use Vail's generator;
they do not establish performance on independent radio recordings. `--chunk N`
changes the libmorsedec processing chunk size without changing Vail's input.
`--filter SUBSTRING` selects a subset for diagnosis.

Generated audio and transient reports belong in ignored `build*` directories.
The script imports the case definitions from the external checkout, so pin its
revision when reproducing results.

For the three archived real recordings (external signed 16-bit WAV files), run:

```sh
python3 benchmarks/recordings.py --library build-perf/libmorsedec.so \
  --audio build/corpus --output build-perf/recordings.json
```

This uses the references already archived in `docs/benchmarks/corpus-after.json`
and preserves sample rate and stereo averaging. It retains current-channel
characters by ordinal after every hop, including revisions and retired channels.

The merged-character regression checks can be run with sanitizers:

```sh
meson setup build-perf-asan -Db_sanitize=address,undefined -Db_lundef=false
meson compile -C build-perf-asan
build-perf-asan/check-patterns
```

They also cover generic-word spacing, a pinned single-dot transmission and
automatic selection when a short message ends directly on its last mark.

To compare streaming CPU cost with preloaded 8 kHz mono PCM:

```sh
cc -O3 -Iinclude benchmarks/speed.c -Lbuild-perf -Wl,-rpath,'$ORIGIN' \
  -lmorsedec -lsndfile -o build-perf/speed
cc -O3 -DVAIL_BASELINE -Ibuild-vail/src benchmarks/speed.c \
  build-vail/src/*.c -lsndfile -lm -o build-perf/vail-speed
build-perf/speed build-perf/comparison-audio/w20_s3_t1_r0.wav
build-perf/vail-speed build-perf/comparison-audio/w20_s3_t1_r0.wav
```

Both measurements include the four-second silent tail but exclude construction,
file I/O and teardown. The JSON contains nine process-CPU timings. Compare
medians on an otherwise idle host; tiny differences do not establish a speed
advantage across workloads.

## Channel parallelism

Build the generated-input latency benchmark (no external audio is needed):

```sh
meson setup build-parallel --buildtype=release -Dparallel=enabled
meson compile -C build-parallel parallel-speed
build-parallel/parallel-speed 32 1 spectrum 48000
build-parallel/parallel-speed 32 4 spectrum 48000
build-parallel/parallel-speed 8 0 audio 48000
build-parallel/parallel-speed 8 4 iq 48000
```

Arguments are carrier count (1..32), total thread budget (0 means automatic),
input mode (`spectrum`, `audio`, or `iq`; default `spectrum`), and sample rate
(default 48000). The requested band must fit the sample rate: use 48 kHz for
32 carriers; 8 kHz supports the cases through eight carriers. Inputs contain
staggered three-dot letters. They are generated once and preloaded; construction,
a 400-hop acquisition/replay warmup, and destruction are excluded from timing.
The benchmark reports separate construction/destruction medians, medians over
five runs of 1200 hops, process CPU time,
median/p99/maximum hop wall latency and deadline misses across all runs. Each
run processes 12.75 seconds of steady input at the default hop cadence. Active
channel count is included so missing acquisition cannot masquerade as speedup.
These synthetic cases measure scheduling costs, not decoding accuracy.

Compare carrier counts 1/2/4/8/16/32 and budgets 1/2/4/0 on an otherwise idle
host. Spectrum input isolates the band/channel backend; audio and I/Q include
frontend cost. Thread wakeups can increase CPU and median latency while reducing
p99 latency during expensive word updates. Prefer wall time and latency to
process CPU time when judging parallel speedup.

`meson test -C build` runs both the existing pattern checks and deterministic
serial/parallel snapshot checks. Linux also injects partial worker-start and
allocation failures. Exercise serial builds with `-Dparallel=disabled`, memory
checks with `-Db_sanitize=address,undefined -Db_lundef=false`, and race checks in a
separate build with `-Db_sanitize=thread -Db_lundef=false`.

## WFST versus legacy accuracy gate

[`wfst-ham.md`](../docs/benchmarks/wfst-ham.md) records the current ham CW
optimization and its evidence. [`wfst-comparison.md`](../docs/benchmarks/wfst-comparison.md)
preserves the previous comparison. Keep the legacy checkout and builds separate from the working tree:

```sh
mkdir -p build-wfst-bench/legacy-src
git archive 072f6a7 | tar -x -C build-wfst-bench/legacy-src
meson setup build-wfst-bench/legacy-build build-wfst-bench/legacy-src \
  --buildtype=release -Db_ndebug=true -Dparallel=enabled -Dtui=disabled
meson setup build-wfst-release --buildtype=release -Db_ndebug=true \
  -Dparallel=enabled -Dtui=disabled
meson compile -C build-wfst-bench/legacy-build
meson compile -C build-wfst-release
cc -O3 -Iinclude benchmarks/decode.c -Lbuild-wfst-bench/legacy-build \
  -Wl,-rpath,'$ORIGIN/legacy-build' -lmorsedec -lsndfile \
  -o build-wfst-bench/legacy-decode
cc -O3 -Iinclude benchmarks/decode.c -Lbuild-wfst-release \
  -Wl,-rpath,'$ORIGIN/../build-wfst-release' -lmorsedec -lsndfile \
  -o build-wfst-bench/after-decode
CFLAGS='-O3 -DNDEBUG -g -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Wno-unused-parameter' \
  OUT=build-wfst-bench bash build-vail/build.sh
python3 benchmarks/wfst-compare.py --vail build-vail \
  --vail-build build-vail/build-wfst-bench \
  --decoder wfst=build-wfst-bench/after-decode \
  --decoder legacy=build-wfst-bench/legacy-decode \
  --decoder vail=build-vail/build-wfst-bench/vail_decode \
  --library wfst=build-wfst-release/libmorsedec.so \
  --library legacy=build-wfst-bench/legacy-build/libmorsedec.so \
  --source wfst=. --source legacy=build-wfst-bench/legacy-src \
  --cache build-wfst-bench/audio --output build-wfst-bench/grid.json \
  --require-better wfst=legacy
```

Use the pinned Vail checkout described above. Add `--held-out --seeds 2` and use
separate cache/output paths for the 120 additional cases. The option name is
historical: these are development validation cases, not an untouched holdout.
`--require-better` exits unsuccessfully unless **both mean and pooled CER** are
strictly lower than legacy. The complete report and failed gates remain
reviewable. `complete: true` is written only after binary/source integrity checks.
`--reuse REPORT` reuses matching case/input/binary/source results; never rebuild
or edit a measured decoder during a run.

For CPU comparisons, compile each libmorsedec speed adapter with
`-DMD_BENCH_SERIAL`, the same `-O3` flags and its matching library/rpath. This
selects the existing one-thread API without changing library scheduling. Build
Vail's speed adapter with `-DVAIL_BASELINE` as above. Then run:

```sh
python3 benchmarks/wfst-speed.py \
  --decoder legacy=build-wfst-bench/legacy-speed \
  --decoder wfst=build-wfst-bench/after-speed \
  --decoder vail=build-wfst-bench/vail-speed \
  --library legacy=build-wfst-bench/legacy-build/libmorsedec.so \
  --library wfst=build-wfst-release/libmorsedec.so \
  --audio build-wfst-bench/audio/w20_s3_t1_r0.wav \
  --rounds 5 --cpu 2 --output build-wfst-bench/speed.json
```

Select an available Linux CPU or omit `--cpu` on other systems. Repeat `--audio`
for additional workloads. The runner alternates decoder order, records 45 CPU
samples per decoder/input by default, and checks input/binary integrity. CPU cost
is reported separately from the accuracy gate; it is not inferred from CLI wall
time. Construction, file I/O, teardown and explicit EOF flush are excluded;
streaming acquisition/replay and the silent tail are included.

## Ham QSO speed sweep

Use `benchmarks/wfst-compare.py --speed-sweep --seeds 2` with the decoder,
library, source, cache and output arguments above. This generates 936 cases:
every integer speed from 15 through 40 WPM, three ham QSO messages, six reception
conditions and two deterministic seeds. Conditions are clean, −3 dB noise,
20% timing jitter, two impulses per second, fading and an overlapping carrier.
The interfering transmission ends before the target, preserving the meaning of
the final-current-channel scoring policy. No speed or frequency hints are given.

The runner emits `by_wpm` summaries for all case sets. Keep `--speed-sweep` and
`--held-out` separate. These are development cases from Vail's `cwgen`; the
archived radio recordings remain a separate acceptance check using chronological
snapshots. Current results and source provenance are in
[the ham CW report](../docs/benchmarks/wfst-ham.md).

## SNR sensitivity measurement

`wfst-sensitivity.py` holds the decoder fixed and measures Gaussian-noise curves
for three ham QSO messages, four seeds and every integer speed from 15 to 40 WPM.
The default sweep covers +3, 0 and −1 through −12 dB, using Vail's 2.5 kHz,
key-down SNR convention. It records complete outputs, empty decodes, input hashes,
clipping counts, source/binary provenance and mean/pooled CER for each cell.
The same seeded noise realization is used across SNR levels and generator modes.

Stock `cwgen` has peak tone amplitude 0.3 and clips its 16-bit output increasingly
at negative SNR. To distinguish clipping from Gaussian-noise sensitivity, build
a second generator with amplitude 0.03; its noise amplitude scales by the same
factor, retaining the requested nominal SNR:

```sh
mkdir -p build-wfst-sensitivity
sed 's/amp = 0.3/amp = 0.03/' build-vail/tools/cwgen.c \
  > build-wfst-sensitivity/cwgen-headroom.c
cc -O3 -DNDEBUG -std=c99 -D_DEFAULT_SOURCE -Ibuild-vail/tools \
  build-wfst-sensitivity/cwgen-headroom.c -lm \
  -o build-wfst-sensitivity/cwgen-headroom
python3 benchmarks/wfst-sensitivity.py --vail build-vail \
  --generator stock=build-vail/build-wfst-bench/cwgen \
  --generator headroom=build-wfst-sensitivity/cwgen-headroom \
  --generator-source stock=build-vail/tools/cwgen.c \
  --generator-source headroom=build-wfst-sensitivity/cwgen-headroom.c \
  --decoder build-wfst-bench/after-decode \
  --library build-wfst-release/libmorsedec.so \
  --cache build-wfst-sensitivity/audio \
  --output build-wfst-sensitivity/snr.json --seeds 4 --jobs 4
```

Thresholds at 1%, 5% and 10% mean CER require all higher tested SNR levels to pass
as well. Isolated nonmonotonic passing cells are reported separately; thresholds
are never interpolated. `--speeds`, `--snrs` and `--seeds` support refinement;
use the equals form, such as `--snrs=-3,-4,-5`, when the list begins with a
negative number. `--reuse`
accepts matching case definitions, runner, decoder and generator provenance.
Parallel jobs use separate WAVs and decoder processes; they do not measure CPU
throughput. Keep decoder sources and measured binaries unchanged during a run.

Measured thresholds and complete case evidence are in the
[SNR sensitivity report](../docs/benchmarks/wfst-sensitivity.md).

## Replay the archived CER gate

The [lattice CER report](../docs/benchmarks/lattice-gate.md) compares against the
latest pre-lattice reports, including the SNR sweeps. Preserve those reports as
baselines. Generate or retain their cached WAVs using the commands above:
`build-wfst-range/audio`, `build-wfst-bench/audio`,
`build-wfst-bench/held-out-audio`, and `build-wfst-sensitivity/audio`.
The replay runner verifies every WAV against its archived SHA-256 before decoding.
Missing or different audio stops the run; it cannot silently substitute a new seed.

Build from a fixed checkout and leave its source and binaries unchanged until
measurement finishes:

```sh
meson setup build-lattice --buildtype=release -Db_ndebug=true \
  -Dparallel=enabled -Dtui=disabled
meson compile -C build-lattice
cc -O3 -Iinclude benchmarks/decode.c -Lbuild-lattice \
  -Wl,-rpath,'$ORIGIN' -lmorsedec -lsndfile -o build-lattice/decode
python3 benchmarks/archived-cer.py \
  --decoder build-lattice/decode --library build-lattice/libmorsedec.so \
  --source . --output build-lattice/cer --jobs 8 \
  --sets qso,grid,additional,snr-full,snr-refined
python3 benchmarks/recordings.py --library build-lattice/libmorsedec.so \
  --audio build/corpus --baseline docs/benchmarks/wfst-ham-recordings.json \
  --output build-lattice/recordings.json
```

Synthetic replay accepts equality and requires **both mean and pooled CER to be
no worse on each complete dataset**. It uses exact rational means and integer
edit totals, checks binary/source integrity, and exits nonzero on failure or an
incomplete selection. The default datasets are `qso,grid,additional`; the two SNR
reports share 1,440 cases and are replayed only once per identical input/reference.
An append-only checkpoint allows the same command to resume interrupted work.
Changed binary/library/source or WAV/reference hashes invalidate cached results.
`--filter` is for diagnosis and cannot pass a full-dataset gate.

For live decoder-to-decoder comparisons, `wfst-compare.py --require-not-worse
candidate=baseline` applies the same noninferiority rule. The existing
`--require-better` option still requires strictly lower mean and pooled CER.
Equality was checked to pass the former and fail the latter.

Recordings retain their separate chronological, whitespace-free scoring policy.
The optional `--baseline` argument verifies identical WAVs/references and exits
nonzero if total errors exceed the archived report. The aggregate gate is at most **14
errors / 170 reference characters**. Report individual recording regressions even
when the aggregate passes. These are development corpora, not untouched holdouts.


## Float API and repeat measurements

Version 0.3.0 uses public ABI 2 (`float`); rebuild each C adapter
against its matching public header and library. `recordings.py` defaults to ABI 2;
use `--abi 1` for archived double-API libraries. Never load an ABI-1 library using
the ABI-2 ctypes layouts.

To compare a new build against the merged lattice benchmark, add
`--baseline-prefix docs/benchmarks/lattice-gate` to `archived-cer.py`. It reads
`PREFIX-SET.json.gz` and uses that report's candidate results as the baseline,
while preserving exact WAV/reference checks and strict noninferiority reporting.
Without this argument, the original pre-lattice reports remain the baseline.
Float experiments may report a failed strict gate when CER changes slightly;
the complete measurements remain in the output and must be reported explicitly.
This float experiment accepts tiny CER changes after precision experiments;
strict gate failures remain explicit.

A build with `-Dc_args=-DMD_FLOAT_LIBM` uses standard finite/absolute/min/max
operations on the same float data and serves as a control for the IEEE bit helpers.
The default helpers include NaN handling and signed zeros; they are not approximate
transcendental functions. See [float refactor measurements](../docs/benchmarks/float-refactor.md).


## SIMDe optimization measurements

The [SIMDe report](../docs/benchmarks/simde.md) compares against the float
implementation at `fa428c6`. Use `--baseline-prefix docs/benchmarks/float-refactor`
with the archived CER runner for this comparison. The runner also records
`changed_text_cases` and hashes vendored dependency headers.

The normal build uses the vendored SIMDe headers without extra ISA flags.
For fallback measurements, build separate directories:

```sh
meson setup build-simde --buildtype=release -Db_ndebug=true \
  -Dparallel=enabled -Dtui=disabled -Dwerror=true
meson setup build-simde-portable --buildtype=release -Db_ndebug=true \
  -Dparallel=enabled -Dtui=disabled -Dwerror=true -Dc_args=-DSIMDE_NO_NATIVE
meson setup build-simde-scalar --buildtype=release -Db_ndebug=true \
  -Dparallel=enabled -Dtui=disabled -Dwerror=true \
  '-Dc_args=-DSIMDE_NO_NATIVE -DSIMDE_NO_VECTOR'
meson compile -C build-simde
meson compile -C build-simde-portable
meson compile -C build-simde-scalar
cc -O3 -DMD_BENCH_SERIAL -Iinclude benchmarks/speed.c -Lbuild-simde \
  -Wl,-rpath,'$ORIGIN' -lmorsedec -lsndfile -o build-simde/speed
cc -O3 -Iinclude benchmarks/decode.c -Lbuild-simde \
  -Wl,-rpath,'$ORIGIN' -lmorsedec -lsndfile -o build-simde/decode
python3 benchmarks/archived-cer.py --decoder build-simde/decode \
  --library build-simde/libmorsedec.so --source . \
  --output build-simde/cer --jobs 8 \
  --sets qso,grid,additional,snr-full,snr-refined \
  --baseline-prefix docs/benchmarks/float-refactor
```

Compile adapters separately for each library and preserve immutable source
snapshots during measurement, including `third_party/`. Pass repeatable
`--decoder LABEL=EXE` and `--library LABEL=LIB` arguments to `wfst-speed.py`,
the five WAVs recorded in `simde-speed.json`, `--rounds 5`, and a permitted
`--cpu` affinity. Keep CER jobs and compilation separate from CPU timing.
`SIMDE_NO_NATIVE` permits compiler vector extensions; `SIMDE_NO_VECTOR`
disables those within SIMDe too. The compiler may still auto-vectorize C loops.
Fallback builds on x86 do not measure performance on ARM or other hardware.
