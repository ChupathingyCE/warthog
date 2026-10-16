/*
X360_POSIX.C

The POSIX calls the desktop platform layer makes (port/linux/src), on the
console: threads, mutexes and condition variables, clocks and sleeping,
sysconf, positioned reads and writes, and the memory mapping the Xbox window
uses. The headers are the Windows build's (port/windows/include/posix); this
is port/windows/src/win32_posix.c for the console's kernel, which has no
condition variables, no one-time initialization and no address space
queries.
*/

#include <xtl.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pthread.h"
#include "sched.h"
#include "sys/mman.h"
#include "time.h"
#include "unistd.h"

#include "x360_port.h"

static int errno_from_error(DWORD error)
{
	switch (error)
	{
	case ERROR_FILE_NOT_FOUND:
	case ERROR_PATH_NOT_FOUND:
	case ERROR_INVALID_DRIVE:
		return ENOENT;
	case ERROR_ACCESS_DENIED:
	case ERROR_SHARING_VIOLATION:
	case ERROR_LOCK_VIOLATION:
		return EACCES;
	case ERROR_ALREADY_EXISTS:
	case ERROR_FILE_EXISTS:
		return EEXIST;
	case ERROR_NOT_ENOUGH_MEMORY:
	case ERROR_OUTOFMEMORY:
		return ENOMEM;
	case ERROR_INVALID_ADDRESS:
	case ERROR_INVALID_PARAMETER:
		return EINVAL;
	default:
		return EIO;
	}
}

int x360_errno_from_error(unsigned long error)
{
	return errno_from_error((DWORD)error);
}

/* ---------- threads */

struct thread_start
{
	void *(*start)(void *);
	void *argument;
};

static DWORD WINAPI thread_main(LPVOID parameter)
{
	struct thread_start start = *(struct thread_start *)parameter;

	free(parameter);
	start.start(start.argument);
	return 0;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attributes, void *(*start)(void *), void *argument)
{
	struct thread_start *parameter = malloc(sizeof(*parameter));
	DWORD identifier;
	HANDLE handle;
	/* (the console's default is 64 KB; the desktop's threads have 8 MB, and
	the game's own threads were given 64 KB to 256 KB on the Xbox) */
	SIZE_T stack_size = attributes && attributes->stack_size ? attributes->stack_size : 0x100000;

	if (!parameter)
		return EAGAIN;
	parameter->start = start;
	parameter->argument = argument;
	handle = CreateThread(NULL, stack_size, thread_main, parameter, 0, &identifier);
	if (!handle)
	{
		free(parameter);
		return EAGAIN;
	}
	CloseHandle(handle);
	*thread = identifier;
	return 0;
}

int pthread_detach(pthread_t thread)
{
	(void)thread;
	return 0;
}

pthread_t pthread_self(void)
{
	return GetCurrentThreadId();
}

int pthread_equal(pthread_t thread1, pthread_t thread2)
{
	return thread1 == thread2;
}

/* 0: not run, 1: running, 2: done */
int pthread_once(pthread_once_t *once, void (*routine)(void))
{
	volatile LONG *state = (volatile LONG *)&once->once;

	if (*state == 2)
		return 0;
	if (InterlockedCompareExchange(state, 1, 0) == 0)
	{
		routine();
		X360_LWSYNC();
		*state = 2;
		return 0;
	}
	while (*state != 2)
		Sleep(0);
	return 0;
}

int pthread_attr_init(pthread_attr_t *attributes)
{
	attributes->stack_size = 0;
	attributes->detached = 0;
	return 0;
}

int pthread_attr_destroy(pthread_attr_t *attributes)
{
	(void)attributes;
	return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attributes, int state)
{
	attributes->detached = state;
	return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *attributes, size_t size)
{
	attributes->stack_size = size;
	return 0;
}

/* ---------- mutexes: critical sections, always recursive */

int pthread_mutexattr_init(pthread_mutexattr_t *attributes)
{
	attributes->type = PTHREAD_MUTEX_DEFAULT;
	return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attributes)
{
	(void)attributes;
	return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attributes, int type)
{
	attributes->type = type;
	return 0;
}

