/*
X360_FILES.C

The file half of port/linux/src/posix.h on the console's file functions:
what posix_files.c gives the desktop builds and win32_files.c the Windows
build. Paths come from the desktop platform layer (forward slashes, relative
to the current folder) and are made the console's (x360_path).
*/

#include <xtl.h>
#include <errno.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "posix.h"
#include "x360_port.h"

/* ---------- times */

/* FILETIME (100 ns since 1601) to seconds and nanoseconds since 1970 */
static void unix_time(const FILETIME *time, posix_ulong *seconds, posix_ulong *nanoseconds)
{
	unsigned long long intervals = ((unsigned long long)time->dwHighDateTime << 32) | time->dwLowDateTime;

	if (intervals < 116444736000000000ULL)
	{
		*seconds = 0;
		*nanoseconds = 0;
		return;
	}
	intervals -= 116444736000000000ULL;
	*seconds = (posix_ulong)(intervals / 10000000ULL);
	*nanoseconds = (posix_ulong)(intervals % 10000000ULL) * 100;
}

static void file_time(posix_ulong seconds, posix_ulong nanoseconds, FILETIME *time)
{
	unsigned long long intervals = (unsigned long long)seconds * 10000000ULL + nanoseconds / 100 +
		116444736000000000ULL;

	time->dwLowDateTime = (DWORD)intervals;
	time->dwHighDateTime = (DWORD)(intervals >> 32);
}

static void fill_information(DWORD attributes, DWORD size_low, DWORD size_high, const FILETIME *creation,
	const FILETIME *access, const FILETIME *modification, struct posix_file_information *information)
{
	memset(information, 0, sizeof(*information));
	if (attributes & FILE_ATTRIBUTE_DIRECTORY)
		information->flags |= _posix_file_is_directory;
	if (attributes & FILE_ATTRIBUTE_READONLY)
		information->flags |= _posix_file_is_read_only;
	information->size_low = size_low;
	information->size_high = size_high;
	unix_time(modification, &information->modification_seconds, &information->modification_nanoseconds);
	unix_time(access, &information->access_seconds, &information->access_nanoseconds);
	unix_time(creation, &information->creation_seconds, &information->creation_nanoseconds);
}

/* a drive's root ("game:\") is a directory GetFileAttributes may not report */
static int is_drive_root(const char *path)
{
	size_t length = strlen(path);

	return length >= 2 && ((path[length - 1] == ':') || (path[length - 1] == '\\' && path[length - 2] == ':'));
}

int posix_stat(const char *path, struct posix_file_information *information)
{
	char console_path[MAX_PATH];
	WIN32_FILE_ATTRIBUTE_DATA data;

	x360_path(path, console_path, sizeof(console_path));
	if (is_drive_root(console_path))
	{
		memset(information, 0, sizeof(*information));
		information->flags = _posix_file_is_directory;
		return 0;
	}
	if (!GetFileAttributesExA(console_path, GetFileExInfoStandard, &data))
	{
		errno = x360_errno_from_error(GetLastError());
		return -1;
	}
	fill_information(data.dwFileAttributes, data.nFileSizeLow, data.nFileSizeHigh, &data.ftCreationTime,
		&data.ftLastAccessTime, &data.ftLastWriteTime, information);
	return 0;
}

int posix_fstat(int descriptor, struct posix_file_information *information)
{
	HANDLE handle = (HANDLE)_get_osfhandle(descriptor);
	BY_HANDLE_FILE_INFORMATION data;

	if (handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle, &data))
	{
		errno = EBADF;
		return -1;
	}
	fill_information(data.dwFileAttributes, data.nFileSizeLow, data.nFileSizeHigh, &data.ftCreationTime,
		&data.ftLastAccessTime, &data.ftLastWriteTime, information);
	return 0;
}

