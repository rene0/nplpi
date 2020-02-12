// Copyright 2013-2019 René Ladan
// SPDX-License-Identifier: BSD-2-Clause

#include "decode_time.h"

#include "calendar.h"

#include <stdbool.h>
#include <string.h>
#include <time.h>

static int dst_count, minute_count;
static struct DT_result dt_res;

static bool
getpar(const int buffer[], unsigned start, unsigned stop, unsigned parity)
{
	int par = 0;

	for (unsigned i = start; i <= stop; i++) {
		par += (buffer[i] & 1); /* check A bits */
	}
	par += (buffer[parity] >> 1); /* parity is B bit */
	return (par & 1) == 1;
}

static int
getbcd(const int buffer[], unsigned start, unsigned stop)
{
	int mul = 1, val = 0;

	for (unsigned i = stop; i >= start; i--) {
		val += mul * (buffer[i] & 1); /* A bits only */
		mul *= 2;
		if (mul == 16) {
			if (val > 9) {
				return 100;
			}
			mul = 10;
		}
	}
	return val;
}

static bool
check_time_sanity(int minlen, const int buffer[])
{
	if (minlen == -1 || minlen > 61) {
		dt_res.minute_length = emin_long;
	} else if (minlen < 59) {
		dt_res.minute_length = emin_short;
	} else {
		dt_res.minute_length = emin_ok;
	}

	dt_res.dst_status = eDST_ok;

	dt_res.bit0_ok = buffer[0] == 4;
	dt_res.bit59_ok = buffer[59] == 0;

	/* only decode if set */
	return (dt_res.minute_length == emin_ok) && dt_res.bit0_ok &&
	    dt_res.bit59_ok;
}

static void
handle_special_bits(const int buffer[])
{
}

static int
increase_old_time(unsigned init_min, int minlen, unsigned acc_minlen,
    struct tm * const time)
{
	static unsigned acc_minlen_partial;
	int increase;

	/* See if there are any partial / split minutes to be combined: */
	if (acc_minlen <= 59000) {
		acc_minlen_partial += acc_minlen;
		if (acc_minlen_partial >= 60000) {
			acc_minlen = acc_minlen_partial;
			acc_minlen_partial %= 60000;
		}
	}
	/* Calculate number of minutes to increase time with: */
	increase = acc_minlen / 60000;
	if (acc_minlen >= 60000) {
		acc_minlen_partial %= 60000;
	}
	/* Account for complete minutes with a short acc_minlen: */
	if (acc_minlen % 60000 > 59000) {
		increase++;
		acc_minlen_partial %= 60000;
	}

	/* There is no previous time on the very first (partial) minute: */
	if (init_min < 2) {
		for (int i = increase; increase > 0 && i > 0; i--) {
			*time = add_minute(*time, dt_res.dst_announce);
		}
		for (int i = increase; increase < 0 && i < 0; i++) {
			*time = substract_minute(*time, dt_res.dst_announce);
		}
	}
	return increase;
}

static unsigned
calculate_date_time(unsigned init_min, unsigned errflags, int increase,
    const int buffer[], struct tm time, struct tm * const newtime)
{
	int tmp0, tmp1;
	bool p1, p2, p3, p4;
	int centofs;

	p1 = getpar(buffer, 17, 24, 54); /* year */
	tmp0 = getbcd(buffer, 17, 24);
	if (!p1) {
		dt_res.year_status = eval_parity;
	} else if (tmp0 > 99) {
		dt_res.year_status = eval_bcd;
		p1 = false;
	} else {
		dt_res.year_status = eval_ok;
	}
	if ((init_min == 2 || increase != 0) && p1 && errflags == 0) {
		newtime->tm_year = tmp0;
		/* check for jumps once month and mday are known and correct */
	}

	p2 = getpar(buffer, 25, 35, 55); /* month and mday */
	tmp0 = getbcd(buffer, 25, 29);
	tmp1 = getbcd(buffer, 30, 35);
	if (!p2) {
		dt_res.month_status = eval_parity;
		dt_res.mday_status = eval_parity;
	} else {
		if (tmp0 == 0 || tmp0 > 12) {
			dt_res.month_status = eval_bcd;
			p2 = false;
		} else {
			dt_res.month_status = eval_ok;
		}
		if (tmp1 == 0 || tmp1 > 31) {
			dt_res.mday_status = eval_bcd;
			p2 = false;
		} else {
			dt_res.mday_status = eval_ok;
		}
	}
	if ((init_min == 2 || increase != 0) && p2 && errflags == 0) {
		newtime->tm_mon = tmp0;
		if (init_min == 0 && time.tm_mon != newtime->tm_mon) {
			dt_res.month_status = eval_jump;
		}
		newtime->tm_mday = tmp1;
		if (init_min == 0 && time.tm_mday != newtime->tm_mday) {
			dt_res.mday_status = eval_jump;
		}
	}

	p3 = getpar(buffer, 36, 38, 56); /* wday */
	tmp0 = getbcd(buffer, 36, 38);
	if (!p3) {
		dt_res.wday_status = eval_parity;
	} else {
		if (tmp0 == 7) {
			dt_res.wday_status = eval_bcd;
			p3 = false;
		} else {
			dt_res.wday_status = eval_ok;
		}
	}
	if ((init_min == 2 || increase != 0) && p3 && errflags == 0) {
		newtime->tm_wday = tmp0;
		if (init_min == 0 && time.tm_wday != newtime->tm_wday) {
			dt_res.wday_status = eval_jump;
		}
	}

	centofs = century_offset(*newtime);
	if (centofs == -1) {
		dt_res.year_status = eval_bcd;
		p1 = false;
	} else {
		if (init_min == 0 && time.tm_year != base_year +
		    100 * centofs + newtime->tm_year) {
			dt_res.year_status = eval_jump;
		}
		newtime->tm_year += base_year + 100 * centofs;
		if (newtime->tm_mday > lastday(*newtime)) {
			dt_res.mday_status = eval_bcd;
			p1 = p2 = p3 = false;
		}
	}

	p4 = getpar(buffer, 39, 51, 57); /* hour and minute */
	tmp0 = getbcd(buffer, 39, 44);
	tmp1 = getbcd(buffer, 45, 51);
	if (!p4) {
		dt_res.hour_status = eval_parity;
		dt_res.minute_status = eval_parity;
	} else {
		if (tmp0 > 23) {
			dt_res.hour_status = eval_bcd;
			p4 = false;
		} else {
			dt_res.hour_status = eval_ok;
		}
		if (tmp1 > 59) {
			dt_res.minute_status = eval_bcd;
			p4 = false;
		} else {
			dt_res.minute_status = eval_ok;
		}
	}
	if ((init_min == 2 || increase != 0) && p4 && errflags == 0) {
		newtime->tm_hour = tmp0;
		if (init_min == 0 && time.tm_hour != newtime->tm_hour) {
			dt_res.hour_status = eval_jump;
		}
		newtime->tm_min = tmp1;
		if (init_min == 0 && time.tm_min != newtime->tm_min) {
			dt_res.minute_status = eval_jump;
		}
	}

	return (errflags << 4) | ((!p4) << 3) | ((!p3) << 2) | ((!p2) << 1) |
	    (!p1);
}

