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

void md_config_init(struct morsedec_config *config)
{
	if (config)
		*config =
		    (struct morsedec_config){48000, 0, 0, 200, 1200, 32, 0};
}

static int configure(struct morsedec_decoder *d,
		     const struct morsedec_config *c)
{
	double n, hop, first, limit, dt;
	if (!c->sample_rate || !isfinite(c->min_frequency) ||
	    !isfinite(c->max_frequency) ||
	    c->min_frequency < (d->iq ? -(double)c->sample_rate / 2 : 0) ||
	    c->max_frequency <= c->min_frequency ||
	    c->max_frequency > c->sample_rate / 2.0 || !c->max_channels ||
	    c->max_channels > SIZE_MAX / sizeof(struct md_channel) ||
	    !isfinite(c->initial_wpm) || c->initial_wpm < 0 ||
	    (c->initial_wpm > 0 && (c->initial_wpm < 5 || c->initial_wpm > 80)))
		return md_fail(EINVAL);
	n = c->fft_size ? (double)c->fft_size
			: 2 * floor(c->sample_rate * .064 + .5);
	hop = c->hop_size ? (double)c->hop_size
			  : floor(c->sample_rate * MD_TICK + .5);
	if (n < 64 || n > INT_MAX || fmod(n, 2) != 0 || hop < 1 || hop > n)
		return md_fail(EINVAL);
	dt = hop / c->sample_rate;
	if (dt < .005 || dt > .020)
		return md_fail(EINVAL);
	first = floor(c->min_frequency * n / c->sample_rate + .5);
	limit = floor(c->max_frequency * n / c->sample_rate + .5);
	/* Include Nyquist when the requested exclusive bound rounds above it.
	 */
	limit = fmin(limit, d->iq ? n / 2 : n / 2 + 1);
	if (limit - first < 32 || first < (d->iq ? -n / 2 : 0) ||
	    limit > (d->iq ? n / 2 : n / 2 + 1))
		return md_fail(EINVAL);
	d->config = *c;
	d->geometry = (struct morsedec_geometry){c->sample_rate,
						 (size_t)n,
						 (size_t)hop,
						 (size_t)(limit - first),
						 first * c->sample_rate / n,
						 c->sample_rate / n,
						 dt};
	d->first_bin = (int)first;
	d->bins = (int)(limit - first);
	d->tick_scale = dt / MD_TICK;
	d->history_size = md_frames(d, 200);
	d->warmup = d->history_size;
	d->merge_period = md_frames(d, 32);
	if ((size_t)d->bins >
	    SIZE_MAX / (size_t)d->history_size / sizeof(float))
		return md_fail(EINVAL);
	return MORSEDEC_SUCCESS;
}

static morsedec_decoder_t *new_decoder(const struct morsedec_config *config,
				       int iq, size_t threads)
{
	struct morsedec_config defaults;
	struct morsedec_decoder *d = calloc(1, sizeof(*d));
	if (!d) {
		errno = ENOMEM;
		return NULL;
	}
	md_config_init(&defaults);
	if (iq)
		defaults.min_frequency = -1200;
	d->iq = iq;
	if (!configure(d, config ? config : &defaults))
		goto fail;
	d->model = calloc(1, sizeof(*d->model));
	if (!d->model || !md_model_init(d->model)) {
		errno = ENOMEM;
		goto fail;
	}
	d->power = calloc((size_t)d->bins, sizeof(float));
	d->smooth = calloc((size_t)d->bins, sizeof(float));
	d->ratio = calloc((size_t)d->bins, sizeof(float));
	d->peaks = calloc((size_t)d->bins, 1);
	d->history =
	    calloc((size_t)d->bins * (size_t)d->history_size, sizeof(float));
	d->channels = calloc(d->config.max_channels, sizeof(*d->channels));
	d->order = calloc(d->config.max_channels, sizeof(*d->order));
	d->jobs = calloc(d->config.max_channels, sizeof(*d->jobs));
	if (!d->power || !d->smooth || !d->ratio || !d->peaks || !d->history ||
	    !d->channels || !d->order || !d->jobs) {
		errno = ENOMEM;
		goto fail;
	}
	for (size_t i = 0; i < d->config.max_channels; i++) {
		d->channels[i].word.states = calloc(
		    (size_t)d->model->count, sizeof(struct md_word_state));
		if (!d->channels[i].word.states) {
			errno = ENOMEM;
			goto fail;
		}
	}
	if (!md_frontend_init(d) || !md_parallel_init(d, threads))
		goto fail;
	return d;
fail: {
	int error = errno;
	md_delete(d);
	errno = error;
	return NULL;
}
}

