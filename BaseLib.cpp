/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */
#include "lua.h"
#include "lauxlib.h"

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

int BaseLib_Assert(lua_State* L)
{
    if (!lua_toboolean(L, 1))
    {
        const char* message = lua_tostring(L, 2);
        if (message == NULL)
        {
            lua_pushstring(L, "assertion failed!");
        }
        lua_error(L);
    }
    return 0;
}

int BaseLib_Next(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    // Set the 2nd argument to nil if it wasn't supplied.
    lua_settop(L, 2);
    if (lua_next(L, 1))
    {
        return 2;
    }
    lua_pushnil(L);
    return 1;
}

int BaseLib_Pairs(lua_State* L)
{   
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushvalue(L, lua_upvalueindex(1)); // "next" function
    lua_pushvalue(L, 1);    // State
    lua_pushnil(L);         // Initial value
    return 3;
}

int BaseLib_Print(lua_State* L)
{

    int numArgs = lua_gettop(L);

    // Get the tostring function, in case it has been overwritten.
    lua_getglobal(L, "tostring");

    for (int i = 1; i <= numArgs; ++i)
    {

        lua_pushvalue(L, -1);  // tostring function
        lua_pushvalue(L, i);   // value to print
        lua_call(L, 1, 1);
        
        const char* string = lua_tostring(L, -1);
        if (string == NULL)
        {
            return luaL_error(L, "\"tostring\" must return a string to \"print\"");
        }
        
        if (i > 1)
        {
            fputc('\t', stdout);
        }

        fputs(string, stdout);
        lua_pop(L, 1);

    }
    fputc('\n', stdout);
    lua_pop(L, 1);
    return 0;
}

int BaseLib_SetMetatable(lua_State* L)
{
    int t = lua_type(L, 2);
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_argcheck(L, t == LUA_TNIL || t == LUA_TTABLE, 2, "nil or table expected");
    if (luaL_getmetafield(L, 1, "__metatable"))
    {
        luaL_error(L, "cannot change a protected metatable");
    }
    lua_settop(L, 2);
    lua_setmetatable(L, 1);
    return 1;
}

int BaseLib_GetMetatable(lua_State* L)
{
    luaL_checkany(L, 1);
    if (!lua_getmetatable(L, 1))
    {
        lua_pushnil(L);
        return 1;
    }
    // Returns either __metatable field (if present) or metatable.
    luaL_getmetafield(L, 1, "__metatable");
    return 1;
}

int BaseLib_Type(lua_State* L)
{
    int type = lua_type(L, 1);
    // TODO: This can be done more efficiently by using lower level operations
    // so that we can just reuse the String value rather than having to rehash.
    lua_pushstring( L, lua_typename(L, type) );
    return 1;
}

int BaseLib_ToNumber(lua_State* L)
{
    int base = luaL_optint(L, 2, 10);
    if (base == 10)
    {  
        // Standard conversion.
        luaL_checkany(L, 1);
        if (lua_isnumber(L, 1))
        {
            lua_pushnumber(L, lua_tonumber(L, 1));
            return 1;
        }
    }
    else
    {
        const char *s1 = luaL_checkstring(L, 1);
        luaL_argcheck(L, 2 <= base && base <= 36, 2, "base out of range");
        char *s2;
        unsigned long n = strtoul(s1, &s2, base);
        // At least one valid digit?
        if (s1 != s2)
        {  
            // Skip trailing spaces
            while (isspace((unsigned char)(*s2)))
            {
                s2++;
            }
            // No invalid trailing characters?
            if (*s2 == '\0')
            {
                lua_pushnumber(L, (lua_Number)n);
                return 1;
            }
        }
    }
    // Otherwise not a number
    lua_pushnil(L);
    return 1;
}

int BaseLib_ToString(lua_State* L)
{
    luaL_checkany(L, 1);
    /*
    if (luaL_callmeta(L, 1, "__tostring"))
    {
        return 1;
    }
    */
    switch (lua_type(L, 1)) {
    case LUA_TNUMBER:
        lua_pushstring(L, lua_tostring(L, 1));
        break;
    case LUA_TSTRING:
        lua_pushvalue(L, 1);
        break;
    case LUA_TBOOLEAN:
        lua_pushstring(L, (lua_toboolean(L, 1) ? "true" : "false"));
        break;
    case LUA_TNIL:
        lua_pushliteral(L, "nil");
        break;
    default:
        lua_pushfstring(L, "%s: %p", luaL_typename(L, 1), lua_topointer(L, 1));
        break;
    }
    return 1;
}

