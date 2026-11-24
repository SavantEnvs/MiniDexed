// Host stand-in for Circle's <circle/types.h> (fuzzing shim; 64-bit host types).
#ifndef _circle_types_h
#define _circle_types_h
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef intptr_t intptr; typedef uintptr_t uintptr;
#ifdef __cplusplus
typedef bool boolean;
#define FALSE false
#define TRUE true
#else
typedef char boolean;
#define FALSE 0
#define TRUE 1
#endif
#endif
