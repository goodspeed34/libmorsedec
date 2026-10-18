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
#include "templates.h"

static int node(struct md_model *m, int selector, int code)
{
	int i = m->count;
	if (i == MD_NODES) {
		m->failed = 1;
		return 0;
	}
	if (selector && m->selector_count == MD_WORD_SELECTORS) {
		m->failed = 1;
		return 0;
	}
	m->count++;
	if (selector) {
		m->selectors[m->selector_count++] = (uint16_t)i;
		m->nodes[i].selector = m->selector_count;
	}
	m->nodes[i].code = code;
	m->nodes[i].kind = m->kind;
	m->nodes[i].outgoing = -1;
	return i;
}

static void edge(struct md_model *m, int from, int to, float weight)
{
	int e = m->edge_count;
	if (e == MD_EDGES || weight <= 0) {
		m->failed = 1;
		return;
	}
	m->edge_count++;
	m->edges[e] =
	    (struct md_edge){from, to, m->nodes[from].outgoing, weight};
	m->nodes[from].outgoing = e;
}

static int successor(struct md_model *m, int from, int code, float weight)
{
	for (int e = m->nodes[from].outgoing; e >= 0; e = m->edges[e].next) {
		int t = m->edges[e].to;
		if (!m->nodes[t].selector && m->nodes[t].code == code &&
		    m->nodes[t].kind == m->kind) {
			m->edges[e].weight += weight;
			return t;
		}
	}
	{
		int t = node(m, 0, code);
		edge(m, from, t, weight);
		m->nodes[t].parent = from;
		return t;
	}
}

static void chain(struct md_model *m, int from, int to, float scale,
		  const struct md_template *entry)
{
	float w = scale * entry->weight;
	for (const char *p = entry->text; *p; p++)
		from = successor(m, from, (unsigned char)*p, w);
	edge(m, from, to, powf(w, 0.85f));
}

static int level(struct md_model *m, int from, float scale,
		 const struct md_template *table, size_t count)
{
	int accept = node(m, 1, 0);
	for (size_t i = 0; i < count; i++)
		chain(m, from, accept, scale, &table[i]);
	return accept;
}
#define MD_COUNT(a) (sizeof(a) / sizeof((a)[0]))

static int core(struct md_model *m, int from, float scale)
{
	static const struct md_template l1[] = {{"@", .199743f},
						{"@@", .74399f},
						{"#@", .054979f},
						{"#@@", .001288f}};
	static const struct md_template l2[] = {{"#", .939686f},
						{"##", .052986f},
						{"###", .00501f},
						{"####", .002317f}};
	static const struct md_template l3[] = {
	    {"@", .021387f},	{"@@", .392918f},    {"@@@", .571806f},
	    {"@@@@", .009582f}, {"@@@@@", .002967f}, {"@@@@@@", .00134f}};
	from = level(m, from, scale, l1, MD_COUNT(l1));
	from = level(m, from, scale, l2, MD_COUNT(l2));
	return level(m, from, scale, l3, MD_COUNT(l3));
}

