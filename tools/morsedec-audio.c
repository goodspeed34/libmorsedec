/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * morsedec-audio - decode Morse code from audio and I/Q files.
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
#define _POSIX_C_SOURCE 200809L
#include "audio-common.h"
#include <unistd.h>
#include <sndfile.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static void usage(FILE *out)
{
	fputs(
	    "Usage: morsedec-audio [options] FILE|-\n"
	    "Decode audio supported by libsndfile (WAV, FLAC, AIFF, ...).\n"
	    "  -i          Decode I,Q pairs; report signed Hz from 0 IF\n"
	    "  -f HZ       Pin a signal; otherwise discover automatically\n"
	    "  -l HZ       Band lower edge (default 200; -i: -1200)\n"
	    "  -u HZ       Exclusive band upper edge (default 1200)\n"
	    "  -w WPM      Initial speed, 5..80; 0 selects model priors\n"
	    "  -p N        Processing threads, including caller (0: auto; 1: "
	    "serial)\n"
	    "  -a          Print all tracked channels\n"
	    "  -j          JSON Lines snapshots, including revisions\n"
	    "  -t FILE     Per-frame channel metrics as CSV\n"
	    "  -c N        Select audio channel N (1-based); default average\n"
	    "  -F s16|f32  Read little-endian raw samples, including stdin\n"
	    "  -r HZ       Raw sample rate (required with -F)\n"
	    "  -n N        Raw channel count (default 1; -i: 2)\n"
	    "  -V          Show version, copyright and warranty\n"
	    "  -h          Show this help\n",
	    out);
}

