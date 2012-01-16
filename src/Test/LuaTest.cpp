/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#include "LuaTest.h"

#include <stdio.h>

LuaFixture::LuaFixture()
{
    L = luaL_newstate();
    // Open the base library.
    lua_pushcfunction(L, luaopen_base);
    lua_pushstring(L, "");
    lua_call(L, 1, 0);
}

LuaFixture::~LuaFixture()
{
    lua_close(L);
}

bool DoString(lua_State* L, const char* string)
{
    if (luaL_dostring(L, string) != 0)
    {
        fprintf(stderr, "%s\n", lua_tostring(L, -1));
        return false;
    }
    return true;
}
