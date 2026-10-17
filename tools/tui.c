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
#include "tui.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef MORSEDEC_TUI

#include <math.h>

/*
 * The ncursesw pkg-config flags add its header directory, so <curses.h>
 * already resolves to the wide-character header there; other installations
 * ship the same file below an ncurses subdirectory.
 */
#if defined(__has_include)
#if __has_include(<curses.h>)
#include <curses.h>
#elif __has_include(<ncursesw/curses.h>)
#include <ncursesw/curses.h>
#elif __has_include(<ncurses/curses.h>)
#include <ncurses/curses.h>
#else
#include <ncurses.h>
#endif
#else
#include <curses.h>
#endif

/* Message revisions kept for the scrollback pane. */
#define MD_TUI_SCROLLBACK 256
/* Width of the channel pane, borders included. */
#define MD_TUI_CHANNEL_COLUMNS 37
/* Smallest terminal that still fits both panes side by side. */
#define MD_TUI_MIN_ROWS 6
#define MD_TUI_MIN_COLUMNS 56
#define MD_TUI_PAIR_HEADER 1

struct md_tui_message {
	uint64_t id;
	double time;
	double frequency;
	char text[MORSEDEC_MESSAGE_CAPACITY * 3 + 1];
};

struct md_tui {
	SCREEN *screen;
	FILE *input, *output;
	WINDOW *channels, *messages;
	struct md_tui_message *log;
	size_t log_size, log_count, log_start;
	size_t scroll, channel_top, lines;
	int quit, colored;
	int rows, columns;
	char title[128];
};

static struct md_tui_message *log_entry(const struct md_tui *tui, size_t index)
{
	return &tui->log[(tui->log_start + index) % tui->log_size];
}

/* Decoded text is Latin-1; the panes are single byte, so anything outside
 * printable ASCII would otherwise disturb the cursor arithmetic. */
static void copy_printable(char *out, size_t size, const char *text)
{
	size_t i;

	for (i = 0; i + 1 < size && text[i]; i++) {
		unsigned char character = (unsigned char)text[i];

		out[i] =
		    character >= 32 && character < 127 ? (char)character : '.';
	}
	out[i] = '\0';
}

static const char *state_label(enum morsedec_state state)
{
	switch (state) {
	case MORSEDEC_STATE_PAUSE:
		return "pause";
	case MORSEDEC_STATE_DECODING:
		return "decode";
	case MORSEDEC_STATE_LOCKED_PAUSE:
		return "hold";
	case MORSEDEC_STATE_LOCKED_DECODING:
		return "locked";
	case MORSEDEC_STATE_NONE:
		break;
	}
	return "-";
}

static FILE *open_terminal(const char *device, const char *mode)
{
#ifdef _WIN32
	FILE *file = fopen(device, mode);

	if (file)
		return file;
#else
	(void)device;
#endif
	return fopen("/dev/tty", mode);
}

/* Replace the top border with a pane title, clipped to the pane. */
static void draw_title(WINDOW *win, int columns, const char *title)
{
	wattron(win, A_BOLD);
	mvwaddnstr(win, 0, 2, title, columns - 3);
	wattroff(win, A_BOLD);
}

