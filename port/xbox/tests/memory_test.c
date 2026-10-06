/*
MEMORY_TEST.C

The memory class of a total (port/xbox/src/xbox_memory.c) on the host:
what the system says of a stock Xbox, a devkit and the bigger mods, and
Delta's class of each. port/xbox/tests/run.sh.
*/

#include <stdio.h>

#include "../include/xbox_memory.h"

static int failures;

static void expect(unsigned long total, int memory_class, int delta_class)
{
	int got = xbox_memory_class_of(total);

	if (got != memory_class || xbox_memory_delta_class_of(got) != delta_class)
	{
		failures++;
		fprintf(stderr, "memory_test: %lu bytes: class %d, Delta %d\n", total, got, xbox_memory_delta_class_of(got));
	}
}

int main(void)
{
	expect(0, _xbox_memory_class_unknown, 0);
	/* a stock Xbox: 16,384 pages, or a little less as a system reports it */
	expect(64UL << 20, _xbox_memory_class_64mb, 1);
	expect((64UL << 20) - 4096, _xbox_memory_class_64mb, 1);
	/* a devkit, or the common upgrade */
	expect(128UL << 20, _xbox_memory_class_128mb, 2);
	expect((128UL << 20) - (1UL << 20), _xbox_memory_class_128mb, 2);
	/* the bigger mods */
	expect(256UL << 20, _xbox_memory_class_256mb, 2);
	expect(192UL << 20, _xbox_memory_class_256mb, 2);
	if (failures)
		return 1;
	printf("memory_test: ok\n");
	return 0;
}
