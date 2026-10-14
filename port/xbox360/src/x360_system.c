/*
X360_SYSTEM.C

The console's side of the rest: the log, paths, the C runtime's file
functions with the platform layer's paths, the C99 and POSIX functions the
console's C runtime does not have, the few SDL functions the desktop
platform layer calls (the console has no SDL: files, globbing, and an audio
device that is not there yet, which leaves the game's mixer silent), and
leaving for the launcher.
*/

#include <xtl.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <math.h>
#include <direct.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>

#include "x360_port.h"

/* ---------- the log */

#define PORT_LOG_PATH "game:\\port.txt"

static HANDLE port_log = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION log_lock;
static volatile LONG log_lock_state;

static void log_lock_enter(void)
{
	if (log_lock_state != 2)
	{
		if (InterlockedCompareExchange(&log_lock_state, 1, 0) == 0)
		{
			InitializeCriticalSection(&log_lock);
			X360_LWSYNC();
			log_lock_state = 2;
		}
		while (log_lock_state != 2)
			Sleep(0);
	}
	EnterCriticalSection(&log_lock);
}

/* every line to the debug channel (XBDM's notifications) and to
game:\port.txt, flushed: a crash keeps everything before it */
void x360_debug_print(const char *text)
{
	DWORD written;
	size_t length = strlen(text);

	log_lock_enter();
	/* (to the debug channel a line at a time, each at most 200 characters:
	a long text in one call, such as a shader's source, brought XBDM down;
	port.txt below has it whole) */
	{
		const char *cursor = text;
		char piece[204];

		while (*cursor)
		{
			size_t span = strcspn(cursor, "\n");

			if (span > 200)
				span = 200;
			memcpy(piece, cursor, span);
			cursor += span;
			if (*cursor == '\n')
			{
				piece[span++] = '\n';
				cursor++;
			}
			else if (*cursor)
			{
				piece[span++] = '\n';
			}
			piece[span] = '\0';
			OutputDebugStringA(piece);
		}
	}
	if (port_log == INVALID_HANDLE_VALUE)
	{
		port_log = CreateFileA(PORT_LOG_PATH, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, NULL);
	}
	if (port_log != INVALID_HANDLE_VALUE)
	{
		WriteFile(port_log, text, (DWORD)length, &written, NULL);
		FlushFileBuffers(port_log);
	}
	LeaveCriticalSection(&log_lock);
}

void x360_log(const char *format, ...)
{
	char text[1024];
	va_list arguments;
	int length;

	strcpy(text, "x360: ");
	va_start(arguments, format);
	length = _vsnprintf(text + 6, sizeof(text) - 9, format, arguments);
	va_end(arguments);
	text[sizeof(text) - 3] = '\0';
	length = (int)strlen(text);
	text[length++] = '\r';
	text[length++] = '\n';
	text[length] = '\0';
	x360_debug_print(text);
}

/* ---------- paths */

void x360_path(const char *path, char *buffer, size_t size)
{
	char *cursor;
	size_t length = 0;

	if (!size)
		return;
	if (!strchr(path, ':'))
	{
		/* relative: below the title's folder */
		_snprintf(buffer, size - 1, "game:\\%s", path);
	}
	else
	{
		_snprintf(buffer, size - 1, "%s", path);
	}
	buffer[size - 1] = '\0';
	for (cursor = buffer; *cursor; cursor++)
	{
		if (*cursor == '/')
			*cursor = '\\';
	}
	/* "." components (the platform layer's current folder) */
	for (cursor = buffer; *cursor; )
	{
		if (cursor[0] == '\\' && cursor[1] == '.' && (cursor[2] == '\\' || !cursor[2]))
			memmove(cursor, cursor + 2, strlen(cursor + 2) + 1);
		else
			cursor++;
	}
	/* no doubled separators, and none at the end but after the drive */
	for (cursor = buffer; *cursor; )
	{
		if (cursor[0] == '\\' && cursor[1] == '\\')
			memmove(cursor, cursor + 1, strlen(cursor + 1) + 1);
		else
			cursor++;
	}
	length = strlen(buffer);
	if (length && buffer[length - 1] == ':' && length + 1 < size)
		strcat(buffer, "\\");
	else if (length > 1 && buffer[length - 1] == '\\' && buffer[length - 2] != ':')
		buffer[length - 1] = '\0';
}

/* ---------- the C runtime's file functions, with the platform layer's
paths (port/xbox360/include/crt/halo_xbox360_file_names.h) */

