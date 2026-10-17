# libmorsedec

A portable C11 library for streaming amateur radio CW decoding, licensed under
**GPL-3.0-or-later**. It uses FFTW3 for real/complex FFTs and combines
multicarrier tracking, adaptive duration modeling, and word segmentation in a
modular decoding pipeline.

The library discovers multiple signals, tracks a separate decoder and message
for each, and exposes a procedural `md_` API through an opaque handle.
Audio devices, GUI code and file formats stay outside the library. This makes
it suitable as a component for audio-band and I/Q CW skimmers.

Sample rate is configurable: the default is 48 kHz, with a 128 ms analysis window
and approximately 10.625 ms hop. The analysis band, FFT length, hop and channel
capacity are configurable too. There is no 8 kHz input requirement or internal
resampling to 8 kHz. See [decoder design and architecture](docs/design.md)
for module responsibilities, signal processing, timing, and implementation tradeoffs.

## Build

Dependencies: a C11 compiler, Meson >= 0.63, Ninja, FFTW3 (double precision).
The file-audio CLI uses libsndfile; the audio and I/Q stdin tools need no audio
I/O library but link ncurses for the optional `-T` terminal view.

```sh
# Debian/Ubuntu dependencies
sudo apt install meson ninja-build pkg-config libfftw3-dev libsndfile1-dev libncurses-dev clang-format
meson setup build
meson compile -C build
meson compile -C build format-check
meson install -C build
```

On Windows, build from an MSYS2 **MINGW64** shell with the `mingw-w64-x86_64-*`
packages. Use `-Dwerror=true` to turn warnings into errors.

```sh
# MSYS2 MINGW64 dependencies
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-meson \
  mingw-w64-x86_64-ninja mingw-w64-x86_64-pkgconf mingw-w64-x86_64-fftw \
  mingw-w64-x86_64-libsndfile mingw-w64-x86_64-ncurses
meson setup build
meson compile -C build
meson compile -C build format-check   # needs mingw-w64-x86_64-clang
```

The Windows build produces `libmorsedec-1.dll` with an import library and the
same tools with a `.exe` suffix. Executables in the build directory find the
DLL beside them; `libfftw3-3.dll` and `libsndfile-1.dll` come from the MINGW64
shell's `PATH`. Consumers that link the installed library must have the install
`bin` directory on `PATH`.

Use `-Dtools=disabled` for the library alone, `-Dtools=enabled` to require the
file CLI, or `-Ddefault_library=static` for a static library. The stdin CLI is
built whenever tools are enabled or auto. The ncurses terminal view is enabled
by default and controlled by `-Dtui`: use `-Dtui=disabled` to build the stdin
tools without it, or `-Dtui=auto` to accept whatever ncurses is available.
Per-channel parallelism is enabled automatically when pthreads are available
(winpthreads on MinGW). `-Dparallel=enabled` requires the backend;
`-Dparallel=disabled` builds a serial library. Run regression checks with
`meson test -C build`.

Installation includes
`morsedec.h` and `morsedec.pc`; consumers can use `dependency('morsedec')` in
Meson or `pkg-config --cflags --libs morsedec`.

The library core is portable C11 and file/device I/O stays in the CLI tools. On
macOS, install `meson ninja fftw libsndfile pkgconf` through Homebrew and use
the same commands as Linux. CI covers Linux, macOS and Windows/MINGW64.

## Decode and inspect audio

All three command-line tools accept `-V` to print their version,
copyright notice, and warranty disclaimer, without opening input or requiring
stream options.

Options use POSIX `getopt` short forms only. Boolean flags can be combined
(for example, `-aj`), and option arguments can be attached (`-r48000`) or
separate (`-r 48000`). Use `--` before a file name that starts with `-`.

```sh
build/morsedec-audio recording.wav
build/morsedec-audio -f 700 -t keying.csv recording.flac
build/morsedec-audio -a -j -l 100 -u 5000 wideband.wav
build/morsedec-audio -F s16 -r 48000 -n 2 -c 1 - < capture.pcm
```

WAV/FLAC/AIFF and other libsndfile formats are supported. Input sample rate comes
from the file. Stereo/multichannel input is averaged unless `-c` selects
one channel. Raw PCM is explicitly little endian; both `s16` and `f32` work.
`-h` lists the options.

Each output row is a **complete channel message snapshot**, which may revise
an earlier row. JSON Lines includes channel ID, stream time in seconds,
frequency, estimated SNR, WPM and text. CSV traces record frame, channel, keying
probability, strength, SNR, speed and state. Drain timestamps include synthetic
silence at EOF. Snapshots are bounded to 500 character records per channel.

## Capture and decode live

Use `arecord` or another recorder to supply raw PCM on stdin. Device selection
and capture buffering belong to the recorder.

