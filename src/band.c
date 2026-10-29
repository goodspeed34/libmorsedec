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
#include "internal.h"
#include <simde/x86/sse.h>

struct md_channel *md_channel_new(struct morsedec_decoder *d, int bin)
{
	for (size_t i = 0; i < d->config.max_channels; i++) {
		struct md_channel *c = &d->channels[i];
		if (!c->used) {
			md_channel_init(d, c, bin);
			d->order[d->count++] = c;
			return c;
		}
	}
	return NULL;
}

static void sort_channels(struct morsedec_decoder *d)
{
	for (size_t i = 1; i < d->count; i++) {
		struct md_channel *value = d->order[i];
		size_t j = i;
		while (j && d->order[j - 1]->bin > value->bin) {
			d->order[j] = d->order[j - 1];
			j--;
		}
		d->order[j] = value;
	}
}

static void merge_channels(struct morsedec_decoder *d)
{
	if (d->count < 2)
		return;
	sort_channels(d);
	for (size_t i = d->count - 1; i > 0; i--) {
		struct md_channel *a = d->order[i - 1], *b = d->order[i];
		struct md_channel *kill;
		if (a->state >= 2 || b->state >= 2 || b->bin - a->bin > 3)
			continue;
		if (a->pinned || a == d->current)
			kill = b;
		else if (b->pinned || b == d->current)
			kill = a;
		else if (a->carrier != b->carrier)
			kill = a->carrier ? b : a;
		else
			kill = a->message.count > b->message.count ? b : a;
		kill->state = 3;
	}
}

