/*
XBOX_MEMORY.C

The console's memory (xbox_memory.h), read the first time anything asks:
physical_memory_allocate, at the start, asks for the texture cache's size.

The total is the system's own answer. GlobalMemoryStatus (xapilib, the
SDK's) gives dwTotalPhys; if it gives nothing, the kernel's
MmQueryStatistics (xboxkrnl, which the SDK's headers leave out) gives the
physical pages. Both count what the kernel uses, so a 128 MB console
reads 128 MB only when the XBE does not ask for 64 (tools/xbox_build.py's
cxbe -LIMIT64MB:no) and the kernel knows the upper 64 MB (a devkit, or a
modified BIOS that supports the upgrade). Allocating 70 MB and freeing it
was suggested as a test; it is not needed with this toolchain, and would
fragment the memory the game then lays out, so it is only the fallback if a
toolchain ever lacks both calls.

What follows from the class: the texture cache (the one cache whose size
the console now sets at run time). It stays the Xbox's 22 MB everywhere by
default: the Xbox's maps fit it. A 128 MB console takes 44 MB, a 256 MB one
64 MB, with console.large_caches in D:\config.toml (xbox_config.h), kept in
the low 128 MB, which the GPU reaches. On a 64 MB console that setting brings a
message and the Xbox's cache, never a crash. A 64 MB console whose free
memory cannot hold the Xbox's caches and still leave Direct3D and the heap
theirs gets a smaller texture cache (down to 19 MB) rather than a failed
start.

A devkit tests the 64 MB path with the XBE's own flag (tools/xbox_package.py
--devkit-64mb: XINIT_LIMIT_DEVKIT_MEMORY), which makes its kernel give the
game 64 MB, as a stock console has. console.simulate_64mb, which held the
memory above 64 MB from inside the game, is replaced by it.

From the first line of main (xbox_memory_startup), each step of the start
is written to D:\debug.txt at once ("boot:" lines: the memory, each cache
as it is made, each large heap allocation), the file closed after each, so
a console that stops before the game's own log opens still leaves where.

Only the parse of a total into a class (xbox_memory_class_of and
xbox_memory_delta_class_of) is built on the host (port/xbox/tests).
*/

#include "../include/xbox_memory.h"

/* XINIT_LIMIT_DEVKIT_MEMORY, in the XBE header's init flags (at 0x124:
tools/xbox_package.py --devkit-64mb) */
#define XBE_INIT_LIMIT_DEVKIT_MEMORY 0x00000004UL

int xbox_memory_class_of(unsigned long total_bytes)
{
	if (!total_bytes)
		return _xbox_memory_class_unknown;
	if (total_bytes < 96UL << 20)
		return _xbox_memory_class_64mb;
	if (total_bytes < 192UL << 20)
		return _xbox_memory_class_128mb;
	return _xbox_memory_class_256mb;
}

int xbox_memory_delta_class_of(int memory_class)
{
	/* (docs/delta.md's: log2 of the megabytes, 0 unknown) */
	switch (memory_class)
	{
	case _xbox_memory_class_64mb:
		return 6;
	case _xbox_memory_class_128mb:
		return 7;
	case _xbox_memory_class_256mb:
		return 8;
	default:
		return 0;
	}
}

#ifdef _XBOX

#include <xtl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../include/xbox_config.h"
#include "../../linux/src/port_config.h"

unsigned long __cdecl DbgPrint(const char *format, ...);
void platform_log(const char *format, ...);
/* the game's (interface/ui_widget.c) */
void display_error_text_deferred(const wchar_t *text, short local_player_index);
unsigned char main_menu_is_active(void);

/* the kernel's (MM_STATISTICS: its Length must be this size, 36 bytes, or
the call fails) */
typedef struct
{
	ULONG Length;
	ULONG TotalPhysicalPages;
	ULONG AvailablePages;
	ULONG VirtualMemoryBytesCommitted;
	ULONG VirtualMemoryBytesReserved;
	ULONG CachePagesCommitted;
	ULONG PoolPagesCommitted;
	ULONG StackPagesCommitted;
	ULONG ImagePagesCommitted;
} xbox_mm_statistics;

