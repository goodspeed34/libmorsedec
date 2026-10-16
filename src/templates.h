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
struct md_template {
	const char *text;
	double weight;
};
static const struct md_template dictionary[] = {
    {"K", 0.884},	{"?", 0.3},	  {"=", 0.26},	     {"TU", 0.0522},
    {"TO", 0.0442},	{"AR", 0.0361},	  {"73", 0.0321},    {"I", 0.0321},
    {"OK", 0.03},	{"ES", 0.0281},	  {"SK", 0.0281},    {"A", 0.0241},
    {"SO", 0.0241},	{"IN", 0.0201},	  {"IS", 0.0201},    {"R", 0.0201},
    {"THE", 0.0201},	{"U", 0.0201},	  {"ABT", 0.0161},   {"NEW", 0.0161},
    {"YOU", 0.0161},	{"ON", 0.0061},	  {"BE", 3e-05},     {"DX", 0.003},
    {"PSE", 0.003},	{"KN", 0.003},	  {"BK", 0.003},     {"TNX", 0.003},
    {"EE", 0.003},	{"GM", 0.003},	  {"GE", 0.003},     {"GA", 0.003},
    {"GN", 0.003},	{"DR", 0.003},	  {"OM", 0.003},     {"UR", 0.003},
    {"URS", 0.003},	{"RST", 0.003},	  {"OP", 0.003},     {"MY", 0.003},
    {"NAME", 0.003},	{"QTH", 0.003},	  {"HR", 0.003},     {"NR", 0.003},
    {"RIG", 0.003},	{"ANT", 0.003},	  {"QSL", 0.003},    {"CFM", 0.003},
    {"GL", 0.003},	{"HPE", 0.003},	  {"CU", 0.003},     {"AGN", 0.003},
    {"CUL", 0.003},	{"RR", 0.003},	  {"HI", 0.003},     {"HEE", 0.003},
    {"QSY", 0.003},	{"QRT", 0.003},	  {"FER", 0.003},    {"END", 0.003},
    {"QSO", 0.003},	{"OF", 0.003},	  {"IT", 0.003},     {"ITS", 0.003},
    {"HVE", 0.003},	{"HAS", 0.003},	  {"HAD", 0.003},    {"FB", 0.003},
    {"CAN", 0.003},	{"ALL", 0.003},	  {"YES", 0.003},    {"XYL", 0.003},
    {"YEAR", 0.003},	{"YRS", 0.003},	  {"WX", 0.003},     {"WEAK", 0.003},
    {"WESR", 0.003},	{"WE", 0.003},	  {"WRK", 0.003},    {"WKD", 0.003},
    {"WAS", 0.003},	{"VY", 0.003},	  {"TKS", 0.003},    {"SRI", 0.003},
    {"QRU", 0.003},	{"QSB", 0.003},	  {"OUR", 0.003},    {"OUT", 0.003},
    {"OLD", 0.003},	{"NOW", 0.003},	  {"NW", 0.003},     {"NOT", 0.003},
    {"NICE", 0.003},	{"NEXT", 0.003},  {"MUST", 0.003},   {"MID", 0.003},
    {"ME", 0.003},	{"MAN", 0.003},	  {"LIKE", 0.003},   {"KNOW", 0.003},
    {"HOME", 0.003},	{"HALF", 0.003},  {"ELSE", 0.003},   {"DONE", 0.003},
    {"DEG", 0.003},	{"CIAO", 0.003},  {"BTR", 0.003},    {"BACK", 0.003},
    {"AT", 0.003},	{"ARE", 0.003},	  {"ALSO", 0.003},   {"AGE", 0.003},
    {"CPY", 0.003},	{"TEMP", 0.003},  {"RPRT", 0.003},   {"GB", 0.003},
    {"QRX", 0.003},	{"KW", 0.003},	  {"CW", 0.003},     {"SSB", 0.003},
    {"PSK", 0.003},	{"RTTY", 0.003},  {"QSB", 0.003},    {"NW", 0.003},
    {"QRU", 0.003},	{"VIA", 0.003},	  {"BURO", 0.003},   {"NA", 0.003},
    {"EU", 0.003},	{"CALL", 0.003},  {"KHZ", 0.012},    {"##F", 0.012},
    {"SNOW", 0.012},	{"SHE", 0.012},	  {"PLUS", 0.012},   {"##DB", 0.012},
    {"GOT", 0.012},	{"SUM", 0.012},	  {"FAMILY", 0.012}, {"MTRS", 0.012},
    {"FREQ", 0.012},	{"AS", 0.012},	  {"PADDLE", 0.012}, {"KEY", 0.012},
    {"KEYER", 0.012},	{"INTO", 0.012},  {"TOP", 0.012},    {"ICOM", 0.012},
    {"RUNNING", 0.012}, {"YOUR", 0.012},  {"USE", 0.012},    {"WHEN", 0.012},
    {"SEND", 0.012},	{"LOT", 0.012},	  {"GET", 0.012},    {"STN", 0.012},
    {"WTS", 0.012},	{"#TT", 0.012},	  {"#OO", 0.012},    {"#5O", 0.012},
    {"IOTA", 0.012},	{"MUCH", 0.012},  {"THAT", 0.012},   {"HIS", 0.012},
    {"EMAIL", 0.012},	{"THEY", 0.012},  {"CANT", 0.012},   {"BIG", 0.012},
    {"GO", 0.012},	{"WELL", 0.012},  {"DIPOLE", 0.012}, {"QSLL", 0.012},
    {"CHAT", 0.012},	{"TTL", 0.012},	  {"VERT", 0.012},   {"WID", 0.012},
    {"TRY", 0.012},	{"OFF", 0.012},	  {"G5RV", 0.012},   {"GREAT", 0.012},
    {"GUESS", 0.012},	{"BY", 0.012},	  {"OPR", 0.012},    {"BT", 0.012},
    {"ONLY", 0.012},	{"LOOP", 0.012},  {"DAY", 0.012},    {"PUT", 0.012},
    {"NEED", 0.012},	{"WORK", 0.012},  {"BED", 0.012},    {"ELE", 0.012},
    {"QUAD", 0.012},	{"YAGI", 0.012},  {"USA", 0.012},    {"BEAM", 0.012},
    {"RRR", 0.012},	{"ARE", 0.012},	  {"FAR", 0.012},    {"THEN", 0.012},
    {"BAND", 0.012},	{"SOLID", 0.012}, {"LID", 0.012},    {"JUST", 0.012},
    {"BTU", 0.012},	{"HW", 0.012},	  {"BAD", 0.012},    {"PHONE", 0.012},
    {"COM", 0.012},	{"SAME", 0.012},  {"NO", 0.012},     {"WILL", 0.012},
    {"SIG", 0.012},	{"SIGS", 0.012},  {"SURE", 0.012},   {"QRM", 0.012},
    {"QRN", 0.012},	{"MNI", 0.012},	  {"HAM", 0.012},    {"GUD", 0.012},
    {"AND", 0.012},	{"BEEN", 0.012},  {"BEST", 0.012},   {"BUT", 0.012},
    {"HW?", 0.012},	{"THIS", 0.012},  {"WHILE", 0.012},
};
