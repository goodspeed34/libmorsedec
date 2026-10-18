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
#include "wfst_decode.h"
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
#define MD_ACQUISITION_ELEMENTS 256
#define MD_CHANNEL_MEMORY_LIMIT (512u * 1024u)
#define MD_LATTICE_MAX_NODES 64
#define MD_LATTICE_MAX_EDGES 128
#define MD_LATTICE_MAX_DEPTH 26
#define MD_LATTICE_NONE UINT16_MAX
#define MD_LATTICE_WORD_BEAM 8
#define MD_WORD_SELECTORS 64
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
	int acquisition_marks, acquisition_elements;
	uint64_t committed_end;
};
/* Timestamp bridge used by the unchanged extraction module. */
struct md_segment {
	uint64_t step;
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
	uint16_t selectors[MD_WORD_SELECTORS];
	int selector_count;
};
enum md_lattice_node_flags {
	MD_LATTICE_NODE_FINAL = 1u << 0,
};
struct md_lattice_node {
	/* Acoustic suffix score from this node to a surviving WFST head.
	 * Valid only when MD_LATTICE_NODE_FINAL is set.
	 */
	double final_score;
	uint16_t first_edge; /* MD_LATTICE_NONE when no outgoing edges. */
	uint16_t edge_count;
	uint16_t max_depth; /* Maximum emitted-byte depth from root. */
	uint16_t flags;
};
struct md_lattice_edge {
	/* Incremental log score, excluding word-layer grammar scores. */
	double acoustic_score;
	/* Existing boundary evidence: [0, 1], or 2 for forced word end. */
	double word_space_probability;
	/* Channel ticks: first key-down through last key-down, exclusive.
	 * The following gap is represented by boundary evidence.
	 */
	uint64_t start_frame;
	uint64_t end_frame;
	/* WFST symbol-table index; prosigns may expand to two bytes. */
	uint16_t character_label;
	/* Source node is implicit in its outgoing-edge list. */
	uint16_t to_node;
	uint16_t next_edge; /* MD_LATTICE_NONE terminates the list. */
	uint8_t low_snr;
};
/* Node zero is the committed root. Live nodes are stored in topological
 * order, with edges targeting higher node indices. Depth counts emitted
 * bytes after expanding WFST symbols, including both bytes of a prosign.
 */