typedef char xbox_mm_statistics_size_assert[sizeof(xbox_mm_statistics) == 36 ? 1 : -1];

LONG __stdcall MmQueryStatistics(xbox_mm_statistics *statistics);

/* the game state (the Xbox's 0x345000, a test build of more than 16 players'
too: halo_port_capacity.h, which this unit does not see) */
#define _game_state_size 0x345000

/* a test build of more than 16 players (configure.py --console-players):
what it takes beyond a stock build's, measured on the build (the XBE's
static data, halo.map: 124 KB at 24 players, the netcode's arrays) and
logged where it counts (xbox_memory_log_point) */
#if defined(HALO_CONSOLE_PLAYERS) && HALO_CONSOLE_PLAYERS > 16
#define XBOX_MEMORY_PLAYERS_TEST 1
#endif

enum
{
	_texture_cache_stock = 0x1600000,
	/* the smallest the cache is made on a console short of memory: the
	attract movie borrows 16 MB of it (bink_playback.c's largest pool,
	texture_cache_steal_memory), between two guards of 0x104000 */
	_texture_cache_minimum = 0x1300000,
	_texture_cache_128mb = 0x2C00000,
	_texture_cache_256mb = 0x4000000,
	/* (the largest is halo_port_capacity.h's
	HALO_PORT_TEXTURE_CACHE_MAXIMUM_SIZE, the texture cache's arrays' size;
	the GPU's reach, where a raised cache is kept) */
	_texture_cache_maximum_address = 0x7FFFFFF,

	/* what physical_memory_allocate takes besides the texture cache: the
	game state, the tag cache and the sound cache (physical_memory_map.c's
	sizes, the Xbox game's) */
	_other_caches_size = _game_state_size + 0x1600000 + 0x400000,
	/* what must stay free once the caches are made: the Direct3D device
	(two 640x480 buffers and the depth buffer, 3.5 MB, and its push
	buffer), and the game's and the system's own allocations */
	_free_after_caches = 0x800000,

	/* how often xbox_memory_update looks at the free memory (main loop
	passes), and how far below the last low it must be to be logged */
	_low_check_interval = 300,
	_low_log_step = 0x40000
};

static struct
{
	int known;
	unsigned long total;
	unsigned long available;
	const char *source;
	int memory_class;
	int large_caches;
	unsigned long texture_cache_size;
	int texture_cache_reduced;
	const wchar_t *message;
	int message_shown;
	unsigned long lowest_free;
	long update_count;
#ifdef XBOX_MEMORY_PLAYERS_TEST
	/* the objects' peaks since the last map started */
	long object_sample_count;
	long objects_peak;
	long object_pool_used_peak;
	long object_pool_contiguous_least;
	long objects_maximum;
	long object_pool_size;
#endif
} memory;

/* the commit and time of this build (tools/xbox_stamp.py, regenerated by every ninja run) */
extern const char xbox_build_stamp[];

/* the game's debug.txt lines, which wait in memory (source/cseries/errors.c) */
void error_log_flush(void);

void xbox_boot_log(const char *format, ...)
{
	char text[512];
	va_list arguments;
	FILE *file;

	va_start(arguments, format);
	_vsnprintf(text, sizeof(text) - 1, format, arguments);
	va_end(arguments);
	text[sizeof(text) - 1] = 0;
	DbgPrint("halo: %s\n", text);
	/* (the game's lines first, which wait in memory: errors.c) */
	error_log_flush();
	/* (opened and closed for each line: a console that stops on the next
	line still leaves this one in the file) */
	file = fopen("d:\\debug.txt", "a+b");
	if (file)
	{
		fprintf(file, "boot: %s\r\n", text);
		fclose(file);
	}
}

