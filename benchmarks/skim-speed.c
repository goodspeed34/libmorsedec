/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Wideband I/Q CW skimming CPU benchmark on a synthetic multi-carrier input. */
#include "morsedec.h"
#include <fftw3.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TWO_PI 6.28318530717958647692
#define PI 3.14159265358979323846

static uint32_t rng_state = 0x2545f491u;

static uint32_t rnd(void)
{
	rng_state = rng_state * 1664525u + 1013904223u;
	return rng_state;
}

static double rndf(void)
{
	return (rnd() >> 8) * (1.0 / 16777216.0);
}

static double cpu_now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* Parity digest over everything the public API exposes for a decoded run.
 * Floats are hashed as raw bit patterns so a perturbation that leaves the
 * text unchanged still changes the digest. */
#define FNV_OFFSET UINT64_C(14695981039346656037)
#define FNV_PRIME UINT64_C(1099511628211)

static uint64_t fnv_bytes(uint64_t h, const void *data, size_t n)
{
	const unsigned char *p = data;

	for (size_t i = 0; i < n; i++) {
		h ^= p[i];
		h *= FNV_PRIME;
	}
	return h;
}

static uint64_t fnv_u64(uint64_t h, uint64_t value)
{
	return fnv_bytes(h, &value, sizeof(value));
}

static uint64_t fnv_i64(uint64_t h, int64_t value)
{
	return fnv_bytes(h, &value, sizeof(value));
}

static uint64_t fnv_float(uint64_t h, float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return fnv_bytes(h, &bits, sizeof(bits));
}

/* Channel fields, every character, and the text, in a stable order. */
static uint64_t channel_digest(morsedec_decoder_t *d, size_t index)
{
	struct morsedec_channel channel;
	const struct morsedec_character *characters = NULL;
	char stack[512];
	char *text = stack;
	size_t count = 0, required = 0;
	uint64_t h = FNV_OFFSET;

	if (!md_get_channel(d, index, &channel))
		return h;
	h = fnv_u64(h, index);
	h = fnv_u64(h, channel.id);
	h = fnv_i64(h, (int64_t)channel.state);
	h = fnv_float(h, channel.frequency);
	h = fnv_float(h, channel.snr);
	h = fnv_float(h, channel.wpm);
	h = fnv_float(h, channel.key_probability);
	h = fnv_float(h, channel.strength);
	h = fnv_u64(h, channel.character_count);
	if (md_get_text(d, channel.id, NULL, 0, &required) ==
		MORSEDEC_SUCCESS &&
	    required > 0) {
		if (required > sizeof(stack)) {
			text = malloc(required);
			if (!text)
				return h;
		}
		if (md_get_text(d, channel.id, text, required, &required) ==
		    MORSEDEC_SUCCESS)
			h = fnv_bytes(h, text, required - 1);
		if (text != stack)
			free(text);
	}
	if (md_get_characters(d, channel.id, &characters, &count) ==
	    MORSEDEC_SUCCESS) {
		h = fnv_u64(h, count);
		for (size_t i = 0; i < count; i++) {
			h = fnv_bytes(h, &characters[i].character, 1);
			h = fnv_i64(h, (int64_t)characters[i].word_type);
			h = fnv_u64(h, characters[i].ordinal);
			h = fnv_i64(h, characters[i].low_snr);
			h = fnv_float(h, characters[i].word_space_probability);
			h = fnv_i64(h, characters[i].word_end);
		}
	}
	return h;
}

static uint64_t decoder_digest(morsedec_decoder_t *d)
{
	uint64_t h = fnv_u64(FNV_OFFSET, md_get_channel_count(d));

	for (size_t i = 0; i < md_get_channel_count(d); i++)
		h = fnv_u64(h, channel_digest(d, i));
	return h;
}

struct gen {
	uint32_t sample_rate;
	size_t count;
	float *iq;
	size_t carriers;
	double noise;
};

/* One carrier: random Morse keying, raised-cosine edges, complex rotation. */
static void carrier(struct gen *g, double freq, double wpm, double amplitude)
{
	double fs = g->sample_rate;
	double dit = (1.2 / wpm) * fs;
	size_t ramp = (size_t)(0.005 * fs);
	double w = TWO_PI * freq / fs;
	double phase = TWO_PI * rndf();
	double cr = cos(phase), sr = sin(phase);
	double ci = cos(w), si = sin(w);
	size_t i = (size_t)(rndf() * dit * 4);
	int mark = 1;

	while (i < g->count) {
		double units = mark ? (rnd() % 4 == 0 ? 3 : 1)
				    : (rnd() % 8 == 0	? 7
				       : rnd() % 3 == 0 ? 3
							: 1);
		size_t end = i + (size_t)(units * dit);

		if (end > g->count)
			end = g->count;
		for (size_t k = i; k < end; k++) {
			double env = 0;
			size_t pos = k - i, len = end - i;

			if (mark) {
				if (pos < ramp)
					env = 0.5 - 0.5 * cos(PI * pos / ramp);
				else if (len - pos <= ramp)
					env = 0.5 - 0.5 * cos(PI * (len - pos) /
							      ramp);
				else
					env = 1;
				g->iq[2 * k] += (float)(amplitude * env * cr);
				g->iq[2 * k + 1] +=
				    (float)(amplitude * env * sr);
			}
			double nr = cr * ci - sr * si;

			sr = cr * si + sr * ci;
			cr = nr;
		}
		i = end;
		mark = !mark;
	}
}

