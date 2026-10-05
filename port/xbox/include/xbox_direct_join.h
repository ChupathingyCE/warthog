/*
XBOX_DIRECT_JOIN.H

D:\join.txt: the address of a system link game's host that the console
joins directly from System Link, without its advertisement, which a host
broadcasts only on its own network (docs/cross-play.md, "Joining"). One
line, an IPv4 address (a.b.c.d); the host's game port (5150) is the
game's. The file is the player's: the address is never logged.
*/

#ifndef __XBOX_DIRECT_JOIN_H
#define __XBOX_DIRECT_JOIN_H

#define XBOX_DIRECT_JOIN_FILE "d:\\join.txt"

/* the address in text (size bytes, its first line, spaces around it
allowed) in host byte order: 1 if it is one a host can have (not 0.0.0.0,
a broadcast, loopback or multicast) */
int xbox_direct_join_parse(const char *text, unsigned long size, unsigned long *address);

/* the address in D:\join.txt (read each time), host byte order: 0 if there
is no file or it holds no such address (told once in debug.txt) */
int xbox_direct_join_address(unsigned long *address);

#endif
