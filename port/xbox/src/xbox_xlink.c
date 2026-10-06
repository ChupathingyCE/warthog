/*
XBOX_XLINK.C

XLink Kai's address rule (xbox_xlink.h). Plain C: port/xbox/tests builds it
on the host too.
*/

#include <string.h>

#include "../include/xbox_xlink.h"

int xbox_xlink_address(const unsigned char *mac, unsigned long *address, unsigned long *mask)
{
	if (!mac || !address || !mask)
		return 0;
	/* (10.252.0.0 is the network, 10.252.255.255 its broadcast) */
	if ((mac[4] == 0 && mac[5] == 0) || (mac[4] == 0xFF && mac[5] == 0xFF))
		return 0;
	*address = 0x0AFC0000UL | (unsigned long)mac[4] << 8 | (unsigned long)mac[5];
	*mask = 0xFFFF0000UL;
	return 1;
}

int xbox_xlink_matches(const unsigned char *mac, unsigned long address)
{
	unsigned long expected, mask;

	return xbox_xlink_address(mac, &expected, &mask) && address == expected;
}

/* a byte in decimal at the end of line */
static void append_byte(char *line, unsigned long value)
{
	char digits[4];
	int count = 0;

	value &= 0xFF;
	do
	{
		digits[count++] = (char)('0' + value % 10);
		value /= 10;
	} while (value);
	line += strlen(line);
	while (count)
		*line++ = digits[--count];
	*line = 0;
}

int xbox_xlink_report(const unsigned char *mac, unsigned long address, int detailed, char *text, int size)
{
	char line[192];
	unsigned long expected, mask;

	if (!text || size <= 0 || !xbox_xlink_address(mac, &expected, &mask))
		return 0;
	strcpy(line, address == expected ? "xlink: this console's address is XLink Kai's" :
		"xlink: this console's address is not XLink Kai's; for system link over Kai, set it in the dashboard");
	if (detailed)
	{
		strcat(line, " (XLink Kai's address for this console is ");
		append_byte(line, expected >> 24);
		strcat(line, ".");
		append_byte(line, expected >> 16);
		strcat(line, ".");
		append_byte(line, expected >> 8);
		strcat(line, ".");
		append_byte(line, expected);
		strcat(line, ", mask 255.255.0.0)");
	}
	else if (address != expected)
		strcat(line, " (D:\\trace.txt shows Kai's address for it)");
	strncpy(text, line, (size_t)size - 1);
	text[size - 1] = 0;
	return 1;
}
