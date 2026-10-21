/*
XBOX_PLAYER_KEY.H

This copy's player identity, as a ChupathingyCE player has it on a desktop
build: the 32-byte player key of D:\game_list_player.key. The console never
makes a key, nor sends one anywhere: a player puts the file there
themselves (exported from their profile on the site) and the console reads
it. What it keeps is the public player ID (the first 16 bytes of the SHA-256
of a label and the key, as browser.c works it out), which Delta Peer's
PROFILE carries when the player shares their profile. The key itself is
wiped as soon as the ID is worked out, and neither is ever written to the
log.

The file is either the raw 32 bytes (what the desktop builds keep) or the
key as 64 hex digits (the text a transfer in ASCII mode leaves intact, where
a raw file's bytes could be changed), with white space around it.

Plain C: port/xbox/tests builds it on the host too.
*/

#ifndef __XBOX_PLAYER_KEY_H
#define __XBOX_PLAYER_KEY_H

#define XBOX_PLAYER_KEY_SIZE 32
#define XBOX_PLAYER_ID_SIZE 16

/* the key in a file's bytes: 1 and the key, or 0 for a file that is neither
form (or a key of zeros, which no copy makes) */
int xbox_player_key_parse(const unsigned char *data, int size, unsigned char *key);

/* the player ID of a key */
void xbox_player_id_from_key(const unsigned char *key, unsigned char *id);

/* the key in the file at path: 1 and the key, or 0 if there is no such file
or it is not a key. The caller wipes the key */
int xbox_player_key_from_file(const char *path, unsigned char *key);

/* the ID of the key in the file at path: 1, or 0 if there is no such file
or it is not a key. The key is wiped before this returns */
int xbox_player_id_from_file(const char *path, unsigned char *id);

/* this copy's ID (D:\game_list_player.key), read once: 1 with the ID, or 0
for an anonymous copy */
int xbox_player_id(unsigned char *id);

#endif
