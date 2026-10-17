/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "internal.h"
#include <assert.h>
#include "wfst_graph.h"

_Static_assert(MD_WFST_GRAPH_DEGREE <= MD_WFST_DEGREE,
	       "WFST candidate capacity is too small");

int md_wfst_graph_valid(void)
{
	unsigned char reachable[MD_WFST_GRAPH_NODES] = {1};
	for (int pass = 0; pass < MD_WFST_GRAPH_NODES; pass++) {
		for (int i = 0; i < MD_WFST_GRAPH_NODES; i++) {
			const struct md_wfst_node *n = &md_wfst_nodes[i];
			if (!n->edge_count || n->edge_count > MD_WFST_DEGREE ||
			    n->first_edge + n->edge_count >
				MD_WFST_GRAPH_EDGES ||
			    n->output_label >= MD_WFST_GRAPH_SYMBOLS ||
			    n->duration_class > MD_WFST_NO_DURATION)
				return 0;
			for (int j = 0; j < n->edge_count; j++) {
				const struct md_wfst_edge *e =
				    &md_wfst_edges[n->first_edge + j];
				if (e->target_node >= MD_WFST_GRAPH_NODES ||
				    e->output_label >= MD_WFST_GRAPH_SYMBOLS ||
				    !isfinite(e->log_weight))
					return 0;
				int k = md_wfst_nodes[e->target_node]
					    .duration_class;
				if (e->input_label != (k < 2 ? k : 2))
					return 0;
				if (reachable[i])
					reachable[e->target_node] = 1;
			}
		}
	}
	for (int i = 0; i < MD_WFST_GRAPH_NODES; i++)
		if (!reachable[i])
			return 0;
	return md_wfst_symbols[0][0] == 0;
}

/* Log of the normal upper tail, including arguments for which erfc underflows.
 */
static double md_wfst_log_survival(double duration, double mean,
				   double variance)
{
	/* A minimum log-duration SD of 0.2 prevents a few precise runs from
	 * making the predictive model overconfident about the next keyed run.
	 */
	double z = (log(duration) - mean) / sqrt(fmax(variance, .04));
	if (z < 8)
		return log(0.5 * erfc(z / sqrt(2.0)));
	double inv = 1 / (z * z);
	double series = 1 - inv + 3 * inv * inv - 15 * inv * inv * inv +
			105 * inv * inv * inv * inv;
	return -0.5 * z * z - log(z) - 0.5 * log(2 * MD_PI) + log(series);
}

static void md_wfst_durations(const struct morsedec_decoder *d,
			      struct md_channel *c)
{
	struct md_wfst_decoder *w = &c->wfst;
	memset(w->duration_age, 0, sizeof(w->duration_age));
	for (int k = 0; k < 5; k++)
		w->survival[k] = md_wfst_log_survival(
		    .5 * d->tick_scale, c->stats.mean[k], c->stats.variance[k]);
	c->stats.dirty = 0;
}

/* The model is invalidated once per frame; materialize only ages actually used.
 */
void md_wfst_prepare_duration(const struct morsedec_decoder *d,
			      struct md_channel *c, int kind, unsigned int age)
{
	struct md_wfst_decoder *w = &c->wfst;
	assert(kind >= 0 && kind < 5 && age <= MD_WFST_AGES);
	while (w->duration_age[kind] < age) {
		unsigned int a = ++w->duration_age[kind];
		double next = md_wfst_log_survival((a + .5) * d->tick_scale,
						   c->stats.mean[kind],
						   c->stats.variance[kind]);
		double cont =
		    md_clamp(next - w->survival[kind], log(1e-6), log1p(-1e-6));
		w->continuation[kind][a] = (float)cont;
		w->termination[kind][a] = (float)log(-expm1(cont));
		w->survival[kind] = next;
	}
}

static void md_wfst_reset_search(struct md_wfst_decoder *w)
{
	memset(w->tokens, 0, sizeof(w->tokens));
	memset(w->history, 0, sizeof(w->history));
	memset(w->buckets, 0, sizeof(w->buckets));
	w->bank = w->candidate_count = w->sealed = 0;
	w->bucket_count = 0;
	memset(w->keys, 0, sizeof(w->keys));
	w->tick = w->history_high = 0;
	w->count = 1;
	w->free_head = 1;
	w->free_count = MD_WFST_HISTORY - 1;
	for (uint32_t i = 1; i < MD_WFST_HISTORY - 1; i++)
		w->history[i].next = i + 1;
}

