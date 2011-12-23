/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include <assert.h>
#include <stdlib.h>
#include <math.h>

static int MathLib_Abs(lua_State* L)
{   
    lua_Number n = luaL_checknumber(L, 1);    
    lua_pushnumber( L, abs(n) );
    return 1;
}

static int MathLib_Random(lua_State *L)
{
    // the '%' avoids the (rare) case of r==1, and is needed also because on
    // some systems (SunOS!) 'rand()' may return a value larger than RAND_MAX   
    lua_Number r = (lua_Number)(rand()%RAND_MAX) / (lua_Number)RAND_MAX;
    // Check number of arguments
    switch (lua_gettop(L))
    {
    case 0:
        {
            // No argument specified; number between 0 and 1
            lua_pushnumber(L, r);
        }
        break;
    case 1:
        {  
            // Only upper limit specified.
            int u = luaL_checkint(L, 1);
            luaL_argcheck(L, 1<=u, 1, "interval is empty");
            lua_pushnumber(L, floor(r*u)+1);
        }
        break;
    case 2:
        {  
            // Lower and upper limits specified.
            int l = luaL_checkint(L, 1);
            int u = luaL_checkint(L, 2);
            luaL_argcheck(L, l<=u, 2, "interval is empty");
            lua_pushnumber(L, floor(r*(u-l+1))+l);
        }
        break;
    default:
        return luaL_error(L, "wrong number of arguments");
    }
    return 1;
}


LUALIB_API int luaopen_math(lua_State *L)
{

    static const luaL_Reg functions[] =
        {
            { "abs",    MathLib_Abs },
            { "random", MathLib_Random },
            { NULL, NULL }
        };

    luaL_register(L, LUA_MATHLIBNAME, functions);
    return 1;

}