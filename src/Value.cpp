/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#include "Value.h"
#include "Function.h"
#include "UserData.h"
#include "Table.h"

#include <ctype.h>

void Value_SetMetatable(lua_State* L, Value* value, Table* table)
{
    switch (value->tag)
    {
    case Tag_Table:
        value->table->metatable = table;
        if (table != NULL)
        {
            Gc_WriteBarrier(L, value->table, table);
        }
        break;
    case Tag_Userdata:
        value->userData->metatable = table;
        if (table != NULL)
        {
            Gc_WriteBarrier(L, value->userData, table);
        }
        break;
    default:
        {
            // Set the global metatable for the type.
            int type = Value_GetType(value);
            ASSERT(type >= 0 && type < NUM_TYPES );
            L->metatable[type] = table;
            // TODO: Gc_WriteBarrier?
        }
        break;
    }
}

Table* Value_GetMetatable(lua_State* L, const Value* value)
{
    switch (value->tag)
    {
    case Tag_Table:
        return value->table->metatable;
    case Tag_Userdata:
        return value->userData->metatable;
    }
    // Get the global metatable for the type.
    int type = Value_GetType(value);
    ASSERT(type >= 0 && type < NUM_TYPES );
    return L->metatable[type];
}

int Value_SetEnv(lua_State* L, Value* value, Table* table)
{
    switch (value->tag)
    {
    case Tag_Function:
        value->closure->env = table;
        Gc_WriteBarrier(L, value->closure, table);
        return 1;
    case Tag_Thread:
        // TODO: implement.
        ASSERT(0);
        return 1;
    case Tag_Userdata:
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
    case Tag_Function:
        return value->closure->env;
    case Tag_Thread:
        // TODO: implement.
        ASSERT(0);
        return 0;
    case Tag_Userdata:
        return value->userData->env;
    }
    return 0;
}

bool StringToNumber(const char* string, lua_Number* result)
{

    char* end;
    *result = lua_str2number(string, &end);

    if (end == string)
    {
        // The conversion failed.
        return false;
    }
    if (*end == 'x' || *end == 'X')
    {
        // Try converting as a hexadecimal number.
        *result = strtoul(string, &end, 16);
    }
    // Allow trailing spaces.
    while (isspace(*end))
    {
        ++end;
    }
    if (*end == '\0')
    {
        // Converted the entire string.
        return true;
    }

    return false;

}
