# WFST impulse and word-boundary tuning

The subsequent [15–40 WPM ham CW optimization](wfst-ham.md) supersedes these
results. This report preserves the previous tuning step.

Measured on 2026-10-18 against the baseline at `2e5b832`. Two targeted scoring
changes remove the reported regressions against legacy. They also improve mean
and pooled CER on both complete synthetic grids and reduce archived recording
errors. These inputs are development validation data, not an independent holdout.

| Condition | Cases | Previous WFST mean CER | Tuned WFST | Legacy | Vail |
| --- | ---: | ---: | ---: | ---: | ---: |
| Two impulses/second, 22 WPM | 4 | 7.26% | **2.42%** | 2.42% | 2.42% |
| Clean 30 WPM | 20 | 1.03% | **0.69%** | 0.69% | 1.03% |
| 40 WPM, −3 dB | 20 | 22.30% | **22.46%** | 40.94% | 36.30% |
| 20% timing jitter, 18 WPM | 4 | 3.33% | **3.33%** | 7.92% | 5.00% |

Impulse errors fall from nine to three over 124 reference characters. Clean
30 WPM errors fall from twelve to eight over 812 characters; the fixed error is
an extra space between `8` and `9` in the digit message, repeated across its four
seeds. This matches legacy, with residual errors remaining in that message.
Fast/noisy errors increase from 166 to 167: the strong advantage over legacy
and Vail remains, with a 0.16 percentage point regression against previous WFST.
Jitter errors remain eight over 240 characters.

| Evaluation | Previous WFST | Tuned WFST | Legacy |
| --- | ---: | ---: | ---: |
| Primary grid, 464 cases, mean CER | 2.054% | **1.949%** | 3.606% |
| Primary grid, pooled errors / 18,940 characters | 421 | **398** | 664 |
| Additional grid, 120 cases, mean CER | 3.209% | **3.088%** | 4.478% |
| Additional grid, pooled errors / 4,560 characters | 174 | **167** | 233 |
| Archived recordings, errors / 170 characters | 17 | **15** | 19 |

Primary pooled CER is 2.101%; additional pooled CER is 3.662%. Space-free errors
are 184 and 68, respectively. Both complete runs pass `--require-better
wfst=legacy`, which checks mean and pooled CER independently. Recording errors
are 6/57 for `UBlxpe5gvv0`, 1/40 for `9AhkEDs2Sko` and 8/73 for
`9jgZ94TzRys`. The third recording still has one more error than legacy.

## Scoring changes

- Floor negative mark log odds at −4 only for a dash self-loop before its
  predicted median end. A brief dropout can otherwise push the ongoing dash
  outside the eight-unit search beam. Onsets, dots, gaps and later dash frames
  keep their original evidence. Initial acquisition keeps the original scoring;
  calibrated replay enables the floor. Timing and frame width determine the
  interval where the floor applies.
- Multiply word-gap evidence by 0.5 when computing boundary odds at text commit.
  This reduces false boundaries on ambiguous gaps before word-graph scoring.
  The rule applies to every character; it does not inspect the message or digits.
  Long-silence and EOF boundary handling retain their forced-boundary sentinel.

Broad probability caps reduced impulse errors but damaged fast/noisy decoding.
A floor across both mark classes, speed-dependent caps and wider beams also
failed to give the chosen balance. Increasing the variance floor for boundary
scoring did not fix the clean 30 WPM regression. The retained changes add no
state, allocation, dictionary entry, public setting or generated graph change.

## CPU measurements

Five rounds alternate decoder order on Linux CPU 2, with nine preloaded-PCM
samples per decoder/input per round. The final run followed completion of the
other verification jobs. Construction, I/O, teardown and EOF flush are excluded;
streaming acquisition/replay and the silent tail are included.

| Input | Legacy CPU median | Tuned WFST | Vail |
| --- | ---: | ---: | ---: |
| 20 WPM, 3 dB, `w20_s3_t1_r0` | 20.418 ms | 21.003 ms | 17.966 ms |
| 12 WPM, clean, `w12_sinf_t0_r0` | 27.600 ms | 27.964 ms | 21.661 ms |
| 40 WPM, −3 dB, `w40_s-3_t0_r0` | 8.875 ms | 9.526 ms | 7.871 ms |
| Interference, `qrm100_r0` | 19.631 ms | 20.312 ms | 17.015 ms |

WFST runs about 1,901 times real time on the representative 20 WPM input, costing
2.9% more CPU than legacy in this run. The four medians do not establish a
universal throughput or worst-case latency bound.

## Evidence and reproduction

- [Full primary grid, provenance and accuracy gate](wfst-tuned-grid.json)
- [Full additional grid, provenance and accuracy gate](wfst-tuned-additional.json)
- [Archived recording output and checksums](wfst-tuned-recordings.json)
- [CPU samples and binary/input checksums](wfst-tuned-speed.json)
- [Previous baseline and its evidence](wfst-comparison.md)

Use the existing [build and comparison commands](../../benchmarks/README.md#wfst-versus-legacy-accuracy-gate)
with the working tree as WFST source. For the primary grid, use
`--reuse docs/benchmarks/wfst-grid.json --output build-wfst-bench/tuned-grid.json`.
For additional cases, add `--held-out --seeds 2`, use cache
`build-wfst-bench/held-out-audio`, and set
`--reuse docs/benchmarks/wfst-additional.json --output build-wfst-bench/tuned-additional.json`.
Reuse accepts old results only when source, executable, library and input hashes
match. The tuned WFST cases were rerun; matching legacy and Vail results were
reused. Source and library hashes match between the two new grids, and their
per-case WAV hashes match the previous reports.

Release, static/serial and ASan/UBSan builds pass all four Meson checks. These
include the full Morse table at three frame periods, 8/48 kHz audio, independent
history convergence, storage pressure, acquisition/replay, allocation-free
streaming, reset/flush and serial/parallel multiple-carrier equivalence. Formatting
and graph regeneration checks pass. The 16 cases selected by `qrn2`,
`w30_sinf_t2`, `w40_s-3_t0` and `jit0.2_w18` also produce identical final text
with chunks of 1, 257 and 4,096 samples. No new test suite or public API was added.
