/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * libmorsedec - streaming multichannel Morse code decoding library.
 * Copyright (C) 2026 Gong BI1XJT
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef MORSEDEC_H
#define MORSEDEC_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && !defined(MORSEDEC_STATIC)
#ifdef MORSEDEC_BUILD
#define MORSEDEC_API __declspec(dllexport)
#else
#define MORSEDEC_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#define MORSEDEC_API __attribute__((visibility("default")))
#else
#define MORSEDEC_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Version 0.3 uses float throughout the public API (shared-library ABI 2). */
#define MORSEDEC_VERSION "0.3.0"
#define MORSEDEC_SUCCESS 1
#define MORSEDEC_FAILURE 0
#define MORSEDEC_MESSAGE_CAPACITY 500

typedef struct morsedec_decoder morsedec_decoder_t;

enum morsedec_state {
	MORSEDEC_STATE_NONE,
	MORSEDEC_STATE_PAUSE,
	MORSEDEC_STATE_DECODING,
	MORSEDEC_STATE_LOCKED_PAUSE,
	MORSEDEC_STATE_LOCKED_DECODING
};

enum morsedec_word_type {
	MORSEDEC_WORD_GENERIC,
	MORSEDEC_WORD_DICTIONARY,
	MORSEDEC_WORD_REPORT,
	MORSEDEC_WORD_PROSIGN,
	MORSEDEC_WORD_QRL,
	MORSEDEC_WORD_CALLSIGN
};

struct morsedec_config {
	uint32_t sample_rate; /* Hz; default 48000. */
	size_t fft_size;      /* 0: even length nearest 128 ms. */
	size_t hop_size;      /* 0: nearest 10.625 ms; supported 5..20 ms. */
	float min_frequency;  /* Lower edge, Hz; signed IF offset for I/Q. */
	float max_frequency;  /* Exclusive upper edge, Hz, <= Nyquist. */
	size_t max_channels;  /* Preallocated tracking slots; default 32. */
	float initial_wpm;    /* 0: default duration priors (~23 WPM). */
};

struct morsedec_geometry {
	uint32_t sample_rate;
	size_t fft_size;
	size_t hop_size;
	size_t spectrum_size;
	float first_frequency;
	float bin_spacing;
	float frame_seconds;
};

struct morsedec_character {
	unsigned char character; /* Latin-1; prosigns are lowercase pairs. */
	enum morsedec_word_type word_type;
	uint64_t ordinal; /* Monotonic position on the channel timebase. */
	int low_snr;
	float word_space_probability; /* 2.0 = forced word end. */
	int word_end; /* 0: none, 1: one space, 2: two spaces. */
};

struct morsedec_channel {
	uint64_t id; /* Stable until retired; never an array index. */
	enum morsedec_state state;
	float frequency; /* Hz; signed offset from 0 Hz IF for I/Q. */
	float snr;
	float wpm;
	float key_probability;
	float strength;
	size_t character_count;
};

/* Return SUCCESS/FAILURE and set errno on failure, unless documented otherwise.
 * Calls on one handle must be serialized. Creation/deletion must also be
 * serialized with other FFTW planner operations in the host application.
 * Processing has no allocation, blocking I/O, or callbacks. */
MORSEDEC_API void md_config_init(struct morsedec_config *config);
MORSEDEC_API morsedec_decoder_t *md_new(const struct morsedec_config *config);
/* Complex I+jQ decoder. sample_rate counts pairs/second; the retained band
 * must lie in [-sample_rate/2, sample_rate/2). NULL uses 48 kHz, -1200..1200 Hz
 * and otherwise audio defaults. Configuration is copied. Positive complex
 * rotation (I=cos, Q=sin) gives positive Hz. No RF center offset is added. */
MORSEDEC_API morsedec_decoder_t *
md_new_iq(const struct morsedec_config *config);
/* Thread budgets include the caller: 0 selects automatic scheduling (also
 * used by md_new/md_new_iq), 1 creates no workers, larger values explicitly
 * enable parallel channel processing. Automatic mode uses the online logical
 * CPU count; all budgets are capped by max_channels.
 * Automatic scheduling stays serial below four runnable channels and falls
 * back to serial if worker initialization fails. Explicit parallel requests
 * fail with ENOTSUP when the backend is unavailable, or the initialization
 * error (e.g. ENOMEM/EAGAIN). Workers and buffers are owned by the decoder;
 * construction creates them, reset preserves them, and deletion joins them.
 * Input and snapshots retain their synchronous ownership and lifetime rules.
 */
