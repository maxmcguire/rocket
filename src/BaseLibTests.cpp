/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#include "Test.h"
#include "LuaTest.h"

TEST_FIXTURE(IPairs, LuaFixture)
{

    const char* code =
        "local t = { 'one', 'two', 'three', 'four', 'five' }\n"
        "t[4] = nil\n"
        "g, s, i = ipairs(t)\n"
        "k1, v1 = g(s, i)\n"
        "k2, v2 = g(s, k1)\n"
        "k3, v3 = g(s, k2)\n"
        "k4 = g(s, k3)";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "k1");
    CHECK( lua_tonumber(L, -1) == 1.0 );
    lua_getglobal(L, "v1");
    CHECK_EQ( lua_tostring(L, -1), "one" );

    lua_getglobal(L, "k2");
    CHECK( lua_tonumber(L, -1) == 2.0 );
    lua_getglobal(L, "v2");
    CHECK_EQ( lua_tostring(L, -1), "two" );

    lua_getglobal(L, "k3");
    CHECK( lua_tonumber(L, -1) == 3.0 );
    lua_getglobal(L, "v3");
    CHECK_EQ( lua_tostring(L, -1), "three" );

    lua_getglobal(L, "k4");
    CHECK( lua_isnil(L, -1) );

}