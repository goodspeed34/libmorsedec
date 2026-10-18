/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "internal.h"
#include <assert.h>
#include "wfst_graph.h"

_Static_assert(MD_WFST_GRAPH_DEGREE <= MD_WFST_DEGREE,
	       "WFST candidate capacity is too small");
_Static_assert(MD_WFST_GRAPH_NODES <= UINT8_MAX + 1,
	       "History boundary states must fit uint8_t");

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

/* Initial speed is uncertain until STA has observed enough complete runs.
 * Broaden acquisition without relaxing the calibrated streaming model.
 */
static double md_wfst_duration_variance(const struct md_channel *c, int kind)
{
	double minimum = c->wfst.acquisition_active ? .25 : .04;
	return fmax(c->stats.variance[kind], minimum);
}

/* Log of the normal upper tail, including arguments for which erfc underflows.
 */
static double md_wfst_log_survival(double duration, double mean,
				   double variance)
{
	double z = (log(duration) - mean) / sqrt(variance);
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
		w->survival[k] =
		    md_wfst_log_survival(.5 * d->tick_scale, c->stats.mean[k],
					 md_wfst_duration_variance(c, k));
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
		double next = md_wfst_log_survival(
		    (a + .5) * d->tick_scale, c->stats.mean[kind],
		    md_wfst_duration_variance(c, kind));
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
	c->decode_frame = 0;
	memset(&c->lattice, 0, sizeof(c->lattice));
	c->lattice.node_count = 1;
	for (int i = 0; i < MD_LATTICE_MAX_NODES; i++)
		c->lattice.nodes[i].first_edge = MD_LATTICE_NONE;
	for (int i = 0; i < MD_LATTICE_MAX_EDGES; i++)
		c->lattice.edges[i].next_edge = MD_LATTICE_NONE;
	c->lattice_work.path_count = 0;
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
	    exp(-MD_WFST_BEAM_WIDE + w->idle_stay - w->idle_start - onset);
	w->idle_threshold = odds / (1 + odds);
	md_wfst_durations(d, c);
}

static uint32_t md_wfst_hash(uint32_t parent, const struct md_wfst_event *e,
			     uint8_t state)
{
	uint64_t bits;
	memcpy(&bits, &e->word_probability, sizeof(bits));
	uint64_t acoustic_bits;
	memcpy(&acoustic_bits, &e->acoustic_score, sizeof(acoustic_bits));
	uint64_t x =
	    e->start ^ (e->ordinal * UINT64_C(0x9e3779b97f4a7c15)) ^ bits ^
	    acoustic_bits ^ ((uint64_t)(uint32_t)e->duration << 19) ^
	    ((uint64_t)parent << 7) ^ ((uint64_t)e->output_label << 1) ^
	    ((uint64_t)e->low << 43) ^ ((uint64_t)state << 51) ^ e->domain;
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
	       a->acoustic_score == b->acoustic_score &&
	       a->duration == b->duration &&
	       a->output_label == b->output_label && a->low == b->low &&
	       a->domain == b->domain;
}

