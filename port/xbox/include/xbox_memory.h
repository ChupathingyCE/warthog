/*
XBOX_MEMORY.H

The console's memory (xbox_memory.c): a stock Xbox has 64 MB, a devkit and
most modified consoles 128 MB, a few mods more. Read once at the start from
the system (GlobalMemoryStatus, else the kernel's MmQueryStatistics),
logged, and used for what may grow with it. The game's own limits never
change with it: the network, the game state and the objects stay the
Xbox's (docs/cross-play.md), so every console plays the same game.
*/

#ifndef __XBOX_MEMORY_H
#define __XBOX_MEMORY_H

#include <stddef.h>

enum
{
	_xbox_memory_class_unknown = 0,
	/* a stock Xbox */
	_xbox_memory_class_64mb = 64,
	/* a devkit, or a console with the common upgrade */
	_xbox_memory_class_128mb = 128,
	/* the rarer bigger mods */
	_xbox_memory_class_256mb = 256
};

/* the class of a total in bytes: 64 below 96 MB, 128 below 192 MB, 256
above (pure: the tests build it on the host) */
int xbox_memory_class_of(unsigned long total_bytes);

/* this console's total memory in bytes, and its class (read once) */
unsigned long xbox_memory_total(void);
int xbox_memory_class(void);

/* the texture cache's size (cache/physical_memory_map.c and
xbox_texture_cache.c), and the highest physical address it may have: the
Xbox's 22 MB, unless a 128 MB console has D:\large_caches.txt */
unsigned long xbox_memory_texture_cache_size(void);
unsigned long xbox_memory_texture_cache_maximum_address(void);

/* whether this console has the class a feature needs; if not, the menus
show text (wide characters, the error dialog's) once the main menu is up,
and the caller goes on without the feature. Never a crash */
int xbox_memory_require(int memory_class, const wchar_t *text);

/* the main loop's (main.c): a message from xbox_memory_require shown once
the main menu is up */
void xbox_memory_update(void);

/* Delta Peer's memory class (struct delta_platform_key's memory_class,
port/linux/include/delta.h, once Delta Peer is in ChupathingyCE: 1 up to 64 MB, 2 up to
512 MB): of a class (pure), and this console's */
int xbox_memory_delta_class_of(int memory_class);
int xbox_memory_delta_class(void);

#endif