morsedec_decoder_t *md_new(const struct morsedec_config *config)
{
	return md_new_with_threads(config, 0);
}

morsedec_decoder_t *md_new_iq(const struct morsedec_config *config)
{
	return md_new_iq_with_threads(config, 0);
}

morsedec_decoder_t *md_new_with_threads(const struct morsedec_config *config,
					size_t threads)
{
	return new_decoder(config, 0, threads);
}

morsedec_decoder_t *md_new_iq_with_threads(const struct morsedec_config *config,
					   size_t threads)
{
	return new_decoder(config, 1, threads);
}

size_t md_get_thread_count(const morsedec_decoder_t *d)
{
	return d ? d->thread_count : 0;
}

void md_delete(morsedec_decoder_t *d)
{
	if (!d)
		return;
	md_parallel_delete(d);
	if (d->plan)
		fftw_destroy_plan(d->plan);
	fftw_free(d->fft_input);
	fftw_free(d->fft_iq);
	fftw_free(d->fft_output);
	free(d->audio);
	free(d->quadrature);
	free(d->window);
	free(d->power);
	free(d->smooth);
	free(d->ratio);
	free(d->peaks);
	free(d->history);
	if (d->channels)
		for (size_t i = 0; i < d->config.max_channels; i++)
			free(d->channels[i].word.states);
	free(d->channels);
	free(d->order);
	free(d->jobs);
	free(d->model);
	free(d);
}

int md_reset(morsedec_decoder_t *d)
{
	if (!d)
		return md_fail(EINVAL);
	memset(d->audio, 0, d->geometry.fft_size * sizeof(double));
	if (d->iq)
		memset(d->quadrature, 0,
		       d->geometry.fft_size * sizeof(*d->quadrature));
	memset(d->power, 0, (size_t)d->bins * sizeof(float));
	memset(d->smooth, 0, (size_t)d->bins * sizeof(float));
	memset(d->ratio, 0, (size_t)d->bins * sizeof(float));
	memset(d->history, 0,
	       (size_t)d->history_size * (size_t)d->bins * sizeof(float));
	for (size_t i = 0; i < d->config.max_channels; i++)
		d->channels[i].used = 0;
	d->frames = 0;
	d->count = 0;
	d->current = NULL;
	d->sample_pos = d->hop_fill = 0;
	d->mode = d->flushed = 0;
	d->noise = 0;
	return MORSEDEC_SUCCESS;
}

int md_get_geometry(const morsedec_decoder_t *d, struct morsedec_geometry *g)
{
	if (!d || !g)
		return md_fail(EINVAL);
	*g = d->geometry;
	return MORSEDEC_SUCCESS;
}

static void sample(struct morsedec_decoder *d, double x, double q)
{
	d->audio[d->sample_pos] = isfinite(x) ? md_clamp(x, -1, 1) : 0;
	if (d->iq)
		d->quadrature[d->sample_pos] =
		    isfinite(q) ? md_clamp(q, -1, 1) : 0;
	d->sample_pos = (d->sample_pos + 1) % d->geometry.fft_size;
	if (++d->hop_fill == d->geometry.hop_size) {
		d->hop_fill = 0;
		md_frontend_frame(d);
	}
}

int md_process(morsedec_decoder_t *d, const float *samples, size_t count)
{
	if (!d || d->iq || (!samples && count) || d->mode == 2 || d->flushed)
		return md_fail(EINVAL);
	d->mode = 1;
	for (size_t i = 0; i < count; i++)
		sample(d, samples[i], 0);
	return MORSEDEC_SUCCESS;
}

