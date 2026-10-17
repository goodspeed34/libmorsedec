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
#include "tui.h"
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <poll.h>
#endif

/* Readiness wait before rechecking the interrupt flag, in milliseconds. */
#define MD_STDIN_WAIT 100

static volatile sig_atomic_t interrupted;

static void interrupt_capture(int signal_number)
{
	(void)signal_number;
	interrupted = 1;
}

#ifdef _WIN32
/*
 * A pipe handle signals I/O completion, not data availability. Peek before
 * reading and let the main loop check interrupts between idle intervals.
 * Disk reads need no readiness wait; console handles support waiting.
 */
static int wait_stdin(int timeout_ms)
{
	HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
	DWORD type, result, available, mode;

	if (handle == NULL || handle == INVALID_HANDLE_VALUE) {
		errno = EBADF;
		return -1;
	}
	type = GetFileType(handle);
	if (type == FILE_TYPE_DISK)
		return 1;
	if (type == FILE_TYPE_PIPE) {
		if (!PeekNamedPipe(handle, NULL, 0, NULL, &available, NULL)) {
			/* Let read() observe EOF after the producer closes. */
			if (GetLastError() == ERROR_BROKEN_PIPE)
				return 1;
			errno = EIO;
			return -1;
		}
		if (available)
			return 1;
		Sleep((DWORD)timeout_ms);
		return 0;
	}
	if (type != FILE_TYPE_CHAR) {
		errno = EBADF;
		return -1;
	}
	/* Character devices such as NUL can be read without a console wait. */
	if (!GetConsoleMode(handle, &mode))
		return 1;
	result = WaitForSingleObject(handle, (DWORD)timeout_ms);
	if (result == WAIT_OBJECT_0)
		return 1;
	if (result == WAIT_TIMEOUT)
		return 0;
	errno = EIO;
	return -1;
}
#endif

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
	      "  -p N        Processing threads, including caller (0: auto; 1: "
	      "serial)\n"
	      "  -s N        Stop after N seconds of input\n"
	      "  -a          Print every tracked channel\n"
	      "  -j          JSON Lines snapshots, including revisions\n"
	      "  -T          Full-screen ncurses view on the terminal\n"
	      "  -t FILE     Per-hop channel metrics as CSV\n"
	      "  -V          Show version, copyright and warranty\n"
	      "  -h          Show this help\n",
	      out);
}

/*
 * Diagnostics must not be written over the curses screen, and the endwin()
 * that follows would erase whatever reached the terminal first. Leave the
 * view, then report.
 */
static void leave_tui(struct md_tui **tui)
{
	if (*tui) {
		md_tui_stop(*tui);
		*tui = NULL;
	}
}

static void fail(struct md_tui **tui, const char *message, int with_errno)
{
	int error = errno;

	leave_tui(tui);
	errno = error;
	if (with_errno)
		perror(message);
	else
		fprintf(stderr, "%s\n", message);
}

int md_cli_live(int argc, char **argv, int iq)
{
	struct morsedec_config config;
	struct morsedec_geometry geometry;
	size_t threads = 0;
	struct md_cli_reporter reporter = {0};
	morsedec_decoder_t *decoder = NULL;
	float *interleaved = NULL, *mono = NULL;
	FILE *trace = NULL;
	const char *trace_path = NULL;
	double rate = 0, frequency = 0, seconds = 0;
	unsigned char *bytes = NULL;
	size_t frame_bytes, capacity, pending = 0;
	int sample_bytes = 2;
#ifndef _WIN32
	struct sigaction action = {0};
#endif
	uint64_t received = 0, limit = UINT64_MAX;
	int channels = iq ? 2 : 1, selected = 0;
	int status = EXIT_FAILURE;
	int frequency_set = 0, tui_requested = 0, option;

	md_config_init(&config);
	if (iq)
		config.min_frequency = -1200;
	opterr = 0;
	while ((option = getopt(argc, argv,
				iq ? ":ajTp:r:F:f:l:u:w:s:t:Vh"
				   : ":ajTp:r:F:f:l:u:w:s:t:n:c:Vh")) != -1) {
		double value = 0;

		if (strchr("rncfluws", option) &&
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
		case 'T':
			tui_requested = 1;
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
#ifdef _WIN32
	/* Raw PCM on stdin must not undergo text-mode translation. */
	_setmode(_fileno(stdin), _O_BINARY);
	if (signal(SIGINT, interrupt_capture) == SIG_ERR) {
		perror("Install signal handler");
		goto done;
	}
#else
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
#endif
	fprintf(stderr, "Reading stdin: %.0f Hz, %d channel(s), %s\n", rate,
		channels, sample_bytes == 2 ? "S16_LE" : "FLOAT_LE");
	if (tui_requested) {
		char stream[128];

		snprintf(stream, sizeof(stream), "%.0f Hz %s %d ch", rate,
			 sample_bytes == 2 ? "S16_LE" : "FLOAT_LE", channels);
		reporter.tui = md_tui_start(
		    iq ? "morsedec-iqlive" : "morsedec-live", stream);
		if (!reporter.tui) {
			perror("Terminal user interface (-T)");
			goto done;
		}
		md_tui_draw(reporter.tui, decoder, &geometry, 0, reporter.all);
	}
	while (!interrupted && received < limit) {
		size_t count, want = geometry.hop_size;
		ssize_t n;
		int ready;
#ifdef _WIN32
		ready = wait_stdin(MD_STDIN_WAIT);
#else
		struct pollfd input = {.fd = STDIN_FILENO, .events = POLLIN};
		ready = poll(&input, 1, MD_STDIN_WAIT);
#endif

		if (interrupted || (reporter.tui && md_tui_poll(reporter.tui)))
			break;
		if (ready < 0) {
			if (errno == EINTR)
				continue;
			fail(&reporter.tui, "Poll stdin", 1);
			goto done;
		}
		if (!ready) {
			md_tui_draw(reporter.tui, decoder, &geometry,
				    md_get_frame_count(decoder) *
					geometry.frame_seconds,
				    reporter.all);
			continue;
		}
		if ((uint64_t)want > limit - received)
			want = (size_t)(limit - received);
		n = read(STDIN_FILENO, bytes + pending,
			 want * frame_bytes - pending);
		if (n < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			fail(&reporter.tui, "Read stdin", 1);
			goto done;
		}
		if (!n) {
			if (pending) {
				fail(&reporter.tui,
				     iq ? "Truncated I/Q pair at EOF"
					: "Truncated PCM frame at EOF",
				     0);
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
			fail(&reporter.tui, "Decode capture input", 1);
			goto done;
		}
		received += count;
		if (!md_cli_report(decoder, &geometry, &reporter)) {
			fail(&reporter.tui, "Output", 1);
			goto done;
		}
	}
	if (!md_flush(decoder)) {
		fail(&reporter.tui, "Drain decoder", 1);
		goto done;
	}
	if (!md_cli_report(decoder, &geometry, &reporter)) {
		fail(&reporter.tui, "Output", 1);
		goto done;
	}
	status = EXIT_SUCCESS;
done:
	leave_tui(&reporter.tui);
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