int main(int argc, char **argv)
{
	struct morsedec_config config;
	struct morsedec_geometry geometry;
	size_t threads = 0;
	SF_INFO info = {0};
	SNDFILE *input = NULL;
	morsedec_decoder_t *decoder = NULL;
	const char *path = NULL, *trace_path = NULL;
	FILE *trace = NULL;
	float *interleaved = NULL, *mono = NULL;
	struct md_cli_seen *seen = NULL;
	struct md_cli_reporter reporter = {0};
	double frequency = 0, rate = 0, channels = 1, selected = 0;
	int raw = 0, all = 0, json = 0, status = EXIT_FAILURE;
	int iq = 0, band_min_set = 0, channels_set = 0, frequency_set = 0;
	int option;

	md_config_init(&config);
	opterr = 0;
	while ((option = getopt(argc, argv, ":iajp:f:l:u:w:t:c:F:r:n:Vh")) !=
	       -1) {
		double value = 0;

		if (strchr("fluwrnc", option) &&
		    !md_cli_number(optarg, &value)) {
			fprintf(stderr, "Invalid number for -%c: %s\n", option,
				optarg);
			goto done;
		}
		switch (option) {
		case 'p':
			if (!md_cli_threads(optarg, &threads))
				goto invalid_value;
			break;
		case 'V':
			return md_cli_version("morsedec-audio");
		case 'h':
			usage(stdout);
			return EXIT_SUCCESS;
		case 'i':
			iq = 1;
			break;
		case 'a':
			all = 1;
			break;
		case 'j':
			json = 1;
			break;
		case 't':
			trace_path = optarg;
			break;
		case 'F':
			if (!strcmp(optarg, "s16"))
				raw = SF_FORMAT_PCM_16;
			else if (!strcmp(optarg, "f32"))
				raw = SF_FORMAT_FLOAT;
			else
				goto invalid_value;
			break;
		case 'f':
			frequency = value;
			frequency_set = 1;
			break;
		case 'l':
			config.min_frequency = value;
			band_min_set = 1;
			break;
		case 'u':
			config.max_frequency = value;
			break;
		case 'w':
			config.initial_wpm = value;
			break;
		case 'r':
			if (value < 1 || value > INT32_MAX ||
			    floor(value) != value)
				goto invalid_value;
			rate = value;
			break;
		case 'n':
		case 'c':
			if (value < 1 || value > 256 || floor(value) != value)
				goto invalid_value;
			if (option == 'n') {
				channels = value;
				channels_set = 1;
			} else {
				selected = value;
			}
			break;
		case ':':
			fprintf(stderr, "Option -%c requires an argument\n",
				optopt);
			goto done;
		default:
			fprintf(stderr, "Unknown option: -%c\n", optopt);
			goto done;
		}
	}
	if (argc - optind != 1) {
		fputs("Exactly one input file or '-' is required\n", stderr);
		usage(stderr);
		goto done;
	}
	path = argv[optind];
	if (iq) {
		if (!band_min_set)
			config.min_frequency = -1200;
		if (!channels_set)
			channels = 2;
		if (selected || channels != 2) {
			fputs("I/Q input requires two channels and no "
			      "-c\n",
			      stderr);
			goto done;
		}
	} else if (frequency_set && frequency <= 0) {
		fputs("Audio pin frequency must be positive\n", stderr);
		goto done;
	}
	if (!path || (raw && !rate) || (!raw && rate)) {
		usage(stderr);
		goto done;
	}
	if (trace_path && !strcmp(trace_path, path)) {
		fputs("Trace output must differ from audio input\n", stderr);
		goto done;
	}
	if (raw) {
		info.samplerate = (int)rate;
		info.channels = (int)channels;
		info.format = SF_FORMAT_RAW | raw | SF_ENDIAN_LITTLE;
	}
	if (!strcmp(path, "-")) {
#ifdef _WIN32
		_setmode(_fileno(stdin), _O_BINARY);
#endif
		input = sf_open_fd(0, SFM_READ, &info, 0);
	} else {
		input = sf_open(path, SFM_READ, &info);
	}
	if (!input) {
		fprintf(stderr, "Audio input: %s\n", sf_strerror(NULL));
		goto done;
	}
	if (info.channels < 1 || info.channels > 256 ||
	    selected > info.channels || info.samplerate <= 0 ||
	    (iq && info.channels != 2)) {
		fputs("Unsupported channel count or sample rate\n", stderr);
		goto done;
	}
	config.sample_rate = (uint32_t)info.samplerate;
	decoder = iq ? md_new_iq_with_threads(&config, threads)
		     : md_new_with_threads(&config, threads);
	if (!decoder) {
		perror("Decoder configuration");
		goto done;
	}
	md_get_geometry(decoder, &geometry);
	if (frequency_set && !md_pin_frequency(decoder, frequency)) {
		perror("Pin frequency");
		goto done;
	}
	interleaved =
	    calloc(geometry.hop_size * (size_t)info.channels, sizeof(float));
	mono = calloc(geometry.hop_size, sizeof(float));
	seen = calloc(config.max_channels, sizeof(*seen));
	if (!interleaved || !mono || !seen) {
		perror("Audio buffers");
		goto done;
	}
	if (trace_path) {
		trace = fopen(trace_path, "w");
		if (!trace) {
			perror("Trace output");
			goto done;
		}
		fputs("frame,time,channel,frequency,key_probability,strength,"
		      "snr,wpm,state\n",
		      trace);
	}
	reporter = (struct md_cli_reporter){.seen = seen,
					    .capacity = config.max_channels,
					    .all = all,
					    .json = json,
					    .trace = trace,
					    .time_offset = 0};
	for (;;) {
		sf_count_t n = sf_readf_float(input, interleaved,
					      (sf_count_t)geometry.hop_size);
		if (!n)
			break;
		if (!iq)
			md_cli_mix(interleaved, mono, (size_t)n, info.channels,
				   (int)selected);
		if (!(iq ? md_process_iq(decoder, interleaved, (size_t)n)
			 : md_process(decoder, mono, (size_t)n))) {
			perror("Decode audio");
			goto done;
		}
		if (!md_cli_report(decoder, &geometry, &reporter)) {
			perror("Output");
			goto done;
		}
	}
	if (sf_error(input)) {
		fprintf(stderr, "Audio read: %s\n", sf_strerror(input));
		goto done;
	}
	md_flush(decoder);
	if (!md_cli_report(decoder, &geometry, &reporter)) {
		perror("Output");
		goto done;
	}
	if (fflush(stdout) || (trace && ferror(trace))) {
		perror("Output");
		goto done;
	}
	status = EXIT_SUCCESS;
done:
	if (trace && fclose(trace))
		status = EXIT_FAILURE;
	if (input && sf_close(input))
		status = EXIT_FAILURE;
	md_delete(decoder);
	free(interleaved);
	free(mono);
	free(seen);
	return status;
invalid_value:
	fprintf(stderr, "Invalid value for -%c: %s\n", option, optarg);
	goto done;
}
