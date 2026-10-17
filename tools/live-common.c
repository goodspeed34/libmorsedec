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
#define _POSIX_C_SOURCE 200809L
#include "audio-common.h"
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t interrupted;

static void interrupt_capture(int signal_number)
{
	(void)signal_number;
	interrupted = 1;
}

static void usage(FILE *out, int iq)
{
	fprintf(out, "Usage: %s [options]\n",
		iq ? "morsedec-iqlive" : "morsedec-live");
	fputs(iq ? "Decode little-endian I,Q from stdin; report signed Hz from "
		   "0 IF.\n"
		 : "Decode little-endian audio PCM from stdin until EOF or "
		   "Ctrl-C.\n",
	      out);
	fputs(
	    "  -r HZ       Input sample rate (required; pairs/second for I/Q)\n"
	    "  -F s16|f32  Sample format (default s16)\n",
	    out);
	if (!iq)
		fputs("  -n N        Interleaved audio channels (default 1)\n"
		      "  -c N        Select channel N (1-based); default "
		      "average\n",
		      out);
	fputs("  -f HZ       Pin a signal; otherwise discover automatically\n",
	      out);
	fputs(iq ? "  -l HZ       Signed band lower edge (default -1200)\n"
		 : "  -l HZ       Band lower edge (default 200)\n",
	      out);
	fputs("  -u HZ       Exclusive band upper edge (default 1200)\n"
	      "  -w WPM      Initial speed, 5..80 (default model priors)\n"
	      "  -s N        Stop after N seconds of input\n"
	      "  -a          Print every tracked channel\n"
	      "  -j          JSON Lines snapshots, including revisions\n"
	      "  -t FILE     Per-hop channel metrics as CSV\n"
	      "  -V          Show version, copyright and warranty\n"
	      "  -h          Show this help\n",
	      out);
}