static void
stamp_date_time(unsigned errflags, struct tm newtime, struct tm * const time)
{
	if ((dt_res.minute_length == emin_ok) && ((errflags & 0x1f) == 0)) {
		time->tm_min = newtime.tm_min;
		time->tm_hour = newtime.tm_hour;
		time->tm_mday = newtime.tm_mday;
		time->tm_mon = newtime.tm_mon;
		time->tm_year = newtime.tm_year;
		time->tm_wday = newtime.tm_wday;
		if (dt_res.dst_status != eDST_jump) {
			time->tm_isdst = newtime.tm_isdst;
		}
	}
}

static unsigned
handle_leap_second(unsigned errflags, int minlen, const int buffer[],
    struct tm time)
{
	/* process possible leap second */
	if (time.tm_min == 0) {
		dt_res.leapsecond_status = els_done;
		if (minlen == 60) {
			/* leap second processed, but missing */
			dt_res.minute_length = emin_short;
			errflags |= (1 << 5);
		} else if (minlen == 61 && buffer[17] == 1) {
			dt_res.leapsecond_status = els_one;
		}
	} else {
		dt_res.leapsecond_status = els_none;
	}
	if (minlen == 61 && dt_res.leapsecond_status == els_none) {
		/* leap second not processed, so bad minute */
		dt_res.minute_length = emin_long;
		errflags |= (1 << 5);
	}

	return errflags;
}

static unsigned
handle_dst(unsigned errflags, bool olderr, const int buffer[], struct tm time,
    struct tm * const newtime)
{
	/* determine if a DST change is announced */
	if (buffer[16] == 1 && errflags == 0) {
		dst_count++;
	}
	if (time.tm_min > 0) {
		dt_res.dst_announce = 2 * dst_count > minute_count;
	}

	if (buffer[17] != time.tm_isdst || buffer[18] == time.tm_isdst) {
		/*
		 * Time offset change is OK if:
		 * - announced and on the hour
		 * - there was an error but not any more (needed if decoding
		 *   at startup is problematic)
		 * - initial state (otherwise DST would never be valid)
		 */
		if ((dt_res.dst_announce && time.tm_min == 0) ||
		    (olderr && errflags == 0) ||
		    (time.tm_isdst == -1)) {
			newtime->tm_isdst = buffer[17]; /* expected change */
		} else {
			dt_res.dst_status = eDST_jump;
			/* sudden change, ignore */
			errflags |= (1 << 6);
		}
	}

	/* done with DST */
	if (dt_res.dst_announce && time.tm_min == 0) {
		dt_res.dst_status = eDST_done;
		/*
		 * like leap second, always clear the DST announcement at hh:00
		 */
	}
	if (time.tm_min == 0) {
		dt_res.dst_announce = false;
		dst_count = 0;
	}
	return errflags;
}

struct DT_result
decode_time(unsigned init_min, int minlen, unsigned acc_minlen,
    const int buffer[], struct tm * const time)
{
	static bool olderr;

	unsigned errflags;
	int increase;
	struct tm newtime;

	memset(&newtime, 0, sizeof(newtime));
	/* Initially, set time offset to unknown */
	if (init_min == 2) {
		time->tm_isdst = -1;
	}
	newtime.tm_isdst = time->tm_isdst; /* save DST value */

	errflags = check_time_sanity(minlen, buffer) ? 0 : 1;
	if (errflags == 0) {
		handle_special_bits(buffer);
		if (++minute_count == 60) {
			minute_count = 0;
		}
	}

	increase = increase_old_time(init_min, minlen, acc_minlen, time);

	errflags = calculate_date_time(init_min, errflags, increase, buffer,
	    *time, &newtime);

#if 0
	if (init_min < 2) {
		errflags = handle_leap_second(errflags, minlen, buffer, *time);

		errflags = handle_dst(errflags, olderr, buffer, *time, &newtime);
	}
#endif

	stamp_date_time(errflags, newtime, time);

	if (olderr && (errflags == 0)) {
		olderr = false;
	}
	if (errflags != 0) {
		olderr = true;
	}

	return dt_res;
}
