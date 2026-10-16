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
	m->count++;
	m->nodes[i].selector = selector;
	m->nodes[i].code = code;
	m->nodes[i].kind = m->kind;
	m->nodes[i].outgoing = -1;
	return i;
}

static void edge(struct md_model *m, int from, int to, double weight)
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

static int successor(struct md_model *m, int from, int code, double weight)
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

static void chain(struct md_model *m, int from, int to, double scale,
		  const struct md_template *entry)
{
	double w = scale * entry->weight;
	for (const char *p = entry->text; *p; p++)
		from = successor(m, from, (unsigned char)*p, w);
	edge(m, from, to, pow(w, 0.85));
}

static int level(struct md_model *m, int from, double scale,
		 const struct md_template *table, size_t count)
{
	int accept = node(m, 1, 0);
	for (size_t i = 0; i < count; i++)
		chain(m, from, accept, scale, &table[i]);
	return accept;
}
#define MD_COUNT(a) (sizeof(a) / sizeof((a)[0]))

static int core(struct md_model *m, int from, double scale)
{
	static const struct md_template l1[] = {
	    {"@", .199743}, {"@@", .74399}, {"#@", .054979}, {"#@@", .001288}};
	static const struct md_template l2[] = {{"#", .939686},
						{"##", .052986},
						{"###", .00501},
						{"####", .002317}};
	static const struct md_template l3[] = {
	    {"@", .021387},    {"@@", .392918},	   {"@@@", .571806},
	    {"@@@@", .009582}, {"@@@@@", .002967}, {"@@@@@@", .00134}};
	from = level(m, from, scale, l1, MD_COUNT(l1));
	from = level(m, from, scale, l2, MD_COUNT(l2));
	return level(m, from, scale, l3, MD_COUNT(l3));
}

int md_model_init(struct md_model *m)
{
	static const struct md_template s1[] = {
	    {"#@@", .002199},  {"@#@", .008175}, {"@##", .014827},
	    {"@@#@", .018811}, {"#@", .045966},	 {"#@#", .054554},
	    {"@", .055701},    {"@#", .08404},	 {"@@", .219683},
	    {"@@#", .496044}};
	static const struct md_template s2[] = {
	    {"FJL", .000367},  {"G", .00078},	 {"E", .001056},
	    {"QRPP", .001446}, {"ANT", .001675}, {"T", .002891},
	    {"J", .004314},    {"LGT", .005002}, {"AM", .007022},
	    {"A", .007},       {"LH", .015627},	 {"MM", .049772},
	    {"QRP", .067441},  {"M", .117189},	 {"#", .272562},
	    {"P", .439088}};
	static const struct md_template reports[] = {
	    {"5NN", .8},   {"HNN", .05},  {"SNN", .05}, {"SENN", .05},
	    {"ESNN", .05}, {"IINN", .05}, {"VNN", .05}, {"FNN", .05},
	    {"LNN", .05},  {"BNN", .05},  {"3#N", .05}, {"4#N", .05},
	    {"5#N", .05},  {"3#9", .05},  {"4#9", .05}, {"5#9", .05},
	    {"H#N", .05}};
	static const struct md_template prosigns[] = {
	    {"DE", .45},  {"TEST", .1}, {"SS", .05},  {"UP", .05},
	    {"UP1", .05}, {"UP2", .05}, {"QRZ", .05}, {"QRZ?", .05},
	    {"FD", .05},  {"NA", .05},	{"CQ", .05}};
	const double a = .806858, b = .076643, c = .002267, d = .075624;
	const double e = .011884, f = .000002, g = .026237, h = .000485,
		     i = .000002;
	int cs, suffix, prefix, split, portable;
	memset(m, 0, sizeof(*m));
	node(m, 1, 0);
	m->kind = MORSEDEC_WORD_CALLSIGN;
	cs = core(m, 0, .6 * (a + b + c + g + h + i));
	edge(m, cs, 0, a);
	suffix = successor(m, cs, '/', .6 * (g + h + i));
	suffix = level(m, suffix, .6 * (g + h + i), s1, MD_COUNT(s1));
	edge(m, suffix, 0, g);
	prefix = level(m, 0, .6 * (d + e + f), s1, MD_COUNT(s1));
	prefix = successor(m, prefix, '/', .6 * (d + e + f));
	prefix = core(m, prefix, .6 * (d + e + f));
	edge(m, prefix, 0, d);
	split = node(m, 1, 0);
	edge(m, cs, split, .6 * (b + c));
	edge(m, suffix, split, .6 * (h + i));
	edge(m, prefix, split, .6 * (e + f));
	portable = successor(m, split, '/', .6 * (b + c + h + i + e + f));
	portable =
	    level(m, portable, .6 * (b + c + h + i + e + f), s2, MD_COUNT(s2));
	edge(m, portable, 0, .6 * (b + h + e));
	portable = successor(m, portable, '/', .6 * (c + i + f));
	portable = level(m, portable, .6 * (c + i + f), s2, MD_COUNT(s2));
	edge(m, portable, 0, .6 * (c + i + f));
	m->kind = MORSEDEC_WORD_DICTIONARY;
	for (size_t k = 0; k < MD_COUNT(dictionary); k++)
		chain(m, 0, 0, .01, &dictionary[k]);
	m->kind = MORSEDEC_WORD_REPORT;
	for (size_t k = 0; k < MD_COUNT(reports); k++)
		chain(m, 0, 0, .6, &reports[k]);
	m->kind = MORSEDEC_WORD_PROSIGN;
	for (size_t k = 0; k < MD_COUNT(prosigns); k++)
		chain(m, 0, 0, .01, &prosigns[k]);
	m->kind = MORSEDEC_WORD_QRL;
	chain(m, 0, 0, .01, &(struct md_template){"QRL?", 1});
	chain(m, 0, 0, .01, &(struct md_template){"QRL", 1});
	m->kind = MORSEDEC_WORD_GENERIC;
	chain(m, 0, 0, .1, &(struct md_template){"+", .12});
	chain(m, 0, 0, .1, &(struct md_template){"##", .03});
	chain(m, 0, 0, .1, &(struct md_template){"###", .1});
	{
		const double weights[16] = {
		    .1672, .2569, .1939, .1745, .0921, .025, .008, .006,
		    .004,  .003,  .003,	 .003,	.003,  .002, .002, .002};
		char text[17] = {0};
		for (int k = 0; k < 16; k++) {
			text[k] = '*';
			chain(m, 0, 0, .1,
			      &(struct md_template){text, weights[k]});
		}
	}
	for (int k = 0; k < m->count; k++) {
		double sum = 0;
		for (int p = m->nodes[k].outgoing; p >= 0; p = m->edges[p].next)
			sum += m->edges[p].weight;
		for (int p = m->nodes[k].outgoing; p >= 0;
		     p = m->edges[p].next) {
			struct md_edge *link = &m->edges[p];
			link->weight = log(link->weight / sum);
			if (!m->nodes[link->to].selector)
				m->nodes[link->to].weight = link->weight;
		}
	}
	return !m->failed;
}

