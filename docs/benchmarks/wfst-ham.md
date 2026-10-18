# Ham CW optimization across 15–40 WPM

Measured on 2026-10-18 against the preceding WFST at `278bb06`. Real ham QSOs
were checked first, followed by a new Vail `cwgen` grid covering every integer
speed from 15 through 40 WPM. All 26 speed groups improve in both mean and
pooled CER. Every one of the six condition groups improves in mean CER.

| Evaluation | Cases | Previous WFST | Current WFST | Legacy | Vail |
| --- | ---: | ---: | ---: | ---: | ---: |
| Ham QSO speed sweep, mean CER | 936 | 2.639% | **1.840%** | 4.756% | 5.795% |
| Original primary grid, mean CER | 464 | 1.949% | **1.138%** | 3.606% | 3.721% |
| Additional grid, mean CER | 120 | 3.088% | **2.917%** | 4.478% | 6.112% |
| Archived radio QSOs, errors / 170 characters | 3 | 15 | **14** | 19 | Not measured |

The QSO sweep reduces mean CER by 30.3% relative to the previous WFST. Pooled
errors fall from 1,133 to 745 over 42,120 characters (2.690% to 1.769%).
Space-free errors fall from 673 to 614: much of the gain comes from correcting
word boundaries around punctuation, with a smaller improvement in character
identity. Primary-grid errors fall from 398 to 264 over 18,940 characters
(2.101% to 1.394%), including a reduction in space-free errors from 184 to 71.
Additional-grid errors fall from 167 to 159 over 4,560 characters (3.662% to
3.487%). Both mean and pooled accuracy gates pass against the previous WFST and
legacy on all three complete grids.

These are development results. The radio recordings were used during tuning,
and synthetic cases share Vail's generator. They do not establish accuracy on
independent senders or receivers.

## Real QSOs and protected behavior

| Recording | Previous WFST errors | Current errors | Legacy errors |
| --- | ---: | ---: | ---: |
| AI5DD / AE0Q, `UBlxpe5gvv0` | 6 | **5** | 10 |
| JO2QOT / K6XX, `9AhkEDs2Sko` | 1 | **1** | 2 |
| WD4DAN / W0ABE, `9jgZ94TzRys` | 8 | **8** | 7 |

The first recording loses an unwanted initial S. Aggregate recording CER falls
from 8.824% to 8.235%. The third recording retains one more error than legacy,
including errors near a handoff and trailing noise. Scoring preserves native
44.1 kHz stereo averaging and chronological current-channel snapshots by source
ordinal, then uppercases and removes whitespace. It retains punctuation and O/0
errors, and differs from the synthetic final-snapshot metric.

| Original condition | Previous WFST CER | Current CER |
| --- | ---: | ---: |
| Two impulses/second, 22 WPM | 2.42% | **2.42%** |
| Clean 30 WPM | 0.69% | **0.69%** |
| 40 WPM, −3 dB | 22.46% | **5.98%** |
| 20% jitter, 18 WPM | 3.33% | **3.33%** |
| Short transmissions, 16 cases | 0.00% | **0.00%** |

Fast/noisy errors fall from 167 to 48 across 20 cases. The original impulse,
spacing, jitter and short-message results remain intact, including all four
standalone TU seeds.

## Changes retained

- Use a minimum predictive log-duration variance of 0.25 during acquisition
  (SD 0.5), then restore 0.04 (SD 0.2) for calibrated decoding. This admits fast
  initial marks against the default timing prior. STA means, variance updates
  and priors retain their existing rules. Rebuild all duration tables before
  calibrated replay so the initial width cannot leak into cached hazards.
- At 75 nominal ticks of carrier quiet, allow the best path to complete
  acquisition when it contains at least four marks, including committed marks. Commit acoustic history, apply the existing training/release rule and
  replay the soft observations before emitting initial text. This preserves
  short acknowledgments that otherwise remain untrained until channel retirement.
  A short release can still use the initial STA means when fewer than 14
  intervals are available. The existing three-mark threshold for natural
  convergence remains unchanged.
