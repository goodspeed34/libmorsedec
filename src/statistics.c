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

static const double initial_mean[5] = {1.62, 2.70, 1.50, 2.72, 3.56};
static const double initial_variance[5] = {0.0567, 0.0509, 0.0710, 0.0779,
					   0.0697};
static const double weight[5] = {0.58, 0.42, 0.65, 0.23, 0.12};
static const double prior[5][5] = {{0.0567, 0.0492, 0.0454, 0.0474, 0.0367},
				   {0.0492, 0.0509, 0.0508, 0.0502, 0.0385},
				   {0.0454, 0.0508, 0.0710, 0.0602, 0.0464},
				   {0.0474, 0.0502, 0.0602, 0.0779, 0.0612},
				   {0.0367, 0.0385, 0.0464, 0.0612, 0.0697}};
static const double inverse[5][5] = {
    {118.485229, -134.391098, 23.396200, -2.068530, -1.913050},
    {-134.391098, 231.483887, -65.243530, -20.121161, 3.999120},
    {23.396200, -65.243530, 64.486313, -22.355499, 0.419330},
    {-2.068530, -20.121161, -22.355499, 74.339058, -38.187592},
    {-1.913050, 3.999120, 0.419330, -38.187592, 46.396938}};
static const double gap_prior[8][8] = {
    {0, 0, 0, 0, 0.00027, 0.00014, 0, 0},
    {0, 0, 0, 0.00027, 0.00041, 0.00082, 0, 0},
    {0, 0.00041, 0.00123, 0.00519, 0.00505, 0.00191, 0, 0},
    {0, 0.00027, 0.00178, 0.01939, 0.04984, 0.01065, 0, 0},
    {0, 0.00096, 0.04438, 0.20306, 0.08111, 0, 0, 0},
    {0, 0, 0.01557, 0.22682, 0.22026, 0, 0, 0},
    {0, 0, 0, 0.03482, 0.07524, 0, 0, 0},
    {0.00014, 0, 0, 0, 0, 0, 0, 0}};

void md_stats_init(const struct morsedec_decoder *d, struct md_statistics *s)
{
	double shift =
	    d->config.initial_wpm > 0 ? log(23.16 / d->config.initial_wpm) : 0;

	memset(s, 0, sizeof(*s));
	for (int i = 0; i < 5; i++) {
		s->mean[i] = s->prior_mean[i] = initial_mean[i] + shift;
		s->variance[i] = initial_variance[i];
	}
	memcpy(s->covariance, prior, sizeof(prior));
	s->learning = s->dirty = 1;
}

double md_score_x(const struct md_statistics *s, int kind, double x,
		  double duration)
{
	double delta = x - s->mean[kind];
	double v = fmax(s->variance[kind], 1e-6);
	double t = weight[kind] * exp(-delta * delta / (2 * v)) /
		   sqrt(2 * MD_PI * v) / duration;
	double lo, hi;

	if (kind < 3)
		return weight[kind] * t;
	if (kind == 3) {
		lo = s->mean[2] + 3 * sqrt(fmax(0, s->covariance[2][2]));
		hi = s->mean[3] + 3 * sqrt(fmax(0, s->covariance[4][4]));
		if (lo < x && x < hi)
			t += 0.0001 / (hi - lo);
	} else {
		lo = s->mean[3] + 3 * sqrt(fmax(0, s->covariance[3][3]));
		if (lo < x && x < 300)
			t += 0.1 / (300 - lo);
	}
	return t;
}

double md_score(const struct md_statistics *s, int kind, double duration)
{
	return md_score_x(s, kind, log(fmax(duration, 1e-9)), duration);
}

void md_classify(const struct md_statistics *s, struct md_element *e,
		 double scale)
{
	int lo = e->duration > 0 ? 0 : 2;
	int hi = e->duration > 0 ? 1 : 4;
	double dur = fabs((double)e->duration) * scale;

	memset(e->scores, 0, sizeof(e->scores));
	e->kind = lo;
	for (int i = lo; i <= hi; i++) {
		e->scores[i] = md_score(s, i, dur);
		if (i == 4)
			e->scores[i] = fmax(e->scores[i], 0.001);
		/* Ties prefer the longer duration class. */
		if (e->scores[i] >= e->scores[e->kind])
			e->kind = i;
	}
}

