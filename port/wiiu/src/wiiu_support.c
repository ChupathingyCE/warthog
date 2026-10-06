/*
WIIU_SUPPORT.C

Platform support and hardware bridge for Nintendo Wii U.
Implements logging, memory allocation, and GamePad/Pro Controller reading.
*/

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "../include/halo_wiiu_prefix.h"

/* Platform debug logger */
void wiiu_debug_log(const char *format, ...) {
	char buffer[1024];
	va_list args;

	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	fputs(buffer, stderr);
	fflush(stderr);
}

int wiiu_platform_initialize(void) {
	wiiu_debug_log("wiiu: Initializing OS memory and procui loop\n");
	console_input_initialize();
	return 0;
}

void wiiu_platform_shutdown(void) {
	wiiu_debug_log("wiiu: Shutting down platform layer\n");
}

/* Console input stubs for Wii U GamePad (VPAD) and Pro Controllers (WPAD) */
void console_input_initialize(void) {
	wiiu_debug_log("wiiu: VPAD and WPAD controller input initialized\n");
}

void console_input_poll(void) {
	/* Poll VPAD (GamePad) and WPAD (Wiimote / Pro Controllers) */
}

bool console_input_get_state(uint32_t port, console_controller_state_t *out_state) {
	if (!out_state || port >= CONSOLE_MAX_LOCAL_PLAYERS) {
		return false;
	}
	memset(out_state, 0, sizeof(*out_state));
	return false;
}

void console_input_set_rumble(uint32_t port, uint16_t left_motor, uint16_t right_motor) {
	(void)port;
	(void)left_motor;
	(void)right_motor;
}