/* created on first use, so that PTHREAD_MUTEX_INITIALIZER can be a constant */
static CRITICAL_SECTION *mutex_section(pthread_mutex_t *mutex)
{
	CRITICAL_SECTION *section = mutex->section;

	if (!section)
	{
		CRITICAL_SECTION *created = malloc(sizeof(*created));

		if (!created)
			abort();
		InitializeCriticalSection(created);
		section = InterlockedCompareExchangePointer(&mutex->section, created, NULL);
		if (section)
		{
			DeleteCriticalSection(created);
			free(created);
		}
		else
		{
			section = created;
		}
	}
	return section;
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attributes)
{
	(void)attributes;
	mutex->section = NULL;
	mutex_section(mutex);
	return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
	CRITICAL_SECTION *section = mutex->section;

	if (section)
	{
		DeleteCriticalSection(section);
		free(section);
		mutex->section = NULL;
	}
	return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
	EnterCriticalSection(mutex_section(mutex));
	return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
	return TryEnterCriticalSection(mutex_section(mutex)) ? 0 : EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	LeaveCriticalSection(mutex_section(mutex));
	return 0;
}

/* ---------- condition variables

The console has none: each waiter waits on an event of its own (one per
thread, kept in a TLS slot), queued on the condition under a lock that
signals take too. A waiter that times out takes itself off the queue,
unless a signal took it off first, in which case it was signaled. */

struct condition_waiter
{
	struct condition_waiter *next;
	HANDLE event;
	volatile BOOL signaled;
};

struct condition
{
	CRITICAL_SECTION lock;
	struct condition_waiter *first;
	struct condition_waiter *last;
};

static DWORD event_slot = TLS_OUT_OF_INDEXES;
static volatile LONG event_slot_state;

static HANDLE thread_event(void)
{
	HANDLE event;

	if (event_slot_state != 2)
	{
		if (InterlockedCompareExchange(&event_slot_state, 1, 0) == 0)
		{
			event_slot = TlsAlloc();
			X360_LWSYNC();
			event_slot_state = 2;
		}
		while (event_slot_state != 2)
			Sleep(0);
	}
	event = TlsGetValue(event_slot);
	if (!event)
	{
		event = CreateEvent(NULL, FALSE, FALSE, NULL);
		if (!event)
			abort();
		TlsSetValue(event_slot, event);
	}
	return event;
}

static struct condition *condition_of(pthread_cond_t *condition)
{
	struct condition *state = condition->variable;

	if (!state)
	{
		struct condition *created = calloc(1, sizeof(*created));

		if (!created)
			abort();
		InitializeCriticalSection(&created->lock);
		state = InterlockedCompareExchangePointer(&condition->variable, created, NULL);
		if (state)
		{
			DeleteCriticalSection(&created->lock);
			free(created);
		}
		else
		{
			state = created;
		}
	}
	return state;
}

int pthread_cond_init(pthread_cond_t *condition, const pthread_condattr_t *attributes)
{
	(void)attributes;
	condition->variable = NULL;
	condition_of(condition);
	return 0;
}

int pthread_cond_destroy(pthread_cond_t *condition)
{
	struct condition *state = condition->variable;

	if (state)
	{
		DeleteCriticalSection(&state->lock);
		free(state);
		condition->variable = NULL;
	}
	return 0;
}

static int condition_wait(pthread_cond_t *condition, pthread_mutex_t *mutex, DWORD milliseconds)
{
	struct condition *state = condition_of(condition);
	struct condition_waiter waiter;
	int result = 0;

	waiter.next = NULL;
	waiter.event = thread_event();
	waiter.signaled = FALSE;
	EnterCriticalSection(&state->lock);
	if (state->last)
		state->last->next = &waiter;
	else
		state->first = &waiter;
	state->last = &waiter;
	LeaveCriticalSection(&state->lock);

	pthread_mutex_unlock(mutex);
	WaitForSingleObject(waiter.event, milliseconds);
	pthread_mutex_lock(mutex);

	EnterCriticalSection(&state->lock);
	if (waiter.signaled)
	{
		/* (signaled as the wait timed out: its event is set, which the
		thread's next wait must not see) */
		WaitForSingleObject(waiter.event, 0);
	}
	else
	{
		struct condition_waiter **link;

		for (link = &state->first; *link; link = &(*link)->next)
		{
			if (*link == &waiter)
			{
				*link = waiter.next;
				break;
			}
		}
		state->last = NULL;
		for (link = &state->first; *link; link = &(*link)->next)
			state->last = *link;
		result = ETIMEDOUT;
	}
	LeaveCriticalSection(&state->lock);
	return result;
}