int md_process_s16(morsedec_decoder_t *d, const int16_t *samples, size_t count)
{
	if (!d || d->iq || (!samples && count) || d->mode == 2 || d->flushed)
		return md_fail(EINVAL);
	d->mode = 1;
	for (size_t i = 0; i < count; i++)
		sample(d, samples[i] / 32768.0, 0);
	return MORSEDEC_SUCCESS;
}

int md_process_iq(morsedec_decoder_t *d, const float *interleaved, size_t count)
{
	if (!d || !d->iq || (!interleaved && count) || count > SIZE_MAX / 2 ||
	    d->mode == 2 || d->flushed)
		return md_fail(EINVAL);
	d->mode = 1;
	for (size_t i = 0; i < count; i++)
		sample(d, interleaved[2 * i], interleaved[2 * i + 1]);
	return MORSEDEC_SUCCESS;
}

int md_process_iq_s16(morsedec_decoder_t *d, const int16_t *interleaved,
		      size_t count)
{
	if (!d || !d->iq || (!interleaved && count) || count > SIZE_MAX / 2 ||
	    d->mode == 2 || d->flushed)
		return md_fail(EINVAL);
	d->mode = 1;
	for (size_t i = 0; i < count; i++)
		sample(d, interleaved[2 * i] / 32768.0,
		       interleaved[2 * i + 1] / 32768.0);
	return MORSEDEC_SUCCESS;
}

int md_process_spectrum(morsedec_decoder_t *d, const float *power, size_t count)
{
	if (!d || !power || count != (size_t)d->bins || d->mode == 1 ||
	    d->flushed)
		return md_fail(EINVAL);
	d->mode = 2;
	for (int i = 0; i < d->bins; i++)
		d->power[i] = isfinite(power[i]) && power[i] > 0 ? power[i] : 0;
	md_band_frame(d, d->power);
	return MORSEDEC_SUCCESS;
}

int md_flush(morsedec_decoder_t *d)
{
	if (!d)
		return md_fail(EINVAL);
	if (d->flushed)
		return MORSEDEC_SUCCESS;
	/* A second of silence covers FFT, ENV delay and SEG traceback. */
	if (d->mode != 2) {
		while (d->hop_fill)
			sample(d, 0, 0);
		for (uint32_t i = 0; i < d->geometry.sample_rate; i++)
			sample(d, 0, 0);
	} else {
		memset(d->power, 0, (size_t)d->bins * sizeof(float));
		for (int i = 0; i < md_frames(d, 100); i++)
			md_band_frame(d, d->power);
	}
	for (size_t i = 0; i < d->count; i++) {
		struct md_channel *c = d->order[i];
		c->quiet = md_frames(d, 100);
		md_stats_commit(d, c, NULL, 0, 1);
		if (c->state == 0 && c->stats.trained) {
			c->state = 1;
			c->age = 0;
		}
	}
	/* A short transmission may first become eligible during the explicit
	 * flush, after the last band frame has already selected its channel. */
	md_band_select(d);
	d->flushed = 1;
	return MORSEDEC_SUCCESS;
}

static enum morsedec_state state(const struct morsedec_decoder *d,
				 const struct md_channel *c)
{
	int decoding = c->state == 1 && c->age < md_frames(d, 140);
	return (enum morsedec_state)((c->pinned ? 3 : 1) + decoding);
}

enum morsedec_state md_get_state(const morsedec_decoder_t *d)
{
	return d && d->current ? state(d, d->current) : MORSEDEC_STATE_NONE;
}

size_t md_get_channel_count(const morsedec_decoder_t *d)
{
	return d ? d->count : 0;
}

