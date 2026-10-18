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
	float weight;
};
static const struct md_template dictionary[] = {
    {"K", 0.884f},	{"?", 0.3f},	     {"=", 0.26f},
    {"TU", 0.0522f},	{"TO", 0.0442f},     {"AR", 0.0361f},
    {"73", 0.0321f},	{"I", 0.0321f},	     {"OK", 0.03f},
    {"ES", 0.0281f},	{"SK", 0.0281f},     {"A", 0.0241f},
    {"SO", 0.0241f},	{"IN", 0.0201f},     {"IS", 0.0201f},
    {"R", 0.0201f},	{"THE", 0.0201f},    {"U", 0.0201f},
    {"ABT", 0.0161f},	{"NEW", 0.0161f},    {"YOU", 0.0161f},
    {"ON", 0.0061f},	{"BE", 3e-05f},	     {"DX", 0.003f},
    {"PSE", 0.003f},	{"KN", 0.003f},	     {"BK", 0.003f},
    {"TNX", 0.003f},	{"EE", 0.003f},	     {"GM", 0.003f},
    {"GE", 0.003f},	{"GA", 0.003f},	     {"GN", 0.003f},
    {"DR", 0.003f},	{"OM", 0.003f},	     {"UR", 0.003f},
    {"URS", 0.003f},	{"RST", 0.003f},     {"OP", 0.003f},
    {"MY", 0.003f},	{"NAME", 0.003f},    {"QTH", 0.003f},
    {"HR", 0.003f},	{"NR", 0.003f},	     {"RIG", 0.003f},
    {"ANT", 0.003f},	{"QSL", 0.003f},     {"CFM", 0.003f},
    {"GL", 0.003f},	{"HPE", 0.003f},     {"CU", 0.003f},
    {"AGN", 0.003f},	{"CUL", 0.003f},     {"RR", 0.003f},
    {"HI", 0.003f},	{"HEE", 0.003f},     {"QSY", 0.003f},
    {"QRT", 0.003f},	{"FER", 0.003f},     {"END", 0.003f},
    {"QSO", 0.003f},	{"OF", 0.003f},	     {"IT", 0.003f},
    {"ITS", 0.003f},	{"HVE", 0.003f},     {"HAS", 0.003f},
    {"HAD", 0.003f},	{"FB", 0.003f},	     {"CAN", 0.003f},
    {"ALL", 0.003f},	{"YES", 0.003f},     {"XYL", 0.003f},
    {"YEAR", 0.003f},	{"YRS", 0.003f},     {"WX", 0.003f},
    {"WEAK", 0.003f},	{"WESR", 0.003f},    {"WE", 0.003f},
    {"WRK", 0.003f},	{"WKD", 0.003f},     {"WAS", 0.003f},
    {"VY", 0.003f},	{"TKS", 0.003f},     {"SRI", 0.003f},
    {"QRU", 0.003f},	{"QSB", 0.003f},     {"OUR", 0.003f},
    {"OUT", 0.003f},	{"OLD", 0.003f},     {"NOW", 0.003f},
    {"NW", 0.003f},	{"NOT", 0.003f},     {"NICE", 0.003f},
    {"NEXT", 0.003f},	{"MUST", 0.003f},    {"MID", 0.003f},
    {"ME", 0.003f},	{"MAN", 0.003f},     {"LIKE", 0.003f},
    {"KNOW", 0.003f},	{"HOME", 0.003f},    {"HALF", 0.003f},
    {"ELSE", 0.003f},	{"DONE", 0.003f},    {"DEG", 0.003f},
    {"CIAO", 0.003f},	{"BTR", 0.003f},     {"BACK", 0.003f},
    {"AT", 0.003f},	{"ARE", 0.003f},     {"ALSO", 0.003f},
    {"AGE", 0.003f},	{"CPY", 0.003f},     {"TEMP", 0.003f},
    {"RPRT", 0.003f},	{"GB", 0.003f},	     {"QRX", 0.003f},
    {"KW", 0.003f},	{"CW", 0.003f},	     {"SSB", 0.003f},
    {"PSK", 0.003f},	{"RTTY", 0.003f},    {"QSB", 0.003f},
    {"NW", 0.003f},	{"QRU", 0.003f},     {"VIA", 0.003f},
    {"BURO", 0.003f},	{"NA", 0.003f},	     {"EU", 0.003f},
    {"CALL", 0.003f},	{"KHZ", 0.012f},     {"##F", 0.012f},
    {"SNOW", 0.012f},	{"SHE", 0.012f},     {"PLUS", 0.012f},
    {"##DB", 0.012f},	{"GOT", 0.012f},     {"SUM", 0.012f},
    {"FAMILY", 0.012f}, {"MTRS", 0.012f},    {"FREQ", 0.012f},
    {"AS", 0.012f},	{"PADDLE", 0.012f},  {"KEY", 0.012f},
    {"KEYER", 0.012f},	{"INTO", 0.012f},    {"TOP", 0.012f},
    {"ICOM", 0.012f},	{"RUNNING", 0.012f}, {"YOUR", 0.012f},
    {"USE", 0.012f},	{"WHEN", 0.012f},    {"SEND", 0.012f},
    {"LOT", 0.012f},	{"GET", 0.012f},     {"STN", 0.012f},
    {"WTS", 0.012f},	{"#TT", 0.012f},     {"#OO", 0.012f},
    {"#5O", 0.012f},	{"IOTA", 0.012f},    {"MUCH", 0.012f},
    {"THAT", 0.012f},	{"HIS", 0.012f},     {"EMAIL", 0.012f},
    {"THEY", 0.012f},	{"CANT", 0.012f},    {"BIG", 0.012f},
    {"GO", 0.012f},	{"WELL", 0.012f},    {"DIPOLE", 0.012f},
    {"QSLL", 0.012f},	{"CHAT", 0.012f},    {"TTL", 0.012f},
    {"VERT", 0.012f},	{"WID", 0.012f},     {"TRY", 0.012f},
    {"OFF", 0.012f},	{"G5RV", 0.012f},    {"GREAT", 0.012f},
    {"GUESS", 0.012f},	{"BY", 0.012f},	     {"OPR", 0.012f},
    {"BT", 0.012f},	{"ONLY", 0.012f},    {"LOOP", 0.012f},
    {"DAY", 0.012f},	{"PUT", 0.012f},     {"NEED", 0.012f},
    {"WORK", 0.012f},	{"BED", 0.012f},     {"ELE", 0.012f},
    {"QUAD", 0.012f},	{"YAGI", 0.012f},    {"USA", 0.012f},
    {"BEAM", 0.012f},	{"RRR", 0.012f},     {"ARE", 0.012f},
    {"FAR", 0.012f},	{"THEN", 0.012f},    {"BAND", 0.012f},
    {"SOLID", 0.012f},	{"LID", 0.012f},     {"JUST", 0.012f},
    {"BTU", 0.012f},	{"HW", 0.012f},	     {"BAD", 0.012f},
    {"PHONE", 0.012f},	{"COM", 0.012f},     {"SAME", 0.012f},
    {"NO", 0.012f},	{"WILL", 0.012f},    {"SIG", 0.012f},
    {"SIGS", 0.012f},	{"SURE", 0.012f},    {"QRM", 0.012f},
    {"QRN", 0.012f},	{"MNI", 0.012f},     {"HAM", 0.012f},
    {"GUD", 0.012f},	{"AND", 0.012f},     {"BEEN", 0.012f},
    {"BEST", 0.012f},	{"BUT", 0.012f},     {"HW?", 0.012f},
    {"THIS", 0.012f},	{"WHILE", 0.012f},
};