```sh
arecord -t raw -f S16_LE -r 48000 -c 1 | build/morsedec-live -r 48000
arecord -D hw:1,0 -t raw -f S16_LE -r 48000 -c 1 | build/morsedec-live -r 48000 -f 700
arecord -t raw -f S16_LE -r 44100 -c 2 | build/morsedec-live -r 44100 -n 2 -c 2 -j
arecord -t raw -f S16_LE -r 48000 -c 1 -d 60 | build/morsedec-live -r 48000 -a -t live.csv
```

`-r` is required and must match the producer. Input defaults to signed 16-bit
little-endian PCM (`-F s16`); `-F f32` accepts little-endian float32.
Channels default to one; multiple channels are averaged unless `-c N`
selects one. Input has no WAV header: pass `-t raw` to `arecord`.

EOF, Ctrl-C and SIGTERM drain pending decoder text. `-s N` limits consumed
audio duration. Output uses the same complete-message snapshots as
`morsedec-audio`, flushed immediately for downstream pipes. `-j`, `-a`,
`-l`, `-u`, `-w` and CSV diagnostics work in both tools. Decoder
acquisition and DSP latency still apply. See [stdin implementation notes](docs/live-capture.md)
for stream behavior and shutdown handling.

### Terminal view

`-T` replaces the text snapshots with a two pane ncurses view on the terminal.
The left pane lists every tracked channel with its frequency, estimated SNR,
speed and state; the right pane shows the decoded messages, wrapped and
scrollable, with the newest line following the channel that is currently
decoding. `q` quits, `c` clears the messages, and the arrow, Page and Home/End
keys scroll the retained lines.

```sh
arecord -t raw -f S16_LE -r 48000 -c 1 | build/morsedec-live -r 48000 -T
build/morsedec-iqlive -F f32 -r 48000 -l -12000 -u 12000 -T < capture.iq
```

The view owns the terminal, not stdin, which keeps carrying the sample stream.
`-j` and `-t FILE` keep working beside it, so machine readable output can feed a
pipeline while the terminal shows the live view. ncurses is optional and enabled
by default; `-Dtui=disabled` builds the tools without `-T`. On Windows the view
attaches to the console and needs a real one rather than a pipe.

All decoder tools accept `-p N`: `0` (default) selects automatic per-channel
parallelism, `1` creates no workers, and larger values set a maximum participant
count including the caller. Automatic mode uses the online logical CPU count,
capped by channel capacity, and stays serial below four runnable channels. FFT
and band management remain serial. Worker wakeups can increase CPU use on small
workloads; compare `-p 1` and `-p 4` on representative captures.

## C API

The [library manual](docs/man/README.md) contains an overview,
`libmorsedec(3)`, and individual reference pages for every public function.
Read a local page with `man -l docs/man/libmorsedec.3` or, after installation,
`man 3 libmorsedec` and `man 3 md_process_iq`.

`md_new()` and `md_new_iq()` use automatic scheduling. Use
`md_new_with_threads(config, N)` or `md_new_iq_with_threads(config, N)` for an
explicit budget; `md_get_thread_count()` reports the resolved maximum including
the caller. Automatic initialization falls back to serial if workers cannot be
created. Explicit parallel requests report initialization failures. Reset retains
the pool, and deletion joins its workers. Calls on each handle still require
serialization, including queries, and processing completes all work before return.

Version 0.2.0 renames every public function from `morsedec_*` to `md_*`.
Update call sites and dynamic symbol lookups, then rebuild consumers against
shared-library ABI 1. Public data types, constants, and the `morsedec.h` header
retain their names. Internal helpers remain hidden from the shared library.

```c
#include <morsedec.h>

struct morsedec_config config;
md_config_init(&config);
config.sample_rate = 44100;
morsedec_decoder_t *decoder = md_new(&config);
if (!decoder) { /* inspect errno */ }
/* Feed normalized, mono PCM floats in arbitrary chunks. */
md_process(decoder, samples, sample_count);
/* Enumerate channels and copy their text after each chunk. */
md_flush(decoder); /* drain once at end of stream */
md_delete(decoder);
```

Every fallible operation returns `MORSEDEC_SUCCESS` (1) or `MORSEDEC_FAILURE` (0)
and sets `errno` on failure; creation returns a pointer or NULL. Scalar geometry
queries return -1 with no current channel. The header documents ownership and
buffer sizes. [examples/decode.c](examples/decode.c) is a compilable s16 example.

A skimmer host can supply one linear-power spectrum per hop through
`md_process_spectrum()`, bypassing the library FFT. Query the exact grid
with `md_get_geometry()`. Enumerate all channels by stable ID rather than
relying on automatic current-channel selection. Copy borrowed character views
before another mutating call. Serialize calls on each handle and serialize
construction/destruction with the host's other FFTW planner operations.

