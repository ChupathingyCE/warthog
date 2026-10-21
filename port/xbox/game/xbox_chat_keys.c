/*
XBOX_CHAT_KEYS.C

Text chat's keys on the original Xbox (port/linux/game/chat.c, the client's
part): a USB keyboard on a controller port (the game's debug keyboard,
input_xbox.c) says a line with T (everyone) or Y (the team) and shows the
log with H, as the desktop builds' default keys. Without a keyboard the
controller's quick lines (D-pad up and down) are the way to talk. The
console's keys are fixed: config.toml's controls.chat_* are the desktop's.
The game reads halo_keyboard_actions too (input_abstraction.c), for the
desktop's keys that play: the console's has only chat's, which it ignores.
*/

#include "cseries.h"
#include "input/input.h"

#include "halo_keyboard.h"

#ifdef HALO_PORT_CHAT

/* the chat keys held, as the desktop's halo_keyboard_actions has them:
none while a line is typed or the log is open (chat.c reads its keys from
the keyboard's queue then) */
static boolean xbox_chat_keys_typing = FALSE;

unsigned long halo_keyboard_actions(
	short controller_index)
{
	unsigned long actions = 0;

	if (controller_index != 0 || xbox_chat_keys_typing)
		return 0;
	if (input_key_is_down(_key_t))
		actions |= 1UL << HALO_KEYBOARD_CHAT_ALL;
	if (input_key_is_down(_key_y))
		actions |= 1UL << HALO_KEYBOARD_CHAT_TEAM;
	if (input_key_is_down(_key_h))
		actions |= 1UL << HALO_KEYBOARD_CHAT_LOG;
	return actions;
}

void platform_chat_typing(
	int typing)
{
	xbox_chat_keys_typing = typing ? TRUE : FALSE;
}

int platform_chat_escape(
	void)
{
	return input_key_is_down(_key_escape) ? 1 : 0;
}

#else

/* (no chat: the keyboard drives nothing, as on the Xbox) */
unsigned long halo_keyboard_actions(
	short controller_index)
{
	(void)controller_index;
	return 0;
}

#endif
