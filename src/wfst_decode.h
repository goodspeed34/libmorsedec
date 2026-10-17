/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MD_WFST_DECODE_H
#define MD_WFST_DECODE_H

#include <stdint.h>

#define MD_WFST_BEAM 100
#ifndef MD_WFST_BEAM_LOG
#define MD_WFST_BEAM_LOG 8.0
#endif
#ifndef MD_WFST_IDLE_START
#define MD_WFST_IDLE_START .05
#endif
#define MD_WFST_ACQUISITION 4096
#define MD_WFST_HISTORY 4096
#define MD_WFST_BUCKETS 8192
#define MD_WFST_AGES 2048
#define MD_WFST_DEGREE 4
#define MD_WFST_CANDIDATES (MD_WFST_BEAM * MD_WFST_DEGREE)
#define MD_WFST_KEYS 1024

enum md_wfst_input_label {
	MD_WFST_DOT,
	MD_WFST_DASH,
	MD_WFST_GAP,
};
enum md_wfst_node_type {
	MD_WFST_IDLE_NODE,
	MD_WFST_MARK_NODE,
	MD_WFST_GAP_NODE,
};
enum md_wfst_duration_class {
	MD_WFST_DOT_DURATION,
	MD_WFST_DASH_DURATION,
	MD_WFST_INTRA_DURATION,
	MD_WFST_CHARACTER_DURATION,
	MD_WFST_WORD_DURATION,
	MD_WFST_NO_DURATION,
};

/* Labels describe the destination observation; duration classes are in nodes.
 */
struct md_wfst_edge {
	uint16_t target_node;
	uint8_t input_label;
	uint16_t output_label;
	float log_weight;
};
struct md_wfst_node {
	uint16_t first_edge, edge_count, output_label;
	uint8_t node_type, duration_class;
};
struct md_token {
	double accumulated_score;
	uint64_t run_start, mark_tick;
	uint32_t history[2];
	uint32_t frame_age;
	uint16_t current_node_id;
	uint8_t run_low, character_low;
};
/* Pool indices belong to one channel; zero denotes the committed root.
 * Events stay immutable until convergence or collection reclaims their slots.
 * Domain 0 records physical intervals; domain 1 records text events. */
struct md_wfst_event {
	uint64_t start, ordinal;
	double word_probability;
	int32_t duration;
	uint16_t output_label;
	uint8_t low, domain;
};
struct md_wfst_history {
	struct md_wfst_event event;
	uint32_t parent, depth, next;
	uint8_t marked;
};
struct md_wfst_candidate {
	double accumulated_score;
	uint32_t frame_age;
	uint16_t current_node_id, source, edge;
};
struct md_wfst_decoder {
	struct md_token tokens[2][MD_WFST_BEAM];
	struct md_wfst_candidate candidates[MD_WFST_CANDIDATES];
	struct md_wfst_history history[MD_WFST_HISTORY];
	uint32_t buckets[MD_WFST_BUCKETS], trace[MD_WFST_HISTORY];
	uint16_t keys[MD_WFST_KEYS], selected[MD_WFST_BEAM];
	uint16_t candidate_keys[MD_WFST_CANDIDATES];
	uint16_t bucket_keys[MD_WFST_HISTORY];
	unsigned int bucket_count;
	double survival[5], idle_start, idle_stay, idle_threshold;
	uint16_t duration_age[5];
	float continuation[5][MD_WFST_AGES + 1];
	float termination[5][MD_WFST_AGES + 1];
	uint64_t tick, forced_decisions, recoveries, acoustic_commits,
	    text_commits;
	uint32_t free_head, free_count, history_peak, history_high;
	int bank, count, candidate_count, sealed;
	double acquisition_probability[MD_WFST_ACQUISITION];
	uint8_t acquisition_low[MD_WFST_ACQUISITION];
	uint64_t acquisition_start, acquisition_replays, acquisition_drops;
	unsigned int acquisition_head, acquisition_count;
	int acquisition_active, replaying;
};

_Static_assert(MD_WFST_HISTORY > 2 * MD_WFST_BEAM + 1,
	       "History must hold a full frame of survivor events");
_Static_assert(
    MD_WFST_KEYS > MD_WFST_CANDIDATES && !(MD_WFST_KEYS & (MD_WFST_KEYS - 1)),
    "Candidate index requires a spare slot and power-of-two capacity");
_Static_assert(!(MD_WFST_BUCKETS & (MD_WFST_BUCKETS - 1)),
	       "History index requires power-of-two capacity");
_Static_assert(sizeof(struct md_wfst_decoder) <= 512 * 1024,
	       "WFST state exceeds the per-channel RAM budget");
#endif
