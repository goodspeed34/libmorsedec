/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Soft observations, independent convergence, bounded storage and lifecycle. */
#include "internal.h"
#include "codes.h"
#include "wfst_graph.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>

static int md_allocation_guard;
static uint64_t md_frame_count;
static double md_latency;
static clock_t md_decode_time;

#ifdef MD_WRAP_ALLOC
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);

void *__wrap_malloc(size_t size)
{
	assert(!md_allocation_guard);
	return __real_malloc(size);
}
void *__wrap_calloc(size_t n, size_t size)
{
	assert(!md_allocation_guard);
	return __real_calloc(n, size);
}
void *__wrap_realloc(void *p, size_t size)
{
	assert(!md_allocation_guard);
	return __real_realloc(p, size);
}
void __wrap_free(void *p)
{
	assert(!md_allocation_guard);
	__real_free(p);
}
#endif

static struct morsedec_decoder *md_direct(uint32_t rate, size_t hop)
{
	struct morsedec_config config;
	md_config_init(&config);
	config.max_channels = 1;
	config.sample_rate = rate;
	config.hop_size = hop;
	struct morsedec_decoder *d = md_new_with_threads(&config, 1);
	assert(d && md_pin_frequency(d, 700));
	return d;
}

static void md_frames_feed(struct morsedec_decoder *d, double p, int count)
{
	md_allocation_guard = 1;
	for (int i = 0; i < count; i++) {
		if (p > .5)
			d->current->quiet = 0;
		else if (d->current->quiet < md_frames(d, 100))
			d->current->quiet++;
		md_wfst_advance(d, d->current, p, d->current->wfst.tick + 1, 0);
		if (d->current->quiet == md_frames(d, 75))
			md_stats_commit(d, d->current, NULL, 0, 1);
	}
	md_allocation_guard = 0;
	md_frame_count += (uint64_t)count;
}

static void md_pattern_feed(struct morsedec_decoder *d, const char *code,
			    int dot)
{
	for (size_t i = 0; code[i]; i++) {
		md_frames_feed(d, .999, code[i] == '.' ? dot : 3 * dot);
		if (code[i + 1])
			md_frames_feed(d, .001, dot);
	}
}

static const char *md_code_find(unsigned char ch)
{
	for (size_t i = 0; i < sizeof(md_codes) / sizeof(md_codes[0]); i++)
		if (md_codes[i].text[0] == ch && !md_codes[i].text[1])
			return md_codes[i].pattern;
	assert(0);
	return NULL;
}

static void md_text_check(struct morsedec_decoder *d, const char *expected)
{
	char text[128];
	size_t required;
	assert(md_get_text(d, 0, text, sizeof(text), &required));
	if (strncmp(text, expected, strlen(expected)))
		fprintf(stderr, "expected %s, got %s\n", expected, text);
	assert(!strncmp(text, expected, strlen(expected)));
	for (size_t i = strlen(expected); text[i]; i++)
		assert(text[i] == ' ');
}

static void md_pattern_check(const char *code, const char *expected, size_t hop,
			     int trailing)
{
	struct morsedec_decoder *d = md_direct(8000, hop);
	struct md_channel *c = d->current;
	int dot = (int)floor(exp(c->stats.mean[0]) / d->tick_scale + .5);
	clock_t start = clock();
	md_frames_feed(d, .001, 20);
	md_pattern_feed(d, code, dot);
	if (trailing) {
		uint64_t mark_tick = c->wfst.tick;
		uint64_t emitted = 0;
		for (int i = 0; i < md_frames(d, 300); i++) {
			md_frames_feed(d, .001, 1);
			if (!emitted && c->wfst.text_commits)
				emitted = c->wfst.tick - mark_tick;
		}
		if (!emitted)
			fprintf(stderr,
				"no streaming output for %s (%s), hop %zu, %d "
				"tokens\n",
				code, expected, hop, c->wfst.count);
		assert(emitted);
		if (emitted * d->geometry.frame_seconds > md_latency)
			md_latency = emitted * d->geometry.frame_seconds;
		md_text_check(d,
			      expected); /* Output must precede explicit EOF. */
	}
	md_allocation_guard = 1;
	md_wfst_flush(d, c);
	md_allocation_guard = 0;
	md_text_check(d, expected);
	uint64_t acoustic = c->wfst.acoustic_commits,
		 text = c->wfst.text_commits;
	md_allocation_guard = 1;
	md_wfst_flush(d, c);
	md_allocation_guard = 0;
	assert(c->wfst.acoustic_commits == acoustic &&
	       c->wfst.text_commits == text);
	assert(!c->wfst.recoveries && !c->wfst.forced_decisions);
	md_decode_time += clock() - start;
	md_delete(d);
}

