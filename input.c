// Copyright 2013-2019 René Ladan and Udo Klein and "JsBergbau"
// SPDX-License-Identifier: BSD-2-Clause

#include "input.h"

#include "json_object.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#if defined(__FreeBSD__)
#  include <sys/param.h>
#  if __FreeBSD_version >= 900022
#    include <sys/gpio.h>
#    include <sys/ioccom.h>
#  else
#    define NOLIVE 1
#  endif
#elif defined(__NetBSD__)
#  warning NetBSD, GPIO support not yet implemented
#  define NOLIVE 1
#elif defined(__linux__)
/* NOP */
#elif defined(__APPLE__) && (defined(__OSX__) || defined(__MACH__))
#  warning MacOS, GPIO support available but no port for Rapberry Pi
#  define NOLIVE 1
#  define MACOS 1
#elif defined(__CYGWIN__)
#  warning Cygwin, GPIO support not yet implemented
#  define NOLIVE 1
#elif defined(_WIN16) || defined(_WIN32) || defined(_WIN64)
#  error Use Cygwin to use this software on Windows
#else
#  error Unsupported operating system, please send a patch to the author
#endif

/** maximum number of bits in a minute */
#define BUFLEN 61

static int bitpos;              /* second */
static unsigned dec_bp;         /* bitpos decrease in file mode */
static int buffer[BUFLEN];      /* wrap after BUFLEN positions */
static FILE *logfile;           /* auto-appended in live mode */
static int fd;                  /* gpio file */
static struct hardware hw;
static struct bitinfo bit;
static unsigned acc_minlen;
static int cutoff;
static struct GB_result gb_res;
static unsigned filemode = 0;   /* 0 = no file, 1 = input, 2 = output */

int
set_mode_file(const char * const infilename)
{
	if (filemode == 1) {
		fprintf(stderr, "Already initialized to live mode.\n");
		cleanup();
		return -1;
	}
	if (infilename == NULL) {
		fprintf(stderr, "infilename is NULL\n");
		return -1;
	}
	logfile = fopen(infilename, "r");
	if (logfile == NULL) {
		perror("fopen(logfile)");
		return errno;
	}
	filemode = 2;
	return 0;
}

