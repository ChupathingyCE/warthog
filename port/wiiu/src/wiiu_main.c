/*
WIIU_MAIN.C

Entry point and initialization scaffolding for Nintendo Wii U (Cafe OS).
Uses devkitPPC and wut (coreinit, procui, vpad).
*/

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "../include/halo_wiiu_prefix.h"

/* Forward declarations */
void wiiu_debug_log(const char *format, ...);
int wiiu_platform_initialize(void);
void wiiu_platform_shutdown(void);

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;

	wiiu_debug_log("Warthog Nintendo Wii U starting...\n");
	wiiu_debug_log("Architecture: PowerPC Espresso (Big-Endian), Memory: 1GB App RAM\n");

	if (wiiu_platform_initialize() != 0) {
		wiiu_debug_log("Fatal: Failed to initialize Wii U platform layer\n");
		return 1;
	}

	wiiu_debug_log("Platform initialized successfully. Ready for engine main loop.\n");

	return 0;
}