int posix_set_file_times(const char *path, posix_ulong access_seconds, posix_ulong access_nanoseconds,
	posix_ulong modification_seconds, posix_ulong modification_nanoseconds)
{
	char console_path[MAX_PATH];
	FILETIME access, modification;
	HANDLE handle;
	BOOL result;

	x360_path(path, console_path, sizeof(console_path));
	handle = CreateFileA(console_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE)
	{
		errno = x360_errno_from_error(GetLastError());
		return -1;
	}
	file_time(access_seconds, access_nanoseconds, &access);
	file_time(modification_seconds, modification_nanoseconds, &modification);
	result = SetFileTime(handle, NULL, access_seconds ? &access : NULL, modification_seconds ? &modification : NULL);
	CloseHandle(handle);
	return result ? 0 : -1;
}

void posix_describe_address(void *address, char *buffer, posix_ulong size)
{
	if (size)
	{
		_snprintf(buffer, size - 1, "0x%08x", (unsigned int)(UINT_PTR)address);
		buffer[size - 1] = '\0';
	}
}

int posix_seek(int descriptor, posix_long offset_low, posix_long offset_high, int whence,
	posix_ulong *position_low, posix_ulong *position_high)
{
	HANDLE handle = (HANDLE)_get_osfhandle(descriptor);
	LONG high = offset_high;
	DWORD method = whence == SEEK_CUR ? FILE_CURRENT : whence == SEEK_END ? FILE_END : FILE_BEGIN;
	DWORD low;

	if (handle == INVALID_HANDLE_VALUE)
	{
		errno = EBADF;
		return -1;
	}
	low = SetFilePointer(handle, offset_low, &high, method);
	if (low == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
	{
		errno = EINVAL;
		return -1;
	}
	if (position_low)
		*position_low = low;
	if (position_high)
		*position_high = (posix_ulong)high;
	return 0;
}

int posix_truncate(int descriptor, posix_ulong size_low, posix_ulong size_high)
{
	HANDLE handle = (HANDLE)_get_osfhandle(descriptor);
	LONG high = (LONG)size_high;
	LONG current_high = 0;
	DWORD current;

	if (handle == INVALID_HANDLE_VALUE)
	{
		errno = EBADF;
		return -1;
	}
	current = SetFilePointer(handle, 0, &current_high, FILE_CURRENT);
	SetFilePointer(handle, (LONG)size_low, &high, FILE_BEGIN);
	if (!SetEndOfFile(handle))
	{
		errno = EIO;
		return -1;
	}
	SetFilePointer(handle, (LONG)current, &current_high, FILE_BEGIN);
	return 0;
}

int posix_disk_space(const char *path, posix_ulong *free_low, posix_ulong *free_high, posix_ulong *total_low,
	posix_ulong *total_high)
{
	char console_path[MAX_PATH];
	char root[16];
	ULARGE_INTEGER available, total, free_bytes;
	char *colon;

	x360_path(path, console_path, sizeof(console_path));
	colon = strchr(console_path, ':');
	if (!colon || colon - console_path > 10)
		return -1;
	memcpy(root, console_path, (size_t)(colon - console_path) + 1);
	root[colon - console_path + 1] = '\\';
	root[colon - console_path + 2] = '\0';
	if (!GetDiskFreeSpaceExA(root, &available, &total, &free_bytes))
	{
		errno = x360_errno_from_error(GetLastError());
		return -1;
	}
	*free_low = available.LowPart;
	*free_high = available.HighPart;
	*total_low = total.LowPart;
	*total_high = total.HighPart;
	return 0;
}

int posix_set_read_only(const char *path, int read_only)
{
	char console_path[MAX_PATH];
	DWORD attributes;

	x360_path(path, console_path, sizeof(console_path));
	attributes = GetFileAttributesA(console_path);
	if (attributes == INVALID_FILE_ATTRIBUTES)
		return -1;
	attributes = read_only ? attributes | FILE_ATTRIBUTE_READONLY : attributes & ~FILE_ATTRIBUTE_READONLY;
	return SetFileAttributesA(console_path, attributes) ? 0 : -1;
}

int posix_make_directory(const char *path)
{
	char console_path[MAX_PATH];

	x360_path(path, console_path, sizeof(console_path));
	if (!CreateDirectoryA(console_path, NULL))
	{
		errno = x360_errno_from_error(GetLastError());
		return -1;
	}
	return 0;
}

/* ---------- directories */

struct directory
{
	HANDLE find;
	WIN32_FIND_DATAA data;
	BOOL pending;
};

void *posix_directory_open(const char *path)
{
	char console_path[MAX_PATH];
	char search[MAX_PATH];
	struct directory *directory = calloc(1, sizeof(*directory));

	if (!directory)
		return NULL;
	x360_path(path, console_path, sizeof(console_path));
	_snprintf(search, sizeof(search) - 1, "%s%s*", console_path,
		console_path[strlen(console_path) - 1] == '\\' ? "" : "\\");
	search[sizeof(search) - 1] = '\0';
	directory->find = FindFirstFileA(search, &directory->data);
	if (directory->find == INVALID_HANDLE_VALUE)
	{
		DWORD error = GetLastError();

		if (error != ERROR_FILE_NOT_FOUND && error != ERROR_NO_MORE_FILES)
		{
			free(directory);
			errno = x360_errno_from_error(error);
			return NULL;
		}
	}
	directory->pending = directory->find != INVALID_HANDLE_VALUE;
	return directory;
}

int posix_directory_next(void *handle, char *name, posix_ulong name_size)
{
	struct directory *directory = handle;

	while (directory->pending)
	{
		WIN32_FIND_DATAA *data = &directory->data;
		int keep = strcmp(data->cFileName, ".") && strcmp(data->cFileName, "..");

		if (keep)
		{
			_snprintf(name, name_size - 1, "%s", data->cFileName);
			name[name_size - 1] = '\0';
		}
		directory->pending = FindNextFileA(directory->find, &directory->data);
		if (keep)
			return 1;
	}
	return 0;
}

void posix_directory_close(void *handle)
{
	struct directory *directory = handle;

	if (!directory)
		return;
	if (directory->find != INVALID_HANDLE_VALUE)
		FindClose(directory->find);
	free(directory);
}

int posix_find_entry_case_insensitive(const char *directory_path, const char *name, char *result,
	posix_ulong result_size)
{
	void *directory = posix_directory_open(directory_path);
	char entry[MAX_PATH];
	int found = 0;

	if (!directory)
		return 0;
	while (posix_directory_next(directory, entry, sizeof(entry)))
	{
		if (!_stricmp(entry, name))
		{
			_snprintf(result, result_size - 1, "%s", entry);
			result[result_size - 1] = '\0';
			found = 1;
			break;
		}
	}
	posix_directory_close(directory);
	return found;
}

/* ---------- the process (internet play, p2p.c) */

int posix_command_line_argument(int index, char *buffer, posix_ulong size)
{
	(void)index;
	if (size)
		buffer[0] = '\0';
	return 0;
}

posix_ulong posix_process_id(void)
{
	return 1;
}

int posix_register_url_scheme(const char *scheme, const char *description)
{
	(void)scheme;
	(void)description;
	return 0;
}

int posix_user_secret(unsigned char *secret, int size)
{
	(void)secret;
	(void)size;
	return 0;
}

int posix_hardware_id_source(char *text, int size)
{
	(void)text;
	(void)size;
	return 0;
}

int posix_discord_connect(void)
{
	return -1;
}

int posix_discord_write(int handle, const void *buffer, int length)
{
	(void)handle;
	(void)buffer;
	(void)length;
	return -1;
}

int posix_discord_read(int handle, void *buffer, int length)
{
	(void)handle;
	(void)buffer;
	(void)length;
	return -1;
}

void posix_discord_close(int handle)
{
	(void)handle;
}

void posix_random_bytes(void *buffer, posix_ulong size)
{
	XNetRandom((BYTE *)buffer, (UINT)size);
}
