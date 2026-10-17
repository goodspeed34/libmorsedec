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

void md_filter_init(struct md_filter *f, int n)
{
	memset(f, 0, sizeof(*f));
	f->n = n;
}

/* Block suffix extrema: constant amortized work and first-value priming. */
double md_extreme(struct md_filter *f, double x, int maximum)
{
	int n = f->n;

	if (!f->primed) {
		for (int i = 0; i < n; i++)
			f->ring[i] = x;
		f->pos = n - 1;
		f->acc = x;
		f->primed = 1;
		return x;
	}
	f->ring[++f->pos] = x;
	if (f->pos == 2 * n - 1) {
		double acc = x;

		for (int i = n - 2; i >= 0; i--) {
			f->ring[i] = acc;
			acc = maximum ? fmax(acc, f->ring[i + n])
				      : fmin(acc, f->ring[i + n]);
		}
		f->acc = x;
		f->pos -= n;
		return acc;
	}
	f->acc = maximum ? fmax(f->acc, x) : fmin(f->acc, x);
	return maximum ? fmax(f->acc, f->ring[f->pos - n])
		       : fmin(f->acc, f->ring[f->pos - n]);
}

double md_average(struct md_filter *f, double x)
{
	f->ring[f->pos] = x;
	/* Re-sum each wrap to bound accumulated roundoff. */
	if (!f->pos) {
		f->sum = 0;
		for (int i = 0; i < f->n; i++)
			f->sum += f->ring[i];
	} else {
		f->sum += x - f->acc;
	}
	f->pos = (f->pos + 1) % f->n;
	f->acc = f->ring[f->pos];
	return f->sum / f->n;
}

void md_channel_init(struct morsedec_decoder *d, struct md_channel *c, int bin)
{
	struct md_word_state *states = c->word.states;

	memset(c, 0, sizeof(*c));
	c->word.states = states;
	c->bin = bin;
	c->used = 1;
	c->id = ++d->next_id;
	c->next_frame = d->frames > (uint64_t)d->history_size
			    ? d->frames - d->history_size + 1
			    : 1;
	md_filter_init(&c->floor_max, md_frames(d, 11));
	md_filter_init(&c->floor_min, md_frames(d, 51));
	md_filter_init(&c->env_avg, md_frames(d, 71));
	md_filter_init(&c->peak_hold, md_frames(d, 141));
	md_filter_init(&c->max_short, md_frames(d, 17));
	md_filter_init(&c->thr_avg, md_frames(d, 9));
	md_filter_init(&c->thr_min, md_frames(d, 111));
	md_stats_init(d, &c->stats);
	md_segment_init(d, c);
	md_word_reset(d, &c->word);
}

