/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Preserve the extraction module's private hooks without changing its DSP. */
#include "internal.h"

void md_segment_init(const struct morsedec_decoder *d, struct md_channel *c)
{
	md_wfst_init(d, c);
}

void md_segment_step(const struct morsedec_decoder *d, struct md_channel *c,
		     int bit, double probability, int drop)
{
	md_wfst_advance(d, c, probability, c->seg.step, bit);
	if (drop)
		md_wfst_check_convergence(d, c);
}
