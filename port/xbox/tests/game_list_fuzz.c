/*
GAME_LIST_FUZZ.C

libFuzzer's entry for the game list parsers (port/xbox/tests/run.sh):
each input parsed as a response and its body as a list, and as a list
alone.
*/

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../include/xbox_game_list.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	static struct game_list list;
	struct game_list_response response;
	char host[64];
	unsigned short port;
	int index;

	if (size > GAME_LIST_RESPONSE_SIZE)
		return 0;
	if (game_list_parse_response((const char *)data, (unsigned long)size, &response) == GAME_LIST_OK)
	{
		if ((const uint8_t *)response.body < data || (const uint8_t *)response.body + response.body_size > data + size)
			__builtin_trap();
		game_list_parse(response.body, response.body_size, &list);
	}
	if (game_list_parse((const char *)data, (unsigned long)size, &list) == GAME_LIST_OK)
	{
		if (list.count < 0 || list.count > GAME_LIST_MAXIMUM_GAMES)
			__builtin_trap();
		for (index = 0; index < list.count; index++)
		{
			if (strlen(list.games[index].name) > GAME_LIST_NAME_LENGTH ||
				strlen(list.games[index].map) > GAME_LIST_MAP_LENGTH)
			{
				__builtin_trap();
			}
		}
	}
	game_list_parse_server((const char *)data, (unsigned long)size, host, sizeof(host), &port);
	return 0;
}
