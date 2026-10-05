/*
XBOX_BROWSER_SCREEN.C

The Multiplayer menu's ONLINE GAMES on the original Xbox: the game list
(port/xbox/src/xbox_game_list.c, warthog.milenko.org's plain HTTP list) on
a screen of its own over the menus, as the game's virtual keyboard is
(interface/virtual_keyboard.c): drawn and driven by code, in the menus'
own fonts and button icons. The menu item is the desktop builds' (ONLINE
GAMES, interface/ui_widget.c); their screen (port/linux/game/
browser_screen.c) draws through their platform layer, which the console
lacks.

Up and down pick a game, left and right turn the page, X asks for the list
again, B goes back. The selected game's details show below the list. A
joins it as the desktop builds' screen does: through its invite (the list's
first field), internet play's tunnel to its host (port/xbox/src/
xbox_p2p.c, the desktop builds' p2p.c) is made, and once the host's game
is advertised through it, the game is joined and its lobby opens. Y joins
the invite in D:\join.txt the same way (port/xbox/src/xbox_direct_join.c),
for testing. Internet play needs D:\bypass_security.txt.

Memory: the screen's own state is the list's order and a few numbers; the
list itself is the fetch's one copy (xbox_game_list_get).
*/

#ifdef HALO_XBOX_CONSOLE

#include "cseries/cseries.h"
#include "cseries/cseries_windows.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "interface/player_ui.h"
#include "networking/network_game_globals.h"
#include "saved games/player_profile.h"
#include "saved games/saved_game_files.h"
#include "bitmaps/bitmap_group.h"
#include "cutscene/cinematics.h"
#include "input/input.h"
#include "interface/event_manager.h"
#include "interface/ui_widget.h"
#include "rasterizer/rasterizer.h"
#include "tag_files/tag_groups.h"
#include "text/draw_string.h"
#include "text/font_group.h"

#include <stdio.h>
#include <string.h>

#include "../include/xbox_game_list.h"
#include "../include/xbox_direct_join.h"

/* (ui_widget.c's) */
long ui_widget_online_games_font(boolean heading);
/* (ui_widget_event_handler_functions.c's: the network searching, as System
Link's list starts it) */
boolean ui_online_games_start_network(void);
/* (network_client_manager.c's: the game whose host's identifier the invite
starts with, joined once it is advertised) */
long network_game_client_join_invite_host(char const *invite);
boolean create_global_network_game_client(void);
void game_connection_set(short connection);
/* (internet play's, port/linux/src/p2p.c: the tunnel to an invite's host) */
int p2p_join_invite(char const *text);
/* (port/xbox/src/xbox_p2p.c's: whether internet play is on) */
int xbox_p2p_online(void);

/* ---------- constants */

enum
{
	/* (event_manager.c's event types, which it keeps to itself) */
	_browser_event_left_stick = 1,
	_browser_event_button = 3,

	/* (ui_widget.c's private enums) */
	_justify_left = 0,
	_justify_right,
	_justify_center,
	_browser_sound_cursor = 1,
	_browser_sound_forward = 2,
	_browser_sound_back = 3,

	/* the screen takes no A this soon after it opens (the menu's A) */
	OPEN_SETTLE = 600,
	STATUS_DURATION = 5000,
	/* how long the tunnel and the host's advertisement may take (the
	desktop builds' screen waits 15 seconds; the console's DNS and the
	brokers' first answers can take longer) */
	CONNECT_TIMEOUT = 45000,

	/* the 640x480 layout */
	SCREEN_LEFT = 48,
	SCREEN_RIGHT = 592,
	TITLE_TOP = 34,
	RULE_Y = 74,
	HEAD_TOP = 80,
	LIST_TOP = 100,
	LIST_BOTTOM = 296,
	MAXIMUM_ROWS = 9,
	DETAIL_TOP = 318,
	DETAIL_BOTTOM = 426,
	PROMPT_TOP = 438,

