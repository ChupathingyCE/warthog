/*
XBOX_DIRECT_JOIN.C

D:\join.txt (xbox_direct_join.h): read by network_client_manager.c while
System Link searches. Only the parse is built on the host
(port/xbox/tests).
*/

#include "../include/xbox_direct_join.h"

int xbox_direct_join_parse(const char *text, unsigned long size, unsigned long *address)
{
	unsigned long start = 0, end, cursor, value = 0;
	int part;

	if (!text || !address)
		return 0;
	for (end = 0; end < size && text[end] != '\n' && text[end] != '\r' && text[end]; end++)
		;
	while (start < end && (text[start] == ' ' || text[start] == '\t'))
		start++;
	while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t'))
		end--;
	cursor = start;
	for (part = 0; part < 4; part++)
	{
		unsigned long number = 0;
		int digits = 0;

		while (cursor < end && text[cursor] >= '0' && text[cursor] <= '9' && digits < 3)
		{
			number = number * 10 + (unsigned long)(text[cursor++] - '0');
			digits++;
		}
		if (!digits || number > 255)
			return 0;
		value = value << 8 | number;
		if (part < 3)
		{
			if (cursor >= end || text[cursor] != '.')
				return 0;
			cursor++;
		}
	}
	if (cursor != end)
		return 0;
	/* (no host has these: nobody, everyone, this machine, a group) */
	if (value == 0 || value == 0xFFFFFFFFUL || (value >> 24) == 127 || (value >> 24) == 0 || (value >> 28) == 0xE)
		return 0;
	*address = value;
	return 1;
}

int xbox_direct_join_parse_invite(const char *text, unsigned long size, char *invite)
{
	static const char prefix[] = "halo://join/";
	unsigned long start = 0, end, index;

	if (!text || !invite)
		return 0;
	for (end = 0; end < size && text[end] != '\n' && text[end] != '\r' && text[end]; end++)
		;
	while (start < end && (text[start] == ' ' || text[start] == '\t'))
		start++;
	while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '/'))
		end--;
	if (end - start > sizeof(prefix) - 1)
	{
		for (index = 0; index < sizeof(prefix) - 1; index++)
		{
			char c = text[start + index];

			if ((c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c) != prefix[index])
				break;
		}
		if (index == sizeof(prefix) - 1)
			start += index;
	}
	if (end - start != XBOX_DIRECT_JOIN_INVITE_LENGTH)
		return 0;
	for (index = 0; index < XBOX_DIRECT_JOIN_INVITE_LENGTH; index++)
	{
		char c = text[start + index];

		if (c >= 'A' && c <= 'F')
			c = (char)(c - 'A' + 'a');
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
			return 0;
		invite[index] = c;
	}
	invite[XBOX_DIRECT_JOIN_INVITE_LENGTH] = 0;
	return 1;
}

#ifdef _XBOX

#include <stdio.h>

/* the game's (cseries/errors.c) */
void error(short priority, const char *format, ...);

#define ERROR_LOG 3 /* _error_log: debug.txt only */

int xbox_direct_join_address(unsigned long *address)
{
	static int told = 0;
	char line[64];
	FILE *file = fopen(XBOX_DIRECT_JOIN_FILE, "rb");
	size_t size;

	if (!file)
		return 0;
	size = fread(line, 1, sizeof(line), file);
	fclose(file);
	if (!xbox_direct_join_parse(line, (unsigned long)size, address))
	{
		char invite[XBOX_DIRECT_JOIN_INVITE_LENGTH + 1];

		/* (an invite is ONLINE GAMES' to join: xbox_direct_join_invite) */
		if (!told && !xbox_direct_join_parse_invite(line, (unsigned long)size, invite))
			error(ERROR_LOG, "cross-play: %s holds neither a host's IPv4 address (a.b.c.d) nor an invite; not joining by it",
				XBOX_DIRECT_JOIN_FILE);
		told = 1;
		return 0;
	}
	return 1;
}

int xbox_direct_join_invite(char *invite)
{
	char line[128];
	FILE *file = fopen(XBOX_DIRECT_JOIN_FILE, "rb");
	size_t size;

	if (!file)
		return 0;
	size = fread(line, 1, sizeof(line), file);
	fclose(file);
	return xbox_direct_join_parse_invite(line, (unsigned long)size, invite);
}

#endif
