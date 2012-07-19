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
#include "State.h"
#include "Table.h"

#include <stdlib.h>

UserData* UserData_Create(lua_State* L, size_t size, Table* env)
{
    ASSERT(env != NULL);

    UserData* userData = static_cast<UserData*>(Gc_AllocateObject(L, LUA_TUSERDATA, sizeof(UserData) + size));
    userData->size      = size;
    userData->metatable = NULL;
    userData->env       = env;

    Gc* gc = &L->gc;
    Gc_IncrementReference(gc, userData, userData->env);

    return userData;
}

void UserData_Destroy(lua_State* L, UserData* userData, bool releaseRefs)
{
    if (releaseRefs)
    {
        Gc* gc = &L->gc;
        if (userData->metatable != NULL)
        {
            Gc_DecrementReference(L, gc, userData->metatable);
        }
        Gc_DecrementReference(L, gc, userData->env);
    }
    Free(L, userData, sizeof(UserData) + userData->size);
}