int md_get_channel(const morsedec_decoder_t *d, size_t index,
		   struct morsedec_channel *channel)
{
	const struct md_channel *c;
	if (!d || !channel || index >= d->count)
		return md_fail(EINVAL);
	c = d->order[index];
	*channel = (struct morsedec_channel){
	    c->id,
	    state(d, c),
	    d->geometry.first_frequency + c->bin * d->geometry.bin_spacing,
	    10 * log10(fmax(1, c->square)) - 10,
	    md_speed(&c->stats),
	    c->probability,
	    c->strength,
	    c->message.count};
	return MORSEDEC_SUCCESS;
}

int md_set_frequency(morsedec_decoder_t *d, double hz)
{
	return hz == 0 ? md_unpin_frequency(d) : md_pin_frequency(d, hz);
}

int md_unpin_frequency(morsedec_decoder_t *d)
{
	if (!d || d->flushed)
		return md_fail(EINVAL);
	if (d->current)
		d->current->pinned = 0;
	return MORSEDEC_SUCCESS;
}

int md_pin_frequency(morsedec_decoder_t *d, double hz)
{
	int bin;
	struct md_channel *selected = NULL;
	if (!d || !isfinite(hz) || (!d->iq && hz < 0) || d->flushed)
		return md_fail(EINVAL);
	bin = (int)md_clamp(nearbyint((hz - d->geometry.first_frequency) /
				      d->geometry.bin_spacing),
			    10, d->bins - 11);
	for (size_t i = 0; i < d->count; i++) {
		struct md_channel *c = d->order[i];
		if (abs(c->bin - bin) <= 2 &&
		    (!selected || abs(c->bin - bin) < abs(selected->bin - bin)))
			selected = c;
	}
	if (!selected)
		selected = md_channel_new(d, bin);
	if (!selected)
		return md_fail(ENOSPC);
	if (d->current)
		d->current->pinned = 0;
	selected->pinned = 1;
	d->current = selected;
	return MORSEDEC_SUCCESS;
}

double md_get_frequency(const morsedec_decoder_t *d)
{
	return d && d->current ? d->geometry.first_frequency +
				     d->current->bin * d->geometry.bin_spacing
			       : -1;
}

double md_get_snr(const morsedec_decoder_t *d)
{
	return d && d->current ? 10 * log10(fmax(1, d->current->square)) - 10
			       : -1;
}

double md_get_wpm(const morsedec_decoder_t *d)
{
	return d && d->current ? md_speed(&d->current->stats) : -1;
}

static const struct md_channel *find_channel(const morsedec_decoder_t *d,
					     uint64_t id)
{
	if (!id)
		return d->current;
	for (size_t i = 0; i < d->count; i++)
		if (d->order[i]->id == id)
			return d->order[i];
	return NULL;
}

int md_get_characters(const morsedec_decoder_t *d, uint64_t id,
		      const struct morsedec_character **characters,
		      size_t *count)
{
	const struct md_channel *c;
	if (!d || !characters || !count)
		return md_fail(EINVAL);
	c = find_channel(d, id);
	if (!c && id)
		return md_fail(ENOENT);
	*characters = c ? c->message.chars : NULL;
	*count = c ? c->message.count : 0;
	return MORSEDEC_SUCCESS;
}

int md_get_text(const morsedec_decoder_t *d, uint64_t id, char *buffer,
		size_t capacity, size_t *required)
{
	const struct morsedec_character *chars;
	size_t count, need = 1, pos = 0;
	if (!required || (!buffer && capacity))
		return md_fail(EINVAL);
	if (!md_get_characters(d, id, &chars, &count))
		return MORSEDEC_FAILURE;
	for (size_t i = 0; i < count; i++)
		need += 1 + (size_t)chars[i].word_end;
	*required = need;
	if (!buffer && !capacity)
		return MORSEDEC_SUCCESS;
	if (capacity < need)
		return md_fail(ENOSPC);
	for (size_t i = 0; i < count; i++) {
		buffer[pos++] = (char)chars[i].character;
		for (int j = 0; j < chars[i].word_end; j++)
			buffer[pos++] = ' ';
	}
	buffer[pos] = 0;
	return MORSEDEC_SUCCESS;
}

uint64_t md_get_frame_count(const morsedec_decoder_t *d)
{
	return d ? d->frames : 0;
}
