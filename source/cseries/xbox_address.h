/*
XBOX_ADDRESS.H

Pointers inside Xbox data, for the 64-bit builds (HALO_64BIT).

The game's data formats embed 32-bit pointers: cache files hold tag data
whose blocks, references and data point at each other by address, game
state is saved and restored as raw memory, and Direct3D resources carry
physical addresses. A 64-bit build keeps all of that memory inside one
4 GB region reserved at a fixed host address, so that Xbox address X is host
address XBOX_ADDRESS_SPACE_BASE + X (port/linux/src/xbox_memory.c).
Structures laid out like the Xbox's declare their pointer fields XPTR(type)
(a 32-bit Xbox address) and convert with XBOX_POINTER and XBOX_ADDRESS.

HALO_XBOX_WINDOW is the same layer in a 32-bit build that cannot have the
Xbox's addresses (the Xbox 360): a smaller window, anywhere in its memory.

Everywhere else (the Xbox, the matching MSVC build and the 32-bit ports) a
pointer is an Xbox address: XPTR(type) is `type *` and the conversions do
nothing, so the code compiles as before.
*/

#ifndef __XBOX_ADDRESS_H
#define __XBOX_ADDRESS_H

#if defined(HALO_64BIT) && defined(HALO_XBOX_WINDOW)

/* A 32-bit build whose host cannot have the Xbox's addresses (the Xbox 360:
0x80000000 up is its executable's): the Xbox addresses the game uses,
XBOX_WINDOW_LOW to XBOX_WINDOW_HIGH (its heap below 0x80000000, the
contiguous memory above), are one block of host memory placed wherever the
host had room. Xbox address X is host address xbox_address_space_base + X,
modulo 2^32 (port/xbox360/src/x360_memory.c). */

#define XBOX_WINDOW_LOW 0x78000000U
#define XBOX_WINDOW_HIGH 0x88000000U
#define XBOX_WINDOW_SIZE (XBOX_WINDOW_HIGH - XBOX_WINDOW_LOW)

/* the host address of Xbox address 0, modulo 2^32 */
extern unsigned int xbox_address_space_base;

#define XPTR(type) unsigned int
#define XBOX_NULL 0

static __inline__ __attribute__((always_inline)) void *xbox_pointer(unsigned int address)
{
	return address ? (void *)(__UINTPTR_TYPE__)(xbox_address_space_base + address) : (void *)0;
}

void xbox_address_out_of_range(void const *pointer) __attribute__((noreturn));

static __inline__ __attribute__((always_inline)) unsigned int xbox_address(void const *pointer)
{
	unsigned int address = (unsigned int)(__UINTPTR_TYPE__)pointer - xbox_address_space_base;

	if (!pointer)
		return 0;
	if (address - XBOX_WINDOW_LOW >= XBOX_WINDOW_SIZE)
		xbox_address_out_of_range(pointer);
	return address;
}

#define POINTER_BITS(pointer) ((__UINTPTR_TYPE__)(pointer))

#elif defined(HALO_64BIT)

#if defined(__linux__) && defined(__aarch64__)
/* 256 GB: below the top of the smallest address space a 64-bit ARM Linux
kernel gives a program (512 GB, with 39-bit addresses: Raspberry Pi OS's
kernels, Android's) */
#define XBOX_ADDRESS_SPACE_BASE 0x4000000000ULL /* 256 GB */
#else
#define XBOX_ADDRESS_SPACE_BASE 0x10000000000ULL /* 1 TB */
#endif
#define XBOX_ADDRESS_SPACE_SIZE 0x100000000ULL /* 4 GB */

/* a pointer field of an Xbox-layout structure; type documents the target */
#define XPTR(type) unsigned int
/* an XPTR field's null */
#define XBOX_NULL 0

/* Xbox address to host pointer; 0 stays NULL */
static __inline__ __attribute__((always_inline)) void *xbox_pointer(unsigned int address)
{
	return address ? (void *)(XBOX_ADDRESS_SPACE_BASE + address) : (void *)0;
}

/* reports a pointer outside the Xbox address space and aborts */
void xbox_address_out_of_range(void const *pointer) __attribute__((noreturn));

/* host pointer to Xbox address; NULL stays 0 */
static __inline__ __attribute__((always_inline)) unsigned int xbox_address(void const *pointer)
{
	unsigned long long offset = (unsigned long long)(__UINTPTR_TYPE__)pointer - XBOX_ADDRESS_SPACE_BASE;

	if (!pointer)
		return 0;
	if (offset >= XBOX_ADDRESS_SPACE_SIZE)
		xbox_address_out_of_range(pointer);
	return (unsigned int)offset;
}

/* a pointer as an integer, for alignment tests, ordering and differences
(the Xbox code cast pointers to int or unsigned int for these) */
#define POINTER_BITS(pointer) ((__UINTPTR_TYPE__)(pointer))

#else

#define XPTR(type) type *
#define XBOX_NULL NULL
#define xbox_pointer(address) ((void *)(address))
#define xbox_address(pointer) ((void *)(pointer))
#define POINTER_BITS(pointer) ((unsigned long)(pointer))

#endif

#define XBOX_POINTER(type, address) ((type *)xbox_pointer(address))
#define XBOX_ADDRESS(pointer) xbox_address(pointer)

#endif // __XBOX_ADDRESS_H