void md_wfst_init(const struct morsedec_decoder *d, struct md_channel *c)
{
	struct md_wfst_decoder *w = &c->wfst;
	memset(w, 0, sizeof(*w));
	md_wfst_reset_search(w);
	w->acquisition_active = c->stats.learning;
	w->idle_stay = d->tick_scale * log1p(-MD_WFST_IDLE_START);
	w->idle_start = log(-expm1(w->idle_stay));
	double onset = MD_NEG;
	for (int i = 0; i < md_wfst_nodes[0].edge_count; i++) {
		const struct md_wfst_edge *e = &md_wfst_edges[i];
		if (e->target_node && e->log_weight > onset)
			onset = e->log_weight;
	}
	double odds =
	    exp(-MD_WFST_BEAM_LOG + w->idle_stay - w->idle_start - onset);
	w->idle_threshold = odds / (1 + odds);
	md_wfst_durations(d, c);
}

static uint32_t md_wfst_hash(uint32_t parent, const struct md_wfst_event *e)
{
	uint64_t bits;
	memcpy(&bits, &e->word_probability, sizeof(bits));
	uint64_t x = e->start ^ (e->ordinal * UINT64_C(0x9e3779b97f4a7c15)) ^
		     bits ^ ((uint64_t)(uint32_t)e->duration << 19) ^
		     ((uint64_t)parent << 7) ^
		     ((uint64_t)e->output_label << 1) ^
		     ((uint64_t)e->low << 43) ^ e->domain;
	x ^= x >> 33;
	x *= UINT64_C(0xff51afd7ed558ccd);
	x ^= x >> 33;
	return (uint32_t)x & (MD_WFST_BUCKETS - 1);
}

static int md_wfst_event_equal(const struct md_wfst_event *a,
			       const struct md_wfst_event *b)
{
	return a->start == b->start && a->ordinal == b->ordinal &&
	       a->word_probability == b->word_probability &&
	       a->duration == b->duration &&
	       a->output_label == b->output_label && a->low == b->low &&
	       a->domain == b->domain;
}

static uint32_t md_wfst_intern(struct md_wfst_decoder *w, uint32_t parent,
			       const struct md_wfst_event *event)
{
	uint32_t bucket = md_wfst_hash(parent, event);
	for (uint32_t i = w->buckets[bucket]; i; i = w->history[i].next)
		if (w->history[i].parent == parent &&
		    md_wfst_event_equal(&w->history[i].event, event))
			return i;
	/* advance reserves two entries per survivor before any expansion. */
	uint32_t id = w->free_head;
	assert(id && w->free_count);
	struct md_wfst_history *h = &w->history[id];
	w->free_head = h->next;
	w->free_count--;
	if (MD_WFST_HISTORY - 1 - w->free_count > w->history_peak)
		w->history_peak = MD_WFST_HISTORY - 1 - w->free_count;
	if (id > w->history_high)
		w->history_high = id;
	h->event = *event;
	h->parent = parent;
	h->depth = w->history[parent].depth + 1;
	h->next = w->buckets[bucket];
	if (!w->buckets[bucket])
		w->bucket_keys[w->bucket_count++] = (uint16_t)bucket;
	w->buckets[bucket] = id;
	return id;
}

/* Rebuild indices after pruning or rerooting; never overwrite a live ancestor.
 */
