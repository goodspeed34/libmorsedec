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
/* Morse character and procedural-signal lookup table. */
static const struct {
	const char *text, *pattern;
} md_codes[] = {
    {"1", ".----"},   {"2", "..---"},	  {"3", "...--"},    {"4", "....-"},
    {"5", "....."},   {"6", "-...."},	  {"7", "--..."},    {"8", "---.."},
    {"9", "----."},   {"0", "-----"},	  {"A", ".-"},	     {"B", "-..."},
    {"C", "-.-."},    {"D", "-.."},	  {"E", "."},	     {"F", "..-."},
    {"G", "--."},     {"H", "...."},	  {"I", ".."},	     {"J", ".---"},
    {"K", "-.-"},     {"L", ".-.."},	  {"M", "--"},	     {"N", "-."},
    {"O", "---"},     {"P", ".--."},	  {"Q", "--.-"},     {"R", ".-."},
    {"S", "..."},     {"T", "-"},	  {"U", "..-"},	     {"V", "...-"},
    {"W", ".--"},     {"X", "-..-"},	  {"Y", "-.--"},     {"Z", "--.."},
    {"/", "-..-."},   {".", ".-.-.-"},	  {",", "--..--"},   {"?", "..--.."},
    {"=", "-...-"},   {"sk", "...-.-"},	  {"ar", ".-.-."},   {"as", ".-..."},
    {"kn", "-.--."},  {"cq", "-.-.--.-"}, {"bk", "-...-.-"}, {"dx", "-..-..-"},
    {"=", "-....-"},  {"=", "-.....-"},	  {"?", "...--.."},  {"?", "..--..."},
    {"?", "..--."},   {"?", ".--.."},	  {"1", ".-----"},   {"2", "..----"},
    {"2", "...---"},  {"3", "....--"},	  {"4", ".....-"},   {"5", "......"},
    {"5", "......."}, {"6", "-....."},	  {"7", "--...."},   {"8", "---..."},
};