int md_cli_live(int argc, char **argv, int iq)
{
	struct morsedec_config config;
	struct morsedec_geometry geometry;
	struct md_cli_reporter reporter = {0};
	morsedec_decoder_t *decoder = NULL;
	float *interleaved = NULL, *mono = NULL;
	FILE *trace = NULL;
	const char *trace_path = NULL;
	double rate = 0, frequency = 0, seconds = 0;
	unsigned char *bytes = NULL;
	size_t frame_bytes, capacity, pending = 0;
	int sample_bytes = 2;
	struct sigaction action = {0};
	uint64_t received = 0, limit = UINT64_MAX;
	int channels = iq ? 2 : 1, selected = 0;
	int status = EXIT_FAILURE;
	int frequency_set = 0, option;

	md_config_init(&config);
	if (iq)
		config.min_frequency = -1200;
	opterr = 0;
	while ((option = getopt(argc, argv,
				iq ? ":ajr:F:f:l:u:w:s:t:Vh"
				   : ":ajr:F:f:l:u:w:s:t:n:c:Vh")) != -1) {
		double value = 0;

		if (strchr("rncfluws", option) &&
		    !md_cli_number(optarg, &value)) {
			fprintf(stderr, "Invalid number for -%c: %s\n", option,
				optarg);
			goto done;
		}
		switch (option) {
		case 'V':
			return md_cli_version(iq ? "morsedec-iqlive"
						 : "morsedec-live");
		case 'h':
			usage(stdout, iq);
			return EXIT_SUCCESS;
		case 'a':
			reporter.all = 1;
			break;
		case 'j':
			reporter.json = 1;
			break;
		case 't':
			trace_path = optarg;
			break;
		case 'F':
			if (!strcmp(optarg, "s16"))
				sample_bytes = 2;
			else if (!strcmp(optarg, "f32"))
				sample_bytes = 4;
			else
				goto invalid_value;
			break;
		case 'r':
			if (value < 1 || value > UINT32_MAX ||
			    floor(value) != value)
				goto invalid_value;
			rate = value;
			break;
		case 'n':
		case 'c':
			if (value < 1 || value > 256 || floor(value) != value)
				goto invalid_value;
			if (option == 'n')
				channels = (int)value;
			else
				selected = (int)value;
			break;
		case 'f':
			frequency = value;
			frequency_set = 1;
			break;
		case 'l':
			config.min_frequency = value;
			break;
		case 'u':
			config.max_frequency = value;
			break;
		case 'w':
			if (value < 5 || value > 80)
				goto invalid_value;
			config.initial_wpm = value;
			break;
		case 's':
			if (value <= 0)
				goto invalid_value;
			seconds = value;
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
	if (optind != argc) {
		fputs("Unexpected operand; input is read from stdin\n", stderr);
		goto done;
	}
	if (!iq && frequency_set && frequency <= 0) {
		fputs("Audio pin frequency must be positive\n", stderr);
		goto done;
	}
	if (selected > channels) {
		fputs("-c exceeds -n\n", stderr);
		goto done;
	}
	if (!rate) {
		fputs("-r HZ is required for raw stdin input\n", stderr);
		goto done;
	}
	config.sample_rate = (uint32_t)rate;
	if (seconds) {
		double frames = floor(seconds * rate);
		if (!isfinite(frames) || frames < 1 ||
		    frames >= (double)UINT64_MAX) {
			fputs("Capture duration is out of range\n", stderr);
			goto done;
		}
		limit = (uint64_t)frames;
	}
	decoder = iq ? md_new_iq(&config) : md_new(&config);
	if (!decoder) {
		perror("Decoder configuration");
		goto done;
	}
	md_get_geometry(decoder, &geometry);
	if (frequency_set && !md_pin_frequency(decoder, frequency)) {
		perror("Pin frequency");
		goto done;
	}
	if (geometry.hop_size > SIZE_MAX / ((size_t)channels * sizeof(float))) {
		fputs("Capture buffer size is out of range\n", stderr);
		goto done;
	}
	interleaved =
	    calloc(geometry.hop_size * (size_t)channels, sizeof(float));
	if (!iq)
		mono = calloc(geometry.hop_size, sizeof(float));
	frame_bytes = (size_t)channels * (size_t)sample_bytes;
	capacity = geometry.hop_size * frame_bytes;
	bytes = malloc(capacity);
	reporter.seen = calloc(config.max_channels, sizeof(*reporter.seen));
	reporter.capacity = config.max_channels;
	if (!interleaved || (!iq && !mono) || !bytes || !reporter.seen) {
		perror("Capture buffers");
		goto done;
	}
	if (trace_path) {
		trace = fopen(trace_path, "w");
		if (!trace) {
			perror("Trace output");
			goto done;
		}
		reporter.trace = trace;
		fputs("frame,time,channel,frequency,key_probability,strength,"
		      "snr,wpm,state\n",
		      trace);
	}
	action.sa_handler = interrupt_capture;
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGINT, &action, NULL) ||
	    sigaction(SIGTERM, &action, NULL)) {
		perror("Install signal handler");
		goto done;
	}
	action.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &action, NULL)) {
		perror("Ignore SIGPIPE");
		goto done;
	}
	fprintf(stderr, "Reading stdin: %.0f Hz, %d channel(s), %s\n", rate,
		channels, sample_bytes == 2 ? "S16_LE" : "FLOAT_LE");
	while (!interrupted && received < limit) {
		struct pollfd input = {.fd = STDIN_FILENO, .events = POLLIN};
		size_t count, want = geometry.hop_size;
		ssize_t n;
		int ready = poll(&input, 1, 100);

		if (ready < 0) {
			if (errno == EINTR)
				continue;
			perror("Poll stdin");
			goto done;
		}
		if (!ready)
			continue;
		if ((uint64_t)want > limit - received)
			want = (size_t)(limit - received);
		n = read(STDIN_FILENO, bytes + pending,
			 want * frame_bytes - pending);
		if (n < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			perror("Read stdin");
			goto done;
		}
		if (!n) {
			if (pending) {
				fputs(iq ? "Truncated I/Q pair at EOF\n"
					 : "Truncated PCM frame at EOF\n",
				      stderr);
				goto done;
			}
			break;
		}
		pending += (size_t)n;
		count = pending / frame_bytes;
		for (size_t i = 0; i < count * (size_t)channels; i++) {
			const unsigned char *p =
			    bytes + i * (size_t)sample_bytes;
			uint32_t bits = (uint32_t)p[0] | (uint32_t)p[1] << 8;
			if (sample_bytes == 2) {
				int value = bits >= 32768 ? (int)bits - 65536
							  : (int)bits;
				interleaved[i] = value / 32768.f;
			} else {
				bits |= (uint32_t)p[2] << 16 | (uint32_t)p[3]
								   << 24;
				memcpy(&interleaved[i], &bits, sizeof(bits));
			}
		}
		pending -= count * frame_bytes;
		memmove(bytes, bytes + count * frame_bytes, pending);
		if (!iq)
			md_cli_mix(interleaved, mono, count, channels,
				   selected);
		if (!(iq ? md_process_iq(decoder, interleaved, count)
			 : md_process(decoder, mono, count))) {
			perror("Decode capture input");
			goto done;
		}
		received += count;
		if (!md_cli_report(decoder, &geometry, &reporter)) {
			perror("Output");
			goto done;
		}
	}
	if (!md_flush(decoder)) {
		perror("Drain decoder");
		goto done;
	}
	if (!md_cli_report(decoder, &geometry, &reporter)) {
		perror("Output");
		goto done;
	}
	status = EXIT_SUCCESS;
done:
	if (trace && fclose(trace)) {
		perror("Close trace");
		status = EXIT_FAILURE;
	}
	md_delete(decoder);
	free(bytes);
	free(interleaved);
	free(mono);
	free(reporter.seen);
	return status;
invalid_value:
	fprintf(stderr, "Invalid value for -%c: %s\n", option, optarg);
	goto done;
}
