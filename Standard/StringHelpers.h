/*
	StringHelpers.h

	String helper functions to replace the stdlib string functions
	(c)2004 Palestar Inc, Richard Lyle
*/

#ifndef STRINGHELPERS_H
#define STRINGHELPERS_H

#include <stdlib.h>

#ifndef NULL
#define NULL (0)
#endif

#pragma warning( disable : 4996 )		// This function or variable may be unsafe. Consider using strncpy_s instead. To disable deprecation, use _CRT_SECURE_NO_WARNINGS. See online help for details.

//----------------------------------------------------------------------------

// convert character to lower-case
template<typename C>
inline C tolower( C ch )
{
	if ( ch >= 'A' && ch <= 'Z' )
		ch += 'a' - 'A';
	return ch;
}

// convert character to upper-case
template<typename C>
inline C toupper( C ch )
{
	if ( ch >= 'a' && ch <= 'z' )
		ch += 'A' - 'a';
	return ch;
}

// string length
template<typename C>
inline size_t strlen( const C * pSrc )
{
	size_t nCount = 0;
	while( *pSrc++ != 0 )
		nCount++;

	return nCount;
}

// string copy
template<typename C>
inline C * strcpy( C * pDst, const C * pSrc )
{
	C * pCopy = pDst;
	for(;(*pDst = *pSrc); ++pDst, ++pSrc);
	return pCopy;
}

// string copy limit
template<typename C>
inline C * strncpy( C * dst, const C * src, size_t n )
{
    if (n != 0) 
	{
        C *d = dst;
        const C *s = src;

        do {
            if ((*d++ = *s++) == 0) 
			{
                /* NUL pad the remaining n-1 bytes */
                while (--n != 0)
                        *d++ = 0;
                break;
            }
        } while (--n != 0);
    }
    return dst;
}

// string compare — returns 3-way comparison result like the C stdlib strcmp.
// Was size_t historically (returned ((size_t)*s1) - ((size_t)*s2), which on
// x64 is unsigned 64-bit and never negative, breaking ordering).  int is the
// correct return type and matches the C standard.
template<typename C>
inline int strcmp( const C * s1, const C * s2 )
{
    while (*s1 == *s2)
	{
		s2++;
		if (*s1++ == 0)
			return 0;
	}

	return ((int)*s1) - ((int)*s2);
}

// case insenstive compare
template<typename C>
inline int stricmp( const C * s1, const C * s2 )
{
    while ( tolower(*s1) == tolower(*s2) )
	{
		s2++;
		if (*s1++ == 0)
			return 0;
	}

	return ((int)tolower( *s1 )) - ((int)tolower( *s2 ));
}

// compare with limit
template<typename C>
inline int strncmp( const C * s1, const C * s2, size_t n )
{
	if ( n == 0 )
		return 0;

    do {
		if (*s1 != *s2)
			return ((int)*s1) - ((int)*s2);
		if (*s1++ == 0)
			break;
		s2++;

    } while (--n != 0);

    return 0;
}

// case insenstive compare with limit
template<typename C>
inline int strnicmp( const C * s1, const C * s2, size_t n )
{
	if ( n == 0 )
		return 0;

    do {
		C c1 = tolower(*s1);
		C c2 = tolower(*s2);

		if (c1 != c2)
			return ((int)c1) - ((int)c2);
		if (*s1++ == 0)
			break;
		s2++;

    } while (--n != 0);

    return 0;
}

// search for the first occurance of a character
template<typename C>
inline C * strchr( const C * p, wchar_t ch )
{
	C c = ch;
	for (;; ++p) 
	{
		if (*p == c)
			return (C *)p;
		if (*p == '\0')
			return NULL;
	}
	/* NOTREACHED */
}

template<typename C>
inline C * strichr( const C * p, char ch )
{
	C c = tolower( ch );
	for (;; ++p) 
	{
		if (tolower(*p) == c)
			return (C *)p;
		if (*p == '\0')
			return NULL;
	}
	/* NOTREACHED */
}

// search for the last occurance of a character
template<typename C>
inline C * strrchr( const C * p, size_t ch )
{
	C *save = NULL;
	C c = ch;
	for(;; ++p) 
	{
		if (*p == c)
			save = (C *)p;
		if (*p == '\0')
			return save;
	}
	/* NOTREACHED */
}

// search for a string
template<typename C>
inline C * strstr( const C * s, const C * find )
{
    C c, sc;
    size_t len;

    if ((c = *find++) != 0) 
	{
        len = strlen(find);
        do {
            do {
                if ((sc = *s++) == 0)
                    return NULL;
            } while (sc != c);
        } while (strncmp(s, find, len) != 0);
        s--;
    }
    return ((C *)s);
}

// search for a string inside another string, case insenstive
template<typename C>
inline C * stristr( const C * s, const C * find )
{
    C c, sc;
    size_t len;

    if ((c = *find++) != 0) 
	{
        len = strlen(find);
        do {
            do {
                if ((sc = *s++) == 0)
                    return NULL;
            } while ( tolower(sc) != tolower(c) );
        } while (strnicmp<C>(s, find, len) != 0);
        s--;
    }
    return ((C *)s);
}

// append a string
template<typename C>
inline C * strcat( C * s, const C * append )
{
    C *save = s;

    for (; *s; ++s);
    while ((*s++ = *append++));
    return save;
}

// convert a string to lower-case
template<typename C>
inline C * strlwr( C * s )
{
	C * save = s;
	while( (*s = tolower(*s)) )
		s++;
	return save;
}

// convert a string to upper-case
template<typename C>
inline C * strupr( C * s )
{
	C * save = s;
	while( (*s = toupper(*s)) )
		s++;
	return save;
}

template<typename C>
inline size_t strcspn( const C * s, const C *set )
{
    const C *p;
    const C *q;

    p = s;
    while (*p) {
            q = set;
            while (*q) {
                    if (*p == *q)
                            goto done;
                    q++;
            }
            p++;
    }

done:
    return (p - s);
}

//----------------------------------------------------------------------------

template<typename C>
inline C * strdup( const C * pSrc )
{
	C * p = (C *)malloc( (strlen( pSrc ) + 1) * sizeof(C) );
	strcpy( p, pSrc );

	return p;
}

//----------------------------------------------------------------------------



#endif

//----------------------------------------------------------------------------
//EOF
