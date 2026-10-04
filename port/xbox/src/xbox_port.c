/*
XBOX_PORT.C

What the shared sources ask of the native builds' platform layer
(port/linux/src) that the console answers itself, until it has more of that
layer: the settings (their defaults: there is no config.toml yet), the log,
the screen (640x480, no widescreen), and none of the mouse, the high-res
HUD and text, or internet play.
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../linux/include/halo_ui_pointer.h"

unsigned long __cdecl DbgPrint(const char *format, ...);

/* ---------- the C runtime's newer functions */

int vsnprintf(char *buffer, size_t size, const char *format, va_list arguments)
{
	int length;

	if (!size)
		return _vsnprintf(NULL, 0, format, arguments);
	length = _vsnprintf(buffer, size, format, arguments);
	/* (_vsnprintf leaves a full buffer unterminated, and says -1) */
	buffer[size - 1] = 0;
	return length < 0 ? (int)size - 1 : length;
}

int snprintf(char *buffer, size_t size, const char *format, ...)
{
	va_list arguments;
	int length;

	va_start(arguments, format);
	length = vsnprintf(buffer, size, format, arguments);
	va_end(arguments);
	return length;
}

/* round to the nearest integer, halves to even: adding and taking away 2^52
leaves the integer part, rounded as the FPU rounds (to nearest) */
double rint(double x)
{
	const double two52 = 4503599627370496.0;
	volatile double sum;

	if (x >= two52 || x <= -two52 || x != x)
		return x;
	if (x >= 0.0)
	{
		sum = x + two52;
		return sum - two52;
	}
	sum = x - two52;
	return sum + two52;
}

float rintf(float x)
{
	return (float)rint(x);
}

/* ---------- the log */

void platform_log(const char *format, ...)
{
	char text[1024];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	DbgPrint("halo: %s\n", text);
}

void platform_show_message(const char *title, const char *message)
{
	DbgPrint("halo: %s: %s\n", title, message);
}

/* ---------- the bring-up watchdog: the main loop's progress (main.c's
MAIN_STAGE), to the debug output once a second, and where it is when a
pass takes more than three seconds */

typedef void *HANDLE_TYPE;
__declspec(dllimport) HANDLE_TYPE __stdcall CreateThread(void *attributes, unsigned long stack_size,
	unsigned long (__stdcall *start)(void *), void *parameter, unsigned long flags, unsigned long *id);
__declspec(dllimport) void __stdcall Sleep(unsigned long milliseconds);

volatile unsigned long xbox_main_loops;
const char *volatile xbox_main_stage = "start";

static unsigned long __stdcall xbox_watchdog(void *parameter)
{
	unsigned long last_loops = 0, still = 0;

	(void)parameter;
	for (;;)
	{
		unsigned long loops = xbox_main_loops;

		Sleep(1000);
		if (xbox_main_loops == loops)
		{
			if (++still % 3 == 0)
				DbgPrint("halo: main loop stalled %lus at '%s' (loop %lu)\n", still, xbox_main_stage, loops);
		}
		else
		{
			still = 0;
			DbgPrint("halo: main loop %lu (%lu a second), at '%s'\n", xbox_main_loops, xbox_main_loops - last_loops,
				xbox_main_stage);
		}
		last_loops = xbox_main_loops;
	}
	return 0;
}

void xbox_watchdog_start(void)
{
	CreateThread(NULL, 0, xbox_watchdog, NULL, 0, NULL);
}

/* ---------- the settings: their defaults (port/linux/src/port_config.c's)
for those the console's code reads */

static const struct
{
	const char *name;
	const char *value;
} settings[] =
{
	{ "debug.telnet_console", "false" },
	{ "debug.telnet_console_port", "2323" },
	{ "debug.network_test", "" },
	{ "debug.network_test_start", "15.0" },
	{ "debug.network_test_kill", "0.0" },
	{ "debug.network_test_score", "0" },
	{ "debug.network_test_shoot", "0.0" },
	{ "debug.network_test_vehicle", "0.0" },
	{ "debug.network_test_pickup", "0.0" },
	{ "debug.network_test_pickup_weapon", "" },
	{ "display.direct_camera", "true" },
	{ "game.console_log", "important" },
};

