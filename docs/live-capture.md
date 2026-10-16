# Live decoding from stdin

`morsedec-live` reads raw audio PCM and `morsedec-iqlive` reads raw I/Q pairs
from stdin on Unix-like systems. Both link only to libmorsedec and the math library. Audio device access belongs to a separate
recorder such as `arecord`; no capture backend is integrated into the project.
The file decoder `morsedec-audio` additionally uses libsndfile.

## Build and use

`meson setup build` and `meson compile -C build` build both stdin tools without
additional audio dependencies. `-Dtools=disabled` omits all command-line tools.

```sh
arecord -t raw -f S16_LE -r 48000 -c 1 | build/morsedec-live -r 48000
arecord -D hw:1,0 -t raw -f S16_LE -r 44100 -c 2 | build/morsedec-live -r 44100 -n 2 -c 2 -f 700 -j
```

Select the capture device with `arecord -D`; use `arecord -l` to list hardware.
Always use `-t raw`: stdin has no container header. `-r` is required and must
match the recorder; `-n` defaults to one. Multichannel samples are
interleaved and averaged unless `-c N` selects a 1-based channel.
`-F s16` (default) means signed 16-bit little endian; `-F f32` means IEEE
754 binary32 little endian. No resampling is performed. File redirection also
works: `build/morsedec-live -r 48000 < recording.s16`.

For SDR input, `morsedec-iqlive` reads I,Q pairs directly, with a default
-1200..1200 Hz retained band. Set `-l` and
`-u` to widen it. The rate is complex pairs/second; reported frequencies are
signed Hz offsets from the stream's 0 Hz IF center. `I+jQ` with positive complex
rotation reports a positive offset. `-f 0` pins the IF center;
negative pin frequencies are also supported. There are no `-c`,
`-n`, or `-i` options; the input always contains I then Q.

```sh
build/morsedec-iqlive -F f32 -r 48000 -l -12000 -u 12000 -a -j < capture.iq
```

## Stream handling and shutdown

The Unix CLI uses poll/read, retaining incomplete samples or channel frames
across reads. Each read processes available complete frames, up to one decoder
hop, without waiting for a full hop. An incomplete frame at EOF is an error;
empty input is valid. Byte conversion is independent of host endianness.

EOF, Ctrl-C, SIGTERM or `-s N` flush pending decoder output and release
resources. The duration counts input frames, rather than wall-clock time.
Signals remain responsive while stdin is idle. Signal handlers only set a flag;
all decoding and cleanup happen in the main loop. I/O failures return nonzero.

Text and JSON Lines are complete message snapshots, including revisions, flushed
after processing so downstream pipes receive live updates. CSV diagnostics use
`-t FILE`. Timestamps count consumed audio and include synthetic drain
silence at shutdown. PCM contains no timestamps or discontinuity markers, so the
decoder cannot identify missing samples. Monitor the recorder's stderr for
capture overruns; restart decoding after an audio discontinuity. Slow downstream
consumers can propagate backpressure through the pipeline.

## Operational checks

The stdin tools can be exercised with recorded input without an audio device.
Use input with a known sample rate and byte format, and confirm that reported
frequencies and message snapshots match the capture. Partial byte frames are
retained across reads; an incomplete frame at EOF is an error. Empty input is
valid. For live capture, confirm snapshots arrive before EOF and shutdown drains
pending output when stdin closes or the process receives SIGINT/SIGTERM.
