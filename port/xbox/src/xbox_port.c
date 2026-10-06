/*
XBOX_PORT.C

What the shared sources ask of the native builds' platform layer
(port/linux/src) that the console answers itself, until it has more of that
layer: the settings (their defaults: there is no config.toml yet), the log,
the screen (640x480, no widescreen), and none of the mouse or the high-res
HUD and text. Internet play's platform is port/xbox/src/xbox_p2p.c.
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../linux/include/halo_ui_pointer.h"
#include "../p2p/pthread.h"

unsigned long __cdecl DbgPrint(const char *format, ...);
/* the game's (cseries/errors.c) */
void error(short priority, const char *format, ...);

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

/* ---------- the log: the debug monitor at once, and debug.txt from the
main thread (error() is the game's, not for other threads: internet play's
thread's lines wait for the main loop, xbox_log_flush) */

__declspec(dllimport) unsigned long __stdcall GetCurrentThreadId(void);

enum
{
	LOG_QUEUE_LINES = 32,
	LOG_LINE_SIZE = 256,
};

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static char log_queue[LOG_QUEUE_LINES][LOG_LINE_SIZE];
static long log_queue_count;
static long log_queue_dropped;
static unsigned long log_main_thread;

void platform_log(const char *format, ...)
{
	char text[1024];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	DbgPrint("halo: %s\n", text);
	if (log_main_thread && GetCurrentThreadId() == log_main_thread)
	{
		error(3, "%s", text);
		return;
	}
	pthread_mutex_lock(&log_lock);
	if (log_queue_count < LOG_QUEUE_LINES)
	{
		strncpy(log_queue[log_queue_count], text, LOG_LINE_SIZE - 1);
		log_queue[log_queue_count][LOG_LINE_SIZE - 1] = 0;
		log_queue_count++;
	}
	else
		log_queue_dropped++;
	pthread_mutex_unlock(&log_lock);
}

/* the main loop's (main.c): the other threads' lines into debug.txt */
void xbox_log_flush(void)
{
	char lines[LOG_QUEUE_LINES][LOG_LINE_SIZE];
	long count, dropped, index;

	log_main_thread = GetCurrentThreadId();
	if (!log_queue_count)
		return;
	pthread_mutex_lock(&log_lock);
	count = log_queue_count;
	dropped = log_queue_dropped;
	memcpy(lines, log_queue, (size_t)count * LOG_LINE_SIZE);
	log_queue_count = 0;
	log_queue_dropped = 0;
	pthread_mutex_unlock(&log_lock);
	for (index = 0; index < count; index++)
		error(3, "%s", lines[index]);
	if (dropped)
		error(3, "(%ld more lines not logged)", dropped);
}

void platform_show_message(const char *title, const char *message)
{
	DbgPrint("halo: %s: %s\n", title, message);
}

/* ---------- the trace switch: D:\trace.txt (an empty file beside
default.xbe, as bypass_security.txt) turns on the bring-up traces, the main
loop's progress each second and every hop of internet play's join
("tunnel:" lines), which also name hosts by their Ethernet addresses. Off,
the log has what a player's report needs: stalls, failures, the game's
own events */

int xbox_trace_enabled(void)
{
	static int known = -1;

	if (known < 0)
	{
		FILE *file = fopen("d:\\trace.txt", "r");

		known = file != NULL;
		if (file)
			fclose(file);
	}
	return known;
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
			if (xbox_trace_enabled())
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
	/* internet play (port/xbox/src/xbox_p2p.c): network.online is the
	console's own (D:\\bypass_security.txt); no UPnP, the system's port */
	{ "network.allow_upnp", "false" },
	{ "network.tunnel_port", "0" },
	{ "network.brokers_file", "brokers.txt" },
	{ "network.signalling_brokers", "broker.emqx.io:1883,broker.hivemq.com:1883,test.mosquitto.org:1883" },
	{ "network.stun_servers", "stun.l.google.com:19302,stun.cloudflare.com:3478" },
	{ "debug.hidden_window", "false" },
	{ "debug.exit_after", "0.0" },
	{ "debug.null_renderer", "false" },
	{ "debug.log_addresses", "false" },
};

int xbox_p2p_online(void);

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
	if (!strcmp(name, "network.online"))
		return xbox_p2p_online();
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

/* ---------- internet play: the desktop builds' p2p.c joins (port/xbox/src/
xbox_p2p.c); what it leaves to p2p_lobby.c and p2p_discord.c, which the
console does without */

void p2p_hardware_id_sanitize(char *destination, int size, const char *source);

void p2p_discord_sanitize(char *destination, int size, const char *source, int name)
{
	(void)name;
	p2p_hardware_id_sanitize(destination, size, source);
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

const char *log_address(const unsigned char *bytes, int length, int port, char *text, int size);

/* (log_address.h's, as port/linux/src/log_address.c has it: a sockaddr_in's
address and port, as they are in memory) */
const char *log_address_ipv4(unsigned long network_address, unsigned short network_port, char *text, int size)
{
	unsigned int value = (unsigned int)network_address;
	unsigned char bytes[4];
	unsigned char port[2];

	memcpy(bytes, &value, 4);
	memcpy(port, &network_port, 2);
	return log_address(bytes, 4, network_port ? (port[0] << 8 | port[1]) : -1, text, size);
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

struct bitmap_data;

boolean_type pc_menu_frame_placement(struct bitmap_data const *bitmap, short *x, short *y, short *width,
	short *height)
{
	(void)bitmap;
	(void)x;
	(void)y;
	(void)width;
	(void)height;
	return 0;
}

/* ---------- the desktop renderer's options (d3d8_gl.c): the Xbox's own
renderer has none of them; shadows at the Xbox's 128 texels */

long halo_shadow_map_scale(void)
{
	return 1;
}

void halo_vertex_shader_lighting(unsigned long handle)
{
	(void)handle;
}

void halo_screen_anti_alias(short x0, short y0, short x1, short y1)
{
	(void)x0;
	(void)y0;
	(void)x1;
	(void)y1;
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
