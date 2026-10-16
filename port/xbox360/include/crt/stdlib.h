/*
STDLIB.H

The console's <stdlib.h> (through port/windows/include/crt/stdlib.h), and
C99's 64-bit conversions, which its C runtime has only as _strtoi64 and
_strtoui64: those return their 64-bit result the way Microsoft's compiler
does, which this compiler reads back wrong (port/xbox360/README.md), so
they are the port's own (port/xbox360/src/x360_crt.c).
*/

#include_next <stdlib.h>

#ifndef __HALO_XBOX360_STDLIB_H
#define __HALO_XBOX360_STDLIB_H

long long halo_xbox360_strtoll(const char *text, char **end, int base);
unsigned long long halo_xbox360_strtoull(const char *text, char **end, int base);

/* the console has no environment: a table of the port's own
(port/xbox360/src/x360_system.c) */
int setenv(const char *name, const char *value, int overwrite);

#define strtoll halo_xbox360_strtoll
#define strtoull halo_xbox360_strtoull
#define _strtoi64 halo_xbox360_strtoll
#define _strtoui64 halo_xbox360_strtoull
#define atoll(text) halo_xbox360_strtoll((text), NULL, 10)
#define _atoi64(text) halo_xbox360_strtoll((text), NULL, 10)

#endif
