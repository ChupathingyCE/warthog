/*
XBOX_GAME_LIST.H

The game list on the console: warthog.milenko.org's plain HTTP list
(GET /v1/console/games, the site's list for consoles), fetched over
HTTP/1.0 on a thread of its own (xbox_game_list.c), the response and the
list parsed with bounds on everything (xbox_game_list_parse.c). The site's
text is untrusted: no length or count from it is believed past these
limits.

The parsers and the fetch are plain C, so port/xbox/tests builds them on
the host too.
*/

#ifndef __XBOX_GAME_LIST_H
#define __XBOX_GAME_LIST_H

#define GAME_LIST_HOST "warthog.milenko.org"
#define GAME_LIST_PATH "/v1/console/games"
#define GAME_LIST_PORT 80
/* D:\game_list.txt: one line, a host or an IPv4 address, ":port" optional;
used when the list's host does not resolve */
#define GAME_LIST_FALLBACK_FILE "d:\\game_list.txt"

/* the whole response, headers and all, at most */
#define GAME_LIST_RESPONSE_SIZE 65536
/* the response's header lines, at most this many of this many bytes */
#define GAME_LIST_HEADER_LINE 1024
#define GAME_LIST_HEADERS 64
/* the list: games and a line's bytes (its line break apart), as the server's
CONSOLE_GAMES and CONSOLE_LINE */
#define GAME_LIST_MAXIMUM_GAMES 64
#define GAME_LIST_LINE 255
#define GAME_LIST_FORMAT 1
/* the fields' characters, at most (the server's caps) */
#define GAME_LIST_INVITE_LENGTH 64
#define GAME_LIST_NAME_LENGTH 32
#define GAME_LIST_MAP_LENGTH 64
#define GAME_LIST_GAMETYPE_LENGTH 24
#define GAME_LIST_REGION_LENGTH 16
#define GAME_LIST_STATE_LENGTH 8

enum
{
	GAME_LIST_OK = 0,
	/* the HTTP response */
	GAME_LIST_ERROR_STATUS_LINE,
	GAME_LIST_ERROR_HEADER,
	GAME_LIST_ERROR_HEADERS_TRUNCATED,
	GAME_LIST_ERROR_CONTENT_LENGTH,
	GAME_LIST_ERROR_BODY_TRUNCATED,
	GAME_LIST_ERROR_ENCODING,
	/* the list */
	GAME_LIST_ERROR_LIST_HEADER,
	GAME_LIST_ERROR_FORMAT,
	GAME_LIST_ERROR_LIST_TRUNCATED,
	GAME_LIST_ERROR_COUNT,
	/* the fetch */
	GAME_LIST_ERROR_RESOLVE,
	GAME_LIST_ERROR_CONNECT,
	GAME_LIST_ERROR_SEND,
	GAME_LIST_ERROR_RECEIVE,
	GAME_LIST_ERROR_TIMEOUT,
	GAME_LIST_ERROR_TOO_LARGE,
	GAME_LIST_ERROR_HTTP,
	NUMBER_OF_GAME_LIST_ERRORS
};

struct game_list_response
{
	int status;
	/* -1: none given */
	long content_length;
	const char *body;
	unsigned long body_size;
};

struct game_list_game
{
	char invite[GAME_LIST_INVITE_LENGTH + 1];
	char name[GAME_LIST_NAME_LENGTH + 1];
	char map[GAME_LIST_MAP_LENGTH + 1];
	char gametype[GAME_LIST_GAMETYPE_LENGTH + 1];
	char region[GAME_LIST_REGION_LENGTH + 1];
	char state[GAME_LIST_STATE_LENGTH + 1];
	short engine;
	short players;
	short maximum_players;
	unsigned short version;
};

struct game_list
{
	int format;
	int count;
	/* the lines left out (a field missing or out of bounds) */
	int skipped;
	struct game_list_game games[GAME_LIST_MAXIMUM_GAMES];
};

const char *game_list_error_string(int error);

/* an HTTP/1.x response, size bytes (no terminator needed): its status, and
its body (pointing into data) as Content-Length says, else to the end */
int game_list_parse_response(const char *data, unsigned long size, struct game_list_response *response);

/* the list's text, size bytes (no terminator needed) */
int game_list_parse(const char *text, unsigned long size, struct game_list *list);

/* "host[:port]" or "a.b.c.d[:port]" (D:\game_list.txt's line): the host
(at most host_size - 1 characters) and the port; 0 if it is neither */
int game_list_parse_server(const char *text, unsigned long size, char *host, int host_size, unsigned short *port);

/* ---------- the screen's words (xbox_game_list_text.c) */

/* a map as the menus name it ("Blood Gulch" for bloodgulch, or for its
scenario's path; else the file's name, "timberland (PC)" for a Halo PC map,
"(MD)" for HaloMD's) in text, at most size - 1 characters: 1 if it is one of
the console's own multiplayer maps */
int game_list_map_name(const char *map, char *text, unsigned long size);
/* a game's type: its gametype, else its engine's name, else "Unknown" */
const char *game_list_type_name(const struct game_list_game *game);
/* a game's state ("open", ...) in words */
const char *game_list_state_name(const char *state);
/* whether a game is network co-op (its gametype "Co-op", or no engine on a
campaign level), which the console does not play */
int game_list_is_coop(const struct game_list_game *game);
/* the games in the screen's order (most players first, then by name), co-op
games left out, as indices into list->games in order; their count */
int game_list_order(const struct game_list *list, unsigned char *order);

/* ---------- the fetch (xbox_game_list_fetch.c) */

/* the host's IPv4 address in network order: a dotted address as it is,
else looked up (XNet's DNS on the console) */
int game_list_resolve(const char *host, unsigned long *address);

/* GET path from address:port, with Host: host, into buffer (at most size
bytes, the whole response); within timeout_ms */
int game_list_fetch(unsigned long address, unsigned short port, const char *host, const char *path,
	char *buffer, unsigned long size, unsigned long *received, unsigned long timeout_ms);

/* ---------- the console (xbox_game_list.c) */

enum
{
	XBOX_GAME_LIST_NONE,
	XBOX_GAME_LIST_FETCHING,
	XBOX_GAME_LIST_READY,
	XBOX_GAME_LIST_FAILED
};

/* the list fetched once the network is up, on a thread of its own; again
on a later call, once that one's list is taken (xbox_game_list_update) */
void xbox_game_list_request(void);
/* the main thread's part, each loop: a list that came taken (and logged
to debug.txt) */
void xbox_game_list_update(void);
/* the main thread's: the last list taken (NULL before one came; kept while
a refresh fails), and how many have come */
const struct game_list *xbox_game_list_get(int *lists);
/* the latest request's state (XBOX_GAME_LIST_...), and its error's words */
int xbox_game_list_state(void);
const char *xbox_game_list_error(void);

#endif
