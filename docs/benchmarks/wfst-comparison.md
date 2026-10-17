# Streaming WFST versus legacy libmorsedec and Vail

Measured on 2026-10-18. The accuracy gate passes: the optimized WFST has strictly
lower mean and pooled character error rate (CER) than legacy on both synthetic
sets, and fewer total errors on the three archived tuning recordings. CPU cost
is an optimization target rather than an accuracy gate. These aggregate results
do not establish superiority on every condition or on independent radio input.

| Evaluation | Cases | Legacy | Initial WFST | Optimized WFST | Vail |
| --- | ---: | ---: | ---: | ---: | ---: |
| Upstream synthetic grid, mean CER | 464 | 3.606% | 10.411% | **2.054%** | 3.721% |
| Additional synthetic cases, mean CER | 120 | 4.478% | Not measured | **3.209%** | 6.112% |
| Archived recordings, errors / 170 characters | 3 | 19 | 33 | **17** | Not measured |

On the primary grid, mean CER falls 43.0% relative to legacy. Pooled errors are
421/18,940 (2.223%) versus legacy's 664 (3.506%) and Vail's 716 (3.780%). Removing
spaces gives 189 errors for WFST, 391 for legacy and 418 for Vail. The additional
grid has 174/4,560 pooled errors (3.816%), versus 233 (5.110%) and 310 (6.798%).
Both mean and pooled synthetic metrics must improve; neither substitutes for the
other. The runner's `--require-better wfst=legacy` enforces this gate and records
its result in JSON.

## Builds, inputs and scoring

Legacy is Git revision `072f6a75176ff6f2d2cdc61b5ab883f954fa1786`, archived into a
separate source directory before building. Initial WFST is the experimental
implementation before benchmark-driven optimization, preserved as a separate
source/build snapshot. All libmorsedec builds use GCC 15.2.0, release `-O3`,
`-DNDEBUG`, FFTW 3.3.10 and the existing pthread backend. Vail is revision
`ffb61d5bd7f010cedeb291e440c1781052c24c48`, rebuilt with `-O3 -DNDEBUG` through its
own build script. Adapters also use `-O3`. Exact binary, source and input SHA-256
hashes and libmorsedec build options accompany the reports.

Each decoder receives byte-identical seeded 8 kHz mono WAVs from Vail's `cwgen`.
The primary grid uses the upstream 116 conditions and four CRC32-derived seeds:
12/20/30/40 WPM, noise through −3 dB, jitter, fading, drift, interference,
impulses and short messages. Defaults select the current channel automatically;
there are no frequency or speed hints. Both adapters add four seconds of silence;
libmorsedec then calls `md_flush()`. Final current-channel text is uppercased and
whitespace collapsed. Spaces, punctuation and O/0 differences count as edits.
Headline CER averages each case's edit distance divided by reference length.

The 120 additional cases use three other messages, 14/19/27/34/39 WPM,
543/680/817 Hz, noise, jitter, fading and two seeds. They remain development
validation data from the same generator, not an untouched holdout. The recordings
were also used during tuning. Independent senders and reception conditions are
still needed for external validation.

## Changes retained

- Hold initial text until STA acquisition ends, then replay at most 4,096 cached
  soft keying observations under the calibrated timing model. Do not replay DSP
  or train STA twice. This fixes irreversible first-word errors when initial
  timing priors disagree with the sender. Overflow drops the oldest observations
  and increments a counter.
- Apply a minimum predictive log-duration standard deviation of 0.2. STA means
  and variances continue adapting with their existing update rules; the emission
  model avoids becoming too confident after a few consistent intervals.
- Use the duration class priors without renormalizing over the legal outgoing
  arcs of each character prefix. Add a frame-rate-scaled idle onset probability
  of 0.05 per nominal tick and an eight-natural-log-unit score beam, retaining at
  most 100 tokens.
- Build duration hazard tables only for visited ages. Carry compact candidate
  score/source/edge records; construct tokens and immutable events only for
  selected survivors. Calculate word-boundary probabilities at text commit.
- Clear only occupied hash buckets and reclaim only previously used history
  entries. Skip idle expansion exactly when both onset arcs would be pruned.
  Use a single observation log-odds value, removing the common per-frame score
  offset that cancels during normalization.