static void status_read(unsigned long *total, unsigned long *available, const char **source)
{
	MEMORYSTATUS status;

	memset(&status, 0, sizeof(status));
	status.dwLength = sizeof(status);
	GlobalMemoryStatus(&status);
	*total = status.dwTotalPhys;
	*available = status.dwAvailPhys;
	*source = "GlobalMemoryStatus";
	if (!*total)
	{
		xbox_mm_statistics statistics;

		memset(&statistics, 0, sizeof(statistics));
		statistics.Length = sizeof(statistics);
		*source = "MmQueryStatistics";
		if (MmQueryStatistics(&statistics) >= 0)
		{
			*total = statistics.TotalPhysicalPages << 12;
			*available = statistics.AvailablePages << 12;
		}
	}
}

unsigned long xbox_memory_free(void)
{
	unsigned long total, available;
	const char *source;

	status_read(&total, &available, &source);
	return available;
}

static void memory_read(void)
{
	if (memory.known)
		return;
	memory.known = 1;
#ifdef XBOX_MEMORY_PLAYERS_TEST
	/* a test build (configure.py --console-players): its network tables hold
	more than the Xbox's 16 players. The game state stays the Xbox's
	(halo_port_capacity.h); the rest is about 170 KB more of ordinary
	memory, which a 64 MB console is expected to hold, so it starts on
	every console and says so (the first message, shown at the main menu) */
	xbox_boot_log("players: %d, a test build (the Xbox's: 16); game state %lu KB, as the Xbox's",
		HALO_CONSOLE_PLAYERS, (unsigned long)_game_state_size >> 10);
	memory.message = L"Test build: up to 24 players.\r\nExperimental: please send D:\\debug.txt.";
#endif
	status_read(&memory.total, &memory.available, &memory.source);
	if (config_boolean("console.simulate_64mb"))
		xbox_boot_log("console.simulate_64mb is deprecated and ignored: a devkit gets 64 MB from the XBE's flag "
			"(tools/xbox_package.py --devkit-64mb)");
	memory.memory_class = xbox_memory_class_of(memory.total);
	/* a devkit started with XINIT_LIMIT_DEVKIT_MEMORY (the XBE's init flags,
	which the kernel keeps in the header at the image's base) still reports
	its 128 MB in all, but gives the title only a stock console's: it is in
	the 64 MB class */
	if ((*(unsigned long const *)0x00010124UL & XBE_INIT_LIMIT_DEVKIT_MEMORY) &&
		memory.memory_class > _xbox_memory_class_64mb)
	{
		memory.memory_class = _xbox_memory_class_64mb;
	}
	memory.lowest_free = memory.available;
	memory.large_caches = config_boolean("console.large_caches");

	memory.texture_cache_size = _texture_cache_stock;
	if (memory.large_caches && memory.memory_class == _xbox_memory_class_128mb)
		memory.texture_cache_size = _texture_cache_128mb;
	else if (memory.large_caches && memory.memory_class == _xbox_memory_class_256mb)
		memory.texture_cache_size = _texture_cache_256mb;
	else
	{
		if (memory.large_caches)
		{
			xbox_memory_require(_xbox_memory_class_128mb,
				L"Larger caches (console.large_caches) need\r\na console with 128 MB of memory.\r\n"
				L"This one has 64 MB, so the game\r\nkeeps the Xbox's own.");
		}
		/* a stock console (or one that cannot say): the Xbox's cache if
		what is free holds it and still leaves Direct3D and the heap
		theirs, else what does, down to the smallest the game can use */
		if (memory.memory_class <= _xbox_memory_class_64mb &&
			memory.available < _other_caches_size + _free_after_caches + _texture_cache_stock)
		{
			unsigned long room = memory.available > _other_caches_size + _free_after_caches ?
				memory.available - _other_caches_size - _free_after_caches : 0;

			room &= ~0xFFFFFUL;
			memory.texture_cache_size = room > _texture_cache_minimum ? room : _texture_cache_minimum;
#ifdef XBOX_MEMORY_PLAYERS_TEST
			/* a test build of more than 16 players keeps the smallest: the
			room is counted in whole megabytes, and a console a few KB over
			the next would give the cache a megabyte the heap then lacks
			(the 24 players' netcode takes about 170 KB of it) */
			memory.texture_cache_size = _texture_cache_minimum;
#endif
			memory.texture_cache_reduced = 1;
		}
	}

	xbox_boot_log("memory: %lu MB in all, %lu KB free at the start (%s), image %lu KB: the %d MB class",
		memory.total >> 20, memory.available >> 10, memory.source,
		/* (the XBE's header, which the kernel keeps at the image's base:
		its SizeOfImage) */
		*(unsigned long const *)0x0001010CUL >> 10, memory.memory_class);
	xbox_boot_log("memory: texture cache %lu KB%s", memory.texture_cache_size >> 10,
		memory.texture_cache_reduced ? " (less than the Xbox's 22 MB: what this console has free)" :
		memory.texture_cache_size != _texture_cache_stock ? " (raised: console.large_caches)" :
		memory.memory_class > _xbox_memory_class_64mb ? " (the Xbox's; console.large_caches raises it)" :
		" (the Xbox's)");
}

