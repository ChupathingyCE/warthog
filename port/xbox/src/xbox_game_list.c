/*
XBOX_GAME_LIST.C

The game list on the console (xbox_game_list.h): once the game's network is
up (transport_initialize), a thread of its own resolves the list's host,
fetches GET /v1/console/games over plain HTTP (the console has no TLS),
and parses it; the main loop then logs it to debug.txt, as the thread did
to the debug monitor. Nothing waits on it: the game goes on as if there
were no list. Showing the list in a menu, and joining, come later (the
top level README, "Online Games on the Xbox").

No address is ever logged: the host's name, or "the address in
game_list.txt", only.
*/

#include <xtl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/xbox_game_list.h"

ULONG __cdecl DbgPrint(PCSTR format, ...);
int snprintf(char *buffer, size_t size, const char *format, ...);
/* the game's (cseries/errors.c, bungie_net/network/transport_endpoint_set_winsock.c) */
void error(short priority, const char *format, ...);
extern unsigned char transport_initialized;

#define ERROR_LOG 3 /* _error_log: debug.txt only */
#define NETWORK_WAIT 60000
#define FETCH_TIMEOUT 15000
#define ATTEMPTS 3
#define RETRY_WAIT 10000

enum
{
	_list_idle,
	_list_fetching,
	_list_done,
	_list_failed
};

static volatile LONG list_state = _list_idle;
static volatile LONG list_logged = 1;
static struct game_list list;
static int list_error;
static int list_status;

/* the server to ask: the list's host, else D:\game_list.txt's */
static int find_server(unsigned long *address, char *host, int host_size, unsigned short *port)
{
	char line[128];
	FILE *file;
	size_t size;

	snprintf(host, host_size, "%s", GAME_LIST_HOST);
	*port = GAME_LIST_PORT;
	if (game_list_resolve(GAME_LIST_HOST, address) == GAME_LIST_OK)
		return 1;
	file = fopen(GAME_LIST_FALLBACK_FILE, "rb");
	if (!file)
	{
		DbgPrint("halo: game list: %s did not resolve, and there is no %s\n", GAME_LIST_HOST, GAME_LIST_FALLBACK_FILE);
		return 0;
	}
	size = fread(line, 1, sizeof(line), file);
	fclose(file);
	if (!game_list_parse_server(line, (unsigned long)size, host, host_size, port))
	{
		DbgPrint("halo: game list: %s did not resolve, and %s is not a host or address\n", GAME_LIST_HOST,
			GAME_LIST_FALLBACK_FILE);
		return 0;
	}
	if (game_list_resolve(host, address) != GAME_LIST_OK)
	{
		DbgPrint("halo: game list: neither %s nor %s's host resolved\n", GAME_LIST_HOST, GAME_LIST_FALLBACK_FILE);
		return 0;
	}
	DbgPrint("halo: game list: %s did not resolve; using %s's server\n", GAME_LIST_HOST, GAME_LIST_FALLBACK_FILE);
	/* (a dotted address: the request still names the list's host, which the
	server's virtual host answers to) */
	if (host[0] >= '0' && host[0] <= '9')
		snprintf(host, host_size, "%s", GAME_LIST_HOST);
	return 1;
}

static int fetch_once(char *buffer)
{
	struct game_list_response response;
	unsigned long address, received;
	unsigned short port;
	char host[96];
	int result;

	list_status = 0;
	if (!find_server(&address, host, sizeof(host), &port))
		return GAME_LIST_ERROR_RESOLVE;
	result = game_list_fetch(address, port, host, GAME_LIST_PATH, buffer, GAME_LIST_RESPONSE_SIZE, &received,
		FETCH_TIMEOUT);
	if (result != GAME_LIST_OK)
		return result;
	result = game_list_parse_response(buffer, received, &response);
	if (result != GAME_LIST_OK)
		return result;
	list_status = response.status;
	if (response.status != 200)
		return GAME_LIST_ERROR_HTTP;
	return game_list_parse(response.body, response.body_size, &list);
}