static void md_wfst_collect(struct md_wfst_decoder *w, const uint32_t depth[2])
{
	for (int i = 0; i < w->count; i++) {
		for (int domain = 0; domain < 2; domain++) {
			uint32_t p = w->tokens[w->bank][i].history[domain];
			while (p && !w->history[p].marked) {
				w->history[p].marked = 1;
				p = w->history[p].parent;
			}
		}
	}
	while (w->bucket_count)
		w->buckets[w->bucket_keys[--w->bucket_count]] = 0;
	uint32_t high = 0;
	for (uint32_t i = w->history_high; i; i--) {
		struct md_wfst_history *h = &w->history[i];
		if (!h->depth)
			continue;
		if (!h->marked) {
			h->depth = 0;
			h->next = w->free_head;
			w->free_head = i;
			w->free_count++;
		} else {
			if (i > high)
				high = i;
			h->marked = 0;
			h->depth -= depth[h->event.domain];
			uint32_t bucket = md_wfst_hash(h->parent, &h->event);
			h->next = w->buckets[bucket];
			if (!w->buckets[bucket])
				w->bucket_keys[w->bucket_count++] =
				    (uint16_t)bucket;
			w->buckets[bucket] = i;
		}
	}
	w->history_high = high;
}

static uint32_t md_wfst_ancestor(const struct md_wfst_decoder *w, uint32_t a,
				 uint32_t b)
{
	while (w->history[a].depth > w->history[b].depth)
		a = w->history[a].parent;
	while (w->history[b].depth > w->history[a].depth)
		b = w->history[b].parent;
	while (a != b) {
		a = w->history[a].parent;
		b = w->history[b].parent;
	}
	return a;
}

static double md_wfst_word_probability(const struct morsedec_decoder *d,
				       const struct md_channel *c,
				       uint64_t frames);

static void md_wfst_output(const struct morsedec_decoder *d,
			   struct md_channel *c, const struct md_wfst_event *e)
{
	const char *text = md_wfst_symbols[e->output_label];
	double boundary =
	    e->word_probability < 0
		? md_wfst_word_probability(d, c, (uint64_t)e->duration)
		: e->word_probability;
	for (size_t i = 0; text[i]; i++) {
		struct morsedec_character ch = {0};
		ch.character = (unsigned char)text[i];
		ch.ordinal = e->ordinal * 256 + i;
		ch.low_snr = e->low;
		ch.word_space_probability = text[i + 1] ? .001 : boundary;
		md_word_feed(d, c, &ch);
	}
	md_word_emit(d, c, 0);
}

void md_wfst_check_convergence(const struct morsedec_decoder *d,
			       struct md_channel *c)
{
	struct md_wfst_decoder *w = &c->wfst;
	uint32_t root[2], depth[2];
	if (!w->count)
		return;
	for (int domain = 0; domain < 2; domain++) {
		root[domain] = w->tokens[w->bank][0].history[domain];
		for (int i = 1; i < w->count && root[domain]; i++)
			root[domain] = md_wfst_ancestor(
			    w, root[domain],
			    w->tokens[w->bank][i].history[domain]);
		depth[domain] = w->history[root[domain]].depth;
		int count = 0;
		for (uint32_t p = root[domain]; p; p = w->history[p].parent)
			w->trace[count++] = p;
		while (count) {
			const struct md_wfst_event *e =
			    &w->history[w->trace[--count]].event;
			if (!domain) {
				if (w->replaying) {
					if (e->ordinal > c->stats.committed_end)
						c->stats.committed_end =
						    e->ordinal;
					continue;
				}
				struct md_element element = {0};
				element.duration = e->duration;
				element.ordinal = e->ordinal;
				element.low = e->low;
				md_stats_observe(d, c, &element);
				w->acoustic_commits++;
			} else {
				md_wfst_output(d, c, e);
				w->text_commits++;
			}
		}
	}
	if (!root[0] && !root[1])
		return;
	for (int i = 0; i < w->count; i++)
		for (int domain = 0; domain < 2; domain++)
			if (w->tokens[w->bank][i].history[domain] ==
			    root[domain])
				w->tokens[w->bank][i].history[domain] = 0;
	for (uint32_t i = 1; i <= w->history_high; i++) {
		struct md_wfst_history *h = &w->history[i];
		if (h->depth && root[h->event.domain] &&
		    h->parent == root[h->event.domain])
			h->parent = 0;
	}
	md_wfst_collect(w, depth);
}

static void md_wfst_reserve(const struct morsedec_decoder *d,
			    struct md_channel *c)
{
	struct md_wfst_decoder *w = &c->wfst;
	const uint32_t depth[2] = {0};
	if (w->free_count >= 2 * MD_WFST_BEAM)
		return;
	md_wfst_collect(w, depth);
	if (w->free_count < 2 * MD_WFST_BEAM) {
		/* Banks are score ordered; preserve the best unfinished state.
		 */
		w->count = 1;
		w->forced_decisions++;
		md_wfst_check_convergence(d, c);
	}
	assert(w->free_count >= 2 * MD_WFST_BEAM);
}