void xbox_memory_startup(void)
{
	xbox_boot_log("start (build %s)", xbox_build_stamp);
	/* (the settings first: the memory's are among them) */
	xbox_config_startup();
	memory_read();
}

/* physical_memory_allocate's: each cache as it is made */
void xbox_memory_allocation(const char *name, unsigned long size, const void *address)
{
	if (address)
		xbox_boot_log("memory: %s %lu KB at %p, %lu KB free", name, size >> 10, address, xbox_memory_free() >> 10);
	else
		xbox_boot_log("memory: %s %lu KB FAILED, %lu KB free", name, size >> 10, xbox_memory_free() >> 10);
}

/* debug_malloc's (cseries/debug_memory.c): the large allocations, and any
that fails */
void xbox_memory_heap_allocation(const char *file, long line, unsigned long size, const void *address)
{
	const char *name = file ? strrchr(file, '\\') : NULL;

	name = name ? name + 1 : file ? file : "?";
	if (address)
		xbox_boot_log("heap: %lu KB for %s,#%ld, %lu KB free", size >> 10, name, line, xbox_memory_free() >> 10);
	else
		xbox_boot_log("heap: %lu KB for %s,#%ld FAILED, %lu KB free", size >> 10, name, line,
			xbox_memory_free() >> 10);
}

#ifdef XBOX_MEMORY_PLAYERS_TEST
/* the game's (saved games/game_state.c, objects/objects.c) */
void game_state_cpu_usage(long *used, long *size);
int objects_usage(long *count, long *maximum, long *pool_free, long *pool_size, long *pool_contiguous_free);

/* the largest run of contiguous physical memory free, to 16 KB: each size
tried is taken and given back at once (XPhysicalAlloc, as the caches and
Direct3D's resources are), so this is only done at the few points that
log it. The heap does not need contiguous memory; Direct3D and the
caches do. */
static unsigned long largest_free_block(unsigned long available)
{
	unsigned long low = 0;
	unsigned long high = (available & ~0x3FFFUL) + 0x4000;

	while (high - low > 0x4000)
	{
		unsigned long middle = (low + (high - low) / 2) & ~0x3FFFUL;
		void *block;

		if (middle <= low)
			middle = low + 0x4000;
		block = XPhysicalAlloc(middle, MAXULONG_PTR, 0, PAGE_READWRITE);
		if (block)
		{
			XPhysicalFree(block);
			low = middle;
		}
		else
		{
			high = middle;
		}
	}
	return low;
}

