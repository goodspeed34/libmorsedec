/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Measure CPU time of streaming the same preloaded 8 kHz mono PCM. */
#include <sndfile.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef VAIL_BASELINE
#include "vail.h"
#else
#include "morsedec.h"
#endif

int main(int argc, char **argv)
{
	SF_INFO info = {0};
	SNDFILE *file;
	int16_t *pcm;
	size_t count;

	if (argc != 2 || !(file = sf_open(argv[1], SFM_READ, &info)))
		return 1;
	if (info.samplerate != 8000 || info.channels != 1 || info.frames <= 0)
		return 1;
	count = ((size_t)info.frames + 32000 + 84) / 85 * 85;
	pcm = calloc(count, sizeof(*pcm));
	if (!pcm || sf_readf_short(file, pcm, info.frames) != info.frames)
		return 1;
	sf_close(file);
	printf("{\"audio_seconds\":%.6f,\"cpu_seconds\":[", count / 8000.0);
	for (int repeat = 0; repeat < 9; repeat++) {
		clock_t start;
		double seconds;
#ifdef VAIL_BASELINE
		vail_fe *fe = vail_fe_new();
		vail_api *d = API_New();
		if (!fe || !d)
			return 1;
#else
		struct morsedec_config config;
		morsedec_decoder_t *d;
		md_config_init(&config);
		config.sample_rate = 8000;
		d = md_new(&config);
		if (!d)
			return 1;
#endif
		start = clock();
		for (size_t offset = 0; offset < count; offset += 85) {
#ifdef VAIL_BASELINE
			float spectrum[128];
			memcpy(spectrum, vail_fe_process(fe, pcm + offset),
			       sizeof(spectrum));
			API_Feed(d, spectrum);
#else
			if (!md_process_s16(d, pcm + offset, 85))
				return 1;
#endif
		}
		seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
		printf("%s%.9f", repeat ? "," : "", seconds);
#ifdef VAIL_BASELINE
		API_Free(d);
		vail_fe_free(fe);
#else
		md_delete(d);
#endif
	}
	puts("]}");
	free(pcm);
	return 0;
}
