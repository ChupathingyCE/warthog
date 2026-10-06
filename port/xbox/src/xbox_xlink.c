/*
XBOX_XLINK.C

XLink Kai's address rule (xbox_xlink.h). Plain C: port/xbox/tests builds it
on the host too.
*/

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
