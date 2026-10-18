# Float API and decoder refactor

Branch: `perf/float-decoder`, compared with merged lattice commit `f4882a9`.
Version 0.3.0 changes all public floating-point fields, parameters and return
values to `float`, shared-library ABI 2. Rebuild consumers and update FFI layouts.
FFTW now uses `fftw3f`; the public header contains no `double` types.

## Implementation

- Float sample/window buffers, FFTW frontend, extraction state/rings, WFST
  tokens/history/scores, lattice, grammar topology and word scores.
- Exact IEEE-754 binary32 finite checks, absolute value and min/max through
  alias-safe bit copies and ordered integer keys. NaN min/max falls back to
  libm; standard helpers are available for non-IEEE targets and A/B measurement.
- Compensated float sliding means with a bounded re-sum each ring wrap.
- Integer duration saturation and nominal-frame rounding avoid unnecessary
  floating operations and preserve integer ages above binary32 exact range.
- Signed-zero canonicalization keeps history hashing consistent with equality.
- Configuration/window construction, covariance/acquisition likelihoods and
  log-survival tail differences retain double intermediates for numerical range
  and cancellation; duration tables and the streaming search remain float.
- No approximate logarithms, exponentials, reciprocal square roots or fast-math.

## CPU cost

Same five inputs, 45 CPU samples per cell, one thread on Linux CPU 2
(i7-1165G7). The CER sweep was stopped during CPU timing. Preloaded PCM,
streaming/replay and a four-second silent tail are timed; construction, I/O,
teardown and explicit EOF flush are excluded. Compare fresh double and float
times from this run, rather than mixing with earlier host timings.

| Input | Double | Float + bits | Float + libm | Float CPU reduction vs double |
| --- | ---: | ---: | ---: | ---: |
| 20 WPM, +3 dB | 171.468 ms | 153.479 ms | 154.796 ms | 10.5% |
| 12 WPM, clean | 160.011 ms | 144.831 ms | 145.469 ms | 9.5% |
| 40 WPM, −3 dB | 47.139 ms | 45.579 ms | 49.012 ms | 3.3% |
| Overlapping carrier | 91.635 ms | 83.752 ms | 84.190 ms | 8.6% |
| 35 WPM, −5 dB, headroom | 109.062 ms | 103.402 ms | 105.108 ms | 5.2% |

Float reduces median streaming CPU by **3.3%–10.5%** and runs
**260–413× real time** on these inputs. The bit-helper gains over
the standard-math float control are small and overlap host timing variation;
they do not establish a universal speedup. The lattice search still costs
substantially more CPU than Vail. [All 900 timings and provenance](float-refactor-speed.json).

## CER comparison

The user accepted tiny CER changes after best-effort precision work. The
original strict noninferiority gate remains unchanged and can fail against
the merged double decoder. Those failures are reported explicitly.

| Dataset | Cases | Double mean CER | Float mean CER | Change, percentage points | Errors before → after | Strict gate |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| QSO, 15–40 WPM | 936 | 1.5221% | 1.5714% | +0.0493 | 637 → 657 | FAIL |
| Vail primary grid | 464 | 0.9664% | 0.9662% | -0.0002 | 235 → 235 | PASS |
| Additional grid | 120 | 2.7488% | 2.8265% | +0.0776 | 153 → 157 | FAIL |
| All-speed SNR | 8736 | 37.2264% | 37.2435% | +0.0171 | 145212 → 145309 | FAIL |
| Refined SNR | 6912 | 14.2025% | 14.1976% | -0.0050 | 43690 → 43692 | FAIL |

Recorded QSOs remain **13/170 errors** (2/57, 2/40 and 9/73), matching the
merged decoder. All five mean/pooled CER gates against the original pre-lattice
benchmark pass. This is development validation, not independent radio data.

The first float trial had 662 QSO errors. Compensated means reduce that to 657.
Wider extraction trials reduce QSO errors to 643–646, but do not restore all
strict gates; they retain more double arithmetic. Their complete synthetic
summaries are in the JSON. The selected build keeps float extraction and
preserves the small, disclosed tradeoff.

## SNR thresholds

SNR uses the same nominal key-down tone / Gaussian noise power in 2,500 Hz
before PCM clipping. The sustained 5% mean-CER threshold requires every higher
tested SNR to pass, with no interpolation. The refined sweep has three texts
and sixteen seeds per speed/SNR/generator.

| WPM | Headroom, double → float | Stock cwgen, double → float |
| ---: | ---: | ---: |
| 15 | -4 → -4 dB | -4 → -4 dB |
| 20 | -4 → -4 dB | -4 → -4 dB |
| 25 | -4 → -4 dB | -4 → -4 dB |
| 30 | -4 → -4 dB | -4 → -4 dB |
| 35 | -4 → -4 dB | -4 → -4 dB |
| 40 | -2 → -2 dB | -2 → -2 dB |

## Memory and checks

On this 64-bit build, per-channel payload falls from **523,152 to 413,384 bytes**
(21.0% reduction), including 618 word states and both index slots. Inline state
is 405,952 bytes; the complete payload leaves 110,904 bytes below the 512 KiB cap.
Float FFT arrays use 16*N+8 bytes for audio or 28*N for I/Q, excluding FFTW plans.

Release, static/serial, ASan/UBSan and the standard-math control each pass all
four Meson checks. New checks cover IEEE bit patterns, zeros, subnormals,
infinities, NaNs, compensated means and float public signatures. Existing
allocation-free streaming, symbol, duration, graph and parallel tests pass.
Formatting, manual-page lint and diff checks pass.

## Evidence and reproduction

[Machine-readable summary](float-refactor.json) includes per-speed/SNR curves,
1%/5%/10% thresholds, original-baseline comparisons, trial summaries, source and
binary hashes. Paired case reports: [QSO](float-refactor-qso.json.gz),
[primary](float-refactor-grid.json.gz), [additional](float-refactor-additional.json.gz),
[all-speed SNR](float-refactor-snr-full.json.gz), [refined SNR](float-refactor-snr-refined.json.gz),
and [recordings](float-refactor-recordings.json). The 15,728 distinct synthetic
WAVs include 14,208 distinct SNR WAVs; the two SNR reports overlap in 1,440 rows.

See [benchmark commands](../../benchmarks/README.md#float-api-and-repeat-measurements).
Use `archived-cer.py --baseline-prefix docs/benchmarks/lattice-gate` for merged
baseline replay; omit the prefix for original pre-lattice gates. The strict
replay exits nonzero when any dataset regresses. `recordings.py --abi 2` handles
the float API; `--abi 1` replays historical double libraries. Preserve fixed
binaries/source throughout a run and verify their hashes before comparing.