int md_model_init(struct md_model *m)
{
	static const struct md_template s1[] = {
	    {"#@@", .002199f},	{"@#@", .008175f}, {"@##", .014827f},
	    {"@@#@", .018811f}, {"#@", .045966f},  {"#@#", .054554f},
	    {"@", .055701f},	{"@#", .08404f},   {"@@", .219683f},
	    {"@@#", .496044f}};
	static const struct md_template s2[] = {
	    {"FJL", .000367f},	{"G", .00078f},	   {"E", .001056f},
	    {"QRPP", .001446f}, {"ANT", .001675f}, {"T", .002891f},
	    {"J", .004314f},	{"LGT", .005002f}, {"AM", .007022f},
	    {"A", .007f},	{"LH", .015627f},  {"MM", .049772f},
	    {"QRP", .067441f},	{"M", .117189f},   {"#", .272562f},
	    {"P", .439088f}};
	static const struct md_template reports[] = {
	    {"5NN", .8f},   {"HNN", .05f},  {"SNN", .05f}, {"SENN", .05f},
	    {"ESNN", .05f}, {"IINN", .05f}, {"VNN", .05f}, {"FNN", .05f},
	    {"LNN", .05f},  {"BNN", .05f},  {"3#N", .05f}, {"4#N", .05f},
	    {"5#N", .05f},  {"3#9", .05f},  {"4#9", .05f}, {"5#9", .05f},
	    {"H#N", .05f}};
	static const struct md_template prosigns[] = {
	    {"DE", .45f},  {"TEST", .1f}, {"SS", .05f},	 {"UP", .05f},
	    {"UP1", .05f}, {"UP2", .05f}, {"QRZ", .05f}, {"QRZ?", .05f},
	    {"FD", .05f},  {"NA", .05f},  {"CQ", .05f}};
	const float a = .806858f, b = .076643f, c = .002267f, d = .075624f;
	const float e = .011884f, f = .000002f, g = .026237f, h = .000485f,
		    i = .000002f;
	int cs, suffix, prefix, split, portable;
	memset(m, 0, sizeof(*m));
	node(m, 1, 0);
	m->kind = MORSEDEC_WORD_CALLSIGN;
	cs = core(m, 0, .6f * (a + b + c + g + h + i));
	edge(m, cs, 0, a);
	suffix = successor(m, cs, '/', .6f * (g + h + i));
	suffix = level(m, suffix, .6f * (g + h + i), s1, MD_COUNT(s1));
	edge(m, suffix, 0, g);
	prefix = level(m, 0, .6f * (d + e + f), s1, MD_COUNT(s1));
	prefix = successor(m, prefix, '/', .6f * (d + e + f));
	prefix = core(m, prefix, .6f * (d + e + f));
	edge(m, prefix, 0, d);
	split = node(m, 1, 0);
	edge(m, cs, split, .6f * (b + c));
	edge(m, suffix, split, .6f * (h + i));
	edge(m, prefix, split, .6f * (e + f));
	portable = successor(m, split, '/', .6f * (b + c + h + i + e + f));
	portable =
	    level(m, portable, .6f * (b + c + h + i + e + f), s2, MD_COUNT(s2));
	edge(m, portable, 0, .6f * (b + h + e));
	portable = successor(m, portable, '/', .6f * (c + i + f));
	portable = level(m, portable, .6f * (c + i + f), s2, MD_COUNT(s2));
	edge(m, portable, 0, .6f * (c + i + f));
	m->kind = MORSEDEC_WORD_DICTIONARY;
	for (size_t k = 0; k < MD_COUNT(dictionary); k++)
		chain(m, 0, 0, .01f, &dictionary[k]);
	m->kind = MORSEDEC_WORD_REPORT;
	for (size_t k = 0; k < MD_COUNT(reports); k++)
		chain(m, 0, 0, .6f, &reports[k]);
	m->kind = MORSEDEC_WORD_PROSIGN;
	for (size_t k = 0; k < MD_COUNT(prosigns); k++)
		chain(m, 0, 0, .01f, &prosigns[k]);
	m->kind = MORSEDEC_WORD_QRL;
	chain(m, 0, 0, .01f, &(struct md_template){"QRL?", 1});
	chain(m, 0, 0, .01f, &(struct md_template){"QRL", 1});
	m->kind = MORSEDEC_WORD_GENERIC;
	chain(m, 0, 0, .1f, &(struct md_template){"+", .12f});
	chain(m, 0, 0, .1f, &(struct md_template){"##", .03f});
	chain(m, 0, 0, .1f, &(struct md_template){"###", .1f});
	{
		const float weights[16] = {.1672f, .2569f, .1939f, .1745f,
					   .0921f, .025f,  .008f,  .006f,
					   .004f,  .003f,  .003f,  .003f,
					   .003f,  .002f,  .002f,  .002f};
		char text[17] = {0};
		for (int k = 0; k < 16; k++) {
			text[k] = '*';
			chain(m, 0, 0, .1f,
			      &(struct md_template){text, weights[k]});
		}
	}
	for (int k = 0; k < m->count; k++) {
		float sum = 0;
		for (int p = m->nodes[k].outgoing; p >= 0; p = m->edges[p].next)
			sum += m->edges[p].weight;
		for (int p = m->nodes[k].outgoing; p >= 0;
		     p = m->edges[p].next) {
			struct md_edge *link = &m->edges[p];
			link->weight = logf(link->weight / sum);
			if (!m->nodes[link->to].selector)
				m->nodes[link->to].weight = link->weight;
		}
	}
	return !m->failed;
}

