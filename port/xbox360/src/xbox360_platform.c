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
	/* (what the renderer will stand on: logged, a second of a triangle) */
	x360_video_probe();
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

/* ---------- the main loop's pulse */

/* the game's (game.h) */
long game_time_get(void);

/* every pass of the main loop (main.c): once a second, how many passes
there were and the game's time in ticks */
void xbox360_main_loop_tick(void)
{
	static DWORD last_report;
	static unsigned long loops, total;
	DWORD now = GetTickCount();

	loops++;
	total++;
	if (!last_report)
		last_report = now;
	if (now - last_report >= 1000)
	{
		platform_log("main loop: %lu passes in %lu ms (%lu in all), game time %ld", loops,
			(unsigned long)(now - last_report), total, game_time_get());
		loops = 0;
		last_report = now;
	}
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

/* gl_functions.c's */
int gl_functions_load(void);

/* the screen: the console's Direct3D device, under the desktop renderer's
OpenGL (port/xbox360/src/x360_gl.c) */
BOOL platform_video_initialize(unsigned long width, unsigned long height)
{
	static BOOL ready;

	if (ready)
		return TRUE;
	if (!x360_video_device())
		return FALSE;
	if (!gl_functions_load())
	{
		platform_log("video: an OpenGL entry point is missing");
		return FALSE;
	}
	platform_log("video: the game's %lux%lu screen, drawn through OpenGL on Direct3D 9", width, height);
	ready = TRUE;
	return TRUE;
}

void platform_display_apply(void)
{
}

void platform_video_drawable_size(int *width, int *height)
{
	*width = 1280;
	*height = 720;
}

void platform_video_window_size(int *width, int *height)
{
	*width = 1280;
	*height = 720;
}

void platform_video_swap(void)
{
	x360_gl_swap();
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

/* ---------- the texture write watch (memory_watch.c)

The console's title memory has no page protection to fault on, so a
texture's pages are marked written only by the writes the port knows of: a
file read into the window (platform_contiguous_write), a block allocated or
freed, and a texture locked through Direct3D (d3d8_resources.c), which is how
the game writes its textures (the text cache, the progress bar, the
defaults). The rest of the time a texture keeps its upload.
"debug.texture_no_cache" in the config uploads every texture at every use
again, should a write ever be missed. */

#define WATCH_PAGE_SIZE 0x1000UL
#define WATCH_PAGE_COUNT (PLATFORM_CONTIGUOUS_SIZE / WATCH_PAGE_SIZE)

extern unsigned int xbox_address_space_base;

static LONG watch_counter = 1;
static unsigned long page_generation[WATCH_PAGE_COUNT];

/* the pages of a range of Xbox addresses within the window, or FALSE */
static BOOL watch_pages(unsigned long address, unsigned long size, unsigned long *first, unsigned long *last)
{
	unsigned long end;

	if (!size)
		return FALSE;
	end = address + size;
	if (end <= PLATFORM_CONTIGUOUS_BASE || address >= PLATFORM_CONTIGUOUS_BASE + PLATFORM_CONTIGUOUS_SIZE)
		return FALSE;
	if (address < PLATFORM_CONTIGUOUS_BASE)
		address = PLATFORM_CONTIGUOUS_BASE;
	if (end > PLATFORM_CONTIGUOUS_BASE + PLATFORM_CONTIGUOUS_SIZE || end < address)
		end = PLATFORM_CONTIGUOUS_BASE + PLATFORM_CONTIGUOUS_SIZE;
	*first = (address - PLATFORM_CONTIGUOUS_BASE) / WATCH_PAGE_SIZE;
	*last = (end - 1 - PLATFORM_CONTIGUOUS_BASE) / WATCH_PAGE_SIZE;
	return TRUE;
}

static void watch_written(void *pointer, unsigned long size)
{
	unsigned long address = (unsigned long)((unsigned int)(__UINTPTR_TYPE__)pointer - xbox_address_space_base);
	unsigned long first, last, page, generation;

	if (!pointer || !watch_pages(address, size, &first, &last))
		return;
	generation = (unsigned long)InterlockedIncrement(&watch_counter);
	for (page = first; page <= last; page++)
		page_generation[page] = generation;
}

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
	unsigned long first, last, page, newest = 0;

	/* (outside the window: never known to be unchanged) */
	if (!watch_pages(address, size, &first, &last))
		return (unsigned long)InterlockedIncrement(&watch_counter);
	for (page = first; page <= last; page++)
	{
		if (page_generation[page] > newest)
			newest = page_generation[page];
	}
	return newest;
}

unsigned long memory_watch_serial(void)
{
	return (unsigned long)*(volatile LONG *)&watch_counter;
}

void memory_watch_prepare_write(void *address, unsigned long size)
{
	watch_written(address, size);
}

void memory_watch_forget(void *address, unsigned long size)
{
	watch_written(address, size);
}

/* ---------- the version (updater.c's, which the console leaves out) */

void updater_start(void)
{
}

const char *updater_version(void)
{
	return HALO_VERSION;
}