static void sort_seven(double *x)
{
	for (int i = 1; i < 7; i++) {
		double value = x[i];
		int j = i;
		while (j && x[j - 1] > value) {
			x[j] = x[j - 1];
			j--;
		}
		x[j] = value;
	}
}

static double group_mean(const double *x, int begin, int end)
{
	double sum = 0;
	for (int i = begin; i < end; i++)
		sum += x[i];
	return end > begin ? sum / (end - begin) : 0;
}

static double group_error(const double *x, int begin, int end, double mean,
			  double variance)
{
	double error = 0;

	for (int i = begin; i < end; i++) {
		double delta = x[i] - mean;
		error += delta * delta;
	}
	return error / variance;
}

static void update_class(struct md_statistics *s, int kind, double x)
{
	double p[5][5], gain[5];
	double delta = x - s->mean[kind];
	double alpha = s->learning ? 0.5 : 0.2;
	double mu = fmax(fabs(s->mean[kind]), 0.1);
	double lo = 0.15 / mu, hi = (kind == 4 ? 1.0 : 0.5) / mu;
	double inv;

	s->variance[kind] =
	    md_clamp64((1 - alpha) * s->variance[kind] + alpha * delta * delta,
		       lo * lo, hi * hi);
	for (int i = 0; i < 5; i++)
		for (int j = 0; j < 5; j++)
			p[i][j] = s->covariance[i][j] + 0.003 * prior[i][j];
	inv = 1 / (s->variance[kind] + p[kind][kind]);
	for (int i = 0; i < 5; i++)
		for (int j = 0; j < 5; j++)
			s->covariance[i][j] =
			    p[i][j] - p[i][kind] * p[kind][j] * inv;
	for (int i = 0; i < 5; i++)
		gain[i] = s->covariance[i][kind] / s->variance[kind];
	for (int i = 0; i < 5; i++)
		s->mean[i] += gain[i] * delta;
}

static void reestimate(struct md_statistics *s, double scale)
{
	static const int choose[8] = {1, 7, 21, 35, 35, 21, 7, 1};
	double marks[7], gaps[7], accum[5] = {0}, old[5], total = 0;
	int nm = 0, ng = 0;

	for (int i = 0; i < 14; i++) {
		double x = log(fabs((double)s->window[i].duration) * scale);
		if (s->window[i].duration > 0)
			marks[nm++] = x;
		else
			gaps[ng++] = x;
	}
	if (nm != 7 || ng != 7)
		return;
	/* Acquisition and inter-transmission silence must not determine the
	 * spacing model. Substitute the longest observed ordinary gap. */
	{
		double limit = log(60), longest = s->mean[4];
		int found = 0;

		for (int i = 0; i < 7; i++)
			if (gaps[i] < limit && (!found || gaps[i] > longest)) {
				longest = gaps[i];
				found = 1;
			}
		for (int i = 0; i < 7; i++)
			if (gaps[i] >= limit)
				gaps[i] = longest;
	}
	sort_seven(marks);
	sort_seven(gaps);
	for (int a = 0; a <= 7; a++) {
		for (int k = 0; k <= 4; k++) {
			for (int c = 0; c <= 5 - k; c++) {
				int b = 7 - c - k, filled = 0;
				double candidate[5], ref = 0, mah = 0, error, w;

				candidate[0] = group_mean(marks, 0, a);
				candidate[1] = group_mean(marks, a, 7);
				candidate[2] = group_mean(gaps, 0, b);
				candidate[3] = group_mean(gaps, b, b + c);
				candidate[4] = group_mean(gaps, b + c, 7);
				for (int i = 0; i < 5; i++) {
					if (candidate[i] > 0) {
						ref +=
						    candidate[i] -
						    ((i == 1 || i == 3) ? log(3)
						     : i == 4		? log(8)
									: 0);
						filled++;
					}
				}
				ref = filled ? ref / filled : initial_mean[0];
				for (int i = 0; i < 5; i++)
					if (candidate[i] == 0)
						candidate[i] =
						    ref + ((i == 1 || i == 3)
							       ? log(3)
							   : i == 4 ? log(8)
								    : 0);
				for (int i = 0; i < 5; i++)
					for (int j = 0; j < 5; j++)
						mah += inverse[i][j] *
						       (candidate[i] -
							s->prior_mean[i]) *
						       (candidate[j] -
							s->prior_mean[j]);
				/* A plausible prior alone cannot distinguish
				 * partitions that lump dots and dashes into one
				 * broad class. Include the observed
				 * within-class log-duration likelihood. */
				error =
				    group_error(marks, 0, a, candidate[0],
						initial_variance[0]) +
				    group_error(marks, a, 7, candidate[1],
						initial_variance[1]) +
				    group_error(gaps, 0, b, candidate[2],
						initial_variance[2]) +
				    group_error(gaps, b, b + c, candidate[3],
						initial_variance[3]) +
				    group_error(gaps, b + c, 7, candidate[4],
						initial_variance[4]);
				w = exp(-0.5 * (mah + error)) * choose[a] *
				    pow(0.58, a) * pow(0.42, 7 - a) *
				    (gap_prior[b][c] + 1e-9);
				total += w;
				for (int i = 0; i < 5; i++)
					accum[i] += candidate[i] * w;
			}
		}
	}
	memcpy(old, s->mean, sizeof(old));
	for (int i = 0; i < 5; i++)
		s->mean[i] = total > 1e-200 && isfinite(total)
				 ? accum[i] / total
				 : s->prior_mean[i];
	for (int i = 0; i < 14; i++)
		md_classify(s, &s->window[i], scale);
	memcpy(s->mean, old, sizeof(old));
	for (int i = 0; i < 14; i++) {
		double duration = fabs((double)s->window[i].duration) * scale;
		if (duration < 60)
			update_class(s, s->window[i].kind, log(duration));
	}
	/* Enforce physically ordered mark and gap distributions. */
	s->mean[1] = fmax(s->mean[1], s->mean[0] + log(1.5));
	s->mean[4] = fmax(s->mean[4], s->mean[3] + log(1.5));
}

