/*
X360_PROFILE.C

Where a frame's time goes, without the debugger: the main loop and the
game's own steps name the stage they enter (frame_profile.h), each stage
gets the time until the next one starts, and every ten seconds the log has
the costliest stages in milliseconds a pass of the main loop.

Flat on purpose: a stage runs until the next one is named, so a label
inside a function covers the rest of it and whatever its caller does until
the caller names its next stage. One thread (the game's) only.
*/

#include <xtl.h>
#include <stdlib.h>
#include <string.h>

#include "x360_port.h"

enum
{
	PROFILE_SLOTS = 256,
	PROFILE_REPORT_MS = 10000,
	PROFILE_REPORT_LINES = 14,
};

struct profile_slot
{
	const char *name;
	double ms;
	unsigned long entries;
};

static struct
{
	struct profile_slot slots[PROFILE_SLOTS];
	const char *current;
	LARGE_INTEGER entered;
	double ms_per_count;
	unsigned long passes;
	DWORD last_report;
	int full_warned;
} profile;

/* the stage's slot, found by its name's address (each label is a literal) */
static struct profile_slot *profile_slot(const char *name)
{
	unsigned long index = ((unsigned long)name >> 2) % PROFILE_SLOTS;
	unsigned long tries;

	for (tries = 0; tries < PROFILE_SLOTS; tries++, index = (index + 1) % PROFILE_SLOTS)
	{
		struct profile_slot *slot = &profile.slots[index];

		if (slot->name == name)
			return slot;
		if (!slot->name)
		{
			slot->name = name;
			return slot;
		}
	}
	if (!profile.full_warned)
	{
		profile.full_warned = 1;
		x360_log("profile: more than %d stages; the rest are not counted", PROFILE_SLOTS);
	}
	return NULL;
}

void x360_profile_stage(const char *name)
{
	LARGE_INTEGER now;

	QueryPerformanceCounter(&now);
	if (profile.ms_per_count == 0.0)
	{
		LARGE_INTEGER frequency;

		QueryPerformanceFrequency(&frequency);
		profile.ms_per_count = 1000.0 / (double)frequency.QuadPart;
	}
	if (profile.current)
	{
		struct profile_slot *slot = profile_slot(profile.current);

		if (slot)
		{
			slot->ms += (double)(now.QuadPart - profile.entered.QuadPart) * profile.ms_per_count;
			slot->entries++;
		}
	}
	profile.current = name;
	QueryPerformanceCounter(&profile.entered);
}

static int profile_compare(const void *a, const void *b)
{
	const struct profile_slot *x = a, *y = b;

	return x->ms < y->ms ? 1 : x->ms > y->ms ? -1 : 0;
}

/* a pass of the main loop (xbox360_platform.c's pulse): every few seconds
the costliest stages, then a fresh count */
void x360_profile_pass(void)
{
	DWORD now = GetTickCount();
	struct profile_slot sorted[PROFILE_SLOTS];
	double total = 0.0;
	int i, count = 0;

	profile.passes++;
	if (!profile.last_report)
		profile.last_report = now;
	if (now - profile.last_report < PROFILE_REPORT_MS)
		return;

	for (i = 0; i < PROFILE_SLOTS; i++)
	{
		if (profile.slots[i].name && profile.slots[i].ms > 0.0)
		{
			sorted[count++] = profile.slots[i];
			total += profile.slots[i].ms;
		}
	}
	qsort(sorted, count, sizeof(sorted[0]), profile_compare);
	x360_log("profile: %lu passes in %lu ms, %.1f ms a pass; the costliest stages (ms a pass, entries a pass):",
		profile.passes, (unsigned long)(now - profile.last_report), total / profile.passes);
	for (i = 0; i < count && i < PROFILE_REPORT_LINES; i++)
	{
		x360_log("profile: %6.2f ms %5.1f %s", sorted[i].ms / profile.passes,
			(double)sorted[i].entries / profile.passes, sorted[i].name);
	}
	for (i = 0; i < PROFILE_SLOTS; i++)
	{
		profile.slots[i].ms = 0.0;
		profile.slots[i].entries = 0;
	}
	profile.passes = 0;
	profile.last_report = now;
}