FILE *halo_xbox360_fopen(const char *path, const char *mode)
{
	char console_path[MAX_PATH];

	x360_path(path, console_path, sizeof(console_path));
	return fopen(console_path, mode);
}

FILE *halo_xbox360_freopen(const char *path, const char *mode, FILE *stream)
{
	char console_path[MAX_PATH];

	if (!path)
		return freopen(NULL, mode, stream);
	x360_path(path, console_path, sizeof(console_path));
	return freopen(console_path, mode, stream);
}

int halo_xbox360_open(const char *path, int flags, ...)
{
	char console_path[MAX_PATH];
	int mode = _S_IREAD | _S_IWRITE;

	if (flags & _O_CREAT)
	{
		va_list arguments;

		va_start(arguments, flags);
		mode = va_arg(arguments, int);
		va_end(arguments);
	}
	x360_path(path, console_path, sizeof(console_path));
	/* (binary, as the Xbox's files are) */
	return _open(console_path, flags | _O_BINARY, mode);
}

int halo_xbox360_remove(const char *path)
{
	char console_path[MAX_PATH];

	x360_path(path, console_path, sizeof(console_path));
	return DeleteFileA(console_path) ? 0 : (errno = x360_errno_from_error(GetLastError()), -1);
}

int halo_xbox360_rename(const char *old_path, const char *new_path)
{
	char console_old[MAX_PATH], console_new[MAX_PATH];

	x360_path(old_path, console_old, sizeof(console_old));
	x360_path(new_path, console_new, sizeof(console_new));
	return MoveFileA(console_old, console_new) ? 0 : (errno = x360_errno_from_error(GetLastError()), -1);
}

int halo_xbox360_mkdir(const char *path)
{
	char console_path[MAX_PATH];

	x360_path(path, console_path, sizeof(console_path));
	return CreateDirectoryA(console_path, NULL) ? 0 : (errno = x360_errno_from_error(GetLastError()), -1);
}

int halo_xbox360_rmdir(const char *path)
{
	char console_path[MAX_PATH];

	x360_path(path, console_path, sizeof(console_path));
	return RemoveDirectoryA(console_path) ? 0 : (errno = x360_errno_from_error(GetLastError()), -1);
}

int halo_xbox360_access(const char *path, int mode)
{
	char console_path[MAX_PATH];
	DWORD attributes;

	x360_path(path, console_path, sizeof(console_path));
	attributes = GetFileAttributesA(console_path);
	if (attributes == INVALID_FILE_ATTRIBUTES)
	{
		errno = ENOENT;
		return -1;
	}
	if ((mode & 2) && (attributes & FILE_ATTRIBUTE_READONLY))
	{
		errno = EACCES;
		return -1;
	}
	return 0;
}

/* ---------- C99's formatted output (the console's runtime has MSVC's
_vsnprintf, which leaves a cut-short buffer unterminated and returns -1) */

/* (the SDK's stdarg.h is MSVC's: va_list is a pointer, and it has no va_copy) */
#ifndef va_copy
#define va_copy(destination, source) ((destination) = (source))
#endif

int halo_xbox360_vsnprintf(char *buffer, size_t size, const char *format, va_list arguments)
{
	va_list again;
	int length;

	if (!buffer || !size)
		return _vscprintf(format, arguments);
	/* (a copy for measuring a cut-short result: the first walk uses the
	arguments up; callers size a second buffer from the full length) */
	va_copy(again, arguments);
	length = _vsnprintf(buffer, size, format, arguments);
	if (length < 0 || (size_t)length >= size)
	{
		buffer[size - 1] = '\0';
		length = _vscprintf(format, again);
		if (length < (int)size)
			length = (int)size;
	}
	va_end(again);
	return length;
}

int halo_xbox360_snprintf(char *buffer, size_t size, const char *format, ...)
{
	va_list arguments;
	int length;

	va_start(arguments, format);
	length = halo_xbox360_vsnprintf(buffer, size, format, arguments);
	va_end(arguments);
	return length;
}

/* ---------- 64-bit conversions (the runtime's _strtoi64 returns its
result where this compiler does not look for it) */

