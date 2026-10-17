/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Deterministic worker scheduling, lifecycle and public snapshot checks. */
#include "internal.h"
#include <assert.h>
#include <stdio.h>
#include <fenv.h>
#ifdef MD_PARALLEL
#include <pthread.h>
#endif

#ifdef MD_WRAP_THREADS
static int create_fail_after = -1;
static int calloc_fail_after = -1;
int __real_pthread_create(pthread_t *, const pthread_attr_t *,
			  void *(*)(void *), void *);
void *__real_calloc(size_t, size_t);

int __wrap_pthread_create(pthread_t *t, const pthread_attr_t *a,
			  void *(*entry)(void *), void *arg)
{
	if (create_fail_after == 0) {
		create_fail_after = -1;
		return EAGAIN;
	}
	if (create_fail_after > 0)
		create_fail_after--;
	return __real_pthread_create(t, a, entry, arg);
}

void *__wrap_calloc(size_t n, size_t size)
{
	if (calloc_fail_after == 0) {
		calloc_fail_after = -1;
		errno = ENOMEM;
		return NULL;
	}
	if (calloc_fail_after > 0)
		calloc_fail_after--;
	return __real_calloc(n, size);
}
#endif

static void compare(const morsedec_decoder_t *a, const morsedec_decoder_t *b)
{
	assert(md_get_frame_count(a) == md_get_frame_count(b));
	assert(md_get_channel_count(a) == md_get_channel_count(b));
	assert(md_get_state(a) == md_get_state(b));
	assert(md_get_frequency(a) == md_get_frequency(b));
	assert(md_get_snr(a) == md_get_snr(b));
	assert(md_get_wpm(a) == md_get_wpm(b));
	for (size_t i = 0; i < md_get_channel_count(a); i++) {
		struct morsedec_channel x, y;
		const struct morsedec_character *cx, *cy;
		char tx[1501], ty[1501];
		size_t nx, ny, rx, ry;

		assert(md_get_channel(a, i, &x));
		assert(md_get_channel(b, i, &y));
		assert(x.id == y.id && x.state == y.state);
		assert(x.frequency == y.frequency && x.snr == y.snr);
		assert(x.wpm == y.wpm &&
		       x.key_probability == y.key_probability);
		assert(x.strength == y.strength);
		assert(x.character_count == y.character_count);
		assert(a->order[i]->next_frame == b->order[i]->next_frame);
		assert(a->order[i]->stats.learning ==
		       b->order[i]->stats.learning);
		assert(md_get_characters(a, x.id, &cx, &nx));
		assert(md_get_characters(b, y.id, &cy, &ny));
		assert(nx == ny);
		for (size_t j = 0; j < nx; j++) {
			assert(cx[j].character == cy[j].character);
			assert(cx[j].word_type == cy[j].word_type);
			assert(cx[j].ordinal == cy[j].ordinal);
			assert(cx[j].low_snr == cy[j].low_snr);
			assert(cx[j].word_space_probability ==
			       cy[j].word_space_probability);
			assert(cx[j].word_end == cy[j].word_end);
		}
		assert(md_get_text(a, x.id, tx, sizeof(tx), &rx));
		assert(md_get_text(b, y.id, ty, sizeof(ty), &ry));
		assert(rx == ry && !strcmp(tx, ty));
	}
}

static void lifecycle_case(size_t threads)
{
	struct morsedec_config config;
	morsedec_decoder_t *a, *b;
	float power[512];
	uint64_t retired;
	const struct morsedec_character *chars;
	size_t count;

	md_config_init(&config);
	config.sample_rate = 8000;
	config.max_frequency = 3800;
	config.max_channels = 8;
	a = md_new_with_threads(&config, 1);
	b = md_new_with_threads(&config, threads);
	assert(a && b);
	for (int i = 0; i < 8; i++) {
		/* Seed adjacent untrained channels to exercise the merge
		 * boundary. */
		int bin = i == 1 ? 25 : 24 + 50 * i;
		assert(md_channel_new(a, bin) && md_channel_new(b, bin));
	}
	a->current = &a->channels[0];
	b->current = &b->channels[0];
	a->current->pinned = b->current->pinned = 1;
	a->frames = b->frames = 31;
	retired = a->channels[1].id;
	for (int i = 0; i < a->bins; i++)
		power[i] = .01f;
	assert(md_process_spectrum(a, power, (size_t)a->bins));
	assert(md_process_spectrum(b, power, (size_t)b->bins));
	compare(a, b);
	assert(a->count == 7 && !a->channels[1].used);
	assert(!md_get_characters(a, retired, &chars, &count) &&
	       errno == ENOENT);
	assert(!md_get_characters(b, retired, &chars, &count) &&
	       errno == ENOENT);
	assert(md_channel_new(a, 74) == &a->channels[1]);
	assert(md_channel_new(b, 74) == &b->channels[1]);
	assert(a->channels[1].id > retired);
	for (int i = 0; i < 40; i++) {
		assert(md_process_spectrum(a, power, (size_t)a->bins));
		assert(md_process_spectrum(b, power, (size_t)b->bins));
		compare(a, b);
	}
	md_delete(a);
	md_delete(b);
}