static unsigned long __stdcall game_list_thread(void *parameter)
{
	char *buffer;
	unsigned long waited = 0;
	int attempt;
	int result = GAME_LIST_ERROR_CONNECT;

	(void)parameter;
	while (!transport_initialized && waited < NETWORK_WAIT)
	{
		Sleep(500);
		waited += 500;
	}
	if (!transport_initialized)
	{
		DbgPrint("halo: game list: the network did not start; no list\n");
		list_error = GAME_LIST_ERROR_CONNECT;
		list_state = _list_failed;
		return 0;
	}
	buffer = (char *)malloc(GAME_LIST_RESPONSE_SIZE);
	if (!buffer)
	{
		list_error = GAME_LIST_ERROR_TOO_LARGE;
		list_state = _list_failed;
		return 0;
	}
	DbgPrint("halo: game list: getting http://%s%s\n", GAME_LIST_HOST, GAME_LIST_PATH);
	for (attempt = 1; attempt <= ATTEMPTS; attempt++)
	{
		result = fetch_once(buffer);
		if (result == GAME_LIST_OK || result == GAME_LIST_ERROR_FORMAT)
			break;
		DbgPrint("halo: game list: try %d of %d: %s%s\n", attempt, ATTEMPTS, game_list_error_string(result),
			result == GAME_LIST_ERROR_HTTP ? " (an HTTP error status)" : "");
		if (attempt < ATTEMPTS)
			Sleep(RETRY_WAIT);
	}
	free(buffer);
	list_error = result;
	if (result == GAME_LIST_OK)
	{
		int index;

		DbgPrint("halo: game list: %d games (format %d)%s\n", list.count, list.format,
			list.skipped ? ", some lines left out" : "");
		for (index = 0; index < list.count; index++)
		{
			const struct game_list_game *game = &list.games[index];

			DbgPrint("halo: game list: %s | %s | %s | %d/%d | v%u | %s | %s\n", game->name, game->map,
				game->gametype[0] ? game->gametype : "?", game->players, game->maximum_players, game->version,
				game->region[0] ? game->region : "-", game->state);
		}
	}
	else if (result == GAME_LIST_ERROR_CONNECT || result == GAME_LIST_ERROR_RESOLVE || result == GAME_LIST_ERROR_TIMEOUT)
	{
		FILE *bypass = fopen("d:\\bypass_security.txt", "r");

		if (bypass)
			fclose(bypass);
		else
			DbgPrint("halo: game list: (the internet needs XNet's insecure mode: put an empty "
				"d:\\bypass_security.txt beside default.xbe)\n");
	}
	list_state = result == GAME_LIST_OK ? _list_done : _list_failed;
	return 0;
}

void xbox_game_list_request(void)
{
	HANDLE thread;

	/* (the main thread's only: the fetch's thread sets the state once, last,
	an aligned store, after the list) */
	if (list_state == _list_fetching)
		return;
	list_state = _list_fetching;
	list_logged = 0;
	thread = CreateThread(NULL, 0, game_list_thread, NULL, 0, NULL);
	if (!thread)
	{
		list_error = GAME_LIST_ERROR_CONNECT;
		list_state = _list_failed;
		return;
	}
	CloseHandle(thread);
}

void xbox_game_list_update(void)
{
	LONG state = list_state;
	int index;

	if (list_logged || (state != _list_done && state != _list_failed))
		return;
	list_logged = 1;
	if (state == _list_failed)
	{
		error(ERROR_LOG, "game list: could not get the list from %s (%s)", GAME_LIST_HOST,
			game_list_error_string(list_error));
		return;
	}
	error(ERROR_LOG, "game list: %d games from %s", list.count, GAME_LIST_HOST);
	for (index = 0; index < list.count; index++)
	{
		const struct game_list_game *game = &list.games[index];

		error(ERROR_LOG, "game list: %s | %s | %s | %d/%d | v%u | %s | %s", game->name, game->map,
			game->gametype[0] ? game->gametype : "?", game->players, game->maximum_players, game->version,
			game->region[0] ? game->region : "-", game->state);
	}
}