struct md_lattice {
	struct md_lattice_node nodes[MD_LATTICE_MAX_NODES];
	struct md_lattice_edge edges[MD_LATTICE_MAX_EDGES];
	uint64_t forced_commits;
	uint16_t node_count;
	uint16_t edge_count;
	uint16_t max_depth;
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
/* Joint lattice/grammar tokens; previous is a flattened node/beam index. */
struct md_lattice_hyp {
	double score;
	uint16_t model_node, previous, edge;
	uint8_t length, kind;
};
struct md_lattice_work {
	struct md_lattice_hyp hypotheses[MD_LATTICE_MAX_NODES]
					[MD_LATTICE_WORD_BEAM];
	struct md_lattice_hyp scratch[MD_LATTICE_WORD_BEAM];
	double closure[MD_WORD_SELECTORS];
	uint16_t history_nodes[MD_WFST_HISTORY];
	uint16_t node_history[MD_LATTICE_MAX_NODES];
	struct md_lattice_node sorted_nodes[MD_LATTICE_MAX_NODES];
	uint16_t node_order[MD_LATTICE_MAX_NODES];
	uint16_t path[MD_LATTICE_MAX_DEPTH];
	uint8_t counts[MD_LATTICE_MAX_NODES];
	int path_count;
};
_Static_assert(MD_LATTICE_MAX_NODES *MD_LATTICE_WORD_BEAM < MD_LATTICE_NONE &&
		   MD_NODES < MD_LATTICE_NONE &&
		   MD_LATTICE_MAX_DEPTH <= UINT8_MAX,
	       "Lattice hypotheses must fit their compact indices");
struct md_message {
	struct morsedec_character chars[MORSEDEC_MESSAGE_CAPACITY];
	size_t count;
};
struct md_channel {
	uint64_t id, next_frame, decode_frame;
	int used, bin, state, age, last_signal, pinned, changed;
	double offset, floor, envelope, strength, probability, square, power;
	int mean_length, carrier, quiet;
	struct md_filter floor_max, floor_min, env_avg, peak_hold;
	struct md_filter max_short, thr_avg, thr_min;
	double memory[MD_TIME_MAX][8];
	struct md_statistics stats;
	struct md_segment seg;
	struct md_wfst_decoder wfst;
	struct md_lattice lattice;
	struct md_lattice_work lattice_work;
	struct md_word word;
	struct md_message message;
};
/* Initialization must also account for separately allocated word states,
 * channel index slots and any future rescoring workspace.
 */
_Static_assert(sizeof(struct md_channel) <= MD_CHANNEL_MEMORY_LIMIT,
	       "Inline channel state exceeds the per-channel RAM budget");
struct md_parallel;

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
	struct md_channel **order, **jobs, *current;
	struct md_parallel *parallel;
	size_t thread_count;
	int parallel_auto;
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
void md_band_select(struct morsedec_decoder *d);
void md_channel_replay(const struct morsedec_decoder *d, struct md_channel *c);
int md_parallel_init(struct morsedec_decoder *d, size_t threads);
void md_parallel_delete(struct morsedec_decoder *d);
void md_parallel_run(struct morsedec_decoder *d, size_t count);
struct md_channel *md_channel_new(struct morsedec_decoder *d, int bin);
void md_channel_init(struct morsedec_decoder *d, struct md_channel *c, int bin);
void md_signal_frame(const struct morsedec_decoder *d, struct md_channel *c,
		     const float *spectrum, uint64_t frame);
void md_segment_init(const struct morsedec_decoder *d, struct md_channel *c);
void md_segment_step(const struct morsedec_decoder *d, struct md_channel *c,
		     int bit, double probability, int drop);
int md_wfst_graph_valid(void);
void md_wfst_prepare_duration(const struct morsedec_decoder *d,
			      struct md_channel *c, int kind, unsigned int age);
void md_wfst_init(const struct morsedec_decoder *d, struct md_channel *c);
/* One delayed keying observation per increasing channel tick; no allocation. */
void md_wfst_advance(const struct morsedec_decoder *d, struct md_channel *c,
		     double p_key, uint64_t current_tick, int low_snr);
void md_wfst_check_convergence(const struct morsedec_decoder *d,
			       struct md_channel *c);
void md_wfst_flush(const struct morsedec_decoder *d, struct md_channel *c);
const char *md_wfst_text(uint16_t label);
void md_stats_observe(const struct morsedec_decoder *d, struct md_channel *c,
		      const struct md_element *element);
void md_stats_release(const struct morsedec_decoder *d, struct md_channel *c);
void md_stats_init(const struct morsedec_decoder *d, struct md_statistics *s);
double md_score(const struct md_statistics *s, int kind, double duration);
void md_classify(const struct md_statistics *s, struct md_element *e,
		 double scale);
/* Compatibility notification from signal.c; flush releases acquisition only. */
void md_stats_commit(const struct morsedec_decoder *d, struct md_channel *c,
		     struct md_element *elements, int count, int flush);
double md_speed(const struct md_statistics *s);
int md_model_init(struct md_model *model);
void md_word_reset(const struct morsedec_decoder *d, struct md_word *w);
void md_word_feed(const struct morsedec_decoder *d, struct md_channel *c,
		  const struct morsedec_character *character);
void md_word_emit(const struct morsedec_decoder *d, struct md_channel *c,
		  int flush);
int md_word_rescore(const struct morsedec_decoder *d, struct md_channel *c,
		    int require_text);
void md_word_lattice_commit(const struct morsedec_decoder *d,
			    struct md_channel *c, int edges);
void md_message_merge(struct md_message *m,
		      const struct morsedec_character *batch, size_t count);
#endif