	COLUMN_MAP = 250,
	COLUMN_TYPE = 380,
	COLUMN_PLAYERS = 470,
	COLUMN_REGION = 528,

	PICTURE_LEFT = SCREEN_LEFT + 8,
	PICTURE_RIGHT = PICTURE_LEFT + 120,
	DETAIL_TEXT = PICTURE_RIGHT + 14,
};

/* the screen's colors (pixel32, 0xAARRGGBB), the blues of the desktop
builds' screen */
#define COLOR_BACKGROUND 0xF0050B18
#define COLOR_RULE 0xFF2A62C8
#define COLOR_PANEL 0xC0081530
#define COLOR_ROW_SELECTED 0xFF2052B0
#define COLOR_PICTURE 0xFF0A1A33

/* the multiplayer maps' pictures (the menus' mp_map_grafix, in its order:
ui_widget_game_data_input_functions.c) */
static char const *const map_picture_order[] =
{
	"beavercreek", "sidewinder", "damnation", "ratrace", "prisoner", "hangemhigh", "chillout",
	"carousel", "boardingaction", "bloodgulch", "wizard", "putput", "longest",
};

/* ---------- globals */

static struct
{
	boolean active;
	unsigned long opened_time;
	/* the list taken (xbox_game_list_get's count of lists), in the screen's order */
	int lists;
	int count;
	unsigned char order[GAME_LIST_MAXIMUM_GAMES];
	short selected;
	/* the rows a page (as the menu's font fits them: browser_screen_render) */
	short rows;
	/* the selected game's invite, to keep it selected when the list comes again */
	char selected_invite[GAME_LIST_INVITE_LENGTH + 1];
	char status[64];
	unsigned long status_time;
	/* a join under way: the invite's digits and when it started */
	boolean connecting;
	char connecting_invite[GAME_LIST_INVITE_LENGTH + 1];
	unsigned long connecting_time;
} browser_screen;

/* ---------- private code */

static void set_status(
	char const *status)
{
	csstrncpy(browser_screen.status, status, sizeof(browser_screen.status) - 1);
	browser_screen.status[sizeof(browser_screen.status) - 1] = 0;
	browser_screen.status_time = system_milliseconds();
}

static struct game_list_game const *selected_game(
	void)
{
	struct game_list const *list = xbox_game_list_get(NULL);

	if (!list || browser_screen.selected < 0 || browser_screen.selected >= browser_screen.count)
		return NULL;
	return &list->games[browser_screen.order[browser_screen.selected]];
}

/* a list that came since the last look: put in the screen's order, the
selected game kept where it is still listed */
static void take_list(
	void)
{
	int lists;
	struct game_list const *list = xbox_game_list_get(&lists);
	short index;

	if (!list || lists == browser_screen.lists)
		return;
	browser_screen.lists = lists;
	browser_screen.count = game_list_order(list, browser_screen.order);
	for (index = 0; index < browser_screen.count; index++)
	{
		if (!strcmp(list->games[browser_screen.order[index]].invite, browser_screen.selected_invite))
			break;
	}
	browser_screen.selected = index < browser_screen.count ? index : 0;
}

static void remember_selected(
	void)
{
	struct game_list_game const *game = selected_game();

	csstrncpy(browser_screen.selected_invite, game ? game->invite : "", GAME_LIST_INVITE_LENGTH);
	browser_screen.selected_invite[GAME_LIST_INVITE_LENGTH] = 0;
}

static void refresh(
	void)
{
	if (xbox_game_list_state() == XBOX_GAME_LIST_FETCHING)
		return;
	xbox_game_list_request();
}