static const char *setting(const char *name)
{
	unsigned int index;

	for (index = 0; index < sizeof(settings) / sizeof(settings[0]); index++)
	{
		if (!strcmp(settings[index].name, name))
			return settings[index].value;
	}
	DbgPrint("halo: no setting %s on the console\n", name);
	return "";
}

int config_boolean(const char *name)
{
	return !strcmp(setting(name), "true");
}

long config_integer(const char *name)
{
	return atol(setting(name));
}

double config_real(const char *name)
{
	return atof(setting(name));
}

const char *config_string(const char *name)
{
	return setting(name);
}

/* ---------- the screen: the console's 640x480 */

long halo_screen_width(void)
{
	return 640;
}

long halo_screen_commit(void)
{
	return 640;
}

void halo_screen_ui_offset(unsigned char centered)
{
	(void)centered;
}

/* the game ticks at 30 Hz and draws its ticks: no frames between them */
int halo_interpolation_enabled(void)
{
	return 0;
}

/* ---------- input: the controller only */

int halo_ui_pointer_update(int menus_active, struct halo_ui_pointer *pointer)
{
	(void)menus_active;
	(void)pointer;
	return 0;
}

int halo_linux_mouse_aiming(short gamepad_index)
{
	(void)gamepad_index;
	return 0;
}

int halo_linux_mouse_look(short gamepad_index, float *yaw, float *pitch)
{
	(void)gamepad_index;
	(void)yaw;
	(void)pitch;
	return 0;
}

/* (the automated network tests' held action button) */
void test_input_hold_action(int hold)
{
	(void)hold;
}

/* ---------- the high-res HUD and text: none, the game's own bitmaps */

long hud_hires_asset_count(void)
{
	return 0;
}

long hud_hires_asset_bitmap(long asset)
{
	(void)asset;
	return -1;
}

long hud_hires_asset_fits(long asset, long width, long height)
{
	(void)asset;
	(void)width;
	(void)height;
	return 0;
}

const char *hud_hires_asset_tag(long asset)
{
	(void)asset;
	return "";
}

struct text_hires_glyph;

long text_hires_font(const char *tag_name, float cap_height)
{
	(void)tag_name;
	(void)cap_height;
	return -1;
}

int text_hires_covers(long font, unsigned long code)
{
	(void)font;
	(void)code;
	return 0;
}

int text_hires_glyph(long font, unsigned long code, struct text_hires_glyph *glyph)
{
	(void)font;
	(void)code;
	(void)glyph;
	return 0;
}

void text_hires_register_atlas(const unsigned long *texture, unsigned long width, unsigned long height)
{
	(void)texture;
	(void)width;
	(void)height;
}

/* ---------- internet play (p2p.c): none yet; system link only */

void p2p_hardware_id(char *hex, int size)
{
	if (size > 0)
		hex[0] = 0;
}

void p2p_hardware_id_sanitize(char *destination, int size, const char *source)
{
	if (size <= 0)
		return;
	strncpy(destination, source ? source : "", (size_t)size - 1);
	destination[size - 1] = 0;
}

void p2p_discord_identity(char *id, int id_size, char *name, int name_size)
{
	if (id_size > 0)
		id[0] = 0;
	if (name_size > 0)
		name[0] = 0;
}

void p2p_discord_sanitize(char *destination, int size, const char *source, int name)
{
	(void)name;
	p2p_hardware_id_sanitize(destination, size, source);
}

unsigned long p2p_peer_endpoint_address(unsigned long virtual_address)
{
	(void)virtual_address;
	return 0;
}

