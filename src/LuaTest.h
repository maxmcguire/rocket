/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */

#ifndef ROCKETVM_LUA_TEST_H
#define ROCKETVM_LUA_TEST_H

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

bool DoString(lua_State* L, const char* string);

#endif
