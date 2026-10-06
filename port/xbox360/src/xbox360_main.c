/*
XBOX360_MAIN.C

Entry point and initialization scaffolding for Xbox 360 (Xenon).
*/

#include <stdint.h>
#include <stdbool.h>

#if defined(__has_include) && __has_include(<stdio.h>)
#include <stdio.h>
#endif

#include "../include/halo_xbox360_prefix.h"

/* Forward declarations */
void xbox360_debug_log(const char *format, ...);
int xbox360_platform_initialize(void);
void xbox360_platform_shutdown(void);

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;

	xbox360_debug_log("Warthog Xbox 360 (Xenon) starting...\n");
	xbox360_debug_log("Architecture: PowerPC (Big-Endian), Memory: 512MB UMA\n");

	if (xbox360_platform_initialize() != 0) {
		xbox360_debug_log("Fatal: Failed to initialize Xbox 360 platform layer\n");
		return 1;
	}

	xbox360_debug_log("Platform initialized successfully. Ready for engine main loop.\n");

	return 0;
}