void md_word_reset(const struct morsedec_decoder *d, struct md_word *w)
{
	for (int i = 0; i < d->model->count; i++)
		w->states[i] = (struct md_word_state){-10000, 0, 0};
	w->states[0].score = 0;
	w->count = 0;
	w->saved = -1;
	memset(w->slots, 0, sizeof(w->slots));
}

static float emission(int expected, int input)
{
	if (input >= 'a' && input <= 'z')
		input -= 'a' - 'A';
	if (expected == input)
		return 0;
	if (input >= '0' && input <= '9')
		return expected == '#'	 ? -2.303f
		       : expected == '*' ? -5.303f
					 : -10000;
	if (input >= 'A' && input <= 'Z')
		return expected == '@'	 ? -3.258f
		       : expected == '*' ? -3.738f
					 : -10000;
	/* Generic words may include attached punctuation, such as CPY?. */
	return expected == '+' ? -2.303f : expected == '*' ? -5.303f : -10000;
}

static void install(const struct morsedec_decoder *d, struct md_word *w, int ch)
{
	struct md_word_state saved = w->states[w->saved];
	int n = w->count, length = saved.length;
	w->saved = -1;
	if (length <= 0 || length > n || ch == w->chars[n - length].character)
		return;
	for (int i = 0; i < d->model->count; i++)
		w->states[i].score = -10000;
	w->states[0] = saved;
	w->states[0].score = 0;
	w->slots[n] = w->slots[n - length] = saved;
}

void md_word_feed(const struct morsedec_decoder *d, struct md_channel *c,
		  const struct morsedec_character *ch)
{
	const struct md_model *m = d->model;
	struct md_word *w = &c->word;
	if (w->saved >= 0)
		install(d, w, ch->character);
	if (w->count == 26) {
		memmove(w->chars, w->chars + 1, 25 * sizeof(*w->chars));
		memmove(w->slots, w->slots + 1, 26 * sizeof(*w->slots));
		w->count--;
	}
	w->chars[w->count++] = *ch;
	w->states[0].length = 0;
	for (int i = m->count - 1; i > 0; i--) {
		const struct md_node *n = &m->nodes[i];
		if (n->selector)
			continue;
		w->states[i] = w->states[n->parent];
		w->states[i].score +=
		    n->weight + emission(n->code, ch->character);
		w->states[i].length++;
		w->states[i].kind = n->kind;
	}
	/* Selector creation order is a topological order of the epsilon graph;
	 * root comes last to close each possible word. */
	for (int k = 1; k <= m->count; k++) {
		int target = k == m->count ? 0 : k;
		struct md_word_state best = {MD_NEG, 0, 0};
		if (!m->nodes[target].selector)
			continue;
		for (int p = 0; p < m->edge_count; p++) {
			const struct md_edge *link = &m->edges[p];
			if (link->to == target &&
			    w->states[link->from].score + link->weight >
				best.score) {
				best = w->states[link->from];
				best.score += link->weight;
			}
		}
		w->states[target] = best;
	}
	{
		float x = ch->word_space_probability;
		float p = 0.5f * (tanhf(md_clamp(6 * (x - .5f), -5, 5)) + 1);
		w->states[0].score +=
		    x == 2   ? 100
		    : x == 0 ? -10000
			     : logf(p + 1e-5f) - logf(1 - p + 1e-5f);
	}
	{
		int n = w->count, repeat = 0;
		for (int len = 4; len <= n / 2 && len <= 10; len++) {
			int same = 1;
			for (int j = 1; j <= len; j++)
				if (w->chars[n - j].character !=
				    w->chars[n - len - j].character)
					same = 0;
			if (same) {
				repeat = len;
				break;
			}
		}
		if (repeat && 2 * repeat < n &&
		    w->chars[n - 2 * repeat - 1].character ==
			w->chars[n - 1].character)
			repeat = 0;
		for (int j = 2; j <= repeat; j++)
			if (w->chars[n - j].word_end &&
			    w->chars[n - repeat - j].word_end)
				repeat = 0;
		if (repeat) {
			float top = -10000;
			for (int p = 0; p < m->edge_count; p++) {
				const struct md_edge *link = &m->edges[p];
				struct md_word_state st = w->states[link->from];
				if (link->to == 0 && st.length == repeat &&
				    st.kind != 2 && st.kind != 3 &&
				    st.score + link->weight > top) {
					top = st.score + link->weight;
					w->saved = link->from;
				}
			}
			if (w->saved >= 0 && ch->word_space_probability == 2)
				install(d, w, 0);
		}
	}
	w->slots[w->count - 1] = w->states[0];
}

