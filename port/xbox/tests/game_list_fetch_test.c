/*
GAME_LIST_FETCH_TEST.C

The console's fetch (port/xbox/src/xbox_game_list_fetch.c) on the host's
BSD sockets: resolves the list's host, gets the list over plain HTTP, and
prints it parsed, as the console logs it. port/xbox/tests/run.sh.

    game_list_fetch_test [host[:port]]
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/xbox_game_list.h"

int main(int argc, char **argv)
{
	static char buffer[GAME_LIST_RESPONSE_SIZE];
	static struct game_list list;
	struct game_list_response response;
	char host[96];
	unsigned short port = GAME_LIST_PORT;
	unsigned long address, received;
	int result, index;

	snprintf(host, sizeof(host), "%s", GAME_LIST_HOST);
	if (argc > 1 && !game_list_parse_server(argv[1], (unsigned long)strlen(argv[1]), host, sizeof(host), &port))
	{
		fprintf(stderr, "usage: game_list_fetch_test [host[:port]]\n");
		return 2;
	}
	result = game_list_resolve(host, &address);
	if (result != GAME_LIST_OK)
	{
		printf("game list: %s: %s\n", host, game_list_error_string(result));
		return 1;
	}
	result = game_list_fetch(address, port, host, GAME_LIST_PATH, buffer, sizeof(buffer), &received, 15000);
	if (result == GAME_LIST_OK)
		result = game_list_parse_response(buffer, received, &response);
	if (result == GAME_LIST_OK && response.status != 200)
		result = GAME_LIST_ERROR_HTTP;
	if (result == GAME_LIST_OK)
		result = game_list_parse(response.body, response.body_size, &list);
	if (result != GAME_LIST_OK)
	{
		printf("game list: could not get the list (%s)\n", game_list_error_string(result));
		return 1;
	}
	printf("game list: %d games (format %d, %lu bytes)%s\n", list.count, list.format, received,
		list.skipped ? ", some lines left out" : "");
	for (index = 0; index < list.count; index++)
	{
		const struct game_list_game *game = &list.games[index];

		printf("game list: %s | %s | %s | %d/%d | v%u | %s | %s\n", game->name, game->map,
			game->gametype[0] ? game->gametype : "?", game->players, game->maximum_players, game->version,
			game->region[0] ? game->region : "-", game->state);
	}
	return 0;
}
