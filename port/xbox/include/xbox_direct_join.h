/*
XBOX_DIRECT_JOIN.H

D:\join.txt, one line, for testing joins without the game list:

- an IPv4 address (a.b.c.d): System Link joins that host directly, without
  its advertisement, which a host broadcasts only on its own network; the
  host's game port (5150) is the game's;
- an invite (halo://join/ and 64 hexadecimal digits, or the digits alone):
  ONLINE GAMES' Y joins it through internet play's tunnel, as A joins a
  listed game's.

The file is the player's: neither is ever logged whole.
*/

#ifndef __XBOX_DIRECT_JOIN_H
#define __XBOX_DIRECT_JOIN_H

#define XBOX_DIRECT_JOIN_FILE "d:\\join.txt"

/* the address in text (size bytes, its first line, spaces around it
allowed) in host byte order: 1 if it is one a host can have (not 0.0.0.0,
a broadcast, loopback or multicast) */
int xbox_direct_join_parse(const char *text, unsigned long size, unsigned long *address);

enum
{
	/* an invite's hexadecimal digits (the host's key hash and the token) */
	XBOX_DIRECT_JOIN_INVITE_LENGTH = 64
};

/* the invite in text (size bytes, its first line, spaces around it allowed:
"halo://join/" and the digits, or the digits alone), into invite (its
digits in lower case and a terminator: XBOX_DIRECT_JOIN_INVITE_LENGTH + 1
bytes); 1 if it is one */
int xbox_direct_join_parse_invite(const char *text, unsigned long size, char *invite);

/* the invite in D:\join.txt (read each time); 0 if there is none */
int xbox_direct_join_invite(char *invite);

/* the address in D:\join.txt (read each time), host byte order: 0 if there
is no file or it holds no such address (told once in debug.txt) */
int xbox_direct_join_address(unsigned long *address);

#endif
