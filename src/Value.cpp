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
        if (table != NULL)
        {
            Gc_IncrementReference(&L->gc, value->table, table);
        }
        if (value->table->metatable != NULL)
        {
            Gc_DecrementReference(L, &L->gc, value->table->metatable);
        }
        value->table->metatable = table;
        break;
    case Tag_Userdata:
        if (table != NULL)
        {
            Gc_IncrementReference(&L->gc, value->userData, table);
        }
        if (value->userData->metatable != NULL)
        {
            Gc_DecrementReference(L, &L->gc, value->userData->metatable);
        }
        value->userData->metatable = table;
         break;
    default:
        {
            // Set the global metatable for the type.
            int type = Value_GetType(value);
            ASSERT(type >= 0 && type < NUM_TYPES );
            L->metatable[type] = table;
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
    case Tag_Closure:
        Gc_IncrementReference(&L->gc, value->closure, table);
        Gc_DecrementReference(L, &L->gc, value->closure->env);
        value->closure->env = table;
        return 1;
    case Tag_Thread:
        // TODO: implement.
        ASSERT(0);
        return 1;
    case Tag_Userdata:
        Gc_IncrementReference(&L->gc, value->userData, table);
        Gc_DecrementReference(L, &L->gc, value->userData->env);
        value->userData->env = table;
        return 1;
    }
    return 0;
}

Table* Value_GetEnv(const Value* value)
{
    switch (value->tag)
    {
    case Tag_Closure:
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