/* the first player in the game to be joined, with the profile System
Link's Start would pick (as the desktop builds' screen, port/linux/game/
browser_screen.c): the one last used, else the first saved */
static void join_first_player(
	void)
{
	long profile_index = player_ui_get_player1_last_used_profile_index();
	struct player_profile profile;

	player_ui_local_player_joined_multiplayer_game(0);
	if (profile_index == NONE || !TEST_FLAG(profile_index, _saved_game_file_index_valid_bit))
	{
		long profile_indices[100];
		word profile_count = NUMBEROF(profile_indices);

		player_profiles_enumerate_available_to_local_player_index(0, &profile_count, profile_indices, FALSE);
		profile_index = profile_count ? profile_indices[0] : NONE;
	}
	if (profile_index != NONE && TEST_FLAG(profile_index, _saved_game_file_index_valid_bit) &&
		player_profile_get(profile_index, &profile))
	{
		player_ui_set_active_player_profile(0, profile_index, &profile);
	}
}

/* an invite (its 64 digits) joined: the tunnel to its host, then its game
once advertised through it (wait_for_host). The invite is never logged
whole: its first digits only */
static void join_invite(
	char const *invite,
	char const *name)
{
	char link[16 + GAME_LIST_INVITE_LENGTH];

	if (!xbox_p2p_online())
	{
		set_status("Online play needs D:\\bypass_security.txt.");
		error(_error_log, "online games: no internet play without D:\\bypass_security.txt");
		return;
	}
	if (!global_network_game_client_get())
	{
		if (!create_global_network_game_client())
		{
			set_status("Could not start the network.");
			return;
		}
		game_connection_set(_game_connection_network_client);
	}
	join_first_player();
	snprintf(link, sizeof(link), "halo://join/%s", invite);
	if (!p2p_join_invite(link))
	{
		set_status("Internet play is off.");
		return;
	}
	browser_screen.connecting = TRUE;
	csstrncpy(browser_screen.connecting_invite, invite, GAME_LIST_INVITE_LENGTH);
	browser_screen.connecting_invite[GAME_LIST_INVITE_LENGTH] = 0;
	browser_screen.connecting_time = system_milliseconds();
	set_status("Connecting...");
	error(_error_log, "online games: joining %s (invite %.8s...)", name, invite);
}

/* the picked game's host: its game joined once it is advertised, and its
lobby opened */
static void wait_for_host(
	void)
{
	long joined = network_game_client_join_invite_host(browser_screen.connecting_invite);

	if (joined > 0)
	{
		error(_error_log, "online games: the host's game was advertised through the tunnel; joining it");
		browser_screen.connecting = FALSE;
		browser_screen.active = FALSE;
		ui_widgets_close_all();
		ui_widget_load_by_name_or_tag(
			"ui\\shell\\main_menu\\multiplayer_type_select\\connected\\pregame\\connected_pregame_screen",
			NONE, NULL, NONE, NONE, NONE, NONE);
	}
	else if (joined == -2)
	{
		browser_screen.connecting = FALSE;
		set_status("Could not connect to the host.");
		error(_error_log, "online games: the connection to the host's game failed");
	}
	else if (joined < 0)
	{
		browser_screen.connecting = FALSE;
		set_status("That game can't be joined from this version.");
		error(_error_log, "online games: the host's game is not one this version joins");
	}
	else if (system_milliseconds() - browser_screen.connecting_time > CONNECT_TIMEOUT)
	{
		browser_screen.connecting = FALSE;
		set_status("The host did not answer.");
		error(_error_log, "online games: the host's game was not advertised within %d seconds",
			(int)(CONNECT_TIMEOUT / 1000));
	}
}

/* ---------- public code */

boolean browser_screen_active(
	void)
{
	return browser_screen.active;
}

/* the screen opened (the Multiplayer menu's ONLINE GAMES, interface/ui_widget.c) */
void browser_screen_open(
	void)
{
	browser_screen.active = TRUE;
	browser_screen.opened_time = system_milliseconds();
	browser_screen.status[0] = 0;
	if (!browser_screen.rows)
		browser_screen.rows = MAXIMUM_ROWS;
	/* (the list, as it came at the start or last time; a fresh one asked for) */
	browser_screen.lists = 0;
	browser_screen.connecting = FALSE;
	take_list();
	refresh();
	/* the network searching, as System Link's list starts it (the joined
	host's advertisement comes to it through the tunnel) */
	if (!ui_online_games_start_network())
		set_status("Could not start the network.");
	error(_error_log, "online games: opened, %d games listed", browser_screen.count);
	/* (the menu's A, still queued, is not a pick) */
	event_manager_flush();
}

