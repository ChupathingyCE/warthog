/*
XLINK_TEST.C

XLink Kai's address rule (port/xbox/src/xbox_xlink.c) on the host.
port/xbox/tests/run.sh.
*/

#include <stdio.h>
#include <string.h>

#include "../include/xbox_xlink.h"

static int failures;

static void expect(const unsigned char *mac, int ok, unsigned long want)
{
	unsigned long address = 0, mask = 0;
	int result = xbox_xlink_address(mac, &address, &mask);

	if (result != ok || (ok && (address != want || mask != 0xFFFF0000UL || !xbox_xlink_matches(mac, want))))
	{
		failures++;
		fprintf(stderr, "xlink_test: %02x:%02x: got %d %08lx/%08lx\n", mac[4], mac[5], result, address, mask);
	}
}

int main(void)
{
	static const unsigned char example[6] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
	static const unsigned char low[6] = { 0x00, 0x50, 0xF2, 0x12, 0x00, 0x01 };
	static const unsigned char network[6] = { 0x00, 0x50, 0xF2, 0x12, 0x00, 0x00 };
	static const unsigned char broadcast[6] = { 0x00, 0x50, 0xF2, 0x12, 0xFF, 0xFF };

	/* the rule's own example: AA:BB:CC:DD:EE:FF is 10.252.238.255 */
	expect(example, 1, 0x0AFCEEFFUL);
	expect(low, 1, 0x0AFC0001UL);
	expect(network, 0, 0);
	expect(broadcast, 0, 0);
	if (xbox_xlink_matches(example, 0xC0A80114UL))
	{
		failures++;
		fprintf(stderr, "xlink_test: a LAN address matched\n");
	}
	/* debug.txt's line: yes or no by default, Kai's address only detailed */
	{
		char text[192];

		if (!xbox_xlink_report(example, 0x0AFCEEFFUL, 0, text, sizeof(text)) ||
			strcmp(text, "xlink: this console's address is XLink Kai's"))
		{
			failures++;
			fprintf(stderr, "xlink_test: matching report: %s\n", text);
		}
		if (!xbox_xlink_report(example, 0xC0A80114UL, 0, text, sizeof(text)) || strstr(text, "10.252") ||
			!strstr(text, "is not XLink Kai's"))
		{
			failures++;
			fprintf(stderr, "xlink_test: default report: %s\n", text);
		}
		if (!xbox_xlink_report(example, 0xC0A80114UL, 1, text, sizeof(text)) ||
			!strstr(text, "10.252.238.255, mask 255.255.0.0"))
		{
			failures++;
			fprintf(stderr, "xlink_test: detailed report: %s\n", text);
		}
		if (xbox_xlink_report(broadcast, 0xC0A80114UL, 1, text, sizeof(text)))
		{
			failures++;
			fprintf(stderr, "xlink_test: a report with no Kai address\n");
		}
		if (!xbox_xlink_report(example, 0xC0A80114UL, 1, text, 8) || strlen(text) != 7)
		{
			failures++;
			fprintf(stderr, "xlink_test: a report not cut to its buffer\n");
		}
	}
	if (failures)
		return 1;
	printf("xlink_test: ok\n");
	return 0;
}