void md_word_emit(const struct morsedec_decoder *d, struct md_channel *c,
		  int flush)
{
	struct md_word *w = &c->word;
	struct md_word_state best;
	if (!w->count)
		return;
	if (flush) {
		w->chars[w->count - 1].word_space_probability = 2;
		best = w->states[0];
	} else {
		best = (struct md_word_state){MD_NEG, 1, 0};
		for (int i = 1; i < d->model->count; i++)
			if (!d->model->nodes[i].selector &&
			    w->states[i].score > best.score)
				best = w->states[i];
	}
	{
		int pos = w->count - 1;
		struct md_word_state current = best;
		while (pos >= 0) {
			int length = (int)md_clamp(current.length, 1, pos + 1);
			int start = pos - length + 1;
			for (int j = pos; j >= start; j--) {
				w->chars[j].word_type =
				    (enum morsedec_word_type)current.kind;
				w->chars[j].word_end =
				    j == pos
					? (w->chars[j].word_space_probability ==
						   2
					       ? 2
					       : 1)
					: 0;
			}
			pos = start - 1;
			if (pos >= 0)
				current = w->slots[pos];
		}
	}
	md_message_merge(&c->message, w->chars, (size_t)w->count);
	c->changed = 1;
	/* The root is a live hypothesis for starting the next word. Keep its
	 * score on the same scale as continuing-word hypotheses. */
	for (int i = 0; i < d->model->count; i++)
		w->states[i].score -= best.score;
	if (flush)
		md_word_reset(d, w);
}

static void lattice_offer(struct md_lattice_hyp *bank, uint8_t *count,
			  struct md_lattice_hyp candidate)
{
	if (!md_finite(candidate.score) || candidate.score <= MD_NEG / 2)
		return;
	for (int i = 0; i < *count; i++) {
		if (bank[i].model_node != candidate.model_node ||
		    bank[i].length != candidate.length)
			continue;
		if (bank[i].score >= candidate.score)
			return;
		for (int j = i; j + 1 < *count; j++)
			bank[j] = bank[j + 1];
		(*count)--;
		break;
	}
	int pos = 0;
	while (pos < *count && bank[pos].score >= candidate.score)
		pos++;
	if (pos == MD_LATTICE_WORD_BEAM)
		return;
	int end = *count < MD_LATTICE_WORD_BEAM ? *count : *count - 1;
	for (int i = end; i > pos; i--)
		bank[i] = bank[i - 1];
	bank[pos] = candidate;
	if (*count < MD_LATTICE_WORD_BEAM)
		(*count)++;
}

static float lattice_boundary(float x, int close)
{
	if (x == 2)
		return close ? 0 : -10000;
	if (x == 0)
		return close ? -10000 : 0;
	float p = .5f * (tanhf(md_clamp(6 * (x - .5f), -5, 5)) + 1);
	return logf((close ? p : 1 - p) + 1e-5f) - logf(1 + 2e-5f);
}

/* The selector graph is acyclic in creation order, with root closed last.
 * Propagate only epsilon arcs; consuming arcs are handled one byte at a time.
 */
static void lattice_epsilon(const struct md_model *m, float *closure, int from,
			    float score)
{
	int edge_id = m->nodes[from].outgoing;
	for (int steps = 0; edge_id >= 0 && steps < MD_EDGES; steps++) {
		const struct md_edge *e = &m->edges[edge_id];
		int selector = m->nodes[e->to].selector;
		if (selector && score + e->weight > closure[selector - 1])
			closure[selector - 1] = score + e->weight;
		edge_id = e->next;
	}
}