static double md_wfst_word_probability(const struct morsedec_decoder *d,
				       const struct md_channel *c,
				       uint64_t frames)
{
	double duration = (double)frames * d->tick_scale;
	if (duration > 150)
		return 2;
	double a = md_score(&c->stats, 3, duration);
	double b = md_score(&c->stats, 4, duration);
	return b / (a + b + 1e-12);
}

static unsigned int md_wfst_events(const struct md_channel *c,
				   const struct md_token *source,
				   const struct md_wfst_edge *edge,
				   uint64_t tick, int low, struct md_token *t,
				   struct md_wfst_event event[2])
{
	unsigned int present = 0;
	int was_on = md_wfst_nodes[source->current_node_id].duration_class < 2;
	int on = edge->input_label < 2;
	if (edge->output_label) {
		if (!c->wfst.acquisition_active) {
			present |= 2;
			event[1] = (struct md_wfst_event){
			    .ordinal = source->mark_tick,
			    .duration =
				(int)fmin((double)(tick - source->run_start),
					  INT_MAX / 4),
			    .word_probability = -1,
			    .output_label = edge->output_label,
			    .low = source->character_low,
			    .domain = 1};
		}
		t->character_low = 0;
		t->mark_tick = 0;
	}
	if (was_on != on) {
		if (source->run_start) {
			uint64_t length = tick - source->run_start;
			int duration = (int)fmin((double)length, INT_MAX / 4);
			present |= 1;
			event[0] = (struct md_wfst_event){
			    .start = source->run_start,
			    .ordinal = tick - 1,
			    .duration = was_on ? duration : -duration,
			    .low = source->run_low};
		}
		t->run_start = tick;
		t->run_low = (uint8_t)low;
	} else {
		t->run_low |= (uint8_t)low;
	}
	if (on)
		t->mark_tick = tick;
	if (edge->target_node)
		t->character_low |= (uint8_t)low;
	return present;
}

static void md_wfst_candidate_add(struct md_wfst_decoder *w,
				  const struct md_wfst_candidate *candidate)
{
	const struct md_wfst_candidate *t = candidate;
	uint32_t key =
	    (t->current_node_id * 2053u + t->frame_age) & (MD_WFST_KEYS - 1);
	while (w->keys[key]) {
		struct md_wfst_candidate *old =
		    &w->candidates[w->keys[key] - 1];
		if (old->current_node_id == t->current_node_id &&
		    old->frame_age == t->frame_age) {
			if (t->accumulated_score > old->accumulated_score)
				*old = *candidate;
			return;
		}
		key = (key + 1) & (MD_WFST_KEYS - 1);
	}
	assert(w->candidate_count < MD_WFST_CANDIDATES);
	w->candidate_keys[w->candidate_count] = (uint16_t)key;
	w->keys[key] = (uint16_t)(w->candidate_count + 1);
	w->candidates[w->candidate_count++] = *candidate;
}

