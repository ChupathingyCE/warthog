/*
XBOX_COOP_NAME.C

Whether a variant's name is a co-op game's (port/xbox/game/xbox_coop.c): "Co-op",
alone or followed by a space and more, as other builds' hosts add the
difficulty ("Co-op Heroic"). Apart from the game's headers so the host's tests
run it (port/xbox/tests).
*/

#include <wchar.h>

int xbox_coop_name(wchar_t const *description)
{
	return description && description[0] == L'C' && description[1] == L'o' && description[2] == L'-' &&
		description[3] == L'o' && description[4] == L'p' && (!description[5] || description[5] == L' ');
}