Experiments with tighter beams, alternative onset priors and dictionary-derived
character-frequency weights did not give a satisfactory combined result; the
character-frequency feature was discarded. The result keeps the H ∘ C graph
independent of word dictionaries. `word.c` and `message.c` remain unchanged.

## CPU cost

These are process-CPU medians on the same host pinned to Linux CPU 2. Five rounds
alternate decoder order; each executable supplies nine samples, for 45 samples
per decoder/input. Audio is preloaded, libmorsedec uses its existing one-thread
API, and measurements include streaming acquisition/replay and the silent tail.
Construction, file I/O, teardown and explicit EOF flush are excluded. CLI process
wall time is not used as a throughput measurement.

| Input | Audio including tail | Legacy CPU | Initial WFST CPU | Optimized WFST CPU | Vail CPU |
| --- | ---: | ---: | ---: | ---: | ---: |
| 20 WPM, 3 dB, `w20_s3_t1_r0` | 39.929 s | 20.743 ms | 44.100 ms | **20.751 ms** | 17.968 ms |
| 12 WPM, clean, `w12_sinf_t0_r0` | 40.205 s | 28.130 ms | 98.958 ms | **28.049 ms** | 22.079 ms |
| 40 WPM, −3 dB, `w40_s-3_t0_r0` | 17.669 s | 8.794 ms | 30.087 ms | **9.490 ms** | 7.838 ms |
| Interference, `qrm100_r0` | 34.627 s | 19.460 ms | 55.694 ms | **20.319 ms** | 17.177 ms |

The representative 20 WPM workload uses 52.9% less CPU than initial WFST and is
about 1,924× real time. Its difference from legacy is negligible at this timing
resolution. WFST costs 7.9% more than legacy on the fast/noisy input and 4.4%
more on interference; Vail remains faster on these four inputs. Host timing
variation and this limited workload set preclude a universal speed claim. A
calibration event can replay a bounded observation ring inside one processing
call, so average throughput does not imply a worst-case hop-latency guarantee.

## Regressions and recording results

| Recording | Legacy errors | Initial WFST errors | Optimized WFST errors |
| --- | ---: | ---: | ---: |
| UBlxpe5gvv0 | 10 | 18 | **8** |
| 9AhkEDs2Sko | 2 | 2 | **1** |
| 9jgZ94TzRys | **7** | 13 | 8 |

Recordings use chronological current-channel snapshots keyed by source ordinal,
uppercase and whitespace removal. This preserves handoffs and retired-channel
text; its metric is different from the synthetic final-snapshot metric. Sample
rates and stereo averaging are preserved. No Vail recording result is claimed
under this assembly policy. The third recording regresses by one error, while
the aggregate improves from 11.18% to 10.00% CER.

Some synthetic groups regress too. At two impulses/second, WFST mean CER is
7.26% versus 2.42% for both legacy and Vail. Clean 30 WPM is 1.03% versus 0.69%
for legacy. Gains are strongest at 40 WPM/−3 dB (22.30% versus legacy's 40.94%
and Vail's 36.30%) and 20% timing jitter at 18 WPM (3.33% versus 7.92% and 5.00%).
High-speed/noisy decoding therefore remains imperfect even where it improves.

## Evidence and verification

- [Primary grid, all four implementations](wfst-grid.json)
- [Additional grid and accuracy gate](wfst-additional.json)
- [CPU samples and checksums](wfst-speed.json)
- [Optimized recording output](wfst-recordings.json), [legacy](wfst-legacy-recordings.json), [initial WFST](wfst-initial-recordings.json)
- [Reproduction commands](../../benchmarks/README.md#wfst-versus-legacy-accuracy-gate)

Release, static/serial and ASan/UBSan builds pass all four Meson checks. Tests
exercise the full Morse table at three frame periods, live/EOF output,
independent acoustic/text convergence, pool exhaustion and reuse, calibration
with an initial speed mismatch, acquisition-ring overflow, allocation-free
streaming, reset/flush, serial/parallel equivalence, and 8/48 kHz audio. Graph
regeneration, C formatting and manual-page lint also pass. WFST state uses
393,624 bytes/channel on this 64-bit build, below its 512 KiB compile-time bound.

`frontend.c`, `band.c`, `signal.c`, the threading implementation, public API,
`word.c` and `message.c` are unchanged from legacy. The refactor replaces the
heuristic segmentation/character layer; existing signal hooks are bridged through
`decode_compat.c`. Full design details are in [design.md](../design.md).