void p2p_set_game_player_counts(int count, int maximum)
{
	(void)count;
	(void)maximum;
}

void p2p_set_hosting_allowed(int allowed)
{
	(void)allowed;
}

void p2p_set_hosting_public(int public)
{
	(void)public;
}

void p2p_set_game_listing(const char *name, const char *map, const char *gametype, int engine_type, int open,
	int in_progress, int has_teams)
{
	(void)name;
	(void)map;
	(void)gametype;
	(void)engine_type;
	(void)open;
	(void)in_progress;
	(void)has_teams;
}

/* ---------- the menus: the Xbox's own (ui.map); the PC version's menus
(port/linux/game/menu_tags.c) are the desktop builds' */

typedef unsigned char boolean_type;
struct widget_instance;
struct event_record;

void menu_tags_loaded(char const *map_name)
{
	(void)map_name;
}

void menu_tags_unloaded(void)
{
}

char const *pc_menus_root_name(void)
{
	return "ui\\shell\\main_menu\\main_menu";
}

char const *pc_menus_screen(char const *name)
{
	return name;
}

boolean_type pc_menu_tag(long tag_index)
{
	(void)tag_index;
	return 0;
}

boolean_type pc_menu_text_color(struct widget_instance const *widget, void *rgb)
{
	(void)widget;
	(void)rgb;
	return 0;
}

boolean_type pc_menu_event_function_invoke(struct widget_instance *widget, struct event_record *event,
	long function_index, boolean_type *widget_deleted)
{
	(void)widget;
	(void)event;
	(void)function_index;
	(void)widget_deleted;
	return 0;
}

void pc_menu_game_data_function_invoke(struct widget_instance *widget, long function)
{
	(void)widget;
	(void)function;
}

/* ---------- the keyboard and the desktop's settings: none on the console */

unsigned long halo_keyboard_actions(short controller_index)
{
	(void)controller_index;
	return 0;
}

void platform_text_typing(int typing)
{
	(void)typing;
}

void platform_scoreboard_scroll(int open, long *notches, long *pages)
{
	(void)open;
	if (notches)
		*notches = 0;
	if (pages)
		*pages = 0;
}

float halo_screen_scale(void)
{
	return 1.0f;
}

/* (the settings never change: there is no config.toml) */
unsigned long config_changes(void)
{
	return 0;
}

/* ---------- addresses in the log (port/linux/src/log_address.h): the
private ranges whole, a public address only as a tag salted per run */

__declspec(dllimport) unsigned long __stdcall GetTickCount(void);

static int log_address_private(const unsigned char *bytes, int length)
{
	if (length != 4)
		return length == 16 && (bytes[0] & 0xFE) == 0xFC;
	return bytes[0] == 10 || bytes[0] == 127 || bytes[0] == 0 ||
		(bytes[0] == 172 && (bytes[1] & 0xF0) == 16) || (bytes[0] == 192 && bytes[1] == 168) ||
		(bytes[0] == 169 && bytes[1] == 254) || (bytes[0] == 100 && (bytes[1] & 0xC0) == 64);
}

const char *log_address(const unsigned char *bytes, int length, int port, char *text, int size)
{
	static unsigned long salt;
	char host[48];
	char port_text[8] = "";

	if (size <= 0)
		return text;
	if (port >= 0)
		snprintf(port_text, sizeof(port_text), ":%d", port & 0xFFFF);
	if (length == 4 && log_address_private(bytes, length))
		snprintf(host, sizeof(host), "%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
	else
	{
		unsigned long hash;
		int index;

		if (!salt)
			salt = GetTickCount() * 2654435761UL | 1;
		hash = 2166136261UL ^ salt;
		for (index = 0; index < length; index++)
			hash = (hash ^ bytes[index]) * 16777619UL;
		snprintf(host, sizeof(host), "addr#%06lx", hash & 0xFFFFFF);
	}
	snprintf(text, (size_t)size, "%s%s", host, port_text);
	return text;
}