void md_word_reset(struct morsedec_decoder *d, struct md_word *w)
{
	for (int i = 0; i < d->model->count; i++)
		w->states[i] = (struct md_word_state){-10000, 0, 0};
	w->states[0].score = 0;
	w->count = 0;
	w->saved = -1;
	memset(w->slots, 0, sizeof(w->slots));
}

static double emission(int expected, int input)
{
	if (input >= 'a' && input <= 'z')
		input -= 'a' - 'A';
	if (expected == input)
		return 0;
	if (input >= '0' && input <= '9')
		return expected == '#'	 ? -2.303
		       : expected == '*' ? -5.303
					 : -10000;
	if (input >= 'A' && input <= 'Z')
		return expected == '@'	 ? -3.258
		       : expected == '*' ? -3.738
					 : -10000;
	return expected == '+' ? -2.303 : -10000;
}

static void install(struct morsedec_decoder *d, struct md_word *w, int ch)
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

void md_word_feed(struct morsedec_decoder *d, struct md_channel *c,
		  const struct morsedec_character *ch)
{
	struct md_model *m = d->model;
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
		struct md_node *n = &m->nodes[i];
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
		struct md_word_state best = {-1e100, 0, 0};
		if (!m->nodes[target].selector)
			continue;
		for (int p = 0; p < m->edge_count; p++) {
			struct md_edge *link = &m->edges[p];
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
		double x = ch->word_space_probability;
		double p = 0.5 * (tanh(md_clamp(6 * (x - .5), -5, 5)) + 1);
		w->states[0].score += x == 2 ? 100
				      : x == 0
					  ? -10000
					  : log(p + 1e-5) - log(1 - p + 1e-5);
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
			double top = -10000;
			for (int p = 0; p < m->edge_count; p++) {
				struct md_edge *link = &m->edges[p];
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

void md_word_emit(struct morsedec_decoder *d, struct md_channel *c, int flush)
{
	struct md_word *w = &c->word;
	struct md_word_state best;
	if (!w->count)
		return;
	if (flush) {
		w->chars[w->count - 1].word_space_probability = 2;
		best = w->states[0];
	} else {
		best = (struct md_word_state){-1e100, 1, 0};
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
	for (int i = 1; i < d->model->count; i++)
		w->states[i].score -= best.score;
	if (flush)
		md_word_reset(d, w);
}
