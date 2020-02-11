#ifndef MACROS_H
#define MACROS_H

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define UNUSED_PARAM(p) (void)(p)
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#define ABS(x) ((x) >= 0) ? (x) : -(x)

#define STR(x) #x
#define STRX(x) STR(x)
#define CONCAT(x, y) x ## y
#define CONCAT2(x, y) CONCAT(x, y)
#define LINE_SUFFIXED_NAME(name) CONCAT2(name, __LINE__)
#define TBL_SZ(tbl) (sizeof(tbl)/sizeof(tbl[0]))
#define ZEROIZE_STRUCT(s) memset(&(s), 0, sizeof(s))

#define _STR_CPY(from, to) do {strncpy(to, from, sizeof(to)-1); to[sizeof(to)-1] = '\0';} while(0)
#define STR_CPY(from, to) do {if (from) _STR_CPY(from, to);} while(0)

#endif
