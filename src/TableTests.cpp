/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#include "Test.h"
#include "LuaTest.h"

TEST(ArrayRemove)
{

    const char* code =
        "t = { 'one', 'two', 'three', 'four', 'five' }\n"
        "t[4] = nil";

    lua_State* L = luaL_newstate();

    CHECK( DoString(L, code) );

    lua_getglobal(L, "t");
    int table = lua_gettop(L);

    lua_rawgeti(L, table, 1);
    CHECK_EQ( lua_tostring(L, -1), "one" );

    lua_rawgeti(L, table, 2);
    CHECK_EQ( lua_tostring(L, -1), "two" );

    lua_rawgeti(L, table, 3);
    CHECK_EQ( lua_tostring(L, -1), "three" );

    lua_rawgeti(L, table, 4);
    CHECK( lua_isnil(L, -1) );

    lua_rawgeti(L, table, 5);
    CHECK_EQ( lua_tostring(L, -1), "five" );

    lua_close(L);

}