/*
XBOX_DEBUG_TOOLS.C

What the console's build says for a developer tool that is not in it
(halo_xbox_prefix.h, configure.py --xbox-debug-tools): a console command
or script that reaches the tool prints that, and the game goes on.
*/

#include "cseries.h"
#include "main/console.h"

#ifdef HALO_XBOX_NO_DEBUG_TOOLS

void xbox_debug_tools_unavailable(char const *name)
{
	console_printf(FALSE, "%s: not in this build (configure.py --xbox-debug-tools)", name);
	return;
}

#endif