static uint16_t md_label(const char *text)
{
	for (int i = 1; i < MD_WFST_GRAPH_SYMBOLS; i++)
		if (!strcmp(text, md_wfst_symbols[i]))
			return (uint16_t)i;
	assert(0);
	return 0;
}

/* Construct live competing histories, without depending on beam score tuning.
 */
static uint32_t md_history_fixture(struct md_wfst_decoder *w, uint32_t parent,
				   struct md_wfst_event event)
{
	uint32_t id = w->free_head;
	assert(id);
	w->free_head = w->history[id].next;
	w->free_count--;
	w->history_high = id;
	w->history[id] =
	    (struct md_wfst_history){.event = event,
				     .parent = parent,
				     .depth = w->history[parent].depth + 1};
	return id;
}

static void md_convergence_check(void)
{
	struct morsedec_decoder *d = md_direct(8000, 85);
	struct md_channel *c = d->current;
	struct md_wfst_decoder *w = &c->wfst;
	w->acquisition_active = 0;
	uint32_t mark = md_history_fixture(
	    w, 0,
	    (struct md_wfst_event){.start = 1, .ordinal = 5, .duration = 5});
	uint32_t gap = md_history_fixture(
	    w, mark,
	    (struct md_wfst_event){.start = 6, .ordinal = 10, .duration = -5});
	uint32_t e = md_history_fixture(
	    w, 0,
	    (struct md_wfst_event){
		.ordinal = 5, .output_label = md_label("E"), .domain = 1});
	uint32_t t = md_history_fixture(
	    w, 0,
	    (struct md_wfst_event){
		.ordinal = 5, .output_label = md_label("T"), .domain = 1});
	w->count = 2;
	w->tokens[0][0].history[0] = gap;
	w->tokens[0][0].history[1] = e;
	w->tokens[0][1] = w->tokens[0][0];
	w->tokens[0][1].history[1] = t;
	md_allocation_guard = 1;
	md_wfst_check_convergence(d, c);
	md_allocation_guard = 0;
	assert(w->acoustic_commits == 2 && !w->text_commits);
	assert(c->stats.acquisition_marks == 1 && c->stats.committed_end == 10);
	assert(!c->message.count);
	w->count = 1;
	md_allocation_guard = 1;
	md_wfst_check_convergence(d, c);
	md_wfst_check_convergence(d, c);
	md_allocation_guard = 0;
	assert(w->text_commits == 1 && w->acoustic_commits == 2);
	assert(w->free_count == MD_WFST_HISTORY - 1);
	md_text_check(d, "E");
	md_delete(d);
}

static void md_pressure_check(void)
{
	struct morsedec_decoder *d = md_direct(8000, 85);
	struct md_channel *c = d->current;
	struct md_wfst_decoder *w = &c->wfst;
	w->acquisition_active = 0;
	uint32_t heads[2] = {0};
	for (int branch = 0; branch < 2; branch++) {
		uint64_t ordinal = 1;
		int count =
		    branch ? MD_WFST_HISTORY / 2 : MD_WFST_HISTORY / 2 - 1;
		for (int i = 0; i < count; i++)
			heads[branch] = md_history_fixture(
			    w, heads[branch],
			    (struct md_wfst_event){
				.ordinal = ordinal++,
				.output_label = md_label(branch ? "T" : "E"),
				.word_probability = .001,
				.domain = 1});
	}
	assert(!w->free_count);
	w->tick = 10000;
	w->count = 2;
	w->tokens[0][0].history[1] = heads[0];
	w->tokens[0][1].history[1] = heads[1];
	w->tokens[0][1].accumulated_score = -1;
	md_frames_feed(d, .001, 1);
	assert(w->forced_decisions == 1 && !w->recoveries);
	assert(w->text_commits == MD_WFST_HISTORY / 2 - 1);
	assert(w->free_count == MD_WFST_HISTORY - 1);
	assert(c->message.count &&
	       c->message.count <= MORSEDEC_MESSAGE_CAPACITY);
	for (size_t i = 0; i < c->message.count; i++)
		assert(c->message.chars[i].character == 'E');
	/* Freed nodes must be reusable after forced commitment. */
	md_pattern_feed(d, ".-", 5);
	md_frames_feed(d, .001, 300);
	assert(w->text_commits == MD_WFST_HISTORY / 2);
	assert(c->message.chars[c->message.count - 1].character == 'A');
	md_delete(d);
}

