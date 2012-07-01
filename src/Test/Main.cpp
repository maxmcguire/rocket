/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#include "Test.h"

extern "C"
{
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

#include <string.h>

int main(int argc, char* argv[])
{
    const char* pattern = 0;
    if (argc > 1)
    {
        pattern = argv[1];
    }
    if (pattern != NULL && strchr(pattern, '.') != NULL)
    {
        lua_State* L = luaL_newstate();
        luaL_openlibs(L);

        // Create the argument table with the remaining command line arguments.
        lua_createtable(L, argc - 2, 0);
        for (int i = 2; i < argc; ++i)
        {
            lua_pushstring(L, argv[i]);
            lua_rawseti(L, -2, i - 2 + 1);
        }
        lua_setglobal(L, "arg");

        if (luaL_dofile(L, pattern) != 0)
        {
            fputs( lua_tostring(L, -1), stderr );
            fputc( '\n', stderr );
        }
        lua_close(L);
    }
    else
    {
        Test_RunTests(pattern);
    }
    return 0;
} 