static void draw_channels(struct md_tui *tui, morsedec_decoder_t *decoder,
			  const struct morsedec_geometry *geometry)
{
	WINDOW *win = tui->channels;
	enum morsedec_state state = md_get_state(decoder);
	double current = md_get_frequency(decoder);
	size_t count = md_get_channel_count(decoder);
	size_t top = 0, visible, index;
	int rows, columns, width, row, marker = -1;
	char text[96], title[32];

	getmaxyx(win, rows, columns);
	width = columns - 2;
	werase(win);
	box(win, 0, 0);
	snprintf(title, sizeof(title), " CH %zu ", count);
	draw_title(win, columns, title);
	if (rows < 3 || width < 20)
		return;
	for (index = 0; index < count; index++) {
		struct morsedec_channel channel;

		if (!md_get_channel(decoder, index, &channel))
			continue;
		if (state != MORSEDEC_STATE_NONE &&
		    fabs(channel.frequency - current) <=
			geometry->bin_spacing / 2)
			marker = (int)index;
	}
	visible = (size_t)(rows - 2);
	if (count > visible) {
		if (marker >= 0) {
			if ((size_t)marker < tui->channel_top)
				tui->channel_top = (size_t)marker;
			else if ((size_t)marker >= tui->channel_top + visible)
				tui->channel_top = (size_t)marker - visible + 1;
		}
		if (tui->channel_top > count - visible)
			tui->channel_top = count - visible;
		top = tui->channel_top;
	} else {
		tui->channel_top = 0;
	}
	mvwaddnstr(win, 1, 1, "   FREQ/Hz   SNR   WPM  STATE", width);
	row = 2;
	for (index = top; index < count && row < rows - 1; index++, row++) {
		struct morsedec_channel channel;
		char snr[16], wpm[16];

		if (!md_get_channel(decoder, index, &channel))
			continue;
		if (isnan(channel.snr))
			snprintf(snr, sizeof(snr), "-");
		else
			snprintf(snr, sizeof(snr), "%.1f", channel.snr);
		if (isnan(channel.wpm))
			snprintf(wpm, sizeof(wpm), "-");
		else
			snprintf(wpm, sizeof(wpm), "%.1f", channel.wpm);
		snprintf(text, sizeof(text), "%c %8.1f %5s %5s  %-8s",
			 (int)index == marker ? '>' : ' ', channel.frequency,
			 snr, wpm, state_label(channel.state));
		if ((int)index == marker)
			wattron(win, A_BOLD);
		mvwaddnstr(win, row, 1, text, width);
		if ((int)index == marker)
			wattroff(win, A_BOLD);
	}
}

static size_t entry_line(const struct md_tui_message *entry, char *out,
			 size_t size)
{
	int length = snprintf(out, size, "%7.2fs%9.1fHz #%-3llu %s",
			      entry->time, entry->frequency,
			      (unsigned long long)entry->id, entry->text);
	size_t written = length < 0 ? 0 : (size_t)length;

	return written >= size ? size - 1 : written;
}

static size_t entry_height(const struct md_tui_message *entry, int width)
{
	char line[sizeof(entry->text) + 64];
	size_t length = entry_line(entry, line, sizeof(line));

	return length ? (length + (size_t)width - 1) / (size_t)width : 1;
}

/* Write the wrapped entry from screen row `row`, dropping the first `clip`
 * wrapped rows so a partially scrolled entry stays aligned, and never
 * writing more than `limit` rows. */
static void draw_entry(WINDOW *win, int row, int width, size_t clip,
		       size_t limit, const char *line, size_t length)
{
	size_t offset = 0, dropped = 0, written = 0;

	while (offset < length && written < limit) {
		size_t chunk = length - offset;

		if (chunk > (size_t)width)
			chunk = (size_t)width;
		if (dropped < clip) {
			dropped++;
		} else {
			mvwaddnstr(win, 1 + row + (int)written, 1,
				   line + offset, (int)chunk);
			written++;
		}
		offset += chunk;
	}
}

static void draw_messages(struct md_tui *tui)
{
	WINDOW *win = tui->messages;
	size_t total = 0, skip = 0, position = 0, index;
	int rows, columns, width, available, row = 0;

	getmaxyx(win, rows, columns);
	width = columns - 2;
	werase(win);
	box(win, 0, 0);
	draw_title(win, columns, " MESSAGES ");
	tui->lines = 0;
	if (rows < 3 || width < 4)
		return;
	available = rows - 2;
	for (index = 0; index < tui->log_count; index++)
		total += entry_height(log_entry(tui, index), width);
	tui->lines = total;
	if (total <= (size_t)available) {
		tui->scroll = 0;
	} else {
		if (tui->scroll > total - (size_t)available)
			tui->scroll = total - (size_t)available;
		skip = total - (size_t)available - tui->scroll;
	}
	for (index = 0; index < tui->log_count && row < available; index++) {
		const struct md_tui_message *entry = log_entry(tui, index);
		char line[sizeof(entry->text) + 64];
		size_t length = entry_line(entry, line, sizeof(line));
		size_t height =
		    length ? (length + (size_t)width - 1) / (size_t)width : 1;
		size_t clip = 0, drawn;

		if (position + height <= skip) {
			position += height;
			continue;
		}
		if (position < skip)
			clip = skip - position;
		drawn = height - clip;
		if (drawn > (size_t)(available - row))
			drawn = (size_t)(available - row);
		draw_entry(win, row, width, clip, drawn, line, length);
		row += (int)drawn;
		position += height;
	}
}