void browser_screen_process(
	void)
{
	struct event_record event;
	short move = 0;

	take_list();
	while (browser_screen.active && get_next_event(&event, NONE))
	{
		if (event.type == _browser_event_left_stick)
		{
			if (event.data.stick.y == SHORT_MAX)
				move = -1;
			else if (event.data.stick.y == SHORT_MIN)
				move = 1;
			else if (event.data.stick.x == SHORT_MIN)
				move = -browser_screen.rows;
			else if (event.data.stick.x == SHORT_MAX)
				move = browser_screen.rows;
		}
		else if (event.type == _browser_event_button)
		{
			switch (event.data.button.index)
			{
			case _gamepad_binary_button_dpad_up: move = -1; break;
			case _gamepad_binary_button_dpad_down: move = 1; break;
			case _gamepad_binary_button_dpad_left: move = -browser_screen.rows; break;
			case _gamepad_binary_button_dpad_right: move = browser_screen.rows; break;
			case _gamepad_analog_button_a:
				if (!browser_screen.connecting && system_milliseconds() - browser_screen.opened_time > OPEN_SETTLE &&
					selected_game())
				{
					struct game_list_game const *game = selected_game();

					ui_play_audio_feedback_sound(_browser_sound_forward);
					if (strcmp(game->state, "open"))
						set_status("That game is not accepting players.");
					else
						join_invite(game->invite, game->name);
				}
				break;
			case _gamepad_analog_button_y:
				if (!browser_screen.connecting && system_milliseconds() - browser_screen.opened_time > OPEN_SETTLE)
				{
					char invite[XBOX_DIRECT_JOIN_INVITE_LENGTH + 1];

					ui_play_audio_feedback_sound(_browser_sound_forward);
					if (xbox_direct_join_invite(invite))
						join_invite(invite, "the game in D:\\join.txt");
					else
						set_status("D:\\join.txt holds no invite.");
				}
				break;
			case _gamepad_analog_button_x:
				ui_play_audio_feedback_sound(_browser_sound_forward);
				error(_error_log, "online games: refresh");
				refresh();
				break;
			case _gamepad_analog_button_b:
				ui_play_audio_feedback_sound(_browser_sound_back);
				/* (B stops a join under way; again, goes back) */
				if (browser_screen.connecting)
				{
					browser_screen.connecting = FALSE;
					set_status("Stopped.");
				}
				else
					browser_screen.active = FALSE;
				break;
			default: break;
			}
		}
		if (move)
		{
			short selected = (short)PIN(browser_screen.selected + move, 0, MAX(0, browser_screen.count - 1));

			if (selected != browser_screen.selected)
			{
				ui_play_audio_feedback_sound(_browser_sound_cursor);
				browser_screen.selected = selected;
				browser_screen.status[0] = 0;
			}
			move = 0;
		}
	}
	remember_selected();
	if (browser_screen.active && browser_screen.connecting)
		wait_for_host();
	/* (the widgets behind take nothing while the screen is up) */
	event_manager_flush();
}

/* ---------- drawing */

static void set_color(
	real_argb_color *color,
	real alpha,
	real red,
	real green,
	real blue)
{
	color->alpha = alpha;
	color->red = red;
	color->green = green;
	color->blue = blue;
}

static void quad(
	short x0,
	short y0,
	short x1,
	short y1,
	pixel32 color)
{
	rectangle2d bounds;

	bounds.x0 = x0;
	bounds.y0 = y0;
	bounds.x1 = x1;
	bounds.y1 = y1;
	draw_quad(&bounds, color);
}

