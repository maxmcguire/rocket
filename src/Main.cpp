#include "Test.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <stdio.h>
#include <malloc.h>

extern "C" void clear();

int main(int argc, char* argv[])
{

    Test_RunTests("ConcatOperator");

    /*
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    int result = luaL_dofile(L, "Test.lua");

    if (result != 0)
    {
        const char* message = lua_tostring(L, -1);
        if (message == NULL)
        {
            message = "an error occurred";
        }
        fputs(message, stderr);
        fputc('\n', stderr);
    }

    lua_close(L);
    */

    return 0;

}