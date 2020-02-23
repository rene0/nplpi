// Copyright 2013-2019 René Ladan
// SPDX-License-Identifier: BSD-2-Clause

#ifndef NPLPI_DECODE_TIME_H
#define NPLPI_DECODE_TIME_H

#include <stdbool.h>
struct tm;

/** Minute length state */
enum eDT_length {
	/** minute length ok */
	emin_ok,
	/** minute too short */
	emin_short,
	/** minute too long */
	emin_long
};

/** State of the decoded data/time values */
enum eDT_tval {
	/** value ok */
	eval_ok,
	/** bcd error */
	eval_bcd,
	/** parity error */
	eval_parity,
	/** value ok but jumped */
	eval_jump
};

/** Daylight saving time state */
enum eDT_DST {
	/** daylight saving time ok **/
	eDST_ok,
	/** unexpected daylight saving time change */
	eDST_jump,
	/** daylight saving time just changed */
	eDST_done
};

/** Leap second state */
enum eDT_leapsecond {
	/** no leap second */
	els_none,
	/**
	 * ??? leap second should always be 0 if present :
	 */
	els_one,
	/** leap second just processed */
	els_done
};

/** Structure containing the state of all decoded information of this minute */
struct DT_result {
	/**
	 * bit 0 must always be 500 ms long
	 */
	bool bit0_ok;
	/**
	 * bits 52 and 59 must always be 100 ms long
	 */
	bool bit52_ok;
	bool bit59_ok;
	/** minute length ok ? */
	enum eDT_length minute_length;
	/** minute value ok ? */
	enum eDT_tval minute_status;
	/** hour value ok ? */
	enum eDT_tval hour_status;
	/** day value ok ? */
	enum eDT_tval mday_status;
	/** weekday value ok ? */
	enum eDT_tval wday_status;
	/** month value ok ? */
	enum eDT_tval month_status;
	/** year value ok ? */
	enum eDT_tval year_status;
	/** DST ok ? */
	enum eDT_DST dst_status;
	/** leap second ok ? */
	enum eDT_leapsecond leapsecond_status;
	/** DST announcement ? */
	bool dst_announce;
};

/**
 * Decodes the current time from the internal bit buffer.
 *
 * The current time is first increased using add_minute(), and only if the
 * parities and other checks match these values are replaced by their
 * calculated counterparts.
 *
 * @param init_min Indicates whether the state of the decoder is initial:
 *   0 = normal, first two minute marks passed
 *   1 = first minute mark passed
 *   2 = just starting
 * @param minlen The length of this minute in bits (normally 59 or 60 in case
 * of a leap second).
 * @param acc_minlen The accumulated minute length of this minute in
 * milliseconds.
 * @param buffer The bit buffer.
 * @param time The current time, to be updated.
 * @return A structure containing the results of all the checks performed on
 * the calculated time.
 */
struct DT_result decode_time(unsigned init_min, int minlen, unsigned acc_minlen,
    const int buffer[], struct tm * const time);

#endif
