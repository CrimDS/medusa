/*
** ljit_stub.c — minimal JIT-API stubs for the no-JIT (USE_PLAIN_LUA) build.
**
** When USE_PLAIN_LUA is defined the core interpreter skips every luaJIT_*
** entry point, so we can exclude ljit_core.c / ljit_backend.c / ljit_dasm.c
** / ljit_mem.c from the build (they require DynASM, which isn't bundled and
** is x86-only anyway).  We also exclude ljitlib.c (the Lua-side `jit.*`
** module) because it #includes ljit.h's full JIT internals.
**
** BUT — WorldContextScript.cpp's library table at line 317 still references
** `luaopen_jit` to register the `jit` module name.  This stub provides it,
** registering an empty table so any script that does `require "jit"` or
** `jit.on()` gets a sensible no-op rather than a link failure.
**
** Only built on the Linux (gcc) path; the Windows build keeps the real
** ljitlib.c / ljit_*.c from LuaLib.vcxproj.
*/

#define ljit_stub_c
#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

LUALIB_API int luaopen_jit(lua_State *L)
{
	/* Register an empty `jit` table.  Scripts that probe for jit.on /
	   jit.off / jit.status will see nil and treat the JIT as disabled,
	   which is exactly the truth on this build. */
	lua_newtable(L);
	lua_pushliteral(L, "PluginAPI: jit module disabled (USE_PLAIN_LUA build)");
	lua_setfield(L, -2, "version");
	return 1;
}
