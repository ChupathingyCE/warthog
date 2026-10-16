/*
HALO_XBOX360_FILE_NAMES.H

The desktop platform layer (port/linux/src) opens host paths through the C
runtime: forward slashes, relative to the current folder. The console takes
only backslashes, and has no current folder but its drives (game:\, the
title's folder). In the platform layer's units these calls go to wrappers
that convert the path (port/xbox360/src/x360_system.c). The game's own
units call the Windows build's wrappers instead (halo_windows_file_names.h),
which translate Xbox paths and then call these.
*/

#ifndef __HALO_XBOX360_FILE_NAMES_H
#define __HALO_XBOX360_FILE_NAMES_H

#ifdef HALO_LINUX_PLATFORM_LAYER
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <direct.h>

FILE *halo_xbox360_fopen(const char *path, const char *mode);
FILE *halo_xbox360_freopen(const char *path, const char *mode, FILE *stream);
int halo_xbox360_open(const char *path, int flags, ...);
int halo_xbox360_remove(const char *path);
int halo_xbox360_rename(const char *old_path, const char *new_path);
int halo_xbox360_mkdir(const char *path);
int halo_xbox360_rmdir(const char *path);
int halo_xbox360_access(const char *path, int mode);

#define fopen halo_xbox360_fopen
#define freopen halo_xbox360_freopen
#define open halo_xbox360_open
#define _open halo_xbox360_open
#define remove halo_xbox360_remove
#define unlink halo_xbox360_remove
#define _unlink halo_xbox360_remove
#define rename halo_xbox360_rename
#define mkdir halo_xbox360_mkdir
#define _mkdir halo_xbox360_mkdir
#define rmdir halo_xbox360_rmdir
#define _rmdir halo_xbox360_rmdir
#define access halo_xbox360_access
#define _access halo_xbox360_access
#endif

#endif
