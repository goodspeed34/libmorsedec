# Lattice decoder CPU cost

Measured after the CER runs, on an Intel Core i7-1165G7, Linux CPU 2.
Both library builds use GCC 15.2, release `-O3`, and the one-thread API. Vail
uses the pinned upstream revision and its existing frontend adapter with `-O3`.

Five rounds alternate decoder order; each round contains nine process-CPU
samples per decoder/input, giving 45 samples per table cell. Each input is
preloaded 8 kHz mono PCM. Timing includes frontend/channel processing, acquisition,
replay and a four-second silent tail. Construction, I/O, teardown and explicit
EOF flush are excluded.

## Results

| Input | Earlier CPU | Lattice CPU | Vail CPU | Lattice / earlier | Lattice real-time speed |
| --- | ---: | ---: | ---: | ---: | ---: |
| 20 WPM, +3 dB | 20.937 ms | 141.729 ms | 17.805 ms | 6.77× | 282× |
| 12 WPM, clean | 28.540 ms | 132.579 ms | 21.798 ms | 4.65× | 303× |
| 40 WPM, −3 dB | 9.706 ms | 45.073 ms | 7.767 ms | 4.64× | 392× |
| Overlapping carrier | 21.049 ms | 89.062 ms | 16.889 ms | 4.23× | 389× |
| 35 WPM, −5 dB, unclipped | 15.944 ms | 106.164 ms | 12.288 ms | 6.66× | 263× |

The current lattice decoder uses **4.23–6.77× the CPU of the earlier decoder**
and **5.27–8.64× Vail** on these inputs. The [CER improvements](lattice-gate.md)
therefore come with a substantial CPU increase. These medians correspond to
about 0.26–0.38% of one core for one real-time stream on this host. They do not
establish a worst-case per-call latency bound or throughput for many carriers.

## Control and provenance

Rebuilt pre-lattice control reproduces all 1520 archived synthetic decoded outputs and the 14/170 recording error count. Its source/binary checksums differ from the original archived benchmark; comparisons here use freshly measured control times, not historical timings.

[Raw timings and checksums](lattice-speed.json) retain all 675 CPU samples,
input/library/executable/source hashes, build flags and ratios. Streaming CPU
time is separate from the concurrent wall times in the CER reports. No decoder
implementation was changed for this measurement.

## Reproduction

Compile `benchmarks/speed.c` against each fixed library with `-O3
-DMD_BENCH_SERIAL`; compile the Vail adapter with `-O3 -DVAIL_BASELINE` and
the pinned Vail sources. See [CPU benchmark commands](../../benchmarks/README.md).
Pass each table input to `benchmarks/wfst-speed.py`, with the three decoder
executables, matching libraries, `--rounds 5 --cpu 2`, and an output JSON path.
Use an allowed CPU on other hosts; compare medians measured in the same run.
