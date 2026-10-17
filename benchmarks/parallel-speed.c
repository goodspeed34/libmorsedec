/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Preloaded synthetic carriers: wall/CPU time and per-hop latency. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "morsedec.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define WARMUP 400
#define HOPS 1200
#define REPEATS 5
#define PI 3.14159265358979323846

static double now(void)
{
#ifdef _WIN32
	LARGE_INTEGER counter, frequency;
	QueryPerformanceCounter(&counter);
	QueryPerformanceFrequency(&frequency);
	return (double)counter.QuadPart / frequency.QuadPart;
#else
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec * 1e-9;
#endif
}

static double cpu_now(void)
{
#ifdef _WIN32
	FILETIME created, exited, kernel, user;
	ULARGE_INTEGER k, u;
	GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
	k.LowPart = kernel.dwLowDateTime;
	k.HighPart = kernel.dwHighDateTime;
	u.LowPart = user.dwLowDateTime;
	u.HighPart = user.dwHighDateTime;
	return (double)(k.QuadPart + u.QuadPart) * 1e-7;
#else
	return (double)clock() / CLOCKS_PER_SEC;
#endif
}

static int order(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return (x > y) - (x < y);
}

static int mark(size_t frame, size_t channel)
{
	/* Three dots followed by a letter gap, staggered between carriers. */
	size_t position = (frame + channel * 7) % 48;
	return position < 30 && position % 12 < 6;
}

int main(int argc, char **argv)
{
	struct morsedec_config config;
	struct morsedec_geometry g;
	morsedec_decoder_t *d;
	const char *mode = argc > 3 ? argv[3] : "spectrum";
	size_t carriers, threads, width, resolved = 1, active = 0, misses = 0;
	float *input;
	double wall[REPEATS], cpu[REPEATS], latency[REPEATS * HOPS];
	double construction[REPEATS], destruction[REPEATS];
	int iq = !strcmp(mode, "iq"), spectrum = !strcmp(mode, "spectrum");

	if (argc < 3 || argc > 5 || (!iq && !spectrum && strcmp(mode, "audio")))
		return 1;
	carriers = (size_t)strtoul(argv[1], NULL, 10);
	threads = (size_t)strtoul(argv[2], NULL, 10);
	if (!carriers || carriers > 32)
		return 1;
	md_config_init(&config);
	config.sample_rate =
	    argc > 4 ? (uint32_t)strtoul(argv[4], NULL, 10) : 48000;
	config.max_frequency = 500 + carriers * 220;
	config.max_channels = 32;
	if (iq)
		config.min_frequency = -config.max_frequency;
	d = iq ? md_new_iq_with_threads(&config, 1)
	       : md_new_with_threads(&config, 1);
	if (!d || !md_get_geometry(d, &g))
		return 1;
	md_delete(d);
	width = spectrum ? g.spectrum_size : g.hop_size * (iq ? 2 : 1);
	input = calloc((WARMUP + HOPS) * width, sizeof(*input));
	if (!input)
		return 1;
	for (size_t frame = 0; frame < WARMUP + HOPS; frame++) {
		float *out = input + frame * width;
		if (spectrum) {
			for (size_t j = 0; j < width; j++)
				out[j] = .01f;
			for (size_t k = 0; k < carriers; k++) {
				size_t bin = (size_t)llround(
				    (500 + k * 220 - g.first_frequency) /
				    g.bin_spacing);
				if (mark(frame, k)) {
					out[bin] = 100;
					out[bin - 1] = out[bin + 1] = 8;
				}
			}
		} else {
			for (size_t i = 0; i < g.hop_size; i++) {
				double t = (double)(frame * g.hop_size + i) /
					   config.sample_rate;
				for (size_t k = 0; k < carriers; k++) {
					double hz = 500 + k * 220;
					if (iq && k % 2)
						hz = -hz;
					if (!mark(frame, k))
						continue;
					out[i * (iq ? 2 : 1)] +=
					    (float)(.5 / carriers *
						    cos(2 * PI * hz * t));
					if (iq)
						out[2 * i + 1] +=
						    (float)(.5 / carriers *
							    sin(2 * PI * hz *
								t));
				}
			}
		}
	}
	for (int repeat = 0; repeat < REPEATS; repeat++) {
		double start = 0;
		double cpu_start = 0, created = now();
		d = iq ? md_new_iq_with_threads(&config, threads)
		       : md_new_with_threads(&config, threads);
		construction[repeat] = now() - created;
		if (!d)
			return 1;
		resolved = md_get_thread_count(d);
		for (size_t frame = 0; frame < WARMUP + HOPS; frame++) {
			double before = 0;
			int ok;
			const float *in = input + frame * width;
			if (frame == WARMUP) {
				cpu_start = cpu_now();
				start = now();
			}
			if (frame >= WARMUP)
				before = now();
			ok = spectrum ? md_process_spectrum(d, in, width)
			     : iq     ? md_process_iq(d, in, g.hop_size)
				      : md_process(d, in, g.hop_size);
			if (!ok)
				return 1;
			if (frame >= WARMUP) {
				double elapsed = now() - before;
				latency[repeat * HOPS + frame - WARMUP] =
				    elapsed;
				misses += elapsed > g.frame_seconds;
			}
		}
		wall[repeat] = now() - start;
		cpu[repeat] = cpu_now() - cpu_start;
		active = md_get_channel_count(d);
		created = now();
		md_delete(d);
		destruction[repeat] = now() - created;
	}
	qsort(wall, REPEATS, sizeof(*wall), order);
	qsort(cpu, REPEATS, sizeof(*cpu), order);
	qsort(construction, REPEATS, sizeof(*construction), order);
	qsort(destruction, REPEATS, sizeof(*destruction), order);
	qsort(latency, REPEATS * HOPS, sizeof(*latency), order);
	printf("{\"carriers\":%llu,\"requested_threads\":%llu,"
	       "\"resolved_threads\":%llu,\"active_channels\":%llu,"
	       "\"mode\":\"%s\",\"sample_rate\":%u,\"audio_seconds\":%.6f,"
	       "\"median_wall_seconds\":%.9f,\"median_cpu_seconds\":%.9f,"
	       "\"median_construction_seconds\":%.9f,\"median_destruction_"
	       "seconds\":%.9f,"
	       "\"hop_median_us\":%.3f,\"hop_p99_us\":%.3f,\"hop_max_us\":%.3f,"
	       "\"deadline_misses\":%llu}\n",
	       (unsigned long long)carriers, (unsigned long long)threads,
	       (unsigned long long)resolved, (unsigned long long)active, mode,
	       (unsigned int)config.sample_rate, HOPS * g.frame_seconds,
	       wall[REPEATS / 2], cpu[REPEATS / 2], construction[REPEATS / 2],
	       destruction[REPEATS / 2], latency[REPEATS * HOPS / 2] * 1e6,
	       latency[REPEATS * HOPS * 99 / 100] * 1e6,
	       latency[REPEATS * HOPS - 1] * 1e6, (unsigned long long)misses);
	free(input);
	return 0;
}