static void md_wfst_step(const struct morsedec_decoder *d, struct md_channel *c,
			 double p_key, uint64_t current_tick, int low_snr)
{
	struct md_wfst_decoder *w = &c->wfst;
	if (w->sealed || current_tick <= w->tick)
		return;
	md_wfst_reserve(d, c);
	if (c->stats.dirty)
		md_wfst_durations(d, c);
	w->tick = current_tick;
	p_key = isfinite(p_key) ? md_clamp(p_key, 1e-10, 1 - 1e-10) : .5;
	/* Exact idle shortcut: both onset edges would be outside the beam.
	 * The previous convergence pass has already committed both histories.
	 */
	struct md_token *idle = &w->tokens[w->bank][0];
	if (w->count == 1 && !idle->current_node_id && !idle->history[0] &&
	    !idle->history[1] && p_key < w->idle_threshold) {
		idle->run_low |= (uint8_t)!!low_snr;
		if (idle->run_start && current_tick - idle->run_start >=
					   (uint64_t)md_frames(d, 150))
			md_word_emit(d, c, 1);
		return;
	}
	/* The common gap log likelihood cancels during normalization. */
	double observation[2] = {log(p_key / (1 - p_key)), 0};
	for (int i = 0; i < w->candidate_count; i++)
		w->keys[w->candidate_keys[i]] = 0;
	w->candidate_count = 0;
	double best = MD_NEG;
	for (int i = 0; i < w->count; i++) {
		const struct md_token *t = &w->tokens[w->bank][i];
		const struct md_wfst_node *n =
		    &md_wfst_nodes[t->current_node_id];
		if (n->duration_class != MD_WFST_NO_DURATION &&
		    w->duration_age[n->duration_class] < t->frame_age)
			md_wfst_prepare_duration(d, c, n->duration_class,
						 t->frame_age);
		for (int j = 0; j < n->edge_count; j++) {
			const struct md_wfst_edge *e =
			    &md_wfst_edges[n->first_edge + j];
			int stay = e->target_node == t->current_node_id;
			double duration =
			    n->duration_class == MD_WFST_NO_DURATION
				? (stay ? w->idle_stay : w->idle_start)
			    : stay ? w->continuation[n->duration_class]
						    [t->frame_age]
				   : w->termination[n->duration_class]
						   [t->frame_age];
			double score =
			    t->accumulated_score + duration + e->log_weight +
			    observation[e->input_label == MD_WFST_GAP];
			if (!isfinite(score) || score < best - MD_WFST_BEAM_LOG)
				continue;
			if (score > best)
				best = score;
			struct md_wfst_candidate candidate;
			candidate.source = (uint16_t)i;
			candidate.edge = n->first_edge + j;
			candidate.current_node_id = e->target_node;
			candidate.frame_age =
			    !e->target_node ? 0
			    : stay
				? (t->frame_age + (t->frame_age < MD_WFST_AGES))
				: 1;
			candidate.accumulated_score = score;
			md_wfst_candidate_add(w, &candidate);
		}
	}
	int count = 0;
	for (int i = 0; i < w->candidate_count; i++) {
		double score = w->candidates[i].accumulated_score;
		if (score < best - MD_WFST_BEAM_LOG)
			continue;
		int pos = 0;
		while (pos < count &&
		       score <=
			   w->candidates[w->selected[pos]].accumulated_score)
			pos++;
		if (pos == MD_WFST_BEAM)
			continue;
		for (int j = count < MD_WFST_BEAM ? count : MD_WFST_BEAM - 1;
		     j > pos; j--)
			w->selected[j] = w->selected[j - 1];
		w->selected[pos] = (uint16_t)i;
		if (count < MD_WFST_BEAM)
			count++;
	}
	for (int i = 0; i < count; i++) {
		struct md_wfst_candidate *candidate =
		    &w->candidates[w->selected[i]];
		struct md_token *t = &w->tokens[!w->bank][i];
		*t = w->tokens[w->bank][candidate->source];
		t->current_node_id = candidate->current_node_id;
		t->frame_age = candidate->frame_age;
		t->accumulated_score = candidate->accumulated_score - best;
		struct md_wfst_event event[2] = {0};
		unsigned int present =
		    md_wfst_events(c, &w->tokens[w->bank][candidate->source],
				   &md_wfst_edges[candidate->edge],
				   current_tick, !!low_snr, t, event);
		for (int domain = 0; domain < 2; domain++)
			if (present & (1u << domain))
				t->history[domain] = md_wfst_intern(
				    w, t->history[domain], &event[domain]);
	}
	w->bank = !w->bank;
	w->count = count;
	if (!count) {
		w->recoveries++;
		w->count = 1;
		w->tokens[w->bank][0] = (struct md_token){0};
	}
	md_wfst_check_convergence(d, c);
	if (w->count == 1 && !w->tokens[w->bank][0].current_node_id &&
	    w->tokens[w->bank][0].run_start &&
	    current_tick - w->tokens[w->bank][0].run_start >=
		(uint64_t)md_frames(d, 150))
		md_word_emit(d, c, 1);
}

/* Replay only bounded soft observations, never DSP or STA updates. Initial
 * priors must not irreversibly label the first characters before calibration.
 */
