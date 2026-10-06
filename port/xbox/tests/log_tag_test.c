/*
LOG_TAG_TEST.C

The log's tags (port/xbox/src/xbox_log_tag.c) on the host: stable for the
same bytes and salt, different for other bytes or another run's salt, the
same on 32- and 64-bit machines, never the bytes themselves, and cut to the
buffer. port/xbox/tests/run.sh.
*/

#include <stdio.h>
#include <string.h>

#include "../include/xbox_log_tag.h"

static int failures;

#define CHECK(condition) do { if (!(condition)) { failures++; fprintf(stderr, "log_tag_test: line %d: %s\n", __LINE__, #condition); } } while (0)

int main(void)
{
	static const unsigned char mac[6] = { 0x00, 0x50, 0xF2, 0x12, 0x34, 0x56 };
	static const unsigned char other[6] = { 0x00, 0x50, 0xF2, 0x12, 0x34, 0x57 };
	char a[32], b[32], c[32], d[32], small[6];

	xbox_log_tag("enet", mac, 6, 12345UL, a, sizeof(a));
	xbox_log_tag("enet", mac, 6, 12345UL, b, sizeof(b));
	xbox_log_tag("enet", other, 6, 12345UL, c, sizeof(c));
	xbox_log_tag("enet", mac, 6, 54321UL, d, sizeof(d));
	CHECK(strlen(a) == 11 && !strncmp(a, "enet#", 5));
	CHECK(!strcmp(a, b));
	CHECK(strcmp(a, c));
	CHECK(strcmp(a, d));
	/* (FNV-1a in 32 bits: the same tag whatever unsigned long's size) */
	xbox_log_tag("enet", mac, 6, 0UL, d, sizeof(d));
	CHECK(!strcmp(d, "enet#e30547"));
	/* never the address's hex, nor any of its bytes in order */
	CHECK(!strstr(a, "0050f2") && !strstr(a, "123456") && !strstr(a, "3456"));
	xbox_log_tag("addr", mac, 4, 12345UL, d, sizeof(d));
	CHECK(!strncmp(d, "addr#", 5) && strlen(d) == 11);
	xbox_log_tag("enet", mac, 6, 12345UL, small, sizeof(small));
	CHECK(!strcmp(small, "enet#"));
	CHECK(xbox_log_tag("enet", mac, 6, 1UL, small, 0) == small);
	if (failures)
		return 1;
	printf("log_tag_test: ok\n");
	return 0;
}
