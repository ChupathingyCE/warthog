/*
X360_TLS.C

Thread-local variables (__thread: the platform layer's last error and
queued completions, the game's assert flag). The console's own TLS is
Microsoft's compiler's (r13 and the loader's TLS directory), which this
compiler cannot emit; its ELF TLS would read through r2, which the console
leaves zero. The build compiles with -femulated-tls instead, so each access
calls __emutls_get_address, given here: every thread keeps an array of its
variables in one TLS slot, each variable's copy made on its first use from
the variable's initial value.
*/

#include <xtl.h>
#include <stdlib.h>
#include <string.h>

#include "x360_port.h"

/* what clang emits for each thread-local variable (compiler-rt's emutls) */
struct emutls_control
{
	size_t size;
	size_t align;
	union
	{
		UINT_PTR index;
		void *address;
	} object;
	void *value; /* the initial value, or NULL for zeros */
};

struct emutls_array
{
	UINT_PTR count;
	void *data[1];
};

static DWORD slot = TLS_OUT_OF_INDEXES;
static volatile LONG lock_state;
static CRITICAL_SECTION lock;
static UINT_PTR variables;

static void lock_enter(void)
{
	if (lock_state != 2)
	{
		if (InterlockedCompareExchange(&lock_state, 1, 0) == 0)
		{
			InitializeCriticalSection(&lock);
			slot = TlsAlloc();
			X360_LWSYNC();
			lock_state = 2;
		}
		while (lock_state != 2)
			Sleep(0);
	}
	EnterCriticalSection(&lock);
}

void *__emutls_get_address(struct emutls_control *control)
{
	UINT_PTR index = control->object.index;
	struct emutls_array *array;
	void *copy;

	if (!index)
	{
		lock_enter();
		index = control->object.index;
		if (!index)
		{
			index = ++variables;
			X360_LWSYNC();
			control->object.index = index;
		}
		LeaveCriticalSection(&lock);
	}
	array = TlsGetValue(slot);
	if (!array || array->count < index)
	{
		UINT_PTR old_count = array ? array->count : 0;
		UINT_PTR count = index + 16;
		struct emutls_array *grown = realloc(array, sizeof(*grown) + sizeof(void *) * count);

		if (!grown)
			abort();
		memset(&grown->data[old_count], 0, sizeof(void *) * (count + 1 - old_count));
		grown->count = count;
		array = grown;
		TlsSetValue(slot, array);
	}
	copy = array->data[index - 1];
	if (!copy)
	{
		size_t align = control->align < sizeof(void *) ? sizeof(void *) : control->align;

		copy = _aligned_malloc(control->size ? control->size : 1, align);
		if (!copy)
			abort();
		if (control->value)
			memcpy(copy, control->value, control->size);
		else
			memset(copy, 0, control->size);
		array->data[index - 1] = copy;
	}
	return copy;
}
