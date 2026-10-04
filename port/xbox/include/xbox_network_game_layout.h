/*
XBOX_NETWORK_GAME_LAYOUT.H

The game settings record (struct network_game, source/networking/
network_game_manager.h) between the PC builds' layout (128 machines, 128
players: 13,120 bytes) and the console's (16 and 16: 1,920 bytes), byte for
byte (docs/cross-play.md). The record is slot-indexed: machines[i] is the
machine in slot i (its machine_index, i, or NONE), players[i] the player in
slot i (player_list_index i, or NONE). Folding to fewer slots is exact only
while every used slot, and every index a used entry names, fits; else it
fails and nothing is guessed. Widening fills the new slots as the game
leaves an empty one (network_game_invalidate_player).

Plain C on bytes, little-endian, built on the host too (port/xbox/tests).
Not yet called by the game: see docs/cross-play.md, "What is built".
*/

#ifndef __XBOX_NETWORK_GAME_LAYOUT_H
#define __XBOX_NETWORK_GAME_LAYOUT_H

#define NETWORK_GAME_LAYOUT_PC_SLOTS 128
#define NETWORK_GAME_LAYOUT_CONSOLE_SLOTS 16

/* the fixed parts (halo_port_limits.h's HALO_PORT_NETWORK_GAME_*) */
#define NETWORK_GAME_LAYOUT_MACHINES_OFFSET 0x114
#define NETWORK_GAME_LAYOUT_MACHINE_SIZE 0x44
#define NETWORK_GAME_LAYOUT_PLAYER_SIZE 0x20
#define NETWORK_GAME_LAYOUT_TAIL_SIZE 0x2A
/* (in the header) */
#define NETWORK_GAME_LAYOUT_MAXIMUM_PLAYERS_OFFSET 0x10E
#define NETWORK_GAME_LAYOUT_MACHINE_COUNT_OFFSET 0x112
/* (in a machine and a player) */
#define NETWORK_GAME_LAYOUT_MACHINE_INDEX 0x40
#define NETWORK_GAME_LAYOUT_PLAYER_MACHINE_INDEX 0x1C
#define NETWORK_GAME_LAYOUT_PLAYER_CONTROLLER_INDEX 0x1D
#define NETWORK_GAME_LAYOUT_PLAYER_TEAM_INDEX 0x1E
#define NETWORK_GAME_LAYOUT_PLAYER_LIST_INDEX 0x1F

enum
{
	NETWORK_GAME_LAYOUT_OK = 0,
	NETWORK_GAME_LAYOUT_BAD_SIZE,
	NETWORK_GAME_LAYOUT_TOO_MANY_MACHINES,
	NETWORK_GAME_LAYOUT_TOO_MANY_PLAYERS,
	/* a used machine or player slot past the narrower layout's */
	NETWORK_GAME_LAYOUT_MACHINE_SLOT,
	NETWORK_GAME_LAYOUT_PLAYER_SLOT,
	/* a used entry naming a machine, slot or team past it */
	NETWORK_GAME_LAYOUT_INDEX
};

/* the record's size for these slot counts */
unsigned long network_game_layout_size(int machines, int players);

/* the record from (from_machines and from_players slots) into to (to_
machines and to_players), each buffer of its layout's size. Folding: the
gametype's maximum players is held to to_players (*clamped then 1). An
error leaves to unspecified. */
int network_game_layout_convert(const unsigned char *from, int from_machines, int from_players,
	unsigned char *to, int to_machines, int to_players, int *clamped);

const char *network_game_layout_error_string(int error);

#endif
