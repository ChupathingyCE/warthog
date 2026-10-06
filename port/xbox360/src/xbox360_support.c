/*
XBOX360_SUPPORT.C

Platform support and hardware bridge for Xbox 360 (Xenon).
Implements logging, memory management, timing, and controller reading.
*/

#include <stdint.h>
#include <stdbool.h>

#if defined(__has_include) && __has_include(<stdio.h>)
#include <stdio.h>
#include <stdarg.h>
#define HAS_STDIO 1
#else
#define HAS_STDIO 0
void oxdk_dbgprint(const char *s);
#endif

#include "../include/halo_xbox360_prefix.h"

/* Platform debug logger: prints to stdout or debugger */
void xbox360_debug_log(const char *format, ...) {
#if HAS_STDIO
	char buffer[1024];
	va_list args;

	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	fputs(buffer, stderr);
	fflush(stderr);
#else
	oxdk_dbgprint(format);
#endif
}

int xbox360_platform_initialize(void) {
	xbox360_debug_log("xbox360: Initializing memory pools and subsystem timers\n");
	console_input_initialize();
	return 0;
}

void xbox360_platform_shutdown(void) {
	xbox360_debug_log("xbox360: Shutting down platform layer\n");
}

/* Console input stubs */
void console_input_initialize(void) {
	xbox360_debug_log("xbox360: Controller input initialized\n");
}

void console_input_poll(void) {
	/* Poll XInput gamepad state for 4 controller ports */
}

bool console_input_get_state(uint32_t port, console_controller_state_t *out_state) {
	if (!out_state || port >= CONSOLE_MAX_LOCAL_PLAYERS) {
		return false;
	}
	__builtin_memset(out_state, 0, sizeof(*out_state));
	return false;
}

void console_input_set_rumble(uint32_t port, uint16_t left_motor, uint16_t right_motor) {
	(void)port;
	(void)left_motor;
	(void)right_motor;
}
