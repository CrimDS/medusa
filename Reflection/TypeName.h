/**
	@file TypeName.h
	@brief TODO

	(c)2012 Palestar Inc
	@author Richard Lyle @date 4/22/2012 10:29:08 AM
*/

#ifndef TYPENAME_H
#define TYPENAME_H

#include "Standard/CharString.h"
#include "Standard/Types.h"

#if !defined(_WIN32)
#include <cxxabi.h>
#include <cstdlib>
#endif

//---------------------------------------------------------------------------------------------------

//! This function returns the name of the given type..
template<typename T>
struct TypeName
{
	static const CharString & name()
	{
		static CharString sName;
		if ( sName[0] == 0 )
		{
	#if defined(_WIN32)
			sName = typeid(T).name();
			// strip the type qualifiers off the front of VS generated type names,
			// so our types will be normalized between windows & linux..
			sName.replace( "class ", "" );
			sName.replace( "struct ", "" );
			sName.replace( "enum ", "" );
			sName.replace( "union ", "" );
	#else
			// gcc/clang Itanium ABI typeid().name() returns the mangled name
			// (e.g. `15WidgetReferenceI4NounE` for `WidgetReference<Noun>`).
			// Demangle it so the resulting CharString matches what MSVC's
			// strip pass produces (`WidgetReference<Noun>`).  Without this,
			// every templated/non-primitive type hashes to a different
			// nameHash on Linux vs Windows, and Value::read on the client
			// can't resolve types the Linux server sends over the wire.
			int status = 0;
			char * demangled = abi::__cxa_demangle(
				typeid(T).name(), nullptr, nullptr, &status );
			if ( status == 0 && demangled != nullptr )
			{
				sName = demangled;
				::free( demangled );
			}
			else
			{
				sName = typeid(T).name();
			}
	#endif
		}

		return sName;
	}
};

//---------------------------------------------------------------------------------------------------
// Use specialization on all the basic types, so our type names will be normalized between platforms.

template<>
struct TypeName<bool>
{
	static const CharString & name() 
	{ 
		static CharString name( "bool" );
		return name; 
	}
};

template<>
struct TypeName<u8>
{
	static const CharString & name() 
	{ 
		static CharString name( "u8" );
		return name; 
	}
};

template<>
struct TypeName<s8>
{
	static const CharString & name() 
	{ 
		static CharString name( "s8" );
		return name; 
	}
};

template<>
struct TypeName<u16>
{
	static const CharString & name() 
	{ 
		static CharString name( "u16" );
		return name; 
	}
};

template<>
struct TypeName<s16>
{
	static const CharString & name() 
	{ 
		static CharString name( "s16" );
		return name; 
	}
};

template<>
struct TypeName<u32>
{
	static const CharString & name() 
	{ 
		static CharString name( "u32" );
		return name; 
	}
};

template<>
struct TypeName<s32>
{
	static const CharString & name() 
	{ 
		static CharString name( "s32" );
		return name; 
	}
};

template<>
struct TypeName<u64>
{
	static const CharString & name() 
	{ 
		static CharString name( "u64" );
		return name; 
	}
};

template<>
struct TypeName<s64>
{
	static const CharString & name() 
	{ 
		static CharString name( "s64" );
		return name; 
	}
};

// On Windows LLP64 `ul32` is `unsigned long`, which is a distinct type from
// `u32` (= `unsigned int`) even though both are 32-bit — needs its own
// specialization or templates picking up `unsigned long` fields fall through
// to typeid(T).name() and produce non-normalized names.
// On Linux LP64 we re-typedef `ul32` to `unsigned int` (see Types.h: native
// `unsigned long` is 64-bit and would silently corrupt hash math).  That
// makes ul32==u32 as a type, and adding a TypeName<ul32> specialization
// would be a redefinition error — so guard these to Windows only.
#if defined(_WIN32)
template<>
struct TypeName<ul32>
{
	static const CharString & name()
	{
		static CharString name( "u32" );
		return name;
	}
};

template<>
struct TypeName<sl32>
{
	static const CharString & name()
	{
		static CharString name( "s32" );
		return name;
	}
};
#endif

template<>
struct TypeName<f32>
{
	static const CharString & name() 
	{ 
		static CharString name( "f32" );
		return name; 
	}
};

template<>
struct TypeName<f64>
{
	static const CharString & name() 
	{ 
		static CharString name( "f64" );
		return name; 
	}
};

template<>
struct TypeName<wchar>
{
	static const CharString & name() 
	{ 
		static CharString name( "wchar" );
		return name; 
	}
};

template<>
struct TypeName<CharString>
{
	static const CharString & name() 
	{ 
		static CharString name( "CharString" );
		return name; 
	}
};

template<>
struct TypeName<WideString>
{
	static const CharString & name() 
	{ 
		static CharString name( "WideString" );
		return name; 
	}
};

#endif

//---------------------------------------------------------------------------------------------------
//EOF
