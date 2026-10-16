/*
X360_MEMORY.C

The Xbox window (source/cseries/xbox_address.h, HALO_XBOX_WINDOW): one block
of the console's memory holding every Xbox address the game uses. The
console's executable is where the Xbox's memory was (0x82000000), so the
block goes wherever the console has room, and Xbox address X is at host
address xbox_address_space_base + X.

The block is committed whole: the console has no pages backed on first
touch, and its 512 MB leave room for it (port/xbox360/README.md).
*/

#include <xtl.h>
#include <stdio.h>
#include <string.h>

#include "x360_port.h"

/* the host address of Xbox address 0, modulo 2^32 (xbox_address.h) */
unsigned int xbox_address_space_base;

static unsigned char *window_begin;
static size_t window_size;

void *platform_xbox_window_reserve(unsigned int size)
{
	MEMORYSTATUS status;
	void *block;

	if (window_begin)
		return window_begin;
	GlobalMemoryStatus(&status);
	x360_log("memory: %u MB of %u MB free before the Xbox window",
		(unsigned int)(status.dwAvailPhys >> 20), (unsigned int)(status.dwTotalPhys >> 20));
	/* 64 KB pages: fewer TLB misses than 4 KB, and no need for the 16 MB
	pages' alignment */
	block = XPhysicalAlloc(size, MAXULONG_PTR, 0, PAGE_READWRITE | MEM_LARGE_PAGES);
	if (!block)
	{
		x360_log("memory: XPhysicalAlloc of %u MB for the Xbox window failed", size >> 20);
		return NULL;
	}
	memset(block, 0, size);
	window_begin = block;
	window_size = size;
	GlobalMemoryStatus(&status);
	x360_log("memory: Xbox window at host 0x%08x, %u MB; %u MB free after", (unsigned int)(UINT_PTR)block,
		size >> 20, (unsigned int)(status.dwAvailPhys >> 20));
	return block;
}

int x360_window_contains(const void *address, size_t size)
{
	const unsigned char *pointer = address;

	return window_begin && pointer >= window_begin && size <= window_size &&
		(size_t)(pointer - window_begin) <= window_size - size;
}

int x360_window_commit(void *address, size_t size)
{
	return x360_window_contains(address, size);
}

/* bring-up: every cache line of the window written back to memory, where the
GPU reads it */
void x360_window_flush(void)
{
	unsigned char *line;

	if (!window_begin)
		return;
	for (line = window_begin; line < window_begin + window_size; line += 128)
		__asm__ volatile ("dcbst 0, %0" : : "r"(line) : "memory");
	__asm__ volatile ("sync" : : : "memory");
}