/* Uniform noise is enough to exercise the noise-floor and peak gates. */
static void add_noise(struct gen *g)
{
	for (size_t i = 0; i < 2 * g->count; i++)
		g->iq[i] +=
		    (float)(g->noise * (rndf() + rndf() + rndf() - 1.5));
}

/* Replicate the library frontend so the spectrum-input path can be timed
 * without the FFT inside the measured region. Mirrors src/frontend.c. */
static float *precompute(struct gen *g, const struct morsedec_geometry *geo,
			 size_t frames)
{
	size_t n = geo->fft_size, hop = geo->hop_size;
	float *window = calloc(n, sizeof(*window));
	float *out = calloc(frames * geo->spectrum_size, sizeof(*out));
	fftwf_complex *in = fftwf_alloc_complex(n);
	fftwf_complex *fft = fftwf_alloc_complex(n);
	fftwf_plan plan;

	for (size_t i = 0; i < n; i++) {
		double x = 2 * PI * (double)i / (double)n;
		double a =
		    2 * PI * 25 * ((double)i - (double)n / 2) / g->sample_rate;
		double sinc = fabs(a) < 1e-12 ? 1 : sin(a) / a;

		window[i] = (0.42 - 0.5 * cos(x) + 0.08 * cos(2 * x)) * sinc;
	}
	for (size_t i = 0; i < n; i++)
		window[i] *= 8000.0 / g->sample_rate;
	plan = fftwf_plan_dft_1d((int)n, in, fft, FFTW_FORWARD, FFTW_ESTIMATE);
	for (size_t m = 0; m < frames; m++) {
		int64_t end = (int64_t)((m + 1) * hop);
		int64_t first = end - (int64_t)n;

		for (size_t i = 0; i < n; i++) {
			int64_t index = first + (int64_t)i;

			if (index < 0 || (uint64_t)index >= g->count) {
				in[i][0] = in[i][1] = 0;
			} else {
				in[i][0] = g->iq[2 * index] * window[i];
				in[i][1] = g->iq[2 * index + 1] * window[i];
			}
		}
		fftwf_execute(plan);
		for (size_t j = 0; j < geo->spectrum_size; j++) {
			int k = (int)llround(geo->first_frequency /
					     geo->bin_spacing) +
				(int)j;
			float re, im, power;

			if (k < 0)
				k += (int)n;
			re = fft[k][0];
			im = fft[k][1];
			power = (re * re + im * im) * 0.00025f;
			out[m * geo->spectrum_size + j] =
			    isfinite(power) && power <= FLT_MAX ? power : 0;
		}
	}
	fftwf_destroy_plan(plan);
	fftwf_free(in);
	fftwf_free(fft);
	free(window);
	return out;
}

/* Same frames, same grid, same window, computed with a block-interleaved
 * Goertzel recurrence instead of the FFT. Blocks break the serial
 * dependency chain that limits a single-bin Goertzel. */