unsigned long long halo_xbox360_strtoull(const char *text, char **end, int base)
{
	const char *cursor = text;
	unsigned long long value = 0;
	int negative = 0, any = 0;

	while (isspace((unsigned char)*cursor))
		cursor++;
	if (*cursor == '+' || *cursor == '-')
		negative = *cursor++ == '-';
	if ((base == 0 || base == 16) && cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X'))
	{
		cursor += 2;
		base = 16;
	}
	else if (base == 0)
	{
		base = cursor[0] == '0' ? 8 : 10;
	}
	for (;; cursor++)
	{
		int digit;

		if (*cursor >= '0' && *cursor <= '9')
			digit = *cursor - '0';
		else if (*cursor >= 'a' && *cursor <= 'z')
			digit = *cursor - 'a' + 10;
		else if (*cursor >= 'A' && *cursor <= 'Z')
			digit = *cursor - 'A' + 10;
		else
			break;
		if (digit >= base)
			break;
		value = value * (unsigned long long)base + (unsigned long long)digit;
		any = 1;
	}
	if (end)
		*end = (char *)(any ? cursor : text);
	return negative ? (unsigned long long)(-(long long)value) : value;
}

long long halo_xbox360_strtoll(const char *text, char **end, int base)
{
	return (long long)halo_xbox360_strtoull(text, end, base);
}

/* ---------- maths the runtime lacks (the game's results must match the
other builds': round to nearest even, as the x87 and SSE do) */

double rint(double value)
{
	const double big = 4503599627370496.0; /* 2^52 */
	volatile double result;

	if (!(value < big && value > -big))
		return value;
	if (value >= 0.0)
	{
		result = value + big;
		result -= big;
	}
	else
	{
		result = value - big;
		result += big;
	}
	return result;
}

float rintf(float value)
{
	const float big = 8388608.0f; /* 2^23 */
	volatile float result;

	if (!(value < big && value > -big))
		return value;
	if (value >= 0.0f)
	{
		result = value + big;
		result -= big;
	}
	else
	{
		result = value - big;
		result += big;
	}
	return result;
}

long lrint(double value)
{
	return (long)rint(value);
}

long lrintf(float value)
{
	return (long)rintf(value);
}

double trunc(double value)
{
	return value < 0.0 ? ceil(value) : floor(value);
}

float truncf(float value)
{
	return (float)trunc(value);
}

double round(double value)
{
	return value < 0.0 ? -floor(-value + 0.5) : floor(value + 0.5);
}

float roundf(float value)
{
	return (float)round(value);
}

double fmin(double left, double right)
{
	return left < right ? left : right;
}

double fmax(double left, double right)
{
	return left > right ? left : right;
}

float fminf(float left, float right)
{
	return left < right ? left : right;
}

float fmaxf(float left, float right)
{
	return left > right ? left : right;
}

double scalbn(double value, int exponent)
{
	while (exponent > 0)
	{
		int step = exponent > 1000 ? 1000 : exponent;
		union { double d; unsigned long long u; } factor;

		factor.u = (unsigned long long)(1023 + step) << 52;
		value *= factor.d;
		exponent -= step;
	}
	while (exponent < 0)
	{
		int step = exponent < -1000 ? -1000 : exponent;
		union { double d; unsigned long long u; } factor;

		factor.u = (unsigned long long)(1023 + step) << 52;
		value *= factor.d;
		exponent -= step;
	}
	return value;
}

float exp2f(float value)
{
	return (float)pow(2.0, value);
}

double exp2(double value)
{
	return pow(2.0, value);
}

float log2f(float value)
{
	return (float)(log(value) / 0.69314718055994530942);
}

float cbrtf(float value)
{
	return value < 0.0f ? -(float)pow(-value, 1.0 / 3.0) : (float)pow(value, 1.0 / 3.0);
}

/* the x87 control word (math/real_math.c): PowerPC's rounding is always
to nearest here, as the game sets the x87's */
unsigned int _control87(unsigned int value, unsigned int mask)
{
	(void)value;
	(void)mask;
	return 0;
}

void _ReadWriteBarrier(void)
{
	X360_SYNC();
}

/* ---------- the 64-bit atomics clang calls out for (the console's 32-bit
PowerPC has no 64-bit compare and swap) */

static CRITICAL_SECTION atomic_lock;
static volatile LONG atomic_lock_state;

static void atomic_enter(void)
{
	if (atomic_lock_state != 2)
	{
		if (InterlockedCompareExchange(&atomic_lock_state, 1, 0) == 0)
		{
			InitializeCriticalSection(&atomic_lock);
			X360_LWSYNC();
			atomic_lock_state = 2;
		}
		while (atomic_lock_state != 2)
			Sleep(0);
	}
	EnterCriticalSection(&atomic_lock);
}

unsigned long long __atomic_load_8(const volatile void *memory, int model)
{
	unsigned long long value;

	(void)model;
	atomic_enter();
	value = *(const volatile unsigned long long *)memory;
	LeaveCriticalSection(&atomic_lock);
	return value;
}

_Bool __atomic_compare_exchange_8(volatile void *memory, void *expected, unsigned long long desired,
	int success, int failure)
{
	volatile unsigned long long *target = memory;
	unsigned long long *wanted = expected;
	_Bool exchanged;

	(void)success;
	(void)failure;
	atomic_enter();
	exchanged = *target == *wanted;
	if (exchanged)
		*target = desired;
	else
		*wanted = *target;
	LeaveCriticalSection(&atomic_lock);
	return exchanged;
}

/* ---------- POSIX leftovers */

/* the return addresses of the calls that led here, by the stack's back
chain: each frame's first word is its caller's stack pointer, and the
Xenon convention keeps a function's return address in the word 8 bytes
below its caller's stack pointer */
__attribute__((noinline)) int backtrace(void **frames, int count)
{
	unsigned int *stack;
	int captured = 0;

	__asm__ __volatile__("mr %0, 1" : "=r"(stack));

	while (captured < count && stack)
	{
		unsigned int *caller = (unsigned int *)(UINT_PTR)stack[0];
		unsigned int address;

		if (!caller || caller <= stack || (UINT_PTR)caller - (UINT_PTR)stack > 0x100000)
			break;
		address = caller[-2];
		/* (a frame that saved no return address: its caller's is further up) */
		if (address >= 0x82000000U && address < 0x84000000U)
			frames[captured++] = (void *)(UINT_PTR)address;
		stack = caller;
	}
	return captured;
}

void x360_log_backtrace(const char *why)
{
	void *frames[24];
	char text[24 * 11 + 1];
	int count = backtrace(frames, 24), index;
	size_t length = 0;

	text[0] = '\0';
	for (index = 0; index < count; index++)
		length += (size_t)_snprintf(text + length, sizeof(text) - length - 1, " %08x",
			(unsigned int)(UINT_PTR)frames[index]);
	text[sizeof(text) - 1] = '\0';
	x360_log("%s; calls:%s", why, text);
}

/* the environment: the console has none, so settings that the desktop
builds take from it are a small table of the port's own */
static struct
{
	char name[64];
	char value[256];
} environment[32];

char *getenv(const char *name)
{
	int index;

	for (index = 0; index < (int)(sizeof(environment) / sizeof(environment[0])); index++)
	{
		if (environment[index].name[0] && !strcmp(environment[index].name, name))
			return environment[index].value;
	}
	return NULL;
}

int setenv(const char *name, const char *value, int overwrite)
{
	int index, free_index = -1;

	for (index = 0; index < (int)(sizeof(environment) / sizeof(environment[0])); index++)
	{
		if (environment[index].name[0] && !strcmp(environment[index].name, name))
		{
			if (overwrite)
				_snprintf(environment[index].value, sizeof(environment[index].value) - 1, "%s", value);
			return 0;
		}
		if (!environment[index].name[0] && free_index < 0)
			free_index = index;
	}
	if (free_index < 0)
		return -1;
	_snprintf(environment[free_index].name, sizeof(environment[free_index].name) - 1, "%s", name);
	_snprintf(environment[free_index].value, sizeof(environment[free_index].value) - 1, "%s", value);
	return 0;
}

/* rand_s (Expat's hash salt) */
int rand_s(unsigned int *value)
{
	XNetRandom((BYTE *)value, sizeof(*value));
	return 0;
}

/* ---------- SDL's few functions (no SDL on the console) */

const char *SDL_GetError(void)
{
	return "not on the Xbox 360";
}

void SDL_free(void *memory)
{
	free(memory);
}

_Bool SDL_SetHint(const char *name, const char *value)
{
	(void)name;
	(void)value;
	return 1;
}

int SDL_strncasecmp(const char *left, const char *right, size_t length)
{
	return _strnicmp(left, right, length);
}

const char *SDL_GetBasePath(void)
{
	return "game:\\";
}

void *SDL_LoadFile(const char *file, size_t *size)
{
	FILE *stream = halo_xbox360_fopen(file, "rb");
	long length;
	char *data;

	if (size)
		*size = 0;
	if (!stream)
		return NULL;
	fseek(stream, 0, SEEK_END);
	length = ftell(stream);
	fseek(stream, 0, SEEK_SET);
	data = length >= 0 ? malloc((size_t)length + 1) : NULL;
	if (!data || fread(data, 1, (size_t)length, stream) != (size_t)length)
	{
		free(data);
		fclose(stream);
		return NULL;
	}
	fclose(stream);
	data[length] = '\0';
	if (size)
		*size = (size_t)length;
	return data;
}

_Bool SDL_SaveFile(const char *file, const void *data, size_t size)
{
	FILE *stream = halo_xbox360_fopen(file, "wb");
	_Bool written;

	if (!stream)
		return 0;
	written = fwrite(data, 1, size, stream) == size;
	return fclose(stream) == 0 && written;
}

_Bool SDL_CreateDirectory(const char *path)
{
	char console_path[MAX_PATH];
	char *cursor;

	x360_path(path, console_path, sizeof(console_path));
	for (cursor = strchr(console_path, '\\'); cursor; cursor = strchr(cursor + 1, '\\'))
	{
		if (cursor > console_path && cursor[-1] == ':')
			continue;
		*cursor = '\0';
		CreateDirectoryA(console_path, NULL);
		*cursor = '\\';
	}
	CreateDirectoryA(console_path, NULL);
	return (GetFileAttributesA(console_path) & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

/* the names in path matching pattern ("*.xml": FindFirstFile's own
matching), one allocation for the list and its names, as SDL's */
char **SDL_GlobDirectory(const char *path, const char *pattern, unsigned int flags, int *count)
{
	char console_path[MAX_PATH];
	char search[MAX_PATH];
	WIN32_FIND_DATAA found;
	HANDLE find;
	char names[64][MAX_PATH];
	int total = 0, index;
	size_t bytes;
	char **list;
	char *text;

	(void)flags;
	if (count)
		*count = 0;
	x360_path(path, console_path, sizeof(console_path));
	_snprintf(search, sizeof(search) - 1, "%s\\%s", console_path, pattern ? pattern : "*");
	search[sizeof(search) - 1] = '\0';
	find = FindFirstFileA(search, &found);
	if (find != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (strcmp(found.cFileName, ".") && strcmp(found.cFileName, "..") && total < 64)
			{
				strncpy(names[total], found.cFileName, MAX_PATH - 1);
				names[total++][MAX_PATH - 1] = '\0';
			}
		} while (FindNextFileA(find, &found));
		FindClose(find);
	}
	bytes = sizeof(char *) * (size_t)(total + 1);
	for (index = 0; index < total; index++)
		bytes += strlen(names[index]) + 1;
	list = malloc(bytes);
	if (!list)
		return NULL;
	text = (char *)(list + total + 1);
	for (index = 0; index < total; index++)
	{
		strcpy(text, names[index]);
		list[index] = text;
		text += strlen(text) + 1;
	}
	list[total] = NULL;
	if (count)
		*count = total;
	return list;
}

/* OpenGL on the console's Direct3D (x360_gl.c) */
void *SDL_GL_GetProcAddress(const char *name)
{
	return x360_gl_function(name);
}

/* (the audio device: x360_audio.c) */

/* ---------- leaving */

void x360_exit_to_launcher(void)
{
	x360_log("returning to the launcher");
	XLaunchNewImage("", 0);
}

/* A title that returns from main or calls exit or abort is not meant to:
the console stops with a bug check (USER_CALLED_KE_BUG_CHECK, which takes
XBDM down with it until the console is power-cycled). Each goes back to the
launcher instead, saying so first. */
static void exit_handler(void)
{
	x360_log("the game exited");
	x360_exit_to_launcher();
}

static void abort_handler(int signal_number)
{
	x360_log("the game aborted (signal %d)", signal_number);
	x360_exit_to_launcher();
}

__attribute__((constructor(99)))
static void exit_handlers_install(void)
{
	atexit(exit_handler);
	signal(SIGABRT, abort_handler);
}

/* ---------- byte swaps the SDK declares and defines nowhere (Microsoft's
compiler expands them inline) */

unsigned short __cdecl _byteswap_ushort(unsigned short value)
{
	return __builtin_bswap16(value);
}

unsigned long __cdecl _byteswap_ulong(unsigned long value)
{
	return __builtin_bswap32(value);
}

unsigned __int64 __cdecl _byteswap_uint64(unsigned __int64 value)
{
	return __builtin_bswap64(value);
}
