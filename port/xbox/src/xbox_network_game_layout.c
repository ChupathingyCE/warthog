/*
XBOX_NETWORK_GAME_LAYOUT.C

The game settings record between the PC builds' and the console's slot
counts (xbox_network_game_layout.h).
*/

#include <string.h>

#include "../include/xbox_network_game_layout.h"

#define NONE_BYTE 0xFF

static const char *const error_strings[] =
{
	"ok",
	"a size the layouts don't have",
	"more machines than the slots",
	"more players than the slots",
	"a machine in a slot past the slots",
	"a player in a slot past the slots",
	"an entry naming a machine, slot or team past the slots",
};

const char *network_game_layout_error_string(int error)
{
	return error >= 0 && error < (int)(sizeof(error_strings) / sizeof(error_strings[0])) ?
		error_strings[error] : "unknown error";
}

static int valid_slots(int count)
{
	return count > 0 && count <= NETWORK_GAME_LAYOUT_PC_SLOTS;
}

unsigned long network_game_layout_size(int machines, int players)
{
	return NETWORK_GAME_LAYOUT_MACHINES_OFFSET + (unsigned long)machines * NETWORK_GAME_LAYOUT_MACHINE_SIZE + 2 +
		(unsigned long)players * NETWORK_GAME_LAYOUT_PLAYER_SIZE + NETWORK_GAME_LAYOUT_TAIL_SIZE;
}

static int read_short(const unsigned char *data)
{
	return (short)(data[0] | (data[1] << 8));
}

/* a slot's index byte (signed char): used if 0 up to slots - 1 */
static int index_in(unsigned char value, int slots)
{
	return (signed char)value >= 0 && (signed char)value < slots;
}

/* an index byte a used entry may hold: one within the slots, or NONE */
static int index_fits(unsigned char value, int slots)
{
	return value == NONE_BYTE || index_in(value, slots);
}

int network_game_layout_convert(const unsigned char *from, int from_machines, int from_players,
	unsigned char *to, int to_machines, int to_players, int *clamped)
{
	const unsigned char *from_machine = from + NETWORK_GAME_LAYOUT_MACHINES_OFFSET;
	const unsigned char *from_player_count = from_machine + from_machines * NETWORK_GAME_LAYOUT_MACHINE_SIZE;
	const unsigned char *from_player = from_player_count + 2;
	const unsigned char *from_tail = from_player + from_players * NETWORK_GAME_LAYOUT_PLAYER_SIZE;
	unsigned char *to_machine = to + NETWORK_GAME_LAYOUT_MACHINES_OFFSET;
	unsigned char *to_player_count = to_machine + to_machines * NETWORK_GAME_LAYOUT_MACHINE_SIZE;
	unsigned char *to_player = to_player_count + 2;
	int machine_count, player_count, index;

	if (clamped)
		*clamped = 0;
	if (!from || !to || !valid_slots(from_machines) || !valid_slots(from_players) || !valid_slots(to_machines) ||
		!valid_slots(to_players))
	{
		return NETWORK_GAME_LAYOUT_BAD_SIZE;
	}
	machine_count = read_short(from + NETWORK_GAME_LAYOUT_MACHINE_COUNT_OFFSET);
	player_count = read_short(from_player_count);
	if (machine_count < 0 || machine_count > to_machines)
		return NETWORK_GAME_LAYOUT_TOO_MANY_MACHINES;
	if (player_count < 0 || player_count > to_players)
		return NETWORK_GAME_LAYOUT_TOO_MANY_PLAYERS;

	/* the used slots past the narrower layout's, and what the used ones name */
	for (index = 0; index < from_machines; index++)
	{
		unsigned char machine_index = from_machine[index * NETWORK_GAME_LAYOUT_MACHINE_SIZE + NETWORK_GAME_LAYOUT_MACHINE_INDEX];

		if (!index_in(machine_index, from_machines))
			continue;
		if (index >= to_machines)
			return NETWORK_GAME_LAYOUT_MACHINE_SLOT;
		if (!index_in(machine_index, to_machines))
			return NETWORK_GAME_LAYOUT_INDEX;
	}
	for (index = 0; index < from_players; index++)
	{
		const unsigned char *player = from_player + index * NETWORK_GAME_LAYOUT_PLAYER_SIZE;

		if (!index_in(player[NETWORK_GAME_LAYOUT_PLAYER_LIST_INDEX], from_players))
			continue;
		if (index >= to_players)
			return NETWORK_GAME_LAYOUT_PLAYER_SLOT;
		if (!index_in(player[NETWORK_GAME_LAYOUT_PLAYER_LIST_INDEX], to_players) ||
			!index_fits(player[NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX], to_machines) ||
			!index_fits(player[NETWORK_GAME_LAYOUT_PLAYER_TEAM_INDEX], to_players))
		{
			return NETWORK_GAME_LAYOUT_INDEX;
		}
	}

	/* the header, the slots both layouts have, then empty ones; the tail */
	memmove(to, from, NETWORK_GAME_LAYOUT_MACHINES_OFFSET);
	for (index = 0; index < to_machines; index++)
	{
		unsigned char *machine = to_machine + index * NETWORK_GAME_LAYOUT_MACHINE_SIZE;

		if (index < from_machines)
			memcpy(machine, from_machine + index * NETWORK_GAME_LAYOUT_MACHINE_SIZE, NETWORK_GAME_LAYOUT_MACHINE_SIZE);
		else
		{
			memset(machine, 0, NETWORK_GAME_LAYOUT_MACHINE_SIZE);
			machine[NETWORK_GAME_LAYOUT_MACHINE_INDEX] = NONE_BYTE;
		}
	}
	memcpy(to_player_count, from_player_count, 2);
	for (index = 0; index < to_players; index++)
	{
		unsigned char *player = to_player + index * NETWORK_GAME_LAYOUT_PLAYER_SIZE;

		if (index < from_players)
			memcpy(player, from_player + index * NETWORK_GAME_LAYOUT_PLAYER_SIZE, NETWORK_GAME_LAYOUT_PLAYER_SIZE);
		else
		{
			memset(player, 0, NETWORK_GAME_LAYOUT_PLAYER_SIZE);
			memset(player + NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX, NONE_BYTE, 4);
		}
	}
	memcpy(to_player + to_players * NETWORK_GAME_LAYOUT_PLAYER_SIZE, from_tail, NETWORK_GAME_LAYOUT_TAIL_SIZE);
	if (to[NETWORK_GAME_LAYOUT_MAXIMUM_PLAYERS_OFFSET] > to_players)
	{
		to[NETWORK_GAME_LAYOUT_MAXIMUM_PLAYERS_OFFSET] = (unsigned char)to_players;
		if (clamped)
			*clamped = 1;
	}
	return NETWORK_GAME_LAYOUT_OK;
}