static void spectrum_case(size_t threads)
{
	struct morsedec_config config;
	morsedec_decoder_t *a, *b;
	float power[512];
	size_t peak = 0, characters = 0;
	md_config_init(&config);
	config.sample_rate = 8000;
	config.max_frequency = 3800;
	config.max_channels = 8;
	a = md_new_with_threads(&config, 1);
	b = md_new_with_threads(&config, threads);
	assert(a && b);
	assert(md_get_thread_count(a) == 1);
	if (threads)
		assert(md_get_thread_count(b) == threads);
	assert(md_get_thread_count(b) >= 1 && md_get_thread_count(b) <= 8);
	for (int pass = 0; pass < 2; pass++) {
		for (int frame = 0; frame < 2400; frame++) {
			/* Workers must follow a rounding mode changed after
			 * construction. */
			assert(!fesetround(frame >= 500 && frame < 750
					       ? FE_DOWNWARD
					       : FE_TONEAREST));
			for (int j = 0; j < a->bins; j++)
				power[j] = .01f;
			/* Acquire/replay, drop every carrier, then reuse slots.
			 */
			for (int k = 0;
			     k < 8 && (frame < 1100 || frame >= 2200); k++) {
				int bin = 24 + k * 50;
				int mark = (frame + 11 * k) % 90 < 45;
				power[bin] = mark ? 100 : .01f;
				power[bin - 1] = power[bin + 1] =
				    mark ? 8 : .01f;
			}
			if (frame == 50) {
				assert(!md_process_spectrum(
					   a, power, (size_t)a->bins - 1) &&
				       errno == EINVAL);
				assert(!md_process_spectrum(
					   b, power, (size_t)b->bins - 1) &&
				       errno == EINVAL);
				power[0] = NAN;
				power[1] = INFINITY;
			}
			if (frame == 600) {
				assert(md_pin_frequency(
				    a, a->geometry.first_frequency +
					   74 * a->geometry.bin_spacing));
				assert(md_pin_frequency(
				    b, b->geometry.first_frequency +
					   74 * b->geometry.bin_spacing));
			}
			if (frame == 900) {
				assert(md_unpin_frequency(a));
				assert(md_unpin_frequency(b));
			}
			if (frame == 1500) {
				assert(md_pin_frequency(
				    a, a->geometry.first_frequency +
					   74 * a->geometry.bin_spacing));
				assert(md_pin_frequency(
				    b, b->geometry.first_frequency +
					   74 * b->geometry.bin_spacing));
			}
			assert(md_process_spectrum(a, power, (size_t)a->bins));
			assert(md_process_spectrum(b, power, (size_t)b->bins));
			compare(a, b);
			if (b->count > peak)
				peak = b->count;
			for (size_t i = 0; i < b->count; i++)
				characters += b->order[i]->message.count;
		}
		assert(peak == 8 && characters);
		assert(md_flush(a) && md_flush(b));
		compare(a, b);
		assert(md_flush(a) && md_flush(b));
		compare(a, b);
		assert(md_reset(a) && md_reset(b));
		compare(a, b);
	}
	md_delete(a);
	md_delete(b);
}

static void audio_case(size_t threads, uint32_t rate, int iq)
{
	struct morsedec_config config;
	morsedec_decoder_t *a, *b;
	float samples[2048];
	int16_t pcm[2048];
	size_t offset = 0, peak = 0;

	md_config_init(&config);
	config.sample_rate = rate;
	if (rate == 16000)
		config.hop_size = 80;
	if (rate == 32000)
		config.hop_size = 640;
	config.min_frequency = iq ? -1800 : 200;
	config.max_frequency = 2200;
	config.max_channels = 8;
	a = iq ? md_new_iq_with_threads(&config, 1)
	       : md_new_with_threads(&config, 1);
	b = iq ? md_new_iq_with_threads(&config, threads)
	       : md_new_with_threads(&config, threads);
	assert(a && b);
	for (size_t step = 0; offset < (size_t)rate * 7; step++) {
		size_t count = step % 3 == 0 ? 1 : step % 3 == 1 ? 173 : 1024;
		for (size_t i = 0; i < count; i++) {
			double re = 0, im = 0, t = (double)(offset + i) / rate;
			for (int k = 0; k < 6; k++) {
				double hz =
				    iq ? -1400 + k * 550 : 400 + k * 300;
				if (((offset + i + k * rate / 7) /
				     (rate / 12)) %
					6 <
				    3) {
					re += .08 * cos(2 * MD_PI * hz * t);
					im += .08 * sin(2 * MD_PI * hz * t);
				}
			}
			if (iq) {
				samples[2 * i] = (float)re;
				samples[2 * i + 1] = (float)im;
				pcm[2 * i] = (int16_t)(re * 32767);
				pcm[2 * i + 1] = (int16_t)(im * 32767);
			} else {
				samples[i] = (float)re;
				pcm[i] = (int16_t)(re * 32767);
			}
		}
		if (step == 64) {
			samples[0] = NAN;
			samples[1] = INFINITY;
		}
		if (iq && step % 2) {
			assert(md_process_iq_s16(a, pcm, count));
			assert(md_process_iq_s16(b, pcm, count / 2));
			assert(md_process_iq_s16(b, pcm + 2 * (count / 2),
						 count - count / 2));
		} else if (iq) {
			assert(md_process_iq(a, samples, count));
			/* Different chunks must preserve every completed hop.
			 */
			assert(md_process_iq(b, samples, count / 2));
			assert(md_process_iq(b, samples + 2 * (count / 2),
					     count - count / 2));
		} else if (step % 2) {
			assert(md_process_s16(a, pcm, count));
			assert(md_process_s16(b, pcm, count / 2));
			assert(md_process_s16(b, pcm + count / 2,
					      count - count / 2));
		} else {
			assert(md_process(a, samples, count));
			assert(md_process(b, samples, count / 2));
			assert(md_process(b, samples + count / 2,
					  count - count / 2));
		}
		offset += count;
		compare(a, b);
		if (b->count > peak)
			peak = b->count;
	}
	assert(peak >= 4);
	assert(md_flush(a) && md_flush(b));
	compare(a, b);
	assert(!md_process(b, samples, 1) && errno == EINVAL);
	md_delete(a);
	md_delete(b);
}