#define GOERTZEL_BLOCK 64
static float *precompute_goertzel(struct gen *g,
				  const struct morsedec_geometry *geo,
				  size_t frames)
{
	size_t n = geo->fft_size, hop = geo->hop_size;
	size_t bins = geo->spectrum_size;
	int first = (int)llround(geo->first_frequency / geo->bin_spacing);
	float *window = calloc(n, sizeof(*window));
	float *in_i = calloc(n, sizeof(*in_i));
	float *in_q = calloc(n, sizeof(*in_q));
	float *out = calloc(frames * bins, sizeof(*out));

	for (size_t i = 0; i < n; i++) {
		double x = 2 * PI * (double)i / (double)n;
		double a =
		    2 * PI * 25 * ((double)i - (double)n / 2) / g->sample_rate;
		double sinc = fabs(a) < 1e-12 ? 1 : sin(a) / a;

		window[i] = (0.42 - 0.5 * cos(x) + 0.08 * cos(2 * x)) * sinc;
	}
	for (size_t i = 0; i < n; i++)
		window[i] *= 8000.0 / g->sample_rate;
	for (size_t m = 0; m < frames; m++) {
		int64_t end = (int64_t)((m + 1) * hop);
		int64_t start = end - (int64_t)n;

		for (size_t i = 0; i < n; i++) {
			int64_t index = start + (int64_t)i;
			int live = index >= 0 && (uint64_t)index < g->count;

			in_i[i] = live ? g->iq[2 * index] * window[i] : 0;
			in_q[i] = live ? g->iq[2 * index + 1] * window[i] : 0;
		}
		for (size_t base = 0; base < bins; base += GOERTZEL_BLOCK) {
			size_t block = bins - base < GOERTZEL_BLOCK
					   ? bins - base
					   : GOERTZEL_BLOCK;
			float coeff[GOERTZEL_BLOCK], cw[GOERTZEL_BLOCK];
			float sw[GOERTZEL_BLOCK];
			float ai[GOERTZEL_BLOCK] = {0},
			      a2[GOERTZEL_BLOCK] = {0};
			float bi[GOERTZEL_BLOCK] = {0},
			      b2[GOERTZEL_BLOCK] = {0};

			for (size_t j = 0; j < block; j++) {
				int k = first + (int)(base + j);
				double w = 2 * PI * k / (double)n;

				if (k < 0)
					k += (int)n;
				coeff[j] = 2.0f * cosf(w);
				cw[j] = cosf(w);
				sw[j] = sinf(w);
			}
			for (size_t i = 0; i < n; i++) {
				float vi = in_i[i], vq = in_q[i];

				for (size_t j = 0; j < block; j++) {
					float s = vi + coeff[j] * ai[j] - a2[j];

					a2[j] = ai[j];
					ai[j] = s;
					s = vq + coeff[j] * bi[j] - b2[j];
					b2[j] = bi[j];
					bi[j] = s;
				}
			}
			for (size_t j = 0; j < block; j++) {
				/* X = DFT(I) + j*DFT(Q) for complex input. */
				float re =
				    (ai[j] - a2[j] * cw[j]) - (b2[j] * sw[j]);
				float im =
				    (a2[j] * sw[j]) + (bi[j] - b2[j] * cw[j]);
				float power = (re * re + im * im) * 0.00025f;

				out[m * bins + base + j] =
				    isfinite(power) && power <= FLT_MAX ? power
									: 0;
			}
		}
	}
	free(window);
	free(in_i);
	free(in_q);
	return out;
}

/* Time both frontends on the same input and compare the frames they emit. */
static void compare_frontends(struct gen *g,
			      const struct morsedec_geometry *geo,
			      size_t frames)
{
	size_t bins = geo->spectrum_size;
	float *fft_frames, *goe_frames;
	double t0, t_fft, t_goe, worst = 0, worst_above = 0, worst_abs = 0;
	size_t probes = frames < 32 ? frames : 32;
	size_t above = 0, over = 0;

	t0 = cpu_now();
	fft_frames = precompute(g, geo, probes);
	t_fft = cpu_now() - t0;
	t0 = cpu_now();
	goe_frames = precompute_goertzel(g, geo, probes);
	t_goe = cpu_now() - t0;
	for (size_t i = 0; i < probes * bins; i++) {
		double a = fft_frames[i], b = goe_frames[i];
		double d = fabs(a - b) / (a > 1e-12 ? a : 1e-12);
		double ad = fabs(a - b);

		if (d > worst)
			worst = d;
		if (ad > worst_abs)
			worst_abs = ad;
		if (a > 1e-9) {
			above++;
			if (d > 0.01)
				over++;
			if (d > worst_above)
				worst_above = d;
		}
	}
	printf("{\"probe_frames\":%zu,\"bins\":%zu,"
	       "\"fft_us_per_frame\":%.2f,\"goertzel_us_per_frame\":%.2f,"
	       "\"ratio\":%.1f,\"max_relative_difference\":%.3e,"
	       "\"max_relative_above_1e-9\":%.3e,"
	       "\"max_absolute_difference\":%.3e,"
	       "\"bins_above_1e-9\":%zu,\"bins_over_1pct\":%zu}\n",
	       probes, bins, t_fft / probes * 1e6, t_goe / probes * 1e6,
	       t_goe / t_fft, worst, worst_above, worst_abs, above, over);
	free(fft_frames);
	free(goe_frames);
}

