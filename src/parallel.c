/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "internal.h"

#ifdef MD_PARALLEL
#include <pthread.h>
#include <fenv.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

struct md_worker {
	pthread_t thread;
	pthread_cond_t wake;
	struct md_parallel *pool;
	uint64_t generation;
};

struct md_parallel {
	struct morsedec_decoder *decoder;
	struct md_worker *workers;
	pthread_mutex_t mutex;
	pthread_cond_t done;
	fenv_t environment;
	size_t conditions, started, active, next, count;
	uint64_t generation;
	int stop, mutex_ready, done_ready;
};

static size_t cpu_count(void)
{
#ifdef _WIN32
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	return info.dwNumberOfProcessors ? info.dwNumberOfProcessors : 1;
#elif defined(_SC_NPROCESSORS_ONLN)
	long count = sysconf(_SC_NPROCESSORS_ONLN);
	return count > 0 ? (size_t)count : 1;
#else
	return 1;
#endif
}

static void run_jobs(struct md_parallel *p)
{
	for (;;) {
		struct md_channel *c;

		pthread_mutex_lock(&p->mutex);
		if (p->next == p->count) {
			pthread_mutex_unlock(&p->mutex);
			return;
		}
		c = p->decoder->jobs[p->next++];
		pthread_mutex_unlock(&p->mutex);
		md_channel_replay(p->decoder, c);
	}
}

static void *worker(void *argument)
{
	struct md_worker *w = argument;
	struct md_parallel *p = w->pool;
	uint64_t generation = 0;

	pthread_mutex_lock(&p->mutex);
	for (;;) {
		while (!p->stop && generation == w->generation)
			pthread_cond_wait(&w->wake, &p->mutex);
		if (p->stop)
			break;
		generation = w->generation;
		pthread_mutex_unlock(&p->mutex);
		fesetenv(&p->environment);
		run_jobs(p);
		pthread_mutex_lock(&p->mutex);
		if (!--p->active)
			pthread_cond_signal(&p->done);
	}
	pthread_mutex_unlock(&p->mutex);
	return NULL;
}

void md_parallel_delete(struct morsedec_decoder *d)
{
	struct md_parallel *p = d->parallel;

	if (!p)
		return;
	if (p->mutex_ready) {
		pthread_mutex_lock(&p->mutex);
		p->stop = 1;
		for (size_t i = 0; i < p->started; i++)
			pthread_cond_signal(&p->workers[i].wake);
		pthread_mutex_unlock(&p->mutex);
	}
	for (size_t i = 0; i < p->started; i++)
		pthread_join(p->workers[i].thread, NULL);
	for (size_t i = 0; i < p->conditions; i++)
		pthread_cond_destroy(&p->workers[i].wake);
	if (p->done_ready)
		pthread_cond_destroy(&p->done);
	if (p->mutex_ready)
		pthread_mutex_destroy(&p->mutex);
	free(p->workers);
	free(p);
	d->parallel = NULL;
	d->thread_count = 1;
}

int md_parallel_init(struct morsedec_decoder *d, size_t threads)
{
	struct md_parallel *p;
	size_t budget = threads ? threads : cpu_count();
	int error = ENOMEM;

	d->parallel_auto = !threads;
	d->thread_count = 1;
	if (budget > d->config.max_channels)
		budget = d->config.max_channels;
	if (budget == 1)
		return MORSEDEC_SUCCESS;
	p = calloc(1, sizeof(*p));
	if (!p)
		goto fail;
	d->parallel = p;
	p->decoder = d;
	p->workers = calloc(budget - 1, sizeof(*p->workers));
	if (!p->workers)
		goto fail;
	error = pthread_mutex_init(&p->mutex, NULL);
	if (error)
		goto fail;
	p->mutex_ready = 1;
	error = pthread_cond_init(&p->done, NULL);
	if (error)
		goto fail;
	p->done_ready = 1;
	for (size_t i = 0; i < budget - 1; i++) {
		p->workers[i].pool = p;
		error = pthread_cond_init(&p->workers[i].wake, NULL);
		if (error)
			goto fail;
		p->conditions++;
	}
	for (size_t i = 0; i < budget - 1; i++) {
		error = pthread_create(&p->workers[i].thread, NULL, worker,
				       &p->workers[i]);
		if (error)
			goto fail;
		p->started++;
	}
	d->thread_count = budget;
	return MORSEDEC_SUCCESS;
fail:
	md_parallel_delete(d);
	return threads ? md_fail(error) : MORSEDEC_SUCCESS;
}

void md_parallel_run(struct morsedec_decoder *d, size_t count)
{
	struct md_parallel *p = d->parallel;
	size_t participants = count < d->thread_count ? count : d->thread_count;

	if (!p || count < (d->parallel_auto ? 4u : 2u)) {
		for (size_t i = 0; i < count; i++)
			md_channel_replay(d, d->jobs[i]);
		return;
	}
	pthread_mutex_lock(&p->mutex);
	fegetenv(&p->environment);
	p->next = 0;
	p->count = count;
	p->active = participants - 1;
	p->generation++;
	for (size_t i = 0; i < p->active; i++) {
		p->workers[i].generation = p->generation;
		pthread_cond_signal(&p->workers[i].wake);
	}
	pthread_mutex_unlock(&p->mutex);
	run_jobs(p);
	pthread_mutex_lock(&p->mutex);
	/* Include workers that were signaled but have not claimed a job yet. */
	while (p->active)
		pthread_cond_wait(&p->done, &p->mutex);
	pthread_mutex_unlock(&p->mutex);
}
#else
int md_parallel_init(struct morsedec_decoder *d, size_t threads)
{
	d->thread_count = 1;
	return threads > 1 ? md_fail(ENOTSUP) : MORSEDEC_SUCCESS;
}

void md_parallel_delete(struct morsedec_decoder *d)
{
	(void)d;
}

void md_parallel_run(struct morsedec_decoder *d, size_t count)
{
	for (size_t i = 0; i < count; i++)
		md_channel_replay(d, d->jobs[i]);
}
#endif