static uint32_t md_wfst_intern(struct md_wfst_decoder *w, uint32_t parent,
			       const struct md_wfst_event *event, uint8_t state)
{
	uint32_t bucket = md_wfst_hash(parent, event, state);
	for (uint32_t i = w->buckets[bucket]; i; i = w->history[i].next)
		if (w->history[i].parent == parent &&
		    w->history[i].state == state &&
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
	h->state = state;
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
			uint32_t bucket =
			    md_wfst_hash(h->parent, &h->event, h->state);
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

const char *md_wfst_text(uint16_t label)
{
	return label && label < MD_WFST_GRAPH_SYMBOLS ? md_wfst_symbols[label]
						      : md_wfst_symbols[1];
}

static uint32_t md_wfst_text_hash(struct md_wfst_decoder *w, uint16_t head)
{
	int count = 0;
	for (uint16_t id = head; id && count < MD_WFST_HISTORY;
	     id = w->history[id].parent)
		w->trace[count++] = id;
	uint32_t hash = 0;
	while (count)
		hash =
		    (hash ^ w->history[w->trace[--count]].event.output_label) *
		    UINT32_C(16777619);
	return hash;
}

static void md_wfst_order(struct md_wfst_decoder *w, int bank, int count)
{
	for (int i = 1; i < count; i++) {
		struct md_token value = w->tokens[bank][i];
		int j = i;
		while (j && w->tokens[bank][j - 1].accumulated_score <
				value.accumulated_score) {
			w->tokens[bank][j] = w->tokens[bank][j - 1];
			j--;
		}
		w->tokens[bank][j] = value;
	}
}

static uint64_t md_wfst_boundary(const struct md_wfst_history *h)
{
	return h->event.ordinal + 1 + (uint64_t)h->event.duration;
}

/* Recombination points have identical source time, post-label WFST state and
 * depth. Sort them by time before exposing the DAG to the word Viterbi pass.
 */
static void md_wfst_lattice_sort(struct md_channel *c)
{
	struct md_wfst_decoder *w = &c->wfst;
	struct md_lattice *l = &c->lattice;
	struct md_lattice_work *work = &c->lattice_work;
	for (int i = 0; i < l->node_count; i++)
		work->node_order[i] = (uint16_t)i;
	for (int i = 2; i < l->node_count; i++) {
		uint16_t value = work->node_order[i];
		uint64_t time =
		    md_wfst_boundary(&w->history[work->node_history[value]]);
		int j = i;
		while (j > 1 &&
		       md_wfst_boundary(
			   &w->history
				[work->node_history[work->node_order[j - 1]]]) >
			   time) {
			work->node_order[j] = work->node_order[j - 1];
			j--;
		}
		work->node_order[j] = value;
	}
	for (int i = 0; i < l->node_count; i++) {
		uint16_t old = work->node_order[i];
		work->sorted_nodes[i] = l->nodes[old];
		w->selected[old] = (uint16_t)i;
		w->trace[i] = work->node_history[old];
	}
	memcpy(l->nodes, work->sorted_nodes,
	       l->node_count * sizeof(l->nodes[0]));
	for (int i = 0; i < l->node_count; i++)
		work->node_history[i] = w->trace[i];
	for (int i = 0; i < MD_WFST_HISTORY; i++)
		if (work->history_nodes[i] != MD_LATTICE_NONE)
			work->history_nodes[i] =
			    w->selected[work->history_nodes[i]];
	for (int i = 0; i < l->edge_count; i++)
		l->edges[i].to_node = w->selected[l->edges[i].to_node];
}

/* Retain a DAG of complete text events. Each terminal also carries the
 * score of its unfinished suffix, so different completion times are comparable.
 * Capacity pressure is resolved by the caller before the next frame expands.
 */
static int md_wfst_lattice_build(const struct morsedec_decoder *d,
				 struct md_channel *c)
{
	struct md_wfst_decoder *w = &c->wfst;
	struct md_lattice *l = &c->lattice;
	struct md_lattice_work *work = &c->lattice_work;
	memset(work->history_nodes, 0xff, sizeof(work->history_nodes));
	work->history_nodes[0] = 0;
	work->node_history[0] = 0;
	l->node_count = 1;
	l->edge_count = l->max_depth = 0;
	l->nodes[0] = (struct md_lattice_node){.first_edge = MD_LATTICE_NONE};
	int overflow = 0;
	for (int i = 0; i < w->count; i++) {
		const struct md_token *t = &w->tokens[w->bank][i];
		int count = 0;
		for (uint16_t id = t->history[1]; id && count < MD_WFST_HISTORY;
		     id = w->history[id].parent)
			w->trace[count++] = id;
		uint16_t from = 0;
		int complete = 1;
		double suffix = t->character_score;
		while (count) {
			uint16_t id = w->trace[count - 1];
			const struct md_wfst_event *e = &w->history[id].event;
			const char *text = md_wfst_text(e->output_label);
			int depth =
			    l->nodes[from].max_depth + (text[1] ? 2 : 1);
			if (depth > MD_LATTICE_MAX_DEPTH) {
				overflow = 1;
				break;
			}
			uint16_t to = work->history_nodes[id];
			if (to == MD_LATTICE_NONE) {
				const struct md_wfst_history *h =
				    &w->history[id];
				for (int j = 1; j < l->node_count; j++) {
					const struct md_wfst_history *other =
					    &w->history[work->node_history[j]];
					if (l->nodes[j].max_depth == depth &&
					    h->state == other->state &&
					    h->depth == other->depth &&
					    (md_wfst_nodes[h->state]
						     .node_type !=
						 MD_WFST_GAP_NODE ||
					     h->event.duration ==
						 other->event.duration) &&
					    md_wfst_boundary(h) ==
						md_wfst_boundary(other)) {
						to = (uint16_t)j;
						break;
					}
				}
				if (to == MD_LATTICE_NONE) {
					if (l->node_count ==
					    MD_LATTICE_MAX_NODES) {
						overflow = 1;
						complete = 0;
						break;
					}
					to = l->node_count++;
					work->node_history[to] = id;
					l->nodes[to] = (struct md_lattice_node){
					    .first_edge = MD_LATTICE_NONE,
					    .max_depth = (uint16_t)depth};
				}
				if (l->edge_count == MD_LATTICE_MAX_EDGES) {
					overflow = 1;
					complete = 0;
					break;
				}
				work->history_nodes[id] = to;
				uint16_t edge = l->edge_count++;
				l->edges[edge] = (struct md_lattice_edge){
				    .acoustic_score = e->acoustic_score,
				    .word_space_probability =
					e->word_probability < 0
					    ? md_wfst_word_probability(
						  d, c, (uint64_t)e->duration)
					    : e->word_probability,
				    .start_frame =
					e->start ? e->start : e->ordinal,
				    .end_frame = e->ordinal + 1,
				    .character_label = e->output_label,
				    .to_node = to,
				    .next_edge = l->nodes[from].first_edge,
				    .low_snr = e->low};
				l->nodes[from].first_edge = edge;
				l->nodes[from].edge_count++;
				if (depth > l->max_depth)
					l->max_depth = (uint16_t)depth;
			}
			from = to;
			count--;
		}
		if (!complete)
			break;
		/* A depth-limited prefix remains a valid terminal. Account for
		 * its omitted acoustic suffix without adding more lattice
		 * nodes.
		 */
		while (count)
			suffix +=
			    w->history[w->trace[--count]].event.acoustic_score;
		struct md_lattice_node *node = &l->nodes[from];
		if (!(node->flags & MD_LATTICE_NODE_FINAL) ||
		    suffix > node->final_score)
			node->final_score = suffix;
		node->flags |= MD_LATTICE_NODE_FINAL;
	}
	md_wfst_lattice_sort(c);
	return overflow;
}

static void md_wfst_lattice_update(const struct morsedec_decoder *d,
				   struct md_channel *c, int force)
{
	struct md_wfst_decoder *w = &c->wfst;
	if (w->acquisition_active)
		return;
	for (int pass = 0; pass < MD_WFST_HISTORY; pass++) {
		int overflow = md_wfst_lattice_build(d, c);
		uint32_t common = w->tokens[w->bank][0].history[1];
		for (int i = 1; i < w->count && common; i++)
			common = md_wfst_ancestor(
			    w, common, w->tokens[w->bank][i].history[1]);
		if (!common && !overflow && !force)
			return;
		int count = md_word_rescore(
		    d, c, common || ((overflow || force == 1) && force != 2));
		if (!count) {
			/* A real empty transcript can win at silence or EOF. */
			if (force || overflow) {
				int kept = 0;
				for (int i = 0; i < w->count; i++)
					if (!w->tokens[w->bank][i].history[1])
						w->tokens[w->bank][kept++] =
						    w->tokens[w->bank][i];
				if (kept) {
					const uint32_t depth[2] = {0, 0};
					if (overflow) {
						c->lattice.forced_commits++;
						w->forced_decisions++;
					}
					w->count = kept;
					md_wfst_collect(w, depth);
					md_wfst_lattice_build(d, c);
				}
			}
			return;
		}
		int commit = force ? count : 1;
		if (!force && common) {
			uint16_t target = c->lattice_work.history_nodes[common];
			for (int i = 0; i < count; i++)
				if (c->lattice.edges[c->lattice_work.path[i]]
					.to_node == target)
					commit = i + 1;
		}
		uint16_t node =
		    c->lattice.edges[c->lattice_work.path[commit - 1]].to_node;
		uint16_t prefix = c->lattice_work.node_history[node];
		uint32_t depth[2] = {0, w->history[prefix].depth};
		double prefix_score = 0;
		for (int i = 0; i < commit; i++)
			prefix_score +=
			    c->lattice.edges[c->lattice_work.path[i]]
				.acoustic_score;
		md_word_lattice_commit(d, c, commit);
		w->text_commits += (uint64_t)commit;
		if (!common && (overflow || force == 1)) {
			c->lattice.forced_commits++;
			w->forced_decisions++;
		}
		int kept = 0;
		for (int i = 0; i < w->count; i++) {
			struct md_token t = w->tokens[w->bank][i];
			uint16_t id = t.history[1];
			for (int steps = 0;
			     id && w->history[id].depth > depth[1] &&
			     steps < MD_WFST_HISTORY;
			     steps++)
				id = w->history[id].parent;
			if (!id || c->lattice_work.history_nodes[id] != node)
				continue;
			double old_prefix = 0;
			for (uint16_t p = id; p; p = w->history[p].parent)
				old_prefix +=
				    w->history[p].event.acoustic_score;
			t.accumulated_score += prefix_score - old_prefix;
			if (t.history[1] == id)
				t.history[1] = 0;
			w->tokens[w->bank][kept++] = t;
		}
		assert(kept);
		w->count = kept;
		md_wfst_order(w, w->bank, kept);
		for (uint32_t i = 1; i <= w->history_high; i++)
			if (w->history[i].event.domain == 1 &&
			    w->history[i].parent &&
			    c->lattice_work
				    .history_nodes[w->history[i].parent] ==
				node)
				w->history[i].parent = 0;
		md_wfst_collect(w, depth);
		for (int i = 0; i < w->count; i++) {
			struct md_token *t = &w->tokens[w->bank][i];
			t->text_hash = md_wfst_text_hash(w, t->history[1]);
			t->text_depth = w->history[t->history[1]].depth;
		}
	}
}

void md_wfst_check_convergence(const struct morsedec_decoder *d,
			       struct md_channel *c)
{
	struct md_wfst_decoder *w = &c->wfst;
	if (!w->count)
		return;
	uint32_t root = w->tokens[w->bank][0].history[0];
	for (int i = 1; i < w->count && root; i++)
		root =
		    md_wfst_ancestor(w, root, w->tokens[w->bank][i].history[0]);
	if (root) {
		uint32_t depth[2] = {w->history[root].depth, 0};
		int count = 0;
		for (uint32_t id = root; id; id = w->history[id].parent)
			w->trace[count++] = (uint16_t)id;
		while (count) {
			const struct md_wfst_event *e =
			    &w->history[w->trace[--count]].event;
			if (w->replaying) {
				if (e->ordinal > c->stats.committed_end)
					c->stats.committed_end = e->ordinal;
				continue;
			}
			struct md_element element = {.duration = e->duration,
						     .ordinal = e->ordinal,
						     .low = e->low};
			md_stats_observe(d, c, &element);
			w->acoustic_commits++;
		}
		for (int i = 0; i < w->count; i++)
			if (w->tokens[w->bank][i].history[0] == root)
				w->tokens[w->bank][i].history[0] = 0;
		for (uint32_t i = 1; i <= w->history_high; i++)
			if (!w->history[i].event.domain &&
			    w->history[i].parent == root)
				w->history[i].parent = 0;
		md_wfst_collect(w, depth);
	}
	md_wfst_lattice_update(d, c, 0);
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
		md_wfst_lattice_update(d, c, 1);
		if (w->free_count >= 2 * MD_WFST_BEAM)
			return;
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
	/* Calibrate ambiguous boundaries before the word graph amplifies
	 * their odds. Long silences still force a boundary above. */
	double b = .5 * md_score(&c->stats, 4, duration);
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
	if (edge->output_label && source->mark_tick) {
		if (!c->wfst.acquisition_active) {
			present |= 2;
			event[1] = (struct md_wfst_event){
			    .start = source->character_start,
			    .ordinal = source->mark_tick,
			    .acoustic_score = t->character_score,
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
		t->character_start = 0;
		t->character_score = 0;
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
	if (on) {
		if (!t->character_start)
			t->character_start = tick;
		t->mark_tick = tick;
	}
	if (edge->target_node)
		t->character_low |= (uint8_t)low;
	return present;
}

/* Threshold uses the local FFT-bin signal/noise power ratio at 0 dB.
 * This estimate is deliberately distinct from a 2,500 Hz benchmark SNR.
 */
static double md_wfst_beam(const struct md_channel *c, uint16_t node, int low)
{
	if (c->wfst.acquisition_active)
		return MD_WFST_BEAM_NORMAL;
	if (md_wfst_nodes[node].node_type != MD_WFST_MARK_NODE || low ||
	    !c->mean_length || !isfinite(c->square) || c->square <= 2)
		return MD_WFST_BEAM_WIDE;
	return MD_WFST_BEAM_CLEAR;
}

static int md_wfst_same_text(const struct md_wfst_decoder *w,
			     const struct md_wfst_candidate *a,
			     const struct md_wfst_candidate *b);
static int md_wfst_near(uint64_t a, uint64_t b, uint64_t tolerance);

static int md_wfst_same_timing(const struct md_wfst_decoder *w,
			       const struct md_token *a,
			       const struct md_token *b, uint64_t tolerance)
{
	if (!md_wfst_near(a->character_start, b->character_start, tolerance) ||
	    !md_wfst_near(a->mark_tick, b->mark_tick, tolerance))
		return 0;
	uint16_t ha = a->history[1], hb = b->history[1];
	for (int i = 0; i < MD_WFST_HISTORY; i++) {
		if (!ha || !hb)
			return ha == hb;
		const struct md_wfst_event *ea = &w->history[ha].event,
					   *eb = &w->history[hb].event;
		if (!md_wfst_near(ea->start, eb->start, tolerance) ||
		    !md_wfst_near(ea->ordinal, eb->ordinal, tolerance) ||
		    !md_wfst_near((uint64_t)ea->duration,
				  (uint64_t)eb->duration, tolerance))
			return 0;
		ha = w->history[ha].parent;
		hb = w->history[hb].parent;
	}
	return 0;
}

static void md_wfst_candidate_add(struct md_wfst_decoder *w,
				  const struct md_wfst_candidate *candidate,
				  uint64_t tolerance)
{
	const struct md_token *source = &w->tokens[w->bank][candidate->source];
	uint32_t key = (candidate->current_node_id * 2053u +
			candidate->frame_age + candidate->text_hash * 97u) &
		       (MD_WFST_KEYS - 1);
	for (int probe = 0; probe < MD_WFST_KEYS && w->keys[key]; probe++) {
		struct md_wfst_candidate *old =
		    &w->candidates[w->keys[key] - 1];
		const struct md_token *other = &w->tokens[w->bank][old->source];
		if (old->current_node_id == candidate->current_node_id &&
		    old->frame_age == candidate->frame_age &&
		    (w->acquisition_active ||
		     (md_wfst_same_text(w, old, candidate) &&
		      md_wfst_same_timing(w, other, source, tolerance)))) {
			if (candidate->accumulated_score >
			    old->accumulated_score)
				*old = *candidate;
			return;
		}
		key = (key + 1) & (MD_WFST_KEYS - 1);
	}
	assert(!w->keys[key] && w->candidate_count < MD_WFST_CANDIDATES);
	w->candidate_keys[w->candidate_count] = (uint16_t)key;
	w->keys[key] = (uint16_t)(w->candidate_count + 1);
	w->candidates[w->candidate_count++] = *candidate;
}

static int md_wfst_same_text(const struct md_wfst_decoder *w,
			     const struct md_wfst_candidate *a,
			     const struct md_wfst_candidate *b)
{
	if (a->text_hash != b->text_hash || a->text_depth != b->text_depth)
		return 0;
	uint16_t ha = w->tokens[w->bank][a->source].history[1];
	uint16_t hb = w->tokens[w->bank][b->source].history[1];
	uint16_t la =
	    w->acquisition_active || !w->tokens[w->bank][a->source].mark_tick
		? 0
		: md_wfst_edges[a->edge].output_label;
	uint16_t lb =
	    w->acquisition_active || !w->tokens[w->bank][b->source].mark_tick
		? 0
		: md_wfst_edges[b->edge].output_label;
	for (int steps = 0; steps <= MD_WFST_HISTORY; steps++) {
		uint16_t ca = la ? la : w->history[ha].event.output_label;
		uint16_t cb = lb ? lb : w->history[hb].event.output_label;
		if (ca != cb)
			return 0;
		if (!ca)
			return 1;
		if (la)
			la = 0;
		else
			ha = w->history[ha].parent;
		if (lb)
			lb = 0;
		else
			hb = w->history[hb].parent;
	}
	return 0;
}

static int md_wfst_near(uint64_t a, uint64_t b, uint64_t tolerance)
{
	return (a >= b ? a - b : b - a) <= tolerance;
}

/* Greedy rank pruning preserves the raw acoustic score. Only selection scores
 * pay a diversity cost for nearby duration variants of the same label history.
 */
static int md_wfst_rank(const struct morsedec_decoder *d, struct md_channel *c,
			double best, int low)
{
	struct md_wfst_decoder *w = &c->wfst;
	uint64_t tolerance = (uint64_t)md_frames(d, 2);
	int count = 0;
	for (; count < MD_WFST_BEAM; count++) {
		int chosen = -1;
		double rank = MD_NEG;
		for (int i = 0; i < w->candidate_count; i++) {
			const struct md_wfst_candidate *t = &w->candidates[i];
			if (t->accumulated_score <
			    best - md_wfst_beam(c, t->current_node_id, low))
				continue;
			if (t->rank_score > rank) {
				rank = t->rank_score;
				chosen = i;
			}
		}
		if (chosen < 0)
			break;
		w->selected[count] = (uint16_t)chosen;
		struct md_wfst_candidate *picked = &w->candidates[chosen];
		picked->rank_score = MD_NEG;
		if (w->acquisition_active)
			continue;
		const struct md_token *source =
		    &w->tokens[w->bank][picked->source];
		for (int i = 0; i < w->candidate_count; i++) {
			struct md_wfst_candidate *other = &w->candidates[i];
			const struct md_token *t =
			    &w->tokens[w->bank][other->source];
			if (other->rank_score <= MD_NEG / 2 ||
			    other->current_node_id != picked->current_node_id ||
			    !md_wfst_near(other->frame_age, picked->frame_age,
					  tolerance) ||
			    !md_wfst_near(t->character_start,
					  source->character_start, tolerance) ||
			    !md_wfst_near(t->mark_tick, source->mark_tick,
					  tolerance))
				continue;
			if (md_wfst_same_text(w, picked, other))
				other->rank_score -= MD_WFST_DIVERSITY_COST;
		}
	}
	return count;
}

/* A long observed silence closes pending characters without requiring every
 * low-scoring duration tail to enter idle. Keep the physical gap state and age;
 * its future continuation costs belong to the next character's score.
 */
static void md_wfst_close_gap(const struct morsedec_decoder *d,
			      struct md_channel *c)
{
	struct md_wfst_decoder *w = &c->wfst;
	md_wfst_reserve(d, c);
	for (int i = 0; i < w->count; i++) {
		struct md_token *t = &w->tokens[w->bank][i];
		const struct md_wfst_node *node =
		    &md_wfst_nodes[t->current_node_id];
		if (!t->mark_tick || node->node_type == MD_WFST_MARK_NODE)
			continue;
		struct md_wfst_event event = {
		    .start = t->character_start,
		    .ordinal = t->mark_tick,
		    .duration = (int32_t)fmin((double)(w->tick - t->mark_tick),
					      INT_MAX / 4),
		    .word_probability = 2,
		    .acoustic_score = t->character_score,
		    .output_label = node->output_label ? node->output_label : 1,
		    .low = t->character_low,
		    .domain = 1};
		t->history[1] = md_wfst_intern(w, t->history[1], &event,
					       (uint8_t)t->current_node_id);
		if (t->mark_tick > c->decode_frame)
			c->decode_frame = t->mark_tick;
		t->character_start = t->mark_tick = 0;
		t->character_low = 0;
		t->character_score = 0;
		t->text_hash =
		    (t->text_hash ^ event.output_label) * UINT32_C(16777619);
		t->text_depth++;
	}
	md_wfst_lattice_update(d, c, 2);
	md_word_emit(d, c, 1);
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
	/* Before the expected end of a calibrated dash, allow a frame of
	 * interference without letting its negative evidence alone prune
	 * the continuing mark. Onsets and dots retain the full likelihood.
	 */
	double dash_frames =
	    exp(c->stats.mean[MD_WFST_DASH_DURATION]) / d->tick_scale;
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
			double acoustic =
			    observation[e->input_label == MD_WFST_GAP];
			if (!w->acquisition_active && stay &&
			    n->duration_class == MD_WFST_DASH_DURATION &&
			    t->frame_age < dash_frames)
				acoustic = fmax(acoustic, -4.0);
			double score = t->accumulated_score + duration +
				       e->log_weight + acoustic;
			if (!isfinite(score) ||
			    score < best - MD_WFST_BEAM_WIDE)
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
			candidate.rank_score = score;
			candidate.text_hash = t->text_hash;
			candidate.text_depth = t->text_depth;
			if (e->output_label && t->mark_tick &&
			    !w->acquisition_active) {
				candidate.text_hash =
				    (candidate.text_hash ^ e->output_label) *
				    UINT32_C(16777619);
				candidate.text_depth++;
			}
			md_wfst_candidate_add(w, &candidate,
					      (uint64_t)md_frames(d, 1));
		}
	}
	int count = md_wfst_rank(d, c, best, low_snr);
	for (int i = 0; i < count; i++) {
		struct md_wfst_candidate *candidate =
		    &w->candidates[w->selected[i]];
		struct md_token *t = &w->tokens[!w->bank][i];
		*t = w->tokens[w->bank][candidate->source];
		t->current_node_id = candidate->current_node_id;
		t->frame_age = candidate->frame_age;
		t->character_score +=
		    candidate->accumulated_score - t->accumulated_score;
		t->accumulated_score = candidate->accumulated_score - best;
		t->text_hash = candidate->text_hash;
		t->text_depth = candidate->text_depth;
		struct md_wfst_event event[2] = {0};
		unsigned int present =
		    md_wfst_events(c, &w->tokens[w->bank][candidate->source],
				   &md_wfst_edges[candidate->edge],
				   current_tick, !!low_snr, t, event);
		if ((present & 2) && event[1].ordinal > c->decode_frame)
			c->decode_frame = event[1].ordinal;
		for (int domain = 0; domain < 2; domain++)
			if (present & (1u << domain))
				t->history[domain] = md_wfst_intern(
				    w, t->history[domain], &event[domain],
				    domain ? (uint8_t)t->current_node_id : 0);
	}
	/* Diversity controls membership; retain acoustic ordering for recovery.
	 */
	md_wfst_order(w, !w->bank, count);
	w->bank = !w->bank;
	w->count = count;
	if (!count) {
		w->recoveries++;
		w->count = 1;
		w->tokens[w->bank][0] = (struct md_token){0};
	}
	md_wfst_check_convergence(d, c);
	const struct md_token *best_token = &w->tokens[w->bank][0];
	if (!w->acquisition_active && p_key < .1 && best_token->run_start &&
	    md_wfst_nodes[best_token->current_node_id].node_type !=
		MD_WFST_MARK_NODE &&
	    current_tick - best_token->run_start >= (uint64_t)md_frames(d, 150))
		md_wfst_close_gap(d, c);
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
	/* Acquisition and calibrated replay use different predictive widths. */
	md_wfst_durations(d, c);
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
	if (w->acquisition_active && c->quiet == md_frames(d, 75)) {
		/* A short QSO may end before acoustic histories agree. Four
		 * marks on the best path permit calibration at a real pause;
		 * fewer marks still require normal convergence or pinning. */
		int marks = c->stats.acquisition_marks;
		uint32_t id = w->tokens[w->bank][0].history[0];
		while (id && marks < 4) {
			marks += w->history[id].event.duration > 0;
			id = w->history[id].parent;
		}
		if (marks >= 4) {
			w->count = 1;
			md_wfst_check_convergence(d, c);
			md_stats_release(d, c);
		}
	}
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
	/* Finalize every surviving head before the word layer chooses a path.
	 */
	for (int i = 0; i < w->count; i++) {
		struct md_token *t = &w->tokens[w->bank][i];
		int k = md_wfst_nodes[t->current_node_id].duration_class;
		if (k != MD_WFST_NO_DURATION) {
			md_wfst_prepare_duration(d, c, k, t->frame_age);
			t->character_score += w->termination[k][t->frame_age];
		}
		if (t->run_start && w->tick >= t->run_start) {
			int duration = (int)fmin(
			    (double)(w->tick - t->run_start + 1), INT_MAX / 4);
			struct md_wfst_event event = {
			    .start = t->run_start,
			    .ordinal = w->tick,
			    .duration = k < 2 ? duration : -duration,
			    .low = t->run_low};
			t->history[0] =
			    md_wfst_intern(w, t->history[0], &event, 0);
		}
		if (t->mark_tick) {
			uint16_t label =
			    md_wfst_nodes[t->current_node_id].output_label;
			struct md_wfst_event event = {
			    .start = t->character_start,
			    .ordinal = t->mark_tick,
			    .duration = (int32_t)fmin(
				(double)(w->tick - t->mark_tick), INT_MAX / 4),
			    .word_probability = 2,
			    .acoustic_score = t->character_score,
			    .output_label = label ? label : 1,
			    .low = t->character_low,
			    .domain = 1};
			t->history[1] =
			    md_wfst_intern(w, t->history[1], &event, 0);
			t->character_score = 0;
			t->text_depth++;
			t->text_hash = (t->text_hash ^ event.output_label) *
				       UINT32_C(16777619);
		}
	}
	md_wfst_check_convergence(d, c);
	md_wfst_lattice_update(d, c, 2);
	md_wfst_check_convergence(d, c);
	md_stats_release(d, c);
	md_word_emit(d, c, 1);
	w->sealed = 1;
}