static void lattice_byte(const struct morsedec_decoder *d,
			 struct md_lattice_work *work,
			 struct md_lattice_hyp base, unsigned char ch,
			 float boundary, struct md_lattice_hyp *out,
			 uint8_t *count)
{
	const struct md_model *m = d->model;
	int edge_id = m->nodes[base.model_node].outgoing;
	for (int steps = 0; edge_id >= 0 && steps < MD_EDGES; steps++) {
		const struct md_edge *edge = &m->edges[edge_id];
		const struct md_node *n = &m->nodes[edge->to];
		edge_id = edge->next;
		if (n->selector || base.length == MD_LATTICE_MAX_DEPTH)
			continue;
		float cost = emission(n->code, ch);
		if (cost <= -10000)
			continue;
		struct md_lattice_hyp next = base;
		next.score += edge->weight + cost;
		next.model_node = (uint16_t)edge->to;
		next.length++;
		next.kind = (uint8_t)n->kind;
		float continuation = lattice_boundary(boundary, 0);
		next.score += continuation;
		lattice_offer(out, count, next);
		next.score -= continuation;
		for (int i = 0; i < m->selector_count; i++)
			work->closure[i] = MD_NEG;
		lattice_epsilon(m, work->closure, edge->to, next.score);
		for (int i = 1; i < m->selector_count; i++)
			if (work->closure[i] > MD_NEG / 2)
				lattice_epsilon(m, work->closure,
						m->selectors[i],
						work->closure[i]);
		for (int i = 0; i < m->selector_count; i++) {
			if (work->closure[i] <= MD_NEG / 2)
				continue;
			struct md_lattice_hyp closed = next;
			closed.model_node = m->selectors[i];
			closed.score = work->closure[i];
			if (!i) {
				closed.score += lattice_boundary(boundary, 1);
				closed.length = 0;
			}
			lattice_offer(out, count, closed);
		}
	}
}

/* Beam pruning may remove every grammar continuation. Capacity handling must
 * still make progress: use a bounded acoustic Viterbi pass as a last resort.
 */
static int lattice_acoustic(struct md_channel *c, int require_text)
{
	struct md_lattice_work *work = &c->lattice_work;
	const struct md_lattice *lattice = &c->lattice;
	for (int i = 0; i < lattice->node_count; i++)
		work->hypotheses[i][0] =
		    (struct md_lattice_hyp){.score = i ? MD_NEG : 0,
					    .previous = MD_LATTICE_NONE,
					    .edge = MD_LATTICE_NONE};
	int best = -1;
	float score = MD_NEG;
	for (int i = 0; i < lattice->node_count; i++) {
		const struct md_lattice_node *node = &lattice->nodes[i];
		const struct md_lattice_hyp *base = &work->hypotheses[i][0];
		if (base->score <= MD_NEG / 2)
			continue;
		if ((node->flags & MD_LATTICE_NODE_FINAL) &&
		    (!require_text || i) &&
		    base->score + node->final_score > score) {
			score = base->score + node->final_score;
			best = i * MD_LATTICE_WORD_BEAM;
		}
		uint16_t edge_id = node->first_edge;
		for (int k = 0;
		     edge_id != MD_LATTICE_NONE && k < MD_LATTICE_MAX_EDGES;
		     k++) {
			const struct md_lattice_edge *edge =
			    &lattice->edges[edge_id];
			struct md_lattice_hyp *to =
			    &work->hypotheses[edge->to_node][0];
			if (base->score + edge->acoustic_score > to->score) {
				to->score = base->score + edge->acoustic_score;
				to->previous =
				    (uint16_t)(i * MD_LATTICE_WORD_BEAM);
				to->edge = edge_id;
			}
			edge_id = edge->next_edge;
		}
	}
	return best;
}

