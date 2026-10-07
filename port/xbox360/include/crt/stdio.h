/*
STDIO.H

The console's <stdio.h> (through port/windows/include/crt's file-name
wrappers), and C99's snprintf and vsnprintf: the console's C runtime has
MSVC's, which return -1 and leave the buffer unterminated when the text is
cut short (port/xbox360/src/x360_crt.c).
*/

#include_next <stdio.h>

#ifndef __HALO_XBOX360_STDIO_H
#define __HALO_XBOX360_STDIO_H

int halo_xbox360_snprintf(char *buffer, size_t size, const char *format, ...);
int halo_xbox360_vsnprintf(char *buffer, size_t size, const char *format, va_list arguments);

#define snprintf halo_xbox360_snprintf
#define vsnprintf halo_xbox360_vsnprintf

#endif

#include "halo_xbox360_file_names.h"
