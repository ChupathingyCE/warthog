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
64 MB, with an empty D:\large_caches.txt beside default.xbe, kept in the
low 128 MB, which the GPU reaches. On a 64 MB console that file brings a
message and the Xbox's cache, never a crash.

Only the parse of a total into a class (xbox_memory_class_of and
xbox_memory_delta_class_of) is built on the host (port/xbox/tests).
*/

#include "../include/xbox_memory.h"

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
	/* (delta.h's: 1 up to 64 MB, 2 up to 512 MB, 0 unknown) */
	switch (memory_class)
	{
	case _xbox_memory_class_64mb:
		return 1;
	case _xbox_memory_class_128mb:
	case _xbox_memory_class_256mb:
		return 2;
	default:
		return 0;
	}
}

#ifdef _XBOX

#include <xtl.h>
#include <stdio.h>
#include <string.h>

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

enum
{
	_texture_cache_stock = 0x1600000,
	_texture_cache_128mb = 0x2C00000,
	_texture_cache_256mb = 0x4000000,
	/* (the largest is halo_port_capacity.h's
	HALO_PORT_TEXTURE_CACHE_MAXIMUM_SIZE, the texture cache's arrays' size;
	the GPU's reach, where a raised cache is kept) */
	_texture_cache_maximum_address = 0x7FFFFFF
};

static struct
{
	int known;
	unsigned long total;
	unsigned long available;
	int memory_class;
	int large_caches;
	unsigned long texture_cache_size;
	const wchar_t *message;
	int message_shown;
} memory;

static int file_exists(const char *path)
{
	FILE *file = fopen(path, "r");

	if (!file)
		return 0;
	fclose(file);
	return 1;
}

static void memory_read(void)
{
	MEMORYSTATUS status;
	const char *source = "GlobalMemoryStatus";

	if (memory.known)
		return;
	memory.known = 1;
	memset(&status, 0, sizeof(status));
	status.dwLength = sizeof(status);
	GlobalMemoryStatus(&status);
	memory.total = status.dwTotalPhys;
	memory.available = status.dwAvailPhys;
	if (!memory.total)
	{
		xbox_mm_statistics statistics;

		memset(&statistics, 0, sizeof(statistics));
		statistics.Length = sizeof(statistics);
		source = "MmQueryStatistics";
		if (MmQueryStatistics(&statistics) >= 0)
		{
			memory.total = statistics.TotalPhysicalPages << 12;
			memory.available = statistics.AvailablePages << 12;
		}
	}
	memory.memory_class = xbox_memory_class_of(memory.total);
	memory.large_caches = file_exists("d:\\large_caches.txt");

	memory.texture_cache_size = _texture_cache_stock;
	if (memory.large_caches)
	{
		if (memory.memory_class == _xbox_memory_class_128mb)
			memory.texture_cache_size = _texture_cache_128mb;
		else if (memory.memory_class == _xbox_memory_class_256mb)
			memory.texture_cache_size = _texture_cache_256mb;
		else
		{
			xbox_memory_require(_xbox_memory_class_128mb,
				L"Larger caches (D:\\large_caches.txt) need\r\na console with 128 MB of memory.\r\n"
				L"This one has 64 MB, so the game\r\nkeeps the Xbox's own.");
		}
	}

	/* (platform_log: to the debug monitor now, and into debug.txt once the
	main loop runs) */
	platform_log("memory: %lu MB in all, %lu MB free at the start (%s): the %d MB class; texture cache %lu MB%s",
		memory.total >> 20, memory.available >> 20, source, memory.memory_class, memory.texture_cache_size >> 20,
		memory.texture_cache_size != _texture_cache_stock ? " (raised: D:\\large_caches.txt)" :
		memory.memory_class > _xbox_memory_class_64mb ? " (the Xbox's; D:\\large_caches.txt raises it)" :
		" (the Xbox's)");
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
	/* (the Xbox's own cache anywhere, as it always was) */
	return memory.texture_cache_size == _texture_cache_stock ? 0xFFFFFFFFUL :
		(unsigned long)_texture_cache_maximum_address;
}

int xbox_memory_require(int memory_class, const wchar_t *text)
{
	memory_read();
	if (memory.memory_class >= memory_class)
		return 1;
	platform_log("memory: a feature needs the %d MB class; this console is in the %d MB class", memory_class,
		memory.memory_class);
	/* (one message at a time: the first) */
	if (!memory.message)
		memory.message = text;
	return 0;
}

void xbox_memory_update(void)
{
	if (!memory.message || memory.message_shown || !main_menu_is_active())
		return;
	memory.message_shown = 1;
	display_error_text_deferred(memory.message, -1);
}

int xbox_memory_delta_class(void)
{
	/* TODO(Delta Peer): this console's platform key (struct
	delta_platform_key, port/linux/include/delta.h, once Delta Peer is in ChupathingyCE)
	takes this as its memory_class once Delta Peer is built for the console:
	delta_peer_platform_policy's row for _delta_platform_xbox says 1 today,
	a 128 MB console is 2 */
	return xbox_memory_delta_class_of(xbox_memory_class());
}

#endif
