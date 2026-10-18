# Lattice decoder CER gate

Measured against the latest pre-lattice benchmark on identical cached WAVs.
Synthetic CER includes spaces after uppercase conversion and whitespace collapse.
Recording CER removes whitespace and assembles chronological snapshots by ordinal.
Punctuation and O/0 remain distinct. Default acquisition receives no transcript,
frequency or speed hints.

## Aggregate accuracy gate

Both **mean and pooled CER must be no worse** on each complete synthetic dataset.
The radio corpus must have no more total errors. Equality is accepted.

| Dataset | Cases | Mean CER before → after | Pooled CER before → after | Errors before → after | Gate |
| --- | ---: | ---: | ---: | ---: | --- |
| QSO, 15–40 WPM | 936 | 1.840% → 1.522% | 1.769% → 1.512% | 745 → 637 | PASS |
| Primary grid | 464 | 1.138% → 0.966% | 1.394% → 1.241% | 264 → 235 | PASS |
| Additional grid | 120 | 2.917% → 2.749% | 3.487% → 3.355% | 159 → 153 | PASS |
| SNR, all integer speeds | 8736 | 38.187% → 37.226% | 37.874% → 36.938% | 148890 → 145212 | PASS |
| SNR, common speeds / more seeds | 6912 | 15.232% → 14.203% | 14.991% → 14.046% | 46628 → 43690 | PASS |
| Recorded QSOs | 3 | — | 8.235% → 7.647% | 14 → 13 / 170 | PASS |

The SNR reports overlap in 1,440 cases: together they contain 14,208 distinct
WAVs. The other synthetic grids contain 1,520 cases. Complete paired results,
input/source/binary checksums, per-speed and per-SNR summaries are retained in
[the machine-readable report](lattice-gate.json) and its linked compressed raw reports.
Raw paired cases: [QSO](lattice-gate-qso.json.gz), [primary](lattice-gate-grid.json.gz),
[additional](lattice-gate-additional.json.gz), [all-speed SNR](lattice-gate-snr-full.json.gz),
[refined SNR](lattice-gate-snr-refined.json.gz), and [recordings](lattice-gate-recordings.json).
Wall times in the raw data include concurrent processes and are not CPU benchmarks.

## Recorded QSOs

| Recording | Before errors | After errors | Reference characters |
| --- | ---: | ---: | ---: |
| UBlxpe5gvv0 | 5 | 2 | 57 |
| 9AhkEDs2Sko | 1 | 2 | 40 |
| 9jgZ94TzRys | 8 | 9 | 73 |

The aggregate radio gate passes; two recordings each regress by one edit.
This is a small development corpus, not independent validation. Synthetic cases
also include individual regressions; the JSON reports their counts.

## Sustained 5% mean CER threshold

SNR is nominal key-down tone power / Gaussian noise power in 2,500 Hz, before
PCM clipping. This is the same metric as the earlier SNR benchmark. A threshold
requires every higher tested SNR to pass too; values are sampled, not interpolated.
The common-speed sweep uses three QSO texts and 16 seeds per SNR and generator.

| WPM | Headroom before → after | Stock cwgen before → after |
| ---: | ---: | ---: |
| 15 | -4 → -4 dB | -4 → -4 dB |
| 20 | -4 → -4 dB | -4 → -4 dB |
| 25 | -4 → -4 dB | -4 → -4 dB |
| 30 | -4 → -4 dB | -4 → -4 dB |
| 35 | -3 → -4 dB | -3 → -4 dB |
| 40 | -2 → -2 dB | -2 → -2 dB |

Detailed 1%, 5% and 10% thresholds and all CER curves are in the JSON report.
Aggregate CER improvement does not guarantee improvement at every speed/SNR.
For example, the all-speed sweep mean at -1 dB rises from 0.716% to 0.750%;
at -11 dB it rises from 90.599% to 91.285%. The gate applies to complete datasets.

## Changes made while enforcing the gate

The first lattice implementation failed all four original accuracy gates. Its
QSO/primary/additional mean CERs were 1.930%, 2.196% and 2.961%; radio errors
were 34/170. The accepted implementation uses these corrections:

- Preserve the original acquisition merging and 8-unit beam before calibration.
- Use an 8-unit mark beam and a 10-unit gap/low-SNR beam. The proposed 6-unit
  clear-mark beam regressed accuracy on these cases.
- Merge identical text histories only when physical boundaries are within one
  nominal tick; apply the diversity rank penalty within two ticks.
- Use normalized word-boundary log probabilities in joint rescoring.
- Keep channel liveness tied to completed candidate labels while publication
  waits for lattice convergence.
- Release pending labels after a long observed gap without waiting for every
  surviving duration tail to reach idle; preserve physical gap ages.

## Verification and reproduction

Release, static, and ASan/UBSan builds pass all four automated checks. Checks
cover allocation-free streaming, bounded lattice depth, word-boundary scoring,
pool pressure/reuse, live/EOF output, graph regeneration and parallel equivalence.
Formatting and `git diff --check` pass. Current 64-bit per-channel state is
523,152 bytes including word states and both index slots: 1,136 bytes below 512 KiB.

See [benchmark commands](../../benchmarks/README.md#replay-the-archived-cer-gate).
`benchmarks/archived-cer.py` verifies WAV hashes against each saved baseline,
uses exact rational comparisons for mean CER and integer edit totals for pooled
CER, and exits nonzero on a failed or incomplete dataset. Its checkpoint is
bound to decoder binary/library/source and input/reference hashes. The existing
strict `--require-better` gate remains strict; `--require-not-worse` accepts equality.

These results were used for development and policy selection. They do not
establish generalization to independent recordings, other interference or
unmeasured SNRs. [Measured CPU cost](lattice-speed.md) increased 4.23–6.77×
against the rebuilt pre-lattice control on five representative inputs.