static void md_duration_check(void)
{
	struct morsedec_decoder *d = md_direct(8000, 40);
	struct md_channel *c = d->current;
	struct md_wfst_decoder *w = &c->wfst;
	for (int k = 0; k < 5; k++) {
		md_wfst_prepare_duration(d, c, k, MD_WFST_AGES);
		double survival = 1;
		for (int age = 1; age <= MD_WFST_AGES; age++) {
			double cont = w->continuation[k][age];
			double end = w->termination[k][age];
			assert(isfinite(cont) && isfinite(end) && cont < 0 &&
			       end < 0);
			assert(fabs(exp(cont) + exp(end) - 1) < 1e-6);
			if (age <= 10)
				survival *= exp(cont);
		}
		double sigma = sqrt(2 * c->stats.variance[k]);
		double expected =
		    erfc((log(10.5 * d->tick_scale) - c->stats.mean[k]) /
			 sigma) /
		    erfc((log(.5 * d->tick_scale) - c->stats.mean[k]) / sigma);
		assert(fabs(survival - expected) < 2e-5);
	}
	double previous = w->continuation[0][5];
	c->stats.mean[0] += log(2.0);
	c->stats.dirty = 1;
	md_frames_feed(d, .001, 1);
	md_wfst_prepare_duration(d, c, 0, 5);
	assert(!c->stats.dirty && w->continuation[0][5] > previous);
	/* A saturated state retains full source timing without growing RAM. */
	for (int i = 1; i < MD_WFST_GRAPH_NODES; i++) {
		if (md_wfst_nodes[i].duration_class != 1)
			continue;
		w->tokens[w->bank][0] =
		    (struct md_token){.current_node_id = (uint16_t)i,
				      .frame_age = MD_WFST_AGES,
				      .run_start = 1,
				      .mark_tick = MD_WFST_AGES};
		break;
	}
	w->count = 1;
	w->tick = MD_WFST_AGES;
	md_frames_feed(d, .999, 1);
	assert(w->tokens[w->bank][0].frame_age == MD_WFST_AGES);
	for (int i = 0; i < w->count; i++)
		assert(isfinite(w->tokens[w->bank][i].accumulated_score));
	md_delete(d);
}

static void md_stream_check(void)
{
	struct morsedec_decoder *d = md_direct(8000, 85);
	struct md_channel *c = d->current;
	md_frames_feed(d, .999, 5);
	md_frames_feed(d, .001, 2);
	assert(!c->wfst.text_commits); /* E may still continue into A. */
	md_frames_feed(d, .001, 3);
	md_allocation_guard = 1;
	md_wfst_advance(d, c, .999, c->wfst.tick + 1, 1);
	md_allocation_guard = 0;
	md_frames_feed(d, .999, 14);
	md_frames_feed(d, .001, 300);
	md_text_check(d, "A");
	assert(c->message.chars[0].low_snr);
	uint64_t commits = c->wfst.text_commits;
	md_frames_feed(d, .001, MD_WFST_AGES * 2);
	assert(c->wfst.text_commits == commits);
	md_frames_feed(d, NAN, 1);
	md_frames_feed(d, INFINITY, 1);
	md_frames_feed(d, 0, 1);
	md_frames_feed(d, 1, 1);
	uint64_t tick = c->wfst.tick;
	md_wfst_advance(d, c, .999, tick, 0);
	assert(c->wfst.tick == tick);
	c->wfst.count = 0;
	md_frames_feed(d, .001, 1);
	assert(c->wfst.recoveries == 1 && c->wfst.count == 1);
	md_text_check(d, "A");
	md_delete(d);
}

static void md_sentence_check(void)
{
	struct morsedec_decoder *d = md_direct(8000, 85);
	const char *text = "TEST SOS";
	for (size_t i = 0; text[i]; i++) {
		if (text[i] == ' ')
			continue;
		md_pattern_feed(d, md_code_find((unsigned char)text[i]), 5);
		if (text[i + 1])
			md_frames_feed(d, .001, text[i + 1] == ' ' ? 35 : 15);
	}
	md_frames_feed(d, .001, 300);
	md_text_check(d, text);
	const struct md_message *m = &d->current->message;
	assert(m->count == 7);
	for (size_t i = 1; i < m->count; i++)
		assert(m->chars[i].ordinal > m->chars[i - 1].ordinal);
	assert(m->chars[3].word_space_probability > .5);
	assert(d->current->stats.trained && !d->current->wfst.forced_decisions);
	md_delete(d);
}