int main(int argc, char **argv)
{
	double seconds = argc > 1 ? atof(argv[1]) : 20.0;
	size_t carriers = argc > 2 ? (size_t)atoi(argv[2]) : 40;
	size_t max_channels = argc > 3 ? (size_t)atoi(argv[3]) : 32;
	size_t threads = argc > 4 ? (size_t)atoi(argv[4]) : 1;
	int repeats = argc > 5 ? atoi(argv[5]) : 5;
	int spectrum_mode = argc > 6 ? atoi(argv[6]) : 0; /* 2: compare only */
	uint32_t sample_rate = argc > 7 ? (uint32_t)atoi(argv[7]) : 96000;
	struct morsedec_config config;
	struct gen g = {.sample_rate = 96000};
	struct morsedec_geometry geo;
	float *frames_power = NULL;
	size_t hop, frames, characters = 0;
	uint64_t digest = 0;
	double best = 1e30, precompute_seconds = 0;

	if (!sample_rate)
		sample_rate = 96000;
	g.sample_rate = sample_rate;
	md_config_init(&config);
	config.sample_rate = sample_rate;
	config.min_frequency = -(float)sample_rate / 2;
	config.max_frequency = (float)sample_rate / 2;
	config.max_channels = max_channels;
	g.count = (size_t)(seconds * g.sample_rate);
	g.carriers = carriers;
	g.noise = 0.004;
	g.iq = calloc(2 * g.count, sizeof(*g.iq));
	if (!g.iq)
		return 1;
	/* Spread carriers across the retained band, avoiding the edges. */
	for (size_t c = 0; c < carriers; c++) {
		double span = 94000.0 * sample_rate / 96000.0;
		double freq = -span / 2 + span * (c + 0.5) / carriers;
		double wpm = 15 + 25 * rndf();

		carrier(&g, freq, wpm, 0.05 + 0.05 * rndf());
	}
	add_noise(&g);

	{
		morsedec_decoder_t *d =
		    md_new_iq_with_threads(&config, threads ? threads : 1);
		if (!d)
			return 1;
		md_get_geometry(d, &geo);
		hop = geo.hop_size;
		md_delete(d);
	}
	frames = g.count / hop;
	if (spectrum_mode == 2) {
		compare_frontends(&g, &geo, frames);
		free(g.iq);
		return 0;
	}
	if (spectrum_mode == 3) {
		double t0 = cpu_now();

		frames_power = precompute_goertzel(&g, &geo, frames);
		precompute_seconds = cpu_now() - t0;
	} else if (spectrum_mode) {
		double t0 = cpu_now();

		frames_power = precompute(&g, &geo, frames);
		precompute_seconds = cpu_now() - t0;
	}
	printf("{\"seconds\":%.3f,\"carriers\":%zu,\"max_channels\":%zu,"
	       "\"threads\":%zu,\"sample_rate\":%u,\"hop\":%zu,\"frames\":%zu,"
	       "\"fft_size\":%zu,\"mode\":\"%s\",\"precompute_seconds\":%.6f,"
	       "\"cpu_seconds\":[",
	       seconds, carriers, max_channels, threads, sample_rate, hop,
	       frames, geo.fft_size,
	       spectrum_mode == 3 ? "goertzel"
	       : spectrum_mode	  ? "spectrum"
				  : "iq",
	       precompute_seconds);
	for (int repeat = 0; repeat < repeats; repeat++) {
		morsedec_decoder_t *d =
		    md_new_iq_with_threads(&config, threads ? threads : 1);
		double start, elapsed;
		size_t seen = 0;

		if (!d)
			return 1;
		start = cpu_now();
		if (spectrum_mode) {
			for (size_t m = 0; m < frames; m++)
				if (!md_process_spectrum(
					d, frames_power + m * geo.spectrum_size,
					geo.spectrum_size))
					return 1;
		} else {
			for (size_t offset = 0; offset < g.count;
			     offset += hop) {
				size_t chunk = g.count - offset < hop
						   ? g.count - offset
						   : hop;

				if (!md_process_iq(d, g.iq + 2 * offset, chunk))
					return 1;
			}
		}
		md_flush(d);
		elapsed = cpu_now() - start;
		if (elapsed < best)
			best = elapsed;
		for (size_t i = 0; i < md_get_channel_count(d); i++) {
			struct morsedec_channel channel;

			if (!md_get_channel(d, i, &channel))
				continue;
			seen += channel.character_count;
		}
		/* Outside the timed region: this walks and allocates. */
		if (seen > characters) {
			characters = seen;
			digest = decoder_digest(d);
		}
		md_delete(d);
		printf("%s%.6f", repeat ? "," : "", elapsed);
		fflush(stdout);
	}
	printf("],\"best\":%.6f,\"realtime\":%.1f,\"characters\":%zu,"
	       "\"channels\":%zu,\"digest\":\"%016llx\"}\n",
	       best, seconds / best, characters, carriers,
	       (unsigned long long)digest);
	free(frames_power);
	free(g.iq);
	return 0;
}