MORSEDEC_API morsedec_decoder_t *
md_new_with_threads(const struct morsedec_config *config, size_t threads);
MORSEDEC_API morsedec_decoder_t *
md_new_iq_with_threads(const struct morsedec_config *config, size_t threads);
/* Resolved maximum processing participants, including caller; NULL returns 0.
 * Fewer participants are used when there are fewer runnable channels. */
MORSEDEC_API size_t md_get_thread_count(const morsedec_decoder_t *decoder);
MORSEDEC_API void md_delete(morsedec_decoder_t *decoder); /* NULL is OK. */
MORSEDEC_API int md_reset(morsedec_decoder_t *decoder);
MORSEDEC_API int md_get_geometry(const morsedec_decoder_t *decoder,
				 struct morsedec_geometry *geometry);
/* Arbitrary chunk sizes; mono normalized PCM floats or signed 16-bit PCM.
 * Non-finite samples become zero. Trailing partial hops are retained.
 * Consume each channel's bounded message snapshot after each call. */
MORSEDEC_API int md_process(morsedec_decoder_t *decoder, const float *samples,
			    size_t count);
MORSEDEC_API int md_process_s16(morsedec_decoder_t *decoder,
				const int16_t *samples, size_t count);
/* I/Q handles only: interleaved I0,Q0,I1,Q1,...; count is the number of
 * complex pairs (2*count scalar values), not bytes. Arbitrary pair-sized chunks
 * are accepted. Float components are normalized to [-1,1]; non-finite
 * components become zero. s16 components are divided by 32768.
 * Input is borrowed only during the call. NULL is valid only with count=0.
 * Audio input on an I/Q handle, or I/Q input on an audio handle, fails EINVAL.
 */
MORSEDEC_API int md_process_iq(morsedec_decoder_t *decoder,
			       const float *interleaved, size_t count);
MORSEDEC_API int md_process_iq_s16(morsedec_decoder_t *decoder,
				   const int16_t *interleaved, size_t count);
/* One linear-power frame with exactly geometry.spectrum_size bins, using
 * geometry's frequency grid/cadence and FE power scale (see docs/design.md).
 * Bins are in ascending frequency order, including signed offsets on I/Q
 * handles. Sample and external-spectrum input cannot be mixed without reset. */
MORSEDEC_API int md_process_spectrum(morsedec_decoder_t *decoder,
				     const float *power, size_t count);
/* Pad the partial hop and drain pipeline latency with silence; idempotent.
 * The next input starts a fresh stream (reset first). */
MORSEDEC_API int md_flush(morsedec_decoder_t *decoder);
MORSEDEC_API enum morsedec_state
md_get_state(const morsedec_decoder_t *decoder);
MORSEDEC_API size_t md_get_channel_count(const morsedec_decoder_t *decoder);
MORSEDEC_API int md_get_channel(const morsedec_decoder_t *decoder, size_t index,
				struct morsedec_channel *channel);
/* Nonzero Hz selects/pins the nearest channel (negative allowed on I/Q
 * handles), 0 releases the pin. Use pin_frequency to select exactly 0 Hz.
 * Frequencies are clamped to the usable spectrum (10-bin guards). */
MORSEDEC_API int md_set_frequency(morsedec_decoder_t *decoder, float hz);
/* Explicit pin/release operations; pin accepts 0 Hz and signed I/Q offsets. */
MORSEDEC_API int md_pin_frequency(morsedec_decoder_t *decoder, float hz);
MORSEDEC_API int md_unpin_frequency(morsedec_decoder_t *decoder);
/* Returns -1 with no current channel; -1 Hz is also a valid I/Q offset.
 * Use get_state or get_channel to distinguish absence from a signed offset. */
MORSEDEC_API float md_get_frequency(const morsedec_decoder_t *decoder);
MORSEDEC_API float md_get_snr(const morsedec_decoder_t *decoder);
MORSEDEC_API float md_get_wpm(const morsedec_decoder_t *decoder);
/* id=0 selects the current channel. A returned view lasts until the next
 * mutating call. No current channel: empty view. Unknown nonzero id: ENOENT. */
MORSEDEC_API int md_get_characters(const morsedec_decoder_t *decoder,
				   uint64_t id,
				   const struct morsedec_character **characters,
				   size_t *count);
/* Copies complete text including spaces; ENOSPC if capacity is insufficient.
 * required includes the NUL terminator; buffer=NULL, capacity=0 queries size.
 */
MORSEDEC_API int md_get_text(const morsedec_decoder_t *decoder, uint64_t id,
			     char *buffer, size_t capacity, size_t *required);
MORSEDEC_API uint64_t md_get_frame_count(const morsedec_decoder_t *decoder);

#ifdef __cplusplus
}
#endif
#endif
