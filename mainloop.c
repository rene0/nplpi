// Copyright 2014-2019 René Ladan
// SPDX-License-Identifier: BSD-2-Clause

#include "mainloop.h"

#include "decode_time.h"
#include "input.h"
#include "setclock.h"

#include <string.h>
#include <time.h>

static void
check_handle_new_minute(struct GB_result bit, struct ML_result *mlr,
    int bitpos, struct tm *curtime, int minlen, bool was_toolong,
    unsigned *init_min, void (*display_minute)(int),
    void (*display_time)(struct DT_result, struct tm),
    struct ML_result (*process_setclock_result)(struct ML_result, int))
{
	bool have_result = false;

	if ((bit.marker == emark_minute || bit.marker == emark_late) &&
	    !was_toolong) {
		struct DT_result dt;

		display_minute(minlen);
		dt = decode_time(*init_min, minlen, get_acc_minlen(),
		    get_buffer(), curtime);

		display_time(dt, *curtime);

		if (mlr->settime) {
			have_result = true;
			if (setclock_ok(*init_min, dt, bit)) {
				mlr->settime_result = setclock(*curtime);
			} else {
				mlr->settime_result = esc_unsafe;
			}
		}
		if (bit.marker == emark_minute || bit.marker == emark_late) {
			reset_acc_minlen();
		}
		if (*init_min > 0) {
			(*init_min)--;
		}
	}
	if (have_result && process_setclock_result != NULL) {
		*mlr = process_setclock_result(*mlr, bitpos);
	}
}

void
mainloop(char *logfilename, struct GB_result (*get_bit)(void),
    void (*display_bit)(struct GB_result, int),
    void (*display_long_minute)(void), void (*display_minute)(int),
    void (*display_new_second)(void),
    void (*display_time)(struct DT_result, struct tm),
    struct ML_result (*process_setclock_result)(struct ML_result, int),
    struct ML_result (*process_input)(struct ML_result, int),
    struct ML_result (*post_process_input)(struct ML_result, int))
{
	int minlen = 0;
	int bitpos = 0;
	int old_bitpos = 0;
	unsigned init_min = 2;
	struct tm curtime;
	struct ML_result mlr;
	bool was_toolong = false;

	(void)memset(&curtime, 0, sizeof(curtime));
	(void)memset(&mlr, 0, sizeof(mlr));
	mlr.logfilename = logfilename;

	for (;;) {
		struct GB_result bit;

		bit = get_bit();
		if (process_input != NULL) {
			mlr = process_input(mlr, bitpos);
			if (bit.done || mlr.quit) {
				break;
			}
		}

		bitpos = get_bitpos();
		if (post_process_input != NULL) {
			mlr = post_process_input(mlr, bitpos);
		}
		if (!bit.skip && !mlr.quit) {
			display_bit(bit, bitpos);
		}

		bit = next_bit();
		if (minlen == -1) {
			check_handle_new_minute(bit, &mlr, bitpos, &curtime,
			    minlen, was_toolong, &init_min, display_minute,
			    display_time, process_setclock_result);
			was_toolong = true;
		}

		if (bit.marker == emark_minute) {
			/* minute marker is at bit 0 */
			minlen = old_bitpos;
		} else if (bit.marker == emark_toolong ||
		    bit.marker == emark_late) {
			minlen = -1;
			/*
			 * leave acc_minlen alone, any minute marker already
			 * processed
			 */
			display_long_minute();
		}
		if (display_new_second != NULL) {
			display_new_second();
		}

		check_handle_new_minute(bit, &mlr, bitpos, &curtime, minlen,
		    was_toolong, &init_min, display_minute,
		    display_time, process_setclock_result);
		was_toolong = false;
		if (bit.done || mlr.quit) {
			break;
		}
		old_bitpos = bitpos; 
	}
	cleanup();
}
