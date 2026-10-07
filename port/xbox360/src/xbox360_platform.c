/*
XBOX360_PLATFORM.C

The Xbox 360's window, in place of the desktop builds' sdl_platform.c: the
same functions, for a console with one screen and no window, keyboard or
mouse. Until the console's renderer exists the game runs as the dedicated
server does (server/platform/server_platform.c), drawing nothing
(d3d8_gl.c without a window), so these are mostly that file's.

Also the build's first log line (its version, commit and build time: a
failed upload leaves the old XEX), the texture write watch the console does
without (memory_watch.c), and the self-updater's version.
*/

#include "platform.h"
#include "sdl_platform.h"
#include "port_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "x360_port.h"
#include "xbox360_build_stamp.h"

#ifndef HALO_VERSION
#define HALO_VERSION "dev"
#endif

/* ---------- start-up */

/* before the Xbox window (xbox_memory.c, priority 101) */
__attribute__((constructor(100)))
static void xbox360_banner(void)
{
	x360_log("Warthog for Xbox 360 %s, commit %s, built %s", XBOX360_BUILD_VERSION, XBOX360_BUILD_COMMIT,
		XBOX360_BUILD_TIME);
}

/* after it, before the game starts */
__attribute__((constructor(102)))
static void xbox360_startup(void)
{
	/* (no sound device yet: dsound_sdl.c's mixer runs on its own clock) */
	setenv("HALO_NO_AUDIO", "1", 0);
	x360_log("platform layer up; the game starts");
}

BOOL platform_sdl_initialize(void)
{
	return TRUE;
}

/* the maps are the player's, in the title's folder */
BOOL platform_offer_game_data(const char *destination)
{
	(void)destination;
	platform_log("no maps: put the maps folder (ui.map and the levels) beside default.xex");
	return FALSE;
}

/* the console's display is 60 Hz; with no frames presented yet, the game's
own clock paces it */
int halo_interpolation_enabled(void)
{
	return 0;
}

/* ---------- the screen (none yet) */

BOOL platform_screen_mode(long *width, long *height)
{
	(void)width;
	(void)height;
	return FALSE;
}

int platform_display_resolutions(long *widths, long *heights, int maximum)
{
	(void)widths;
	(void)heights;
	(void)maximum;
	return 0;
}

int platform_window_sizes(long *widths, long *heights, int maximum)
{
	(void)widths;
	(void)heights;
	(void)maximum;
	return 0;
}

BOOL platform_video_initialize(unsigned long width, unsigned long height)
{
	platform_log("video: %lux%lu asked for; no renderer on the Xbox 360 yet", width, height);
	return FALSE;
}

void platform_display_apply(void)
{
}

void platform_video_drawable_size(int *width, int *height)
{
	*width = 640;
	*height = 480;
}

void platform_video_window_size(int *width, int *height)
{
	*width = 640;
	*height = 480;
}

void platform_video_swap(void)
{
}

void platform_mouse_capture(BOOL capture)
{
	(void)capture;
}

/* ---------- no keyboard or mouse */

BOOL platform_next_keystroke(struct platform_keystroke *keystroke)
{
	(void)keystroke;
	return FALSE;
}

int platform_clipboard_get(char *text, int size)
{
	if (size > 0)
		text[0] = 0;
	return 0;
}

void platform_clipboard_set(const char *text)
{
	(void)text;
}

void platform_show_message(const char *title, const char *message)
{
	platform_log("%s: %s", title, message);
}

void platform_open_url(const char *url)
{
	(void)url;
}

void platform_request_quit(void)
{
	platform_log("quit requested");
	x360_exit_to_launcher();
}

void platform_scoreboard_scroll(int open, long *notches, long *pages)
{
	(void)open;
	if (notches)
		*notches = 0;
	if (pages)
		*pages = 0;
}

void platform_menus_set_active(BOOL active)
{
	(void)active;
}

void platform_binding_capture_begin(void)
{
}

int platform_binding_capture_poll(int *input)
{
	(void)input;
	return 3;
}

void platform_ui_pointer_set_active(BOOL active)
{
	(void)active;
}

BOOL platform_ui_pointer_read(struct platform_ui_pointer *pointer)
{
	memset(pointer, 0, sizeof(*pointer));
	return FALSE;
}

void platform_input_read(struct platform_input_state *state, BOOL consume_motion)
{
	(void)consume_motion;
	memset(state, 0, sizeof(*state));
}

void platform_pump_events(void)
{
}

/* ---------- the texture write watch (memory_watch.c): the console's title
memory has no page protection to fault on, so every look at a texture's
pages says they changed (a re-upload, never a missed change) */

static volatile unsigned long watch_counter = 1;

void memory_watch_initialize(void)
{
}

void memory_watch_protect(unsigned long address, unsigned long size)
{
	(void)address;
	(void)size;
}

unsigned long memory_watch_generation(unsigned long address, unsigned long size)
{
	(void)address;
	(void)size;
	return ++watch_counter;
}

unsigned long memory_watch_serial(void)
{
	return ++watch_counter;
}

void memory_watch_prepare_write(void *address, unsigned long size)
{
	(void)address;
	(void)size;
}

void memory_watch_forget(void *address, unsigned long size)
{
	(void)address;
	(void)size;
}

/* ---------- the version (updater.c's, which the console leaves out) */

void updater_start(void)
{
}

const char *updater_version(void)
{
	return HALO_VERSION;
}
