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