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

static int equal(const struct morsedec_character *a,
		 const struct morsedec_character *b)
{
	return a->character == b->character && a->word_type == b->word_type &&
	       a->ordinal == b->ordinal && a->low_snr == b->low_snr &&
	       a->word_space_probability == b->word_space_probability &&
	       a->word_end == b->word_end;
}

void md_message_merge(struct md_message *m,
		      const struct morsedec_character *batch, size_t count)
{
	size_t overlap = 0, same = 0;
	if (!count)
		return;
	while (overlap < m->count &&
	       m->chars[m->count - overlap - 1].ordinal >= batch[0].ordinal)
		overlap++;
	while (same < overlap && same < count &&
	       equal(&m->chars[m->count - overlap + same], &batch[same]))
		same++;
	m->count -= overlap - same;
	batch += same;
	count -= same;
	while (m->count + count > MORSEDEC_MESSAGE_CAPACITY) {
		size_t shift = m->count < 125 ? m->count : 125;
		if (!shift) {
			batch += count - MORSEDEC_MESSAGE_CAPACITY;
			count = MORSEDEC_MESSAGE_CAPACITY;
			break;
		}
		m->count -= shift;
		memmove(m->chars, m->chars + shift,
			m->count * sizeof(*m->chars));
	}
	memcpy(m->chars + m->count, batch, count * sizeof(*batch));
	m->count += count;
}
