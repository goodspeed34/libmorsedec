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
#include "codes.h"

const char *md_lookup(const char *pattern)
{
	for (size_t i = 0; i < sizeof(md_codes) / sizeof(md_codes[0]); i++)
		if (!strcmp(pattern, md_codes[i].pattern))
			return md_codes[i].text;
	return NULL;
}

static int split_decode(const char *pattern, char *out, size_t capacity)
{
	const char *hit = md_lookup(pattern);
	size_t n = strlen(pattern), mid = n / 2;
	if (hit) {
		if (strlen(hit) + 1 > capacity)
			return 0;
		strcpy(out, hit);
		return 1;
	}
	if (n > 60 || mid < 2)
		return 0;
	for (size_t k = 0; k <= mid - 2; k++) {
		for (int side = 0; side < 2; side++) {
			size_t p = side ? mid + k - 1 : mid - k - 1;
			char left[64], right[64], a[128], b[128];
			if (p + 1 >= n || pattern[p] != '-' ||
			    pattern[p + 1] != '.')
				continue;
			memcpy(left, pattern, p + 1);
			left[p + 1] = 0;
			strcpy(right, pattern + p + 1);
			if (split_decode(left, a, sizeof(a)) &&
			    split_decode(right, b, sizeof(b)) &&
			    strlen(a) + strlen(b) + 1 <= capacity) {
				strcpy(out, a);
				strcat(out, b);
				return 1;
			}
			/* The first matching delimiter determines the split. */
			return 0;
		}
	}
	return 0;
}

void md_decode_pattern(const char *pattern, char *out, size_t capacity)
{
	if (!capacity)
		return;
	if (!split_decode(pattern, out, capacity)) {
		out[0] = capacity > 1 ? '*' : 0;
		if (capacity > 1)
			out[1] = 0;
	}
}

static void decode_run(const struct morsedec_decoder *d, struct md_channel *c,
		       const struct md_element *gap, int flush)
{
	char pattern[64], text[128];
	int n = 0, low = 0;
	uint64_t ordinal = 0;

	for (int i = 0; i < c->element_count; i++) {
		const struct md_element *e = &c->character_elements[i];
		low |= e->low;
		if (e->kind < 2) {
			pattern[n++] = e->kind ? '-' : '.';
			ordinal = e->ordinal;
		}
	}
	c->element_count = 0;
	if (!n)
		return;
	pattern[n] = 0;
	md_decode_pattern(pattern, text, sizeof(text));
	for (size_t i = 0; text[i]; i++) {
		struct morsedec_character ch;
		memset(&ch, 0, sizeof(ch));
		ch.character = (unsigned char)text[i];
		ch.ordinal = ordinal * 256 + i;
		ch.low_snr = low || (gap && gap->low);
		ch.word_space_probability = 0.001;
		if (!text[i + 1] && gap)
			ch.word_space_probability =
			    gap->scores[4] /
			    (gap->scores[3] + gap->scores[4] + 1e-12);
		if (!text[i + 1] &&
		    (flush || (gap && gap->duration * d->tick_scale < -150)))
			ch.word_space_probability = 2;
		md_word_feed(d, c, &ch);
	}
	md_word_emit(d, c, flush);
}

void md_character_feed(const struct morsedec_decoder *d, struct md_channel *c,
		       const struct md_element *e, int flush)
{
	if (e && e->kind >= 3) {
		decode_run(d, c, e, flush);
	} else if (e) {
		if (c->element_count >= 60)
			decode_run(d, c, NULL, 0);
		c->character_elements[c->element_count++] = *e;
	}
	if (flush) {
		decode_run(d, c, NULL, 1);
		md_word_emit(d, c, 1);
	}
}