void md_band_frame(struct morsedec_decoder *d, const float *s)
{
	int n = d->bins;
	size_t jobs = 0;
	/* Bins come from the geometry and are always positive; stating it keeps
	 * the clearing size provable for the compiler. */
	if (n <= 0)
		return;
	float decay = powf(0.97f, d->tick_scale);
	float peak_decay = powf(0.8f, d->tick_scale);

	d->frames++;
	if (d->frames % (uint64_t)d->merge_period == 0)
		merge_channels(d);
	memcpy(d->history + (d->frames % (uint64_t)d->history_size) * (size_t)n,
	       s, (size_t)n * sizeof(float));
	/* Both recursions run across frames, not across bins, so every bin is
	 * independent and vectorizes without changing the arithmetic. The
	 * baseline has no FMA, so lane results stay bit-identical. */
	{
		simde__m128 vdecay = simde_mm_set1_ps(decay);
		simde__m128 vrest = simde_mm_set1_ps(1 - decay);
		simde__m128 vfloor = simde_mm_set1_ps(1e-12f);
		simde__m128 vpeak = simde_mm_set1_ps(peak_decay);
		simde__m128 vtwice = simde_mm_set1_ps((1 - peak_decay) * 2);
		simde__m128 vcap = simde_mm_set1_ps(1e20f);
		simde__m128 vzero = simde_mm_setzero_ps();
		int i = 0;

		for (; i + 4 <= n; i += 4) {
			simde__m128 raw = simde_mm_loadu_ps(s + i);
			simde__m128 previous = simde_mm_loadu_ps(d->smooth + i);
			simde__m128 blended =
			    simde_mm_add_ps(simde_mm_mul_ps(vdecay, previous),
					    simde_mm_mul_ps(vrest, raw));

			simde_mm_storeu_ps(
			    d->smooth + i,
			    simde_mm_and_ps(simde_mm_cmpge_ps(raw, vfloor),
					    blended));
		}
		for (; i < n; i++)
			d->smooth[i] = s[i] >= 1e-12f
					   ? (float)(decay * d->smooth[i] +
						     (1 - decay) * s[i])
					   : 0;
		for (i = 10; i + 4 <= n - 10; i += 4) {
			simde__m128 den = simde_mm_max_ps(
			    simde_mm_loadu_ps(d->smooth + i - 5),
			    simde_mm_loadu_ps(d->smooth + i + 5));
			simde__m128 step = simde_mm_div_ps(
			    simde_mm_mul_ps(vtwice,
					    simde_mm_loadu_ps(d->smooth + i)),
			    den);
			simde__m128 sum = simde_mm_add_ps(
			    simde_mm_mul_ps(vpeak,
					    simde_mm_loadu_ps(d->ratio + i)),
			    step);

			simde_mm_storeu_ps(
			    d->ratio + i,
			    simde_mm_and_ps(simde_mm_cmpgt_ps(den, vzero),
					    simde_mm_min_ps(vcap, sum)));
		}
		for (; i < n - 10; i++) {
			float den = md_max(d->smooth[i - 5], d->smooth[i + 5]);

			d->ratio[i] =
			    den > 0 ? (float)md_min(1e20f,
						    peak_decay * d->ratio[i] +
							(1 - peak_decay) * 2 *
							    d->smooth[i] / den)
				    : 0;
		}
	}
	memset(d->peaks, 0, (size_t)n);
	if (d->frames > (uint64_t)d->warmup) {
		/* Classify four bins at a time, then replay the scalar loop's
		 * skip of the bin that follows a peak. */
		simde__m128 vthreshold = simde_mm_set1_ps(3.5f);
		int i = 10;
		int skipped = 0;

		for (; i + 4 <= n - 10; i += 4) {
			simde__m128 r = simde_mm_loadu_ps(d->ratio + i);
			unsigned int bits =
			    (unsigned int)simde_mm_movemask_ps(simde_mm_and_ps(
				simde_mm_cmpgt_ps(r, vthreshold),
				simde_mm_and_ps(
				    simde_mm_cmpge_ps(
					r, simde_mm_loadu_ps(d->ratio + i - 1)),
				    simde_mm_cmpge_ps(
					r,
					simde_mm_loadu_ps(d->ratio + i + 1)))));

			if (skipped)
				bits &= ~1u;
			skipped = 0;
			for (int lane = 0; lane < 4; lane++) {
				if (!(bits & (1u << lane)))
					continue;
				d->peaks[i + lane - 1] = 0;
				d->peaks[i + lane] = 1;
				if (lane == 3)
					skipped = 1;
				else
					bits &= ~(1u << (lane + 1));
			}
		}
		if (skipped)
			i++;
		for (; i < n - 10; i++) {
			if (d->ratio[i] > 3.5f &&
			    d->ratio[i] >= d->ratio[i - 1] &&
			    d->ratio[i] >= d->ratio[i + 1]) {
				d->peaks[i - 1] = 0;
				d->peaks[i] = 1;
				i++;
			}
		}
	}
	for (size_t i = 0; i < d->count;) {
		struct md_channel *c = d->order[i];
		if (c->state == 3 && !c->pinned) {
			if (d->current == c)
				d->current = NULL;
			c->used = 0;
			memmove(d->order + i, d->order + i + 1,
				(d->count - i - 1) * sizeof(*d->order));
			d->count--;
		} else {
			for (int j = -2; j <= 2; j++)
				d->peaks[c->bin + j] = 0;
			i++;
		}
	}
	for (int i = 10; i < n - 10; i++) {
		if (d->peaks[i] && d->count < d->config.max_channels) {
			md_channel_new(d, i);
			for (int j = -2; j <= 2; j++)
				d->peaks[i + j] = 0;
		}
	}
	if (d->frames % (uint64_t)d->merge_period == 0) {
		float slice[40];
		int length = n < 40 ? n : 40;
		memcpy(slice, s + (n - length) / 2,
		       (size_t)length * sizeof(float));
		for (int i = 1; i < length; i++) {
			float value = slice[i];
			int j = i;
			while (j && slice[j - 1] > value) {
				slice[j] = slice[j - 1];
				j--;
			}
			slice[j] = value;
		}
		d->noise = slice[length / 2];
	}
	for (size_t i = 0; i < d->count; i++) {
		struct md_channel *c = d->order[i];
		c->changed = 0;
		if (d->mode == 1) {
			/* Wait for the centered sinc's effective main lobe, not
			 * the whole zero-padded FFT aperture. */
			uint64_t valid =
			    (d->geometry.fft_size / 2 +
			     (size_t)(d->geometry.sample_rate * .020f) +
			     d->geometry.hop_size - 1) /
			    d->geometry.hop_size;
			if (c->next_frame < valid)
				c->next_frame = valid;
		}
		if (c->next_frame <= d->frames)
			d->jobs[jobs++] = c;
	}
	md_parallel_run(d, jobs);
	md_band_select(d);
	sort_channels(d);
}

void md_band_select(struct morsedec_decoder *d)
{
	/* Compare absolute spectral power: differently aged normalization
	 * floors must not promote a weak noise channel over a real signal. */
	if (!d->current || !d->current->pinned) {
		struct md_channel *best = NULL;
		for (size_t i = 0; i < d->count; i++) {
			struct md_channel *c = d->order[i];
			if (c->state == 1 && c->age < md_frames(d, 140) &&
			    (!best || c->power > best->power))
				best = c;
		}
		if (best &&
		    (!d->current || best->power > 4 * d->current->power))
			d->current = best;
	}
}

void md_channel_replay(const struct morsedec_decoder *d, struct md_channel *c)
{
	for (int replay = 0; replay < 4 && c->next_frame <= d->frames;
	     replay++) {
		const float *frame =
		    d->history + (c->next_frame % (uint64_t)d->history_size) *
				     (size_t)d->bins;
		md_signal_frame(d, c, frame, c->next_frame++);
	}
}
