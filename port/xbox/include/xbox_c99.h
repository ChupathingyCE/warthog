/*
XBOX_C99.H

Force-included ahead of the TOML parser config.toml is read with
(port/third_party/tomlc17, compiled as it is: port/xbox/build.py): the
little of C99's library the console's C runtime lacks or names otherwise.
port/xbox/src/xbox_port.c has snprintf and vsnprintf.
*/

#ifndef __XBOX_C99_H
#define __XBOX_C99_H

#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <math.h>

int snprintf(char *buffer, size_t size, const char *format, ...);
int vsnprintf(char *buffer, size_t size, const char *format, va_list arguments);

#ifndef isnan
#define isnan(x) __builtin_isnan(x)
#endif
#define strtoll _strtoi64

#endif