- Let the generic word wildcard accept punctuation at its existing digit fallback
  cost, −5.303. Unknown tokens such as CPY? can keep attached punctuation instead
  of forcing a separate punctuation word. Dedicated punctuation paths remain
  available. This changes word interpretation, never character identity.

There are no new dictionary phrases, expected callsigns, frequency hints, speed
hints, public API fields, streaming allocations or per-channel storage. The
frontend, channel selector and generated Morse graph are unchanged. Giving the
broader duration width to calibrated decoding, widening the score beam, forcing
acquisition with fewer marks, or replacing STA means directly did not give the
chosen balance of real-QSO and synthetic results.

## QSO grid and speed coverage

Three messages represent CQ/callsign acquisition, a report/name/QTH exchange,
and a short POTA-style exchange with BK, TU and EE. They differ from the original
primary and additional grids. At each integer speed, six conditions and two
CRC32-derived seeds produce 36 cases. Pitch varies deterministically between
550 and 882 Hz. All decoders receive byte-identical 8 kHz WAVs with the same
four-second tail and no frequency or WPM hints. Spaces and punctuation count as
edits. Each group reports mean per-case CER; pooled metrics remain in JSON.

The conditions are clean, −3 dB noise, 20% timing jitter at 10 dB, two impulses
per second at 10 dB, fading depth 0.6 with 6% jitter at 6 dB, and an interfering
carrier 100 Hz away at −6 dB relative amplitude. The interference sends VVV TEST
at 23 WPM and ends before the target. A discarded longer distractor outlasted
some targets and correctly became the current channel, which made final-snapshot
scoring measure a station handoff rather than the intended overlap.

| WPM | Previous WFST mean CER | Current | Legacy | Vail |
| ---: | ---: | ---: | ---: | ---: |
| 15 | 3.552% | 2.881% | 4.062% | 4.002% |
| 16 | 1.693% | 1.128% | 2.865% | 3.903% |
| 17 | 1.514% | 0.949% | 2.202% | 4.301% |
| 18 | 1.202% | 0.684% | 2.468% | 3.715% |
| 19 | 1.958% | 1.393% | 2.668% | 2.626% |
| 20 | 2.164% | 1.457% | 3.553% | 8.946% |
| 21 | 1.899% | 1.381% | 3.279% | 3.767% |
| 22 | 2.061% | 1.497% | 3.203% | 2.711% |
| 23 | 1.599% | 1.034% | 2.498% | 2.596% |
| 24 | 2.253% | 1.633% | 3.053% | 2.938% |
| 25 | 2.095% | 1.625% | 3.853% | 4.011% |
| 26 | 1.676% | 1.265% | 2.994% | 7.171% |
| 27 | 1.809% | 1.244% | 3.510% | 2.998% |
| 28 | 1.698% | 1.295% | 3.579% | 10.936% |
| 29 | 2.634% | 2.073% | 4.019% | 10.758% |
| 30 | 2.074% | 1.360% | 4.024% | 3.118% |
| 31 | 2.134% | 1.779% | 3.819% | 3.084% |
| 32 | 2.104% | 1.381% | 3.254% | 2.959% |
| 33 | 2.374% | 1.402% | 3.824% | 3.075% |
| 34 | 3.130% | 2.762% | 4.806% | 4.421% |
| 35 | 2.801% | 2.244% | 5.935% | 7.336% |
| 36 | 3.895% | 2.929% | 6.281% | 6.413% |
| 37 | 5.933% | 3.655% | 6.887% | 7.931% |
| 38 | 4.986% | 3.326% | 11.223% | 9.120% |
| 39 | 5.003% | 2.565% | 14.054% | 16.195% |
| 40 | 4.383% | 2.890% | 11.744% | 11.632% |

