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
#ifndef MORSEDEC_AUDIO_COMMON_H
#define MORSEDEC_AUDIO_COMMON_H

#include "morsedec.h"
#include <stdio.h>

struct md_cli_seen {
	uint64_t id;
	char text[MORSEDEC_MESSAGE_CAPACITY * 3 + 1];
};

/* Optional full-screen view; see tui.h. */
struct md_tui;

struct md_cli_reporter {
	struct md_cli_seen *seen;
	size_t capacity;
	int all, json;
	FILE *trace;
	double time_offset;
	struct md_tui *tui;
};

int md_cli_number(const char *text, double *out);
int md_cli_version(const char *program);
int md_cli_live(int argc, char **argv, int iq);
int md_cli_report(morsedec_decoder_t *d,
		  const struct morsedec_geometry *geometry,
		  struct md_cli_reporter *reporter);
void md_cli_mix(const float *input, float *mono, size_t count, int channels,
		int selected);
#endif
