/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * libmorsedec example - decode signed 16-bit Morse audio from stdin.
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
#include "morsedec.h"
#include <stdio.h>

/* Minimal native-endian s16 mono stdin example. Prefer morsedec-audio for
 * portable WAV/FLAC input or little-endian raw files. */
int main(void)
{
	struct morsedec_config config;
	morsedec_decoder_t *decoder;
	int16_t samples[4096];
	char text[MORSEDEC_MESSAGE_CAPACITY * 3 + 1];
	size_t count, required;

	md_config_init(&config);
	config.sample_rate = 48000;
	decoder = md_new(&config);
	if (!decoder) {
		perror("md_new");
		return 1;
	}
	while ((count = fread(samples, sizeof(*samples), 4096, stdin))) {
		if (!md_process_s16(decoder, samples, count)) {
			perror("md_process_s16");
			md_delete(decoder);
			return 1;
		}
	}
	md_flush(decoder);
	if (md_get_text(decoder, 0, text, sizeof(text), &required))
		puts(text);
	md_delete(decoder);
	return ferror(stdin) ? 1 : 0;
}
