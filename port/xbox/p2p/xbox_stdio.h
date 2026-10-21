/*
XBOX_STDIO.H (the console's)

Force-included ahead of Delta's sources on the console (port/xbox/build.py):
the C99 calls the console's C runtime lacks (it has _snprintf and
_vsnprintf), which port/xbox/src/xbox_port.c gives.
*/

#ifndef __HALO_XBOX_P2P_STDIO_H
#define __HALO_XBOX_P2P_STDIO_H

#include <stdarg.h>
#include <stddef.h>

int snprintf(char *buffer, size_t size, const char *format, ...) __attribute__((format(printf, 3, 4)));
int vsnprintf(char *buffer, size_t size, const char *format, va_list arguments)
	__attribute__((format(printf, 3, 0)));

#endif
