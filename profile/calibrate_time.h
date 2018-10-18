/*
 * Copyright (c) 2017-2018 Vrije Universiteit Amsterdam
 *
 * This program is licensed under the GPL2+.
 */

#ifndef CALIBRATE_TIME_H
#define CALIBRATE_TIME_H 1

/* System-dependent time-related functions */

#if defined(unix) || defined(__unix) || defined(__unix__)
#define _POSIX_C_SOURCE 200809
#include <unistd.h>

#ifdef _POSIX_TIMERS
#include <time.h>

typedef struct timespec timeval_t;

static inline int get_time(timeval_t *t)
{
	return clock_gettime(CLOCK_REALTIME, t);
}

static inline long long tdiff_us(timeval_t a, timeval_t b)
{
	return ((b.tv_sec - a.tv_sec)*1000000 + (b.tv_nsec - a.tv_nsec)/1000);
}

#endif /* POSIX timers */
#undef _POSIX_C_SOURCE

#endif /* OS selection */

#endif /* calibrate_time.h */
