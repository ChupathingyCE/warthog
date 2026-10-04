#!/bin/sh
# The console's game list code on the host (port/xbox/src/xbox_game_list_*.c),
# under AddressSanitizer and UndefinedBehaviorSanitizer:
#
#   port/xbox/tests/run.sh               the parsers', the screen's words' and the
#                                        settings record layouts' tests,
#                                        and 200,000 mutations
#   port/xbox/tests/run.sh --fuzz 300    and libFuzzer for 300 seconds (LLVM's clang)
#   port/xbox/tests/run.sh --fetch       and the fetch of the real list, over the
#                                        host's sockets (warthog.milenko.org)
#
# CC picks the compiler (default: Homebrew's clang if there, else cc). The
# binaries and libFuzzer's corpus go to build/xbox-tests.
set -e
cd "$(dirname "$0")/../../.."
if [ -z "$CC" ]; then
	CC=cc
	[ -x /opt/homebrew/opt/llvm/bin/clang ] && CC=/opt/homebrew/opt/llvm/bin/clang
fi
OUT=build/xbox-tests
SAN="-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined"
SRC=port/xbox/src
mkdir -p "$OUT"
$CC -std=c89 -pedantic -Wall -Wextra -Werror $SAN -c $SRC/xbox_game_list_parse.c -o "$OUT/parse.o"
$CC -std=c89 -pedantic -Wall -Wextra -Werror $SAN -c $SRC/xbox_game_list_text.c -o "$OUT/text.o"
$CC -Wall -Wextra $SAN port/xbox/tests/game_list_test.c "$OUT/parse.o" "$OUT/text.o" -o "$OUT/game_list_test"
"$OUT/game_list_test" 200000
$CC -std=c89 -pedantic -Wall -Wextra -Werror $SAN port/xbox/tests/network_game_layout_test.c \
	$SRC/xbox_network_game_layout.c -o "$OUT/network_game_layout_test"
"$OUT/network_game_layout_test"
while [ $# -gt 0 ]; do
	case "$1" in
	--fuzz)
		mkdir -p "$OUT/corpus"
		printf 'HTTP/1.0 200 OK\r\nContent-Length: 21\r\n\r\nwarthog-list 1 0\nend\n' > "$OUT/corpus/seed"
		$CC -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined \
			port/xbox/tests/game_list_fuzz.c $SRC/xbox_game_list_parse.c -o "$OUT/game_list_fuzz"
		"$OUT/game_list_fuzz" -max_len=65536 -max_total_time="$2" -print_final_stats=1 "$OUT/corpus"
		shift 2
		;;
	--fetch)
		$CC -Wall -Wextra $SAN port/xbox/tests/game_list_fetch_test.c $SRC/xbox_game_list_fetch.c \
			$SRC/xbox_game_list_parse.c -o "$OUT/game_list_fetch_test"
		"$OUT/game_list_fetch_test"
		shift
		;;
	*)
		echo "usage: $0 [--fuzz SECONDS] [--fetch]" >&2
		exit 2
		;;
	esac
done