int
set_mode_live(struct json_object *config)
{
#if defined(NOLIVE)
	fprintf(stderr,
	    "No GPIO interface available, disabling live decoding\n");
	cleanup();
	return -1;
#else
#if defined(__FreeBSD__)
	struct gpio_pin pin;
#endif
	char buf[64];
	struct json_object *value;
	int res;

	if (filemode == 2) {
		fprintf(stderr, "Already initialized to file mode.\n");
		cleanup();
		return -1;
	}
	/* fill hardware structure and initialize hardware */
	if (json_object_object_get_ex(config, "pin", &value)) {
		hw.pin = (unsigned)json_object_get_int(value);
	} else {
		fprintf(stderr, "Key 'pin' not found\n");
		cleanup();
		return EX_DATAERR;
	}
	if (json_object_object_get_ex(config, "activehigh", &value)) {
		hw.active_high = (bool)json_object_get_boolean(value);
	} else {
		fprintf(stderr, "Key 'activehigh' not found\n");
		cleanup();
		return EX_DATAERR;
	}
	if (json_object_object_get_ex(config, "freq", &value)) {
		hw.freq = (unsigned)json_object_get_int(value);
	} else {
		fprintf(stderr, "Key 'freq' not found\n");
		cleanup();
		return EX_DATAERR;
	}
	if (hw.freq < 10 || hw.freq > 120000 || (hw.freq & 1) == 1) {
		fprintf(stderr, "hw.freq must be an even number between 10 and"
		    "120000 inclusive\n");
		cleanup();
		return EX_DATAERR;
	}
	bit.signal = malloc(hw.freq / 2);
#if defined(__FreeBSD__)
	if (json_object_object_get_ex(config, "iodev", &value)) {
		hw.iodev = (unsigned)json_object_get_int(value);
	} else {
		fprintf(stderr, "Key 'iodev' not found\n");
		cleanup();
		return EX_DATAERR;
	}
	res = snprintf(buf, sizeof(buf), "/dev/gpioc%u", hw.iodev);
	if (res < 0 || res >= sizeof(buf)) {
		fprintf(stderr, "hw.iodev too high? (%i)\n", res);
		cleanup();
		return EX_DATAERR;
	}
	fd = open(buf, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open %s: ", buf);
		perror(NULL);
		cleanup();
		return errno;
	}

	pin.gp_pin = hw.pin;
	pin.gp_flags = GPIO_PIN_INPUT;
	if (ioctl(fd, GPIOSETCONFIG, &pin) < 0) {
		perror("ioctl(GPIOSETCONFIG)");
		cleanup();
		return errno;
	}
#elif defined(__linux__)
	fd = open("/sys/class/gpio/export", O_WRONLY);
	if (fd < 0) {
		perror("open(/sys/class/gpio/export)");
		cleanup();
		return errno;
	}
	res = snprintf(buf, sizeof(buf), "%u", hw.pin);
	if (res < 0 || res >= sizeof(buf)) {
		fprintf(stderr, "hw.pin too high? (%i)\n", res);
		cleanup();
		return EX_DATAERR;
	}
	if (write(fd, buf, res) < 0) {
		if (errno != EBUSY) {
			perror("write(export)");
			cleanup();
			return errno; /* EBUSY -> pin already exported ? */
		}
	}
	if (close(fd) == -1) {
		perror("close(export)");
		cleanup();
		return errno;
	}
	res = snprintf(buf, sizeof(buf), "/sys/class/gpio/gpio%u/direction",
	    hw.pin);
	if (res < 0 || res >= sizeof(buf)) {
		fprintf(stderr, "hw.pin too high? (%i)\n", res);
		cleanup();
		return EX_DATAERR;
	}
	fd = open(buf, O_RDWR);
	if (fd < 0) {
		perror("open(direction)");
		cleanup();
		return errno;
	}
	if (write(fd, "in", 3) < 0) {
		perror("write(in)");
		cleanup();
		return errno;
	}
	if (close(fd) == -1) {
		perror("close(direction)");
		cleanup();
		return errno;
	}
	res = snprintf(buf, sizeof(buf), "/sys/class/gpio/gpio%u/value",
	    hw.pin);
	if (res < 0 || res >= sizeof(buf)) {
		fprintf(stderr, "hw.pin too high? (%i)\n", res);
		cleanup();
		return EX_DATAERR;
	}
	fd = open(buf, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		perror("open(value)");
		cleanup();
		return errno;
	}
#endif
	filemode = 1;
	return 0;
#endif
}

void
cleanup(void)
{
	if (fd > 0 && close(fd) == -1) {
#if defined(__FreeBSD__)
		perror("close(/dev/gpioc*)");
#elif defined(__linux__)
		perror("close(/sys/class/gpio/*)");
#endif
	}
	fd = 0;
	if (logfile != NULL) {
		if (fclose(logfile) == EOF) {
			perror("fclose(logfile)");
		} else {
			logfile = NULL;
		}
	}
	free(bit.signal);
}