static void objects_sample(void)
{
	long count, maximum, pool_free, pool_size, pool_contiguous_free;

	if (!objects_usage(&count, &maximum, &pool_free, &pool_size, &pool_contiguous_free))
		return;
	memory.objects_maximum = maximum;
	memory.object_pool_size = pool_size;
	if (!memory.object_sample_count || pool_contiguous_free < memory.object_pool_contiguous_least)
		memory.object_pool_contiguous_least = pool_contiguous_free;
	memory.object_sample_count++;
	if (count > memory.objects_peak)
		memory.objects_peak = count;
	if (pool_size - pool_free > memory.object_pool_used_peak)
		memory.object_pool_used_peak = pool_size - pool_free;
}
#endif

/* a test build's line at the main loop, each map's start (a game joined or
hosted, or the main menu's) and each map's end: the players, the memory
free and its largest block, the game state's use and, at a map's end, the
objects' peaks over it (HALO_PORT_MAXIMUM_OBJECTS_PER_MAP and the object
pool) */
void xbox_memory_log_point(const char *where)
{
#ifdef XBOX_MEMORY_PLAYERS_TEST
	unsigned long available = xbox_memory_free();
	long game_state_used, game_state_size;

	game_state_cpu_usage(&game_state_used, &game_state_size);
	xbox_boot_log("players: %d, %s: %lu KB free, largest free block %lu KB, game state %ld of %ld bytes used",
		HALO_CONSOLE_PLAYERS, where, available >> 10, largest_free_block(available) >> 10,
		game_state_used, game_state_size);
	if (memory.object_sample_count)
	{
		xbox_boot_log("objects: peak %ld of %ld, pool peak %ld KB used of %ld KB, least contiguous free %ld KB "
			"(%ld samples)", memory.objects_peak, memory.objects_maximum,
			memory.object_pool_used_peak >> 10, memory.object_pool_size >> 10,
			memory.object_pool_contiguous_least >> 10, memory.object_sample_count);
	}
	memory.object_sample_count = 0;
	memory.objects_peak = 0;
	memory.object_pool_used_peak = 0;
	memory.object_pool_contiguous_least = 0;
#else
	(void)where;
#endif
}

unsigned long xbox_memory_total(void)
{
	memory_read();
	return memory.total;
}

int xbox_memory_class(void)
{
	memory_read();
	return memory.memory_class;
}

unsigned long xbox_memory_texture_cache_size(void)
{
	memory_read();
	return memory.texture_cache_size;
}

unsigned long xbox_memory_texture_cache_maximum_address(void)
{
	memory_read();
	/* (the Xbox's own cache, or a smaller one, anywhere, as it always
	was) */
	return memory.texture_cache_size <= _texture_cache_stock ? 0xFFFFFFFFUL :
		(unsigned long)_texture_cache_maximum_address;
}

int xbox_memory_require(int memory_class, const wchar_t *text)
{
	memory_read();
	if (memory.memory_class >= memory_class)
		return 1;
	xbox_boot_log("memory: a feature needs the %d MB class; this console is in the %d MB class", memory_class,
		memory.memory_class);
	/* (one message at a time: the first) */
	if (!memory.message)
		memory.message = text;
	return 0;
}

void xbox_memory_update(void)
{
	/* the free memory's new lows, every few seconds: a map's load and a
	match's peak */
#ifdef XBOX_MEMORY_PLAYERS_TEST
	/* the objects in use, twice a second or so */
	if (!(memory.update_count % 15))
		objects_sample();
#endif
	if (++memory.update_count >= _low_check_interval)
	{
		unsigned long available = xbox_memory_free();

		memory.update_count = 0;
		if (available + _low_log_step <= memory.lowest_free)
		{
			memory.lowest_free = available;
			platform_log("memory: %lu KB free (a new low)", available >> 10);
		}
	}
	if (!memory.message || memory.message_shown || !main_menu_is_active())
		return;
	memory.message_shown = 1;
	display_error_text_deferred(memory.message, -1);
}

int xbox_memory_delta_class(void)
{
	/* TODO(Delta Peer): this console's platform key (struct
	delta_platform_key, docs/delta.md) takes this as its memory_class once
	Delta Peer is built for the console */
	return xbox_memory_delta_class_of(xbox_memory_class());
}

#endif