int BaseLib_Unpack(lua_State* L)
{
 
    luaL_checktype(L, 1, LUA_TTABLE);
    int i = luaL_optint(L, 2, 1);
    int e = luaL_opt(L, luaL_checkint, 3, static_cast<int>(lua_objlen(L, 1)));

    if (i > e)
    {
        // Empty range.
        return 0;
    }

    // Number of elements.
    int num = e - i + 1;
    if (num <= 0 || !lua_checkstack(L, num))
    {
        // n <= 0 means arith. overflow.
        return luaL_error(L, "too many results to unpack");
    }

    // push arg[i] (avoiding overflow problems).
    lua_rawgeti(L, 1, i);
    while (i++ < e)
    {   
        // push arg[i + 1...e] 
        lua_rawgeti(L, 1, i);
    }

    return num;

}

int BaseLib_Error(lua_State* L)
{
    int level = luaL_optint(L, 2, 1);
    lua_settop(L, 1);
    if (lua_isstring(L, 1) && level > 0)
    {
        // Add extra information
        luaL_where(L, level);
        lua_pushvalue(L, 1);
        lua_concat(L, 2);
    }
    return lua_error(L);
}

int BaseLib_PCall(lua_State* L)
{
    luaL_checkany(L, 1);
    int status = lua_pcall(L, lua_gettop(L) - 1, LUA_MULTRET, 0);
    lua_pushboolean(L, (status == 0));
    lua_insert(L, 1);
    // Return status + all results
    return lua_gettop(L);
}

int BaseLib_CollectGarbage(lua_State* L)
{
    const char* what = luaL_checkstring(L, 1);
    int data = luaL_optint(L, 2, 9);
    
    if (strcmp(what, "stop") == 0)
    {
        lua_gc(L, LUA_GCSTOP, data);
    }
    else if (strcmp(what, "restart") == 0)
    {
        lua_gc(L, LUA_GCRESTART, data);
    }
    else if (strcmp(what, "collect") == 0)
    {
        lua_gc(L, LUA_GCCOLLECT, data);
    }
    else if (strcmp(what, "count") == 0)
    {
        lua_pushinteger( L, lua_gc(L, LUA_GCCOUNT, data) );
        return 1;
    }
    else if (strcmp(what, "step") == 0)
    {
        lua_pushboolean( L, lua_gc(L, LUA_GCSTEP, data) ? 1 : 0 );
        return 1;
    }
    else if (strcmp(what, "setpause") == 0)
    {
        lua_pushinteger( L, lua_gc(L, LUA_GCSETPAUSE, data) );
        return 1;
    }
    else if (strcmp(what, "setstepmul") == 0)
    {
        lua_pushinteger( L, lua_gc(L, LUA_GCSETSTEPMUL, data) );
        return 1;
    }
    return 0;

}

void OpenBaseLib(lua_State* L)
{
    
    // TODO: baselib functions
    // dofile
    // getfenv
    // ipairs
    // load
    // loadfile
    // loadstring
    // module
    // rawequal
    // rawget
    // rawset
    // require
    // select
    // setfenv
    // xpcall

    static const luaL_Reg functions[] =
        {
            { "assert",         BaseLib_Assert          },
            { "collectgarbage", BaseLib_CollectGarbage  },
            { "error",          BaseLib_Error           },
            { "getmetatable",   BaseLib_GetMetatable    },
            { "next",           BaseLib_Next            },
            { "pcall",          BaseLib_PCall           },
            { "print",          BaseLib_Print           },
            { "setmetatable",   BaseLib_SetMetatable    },
            { "type",           BaseLib_Type            },
            { "tonumber",       BaseLib_ToNumber        },
            { "tostring",       BaseLib_ToString        },
            { "unpack",         BaseLib_Unpack          },
            { NULL, NULL }
        };

    // Register these separately, since we use up values to provide fast access
    // to the "next" function.
    lua_pushcfunction( L, BaseLib_Next );
    lua_pushcclosure( L, BaseLib_Pairs, 1 );
    lua_setglobal(L, "pairs");

    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_setglobal(L, "_G");
  
    luaL_register(L, "_G", functions);
    
    lua_pushliteral(L, LUA_VERSION);
    lua_setglobal(L, "_VERSION");

}

int luaopen_base(lua_State *L)
{
    OpenBaseLib(L);
    //luaL_register(L, LUA_COLIBNAME, co_funcs);
    return 0;
}