int
get_pulse(void)
{
	int tmpch;
#if defined(NOLIVE)
	tmpch = 2;
#else
	int count = 0;
#if defined(__FreeBSD__)
	struct gpio_req req;

	req.gp_pin = hw.pin;
	count = ioctl(fd, GPIOGET, &req);
	tmpch = (req.gp_value == GPIO_PIN_HIGH) ? 1 : 0;
	if (count < 0) {
#elif defined(__linux__)
	count = read(fd, &tmpch, 1);
	tmpch -= '0';
	if (lseek(fd, 0, SEEK_SET) == (off_t)-1)
		return 2; /* rewind to prevent EBUSY/no read failed */
	if (count != 1) {
#endif
		return 2; /* hardware failure? */
	}

	if (!hw.active_high) {
		tmpch = 1 - tmpch;
	}
#endif
	return tmpch;
}

/*
 * Clear the cutoff value and the state values, except emark_toolong and
 * emark_late to be able to determine if this flag can be cleared again.
 */
static void
set_new_state(void)
{
	if (!gb_res.skip) {
		cutoff = -1;
	}
	gb_res.bad_io = false;
	gb_res.bitval = ebv_none;
	if (gb_res.marker != emark_toolong && gb_res.marker != emark_late) {
		gb_res.marker = emark_none; // XXX never true for NPL ?
	}
	gb_res.hwstat = ehw_ok;
	gb_res.done = false;
	gb_res.skip = false;
}

static void
reset_frequency(void)
{
	if (logfile != NULL) {
		fprintf(logfile, "%s",
		    bit.realfreq <= hw.freq * 500000 ? "<" :
		    bit.realfreq > hw.freq * 1000000 ? ">" : "");
	}
	bit.realfreq = hw.freq * 1000000;
	bit.freq_reset = true;
}

static void
reset_bitlen(void)
{
	if (logfile != NULL) {
		fprintf(logfile, "!");
	}
	bit.bit0 = bit.realfreq / 2;
	bit.bit59 = bit.realfreq / 10;
	bit.bitlen_reset = true;
}

unsigned
collect_pulses(unsigned start, int *init_bit, bool *adj_freq)
{
	char outch;
	long long a, y = 1000000000;
	unsigned stv = 1;
#if !defined(MACOS)
	struct timespec tp0, tp1;
#endif
	struct timespec slp;
	unsigned sec2 = 1000000000 / (hw.freq * hw.freq);

	/* Set up filter, reach 50% after hw.freq/20 samples (i.e. 50 ms) */
	a = 1000000000 - (long long)(1000000000 * exp2(-20.0 / hw.freq));

	for (bit.t = start; bit.t < hw.freq; bit.t++) {
#if !defined(MACOS)
		(void)clock_gettime(CLOCK_MONOTONIC, &tp0);
#endif
		int p = get_pulse();
		if (p == 2) {
			gb_res.bad_io = true;
			outch = '*';
			break;
		}
		if (bit.signal != NULL) {
			if ((bit.t & 7) == 0) {
				bit.signal[bit.t / 8] = 0;
			}
			/* clear data from previous second */
			bit.signal[bit.t / 8] |=
			    p << (unsigned char)(bit.t & 7);
		}

		if (y >= 0 && y < a / 2) {
			bit.tlast0 = (int)bit.t;
		}
		y += a * (p * 1000000000 - y) / 1000000000;

		/*
		 * Prevent algorithm collapse during thunderstorms or
		 * scheduler abuse
		 */
		if (bit.realfreq <= hw.freq * 500000 ||
		    bit.realfreq > hw.freq * 1000000) {
			reset_frequency();
			*adj_freq = false;
		}

		if (bit.t > bit.realfreq * 1500000) {
			if (bit.tlow <= hw.freq / 20) {
				gb_res.hwstat = ehw_receive;
				outch = 'r';
			} else if (bit.tlow * 100 / bit.t >= 99) {
				gb_res.hwstat = ehw_transmit;
				outch = 'x';
			} else {
				gb_res.hwstat = ehw_random;
				outch = '#';
			}
			*adj_freq = false;
			break; /* timeout */
		}

		/*
		 * Schmitt trigger, maximize value to introduce hysteresis and
		 * to avoid infinite memory.
		 */
		if (y < 500000000 && stv == 1) {
			/* end of high part of second */
			y = 0;
			stv = 0;
			bit.tlow = (int)bit.t;
		}
		if (y > 500000000 && stv == 0) {
			/* end of low part of second */
			if (*init_bit == 2) {
				*init_bit = 1;
			}
			break; /* start of new second */
		}
		long long twait = (long long)(sec2 * bit.realfreq / 1000000);
#if !defined(MACOS)
		(void)clock_gettime(CLOCK_MONOTONIC, &tp1);
		twait = twait - (tp1.tv_sec - tp0.tv_sec) *
		    1000000000 - (tp1.tv_nsec - tp0.tv_nsec);
#endif
		slp.tv_sec = twait / 1000000000;
		slp.tv_nsec = twait % 1000000000;
		while (twait > 0 && nanosleep(&slp, &slp) > 0)
			; /* empty loop */
	}
	if (bit.t >= hw.freq) {
		/* this can actually happen */
		if (gb_res.hwstat == ehw_ok) {
			gb_res.hwstat = ehw_random;
			outch = '#';
		}
		reset_frequency();
		*adj_freq = false;
	}
	return bit.t;
}

/*
 * The bits are decoded from the signal using an exponential low-pass filter
 * in conjunction with a Schmitt trigger. The idea and the initial
 * implementation for this come from Udo Klein, with permission.
 * http://blog.blinkenlight.net/experiments/dcf77/binary-clock/#comment-5916
 */
struct GB_result
get_bit_live(void)
{
	char outch = '?';
	bool adj_freq = true;
	static int init_bit = 2;
	unsigned long long len100ms;

	bit.freq_reset = false;
	bit.bitlen_reset = false;

	set_new_state();

	/*
	 * One period is 1000 ms long. The active part is can be 100 ms ('00'),
	 * 200 ms ('10'), 300 ms ('11') or 100+100 ms ('01') long. Bit 0 is
	 * special and 500 ms long to indicate the start of a new minute.
	 *
	 * A reception timeout occurs after 2000 ms.
	 */

	if (init_bit == 2) {
		bit.realfreq = hw.freq * 1000000;
		bit.bit0 = bit.realfreq / 2;
		bit.bit59 = bit.realfreq / 10;
	}
	len100ms = bit.bit0 / 10 + bit.bit59 / 2;

	bit.tlow = -1;
	bit.tlast0 = -1;

	bit.t = collect_pulses(0, &init_bit, &adj_freq);
	if (!gb_res.bad_io && gb_res.hwstat == ehw_ok) {
		 if (2 * bit.tlow * bit.realfreq < 3 * len100ms * bit.t) {
			/* two zero bits, ~100 ms active signal */
			gb_res.bitval = ebv_00;
			outch = '0';
			buffer[bitpos] = 0;
		} else if (2 * bit.tlow * bit.realfreq < 5 * len100ms * bit.t) {
			/* one bit and zero bit, ~200 ms active signal */
			gb_res.bitval = ebv_10;
			outch = '1';
			buffer[bitpos] = 1;
		} else if (2 * bit.tlow * bit.realfreq < 7 * len100ms * bit.t) {
			/* mitigate against 2 bits becoming a 30 combination if the radio signal is noisy */
			if (bit.t >= bit.realfreq / 2500000) {
				/* two one bits, ~300 ms active signal */
				gb_res.bitval = ebv_11;
				outch = '3';
				buffer[bitpos] = 3;
			} else {
				/* zero bit and one bit, split signal */
				gb_res.bitval = ebv_01;
				outch = '2';
				buffer[bitpos] = 2;
				/* read the rest of the second */
				bit.t = collect_pulses(bit.t, &init_bit, &adj_freq);
			}
		} else if (bit.tlow * bit.realfreq < 6 * len100ms * bit.t) {
			if (bit.t >= bit.realfreq / 2500000) {
				/* begin-of-minute, ~500 ms active signal */
				gb_res.marker = emark_minute;
				gb_res.bitval = ebv_bom;
				outch = '4';
				bitpos = 0;
				buffer[bitpos] = 4;
			} else {
				/* zero bit and one bit, split signal */
				gb_res.bitval = ebv_01;
				outch = '2';
				buffer[bitpos] = 2;
				/* read the rest of the second */
				bit.t = collect_pulses(bit.t, &init_bit, &adj_freq);
			}
		} else {
			/* bad radio signal, retain old value */
			gb_res.bitval = ebv_none;
			outch = '_';
			adj_freq = false;
		}
	}

	if (!gb_res.bad_io) {
		if (init_bit == 1) {
			init_bit--;
		} else if (gb_res.hwstat == ehw_ok &&
		    (gb_res.marker == emark_none ||
		     gb_res.marker == emark_minute)) {
			long long avg;
			if (bitpos == 59 && gb_res.bitval == ebv_00) {
				bit.bit59 +=
				    ((long long)(bit.tlow * 1000000 -
				    bit.bit59) / 2);
			}
			if (/*bitpos == 0 && */gb_res.bitval == ebv_bom) {
				bit.bit0 +=
				    ((long long)(bit.tlow * 1000000 -
				    bit.bit0) / 2);
			}
			/* Force sane values during e.g. a thunderstorm */
			avg = (bit.bit0 - bit.bit59) / 2;
			if (4 * bit.bit0 < bit.bit59 * 15 ||
			    2 * bit.bit0 > bit.bit59 * 15) {
				reset_bitlen();
				adj_freq = false;
			}
			if (bit.bit0 + avg < bit.realfreq / 2 ||
			    bit.bit0 - avg > bit.realfreq / 2) {
				reset_bitlen();
				adj_freq = false;
			}
			if (bit.bit59 + avg < bit.realfreq / 10) {
				reset_bitlen();
				adj_freq = false;
			}
		}
	}
	if (adj_freq) {
		bit.realfreq +=
		    ((long long)(bit.t * 1000000 - bit.realfreq) / 20);
	}
	acc_minlen += 1000000 * bit.t / (bit.realfreq / 1000);
	if (logfile != NULL) {
		fprintf(logfile, "%c", outch);
		if (gb_res.marker == emark_minute ||
		    gb_res.marker == emark_late) {
			fprintf(logfile, "a%u\n", acc_minlen);
		}
	}
	if (gb_res.marker == emark_minute || gb_res.marker == emark_late) {
		cutoff = bit.t * 1000000 / (bit.realfreq / 10000);
	}
	return gb_res;
}

/* Skip over invalid characters */
static int
skip_invalid(void)
{
	int inch = EOF;

	do {
		int oldinch = inch;
		if (feof(logfile)) {
			break;
		}
		inch = getc(logfile);
		/*
		 * \r\n is implicitly converted because \r is invalid character
		 * \n\r is implicitly converted because \n is found first
		 * \n is OK
		 * convert \r to \n
		 */
		if (oldinch == '\r' && inch != '\n') {
			ungetc(inch, logfile);
			inch = '\n';
		}
	} while (strchr("012345\nxr#*_a", inch) == NULL);
	return inch;
}

struct GB_result
get_bit_file(void)
{
	static int oldinch;
	static bool read_acc_minlen;
	int inch;

	set_new_state();

	inch = skip_invalid();
	/*
	 * bit.t is set to fake value for compatibility with old log files not
	 * storing acc_minlen values or to increase time when mainloop() splits
	 * too long minutes.
	 */

	switch (inch) {
	case EOF:
		gb_res.done = true;
		return gb_res;
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
		buffer[bitpos] = inch - (int)'0';
		gb_res.bitval = (inch == (int)'0') ? ebv_00 : 
				(inch == (int)'1') ? ebv_10 :
				(inch == (int)'2') ? ebv_01 :
				(inch == (int)'3') ? ebv_11 :
				(inch == (int)'4') ? ebv_bom : ebv_none;
		bit.t = 1000;
		if (inch == '4') {
			if (gb_res.marker == emark_none) {
				gb_res.marker = emark_minute;
			} else if (gb_res.marker == emark_toolong) {
				gb_res.marker = emark_late;
			}
		}
		break;
	case 'x':
		gb_res.hwstat = ehw_transmit;
		bit.t = 1500;
		break;
	case 'r':
		gb_res.hwstat = ehw_receive;
		bit.t = 1500;
		break;
	case '#':
		gb_res.hwstat = ehw_random;
		bit.t = 1500;
		break;
	case '*':
		gb_res.bad_io = true;
		bit.t = 0;
		break;
	case '_':
		/* retain old value in buffer[bitpos] */
		gb_res.bitval = ebv_none;
		bit.t = 1000;
		break;
	case 'a':
		/* acc_minlen, up to 2^32-1 ms */
		gb_res.skip = true;
		bit.t = 0;
		if (fscanf(logfile, "%10u", &acc_minlen) != 1) {
			gb_res.done = true;
		}
		read_acc_minlen = !gb_res.done;
		break;
	default:
		break;
	}

	if (!read_acc_minlen) {
		acc_minlen += bit.t;
	}

	/*
	 * Read-ahead 1 character to check if a minute marker is coming. This
	 * prevents emark_toolong or emark_late being set 1 bit early.
	 */
	oldinch = inch;
	inch = skip_invalid();
	if (!feof(logfile)) {
		if (dec_bp == 0 && bitpos > 0 && oldinch != '\n' &&
		    (inch == '\n' || inch == 'a')) {
			dec_bp = 1;
		}
	} else {
		gb_res.done = true;
	}
	ungetc(inch, logfile);

	return gb_res;
}

bool
is_space_bit(int bitpos)
{
	return (bitpos == 1 || bitpos == 9 || bitpos == 17 ||
	    bitpos == 25 || bitpos == 30 || bitpos == 36 || bitpos == 39 ||
	    bitpos == 45 || bitpos == 52);
}

struct GB_result
next_bit(void)
{
	if (dec_bp == 1) {
		bitpos--;
		dec_bp = 2;
	}
	if (gb_res.marker == emark_minute || gb_res.marker == emark_late) {
		bitpos = 1;
		dec_bp = 0;
	} else if (!gb_res.skip) {
		bitpos++;
	}
	if (bitpos == BUFLEN) {
		gb_res.marker = emark_toolong;
		bitpos = 0;
		return gb_res;
	}
	if (gb_res.marker == emark_toolong) {
		gb_res.marker = emark_none; /* fits again */
	}
	else if (gb_res.marker == emark_late) {
		gb_res.marker = emark_minute; /* cannot happen? */
	}
	return gb_res;
}

int
get_bitpos(void)
{
	return bitpos;
}

const int * const
get_buffer(void)
{
	return buffer;
}

struct hardware
get_hardware_parameters(void)
{
	return hw;
}

void
*flush_logfile(/*@unused@*/ void *arg)
{
	for (;;)
	{
		fflush(logfile);
		sleep(60);
	}
}

int
append_logfile(const char * const logfilename)
{
	pthread_t flush_thread;

	if (logfilename == NULL) {
		fprintf(stderr, "logfilename is NULL\n");
		return -1;
	}
	logfile = fopen(logfilename, "a");
	if (logfile == NULL) {
		return errno;
	}
	fprintf(logfile, "\n--new log--\n\n");
	return pthread_create(&flush_thread, NULL, flush_logfile, NULL);
}

int
close_logfile(void)
{
	int f;

	f = fclose(logfile);
	return (f == EOF) ? errno : 0;
}

struct bitinfo
get_bitinfo(void)
{
	return bit;
}

unsigned
get_acc_minlen(void)
{
	return acc_minlen;
}

void
reset_acc_minlen(void)
{
	acc_minlen = 0;
}

int
get_cutoff(void)
{
	return cutoff;
}