/* ASCII text in a box (clipped to it), in a font and color */
static void text(
	long font,
	short justification,
	real_argb_color const *color,
	short x0,
	short y0,
	short x1,
	short y1,
	char const *string)
{
	wchar_t wide[96];
	rectangle2d bounds;
	short index;

	if (font == NONE || !string)
		return;
	for (index = 0; index < NUMBEROF(wide) - 1 && string[index]; index++)
		wide[index] = (wchar_t)(unsigned char)string[index];
	wide[index] = 0;
	bounds.x0 = x0;
	bounds.y0 = y0;
	bounds.x1 = x1;
	bounds.y1 = y1;
	draw_string_set_draw_mode(font, NONE, justification, 0, color);
	rasterizer_draw_unicode_string(&bounds, &bounds, NULL, 0, wide);
}

/* a font's line, in pixels */
static short line_height(
	long font)
{
	struct font_header *header = font != NONE ? font_definition_get(font) : NULL;

	return header ? (short)(header->ascending_height + header->descending_height) : 16;
}

/* the map's picture (the menus' own), or the box alone for a map the
console has no picture of */
static void draw_map_picture(
	char const *map,
	short y0,
	short y1)
{
	long bitmap_index = tag_loaded('bitm', "ui\\shell\\bitmaps\\mp_map_grafix");
	char file[GAME_LIST_MAP_LENGTH + 1];
	unsigned long start = 0, index;
	short frame = NONE;

	quad(PICTURE_LEFT, y0, PICTURE_RIGHT, y1, COLOR_PICTURE);
	/* (the scenario's name: after the path, and no picture for a Halo PC map) */
	for (index = 0; map[index]; index++)
	{
		if (map[index] == '\\' || map[index] == '/')
			start = index + 1;
	}
	csstrncpy(file, map + start, sizeof(file) - 1);
	file[sizeof(file) - 1] = 0;
	for (index = 0; index < NUMBEROF(map_picture_order); index++)
	{
		if (!_stricmp(file, map_picture_order[index]))
			frame = (short)index;
	}
	if (bitmap_index != NONE && frame != NONE)
	{
		struct bitmap_data *bitmap = bitmap_group_get_bitmap_from_sequence(bitmap_index, 0, frame);

		if (bitmap)
		{
			/* (the picture fills the bitmap's top left, 140 by 116) */
			rectangle2d bounds, art;

			bounds.x0 = PICTURE_LEFT;
			bounds.y0 = y0;
			bounds.x1 = PICTURE_RIGHT;
			bounds.y1 = y1;
			art.x0 = 0;
			art.y0 = 0;
			art.x1 = (short)MIN(140, bitmap->width);
			art.y1 = (short)MIN(116, bitmap->height);
			draw_bitmap_in_rect(bitmap, &bounds, &art, NULL, 0xFFFFFFFF, NULL, FALSE);
		}
	}
}

static void draw_details(
	struct game_list_game const *game,
	long font,
	real_argb_color const *label_color,
	real_argb_color const *text_color,
	real_argb_color const *note_color)
{
	short height = line_height(font) + 2;
	short y = DETAIL_TOP + 6;
	char line[96], value[GAME_LIST_MAP_LENGTH + 8];

	quad(SCREEN_LEFT, DETAIL_TOP, SCREEN_RIGHT, DETAIL_BOTTOM, COLOR_PANEL);
	if (!game)
		return;
	draw_map_picture(game->map, DETAIL_TOP + 6, DETAIL_BOTTOM - 6);
	text(font, _justify_left, text_color, DETAIL_TEXT, y, SCREEN_RIGHT - 8, y + height, game->name);
	y += height;
#define DETAIL_LINE(label, value) \
	text(font, _justify_left, label_color, DETAIL_TEXT, y, DETAIL_TEXT + 80, y + height, label); \
	text(font, _justify_left, text_color, DETAIL_TEXT + 84, y, SCREEN_RIGHT - 8, y + height, value); \
	y += height;
	game_list_map_name(game->map, value, sizeof(value));
	DETAIL_LINE("Map", value);
	DETAIL_LINE("Type", game_list_type_name(game));
	snprintf(line, sizeof(line), "%d of %d", game->players, game->maximum_players);
	DETAIL_LINE("Players", line);
	snprintf(line, sizeof(line), "%s%s%s", game_list_state_name(game->state), game->region[0] ? ", " : "",
		game->region);
	DETAIL_LINE("Status", line);
#undef DETAIL_LINE
	/* (A's answer, a while) */
	if (browser_screen.status[0] && system_milliseconds() - browser_screen.status_time < STATUS_DURATION)
		text(font, _justify_left, note_color, DETAIL_TEXT, y, SCREEN_RIGHT - 8, y + height,
			browser_screen.status);
}

