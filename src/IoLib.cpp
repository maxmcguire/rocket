/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include <stdio.h>

static int Write(lua_State* L, FILE* file)
{
    int numArgs = lua_gettop(L);

    for (int i = 0; i < numArgs; ++i)
    {
        int arg = i + 1;
        if (lua_type(L, arg) == LUA_TNUMBER)
        {   
            // Optimization: could be done exactly as for strings.
            fprintf(file, LUA_NUMBER_FMT, lua_tonumber(L, arg));
        }
        else
        {
            size_t length;
            const char* string = luaL_checklstring(L, arg, &length);
            fwrite(string, sizeof(char), length, file);
        }
    }
    return 0;
}

static int IoLib_Write(lua_State* L)
{
    return Write(L, stdout);
}

int luaopen_io(lua_State *L)
{

    static const luaL_Reg functions[] =
        {
            { "write", IoLib_Write },
            { NULL, NULL }
        };

    luaL_register(L, LUA_IOLIBNAME, functions);
    return 1;

}