| Condition, 156 cases each | Previous WFST mean CER | Current |
| --- | ---: | ---: |
| clean | 0.616% | 0.051% |
| noise | 4.016% | 2.303% |
| jitter | 7.534% | 6.686% |
| impulse | 1.990% | 1.451% |
| fade | 1.013% | 0.457% |
| qrm | 0.668% | 0.090% |

The remaining weaknesses include 20% timing jitter, isolated impulse failures
and word spacing around repeated acknowledgments. Improvements in a speed group
do not imply every individual case improved.

## CPU and verification

Five rounds alternate decoder order on Linux CPU 2, with nine preloaded-PCM CPU
samples per round and decoder. The final run followed completion of the other
checks. It includes acquisition/replay and the silent tail, excludes construction,
I/O, teardown and explicit EOF flush, and uses the one-thread API.

| Input | Previous WFST median | Current WFST | Legacy |
| --- | ---: | ---: | ---: |
| 20 WPM, 3 dB, `w20_s3_t1_r0` | 21.278 ms | 21.028 ms | 20.358 ms |
| 12 WPM, clean, `w12_sinf_t0_r0` | 28.665 ms | 28.971 ms | 27.650 ms |
| 40 WPM, −3 dB, `w40_s-3_t0_r0` | 9.522 ms | 9.743 ms | 8.816 ms |
| Interference, `qrm100_r0` | 20.482 ms | 21.198 ms | 19.747 ms |

The representative 20 WPM input runs about 1,899 times real time. Relative to
the previous WFST, the other medians increase by 1.1–3.5%; these small host
measurements do not establish a universal throughput or worst-case latency bound.

Release, static/serial and ASan/UBSan builds pass all four Meson checks, including
new coverage for a ham QSO with attached punctuation and an unhinted standalone
TU. Duration checks validate both acquisition and calibrated hazards, including
cache rebuilding with unchanged STA variances. Acquisition checks cover multiple
initial speed mismatches. Existing checks cover all Morse symbols at three frame
periods, 8/48 kHz audio, allocation-free streaming, history pressure, non-finite
observations, reset/flush and serial/parallel multiple-carrier equivalence.
Formatting and graph regeneration checks pass.

Across 18 QSO cases (six speeds, clean/noisy/impulsive), chunks of 1, 257 and
4,096 samples produce identical text. Clean CQ cases at 15, 20, 25, 30, 35 and
40 WPM also retain identical text after FFmpeg resampling to 48 kHz, decoded with
257-sample chunks. All 1,520 final synthetic cases retain matching current-source,
executable and library hashes. Recording and CPU reports use the same library.

## Evidence and reproduction

- [Ham QSO sweep, by-speed summaries and accuracy gates](wfst-ham-qso.json)
- [Original primary grid](wfst-ham-grid.json)
- [Additional grid](wfst-ham-additional.json)
- [Actual radio QSO outputs](wfst-ham-recordings.json)
- [CPU samples and checksums](wfst-ham-speed.json)
- [Previous tuning and its evidence](wfst-tuning.md)

Build the previous WFST in a separate source/build directory using `git archive
278bb06`, the same release options and the adapter commands in
[benchmarks/README.md](../../benchmarks/README.md#wfst-versus-legacy-accuracy-gate).
The reports include exact source, input, binary and library checksums and build
options. Legacy remains `072f6a7`; Vail remains
`ffb61d5bd7f010cedeb291e440c1781052c24c48`.

For the new grid, add `--speed-sweep --seeds 2` to `benchmarks/wfst-compare.py`
and provide the current/previous/legacy/Vail decoder, library and source mappings.
Use `--cache build-wfst-range/audio --output build-wfst-range/ham-final-qso.json`
and both `--require-better wfst=baseline --require-better wfst=legacy`. The original
and additional grid commands are unchanged; the latter uses `--held-out --seeds 2`.
Reuse accepts only matching case definitions, generator, source, input and binary
hashes. Final current-WFST cases were rerun after all implementation changes;
matching baseline, legacy and Vail results were reused.

