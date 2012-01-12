/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */

#include "Test.h"
#include "LuaTest.h"

#include <memory.h>

TEST(StringUpper)
{

    // Tests the string.upper library function.

    lua_State* L = luaL_newstate();

    luaopen_string(L);

    lua_getglobal(L, "string");
    CHECK( !lua_isnil(L, -1) );
    lua_getfield(L, -1, "upper");
    CHECK( lua_isfunction(L, -1) );
    int upper = lua_gettop(L);

    // Check that lowercase letters are changed to upper case.
    lua_pushvalue(L, upper);
    lua_pushstring(L, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890");
    CHECK( lua_pcall(L, 1, 1, 0) == 0);
    CHECK_EQ( lua_tostring(L, -1), "ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890" );

    // Check that embedded 0s are handled.
    lua_pushvalue(L, upper);
    lua_pushlstring(L, "abc\0def", 7);
    CHECK( lua_pcall(L, 1, 1, 0) == 0);
    size_t length;
    const char* s = lua_tolstring(L, -1, &length);
    CHECK( length == 7 );
    CHECK( memcmp("ABC\0DEF", s, length) == 0 );
    
    lua_close(L);

}

TEST(StringLower)
{

    // Tests the string.lower library function.

    lua_State* L = luaL_newstate();

    luaopen_string(L);

    lua_getglobal(L, "string");
    CHECK( !lua_isnil(L, -1) );
    lua_getfield(L, -1, "lower");
    CHECK( lua_isfunction(L, -1) );
    int lower = lua_gettop(L);

    // Check that uppercase letters are changed to lower case.
    lua_pushvalue(L, lower);
    lua_pushstring(L, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890");
    CHECK( lua_pcall(L, 1, 1, 0) == 0);
    CHECK_EQ( lua_tostring(L, -1), "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz1234567890" );

    // Check that embedded 0s are handled.
    lua_pushvalue(L, lower);
    lua_pushlstring(L, "ABC\0DEF", 7);
    CHECK( lua_pcall(L, 1, 1, 0) == 0);
    size_t length;
    const char* s = lua_tolstring(L, -1, &length);
    CHECK( length == 7 );
    CHECK( memcmp("abc\0def", s, length) == 0 );
    
    lua_close(L);

}