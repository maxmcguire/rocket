/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */

#include "lua.h"
#include "UserData.h"

#include <stdlib.h>

UserData* UserData_Create(lua_State* L, size_t size)
{
    UserData* userData = static_cast<UserData*>(Gc_AllocateObject(L, LUA_TUSERDATA, sizeof(UserData) + size));
    userData->size = size;
    userData->env  = NULL;
    return userData;
}