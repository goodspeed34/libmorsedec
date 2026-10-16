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

static void duration_tables(struct morsedec_decoder *d, struct md_channel *c)
{
	struct md_segment *g = &c->seg;
	for (int chain = 0; chain < 2; chain++) {
		double survival = 1;
		int base = chain ? g->on_max : 0;
		int n = chain ? g->off_max : g->on_max;
		for (int i = 1; i <= n; i++) {
			double p = 0, hazard;
			double dur = i * d->tick_scale;
			for (int k = chain ? 2 : 0; k <= (chain ? 4 : 1); k++)
				p +=
				    md_score(&c->stats, k, dur) * d->tick_scale;
			p = fmin(0.99, p);
			hazard = md_clamp(1 - p / fmax(survival, 1e-20), 1e-6,
					  1 - 1e-6);
			g->cont[base + i] = log(hazard);
			g->end[base + i] = log(1 - hazard);
			survival *= hazard;
		}
	}
	c->stats.dirty = 0;
}

void md_segment_init(struct morsedec_decoder *d, struct md_channel *c)
{
	struct md_segment *g = &c->seg;
	memset(g, 0, sizeof(*g));
	g->on_max = md_frames(d, 20);
	g->off_max = md_frames(d, 30);
	g->keep = md_frames(d, 25);
	g->window = g->keep * 2;
	for (int i = 1; i <= g->on_max + g->off_max; i++)
		g->score[i] = MD_NEG;
	g->score[g->on_max + g->off_max] = 0;
	duration_tables(d, c);
}

static int expand(const struct md_segment *g, int state)
{
	if (state == g->on_max)
		return state + g->on_run;
	if (state == -g->off_max)
		return state - g->off_run;
	return state;
}

static int survives(const struct md_segment *g, int state,
		    const struct md_element *latest)
{
	int duration = expand(g, state), pos = g->index + 1;
	while (duration) {
		pos -= abs(duration);
		if (pos < 0)
			return 0;
		duration = duration <= 0 ? g->on[pos] : g->off[pos];
		if ((uint64_t)((int64_t)g->step + pos - g->index) ==
			latest->ordinal &&
		    duration == latest->duration)
			return 1;
		if (pos < g->keep)
			return 0;
	}
	return 0;
}

void md_segment_commit(struct morsedec_decoder *d, struct md_channel *c,
		       struct md_element *elements, int count, int drop)
{
	struct md_segment *g = &c->seg;
	int valid = 0;

	/* Traceback is newest first. Retained context can revisit an old run
	 * after carrier drop; never deliver committed time twice. */
	while (valid < count && elements[valid].ordinal > g->committed_end)
		valid++;
	for (int i = valid - 1; i >= 0; i--) {
		struct md_element *e = &elements[i];
		uint64_t available = e->ordinal - g->committed_end;

		if ((uint64_t)abs(e->duration) > available)
			e->duration =
			    (int)available * (e->duration > 0 ? 1 : -1);
		g->committed_end = e->ordinal;
	}
	md_stats_commit(d, c, elements, valid, drop);
}

void md_segment_step(struct morsedec_decoder *d, struct md_channel *c, int bit,
		     double probability, int drop)
{
	struct md_segment *g = &c->seg;
	int n[2] = {g->on_max, g->off_max};
	int base[2] = {0, g->on_max};
	int *run[2] = {&g->on_run, &g->off_run};
	double observation[2] = {log(fmax(probability, 1e-10)),
				 log(fmax(1 - probability, 1e-10))};
	double best_score[2];
	int best_len[2];

	if (c->stats.dirty)
		duration_tables(d, c);
	g->bits[g->index] = bit;
	for (int k = 0; k < 2; k++) {
		int last = base[k] + n[k];
		best_score[k] = g->score[last] + g->end[last];
		best_len[k] = n[k] + *run[k];
		for (int i = n[k] - 1; i >= 1; i--) {
			double score =
			    g->score[base[k] + i] + g->end[base[k] + i];
			if (score > best_score[k]) {
				best_score[k] = score;
				best_len[k] = i;
			}
		}
	}
	g->on[g->index] = best_len[0];
	g->off[g->index] = -best_len[1];
	for (int k = 0; k < 2; k++) {
		int last = base[k] + n[k];
		double stay = g->score[last] + g->cont[last];
		double advance = g->score[last - 1] + g->cont[last - 1];
		if (stay <= advance) {
			g->score[last] = advance + observation[k];
			*run[k] = 0;
		} else {
			g->score[last] = stay + observation[k];
			/* Saturation keeps arbitrarily long silence safe. */
			if (*run[k] < INT_MAX / 4)
				++*run[k];
		}
		for (int i = n[k] - 1; i >= 2; i--)
			g->score[base[k] + i] = g->score[base[k] + i - 1] +
						g->cont[base[k] + i - 1] +
						observation[k];
	}
	g->score[1] = best_score[1] + observation[0];
	g->score[g->on_max + 1] = best_score[0] + observation[1];
	if (g->index == g->window - 1 || drop) {
		struct md_element out[MD_TIME_MAX];
		int count = 0, best = 1, duration, pos = g->index + 1;
		double top = g->score[1];

		for (int k = 0; k < 2; k++)
			for (int i = 1; i <= n[k]; i++)
				if (g->score[base[k] + i] > top) {
					top = g->score[base[k] + i];
					best = k ? -i : i;
				}
		duration = expand(g, best);
		while (duration) {
			pos -= abs(duration);
			if (pos < 0)
				break;
			duration = duration <= 0 ? g->on[pos] : g->off[pos];
			if (pos < g->keep || drop) {
				struct md_element *e = &out[count++];
				memset(e, 0, sizeof(*e));
				e->duration = duration;
				e->ordinal = (uint64_t)((int64_t)g->step + pos -
							g->index);
				for (int j =
					 (int)fmax(0, pos - abs(duration) + 1);
				     j <= pos; j++)
					e->low |= g->bits[j];
			}
		}
		if (!drop) {
			for (int k = 0; k < 2; k++)
				for (int i = 1; i <= n[k]; i++) {
					int state = k ? -i : i;
					g->score[base[k] + i] =
					    !count ||
						    survives(g, state, &out[0])
						? g->score[base[k] + i] - top
						: MD_NEG;
				}
			memmove(g->on, g->on + g->keep,
				(size_t)g->keep * sizeof(int));
			memmove(g->off, g->off + g->keep,
				(size_t)g->keep * sizeof(int));
			memmove(g->bits, g->bits + g->keep,
				(size_t)g->keep * sizeof(int));
			g->index = g->keep;
		} else {
			for (int i = 1; i <= g->on_max + g->off_max; i++)
				g->score[i] = MD_NEG;
			g->score[best > 0 ? best : g->on_max - best] = 0;
			g->index = 0;
		}
		md_segment_commit(d, c, out, count, drop);
	} else {
		g->index++;
	}
}
