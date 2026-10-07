/*
TIME.H

The console's <time.h>, which has no struct timespec, then the POSIX clocks
the platform layer uses (port/windows/include/posix/time.h; implemented in
port/xbox360/src/x360_posix.c).
*/

#ifndef __HALO_XBOX360_TIME_H
#define __HALO_XBOX360_TIME_H

#include <crtdefs.h>

struct timespec
{
	time_t tv_sec;
	long tv_nsec;
};

#endif

#include_next <time.h>