#ifdef MD_PARALLEL
static void *independent(void *argument)
{
	morsedec_decoder_t *d = argument;
	float power[512];
	for (int i = 0; i < 700; i++) {
		for (int j = 0; j < d->bins; j++)
			power[j] = .01f;
		for (int k = 0; k < 4; k++) {
			int bin = 20 + k * 25;
			if ((i + 3 * k) % 24 < 12) {
				power[bin] = 100;
				power[bin - 1] = power[bin + 1] = 8;
			}
		}
		assert(md_process_spectrum(d, power, (size_t)d->bins));
	}
	assert(md_flush(d));
	return NULL;
}
#endif

int main(void)
{
	assert(!md_get_thread_count(NULL));
#ifdef MD_PARALLEL
	const size_t budgets[] = {0, 1, 2, 4};
	for (size_t i = 0; i < sizeof(budgets) / sizeof(*budgets); i++) {
		lifecycle_case(budgets[i]);
		spectrum_case(budgets[i]);
		audio_case(budgets[i], 8000, 0);
		audio_case(budgets[i], 48000, 1);
	}
	audio_case(4, 16000, 0);
	audio_case(2, 32000, 1);
	{
		pthread_t threads[2];
		morsedec_decoder_t *a = md_new_with_threads(NULL, 4);
		morsedec_decoder_t *b = md_new_with_threads(NULL, 2);
		assert(a && b);
		for (int i = 0; i < 4; i++)
			assert(md_channel_new(a, 20 + i * 25) &&
			       md_channel_new(b, 20 + i * 25));
		assert(!pthread_create(&threads[0], NULL, independent, a));
		assert(!pthread_create(&threads[1], NULL, independent, b));
		assert(!pthread_join(threads[0], NULL));
		assert(!pthread_join(threads[1], NULL));
		md_delete(a);
		md_delete(b);
	}
#ifdef MD_WRAP_THREADS
	for (int i = 0; i < 3; i++) {
		create_fail_after = i;
		assert(!md_new_with_threads(NULL, 4) && errno == EAGAIN);
	}
	{
		morsedec_decoder_t *d = md_new(NULL);
		size_t count;
		assert(d);
		count = md_get_thread_count(d);
		md_delete(d);
		for (size_t i = 0; i + 1 < count; i++) {
			create_fail_after = (int)i;
			d = md_new(NULL);
			assert(d && md_get_thread_count(d) == 1);
			md_delete(d);
		}
	}
	for (int i = 0; i < 50; i++) {
		morsedec_decoder_t *d;
		calloc_fail_after = i;
		d = md_new_with_threads(NULL, 4);
		assert(d || errno == ENOMEM);
		calloc_fail_after = -1;
		md_delete(d);
	}
#endif
#else
	lifecycle_case(0);
	spectrum_case(0);
	audio_case(1, 8000, 0);
	audio_case(0, 48000, 1);
	audio_case(1, 16000, 0);
	audio_case(0, 32000, 1);
	assert(!md_new_with_threads(NULL, 2) && errno == ENOTSUP);
	assert(!md_new_iq_with_threads(NULL, 2) && errno == ENOTSUP);
#endif
	for (int i = 0; i < 20; i++) {
		morsedec_decoder_t *d = md_new(NULL);
		assert(d && md_reset(d));
		md_delete(d);
	}
	puts("Parallel decoder checks passed");
	return 0;
}
