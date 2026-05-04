/*
	LuaHeaders.h

	This header includes all required Lua headers and also adds the static Lua lib for linking.
	There is a define USE_PLAIN_LUA to use Lua 5.1 instead of LuaJIT 5.1
	(c)2009 Palestar Development, Richard Lyle
*/

#ifndef LUA_HEADERS_H
#define LUA_HEADERS_H

//----------------------------------------------------------------------------

// Always use LuaJIT/src/ headers regardless of USE_PLAIN_LUA mode.  The LuaLib
// project is built from this same LuaJIT source tree (with USE_PLAIN_LUA on x64
// to compile out the JIT internals — see project_x64_cpp17_migration memory),
// so the headers and the resulting lua51.lib are always ABI-matched.  The
// USE_PLAIN_LUA define still gates JIT-specific call sites in consumer code
// (luaopen_jit registration, JIT optimizer activation) below and in
// WorldContextScript.cpp.  The medusa/ThirdParty/Lua51/ source tree is now
// effectively unused — kept for posterity only.
#pragma include_alias( "lua.h",      "../../Medusa/ThirdParty/LuaJIT/src/lua.h" )
#pragma include_alias( "lualib.h",   "../../Medusa/ThirdParty/LuaJIT/src/lualib.h" )
#pragma include_alias( "lauxlib.h",  "../../Medusa/ThirdParty/LuaJIT/src/lauxlib.h" )
#pragma include_alias( "luajit.h",   "../../Medusa/ThirdParty/LuaJIT/src/luajit.h" )

// Auto-link the Lua interpreter library.  Centralized here so every consumer
// of LuaHeaders.h gets the right .lib without per-vcxproj plumbing.  Path varies
// by platform — x64 lib lives under bin/x64/ and is built without JIT (see
// USE_PLAIN_LUA in LuaLib.vcxproj per project_x64_cpp17_migration).
#if defined(_M_X64)
	#ifdef _DEBUG
		#pragma comment( lib, "../../Medusa/ThirdParty/LuaJIT/bin/x64/Lua51D.lib" )
	#else
		#pragma comment( lib, "../../Medusa/ThirdParty/LuaJIT/bin/x64/Lua51.lib" )
	#endif
#else
	#ifdef _DEBUG
		#pragma comment( lib, "../../Medusa/ThirdParty/LuaJIT/bin/Lua51D.lib" )
	#else
		#pragma comment( lib, "../../Medusa/ThirdParty/LuaJIT/bin/Lua51.lib" )
	#endif
#endif

//----------------------------------------------------------------------------

extern "C" {
	#ifndef USE_PLAIN_LUA
		#include "luajit.h"
	#endif
	#include "lua.h"		// "Lua" scripting language - www.lua.org
	#include "lualib.h"
	#include "lauxlib.h"
};

#endif

//----------------------------------------------------------------------------
// EOF