void md_signal_frame(const struct morsedec_decoder *d, struct md_channel *c,
		     const float *s, uint64_t frame)
{
	double shape[21], shoulder = 0, outer = 0;
	double mx, mn, amp, prom, sample, v, input;
	double (*mem)[8] = c->memory;
	int old_carrier = c->carrier;
	int found = 0, offset = 0;

	/* Track an undilated power maximum behind a peakiness gate. */
	for (int off = -2; off <= 2; off++) {
		int i = c->bin + off;
		if (d->ratio[i] > 7.5 && s[i] > s[i - 1] && s[i] > s[i + 1]) {
			found = 1;
			offset = off;
			break;
		}
	}
	if (found) {
		int move;
		c->offset = pow(0.97, d->tick_scale) * c->offset +
			    (1 - pow(0.97, d->tick_scale)) * offset;
		move = (int)nearbyint(c->offset);
		c->bin += move;
		c->offset -= move;
	}
	c->bin = (int)md_clamp(c->bin, 10, d->bins - 11);
	c->power = pow(0.97, d->tick_scale) * c->power +
		   (1 - pow(0.97, d->tick_scale)) * s[c->bin];
	mx = md_extreme(&c->floor_max, d->noise, 1);
	mn = md_extreme(&c->floor_min, mx, 0);
	c->floor = pow(0.99, d->tick_scale) * c->floor +
		   (1 - pow(0.99, d->tick_scale)) * mn;
	if (c->floor == 0)
		c->floor = d->noise;
	if (c->floor < 1e-20)
		c->floor = s[c->bin];
	for (int i = 0; i < 21; i++)
		shape[i] = s[c->bin + i - 10] / (c->floor + 1e-12);
	for (int i = 4; i <= 10; i++) {
		double power = log(shape[10 - i] * shape[10 + i] + 1e-12);

		if (i <= 8)
			shoulder += power;
		if (i >= 6)
			outer += power;
	}
	amp = sqrt(shape[10]);
	prom = fmax(0, amp - exp(shoulder / 16));
	/* Bound threshold adaptation on nearly noiseless signals. Otherwise
	 * tiny floor estimates give long marks enough leverage to hide the
	 * following shorter marks, despite their ample absolute SNR. */
	prom = fmin(prom, 200);

	/* Delay taps and filter lengths are expressed in physical time. */
	memmove(mem + 1, mem, (size_t)md_frames(d, 38) * sizeof(*mem));
	memset(mem[0], 0, sizeof(*mem));
	mem[0][0] = amp;
	mem[0][1] = prom;
	/* Dimensionless log power contrast against ten shoulder bins. */
	mem[0][3] = log(shape[10] + 1e-12) - shoulder / 10;
	mem[0][5] = log(shape[10] + 1e-12) - outer / 10;
	sample =
	    fmin(mem[md_frames(d, 1)][1], fmax(mem[md_frames(d, 2)][1], prom));
	mem[0][2] = sample;
	c->envelope = md_average(&c->env_avg, sample);
	c->carrier = c->envelope > (old_carrier ? 1.0 : 1.3);
	c->quiet = c->carrier ? 0 : c->quiet + (c->quiet < md_frames(d, 100));
	c->strength = md_extreme(&c->peak_hold, c->envelope, 1);
	v = md_average(&c->thr_avg, md_extreme(&c->max_short, sample, 1));
	mem[0][4] = 0.4 * fmax(v - 0.8, 0) + 0.8;
	if (mem[md_frames(d, 13)][2] <
		fmax(mem[md_frames(d, 12)][2], mem[md_frames(d, 14)][2]) ||
	    mem[1][4] > 3 || mem[md_frames(d, 13)][2] <= 0.5 * (mem[1][6] + 3))
		input = 3;
	else
		input = fmin(3, 0.35 * fmax(mem[md_frames(d, 14)][1] - 0.8, 0) +
				    0.8);
	v = fmax(md_extreme(&c->thr_min, input, 0), mem[md_frames(d, 23)][4]);
	mem[0][6] = v;
	input = fmax(0.3, mem[md_frames(d, 36)][1]) - v;
	input = input > 5 ? 0.9999 : input < -5 ? -0.9999 : tanh(input);
	c->probability = c->carrier ? 0.5 * input + 0.5 : 0.003;
	{
		/* Use the same delayed time as the keying observation. A high
		 * amplitude alone is insufficient evidence of a tonal mark. */
		double contrast = mem[md_frames(d, 36)][3];
		/* Short marks extend into the inner shoulders. Infer their
		 * width from the full timing model, so hand-keyed dots alone do
		 * not weaken noise rejection. Apply the latest estimate to both
		 * observations at the same delayed time. */
		double fast = md_clamp((md_speed(&c->stats) - 28) / 6, 0, 1);
		contrast += fast * (mem[md_frames(d, 36)][5] - contrast);
		double confidence = 0.5 * tanh(2 * (contrast - log(8))) + 0.5;
		c->probability =
		    fmax(0.00005, fmin(c->probability, confidence));
	}
	if (c->probability > 0.8) {
		int n = c->mean_length;
		double delayed = mem[md_frames(d, 36)][0];
		c->square = (c->square * n + delayed * delayed) / (n + 1);
		if (n < md_frames(d, 2048))
			c->mean_length++;
	}
	c->seg.step = frame;
	md_segment_step(d, c, c->envelope < 1.3, c->probability,
			old_carrier && !c->carrier);
	if (c->stats.learning && c->quiet == md_frames(d, 75))
		md_stats_commit(d, c, NULL, 0, 1);
	if (c->state == 0) {
		if (c->stats.trained || c->pinned) {
			c->state = 1;
			c->age = 0;
		} else if (c->carrier != c->last_signal) {
			c->last_signal = c->carrier;
			c->age = 0;
		} else if (++c->age > md_frames(d, 500) && !c->pinned) {
			c->state = c->last_signal ? 2 : 3;
			c->age = 0;
		}
	} else if (c->state == 1) {
		c->age = c->changed ? 0 : c->age + 1;
		if (c->age > md_frames(d, 1000) && !c->pinned)
			c->state = 3;
	} else if (c->state == 2) {
		if (c->pinned) {
			c->state = 1;
			c->age = 0;
		} else if (c->carrier) {
			c->age = 0;
		} else if (++c->age > md_frames(d, 200)) {
			c->state = 3;
		}
	}
}
