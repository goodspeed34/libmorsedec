/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Final-current-channel adapter for the Vail synthetic comparison. */
#include "morsedec.h"
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	SF_INFO info = {0};
	struct morsedec_config config;
	struct morsedec_geometry geometry;
	float *samples = NULL;
	char text[4096];
	size_t chunk, required;
	sf_count_t count;
	morsedec_decoder_t *decoder = NULL;
	SNDFILE *file;
	int status = 1;

	if (argc < 2 || !(file = sf_open(argv[1], SFM_READ, &info)))
		return 1;
	if (info.channels != 1)
		goto done;
	md_config_init(&config);
	config.sample_rate = info.samplerate;
	decoder = md_new(&config);
	if (!decoder || !md_get_geometry(decoder, &geometry))
		goto done;
	chunk = argc > 2 ? strtoul(argv[2], NULL, 10) : geometry.hop_size;
	if (!chunk || chunk > 1048576)
		goto done;
	samples = calloc(chunk, sizeof(*samples));
	if (!samples)
		goto done;
	while ((count = sf_readf_float(file, samples, chunk)) > 0)
		if (!md_process(decoder, samples, count))
			goto done;
	if (sf_error(file))
		goto done;
	for (size_t i = 0; i < chunk; i++)
		samples[i] = 0;
	/* Match vail_decode's four-second silent tail before final output. */
	for (size_t left = 4 * config.sample_rate; left;) {
		size_t n = left < chunk ? left : chunk;
		if (!md_process(decoder, samples, n))
			goto done;
		left -= n;
	}
	if (!md_flush(decoder) ||
	    !md_get_text(decoder, 0, text, sizeof(text), &required))
		goto done;
	status = puts(text) < 0;
done:
	free(samples);
	md_delete(decoder);
	return sf_close(file) ? 1 : status;
}
