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

int md_frontend_init(struct morsedec_decoder *d)
{
	size_t n = d->geometry.fft_size;
	double sum = 0;

	d->audio = calloc(n, sizeof(*d->audio));
	d->window = calloc(n, sizeof(*d->window));
	if (d->iq) {
		d->quadrature = calloc(n, sizeof(*d->quadrature));
		d->fft_iq = fftw_alloc_complex(n);
	} else {
		d->fft_input = fftw_alloc_real(n);
	}
	d->fft_output = fftw_alloc_complex(d->iq ? n : n / 2 + 1);
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
		d->plan = fftw_plan_dft_1d((int)n, d->fft_iq, d->fft_output,
					   FFTW_FORWARD, FFTW_ESTIMATE);
	else
		d->plan = fftw_plan_dft_r2c_1d((int)n, d->fft_input,
					       d->fft_output, FFTW_ESTIMATE);
	return d->plan ? MORSEDEC_SUCCESS : md_fail(ENOMEM);
}

void md_frontend_frame(struct morsedec_decoder *d)
{
	size_t n = d->geometry.fft_size;

	for (size_t i = 0; i < n; i++) {
		size_t pos = (d->sample_pos + i) % n;
		if (d->iq) {
			d->fft_iq[i][0] = d->audio[pos] * d->window[i];
			d->fft_iq[i][1] = d->quadrature[pos] * d->window[i];
		} else {
			d->fft_input[i] = d->audio[pos] * d->window[i];
		}
	}
	fftw_execute(d->plan);
	for (int j = 0; j < d->bins; j++) {
		int k = d->first_bin + j;
		/* Signed bins are contiguous in the public spectrum; FFTW
		 * stores negative frequencies at the end of its complex output.
		 */
		if (k < 0)
			k += (int)n;
		double re = d->fft_output[k][0], im = d->fft_output[k][1];
		/* A complex exponential has twice the FFT amplitude of a real
		 * sinusoid with the same peak amplitude. Match the audio scale.
		 */
		double power = (re * re + im * im) * (d->iq ? 0.00025 : 0.001);

		d->power[j] = isfinite(power) && power <= 3.402823466e38
				  ? (float)power
				  : 0;
	}
	md_band_frame(d, d->power);
}
