/*
	Types.h

	Standard Types
	(c)2005 Palestar, Richard Lyle
*/

#ifndef TYPES_H
#define TYPES_H

#ifdef __GNUC__
#include <wchar.h>
#endif

//----------------------------------------------------------------------------

#if defined(_WIN32)

typedef wchar_t				wchar;
typedef unsigned __int64	u64;
typedef __int64				s64;

#define LL( number )		(number##LL)
#define PATH_SEPERATOR		"\\"
#define PATH_SEPERATORC		'\\'

#else

typedef wchar_t				wchar;
typedef unsigned long long	u64;
typedef long long 			s64;

#define LL( number )		(number##LL)
#define PATH_SEPERATOR		"/"
#define PATH_SEPERATORC		'/'

#endif

//----------------------------------------------------------------------------

// NOTE: Try to use these types only when it's very important to know how many bits are in your type, 
// otherwise try to use the built in compiler types such as int, long, etc..

// Note: This is not declared inside a namespace to avoid ambigous symbol errors

typedef unsigned char		u8;
typedef char				s8;
typedef unsigned short		u16;
typedef short				s16;
typedef unsigned int		u32;
typedef int					s32;
// Must be exactly 32-bit on every target.  `unsigned long` IS 32-bit on
// Windows LLP64 (MSVC x64) but 64-bit on Linux LP64 (gcc x64) — using it
// unconditionally silently broke every StringHash64 / ClassKey computation
// on Linux (hash rotations stopped wrapping at 32 bits, so Linux-computed
// hash of e.g. "GameContext" didn't match the value stored in .wob files
// written by the Windows build).
//
// Branch on platform so we get 32 bits on both, AND match the Win32 API
// `DWORD` (= `unsigned long`) on Windows — otherwise every Reg*/Thread*/
// GetExitCode* call rejects `dword*` arguments with C2664.
#if defined(_WIN32)
typedef unsigned long		ul32;
typedef long				sl32;
#else
typedef unsigned int		ul32;
typedef int					sl32;
#endif

typedef float				f32;
typedef double				f64;

typedef u64					qword;
typedef ul32				dword;
typedef u16					word;
typedef u8					byte;

#endif

//----------------------------------------------------------------------------
// EOF
