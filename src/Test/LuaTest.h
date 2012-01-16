/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#ifndef ROCKETVM_LUA_TEST_H
#define ROCKETVM_LUA_TEST_H

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

/**
 * A fixture that creates and destroys a lua_State with the libraries opened.
 */
struct LuaFixture
{
    LuaFixture();
    ~LuaFixture();
    lua_State*  L;
};

bool DoString(lua_State* L, const char* string);

#endif