static void md_wfst_replay(const struct morsedec_decoder *d,
			   struct md_channel *c)
{
	struct md_wfst_decoder *w = &c->wfst;
	w->acquisition_active = 0;
	w->replaying = 1;
	w->acquisition_replays++;
	md_wfst_reset_search(w);
	for (unsigned int i = 0; i < w->acquisition_count; i++) {
		unsigned int index =
		    (w->acquisition_head + i) % MD_WFST_ACQUISITION;
		md_wfst_step(d, c, w->acquisition_probability[index],
			     w->acquisition_start + i,
			     w->acquisition_low[index]);
	}
	w->replaying = 0;
	w->acquisition_count = w->acquisition_head = 0;
}

void md_wfst_advance(const struct morsedec_decoder *d, struct md_channel *c,
		     double p_key, uint64_t current_tick, int low_snr)
{
	struct md_wfst_decoder *w = &c->wfst;
	if (w->sealed || current_tick <= w->tick)
		return;
	if (w->acquisition_active && !c->stats.learning)
		md_wfst_replay(d, c);
	if (w->acquisition_active) {
		if (w->acquisition_count &&
		    current_tick != w->acquisition_start + w->acquisition_count)
			w->acquisition_count = w->acquisition_head = 0;
		if (!w->acquisition_count)
			w->acquisition_start = current_tick;
		if (w->acquisition_count == MD_WFST_ACQUISITION) {
			w->acquisition_head =
			    (w->acquisition_head + 1) % MD_WFST_ACQUISITION;
			w->acquisition_start++;
			w->acquisition_drops++;
			w->acquisition_count--;
		}
		unsigned int index =
		    (w->acquisition_head + w->acquisition_count++) %
		    MD_WFST_ACQUISITION;
		w->acquisition_probability[index] = p_key;
		w->acquisition_low[index] = !!low_snr;
	}
	md_wfst_step(d, c, p_key, current_tick, low_snr);
	if (w->acquisition_active && !c->stats.learning)
		md_wfst_replay(d, c);
}

void md_wfst_flush(const struct morsedec_decoder *d, struct md_channel *c)
{
	struct md_wfst_decoder *w = &c->wfst;
	if (w->sealed)
		return;
	md_stats_release(d, c);
	if (w->acquisition_active)
		md_wfst_replay(d, c);
	md_wfst_reserve(d, c);
	if (c->stats.dirty)
		md_wfst_durations(d, c);
	int best = 0;
	double score = -INFINITY;
	for (int i = 0; i < w->count; i++) {
		const struct md_token *t = &w->tokens[w->bank][i];
		int k = md_wfst_nodes[t->current_node_id].duration_class;
		if (k != MD_WFST_NO_DURATION)
			md_wfst_prepare_duration(d, c, k, t->frame_age);
		double terminal = t->accumulated_score +
				  (k == MD_WFST_NO_DURATION
				       ? 0
				       : w->termination[k][t->frame_age]);
		if (terminal > score) {
			score = terminal;
			best = i;
		}
	}
	struct md_token *t = &w->tokens[w->bank][0];
	*t = w->tokens[w->bank][best];
	w->count = 1;
	if (t->run_start && w->tick >= t->run_start) {
		int k = md_wfst_nodes[t->current_node_id].duration_class;
		int duration = (int)fmin((double)(w->tick - t->run_start + 1),
					 INT_MAX / 4);
		struct md_wfst_event event = {.start = t->run_start,
					      .ordinal = w->tick,
					      .duration =
						  k < 2 ? duration : -duration,
					      .low = t->run_low};
		t->history[0] = md_wfst_intern(w, t->history[0], &event);
	}
	if (t->mark_tick) {
		uint16_t label = md_wfst_nodes[t->current_node_id].output_label;
		struct md_wfst_event event = {.ordinal = t->mark_tick,
					      .word_probability = 2,
					      .output_label = label ? label : 1,
					      .low = t->character_low,
					      .domain = 1};
		t->history[1] = md_wfst_intern(w, t->history[1], &event);
	}
	md_wfst_check_convergence(d, c);
	md_stats_release(d, c);
	md_word_emit(d, c, 1);
	w->sealed = 1;
}
