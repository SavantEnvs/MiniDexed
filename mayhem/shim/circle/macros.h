// Host stand-in for Circle's <circle/macros.h> (fuzzing shim).
#ifndef _circle_macros_h
#define _circle_macros_h
#define PACKED __attribute__ ((packed))
#define ALIGN(n) __attribute__ ((aligned (n)))
#define NORETURN __attribute__ ((noreturn))
#define MAYBE_UNUSED __attribute__ ((unused))
#define BIT(n) (1U << (n))
#endif
