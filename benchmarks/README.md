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

The adapter uses real FFTW, native mono sample rate, default configuration, one
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
cc -O1 -g -fsanitize=address,undefined -Iinclude -Isrc \
  benchmarks/check-patterns.c src/*.c -lfftw3 -lm \
  -o build-perf/check-patterns
build-perf/check-patterns
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

[`wfst-tuning.md`](../docs/benchmarks/wfst-tuning.md) records the current fine
tuning and its evidence. [`wfst-comparison.md`](../docs/benchmarks/wfst-comparison.md)
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