int md_word_rescore(const struct morsedec_decoder *d, struct md_channel *c,
		    int require_text)
{
	const struct md_lattice *lattice = &c->lattice;
	struct md_lattice_work *work = &c->lattice_work;
	memset(work->counts, 0, sizeof(work->counts));
	work->path_count = 0;
	for (int i = 0; i < d->model->count; i++) {
		const struct md_word_state *state = &c->word.states[i];
		if (state->score <= -9999 ||
		    (i && (state->length < 0 ||
			   state->length > MD_LATTICE_MAX_DEPTH)))
			continue;
		struct md_lattice_hyp seed = {
		    .score = state->score,
		    .model_node = (uint16_t)i,
		    .previous = MD_LATTICE_NONE,
		    .edge = MD_LATTICE_NONE,
		    .length = i ? (uint8_t)state->length : 0,
		    .kind = (uint8_t)state->kind};
		lattice_offer(work->hypotheses[0], &work->counts[0], seed);
	}
	int best = -1;
	float best_score = MD_NEG;
	for (int node_id = 0; node_id < lattice->node_count; node_id++) {
		const struct md_lattice_node *node = &lattice->nodes[node_id];
		for (int slot = 0; slot < work->counts[node_id]; slot++) {
			struct md_lattice_hyp base =
			    work->hypotheses[node_id][slot];
			if ((node->flags & MD_LATTICE_NODE_FINAL) &&
			    (!require_text || node_id) &&
			    base.score + node->final_score > best_score) {
				best_score = base.score + node->final_score;
				best = node_id * MD_LATTICE_WORD_BEAM + slot;
			}
			uint16_t edge_id = node->first_edge;
			for (int k = 0; edge_id != MD_LATTICE_NONE &&
					k < MD_LATTICE_MAX_EDGES;
			     k++) {
				const struct md_lattice_edge *edge =
				    &lattice->edges[edge_id];
				const char *text =
				    md_wfst_text(edge->character_label);
				struct md_lattice_hyp input = base;
				input.score += edge->acoustic_score;
				input.previous =
				    (uint16_t)(node_id * MD_LATTICE_WORD_BEAM +
					       slot);
				input.edge = edge_id;
				if (text[1]) {
					uint8_t count = 0;
					lattice_byte(d, work, input,
						     (unsigned char)text[0],
						     .001f, work->scratch,
						     &count);
					for (int j = 0; j < count; j++)
						lattice_byte(
						    d, work, work->scratch[j],
						    (unsigned char)text[1],
						    edge->word_space_probability,
						    work->hypotheses
							[edge->to_node],
						    &work->counts
							 [edge->to_node]);
				} else {
					lattice_byte(
					    d, work, input,
					    (unsigned char)text[0],
					    edge->word_space_probability,
					    work->hypotheses[edge->to_node],
					    &work->counts[edge->to_node]);
				}
				edge_id = edge->next_edge;
			}
		}
	}
	if (best < 0)
		best = lattice_acoustic(c, require_text);
	for (int steps = 0; best >= 0 && steps < MD_LATTICE_MAX_DEPTH;
	     steps++) {
		const struct md_lattice_hyp *hyp =
		    &work->hypotheses[best / MD_LATTICE_WORD_BEAM]
				     [best % MD_LATTICE_WORD_BEAM];
		if (hyp->edge == MD_LATTICE_NONE)
			break;
		work->path[work->path_count++] = hyp->edge;
		best = hyp->previous == MD_LATTICE_NONE ? -1 : hyp->previous;
	}
	for (int i = 0; i < work->path_count / 2; i++) {
		uint16_t tmp = work->path[i];
		work->path[i] = work->path[work->path_count - i - 1];
		work->path[work->path_count - i - 1] = tmp;
	}
	return work->path_count;
}

void md_word_lattice_commit(const struct morsedec_decoder *d,
			    struct md_channel *c, int edges)
{
	for (int i = 0; i < edges && i < c->lattice_work.path_count; i++) {
		const struct md_lattice_edge *edge =
		    &c->lattice.edges[c->lattice_work.path[i]];
		const char *text = md_wfst_text(edge->character_label);
		for (int j = 0; j < 2 && text[j]; j++) {
			struct morsedec_character ch = {0};
			ch.character = (unsigned char)text[j];
			ch.ordinal = (edge->end_frame - 1) * 256 + (uint64_t)j;
			ch.low_snr = edge->low_snr;
			ch.word_space_probability =
			    text[j + 1] ? .001f : edge->word_space_probability;
			md_word_feed(d, c, &ch);
		}
		md_word_emit(d, c, 0);
	}
}
