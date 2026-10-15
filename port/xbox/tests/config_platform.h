/*
CONFIG_PLATFORM.H

What port/linux/src/port_config.c asks of its platform.h, for its console
build on the host (config_test.c): copied beside it as platform.h by
port/xbox/tests/run.sh.
*/

#ifndef __CONFIG_PLATFORM_H
#define __CONFIG_PLATFORM_H

#include <strings.h>

/* (the console's C runtime's name for it) */
#define _strnicmp strncasecmp

void platform_log(const char *format, ...);

#endif
