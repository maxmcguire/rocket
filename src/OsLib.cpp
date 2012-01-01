/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include <assert.h>
#include <stdlib.h>
#include <time.h>

static int OsLib_Clock(lua_State *L)
{
    lua_pushnumber(L, ((lua_Number)clock())/(lua_Number)CLOCKS_PER_SEC);
    return 1;
}

static int OsLib_GetEnv(lua_State *L)
{
    lua_pushstring(L, getenv(luaL_checkstring(L, 1)));
    return 1;
}

LUALIB_API int luaopen_os(lua_State *L)
{

    static const luaL_Reg functions[] =
        {
            { "clock",   OsLib_Clock },
            { "getenv",  OsLib_GetEnv },
            { NULL, NULL }
        };

    luaL_register(L, LUA_OSLIBNAME, functions);
    return 1;

}