void md_stats_commit(const struct morsedec_decoder *d, struct md_channel *c,
		     struct md_element *elements, int count, int flush)
{
	for (int i = count - 1; i >= 0; i--)
		md_stats_observe(d, c, &elements[i]);
	if (flush)
		md_stats_release(d, c);
}

void md_stats_release(const struct morsedec_decoder *d, struct md_channel *c)
{
	struct md_statistics *s = &c->stats;
	if (s->learning &&
	    (c->quiet >= md_frames(d, 75) ||
	     s->acquisition_elements == MD_ACQUISITION_ELEMENTS) &&
	    (s->acquisition_marks >= 3 ||
	     (s->acquisition_marks && c->pinned))) {
		s->learning = 0;
		s->trained = s->acquisition_marks >= 3;
	}
}

void md_stats_observe(const struct morsedec_decoder *d, struct md_channel *c,
		      const struct md_element *element)
{
	struct md_statistics *s = &c->stats;
	if (!element->duration || element->ordinal <= s->committed_end)
		return;
	s->committed_end = element->ordinal;
	if (!s->acquisition_marks && !s->count && element->duration < 0)
		return;
	if (element->duration > 0 && s->acquisition_marks < 3)
		s->acquisition_marks++;
	if (s->learning && s->acquisition_elements < MD_ACQUISITION_ELEMENTS)
		s->acquisition_elements++;
	if (!s->count ||
	    (s->window[s->count - 1].duration > 0) != (element->duration > 0))
		s->window[s->count++] = *element;
	if (s->count == 14) {
		reestimate(s, d->tick_scale);
		s->count = 0;
		s->dirty = 1;
		s->learning = 0;
		s->trained = 1;
	}
	md_stats_release(d, c);
}

double md_speed(const struct md_statistics *s)
{
	/* PARIS convention: 1.2 seconds per dot at 1 WPM. */
	return (1.2 / MD_TICK) * 2.49 /
	       (0.58 * exp(s->mean[0]) + 0.42 * exp(s->mean[1]) +
		0.65 * exp(s->mean[2]));
}