## I/Q stream skimming

Native complex input uses interleaved `I,Q` pairs representing `I + jQ`.
The sample rate counts complex pairs per second. All reported channel
frequencies are **signed Hz offsets from the input IF center (0 Hz)**:
`I=cos(2*pi*f*t), Q=sin(2*pi*f*t)` reports `+f`. Negative-frequency carriers
remain distinct from positive-frequency carriers. Add a receiver's center
frequency in the host if absolute RF frequencies are needed.

```c
struct morsedec_config config;
md_config_init(&config);
config.sample_rate = 48000;
config.min_frequency = -12000;
config.max_frequency = 12000;
morsedec_decoder_t *decoder = md_new_iq(&config);
if (!decoder) { /* inspect errno */ }
/* iq contains 2 * pair_count normalized floats: I0,Q0,I1,Q1,... */
md_process_iq(decoder, iq, pair_count);
/* md_process_iq_s16 also accepts interleaved signed 16-bit components. */
for (size_t i = 0; i < md_get_channel_count(decoder); i++) {
    struct morsedec_channel channel;
    if (md_get_channel(decoder, i, &channel)) {
        /* channel.frequency is the signed IF offset in Hz.
         * Copy text with md_get_text(decoder, channel.id, ...). */
    }
}
md_flush(decoder);
/* Collect final snapshots after draining, then delete the decoder. */
md_delete(decoder);
```

`md_new_iq(NULL)` defaults to 48 kHz and -1200..1200 Hz. With an explicit
configuration, set both band edges; `md_config_init()` retains audio
defaults for compatibility. Bands may span zero or lie entirely on either side,
within `[-sample_rate/2, sample_rate/2)`. The existing ten-bin tracking guards
apply at the retained band's edges. Frequencies are reported on the FFT grid.
There is no built-in DC-offset or I/Q-imbalance correction.

Use `md_pin_frequency(decoder, offset_hz)` to pin any signed offset,
including 0 Hz, and `md_unpin_frequency()` to release it. The older
`md_set_frequency()` still uses zero to release a pin. Since -1 Hz is a
valid offset, use channel enumeration or state to distinguish an absent current
channel from the scalar getter's -1 sentinel.

`morsedec-iqlive` reads I/Q pairs directly, defaulting to a -1200..1200 Hz
band. `morsedec-live` handles audio input. The file tool `morsedec-audio`
accepts `-i` for two-channel I/Q files. JSON `frequency` and CSV/text
frequencies contain signed offsets:

```sh
# Little-endian normalized float32 I,Q stream from an SDR:
build/morsedec-iqlive -F f32 -r 48000 -l -12000 -u 12000 -a -j < capture.iq
# Signed 16-bit I,Q file; discover both sides of zero:
build/morsedec-audio -i -F s16 -r 48000 -a -j capture.iq
# Pin a carrier at the IF center:
build/morsedec-iqlive -r 8000 -f 0 -j < centered-s16.iq
```

`morsedec-iqlive` always reads pairs and has no `-c`, `-n`, or
`-i` option. File inputs with `morsedec-audio -i` must have exactly two
channels, ordered I then Q, and cannot use `-c`. For arbitrary byte
streams, `morsedec-iqlive` retains
partial pairs between reads and rejects incomplete pairs at EOF. The C API
accepts complete pairs in arbitrary chunk sizes. External power spectra are also
accepted on I/Q handles, using their signed ascending grid and the calibration
in [docs/design.md](docs/design.md).

## Evaluation and current scope

[Comparison with Vail](docs/benchmarks/vail-comparison.md) reproduces its 464-case
synthetic grid: mean CER is **3.606% for libmorsedec versus 3.721% for Vail**,
improved from libmorsedec's 10.439% baseline. On 120 additional synthetic cases,
the figures are **4.478% versus 6.112%**. Measured streaming throughput is about
**1,700× real time**, using about 12% more CPU than Vail on that workload. Some noisy
and jittered conditions still favor Vail. Reproduction
scripts and complete per-case reports are included.

[Archived recording measurements](docs/benchmarks/README.md) document an earlier
three-recording tuning evaluation. Combined character error rate fell from
**47.65% to 14.12%**, ignoring whitespace while retaining punctuation and
distinguishing O from 0. These recordings were used for tuning and do not
establish accuracy on independent input. The current recording runner is in
`benchmarks/`; media fixtures remain external. Rerunning those recordings after
the current changes gives **19/170 errors (11.18% CER)**.

This remains an initial decoder implementation. Fast CW, difficult fading/QRM,
and other receiver recordings need further validation; weak spectral sidelobes
can still create extra channels. See [docs/design.md](docs/design.md) for current
architecture and limitations. GPLv3 text is in [COPYING](COPYING); distribution is under
GPL version 3 or any later version.