/* Initial timing can be wrong; calibration must recover the first word too. */
static void md_acquisition_check(void)
{
	struct morsedec_decoder *d = md_direct(8000, 85);
	const char *text = "R TNX FER";
	for (size_t i = 0; text[i]; i++) {
		if (text[i] == ' ')
			continue;
		md_pattern_feed(d, md_code_find((unsigned char)text[i]), 9);
		if (text[i + 1])
			md_frames_feed(d, .001, text[i + 1] == ' ' ? 63 : 27);
	}
	md_frames_feed(d, .001, 300);
	md_allocation_guard = 1;
	md_wfst_flush(d, d->current);
	md_allocation_guard = 0;
	md_text_check(d, text);
	assert(d->current->wfst.acquisition_replays == 1);
	assert(!d->current->wfst.forced_decisions);
	md_delete(d);

	/* Acquisition is bounded even when a pinned carrier stays quiet first.
	 */
	d = md_direct(8000, 85);
	md_frames_feed(d, .001, MD_WFST_ACQUISITION + 123);
	assert(d->current->wfst.acquisition_drops == 123);
	assert(!d->current->wfst.text_commits);
	md_pattern_feed(d, ".", 5);
	md_frames_feed(d, .001, 300);
	md_text_check(d, "E");
	assert(d->current->wfst.acquisition_replays == 1);
	assert(d->current->stats.acquisition_marks == 1);
	assert(d->current->stats.count == 1);
	assert(!d->current->wfst.acquisition_count);
	md_delete(d);
}

static void md_audio_feed(struct morsedec_decoder *d, uint64_t *position,
			  size_t count, int mark)
{
	float samples[257];
	while (count) {
		size_t n = count < 257 ? count : 257;
		for (size_t i = 0; i < n; i++)
			samples[i] =
			    mark ? (float)(.3 * cos(2 * MD_PI * 700 *
						    (double)(*position + i) /
						    d->config.sample_rate))
				 : 0;
		md_allocation_guard = 1;
		assert(md_process(d, samples, n));
		md_allocation_guard = 0;
		*position += n;
		count -= n;
	}
}

static void md_audio_check(uint32_t rate)
{
	struct morsedec_decoder *d = md_direct(rate, 0);
	const char *text = "TEST SOS";
	uint64_t position = 0;
	size_t dot = rate * 3 / 50; /* 20 WPM, without an initial speed hint. */
	md_audio_feed(d, &position, 3 * rate, 0);
	for (size_t i = 0; text[i]; i++) {
		if (text[i] == ' ')
			continue;
		const char *code = md_code_find((unsigned char)text[i]);
		for (size_t j = 0; code[j]; j++) {
			md_audio_feed(d, &position,
				      code[j] == '.' ? dot : 3 * dot, 1);
			if (code[j + 1])
				md_audio_feed(d, &position, dot, 0);
		}
		if (text[i + 1])
			md_audio_feed(d, &position,
				      text[i + 1] == ' ' ? 7 * dot : 3 * dot,
				      0);
	}
	md_allocation_guard = 1;
	assert(md_flush(d));
	md_allocation_guard = 0;
	md_text_check(d, text);
	md_delete(d);
}

static void md_public_lifecycle_check(void)
{
	struct morsedec_decoder *d = md_direct(8000, 85);
	float samples[85] = {0};
	md_allocation_guard = 1;
	for (int i = 0; i < 100; i++)
		assert(md_process(d, samples, 85));
	assert(md_flush(d) && md_flush(d) && md_reset(d));
	assert(md_pin_frequency(d, 700));
	assert(!d->current->wfst.sealed && !d->current->wfst.text_commits);
	assert(md_process(d, samples, 85) && md_flush(d));
	md_allocation_guard = 0;
	md_delete(d);
}

int main(void)
{
	assert(md_wfst_graph_valid());
	assert(sizeof(md_wfst_edges) / sizeof(md_wfst_edges[0]) ==
	       MD_WFST_GRAPH_EDGES);
	const size_t hops[] = {40, 85, 160};
	for (size_t h = 0; h < sizeof(hops) / sizeof(hops[0]); h++)
		for (size_t i = 0; i < sizeof(md_codes) / sizeof(md_codes[0]);
		     i++) {
			md_pattern_check(md_codes[i].pattern, md_codes[i].text,
					 hops[h], 1);
			md_pattern_check(md_codes[i].pattern, md_codes[i].text,
					 hops[h], 0);
		}
	md_pattern_check("----", "*", 85, 0);
	uint64_t frames = md_frame_count;
	md_sentence_check();
	md_acquisition_check();
	md_convergence_check();
	md_pressure_check();
	md_duration_check();
	md_stream_check();
	md_public_lifecycle_check();
	md_audio_check(8000);
	md_audio_check(48000);
	printf("WFST: %zu bytes/channel, %d nodes, max emission delay %.0f "
	       "ms, %.2f CPU us/frame\n",
	       sizeof(struct md_wfst_decoder), MD_WFST_GRAPH_NODES,
	       1000 * md_latency,
	       (double)md_decode_time / CLOCKS_PER_SEC * 1e6 / frames);
	return 0;
}