int pthread_cond_wait(pthread_cond_t *condition, pthread_mutex_t *mutex)
{
	condition_wait(condition, mutex, INFINITE);
	return 0;
}

int pthread_cond_timedwait(pthread_cond_t *condition, pthread_mutex_t *mutex, const struct timespec *deadline)
{
	struct timespec now;
	long long milliseconds;

	clock_gettime(CLOCK_REALTIME, &now);
	milliseconds = ((long long)deadline->tv_sec - now.tv_sec) * 1000 + (deadline->tv_nsec - now.tv_nsec) / 1000000;
	if (milliseconds < 0)
		milliseconds = 0;
	return condition_wait(condition, mutex, milliseconds >= INFINITE ? INFINITE - 1 : (DWORD)milliseconds);
}

static void condition_wake(pthread_cond_t *condition, BOOL all)
{
	struct condition *state = condition_of(condition);

	EnterCriticalSection(&state->lock);
	while (state->first)
	{
		struct condition_waiter *waiter = state->first;

		state->first = waiter->next;
		if (!state->first)
			state->last = NULL;
		waiter->signaled = TRUE;
		SetEvent(waiter->event);
		if (!all)
			break;
	}
	LeaveCriticalSection(&state->lock);
}

int pthread_cond_signal(pthread_cond_t *condition)
{
	condition_wake(condition, FALSE);
	return 0;
}

int pthread_cond_broadcast(pthread_cond_t *condition)
{
	condition_wake(condition, TRUE);
	return 0;
}

/* ---------- time */

int clock_gettime(clockid_t clock, struct timespec *time)
{
	if (clock == CLOCK_MONOTONIC)
	{
		static LARGE_INTEGER frequency;
		LARGE_INTEGER counter;

		if (!frequency.QuadPart)
			QueryPerformanceFrequency(&frequency);
		QueryPerformanceCounter(&counter);
		time->tv_sec = (time_t)(counter.QuadPart / frequency.QuadPart);
		time->tv_nsec = (long)((counter.QuadPart % frequency.QuadPart) * 1000000000LL / frequency.QuadPart);
	}
	else
	{
		/* 100 ns intervals since 1601 */
		FILETIME now;
		unsigned long long intervals;

		GetSystemTimeAsFileTime(&now);
		intervals = ((unsigned long long)now.dwHighDateTime << 32) | now.dwLowDateTime;
		intervals -= 116444736000000000ULL;
		time->tv_sec = (time_t)(intervals / 10000000ULL);
		time->tv_nsec = (long)(intervals % 10000000ULL) * 100;
	}
	return 0;
}

int nanosleep(const struct timespec *duration, struct timespec *remaining)
{
	long long milliseconds = (long long)duration->tv_sec * 1000 + (duration->tv_nsec + 999999) / 1000000;

	Sleep(milliseconds > 0x7fffffff ? 0x7fffffff : (DWORD)milliseconds);
	if (remaining)
	{
		remaining->tv_sec = 0;
		remaining->tv_nsec = 0;
	}
	return 0;
}

int clock_nanosleep(clockid_t clock, int flags, const struct timespec *time, struct timespec *remaining)
{
	struct timespec now;
	long long nanoseconds;

	if (!(flags & TIMER_ABSTIME))
		return nanosleep(time, remaining);
	for (;;)
	{
		clock_gettime(clock, &now);
		nanoseconds = ((long long)time->tv_sec - now.tv_sec) * 1000000000LL + (time->tv_nsec - now.tv_nsec);
		if (nanoseconds <= 0)
			return 0;
		if (nanoseconds > 2000000)
			Sleep((DWORD)(nanoseconds / 1000000 - 1));
		else
			SwitchToThread();
	}
}