static void pad(char *text, size_t size, int width)
{
	size_t length = strlen(text);

	while (length + 1 < size && length < (size_t)width)
		text[length++] = ' ';
	text[length] = '\0';
}

static void draw_bar(int row, const char *text, int columns, int colored)
{
	if (colored)
		attron(COLOR_PAIR(MD_TUI_PAIR_HEADER));
	else
		attron(A_REVERSE);
	move(row, 0);
	clrtoeol();
	mvaddnstr(row, 0, text, columns);
	if (colored)
		attroff(COLOR_PAIR(MD_TUI_PAIR_HEADER));
	else
		attroff(A_REVERSE);
}

static void draw_bars(struct md_tui *tui, morsedec_decoder_t *decoder,
		      double time, int all)
{
	char text[192];

	if (!tui->channels)
		return;
	snprintf(text, sizeof(text), " %s   %.1f s   %zu channel(s)   %s ",
		 tui->title, time, md_get_channel_count(decoder),
		 all ? "all channels" : "selected channel");
	pad(text, sizeof(text), tui->columns);
	draw_bar(0, text, tui->columns, tui->colored);
	snprintf(text, sizeof(text),
		 " q quit   c clear   Up/Down PgUp/PgDn Home/End scroll   "
		 "%zu line(s)%s ",
		 tui->lines, tui->scroll ? " [scrolled back]" : "");
	pad(text, sizeof(text), tui->columns);
	draw_bar(tui->rows - 1, text, tui->columns, tui->colored);
}

static void scroll_by(struct md_tui *tui, size_t lines, int forward)
{
	if (forward)
		tui->scroll = tui->scroll > lines ? tui->scroll - lines : 0;
	else
		tui->scroll = tui->scroll + lines < tui->lines
				  ? tui->scroll + lines
				  : tui->lines;
}

static int layout(struct md_tui *tui)
{
	int rows, columns, body, right;

	getmaxyx(stdscr, rows, columns);
	if (tui->channels && rows == tui->rows && columns == tui->columns)
		return 0;
	if (tui->channels) {
		delwin(tui->channels);
		delwin(tui->messages);
		tui->channels = tui->messages = NULL;
	}
	if (rows < MD_TUI_MIN_ROWS || columns < MD_TUI_MIN_COLUMNS) {
		errno = ENOSPC;
		return -1;
	}
	tui->rows = rows;
	tui->columns = columns;
	body = rows - 2;
	right = columns - MD_TUI_CHANNEL_COLUMNS;
	tui->channels = newwin(body, MD_TUI_CHANNEL_COLUMNS, 1, 0);
	tui->messages = newwin(body, right, 1, MD_TUI_CHANNEL_COLUMNS);
	if (!tui->channels || !tui->messages) {
		if (tui->channels)
			delwin(tui->channels);
		if (tui->messages)
			delwin(tui->messages);
		tui->channels = tui->messages = NULL;
		errno = ENOMEM;
		return -1;
	}
	box(tui->channels, 0, 0);
	box(tui->messages, 0, 0);
	clearok(stdscr, TRUE);
	return 0;
}

struct md_tui *md_tui_start(const char *program, const char *stream)
{
	struct md_tui *tui;
	FILE *input, *output;
	SCREEN *screen;

	input = open_terminal("CONIN$", "r");
	output = open_terminal("CONOUT$", "w");
	if (!input || !output) {
		if (input)
			fclose(input);
		if (output)
			fclose(output);
		errno = ENOTTY;
		return NULL;
	}
	screen = newterm(NULL, output, input);
	if (!screen) {
		fclose(input);
		fclose(output);
		errno = ENOTTY;
		return NULL;
	}
	tui = calloc(1, sizeof(*tui));
	if (tui)
		tui->log = calloc(MD_TUI_SCROLLBACK, sizeof(*tui->log));
	if (!tui || !tui->log) {
		free(tui);
		delscreen(screen);
		fclose(input);
		fclose(output);
		errno = ENOMEM;
		return NULL;
	}
	tui->screen = screen;
	tui->input = input;
	tui->output = output;
	tui->log_size = MD_TUI_SCROLLBACK;
	snprintf(tui->title, sizeof(tui->title), "%s  %s", program, stream);
	set_term(screen);
	cbreak();
	noecho();
	nonl();
	intrflush(stdscr, FALSE);
	keypad(stdscr, TRUE);
	nodelay(stdscr, TRUE);
	curs_set(0);
	if (has_colors()) {
		start_color();
		init_pair(MD_TUI_PAIR_HEADER, COLOR_BLACK, COLOR_CYAN);
		tui->colored = 1;
	}
	refresh();
	return tui;
}

