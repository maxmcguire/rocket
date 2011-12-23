/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */
#ifndef LAUXLIB_H
#define LAUXLIB_H

#include "Lua.h"

struct luaL_Reg
{
    const char*     name;
    lua_CFunction   func;
};

/* extra error code for `luaL_load' */
#define LUA_ERRFILE     (LUA_ERRERR+1)

lua_State* luaL_newstate();

void luaL_register(lua_State *L, const char* libname, const luaL_Reg* l);

LUALIB_API int luaL_loadfile(lua_State *L, const char *filename);
LUALIB_API int luaL_loadbuffer(lua_State *L, const char *buff, size_t sz, const char *name);
LUALIB_API int luaL_loadstring(lua_State *L, const char *s);

#define luaL_dofile(L, fn) \
	(luaL_loadfile(L, fn) || lua_pcall(L, 0, LUA_MULTRET, 0))

#define luaL_dostring(L, s) \
	(luaL_loadstring(L, s) || lua_pcall(L, 0, LUA_MULTRET, 0))

LUALIB_API int (luaL_typerror) (lua_State *L, int narg, const char *tname);
LUALIB_API int (luaL_argerror) (lua_State *L, int numarg, const char *extramsg);
LUALIB_API const char* luaL_checklstring(lua_State *L, int numArg, size_t *l);
LUALIB_API const char* luaL_checkstring(lua_State* L, int numArg);

LUALIB_API const char *(luaL_optlstring) (lua_State *L, int numArg,
                                          const char *def, size_t *l);
LUALIB_API lua_Number (luaL_checknumber) (lua_State *L, int numArg);
LUALIB_API lua_Number (luaL_optnumber) (lua_State *L, int nArg, lua_Number def);

LUALIB_API lua_Integer (luaL_checkinteger) (lua_State *L, int numArg);
LUALIB_API lua_Integer (luaL_optinteger) (lua_State *L, int nArg,
                                          lua_Integer def);
LUALIB_API int luaL_checkint (lua_State *L, int narg);
LUALIB_API int luaL_optint (lua_State *L, int narg, int d);

LUALIB_API void (luaL_checkstack) (lua_State *L, int sz, const char *msg);
LUALIB_API void (luaL_checktype) (lua_State *L, int narg, int t);
LUALIB_API void (luaL_checkany) (lua_State *L, int narg);
LUALIB_API void *(luaL_checkudata) (lua_State *L, int ud, const char *tname);

LUALIB_API void luaL_where (lua_State* L, int level);
LUALIB_API int (luaL_error) (lua_State *L, const char *fmt, ...);

#define luaL_argcheck(L, cond,numarg,extramsg)	\
		((void)((cond) || luaL_argerror(L, (numarg), (extramsg))))

LUALIB_API const char* luaL_typename(lua_State* L, int index);

#define luaL_opt(L,f,n,d)	(lua_isnoneornil(L,(n)) ? (d) : f(L,(n)))

LUALIB_API int luaL_getmetafield (lua_State *L, int obj, const char* event);

#define luaL_argcheck(L, cond,numarg,extramsg)	\
		((void)((cond) || luaL_argerror(L, (numarg), (extramsg))))
#define luaL_optstring(L,n,d)	(luaL_optlstring(L, (n), (d), NULL))
#define luaL_checklong(L,n)	((long)luaL_checkinteger(L, (n)))
#define luaL_optlong(L,n,d)	((long)luaL_optinteger(L, (n), (d)))

#define luaL_dofile(L, fn) \
	(luaL_loadfile(L, fn) || lua_pcall(L, 0, LUA_MULTRET, 0))

#define luaL_dostring(L, s) \
	(luaL_loadstring(L, s) || lua_pcall(L, 0, LUA_MULTRET, 0))

#define luaL_getmetatable(L,n)	(lua_getfield(L, LUA_REGISTRYINDEX, (n)))

#define luaL_opt(L,f,n,d)	(lua_isnoneornil(L,(n)) ? (d) : f(L,(n)))

#endif