void browser_screen_render(
	void)
{
	long heading_font = ui_widget_online_games_font(TRUE);
	long font = ui_widget_online_games_font(FALSE);
	struct game_list const *list = xbox_game_list_get(NULL);
	struct game_list_game const *selected = selected_game();
	real_argb_color title_color, head_color, text_color, dim_color, note_color;
	short row_height = MAX(line_height(font) + 4, 18);
	short rows = MIN(MAXIMUM_ROWS, (LIST_BOTTOM - LIST_TOP) / row_height);
	short page_first, row;
	long players = 0;
	char line[96], value[GAME_LIST_MAP_LENGTH + 8];
	int state = xbox_game_list_state();
	short index;

	set_color(&title_color, 1.0f, 0.24f, 0.55f, 1.0f);
	set_color(&head_color, 1.0f, 0.5f, 0.69f, 1.0f);
	set_color(&text_color, 1.0f, 0.9f, 0.93f, 0.99f);
	set_color(&dim_color, 1.0f, 0.56f, 0.65f, 0.78f);
	set_color(&note_color, 1.0f, 0.94f, 0.54f, 0.29f);
	if (rows < 1)
		rows = 1;
	browser_screen.rows = rows;

	quad(0, 0, 640, 480, COLOR_BACKGROUND);
	text(heading_font != NONE ? heading_font : font, _justify_left, &title_color, SCREEN_LEFT, TITLE_TOP,
		SCREEN_RIGHT, RULE_Y - 2, "ONLINE GAMES");
	for (index = 0; index < browser_screen.count && list; index++)
		players += list->games[index].players;
	snprintf(line, sizeof(line), "%d GAMES   %ld PLAYERS", browser_screen.count, players);
	text(font, _justify_right, &dim_color, SCREEN_LEFT, RULE_Y - 2 - line_height(font), SCREEN_RIGHT,
		RULE_Y - 2, line);
	quad(SCREEN_LEFT, RULE_Y, SCREEN_RIGHT, RULE_Y + 1, COLOR_RULE);

	/* the list */
	text(font, _justify_left, &head_color, SCREEN_LEFT + 6, HEAD_TOP, COLUMN_MAP - 6, LIST_TOP, "GAME");
	text(font, _justify_left, &head_color, COLUMN_MAP, HEAD_TOP, COLUMN_TYPE - 6, LIST_TOP, "MAP");
	text(font, _justify_left, &head_color, COLUMN_TYPE, HEAD_TOP, COLUMN_PLAYERS - 6, LIST_TOP, "TYPE");
	text(font, _justify_left, &head_color, COLUMN_PLAYERS, HEAD_TOP, COLUMN_REGION - 6, LIST_TOP, "PLAYERS");
	text(font, _justify_left, &head_color, COLUMN_REGION, HEAD_TOP, SCREEN_RIGHT - 6, LIST_TOP, "REGION");
	quad(SCREEN_LEFT, LIST_TOP, SCREEN_RIGHT, LIST_TOP + rows * row_height, COLOR_PANEL);
	page_first = (short)(browser_screen.selected - browser_screen.selected % rows);
	if (!list || !browser_screen.count)
	{
		char const *words =
			state == XBOX_GAME_LIST_FAILED ? "Could not get the game list." :
			state == XBOX_GAME_LIST_FETCHING || !list ? "Getting the game list..." :
			"No one is hosting right now.";

		text(font, _justify_center, &dim_color, SCREEN_LEFT, LIST_TOP + row_height * 2, SCREEN_RIGHT,
			LIST_TOP + row_height * 3, words);
		if (state == XBOX_GAME_LIST_FAILED)
			text(font, _justify_center, &dim_color, SCREEN_LEFT, LIST_TOP + row_height * 3, SCREEN_RIGHT,
				LIST_TOP + row_height * 4, xbox_game_list_error());
	}
	for (row = 0; list && row < rows && page_first + row < browser_screen.count; row++)
	{
		struct game_list_game const *game = &list->games[browser_screen.order[page_first + row]];
		short y0 = (short)(LIST_TOP + row * row_height);
		short y1 = (short)(y0 + row_height);
		short ty = (short)(y0 + (row_height - line_height(font)) / 2);
		real_argb_color const *color = strcmp(game->state, "open") ? &dim_color : &text_color;

		if (page_first + row == browser_screen.selected)
		{
			quad(SCREEN_LEFT, y0, SCREEN_RIGHT, y1, COLOR_ROW_SELECTED);
			color = &text_color;
		}
		text(font, _justify_left, color, SCREEN_LEFT + 6, ty, COLUMN_MAP - 6, y1, game->name);
		game_list_map_name(game->map, value, sizeof(value));
		text(font, _justify_left, color, COLUMN_MAP, ty, COLUMN_TYPE - 6, y1, value);
		text(font, _justify_left, color, COLUMN_TYPE, ty, COLUMN_PLAYERS - 6, y1, game_list_type_name(game));
		snprintf(line, sizeof(line), "%d/%d", game->players, game->maximum_players);
		text(font, _justify_left, color, COLUMN_PLAYERS, ty, COLUMN_REGION - 6, y1, line);
		text(font, _justify_left, color, COLUMN_REGION, ty, SCREEN_RIGHT - 6, y1,
			game->region[0] ? game->region : "-");
	}
	/* under the list: the page, and the list's state */
	{
		short y0 = (short)(LIST_TOP + rows * row_height + 2);
		short y1 = (short)(y0 + line_height(font) + 2);

		if (state == XBOX_GAME_LIST_FETCHING)
			text(font, _justify_left, &dim_color, SCREEN_LEFT + 6, y0, SCREEN_RIGHT, y1, "Refreshing...");
		else if (state == XBOX_GAME_LIST_FAILED && list)
		{
			snprintf(line, sizeof(line), "Could not refresh: %s", xbox_game_list_error());
			text(font, _justify_left, &note_color, SCREEN_LEFT + 6, y0, SCREEN_RIGHT, y1, line);
		}
		snprintf(line, sizeof(line), "PAGE %d OF %d", page_first / rows + 1,
			MAX(1, (browser_screen.count + rows - 1) / rows));
		text(font, _justify_right, &head_color, SCREEN_LEFT, y0, SCREEN_RIGHT - 6, y1, line);
	}

	draw_details(selected, font, &head_color, &text_color, &note_color);

	/* the buttons, as the menus show them */
	if (font != NONE)
	{
		rectangle2d bounds;

		bounds.x0 = SCREEN_LEFT;
		bounds.y0 = PROMPT_TOP;
		bounds.x1 = SCREEN_RIGHT;
		bounds.y1 = 472;
		draw_string_set_draw_mode(font, NONE, _justify_center, 0, &text_color);
		draw_string_and_hack_in_icons(&bounds, &bounds, NULL, 0,
			browser_screen.connecting ? L"Connecting...   %b-button Stop" :
				L"%a-button Join   %x-button Refresh   %y-button join.txt   %b-button Back", FALSE);
	}
}

#endif