void md_tui_stop(struct md_tui *tui)
{
	if (!tui)
		return;
	if (tui->screen) {
		set_term(tui->screen);
		if (tui->channels) {
			delwin(tui->channels);
			delwin(tui->messages);
		}
		curs_set(1);
		endwin();
		delscreen(tui->screen);
	}
	free(tui->log);
	if (tui->input)
		fclose(tui->input);
	if (tui->output)
		fclose(tui->output);
	free(tui);
}

int md_tui_poll(struct md_tui *tui)
{
	size_t page;
	int key;

	if (!tui)
		return 0;
	page = tui->rows > 2 ? (size_t)(tui->rows - 2) : 1;
	while ((key = wgetch(stdscr)) != ERR) {
		switch (key) {
		case 'q':
		case 'Q':
		case 27:
			tui->quit = 1;
			break;
		case 'c':
		case 'C':
			tui->log_count = 0;
			tui->log_start = 0;
			tui->scroll = 0;
			break;
		case KEY_UP:
			scroll_by(tui, 1, 0);
			break;
		case KEY_DOWN:
			scroll_by(tui, 1, 1);
			break;
		case KEY_PPAGE:
			scroll_by(tui, page, 0);
			break;
		case KEY_NPAGE:
			scroll_by(tui, page, 1);
			break;
		case KEY_HOME:
			tui->scroll = tui->lines;
			break;
		case KEY_END:
			tui->scroll = 0;
			break;
		default:
			break;
		}
	}
	return tui->quit;
}

void md_tui_message(struct md_tui *tui, double time, double frequency,
		    uint64_t id, const char *text)
{
	struct md_tui_message *entry;

	if (!tui || !tui->log_size || !text)
		return;
	if (tui->log_count && log_entry(tui, tui->log_count - 1)->id == id) {
		/* The newest line holds this channel's previous snapshot. */
		entry = log_entry(tui, tui->log_count - 1);
	} else if (tui->log_count < tui->log_size) {
		entry = &tui->log[(tui->log_start + tui->log_count) %
				  tui->log_size];
		tui->log_count++;
	} else {
		entry = &tui->log[tui->log_start];
		tui->log_start = (tui->log_start + 1) % tui->log_size;
	}
	entry->id = id;
	entry->time = time;
	entry->frequency = frequency;
	copy_printable(entry->text, sizeof(entry->text), text);
}

void md_tui_draw(struct md_tui *tui, morsedec_decoder_t *decoder,
		 const struct morsedec_geometry *geometry, double time, int all)
{
	if (!tui || !decoder || !geometry)
		return;
	if (layout(tui)) {
		move(0, 0);
		clrtoeol();
		mvaddnstr(0, 0, "Terminal too small for the TUI layout",
			  getmaxx(stdscr));
		refresh();
		return;
	}
	draw_channels(tui, decoder, geometry);
	draw_messages(tui);
	draw_bars(tui, decoder, time, all);
	wnoutrefresh(stdscr);
	wnoutrefresh(tui->channels);
	wnoutrefresh(tui->messages);
	doupdate();
}

#else /* !MORSEDEC_TUI */

struct md_tui;

struct md_tui *md_tui_start(const char *program, const char *stream)
{
	(void)program;
	(void)stream;
	fputs("This build has no TUI support; configure Meson with "
	      "-Dtui=enabled and install ncurses\n",
	      stderr);
	errno = ENOSYS;
	return NULL;
}

void md_tui_stop(struct md_tui *tui)
{
	(void)tui;
}

int md_tui_poll(struct md_tui *tui)
{
	(void)tui;
	return 0;
}

void md_tui_message(struct md_tui *tui, double time, double frequency,
		    uint64_t id, const char *text)
{
	(void)tui;
	(void)time;
	(void)frequency;
	(void)id;
	(void)text;
}

void md_tui_draw(struct md_tui *tui, morsedec_decoder_t *decoder,
		 const struct morsedec_geometry *geometry, double time, int all)
{
	(void)tui;
	(void)decoder;
	(void)geometry;
	(void)time;
	(void)all;
}

#endif
