/*
DIRECT_JOIN_TEST.C

D:\join.txt's parse (port/xbox/src/xbox_direct_join.c) on the host:
addresses taken, and everything else refused. port/xbox/tests/run.sh.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/xbox_direct_join.h"

static int failures;

static void expect(const char *text, unsigned long size, int ok, unsigned long want)
{
	unsigned long address = 0xDEADBEEFUL;
	int result = xbox_direct_join_parse(text, size, &address);

	if (result != ok || (ok && address != want))
	{
		failures++;
		fprintf(stderr, "direct_join_test: \"%.*s\": got %d %08lx\n", (int)size, text, result, address);
	}
	if (!ok && address != 0xDEADBEEFUL)
	{
		failures++;
		fprintf(stderr, "direct_join_test: \"%.*s\": refused but written\n", (int)size, text);
	}
}

#define EXPECT(text, ok, want) expect(text, (unsigned long)strlen(text), ok, want)

int main(void)
{
	char buffer[64];
	unsigned long length;

	EXPECT("192.168.1.20", 1, 0xC0A80114UL);
	EXPECT("  10.0.0.5\t\r\n", 1, 0x0A000005UL);
	EXPECT("203.0.113.7\nsomething else", 1, 0xCB007107UL);
	EXPECT("1.2.3.4", 1, 0x01020304UL);
	/* (not a host's) */
	EXPECT("0.0.0.0", 0, 0);
	EXPECT("255.255.255.255", 0, 0);
	EXPECT("127.0.0.1", 0, 0);
	EXPECT("224.0.0.1", 0, 0);
	EXPECT("0.1.2.3", 0, 0);
	/* (not an address) */
	EXPECT("", 0, 0);
	EXPECT("example.org", 0, 0);
	EXPECT("192.168.1", 0, 0);
	EXPECT("192.168.1.2.3", 0, 0);
	EXPECT("192.168.1.256", 0, 0);
	EXPECT("192.168.1.0020", 0, 0);
	EXPECT("192.168.1.2:5150", 0, 0);
	EXPECT("192. 168.1.2", 0, 0);
	EXPECT("-1.2.3.4", 0, 0);
	/* (every cut of an address, none past its size read: ASan) */
	strcpy(buffer, "172.16.254.3");
	for (length = 0; length < strlen(buffer); length++)
	{
		unsigned long address;
		char exact[64];

		memcpy(exact, buffer, length);
		if (xbox_direct_join_parse(exact, length, &address) && length < 10)
		{
			failures++;
			fprintf(stderr, "direct_join_test: a cut of %lu bytes taken\n", length);
		}
	}
	if (xbox_direct_join_parse(NULL, 4, (unsigned long *)buffer))
		failures++;

	/* invites */
	{
		static const char digits[] = "306400e0a7db2f4f737249587ea5d4c304360ce2006a1d01f04a197ead25e0ca";
		static const char *const good[] =
		{
			"halo://join/306400e0a7db2f4f737249587ea5d4c304360ce2006a1d01f04a197ead25e0ca",
			"  HALO://JOIN/306400E0A7DB2F4F737249587EA5D4C304360CE2006A1D01F04A197EAD25E0CA/ \r\n",
			"306400e0a7db2f4f737249587ea5d4c304360ce2006a1d01f04a197ead25e0ca\nmore",
		};
		static const char *const bad[] =
		{
			"",
			"halo://join/",
			"halo://join/306400e0a7db2f4f737249587ea5d4c304360ce2006a1d01f04a197ead25e0c",
			"halo://join/306400e0a7db2f4f737249587ea5d4c304360ce2006a1d01f04a197ead25e0ca0",
			"halo://join/306400e0a7db2f4f737249587ea5d4c304360ce2006a1d01f04a197ead25e0cg",
			"halo://joim/306400e0a7db2f4f737249587ea5d4c304360ce2006a1d01f04a197ead25e0ca",
			"192.168.1.20",
			"halo://join/3064 0e0a7db2f4f737249587ea5d4c304360ce2006a1d01f04a197ead25e0ca",
		};
		char invite[XBOX_DIRECT_JOIN_INVITE_LENGTH + 1];
		unsigned long index, length;

		for (index = 0; index < sizeof(good) / sizeof(good[0]); index++)
		{
			memset(invite, 'x', sizeof(invite));
			if (!xbox_direct_join_parse_invite(good[index], (unsigned long)strlen(good[index]), invite) ||
				strcmp(invite, digits))
			{
				failures++;
				fprintf(stderr, "direct_join_test: invite %lu not taken\n", index);
			}
		}
		for (index = 0; index < sizeof(bad) / sizeof(bad[0]); index++)
		{
			if (xbox_direct_join_parse_invite(bad[index], (unsigned long)strlen(bad[index]), invite))
			{
				failures++;
				fprintf(stderr, "direct_join_test: bad invite %lu taken\n", index);
			}
		}
		/* (every cut of a link, read no further than its size: ASan) */
		for (length = 0; length < strlen(good[0]); length++)
		{
			char *exact = (char *)malloc(length ? length : 1);

			memcpy(exact, good[0], length);
			if (xbox_direct_join_parse_invite(exact, length, invite))
				failures++;
			free(exact);
		}
	}
	if (failures)
	{
		fprintf(stderr, "direct_join_test: %d failed\n", failures);
		return 1;
	}
	printf("direct_join_test: ok\n");
	return 0;
}
