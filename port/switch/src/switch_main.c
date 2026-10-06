/*
SWITCH_MAIN.C

Entry point and initialization scaffolding for Nintendo Switch (Horizon OS).
Uses devkitA64 and libnx.
*/

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "../include/halo_switch_prefix.h"

/* Forward declarations */
void switch_debug_log(const char *format, ...);
int switch_platform_initialize(void);
void switch_platform_shutdown(void);

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;

	switch_debug_log("Warthog Nintendo Switch starting...\n");
	switch_debug_log("Architecture: ARM64 Cortex-A57 (Little-Endian), Memory: 4GB LPDDR4\n");

	if (switch_platform_initialize() != 0) {
		switch_debug_log("Fatal: Failed to initialize Switch platform layer\n");
		return 1;
	}

	switch_debug_log("Platform initialized successfully. Ready for engine main loop.\n");

	return 0;
}
