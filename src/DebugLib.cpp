/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

LUALIB_API int luaopen_debug(lua_State *L)
{
    return 1;
}