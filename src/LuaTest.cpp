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
    luaL_openlibs(L);
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
