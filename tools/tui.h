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
#ifndef MORSEDEC_TUI_H
#define MORSEDEC_TUI_H

#include "audio-common.h"

/*
 * Optional full-screen view for the stdin tools. Standard input carries the
 * sample stream, so the terminal is opened as a separate device; the caller
 * keeps using read() on standard input. Every entry point tolerates a NULL
 * view, and a build without ncurses returns NULL from md_tui_start().
 */
struct md_tui;

/* program names the tool, stream describes its input; both are copied. */
struct md_tui *md_tui_start(const char *program, const char *stream);
void md_tui_stop(struct md_tui *tui);
/* Drain pending key presses; nonzero once the user asked to quit. */
int md_tui_poll(struct md_tui *tui);
/* Record a message snapshot, revising the newest line when the channel of
 * that line is the same one. */
void md_tui_message(struct md_tui *tui, double time, double frequency,
		    uint64_t id, const char *text);
/* Repaint from the current decoder state. */
void md_tui_draw(struct md_tui *tui, morsedec_decoder_t *decoder,
		 const struct morsedec_geometry *geometry, double time,
		 int all);
#endif
