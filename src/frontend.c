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
#include "internal.h"
#include <simde/x86/sse.h>

int md_frontend_init(struct morsedec_decoder *d)
{
	size_t n = d->geometry.fft_size;
	double sum = 0;

	d->audio = calloc(n, sizeof(*d->audio));
	d->window = calloc(n, sizeof(*d->window));
	if (d->iq) {
		d->quadrature = calloc(n, sizeof(*d->quadrature));
		d->fft_iq = fftwf_alloc_complex(n);
	} else {
		d->fft_input = fftwf_alloc_real(n);
	}
	d->fft_output = fftwf_alloc_complex(d->iq ? n : n / 2 + 1);
	if (!d->audio || !d->window || !d->fft_output ||
	    (d->iq ? !d->quadrature || !d->fft_iq : !d->fft_input))
		return md_fail(ENOMEM);
	for (size_t i = 0; i < n; i++) {
		double x = 2 * MD_PI * (double)i / (double)n;
		double a = 2 * MD_PI * 25 * ((double)i - (double)n / 2) /
			   d->geometry.sample_rate;
		double sinc = fabs(a) < 1e-12 ? 1 : sin(a) / a;

		d->window[i] = (0.42 - 0.5 * cos(x) + 0.08 * cos(2 * x)) * sinc;
		sum += d->window[i];
	}
	/* Keep the calibrated power scale independent of input sample rate. */
	for (size_t i = 0; i < n; i++)
		d->window[i] *= 8000.0 / d->geometry.sample_rate;
	if (fabs(sum) < 1e-12)
		return md_fail(EINVAL);
	if (d->iq)
		d->plan = fftwf_plan_dft_1d((int)n, d->fft_iq, d->fft_output,
					    FFTW_FORWARD, FFTW_ESTIMATE);
	else
		d->plan = fftwf_plan_dft_r2c_1d((int)n, d->fft_input,
						d->fft_output, FFTW_ESTIMATE);
	return d->plan ? MORSEDEC_SUCCESS : md_fail(ENOMEM);
}

/* Split the sample ring into disjoint contiguous spans before windowing.
 * Unaligned loads also cover arbitrary hop sizes; tails never read past n.
 */
static void md_window_audio(float *restrict out, const float *restrict audio,
			    const float *restrict window, size_t n)
{
	size_t i = 0;
	for (; i + 4 <= n; i += 4)
		simde_mm_storeu_ps(
		    out + i, simde_mm_mul_ps(simde_mm_loadu_ps(audio + i),
					     simde_mm_loadu_ps(window + i)));
	for (; i < n; i++)
		out[i] = audio[i] * window[i];
}

static void md_window_iq(fftwf_complex *restrict out,
			 const float *restrict in_i, const float *restrict in_q,
			 const float *restrict window, size_t n)
{
	size_t i = 0;
	for (; i + 4 <= n; i += 4) {
		simde__m128 w = simde_mm_loadu_ps(window + i);
		simde__m128 re =
		    simde_mm_mul_ps(simde_mm_loadu_ps(in_i + i), w);
		simde__m128 im =
		    simde_mm_mul_ps(simde_mm_loadu_ps(in_q + i), w);
		simde_mm_storeu_ps(out[i], simde_mm_unpacklo_ps(re, im));
		simde_mm_storeu_ps(out[i + 2], simde_mm_unpackhi_ps(re, im));
	}
	for (; i < n; i++) {
		out[i][0] = in_i[i] * window[i];
		out[i][1] = in_q[i] * window[i];
	}
}

void md_frontend_frame(struct morsedec_decoder *d)
{
	size_t n = d->geometry.fft_size;

	size_t split = n - d->sample_pos;
	if (d->iq) {
		md_window_iq(d->fft_iq, d->audio + d->sample_pos,
			     d->quadrature + d->sample_pos, d->window, split);
		md_window_iq(d->fft_iq + split, d->audio, d->quadrature,
			     d->window + split, n - split);
	} else {
		md_window_audio(d->fft_input, d->audio + d->sample_pos,
				d->window, split);
		md_window_audio(d->fft_input + split, d->audio,
				d->window + split, n - split);
	}
	fftwf_execute(d->plan);
	for (int j = 0; j < d->bins; j++) {
		int k = d->first_bin + j;
		/* Signed bins are contiguous in the public spectrum; FFTW
		 * stores negative frequencies at the end of its complex output.
		 */
		if (k < 0)
			k += (int)n;
		float re = d->fft_output[k][0], im = d->fft_output[k][1];
		/* A complex exponential has twice the FFT amplitude of a real
		 * sinusoid with the same peak amplitude. Match the audio scale.
		 */
		float power = (re * re + im * im) * (d->iq ? 0.00025f : 0.001f);

		d->power[j] =
		    md_finite(power) && power <= FLT_MAX ? (float)power : 0;
	}
	md_band_frame(d, d->power);
}
