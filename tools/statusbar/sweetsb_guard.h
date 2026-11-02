/* The crash guard's rule, on its own so sweetsb_check.py can run it on a Mac. */
#ifndef SWEETSB_GUARD_H
#define SWEETSB_GUARD_H
#include <string.h>
#define SWEETSB_MAX_STARTS 1

/* This start's number: another boot starts over; the 60 s healthy mark writes 0. */
static inline int
sweetsb_guard_next(int starts, const char *was, const char *boot)
{
	return strcmp(was, boot) == 0 ? (starts < 0 || starts >= SWEETSB_MAX_STARTS + 1 ? SWEETSB_MAX_STARTS + 1 : starts + 1) : 1;
}

/* One unfinished start per boot: launchd's crash budget may already be partly used by something else (next2-audit finding 4). */
static inline int
sweetsb_guard_allows(int start)
{
	return start <= SWEETSB_MAX_STARTS;
}
#endif
