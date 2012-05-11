/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

extern "C"
{
#include "lua.h"
}

#include "Global.h"
#include "UserData.h"

#include <stdlib.h>

UserData* UserData_Create(lua_State* L, size_t size, Table* env)
{
    ASSERT(env != NULL);

    UserData* userData = static_cast<UserData*>(Gc_AllocateObject(L, LUA_TUSERDATA, sizeof(UserData) + size));
    userData->size      = size;
    userData->metatable = NULL;
    userData->env       = env;

    return userData;
}