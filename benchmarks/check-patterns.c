/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Regression checks for word boundaries and short streaming input. */
#include "internal.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
	char text[128];
	const char *words = "MAUVE GOLD CYAN";
	morsedec_decoder_t *d;
	size_t required;

	d = md_new(NULL);
	assert(d && md_pin_frequency(d, 700));
	for (size_t i = 0; words[i]; i++) {
		struct morsedec_character ch = {0};

		if (words[i] == ' ')
			continue;
		ch.character = words[i];
		ch.ordinal = i + 1;
		ch.word_space_probability = words[i + 1] == ' ' ? .999 : .001;
		md_word_feed(d, d->current, &ch);
		md_word_emit(d, d->current, !words[i + 1]);
	}
	assert(md_get_text(d, 0, text, sizeof(text), &required));
	assert(!strncmp(text, words, strlen(words)));
	for (size_t i = strlen(words); text[i]; i++)
		assert(text[i] == ' ');
	md_delete(d);
	{
		struct morsedec_config config;
		float samples[85];

		md_config_init(&config);
		config.sample_rate = 8000;
		d = md_new(&config);
		assert(d && md_pin_frequency(d, 700));
		/* A pinned single dot remains useful even though it cannot
		 * satisfy the automatic three-mark acquisition gate. */
		for (size_t offset = 0; offset < 40000; offset += 85) {
			for (size_t i = 0; i < 85; i++) {
				size_t n = offset + i;
				samples[i] =
				    n >= 24000 && n < 24480
					? .3 * cos(2 * MD_PI * 700 * n / 8000)
					: 0;
			}
			assert(md_process(d, samples, 85));
		}
		assert(md_flush(d));
		assert(md_get_text(d, 0, text, sizeof(text), &required));
		assert(text[0] == 'E');
		for (size_t i = 1; text[i]; i++)
			assert(text[i] == ' ');
		md_delete(d);

		d = md_new(&config);
		assert(d);
		/* TU at 20 WPM, ending on the last mark: explicit flush must
		 * select the newly eligible short channel without extra input.
		 */
		for (size_t offset = 0; offset < 30240; offset += 85) {
			size_t count =
			    30240 - offset < 85 ? 30240 - offset : 85;

			for (size_t i = 0; i < count; i++) {
				size_t n = offset + i;
				int mark = (n >= 24000 && n < 25440) ||
					   (n >= 26880 && n < 27360) ||
					   (n >= 27840 && n < 28320) ||
					   (n >= 28800 && n < 30240);

				samples[i] =
				    mark ? .3 * cos(2 * MD_PI * 700 * n / 8000)
					 : 0;
			}
			assert(md_process(d, samples, count));
		}
		assert(md_flush(d));
		assert(md_get_text(d, 0, text, sizeof(text), &required));
		assert(text[0] == 'T' && text[1] == 'U');
		for (size_t i = 2; text[i]; i++)
			assert(text[i] == ' ');
		md_delete(d);
	}
	puts("Decoder regression checks passed");
	return 0;
}
