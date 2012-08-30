/*
 * RocketVM
 * Copyright (c) 2011-2012 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#include "Test.h"
#include "LuaTest.h"

TEST_FIXTURE(DebugGetInfoLineNumber, LuaFixture)
{

    const char* code =
        "\n"
        "\n"
        "line1 = debug.getinfo(1,'l').currentline\n";

    luaL_openlibs(L);

    CHECK( DoString(L, code) );

    lua_getglobal(L, "line1");
    CHECK_EQ( lua_tonumber(L, -1), 3 );
    
}