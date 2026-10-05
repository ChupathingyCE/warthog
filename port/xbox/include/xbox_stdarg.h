/*
XBOX_STDARG.H

Force-included ahead of every unit the Xbox build compiles with the SDK's
own headers first (tools/xbox_build.py's SUPPORT_FLAGS: port/xbox/src, the
generated sources and internet play's): the SDK's stdarg.h, then clang's
builtins in place of its va_start, va_arg and va_end.

The SDK's i386 va_start is MSVC's "(va_list)&last_parameter + its size": it
takes the address of a parameter and walks past it. Clang does not promise
that the variable arguments are there. Once it inlines a variadic function
(snprintf into log_address, in the same unit), the parameter is a local of
the caller and the arguments are elsewhere, so the callee reads whatever
lies past that local. That crashed the console in the C runtime's _output
on the first public address internet play logged: a string's bytes read as
a %s pointer. The builtins describe the arguments to the compiler, which
then keeps them where va_arg finds them, inlined or not.
*/

#ifndef __XBOX_STDARG_H
#define __XBOX_STDARG_H

#include <stdarg.h>

#undef va_start
#undef va_arg
#undef va_end
#define va_start(list, last) __builtin_va_start(list, last)
#define va_arg(list, type) __builtin_va_arg(list, type)
#define va_end(list) __builtin_va_end(list)
#ifndef va_copy
#define va_copy(destination, source) __builtin_va_copy(destination, source)
#endif

#endif
