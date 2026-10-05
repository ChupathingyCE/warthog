#!/bin/sh
# Checks of the Xbox build's own units (port/xbox/tests/run.sh --xbox): what
# the console's C runtime and SDK headers would get wrong, before a console
# finds it.
#
# - va_start: a unit compiled with the SDK's headers first (port/xbox/src,
#   internet play's) must expand va_start to clang's builtin
#   (port/xbox/include/xbox_stdarg.h), never the SDK's "&last + size", which
#   reads the wrong memory once clang inlines a variadic function.
# - printf formats: the console's C runtime (2003's) has no %z, %ll, %j or
#   %hh length modifiers; the units it formats with must not use them.
#   (Formats against their arguments are -Werror=format in the build.)
#
# Needs a configured Xbox build (build.ninja with the xbox target).
set -e
cd "$(dirname "$0")/../../.."
failures=0
probe=build/xbox-tests/va_probe.c
mkdir -p build/xbox-tests
printf '#include <stdarg.h>\nvoid probe(const char *f, ...) { va_list a; va_start(a, f); va_end(a); }\n' > "$probe"
command=$(ninja -t commands build/xbox/obj/port/xbox/src/xbox_port.c.obj | tail -1)
if [ -z "$command" ]; then
	echo "xbox_check: no Xbox build configured (configure.py --xbox-d3d8)" >&2
	exit 2
fi
command=$(printf '%s' "$command" | sed -e 's| -c port/xbox/src/xbox_port.c| -E '"$probe"'|' -e 's| -o [^ ]*\.obj| -o -|' \
	-e 's|-MD -MF [^ ]* ||')
expanded=$(eval "$command" | grep -A1 'void probe' || true)
if printf '%s' "$expanded" | grep -q '__builtin_va_start'; then
	echo "xbox_check: va_start is clang's builtin"
else
	echo "xbox_check: va_start is not clang's builtin in the SDK-header units:" >&2
	printf '%s\n' "$expanded" >&2
	failures=$((failures + 1))
fi
# (format strings with a length modifier the 2003 runtime lacks)
bad=$(grep -nE '"[^"]*%[-+ #0-9.*]*(z|ll|j|hh|t)[diouxXcs]' port/linux/src/p2p.c port/linux/src/p2p_signal.c \
	port/linux/src/p2p_crypto.c port/xbox/src/*.c port/xbox/game/*.c port/linux/game/network_*.c || true)
if [ -n "$bad" ]; then
	echo "xbox_check: formats the console's C runtime does not read:" >&2
	printf '%s\n' "$bad" >&2
	failures=$((failures + 1))
else
	echo "xbox_check: no %z, %ll, %j, %hh or %t formats"
fi
[ "$failures" -eq 0 ]
