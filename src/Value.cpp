/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */

#include "Value.h"
#include "Function.h"
#include "UserData.h"
#include "Table.h"

#include <assert.h>

int Value_SetEnv(lua_State* L, Value* value, Table* table)
{
    switch (value->tag)
    {
    case TAG_FUNCTION:
        value->closure->env = table;
        Gc_WriteBarrier(L, value->closure, table);
        return 1;
    case TAG_THREAD:
        // TODO: implement.
        assert(0);
        return 1;
    case TAG_USERDATA:
        value->userData->env = table;
        Gc_WriteBarrier(L, value->userData, table);
        return 1;
    }
    return 0;
}

Table* Value_GetEnv(const Value* value)
{
    switch (value->tag)
    {
    case TAG_FUNCTION:
        return value->closure->env;
    case TAG_THREAD:
        // TODO: implement.
        assert(0);
        return 0;
    case TAG_USERDATA:
        return value->userData->env;
    }
    return 0;
}