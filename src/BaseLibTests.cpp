/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */

#include "Test.h"
#include "LuaTest.h"

TEST(IPairs)
{

    const char* code =
        "local t = { 'one', 'two', 'three', 'four' }\n"
        "t[4] = nil\n"
        "g, s, i = ipairs(t)\n"
        "v1 = g(s, i)\n"
        "v2 = g(s, v1)\n"
        "v3 = g(s, v2)\n"
        "v4 = g(s, v3)";

    lua_State* L = luaL_newstate();

    CHECK( DoString(L, code) );

    lua_getglobal(L, "v1");
    CHECK( lua_tonumber(L, -1) == 1.0 );

    lua_getglobal(L, "v2");
    CHECK( lua_tonumber(L, -1) == 2.0 );

    lua_getglobal(L, "v3");
    CHECK( lua_tonumber(L, -1) == 3.0 );

    lua_getglobal(L, "v4");
    CHECK( lua_isnil(L, -1) );

    lua_close(L);

}