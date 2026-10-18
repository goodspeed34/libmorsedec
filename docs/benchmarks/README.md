# Archived recording corpus evaluation

For the lattice decoder accuracy gate, see [lattice CER results](lattice-gate.md).
Its [CPU measurements](lattice-speed.md) compare the earlier decoder and Vail.
For the preceding synthetic head-to-head evaluation and recording regression
results, see [15–40 WPM ham CW results](wfst-ham.md). The preceding heuristic
decoder evaluation is retained in [Comparison with Vail decoder](vail-comparison.md). This page retains
the earlier tuning measurements below.

The three recordings supplied in `CW_TEST_SIGNALS` were used to tune acquisition,
keying confidence and channel selection. Their media files remain external. The
reference transcripts come from that folder's README, including its leading or
trailing partial characters; they have not been independently relabeled.

## Results

Default configuration, automatic frequency/channel selection, native 44.1 kHz
stereo input averaged to mono, one hop per API call, no frequency or WPM hints:

| Recording ID | Reference characters | Before errors / CER | After errors / CER |
| --- | ---: | ---: | ---: |
| UBlxpe5gvv0 (AI5DD) | 57 | 18 / 31.58% | 13 / 22.81% |
| 9AhkEDs2Sko (JO2QOT / K6XX) | 40 | 45 / 112.50% | 2 / 5.00% |
| 9jgZ94TzRys (WD4DAN / W0ABE) | 73 | 18 / 24.66% | 9 / 12.33% |
| **Combined** | **170** | **81 / 47.65%** | **24 / 14.12%** |

This is a **70.37% reduction in edit errors on the tuning corpus**. It does not
establish accuracy on independent recordings. The first clip still has errors
around handoffs and short acknowledgments; the other clips retain clipped
characters, an occasional extra mark, and trailing noise.

[Before](corpus-before.json) and [after](corpus-after.json) JSON reports contain
the complete decoded/reference strings, source and library SHA-256 checksums,
FFmpeg version, geometry and counts. The baseline library was rebuilt from the
source snapshot saved before this iteration; both used the same scoring script.

## Scoring conventions

The archived evaluation used the public C API through Python ctypes. It archived
current-channel snapshots by their source ordinals, applying later annotations at
the same ordinal, and assembled the result in source order. This preserves QSO
interleaving rather than concatenating final per-channel messages. It also retained
messages from channels that later retire. This is the benchmark's assembly policy;
it differs from concatenating the CLI's full snapshot output rows.

Only after decoding does the script compare against the reference. Neither
transcript text nor expected callsigns/frequencies control decoding or channel
selection. Uppercase conversion and whitespace removal are the only normalization;
punctuation remains, prosign letter pairs are compared as letters, and O/0 remain
different. Standard Levenshtein distance counts insertions, deletions and
substitutions. CER can exceed 100% when there are many inserted noise characters.
Combined CER divides total errors by total reference characters, not the mean of
clip percentages. Word spacing is not measured by this metric.

## Archive scope

The linked JSON reports retain decoded/reference strings, recording checksums,
geometry and scoring results. Media files remain external. The evaluation used
FFmpeg to convert recordings to signed 16-bit WAV while preserving sample rate
and channels. It enforced per-clip edit limits of 15, 6 and 12, plus a combined
limit of 33 edits.

The evaluation runner and fixtures are not included in the current distribution;
these results are historical measurements, not checks performed by the build or
CI. To conduct a new evaluation, feed labeled recordings through the public API,
archive revisions by channel ID and ordinal, and apply the scoring conventions
above. Compare both accuracy and false output on independent recordings before
extending the conclusions to other reception conditions.

## Changes retained

- Require sustained carrier evidence and cap keying confidence using delayed
  center-to-shoulder spectral contrast to reject broadband noise excursions.
- Rank channels using smoothed absolute power with 4× power hysteresis, and wait
  for actual duration-model training before automatic selection. A short flush
  does not masquerade as completed training; pinning still permits short input.
- Begin processing when the effective window main lobe is available, and start
  segmentation from an established gap so a signal at sample zero is retained.
- Reject revisited elements and overlapping committed time after carrier drops.

The changes contain no recording IDs, reference text or fixed station pitches.
See [decoder design and architecture](../design.md) for the signal-processing
pipeline, acquisition tradeoffs, and remaining high-speed limitations.
