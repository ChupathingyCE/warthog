/*
PORT_CONFIG.H (the console's)

The settings internet play's sources read (port/linux/src/port_config.h):
the console has no config.toml, so each is the desktop builds' default,
or the console's own (port/xbox/src/xbox_p2p.c).
*/

#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

#include <stddef.h>

int config_boolean(const char *name);
long config_integer(const char *name);
double config_real(const char *name);
/* never NULL; "" when unset */
const char *config_string(const char *name);
/* the folder settings files are in (the game's, D:\), with its separator */
void config_folder(char *path, size_t size);
/* a whole file, NUL terminated, or NULL; free() it */
char *config_file_read(const char *path, size_t *size);

#endif
