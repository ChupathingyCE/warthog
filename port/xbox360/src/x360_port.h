/*
X360_PORT.H

What the console's own units (port/xbox360/src/x360_*.c, compiled against
the console's SDK) give each other and the rest of the build. Only plain C
types cross this line: the game and the desktop platform layer see the
original Xbox's SDK declarations, whose types of the same names differ.
*/

#ifndef __X360_PORT_H
#define __X360_PORT_H

#include <stddef.h>

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif

/* ---------- barriers (the SDK's __sync and __lwsync are __emit, an
intrinsic of Microsoft's compiler alone) */

#define X360_SYNC() __asm__ __volatile__("sync" ::: "memory")
#define X360_LWSYNC() __asm__ __volatile__("lwsync" ::: "memory")

/* ---------- the log: the debug channel (XBDM) and game:\debug.txt */

void x360_debug_print(const char *text);
void x360_log(const char *format, ...);

/* ---------- the Xbox window (x360_memory.c) */

/* whether [address, address + size) is inside the window */
int x360_window_contains(const void *address, size_t size);
/* makes the range usable (the window is committed whole: always 1) */
int x360_window_commit(void *address, size_t size);

/* ---------- errors */

int x360_errno_from_error(unsigned long error);

/* ---------- paths

A host path of the desktop platform layer (forward slashes, relative to
the current folder) as the console takes it: backslashes, and relative
paths below game:\ (the title's folder, its only mounted drive). */
void x360_path(const char *path, char *buffer, size_t size);

/* ---------- controllers (x360_input_pad.c), for xbox360_input.c */

struct x360_pad
{
	unsigned short buttons; /* the original Xbox's XINPUT_GAMEPAD_* digital bits */
	unsigned char analog[8]; /* A, B, X, Y, black, white, left and right trigger */
	short thumb_lx, thumb_ly, thumb_rx, thumb_ry;
};

/* fills pad; 0 if no controller is in that port (0 to 3) */
int x360_pad_read(int port, struct x360_pad *pad);
void x360_pad_rumble(int port, unsigned short left, unsigned short right);

/* ---------- the network (x360_net.c) */

/* starts XNet (insecure: the internet and the PCs) and Winsock, once;
nonzero if they are up */
int x360_network_start(void);

/* ---------- leaving */

void x360_exit_to_launcher(void);

#endif
