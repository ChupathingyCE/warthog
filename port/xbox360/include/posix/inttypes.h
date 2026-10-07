/* POSIX <inttypes.h>, which the console's C runtime does not have: the
format macros of <stdint.h>'s types, 32-bit long */

#ifndef __HALO_XBOX360_INTTYPES_H
#define __HALO_XBOX360_INTTYPES_H

#include <stdint.h>

#define PRId8 "d"
#define PRId16 "d"
#define PRId32 "d"
#define PRId64 "lld"
#define PRIi32 "i"
#define PRIi64 "lli"
#define PRIu8 "u"
#define PRIu16 "u"
#define PRIu32 "u"
#define PRIu64 "llu"
#define PRIx8 "x"
#define PRIx16 "x"
#define PRIx32 "x"
#define PRIx64 "llx"
#define PRIX32 "X"
#define PRIX64 "llX"
#define PRIdPTR "d"
#define PRIuPTR "u"
#define PRIxPTR "x"
#define SCNd32 "d"
#define SCNu32 "u"
#define SCNx32 "x"
#define SCNd64 "lld"
#define SCNu64 "llu"

#endif
