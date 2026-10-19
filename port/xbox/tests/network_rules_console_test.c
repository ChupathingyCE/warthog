/*
NETWORK_RULES_CONSOLE_TEST.C

The console's handling of network version 32's message 90 (the host's policy
on aiming assist for the mouse and keyboard), as network_rules.c, which the
console builds, answers it: the console is a client that honors the host's
word and never hosts a policy of its own for an internet game (its settings
do not have network.block_mouse_aim_assist, which reads false: allowed). The
console has no mouse, so a policy changes nothing it does, but it must take
the message and drop it from a datagram, as the PC builds do, and its input
entry says what version 32's does.
*/

#include "../../linux/game/network_rules.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

int main(void)
{
	uint8_t message[NETWORK_RULES_AIM_ASSIST_SIZE];
	uint8_t head[NETWORK_RULES_INPUT_HEAD_SIZE];
	int block = -1, player, bsp, no_friendly_fire;
	int32_t tick, host_time;
	int32_t latest = 1000;

	/* the console as a host (a System Link game): its setting is false, the
	message says allowed, and a client of the PC builds reads it */
	network_rules_aim_assist_write(message, 0, 77);
	CHECK(network_rules_aim_assist_read(message, 12, 0, &block) && block == 0);
	network_rules_aim_assist_write(message, 1, 77);
	CHECK(network_rules_aim_assist_read(message, 12, 0, &block) && block == 1);
	/* the console as a client: only the reliable stream, only 12 bytes */
	CHECK(!network_rules_aim_assist_read(message, 12, 1, &block));
	CHECK(!network_rules_aim_assist_read(message, 11, 0, &block));
	CHECK(!network_rules_aim_assist_read(message, 16, 0, &block));
	/* a host of 29 to 31 never speaks: the console stays blocked, and
	nothing of it is changed by the console's having no mouse */
	CHECK(network_rules_aim_assist_blocks(1, 0, 0, 1, 0, 1));
	CHECK(!network_rules_aim_assist_blocks(1, 0, 0, 1, 0, 0));
	CHECK(!network_rules_aim_assist_blocks(1, 1, 0, 1, 0, 1));
	CHECK(!network_rules_aim_assist_blocks(0, 0, 0, 1, 0, 1));
	/* its own game as a host with the setting false: allowed */
	CHECK(!network_rules_aim_assist_blocks(1, 0, 1, 0, 0, 1));

	/* the input entry's third byte: 0 outside co-op (the console has none) */
	memset(head, 0xEE, sizeof(head));
	network_rules_input_head_write(head, 1, 3, 0, 1234, 1200);
	CHECK(head[0] == 1 && head[1] == 3 && head[2] == 0 && head[3] == 0);
	network_rules_input_head_read(head, &player, &bsp, &no_friendly_fire, &tick, &host_time);
	CHECK(player == 1 && bsp == 3 && !no_friendly_fire && tick == 1234 && host_time == 1200);
	/* a PC client's declined friendly fire reads as it is */
	network_rules_input_head_write(head, 0, 0, 1, 5, 4);
	network_rules_input_head_read(head, &player, &bsp, &no_friendly_fire, &tick, &host_time);
	CHECK(no_friendly_fire);

	/* version 31's window: a datagram 450 ticks or more away is dropped, a
	stream's far stamp moves the latest */
	CHECK(network_rules_stamp_window(&latest, 1450, 0, NETWORK_RULES_TIME_WINDOW_TICKS) == 1);
	CHECK(network_rules_stamp_window(&latest, 2000, 0, NETWORK_RULES_TIME_WINDOW_TICKS) == 0);
	CHECK(network_rules_stamp_window(&latest, 2000, 1, NETWORK_RULES_TIME_WINDOW_TICKS) == 2 && latest == 2000);

	if (failures)
		return 1;
	printf("network_rules_console_test: ok\n");
	return 0;
}
