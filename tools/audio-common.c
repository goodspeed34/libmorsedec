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
#include "audio-common.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

int md_cli_version(const char *program)
{
	if (printf("%s %s\n\n"
		   "Copyright (C) 2026 Gong BI1XJT\n"
		   "This is free software; see the source for copying "
		   "conditions.  There is NO\n"
		   "warranty; not even for MERCHANTABILITY or FITNESS FOR A "
		   "PARTICULAR PURPOSE.\n",
		   program, MORSEDEC_VERSION) < 0 ||
	    fflush(stdout))
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}

int md_cli_number(const char *text, double *out)
{
	char *end;
	errno = 0;
	*out = strtod(text, &end);
	return text[0] && !*end && !errno && isfinite(*out);
}

static void json_string(const char *text)
{
	putchar('"');
	for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
		if (*p == '"' || *p == '\\')
			printf("\\%c", *p);
		else if (*p < 32 || *p >= 127)
			printf("\\u%04x", *p);
		else
			putchar(*p);
	}
	putchar('"');
}

int md_cli_report(morsedec_decoder_t *d, const struct morsedec_geometry *g,
		  struct md_cli_reporter *reporter)
{
	struct md_cli_seen *seen = reporter->seen;
	size_t capacity = reporter->capacity;
	int all = reporter->all, json = reporter->json;
	FILE *trace = reporter->trace;
	double time =
	    reporter->time_offset + md_get_frame_count(d) * g->frame_seconds;
	for (size_t i = 0; i < md_get_channel_count(d); i++) {
		struct morsedec_channel c;
		char text[MORSEDEC_MESSAGE_CAPACITY * 3 + 1];
		size_t needed, slot = capacity;
		if (!md_get_channel(d, i, &c))
			continue;
		if (trace)
			fprintf(trace,
				"%llu,%.6f,%llu,%.6f,%.6f,%.6f,%.6f,%.6f,%d\n",
				(unsigned long long)md_get_frame_count(d), time,
				(unsigned long long)c.id, c.frequency,
				c.key_probability, c.strength, c.snr, c.wpm,
				(int)c.state);
		if (!all && (md_get_state(d) == MORSEDEC_STATE_NONE ||
			     fabs(c.frequency - md_get_frequency(d)) >
				 g->bin_spacing / 2))
			continue;
		if (!md_get_text(d, c.id, text, sizeof(text), &needed) ||
		    !text[0])
			continue;
		for (size_t j = 0; j < capacity; j++)
			if (seen[j].id == c.id) {
				slot = j;
				break;
			}
		if (slot == capacity) {
			/* Reuse slots whose channel has retired. */
			for (size_t j = 0; j < capacity; j++) {
				int active = 0;
				for (size_t k = 0; k < md_get_channel_count(d);
				     k++) {
					struct morsedec_channel other;
					md_get_channel(d, k, &other);
					active |= other.id == seen[j].id;
				}
				if (!active) {
					slot = j;
					seen[j].id = c.id;
					seen[j].text[0] = 0;
					break;
				}
			}
		}
		if (slot == capacity || !strcmp(seen[slot].text, text))
			continue;
		strcpy(seen[slot].text, text);
		if (json) {
			printf("{\"time\":%.6f,\"channel\":%llu,\"frequency\":%"
			       ".6f,"
			       "\"snr\":%.3f,\"wpm\":%.3f,\"text\":",
			       time, (unsigned long long)c.id, c.frequency,
			       c.snr, c.wpm);
			json_string(text);
			puts("}");
		} else {
			printf("[%8.3f s %8.2f Hz #%llu] %s\n", time,
			       c.frequency, (unsigned long long)c.id, text);
		}
	}
	return !fflush(stdout) && (!trace || !ferror(trace));
}

void md_cli_mix(const float *input, float *mono, size_t count, int channels,
		int selected)
{
	for (size_t i = 0; i < count; i++) {
		double value = 0;
		if (selected) {
			value =
			    input[i * (size_t)channels + (size_t)selected - 1];
		} else {
			for (int c = 0; c < channels; c++)
				value +=
				    input[i * (size_t)channels + (size_t)c];
			value /= channels;
		}
		mono[i] = (float)value;
	}
}
