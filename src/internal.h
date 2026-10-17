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
#ifndef MORSEDEC_INTERNAL_H
#define MORSEDEC_INTERNAL_H
#include "morsedec.h"
#include <fftw3.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MD_PI 3.14159265358979323846
#define MD_TICK 0.010625
#define MD_TIME_MAX 512
#define MD_NODES 4096
#define MD_EDGES 8192
#define MD_PENDING 256
#define MD_NEG (-1.0e30)

struct md_filter {
	double ring[MD_TIME_MAX * 2];
	double sum, acc;
	int n, pos, primed;
};
struct md_element {
	int duration, kind, low;
	uint64_t ordinal;
	double scores[5];
};
struct md_statistics {
	double mean[5], prior_mean[5], variance[5], covariance[5][5];
	struct md_element window[14];
	int count, learning, dirty, trained;
};
struct md_segment {
	double score[MD_TIME_MAX], cont[MD_TIME_MAX], end[MD_TIME_MAX];
	int on[MD_TIME_MAX], off[MD_TIME_MAX], bits[MD_TIME_MAX];
	int index, on_run, off_run, on_max, off_max, window, keep;
	uint64_t step, committed_end;
};
struct md_node {
	int selector, code, kind, parent, outgoing;
	double weight;
};
struct md_edge {
	int from, to, next;
	double weight;
};
struct md_model {
	struct md_node nodes[MD_NODES];
	struct md_edge edges[MD_EDGES];
	int count, edge_count, kind, failed;
};
struct md_word_state {
	double score;
	int length, kind;
};
struct md_word {
	struct md_word_state *states;
	struct morsedec_character chars[26];
	struct md_word_state slots[27];
	int count, saved;
};
struct md_message {
	struct morsedec_character chars[MORSEDEC_MESSAGE_CAPACITY];
	size_t count;
};
struct md_channel {
	uint64_t id, next_frame;
	int used, bin, state, age, last_signal, pinned, changed;
	double offset, floor, envelope, strength, probability, square, power;
	int mean_length, carrier;
	struct md_filter floor_max, floor_min, env_avg, peak_hold;
	struct md_filter max_short, thr_avg, thr_min;
	double memory[MD_TIME_MAX][8];
	struct md_statistics stats;
	struct md_segment seg;
	struct md_element pending[MD_PENDING];
	int pending_count;
	struct md_element character_elements[64];
	int element_count;
	struct md_word word;
	struct md_message message;
};
struct morsedec_decoder {
	struct morsedec_config config;
	struct morsedec_geometry geometry;
	int first_bin, bins, history_size, warmup, merge_period;
	double tick_scale;
	uint64_t frames, next_id;
	int mode, flushed, iq;
	size_t sample_pos, hop_fill;
	double *audio, *window, *fft_input;
	double *quadrature;
	fftw_complex *fft_iq;
	fftw_complex *fft_output;
	fftw_plan plan;
	float *power, *smooth, *ratio, *history;
	unsigned char *peaks;
	struct md_channel *channels;
	struct md_channel **order, *current;
	size_t count;
	double noise;
	struct md_model *model;
};

static inline double md_clamp(double x, double lo, double hi)
{
	return fmax(lo, fmin(hi, x));
}
static inline int md_frames(const struct morsedec_decoder *d, int nominal)
{
	return (int)fmax(1, floor(nominal / d->tick_scale + 0.5));
}
static inline int md_fail(int error)
{
	errno = error;
	return MORSEDEC_FAILURE;
}

void md_filter_init(struct md_filter *f, int n);
double md_extreme(struct md_filter *f, double x, int maximum);
double md_average(struct md_filter *f, double x);
int md_frontend_init(struct morsedec_decoder *d);
void md_frontend_frame(struct morsedec_decoder *d);
void md_band_frame(struct morsedec_decoder *d, const float *spectrum);
struct md_channel *md_channel_new(struct morsedec_decoder *d, int bin);
void md_channel_init(struct morsedec_decoder *d, struct md_channel *c, int bin);
void md_signal_frame(struct morsedec_decoder *d, struct md_channel *c,
		     const float *spectrum, uint64_t frame);
void md_segment_init(struct morsedec_decoder *d, struct md_channel *c);
void md_segment_commit(struct morsedec_decoder *d, struct md_channel *c,
		       struct md_element *elements, int count, int drop);
void md_segment_step(struct morsedec_decoder *d, struct md_channel *c, int bit,
		     double probability, int drop);
void md_stats_init(struct morsedec_decoder *d, struct md_statistics *s);
double md_score(const struct md_statistics *s, int kind, double duration);
void md_classify(const struct md_statistics *s, struct md_element *e,
		 double scale);
void md_stats_commit(struct morsedec_decoder *d, struct md_channel *c,
		     struct md_element *elements, int count, int flush);
double md_speed(const struct md_statistics *s);
void md_character_feed(struct morsedec_decoder *d, struct md_channel *c,
		       const struct md_element *e, int flush);
const char *md_lookup(const char *pattern);
void md_decode_pattern(const char *pattern, char *out, size_t capacity);
int md_model_init(struct md_model *model);
void md_word_reset(struct morsedec_decoder *d, struct md_word *w);
void md_word_feed(struct morsedec_decoder *d, struct md_channel *c,
		  const struct morsedec_character *character);
void md_word_emit(struct morsedec_decoder *d, struct md_channel *c, int flush);
void md_message_merge(struct md_message *m,
		      const struct morsedec_character *batch, size_t count);
#endif
