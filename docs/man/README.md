# Command-line and library manuals

The command-line tools have section 1 manuals: [morsedec-audio(1)](morsedec-audio.1),
[morsedec-live(1)](morsedec-live.1), and
[morsedec-iqlive(1)](morsedec-iqlive.1). They document file and raw PCM/IQ input,
short options, output modes, stream termination, and examples.

Meson installs them in `mandir/man1`:

```sh
man 1 morsedec-audio
man 1 morsedec-live
man 1 morsedec-iqlive
```

## Library function manuals

[LIBMORSEDEC(3)](libmorsedec.3) is the overview manual: configuration, data types,
streaming lifecycle, audio and signed I/Q conventions, external-spectrum
calibration, channel selection, message revisions, ownership, errors, thread
safety, and a complete I/Q consumer example.

Each public function has a separate section 3 page, following the organization
of the [SDL2 function reference](https://wiki.libsdl.org/SDL2/SDL_Init): synopsis,
function parameters, return value, applicable errors, remarks, thread safety,
version, and related functions. The content describes libmorsedec 0.3.0 and is
checked against `include/morsedec.h` and the implementation.

Read from the source tree:

```sh
man -l docs/man/libmorsedec.3
man -l docs/man/md_process_iq.3
man -l docs/man/md_get_text.3
```

Meson installs the overview and all function pages into the configured
`mandir/man3` directory alongside the library:

```sh
meson compile -C build
meson install -C build
man 3 libmorsedec
man 3 md_new_iq
```

The following are installed man pages, not links to internal helper functions.
The public header and data-type names retain their `morsedec` names; exported
function names use `md_`.

| Function group | Manual pages |
| --- | --- |
| Configuration and lifetime | [md_config_init(3)](md_config_init.3), [md_new(3)](md_new.3), [md_new_iq(3)](md_new_iq.3), [md_delete(3)](md_delete.3), [md_reset(3)](md_reset.3) |
| Channel parallelism | [md_new_with_threads(3)](md_new_with_threads.3), [md_new_iq_with_threads(3)](md_new_iq_with_threads.3), [md_get_thread_count(3)](md_get_thread_count.3) |
| Geometry | [md_get_geometry(3)](md_get_geometry.3) |
| Audio input | [md_process(3)](md_process.3), [md_process_s16(3)](md_process_s16.3) |
| I/Q input | [md_process_iq(3)](md_process_iq.3), [md_process_iq_s16(3)](md_process_iq_s16.3) |
| Spectrum input and draining | [md_process_spectrum(3)](md_process_spectrum.3), [md_flush(3)](md_flush.3) |
| Channel enumeration and state | [md_get_channel_count(3)](md_get_channel_count.3), [md_get_channel(3)](md_get_channel.3), [md_get_state(3)](md_get_state.3) |
| Frequency selection | [md_set_frequency(3)](md_set_frequency.3), [md_pin_frequency(3)](md_pin_frequency.3), [md_unpin_frequency(3)](md_unpin_frequency.3) |
| Scalar channel queries | [md_get_frequency(3)](md_get_frequency.3), [md_get_snr(3)](md_get_snr.3), [md_get_wpm(3)](md_get_wpm.3) |
| Message snapshots | [md_get_characters(3)](md_get_characters.3), [md_get_text(3)](md_get_text.3) |
| Frame timebase | [md_get_frame_count(3)](md_get_frame_count.3) |

The `.3` files are the maintained sources in portable roff `man` format. Editing
or installing them requires no document-generation dependency. To validate and
preview changes with optional documentation tools:

```sh
mandoc -T lint docs/man/*.3
mandoc -T utf8 docs/man/libmorsedec.3
# Alternative renderer:
groff -man -Tutf8 docs/man/md_process_iq.3
```

When adding an API function, add its manual page to this index and
[meson.build](meson.build), update related-function references and the overview,
and verify its prototype and behavior against the public header. The manual
examples use caller-owned buffers and check fallible API calls.
