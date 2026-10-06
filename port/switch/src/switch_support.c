/*
SWITCH_SUPPORT.C

Platform support and hardware bridge for Nintendo Switch.
Implements logging, memory allocation, and Joy-Con / Pro Controller reading.
*/

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "../include/halo_switch_prefix.h"

/* Platform debug logger */
void switch_debug_log(const char *format, ...) {
	char buffer[1024];
	va_list args;

	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	fputs(buffer, stderr);
	fflush(stderr);
}

int switch_platform_initialize(void) {
	switch_debug_log("switch: Initializing libnx pad input and memory layout\n");
	console_input_initialize();
	return 0;
}

void switch_platform_shutdown(void) {
	switch_debug_log("switch: Shutting down platform layer\n");
}

/* Console input stubs for Joy-Cons and Pro Controller */
void console_input_initialize(void) {
	switch_debug_log("switch: Controller input initialized\n");
}

void console_input_poll(void) {
	/* Poll libnx padGetButtons / padGetStickPos */
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