int sched_yield(void)
{
	SwitchToThread();
	return 0;
}

long sysconf(int name)
{
	MEMORYSTATUS memory;

	switch (name)
	{
	case _SC_PAGESIZE:
		return 4096;
	case _SC_NPROCESSORS_ONLN:
		return 6;
	case _SC_PHYS_PAGES:
	case _SC_AVPHYS_PAGES:
		GlobalMemoryStatus(&memory);
		return (long)((name == _SC_PHYS_PAGES ? memory.dwTotalPhys : memory.dwAvailPhys) / 4096);
	default:
		errno = EINVAL;
		return -1;
	}
}

/* ---------- files */

static ssize_t positioned_transfer(int descriptor, void *buffer, size_t count, off_t offset, BOOL write)
{
	HANDLE handle = (HANDLE)_get_osfhandle(descriptor);
	OVERLAPPED overlapped;
	DWORD transferred = 0;
	BOOL result;

	if (handle == INVALID_HANDLE_VALUE)
	{
		errno = EBADF;
		return -1;
	}
	memset(&overlapped, 0, sizeof(overlapped));
	overlapped.Offset = (DWORD)offset;
	overlapped.OffsetHigh = (DWORD)((unsigned long long)offset >> 32);
	result = write ?
		WriteFile(handle, buffer, (DWORD)count, &transferred, &overlapped) :
		ReadFile(handle, buffer, (DWORD)count, &transferred, &overlapped);
	if (!result)
	{
		if (!write && GetLastError() == ERROR_HANDLE_EOF)
			return 0;
		errno = errno_from_error(GetLastError());
		return -1;
	}
	return (ssize_t)transferred;
}

ssize_t pread(int descriptor, void *buffer, size_t count, off_t offset)
{
	return positioned_transfer(descriptor, buffer, count, offset, FALSE);
}

ssize_t pwrite(int descriptor, const void *buffer, size_t count, off_t offset)
{
	return positioned_transfer(descriptor, (void *)buffer, count, offset, TRUE);
}

/* the executable's path, for /proc/self/exe (the data folder is found
from it): the title's folder */
ssize_t readlink(const char *path, char *buffer, size_t size)
{
	static const char executable[] = "game:/default.xex";

	if (strcmp(path, "/proc/self/exe") != 0 || size < sizeof(executable))
	{
		errno = EINVAL;
		return -1;
	}
	memcpy(buffer, executable, sizeof(executable) - 1);
	return (ssize_t)(sizeof(executable) - 1);
}

int pause(void)
{
	Sleep(INFINITE);
	return -1;
}

/* ---------- memory mapping: the Xbox window (x360_memory.c) only */

void *mmap(void *address, size_t length, int protection, int flags, int descriptor, long offset)
{
	(void)descriptor;
	(void)offset;
	if (!(flags & MAP_ANONYMOUS))
	{
		errno = ENODEV;
		return MAP_FAILED;
	}
	if ((flags & MAP_FIXED) && x360_window_contains(address, length))
	{
		if (protection == PROT_NONE && (flags & MAP_NORESERVE))
		{
			/* pages given back: their memory stays committed, cleared */
			memset(address, 0, length);
			return address;
		}
		if (!x360_window_commit(address, length))
		{
			errno = ENOMEM;
			return MAP_FAILED;
		}
		memset(address, 0, length);
		return address;
	}
	/* anything else: plain memory from the console's heap */
	{
		void *block = VirtualAlloc(NULL, length, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

		if (!block)
		{
			errno = ENOMEM;
			return MAP_FAILED;
		}
		return block;
	}
}

int munmap(void *address, size_t length)
{
	if (x360_window_contains(address, length))
		return 0;
	if (!VirtualFree(address, 0, MEM_RELEASE))
	{
		errno = EINVAL;
		return -1;
	}
	return 0;
}

/* page protection: the console's title memory is not protected per page
here (the texture write watch is off: x360_memory.c) */
int mprotect(void *address, size_t length, int protection)
{
	(void)address;
	(void)length;
	(void)protection;
	return 0;
}
