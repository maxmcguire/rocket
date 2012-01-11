/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */

#include "Test.h"
#include "LuaTest.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static size_t GetTotalBytes(lua_State* L)
{
    return lua_gc(L, LUA_GCCOUNT, 0) * 1024 + lua_gc(L, LUA_GCCOUNTB, 0);
}

// Currently disabled while the GC is being developed.
/*
TEST(GcTest)
{
    
    lua_State* L = luaL_newstate();

    lua_pushstring(L, "garbage string");
    lua_pop(L, 1);

    lua_gc(L, LUA_GCCOLLECT, 0);
    size_t bytes1 = GetTotalBytes(L);
    CHECK(bytes1 > 0);

    // Create a piece of garbage.
    int table = lua_gettop(L);
    lua_newtable(L);
    lua_pop(L, 1);

    // Check that the garbage is cleaned up.
    lua_gc(L, LUA_GCCOLLECT, 0);
    size_t bytes2 = GetTotalBytes(L);
    CHECK( bytes2 <= bytes1 );

    lua_close(L);

}
*/

TEST(ToCFunction)
{

    struct Locals
    {
        static int F(lua_State* L)
        {
            return 0;
        }
    };

    lua_State* L = luaL_newstate();

    lua_pushcfunction(L, Locals::F);
    CHECK( lua_tocfunction(L, -1) == Locals::F );

    const char* code = "function f() end\n";
    CHECK( DoString(L, code) );

    lua_getglobal(L, "f");
    CHECK( lua_tocfunction(L, -1) == NULL );

    lua_pushstring(L, "test");
    CHECK( lua_tocfunction(L, -1) == NULL );

    lua_pushnumber(L, 1.0);
    CHECK( lua_tocfunction(L, -1) == NULL );

    lua_close(L);

}

TEST(ConcatTest)
{

    lua_State* L = luaL_newstate();

    int top = lua_gettop(L);

    lua_pushstring(L, "Hello ");
    lua_pushnumber(L, 5.0);
    lua_pushstring(L, " goodbye");
    lua_concat(L, 3);

    const char* result = lua_tostring(L, -1);
    CHECK( strcmp(result, "Hello 5 goodbye") == 0 );
    CHECK( lua_gettop(L) - top == 1 );

    lua_close(L);

}

TEST(InsertTest)
{

    lua_State* L = luaL_newstate();

    int top = lua_gettop(L);

    lua_pushinteger(L, 1);
    lua_pushinteger(L, 3);
    lua_pushinteger(L, 2);
    lua_insert(L, -2);

    CHECK( lua_tointeger(L, -3) == 1 );
    CHECK( lua_tointeger(L, -2) == 2 );
    CHECK( lua_tointeger(L, -1) == 3 );
    
    CHECK( lua_gettop(L) - top == 3 );

    lua_close(L);

}

TEST(Replace)
{

    lua_State* L = luaL_newstate();

    int top = lua_gettop(L);

    lua_pushinteger(L, 1);
    lua_pushinteger(L, 3);
    lua_pushinteger(L, 2);
    lua_replace(L, -3);

    CHECK( lua_tointeger(L, -2) == 2 );
    CHECK( lua_tointeger(L, -1) == 3 );
    
    CHECK( lua_gettop(L) - top == 2 );

    lua_close(L);

}

TEST(RawEqual)
{

    lua_State* L = luaL_newstate();

    lua_pushinteger(L, 1);
    lua_pushinteger(L, 3);
    CHECK( lua_rawequal(L, -1, -2) == 0 );
    lua_pop(L, 2);

    lua_pushinteger(L, 1);
    lua_pushinteger(L, 1);
    CHECK( lua_rawequal(L, -1, -2) == 1 );
    lua_pop(L, 2);

    lua_pushstring(L, "test1");
    lua_pushstring(L, "test2");
    CHECK( lua_rawequal(L, -1, -2) == 0 );
    lua_pop(L, 2);

    lua_pushstring(L, "test1");
    lua_pushstring(L, "test1");
    CHECK( lua_rawequal(L, -1, -2) == 1 );
    lua_pop(L, 2);

    lua_pushvalue(L, LUA_GLOBALSINDEX);
    CHECK( lua_rawequal(L, lua_gettop(L), LUA_GLOBALSINDEX) == 1 );
    lua_pop(L, 2);

    lua_pushvalue(L, LUA_REGISTRYINDEX);
    CHECK( lua_rawequal(L, LUA_REGISTRYINDEX, lua_gettop(L)) == 1 );
    lua_pop(L, 2);

    lua_close(L);

}

TEST(Less)
{

    lua_State* L = luaL_newstate();

    lua_pushinteger(L, 1);
    lua_pushinteger(L, 3);
    CHECK( lua_lessthan(L, -2, -1) == 1 );
    lua_pop(L, 2);

    lua_pushinteger(L, 3);
    lua_pushinteger(L, 1);
    CHECK( lua_lessthan(L, -2, -1) == 0 );
    lua_pop(L, 2);

    lua_pushinteger(L, 3);
    lua_pushinteger(L, 3);
    CHECK( lua_lessthan(L, -2, -1) == 0 );
    lua_pop(L, 2);

    // TODO: Test metamethods.

    lua_close(L);

}

TEST(PCallTest)
{

    struct Locals
    {
        static int ErrorFunction(lua_State* L)
        {
            lua_pushstring(L, "Error message");
            lua_error(L);
            return 0;
        }
    };

    lua_State* L = luaL_newstate();

    lua_pushcfunction(L, Locals::ErrorFunction);
    CHECK( lua_pcall(L, 0, 0, 0) == LUA_ERRRUN );
    CHECK( strcmp( lua_tostring(L, -1), "Error message") == 0 );

    lua_close(L);

}

TEST(ErrorRestore)
{

    struct Locals
    {
        static int ErrorFunction(lua_State* L)
        {
            lua_pushnumber(L, 3.0);
            lua_pushnumber(L, 4.0);
            lua_pushnumber(L, 5.0);
            lua_pushstring(L, "Error message");
            lua_error(L);
            return 0;
        }
    };

    lua_State* L = luaL_newstate();

    lua_pushstring(L, "test");

    int top = lua_gettop(L);
    lua_pushcfunction(L, Locals::ErrorFunction);
    lua_pushnumber(L, 1.0);
    lua_pushnumber(L, 2.0);
    CHECK( lua_pcall(L, 2, 0, 0) == LUA_ERRRUN );
    CHECK( lua_gettop(L) - top == 1 );

    CHECK( lua_isstring(L, -1) );
    CHECK( strcmp( lua_tostring(L, -1), "Error message") == 0 );
    
    // Check that the stack is in the correct state.
    CHECK( lua_isstring(L, -2) );
    CHECK( strcmp( lua_tostring(L, -2), "test") == 0);

    lua_close(L);

}

TEST(ErrorHandler)
{

    // Test an error handler for lua_pcall.

    struct Locals
    {
        static int ErrorFunction(lua_State* L)
        {
            lua_pushnumber(L, 3);
            lua_pushnumber(L, 4);
            lua_pushstring(L, "Error message");
            lua_error(L);
            return 0;
        }
        static int ErrorHandler(lua_State* L)
        {
            const char* msg = lua_tostring(L, 1);
            if (msg != NULL && strcmp(msg, "Error message") == 0)
            {
                lua_pushstring(L, "Error handler");
            }
            return 1;
        }
    };

    lua_State* L = luaL_newstate();

    lua_pushcfunction(L, Locals::ErrorHandler );
    int err = lua_gettop(L);

    int top = lua_gettop(L);
    lua_pushcfunction(L, Locals::ErrorFunction);
    lua_pushnumber(L, 1);
    lua_pushnumber(L, 2);
    CHECK( lua_pcall(L, 2, 0, err) == LUA_ERRRUN );
    CHECK( strcmp( lua_tostring(L, -1), "Error handler") == 0 );
    CHECK( lua_gettop(L) - top == 1 );

    lua_close(L);

}

TEST(ErrorHandlerError)
{

    // Test generating an error from inside the error handler for lua_pcall.

    struct Locals
    {
        static int ErrorFunction(lua_State* L)
        {
            lua_pushnumber(L, 3);
            lua_pushnumber(L, 4);
            lua_pushstring(L, "Error message");
            lua_error(L);
            return 0;
        }
        static int ErrorHandler(lua_State* L)
        {
            lua_pushnumber(L, 5);
            lua_pushnumber(L, 6);
            lua_pushstring(L, "Error handler error");
            lua_error(L);
            return 1;
        }
    };

    lua_State* L = luaL_newstate();

    lua_pushcfunction(L, Locals::ErrorHandler );
    int err = lua_gettop(L);

    int top = lua_gettop(L);
    lua_pushcfunction(L, Locals::ErrorFunction);
    lua_pushnumber(L, 1);
    lua_pushnumber(L, 2);
    CHECK( lua_pcall(L, 2, 0, err) == LUA_ERRERR );
    CHECK( lua_isstring(L, -1) );
    CHECK( lua_gettop(L) - top == 1 );

    lua_close(L);

}

TEST(GetTable)
{

    lua_State* L = luaL_newstate();

    lua_newtable(L);
    int table = lua_gettop(L);

    lua_pushstring(L, "key");
    lua_pushstring(L, "value");
    lua_settable(L, table);

    int top = lua_gettop(L);
    lua_pushstring(L, "key");
    lua_gettable(L, table);
    CHECK_EQ( lua_tostring(L, -1), "value" );
    CHECK( lua_gettop(L) - top == 1 );

    top = lua_gettop(L);
    lua_pushstring(L, "dummy");
    lua_gettable(L, table);
    CHECK( lua_isnil(L, -1) );
    CHECK( lua_gettop(L) - top == 1 );

    lua_close(L);

}

TEST(GetTableMetamethod)
{

    struct Locals
    {
        static int Index(lua_State* L)
        {
            Locals* locals = static_cast<Locals*>(lua_touserdata(L, lua_upvalueindex(1)));
            ++locals->calls;
            const char* key = lua_tostring(L, 2);
            if (key != NULL && strcmp(key, "key") == 0)
            {
                locals->success = true;
            }
            lua_pushstring(L, "value");
            return 1;
        }
        int  calls;
        bool success;
    };

    Locals locals;
    locals.success = false;
    locals.calls   = 0;

    lua_State* L = luaL_newstate();

    lua_newtable(L);
    int table = lua_gettop(L);

    // Setup a metatable for the table.
    lua_newtable(L);
    lua_pushlightuserdata(L, &locals);
    lua_pushcclosure(L, Locals::Index, 1);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, table);

    lua_pushstring(L, "key");
    lua_gettable(L, table);
    CHECK_EQ( lua_tostring(L, -1), "value" );
    CHECK( locals.success );
    CHECK( locals.calls == 1 );

    lua_close(L);

}

TEST(CallMetamethod)
{
    
    // Test the __call metamethod.

    struct Locals
    {
        static int Call(lua_State* L)
        {
            Locals* locals = static_cast<Locals*>(lua_touserdata(L, lua_upvalueindex(1)));
            if (lua_gettop(L) == 3
                && lua_touserdata(L, 1) == locals->userData
                && lua_tonumber(L, 2) == 1.0
                && lua_tonumber(L, 3) == 2.0)
            {
                locals->success = true;
            }
            lua_pushstring(L, "result");
            return 1;
        }
        bool  success;
        void* userData;
    };

    lua_State* L = luaL_newstate();

    Locals locals;
    locals.success = false;
    locals.userData = lua_newuserdata(L, 10);
    int object = lua_gettop(L);

    lua_newtable(L);
    int mt = lua_gettop(L);

    lua_pushlightuserdata(L, &locals);
    lua_pushcclosure(L, Locals::Call, 1);
    lua_setfield(L, mt, "__call");

    CHECK( lua_setmetatable(L, object) == 1 );

    lua_pushnumber(L, 1.0);
    lua_pushnumber(L, 2.0);
    CHECK( lua_pcall(L, 2, 1, 0) == 0 );
    CHECK( locals.success );
    CHECK_EQ( lua_tostring(L, -1), "result" );    

    lua_close(L);

}


TEST(RawGetITest)
{

    lua_State* L = luaL_newstate();

    lua_newtable(L);
    int table = lua_gettop(L);

    lua_pushstring(L, "extra");
    lua_pushstring(L, "extra");
    lua_settable(L, table);

    lua_pushinteger(L, 1);
    lua_pushstring(L, "one");
    lua_settable(L, table);

    lua_rawgeti(L, table, 1);
    CHECK( strcmp( lua_tostring(L, -1), "one") == 0 );

    lua_close(L);

}

TEST(RawGetTest)
{

    lua_State* L = luaL_newstate();

    lua_newtable(L);
    int table = lua_gettop(L);

    lua_pushinteger(L, 1);
    lua_pushstring(L, "one");
    lua_settable(L, table);

    int top = lua_gettop(L);
    lua_pushinteger(L, 1);
    lua_rawget(L, table);
    CHECK_EQ( lua_tostring(L, -1), "one" );
    CHECK( lua_gettop(L) - top == 1 );

    lua_close(L);

}

TEST(RawSetTest)
{

    struct Locals
    {
        static int Error(lua_State* L)
        {
            Locals* locals = static_cast<Locals*>(lua_touserdata(L, lua_upvalueindex(1)));
            locals->called = true;
            return 0;
        }
        bool called;
    };

    Locals locals;
    locals.called = false;

    lua_State* L = luaL_newstate();

    lua_newtable(L);
    int table = lua_gettop(L);

    // Setup a metatable for the table.
    lua_newtable(L);
    lua_pushlightuserdata(L, &locals);
    lua_pushcclosure(L, Locals::Error, 1);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, table);

    lua_pushstring(L, "one");
    lua_pushnumber(L, 1.0);
    lua_rawset(L, table);

    lua_pushstring(L, "two");
    lua_pushnumber(L, 2.0);
    lua_rawset(L, table);

    lua_pushstring(L, "one");
    lua_rawget(L, table);
    CHECK( lua_tonumber(L, -1) == 1.0 );

    lua_pushstring(L, "two");
    lua_rawget(L, table);
    CHECK( lua_tonumber(L, -1) == 2.0 );

    lua_close(L);

}

TEST(RawSetITest)
{

    struct Locals
    {
        static int Error(lua_State* L)
        {
            Locals* locals = static_cast<Locals*>(lua_touserdata(L, lua_upvalueindex(1)));
            locals->called = true;
            return 0;
        }
        bool called;
    };

    Locals locals;
    locals.called = false;

    lua_State* L = luaL_newstate();

    lua_newtable(L);
    int table = lua_gettop(L);

    // Setup a metatable for the table.
    lua_newtable(L);
    lua_pushlightuserdata(L, &locals);
    lua_pushcclosure(L, Locals::Error, 1);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, table);

    lua_pushstring(L, "one");
    lua_rawseti(L, table, 1);

    lua_pushstring(L, "three");
    lua_rawseti(L, table, 3);

    lua_rawgeti(L, table, 1);
    CHECK_EQ( lua_tostring(L, -1), "one" );

    lua_rawgeti(L, table, 2);
    CHECK( lua_isnil(L, -1) );

    lua_rawgeti(L, table, 3);
    CHECK_EQ( lua_tostring(L, -1), "three" );

    lua_close(L);

}

TEST(NextTest)
{

    lua_State* L = luaL_newstate();

    lua_newtable(L);
    int table = lua_gettop(L);

    lua_pushnumber(L, 1);
    lua_setfield(L, table, "first");

    lua_pushnumber(L, 2);
    lua_setfield(L, table, "second");

    lua_pushnumber(L, 3);
    lua_setfield(L, table, "third");

    lua_pushnil(L);

    int count[3] = { 0 };

    while (lua_next(L, table))
    {

        const char* key  = lua_tostring(L, -2);
        lua_Number value = lua_tonumber(L, -1);

        int index = -1;
        if (strcmp(key, "first") == 0)
        {
            CHECK(value == 1.0);
            index = 0;
        }
        else if (strcmp(key, "second") == 0)
        {
            CHECK(value == 2.0);
            index = 1;
        }
        else if (strcmp(key, "third") == 0)
        {
            CHECK(value == 3.0);
            index = 2;
        }

        // Check that we didn't get a key not in the table.
        CHECK(index != -1);
        ++count[index];

        lua_pop(L, 1);

    }

    // Check each element was iterated exactly once.
    CHECK(count[0] == 1);
    CHECK(count[1] == 1);
    CHECK(count[2] == 1);

    lua_close(L);

}

TEST(RemoveTest)
{

    lua_State* L = luaL_newstate();

    lua_pushinteger(L, 1);
    int start = lua_gettop(L);
    lua_pushinteger(L, 2);
    lua_pushinteger(L, 3);
    lua_pushinteger(L, 4);

    lua_remove(L, start);
    CHECK( lua_tointeger(L, start) == 2 );

    lua_remove(L, -1);
    CHECK( lua_tointeger(L, -1) == 3 );

    lua_close(L);

}

TEST(Metatable)
{

    lua_State* L = luaL_newstate();

    lua_newtable(L);
    int table = lua_gettop(L);

    lua_pushinteger(L, 2);
    lua_setfield(L, table, "b");

    lua_newtable(L);
    int mt = lua_gettop(L);

    lua_pushvalue(L, mt);
    lua_setfield(L, mt, "__index");

    lua_pushinteger(L, 1);
    lua_setfield(L, mt, "a");

    CHECK( lua_setmetatable(L, table) == 1 );

    // Test the value in the metatable.
    lua_getfield(L, table, "a");
    CHECK( lua_tointeger(L, -1) == 1 );
    lua_pop(L, 1);

    // Test the value in the table.
    lua_getfield(L, table, "b");
    CHECK( lua_tointeger(L, -1) == 2 );
    lua_pop(L, 1);

    // Test a value that doesn't exist in either.
    lua_getfield(L, table, "c");
    CHECK( lua_isnil(L, -1) == 1 );
    lua_pop(L, 1);

    lua_close(L);

}


TEST(NewMetatable)
{

    lua_State* L = luaL_newstate();

    CHECK( luaL_newmetatable(L, "test") == 1 );
    CHECK( lua_istable(L, -1) );
    lua_pop(L, 1);

    lua_getfield(L, LUA_REGISTRYINDEX, "test");
    CHECK( lua_istable(L, -1) );
    lua_pop(L, 1);

    CHECK( luaL_newmetatable(L, "test") == 0 );
    CHECK( lua_istable(L, -1) );
    lua_pop(L, 1);

    lua_close(L);

}

TEST(EnvTable)
{

    lua_State* L = luaL_newstate();

    lua_newtable(L);
    int env = lua_gettop(L);

    int top = lua_gettop(L);

    // Can't set the environment table on a table.
    lua_newtable(L);
    lua_pushvalue(L, env);
    CHECK( lua_setfenv(L, -2) == 0 );
    CHECK( lua_gettop(L) - top == 1 );
    lua_getfenv(L, -1);
    CHECK( lua_isnil(L, -1) );
    lua_pop(L, 2);

    // Set the environment table on a user data.
    lua_newuserdata(L, 10);
    lua_pushvalue(L, env);
    CHECK( lua_setfenv(L, -2) == 1 );
    CHECK( lua_gettop(L) - top == 1 );
    lua_getfenv(L, -1);
    CHECK( lua_istable(L, -1) );
    CHECK( lua_rawequal(L, -1, env) );
    lua_pop(L, 2);

    lua_close(L);

}

TEST(SetMetatableUserData)
{

    lua_State* L = luaL_newstate();

    lua_newuserdata(L, 10);
    int object = lua_gettop(L);
    
    lua_newtable(L);
    int mt = lua_gettop(L);

    int top = lua_gettop(L);
    lua_pushvalue(L, mt);
    lua_setmetatable(L, object);
    CHECK( lua_gettop(L) - top == 0);

    top = lua_gettop(L);
    CHECK( lua_getmetatable(L, object) == 1 );
    CHECK( lua_gettop(L) - top == 1);
    
    CHECK( lua_rawequal(L, -1, mt) );
    
    lua_close(L);

}

TEST(SetMetatableNil)
{

    lua_State* L = luaL_newstate();

    lua_newuserdata(L, 10);
    int object = lua_gettop(L);

    int top = lua_gettop(L);
    lua_pushnil(L);
    lua_setmetatable(L, object);
    CHECK( lua_gettop(L) - top == 0);

    top = lua_gettop(L);
    CHECK( lua_getmetatable(L, object) == 0 );
    CHECK( lua_gettop(L) - top == 0 );
    
    lua_close(L);

}

TEST(CClosure)
{

    struct Locals
    {
        static int Function(lua_State* L)
        {
            Locals* locals = static_cast<Locals*>(lua_touserdata(L, lua_upvalueindex(1)));
            int a = lua_tointeger(L, lua_upvalueindex(2));
            int b = lua_tointeger(L, lua_upvalueindex(3));
            CHECK( a == 10 );
            CHECK( b == 20 );
            CHECK( lua_isnil(L, lua_upvalueindex(4)) == 1 );
            locals->called = true;
            return 0;
        }
        bool called;
    };

    lua_State* L = luaL_newstate();

    int start = lua_gettop(L);

    Locals locals;
    locals.called = false;

    lua_pushlightuserdata(L, &locals);
    lua_pushinteger(L, 10);
    lua_pushinteger(L, 20);
    lua_pushcclosure(L, Locals::Function, 3);

    // The closure should be the only thing left.
    CHECK( lua_gettop(L) - start == 1 );

    lua_call(L, 0, 0);
    CHECK( locals.called );

    lua_close(L);

}

TEST(LightUserData)
{

    lua_State* L = luaL_newstate();

    void* p = reinterpret_cast<void*>(0x12345678);

    lua_pushlightuserdata(L, p);

    CHECK( lua_type(L, -1) == LUA_TLIGHTUSERDATA );
    CHECK( lua_touserdata(L, -1) == p );

    lua_pop(L, 1);

    lua_close(L);

}

TEST(UserData)
{

    lua_State* L = luaL_newstate();

    void* buffer = lua_newuserdata(L, 10);
    CHECK( buffer != NULL );

    CHECK( lua_type(L, -1) == LUA_TUSERDATA );
    CHECK( lua_touserdata(L, -1) == buffer );

    lua_pop(L, 1);

    lua_close(L);

}

TEST(NewIndexMetamethod)
{

    struct Locals
    {
        static int NewIndex(lua_State* L)
        {
            Locals* locals = static_cast<Locals*>(lua_touserdata(L, lua_upvalueindex(1)));
            CHECK( lua_gettop(L) == 3 );
            CHECK_EQ( lua_tostring(L, 2), "key" );
            CHECK_EQ( lua_tostring(L, 3), "value" );
            locals->called = true;
            return 0;
        }
        bool called;
    };

    lua_State* L = luaL_newstate();

    lua_newtable(L);
    int table = lua_gettop(L);

    lua_newtable(L);
    int mt = lua_gettop(L);

    Locals locals;
    locals.called = false;

    lua_pushlightuserdata(L, &locals);
    lua_pushcclosure(L, &Locals::NewIndex, 1);
    lua_setfield(L, mt, "__newindex");

    CHECK( lua_setmetatable(L, table) == 1 );

    lua_pushstring(L, "value");
    lua_setfield(L, table, "key");
    CHECK( locals.called );

    lua_pop(L, 1);

    lua_close(L);

}

TEST(GetUpValueCFunction)
{

    struct Locals
    {
        static int F(lua_State* L)
        {
            return 0;
        }
    };

    lua_State* L = luaL_newstate();

    lua_pushstring(L, "test1");
    lua_pushstring(L, "test2");
    lua_pushcclosure(L, Locals::F, 2);
    int func = lua_gettop(L);

    CHECK_EQ( lua_getupvalue(L, func, 1), "" );
    CHECK_EQ( lua_tostring(L, -1), "test1" );
    lua_pop(L, 1);

    CHECK_EQ( lua_getupvalue(L, func, 2), "" );
    CHECK_EQ( lua_tostring(L, -1), "test2" );
    lua_pop(L, 1);
    
    CHECK( lua_getupvalue(L, func, 3) == NULL );

    lua_close(L);

}

TEST(GetUpValueLuaFunction)
{

    const char* code =
        "local a = 'test1'\n"
        "local b = 'test2'\n"
        "function f()\n"
        "  local x = a\n"
        "  local y = b\n"
        "end";

    lua_State* L = luaL_newstate();

    CHECK( DoString(L, code) );

    lua_getglobal(L, "f");
    CHECK( lua_isfunction(L, -1) );
    int func = lua_gettop(L);

    CHECK_EQ( lua_getupvalue(L, func, 1), "a" );
    CHECK_EQ( lua_tostring(L, -1), "test1" );
    lua_pop(L, 1);

    CHECK_EQ( lua_getupvalue(L, func, 2), "b" );
    CHECK_EQ( lua_tostring(L, -1), "test2" );
    lua_pop(L, 1);
    
    CHECK( lua_getupvalue(L, func, 3) == NULL );

    lua_close(L);

}

/*
TEST(GetStack)
{

    // Test the lua_getstack function.
    
    lua_State* L = luaL_newstate();

    lua_getstack(L, 


    lua_close(L);

}
*/

TEST(GetInfo)
{

    // Test the lua_getinfo function.

    struct Locals
    {
        static int F(lua_State* L)
        {
            Locals* locals = static_cast<Locals*>(lua_touserdata(L, lua_upvalueindex(1)));
            if (lua_getstack(L, 0, &locals->top) == 1)
            {
                lua_getinfo(L, "lnS", &locals->top);
            }
            return 0;
        }
        lua_Debug top;
    };

    lua_State* L = luaL_newstate();

    Locals locals;

    lua_pushlightuserdata(L, &locals);
    lua_pushcclosure(L, &Locals::F, 1);
    lua_call(L, 0, 0);

    lua_close(L);

}

TEST(MultipleAssignment)
{

   const char* code =
        "a, b, c = 1, 2";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 1 );

    lua_getglobal(L, "b");
    CHECK( lua_tonumber(L, -1) == 2 );

    lua_getglobal(L, "c");
    CHECK( lua_isnil(L, -1) );

    lua_close(L);

}

TEST(MultipleAssignment2)
{

    struct Locals
    {
        static int Function(lua_State* L)
        {
            lua_pushnumber(L, 2.0);
            lua_pushnumber(L, 3.0);
            return 2;
        }
    };

   const char* code =
        "a, b, c = 1, F()";

    lua_State* L = luaL_newstate();

    lua_register(L, "F", Locals::Function);
    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 1.0 );

    lua_getglobal(L, "b");
    CHECK( lua_tonumber(L, -1) == 2.0 );

    lua_getglobal(L, "c");
    CHECK( lua_tonumber(L, -1) == 3.0 );

    lua_close(L);

}

TEST(AssignmentSideEffect)
{

   const char* code =
        "function F() b = 2 end\n"
        "a = 1, F(), 3";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 1 );

    lua_getglobal(L, "b");
    CHECK( lua_tonumber(L, -1) == 2 );

    lua_close(L);

}

TEST(LocalMultipleAssignment)
{

   const char* code =
        "local _a, _b, _c = 1, 2\n"
        "a = _a\n"
        "b = _b\n"
        "c = _c\n";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 1 );

    lua_getglobal(L, "b");
    CHECK( lua_tonumber(L, -1) == 2 );

    lua_getglobal(L, "c");
    CHECK( lua_isnil(L, -1) );

    lua_close(L);

}

TEST(LocalMultipleAssignment2)
{

    struct Locals
    {
        static int Function(lua_State* L)
        {
            lua_pushnumber(L, 2.0);
            lua_pushnumber(L, 3.0);
            return 2;
        }
    };

   const char* code =
        "local _a, _b, _c = 1, F()\n"
        "a = _a\n"
        "b = _b\n"
        "c = _c\n";

    lua_State* L = luaL_newstate();

    lua_register(L, "F", Locals::Function);
    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 1.0 );

    lua_getglobal(L, "b");
    CHECK( lua_tonumber(L, -1) == 2.0 );

    lua_getglobal(L, "c");
    CHECK( lua_tonumber(L, -1) == 3.0 );

    lua_close(L);

}

TEST(LocalMultipleAssignment3)
{

   const char* code =
        "local _a, _b = 1, _a\n"
        "a = _a\n"
        "b = _b";

    lua_State* L = luaL_newstate();

    CHECK( DoString(L, code) );
    
    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 1.0 );

    // a should not be visible until after the declaration is complete.
    lua_getglobal(L, "b");
    CHECK( lua_isnil(L, -1) );

    lua_close(L);

}

TEST(TableConstructor)
{
    const char* code =
        "t = { 'one', three = 3, 'two', [2 + 2] = 'four', (function () return 3 end)() }";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "t");
    CHECK( lua_istable(L, -1) == 1 );

    lua_rawgeti(L, -1, 1);
    CHECK( strcmp(lua_tostring(L, -1), "one") == 0 );
    lua_pop(L, 1);

    lua_rawgeti(L, -1, 2);
    CHECK( strcmp(lua_tostring(L, -1), "two") == 0 );
    lua_pop(L, 1);

    lua_getfield(L, -1, "three");
    CHECK( lua_tonumber(L, -1) == 3.0 );
    lua_pop(L, 1);

    lua_rawgeti(L, -1, 4);
    CHECK( strcmp(lua_tostring(L, -1), "four") == 0 );
    lua_pop(L, 1);

    lua_rawgeti(L, -1, 3);
    CHECK( lua_tonumber(L, -1) == 3.0 );
    lua_pop(L, 1);

    lua_close(L);

}

TEST(TableConstructorVarArg)
{

    const char* code =
        "function f(...)\n"
        "  t = { 'zero', ... }\n"
        "end\n"
        "f('one', 'two', 'three')";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "t");
    CHECK( lua_istable(L, -1) == 1 );

    lua_rawgeti(L, -1, 1);
    CHECK( lua_isstring(L, -1) );
    CHECK( strcmp(lua_tostring(L, -1), "zero") == 0 );
    lua_pop(L, 1);

    lua_rawgeti(L, -1, 2);
    CHECK( lua_isstring(L, -1) );
    CHECK( strcmp(lua_tostring(L, -1), "one") == 0 );
    lua_pop(L, 1);

    lua_rawgeti(L, -1, 3);
    CHECK( lua_isstring(L, -1) );
    CHECK( strcmp(lua_tostring(L, -1), "two") == 0 );
    lua_pop(L, 1);

    lua_rawgeti(L, -1, 4);
    CHECK( lua_isstring(L, -1) );
    CHECK( strcmp(lua_tostring(L, -1), "three") == 0 );
    lua_pop(L, 1);

    lua_close(L);

}

TEST(TableConstructorFunction)
{

    const char* code =
        "function f()\n"
        "  return 'one', 'two', 'three'\n"
        "end\n"
        "t = { 'zero', f() }";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "t");
    CHECK( lua_istable(L, -1) == 1 );

    lua_rawgeti(L, -1, 1);
    CHECK( lua_isstring(L, -1) );
    CHECK( strcmp(lua_tostring(L, -1), "zero") == 0 );
    lua_pop(L, 1);

    lua_rawgeti(L, -1, 2);
    CHECK( lua_isstring(L, -1) );
    CHECK( strcmp(lua_tostring(L, -1), "one") == 0 );
    lua_pop(L, 1);

    lua_rawgeti(L, -1, 3);
    CHECK( lua_isstring(L, -1) );
    CHECK( strcmp(lua_tostring(L, -1), "two") == 0 );
    lua_pop(L, 1);

    lua_rawgeti(L, -1, 4);
    CHECK( lua_isstring(L, -1) );
    CHECK( strcmp(lua_tostring(L, -1), "three") == 0 );
    lua_pop(L, 1);

    lua_close(L);

}

TEST(TableConstructorTrailingComma)
{

    // Lua allows for a trailing comma in a table, even though it doesn't
    // actually syntatically make sense.

    const char* code =
        "t = { 'one', }";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "t");
    CHECK( lua_istable(L, -1) == 1 );

    lua_rawgeti(L, -1, 1);
    CHECK( lua_isstring(L, -1) );
    CHECK( strcmp(lua_tostring(L, -1), "one") == 0 );
    lua_pop(L, 1);

    lua_close(L);

}

TEST(Return)
{

    const char* code =
        "function Foo()\n"
        "  return 5\n"
        "end\n"
        "v = Foo()";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );
    
    lua_getglobal(L, "v");
    CHECK( lua_tonumber(L, -1) == 5.0 );
    
    lua_close(L);

}

TEST(ReturnMultiple)
{

    const char* code =
        "function Foo()\n"
        "  return 5, 6\n"
        "end\n"
        "v1, v2 = Foo()";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );
    
    lua_getglobal(L, "v1");
    CHECK( lua_tonumber(L, -1) == 5.0 );

    lua_getglobal(L, "v2");
    CHECK( lua_tonumber(L, -1) == 6.0 );
    
    lua_close(L);

}

TEST(ReturnEmpty)
{

    const char* code =
        "function Foo()\n"
        "  return ;\n"
        "end";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_close(L);

}

TEST(FunctionStringArgument)
{

    const char* code =
        "function Foo(arg1, arg2)\n"
        "  s = arg1\n"
        "  n = arg2\n"
        "end\n"
        "Foo 'test'";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "s");
    CHECK( lua_isstring(L, -1) );
    CHECK( strcmp(lua_tostring(L, -1), "test") == 0 );
    
    lua_getglobal(L, "n");
    CHECK( lua_isnil(L, -1) );

    lua_close(L);

}

TEST(FunctionTableArgument)
{

    const char* code =
        "function Foo(arg1, arg2)\n"
        "  t = arg1\n"
        "  n = arg2\n"
        "end\n"
        "Foo { }";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "t");
    CHECK( lua_istable(L, -1) );
    
    lua_getglobal(L, "n");
    CHECK( lua_isnil(L, -1) );

    lua_close(L);

}

TEST(FunctionMethod)
{

    const char* code =
        "result = false\n"
        "Foo = { }\n"
        "function Foo:Bar()\n"
        "  if self == Foo then result = true end\n"
        "end\n"
        "Foo:Bar()\n";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );
    
    // Check that the function was properly created.
    lua_getglobal(L, "Foo");
    lua_getfield(L, -1, "Bar");
    CHECK( lua_type(L, -1) == LUA_TFUNCTION );
    lua_pop(L, 2);

    // Check that the function was properly called.
    lua_getglobal(L, "result");
    CHECK( lua_toboolean(L, -1) == 1 );
    
    lua_close(L);

}

TEST(FunctionMethodStringArg)
{

    const char* code =
        "result = false\n"
        "Foo = { }\n"
        "function Foo:Bar(_arg)\n"
        "  if self == Foo then result = true end\n"
        "  arg = _arg\n"
        "end\n"
        "Foo:Bar 'test'\n";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "result");
    CHECK( lua_toboolean(L, -1) == 1 );

    lua_getglobal(L, "arg");
    CHECK( lua_isstring(L, -1) );
    CHECK( strcmp(lua_tostring(L, -1), "test") == 0 );
    
    lua_close(L);

}

TEST(FunctionDefinition)
{
    const char* code = "function Foo() end";

    lua_State* L = luaL_newstate();
    CHECK( luaL_dostring(L, code) == 0 );

    lua_getglobal(L, "Foo");
    CHECK( lua_type(L, -1) == LUA_TFUNCTION );
    
    lua_close(L);

}

TEST(ScopedFunctionDefinition)
{
    
    const char* code = 
        "Foo = { }\n"
        "Foo.Bar = { }\n"
        "function Foo.Bar.Baz() end";
    
    lua_State* L = luaL_newstate();

    CHECK( luaL_dostring(L, code) == 0 );
    
    lua_getglobal(L, "Foo");
    lua_getfield(L, -1, "Bar");
    lua_getfield(L, -1, "Baz");
    
    CHECK( lua_type(L, -1) == LUA_TFUNCTION );
    
    lua_close(L);

}

TEST(FunctionMethodDefinition)
{

    const char* code =
        "Foo = { }\n"
        "function Foo:Bar() end";

    lua_State* L = luaL_newstate();
    CHECK( luaL_dostring(L, code) == 0 );
    
    lua_getglobal(L, "Foo");
    lua_getfield(L, -1, "Bar");
    CHECK( lua_type(L, -1) == LUA_TFUNCTION );
    
    lua_close(L);

}

TEST(LocalFunctionDefinition)
{
    const char* code =
        "local function Foo() end\n"
        "Bar = Foo";

    lua_State* L = luaL_newstate();
    CHECK( luaL_dostring(L, code) == 0 );
    
    lua_getglobal(L, "Bar");
    CHECK( lua_type(L, -1) == LUA_TFUNCTION );

    lua_getglobal(L, "Foo");
    CHECK( lua_type(L, -1) == LUA_TNIL );

    lua_close(L);

}

TEST(LocalScopedFunctionDefinition)
{
    
    const char* code = 
        "Foo = { }\n"
        "local function Foo.Bar() end";

    // Scoping makes no sense when we're defining a local.
    lua_State* L = luaL_newstate();
    CHECK( luaL_dostring(L, code) == 1 );
    
    lua_close(L);

}

TEST(LocalMethod)
{

    const char* code =
        "local t = { }\n"
        "function t:c() return { d = 5 } end\n"
        "a = t:c().d";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 5 );

    lua_close(L);

}

TEST(WhileLoop)
{

    const char* code = 
        "index = 0\n"
        "while index < 10 do\n"
        "  index = index + 1\n"
        "end";

    lua_State* L = luaL_newstate();

    CHECK( DoString(L, code) );

    lua_getglobal(L, "index");
    CHECK( lua_type(L, -1) == LUA_TNUMBER );
    CHECK( lua_tointeger(L, -1) == 10 );

    lua_close(L);

}

TEST(ForLoop1)
{

    const char* code = 
        "index = 0\n"
        "for i = 1,10 do\n"
        "  index = index + 1\n"
        "end";

    lua_State* L = luaL_newstate();

    CHECK( DoString(L, code) );

    lua_getglobal(L, "index");
    CHECK( lua_type(L, -1) == LUA_TNUMBER );
    CHECK( lua_tointeger(L, -1) == 10 );

    // The index for the loop shouldn't be in the global space.
    lua_getglobal(L, "i");
    CHECK( lua_isnil(L, -1) != 0 );

    lua_close(L);

}

TEST(ForLoop2)
{

    const char* code = 
        "index = 0\n"
        "for i = 1,10,2 do\n"
        "  index = index + 1\n"
        "end";

    lua_State* L = luaL_newstate();

    CHECK( DoString(L, code) );

    lua_getglobal(L, "index");
    CHECK( lua_type(L, -1) == LUA_TNUMBER );
    CHECK( lua_tointeger(L, -1) == 5 );

    // The index for the loop shouldn't be in the global space.
    lua_getglobal(L, "i");
    CHECK( lua_isnil(L, -1) != 0 );

    lua_close(L);

}

/*
TEST(ForLoop3)
{

    const char* code = 
        "values = { first=1, second=2 }\n"
        "results = { }\n"
        "index = 0\n"
        "for k,v in pairs(values) do\n"
        "  index = index + 1\n"
        "  results[v] = k\n"
        "end";

    lua_State* L = luaL_newstate();

    CHECK( DoString(L, code) );

    lua_getglobal(L, "index");
    CHECK( lua_type(L, -1) == LUA_TNUMBER );
    CHECK( lua_tointeger(L, -1) == 2 );

    // The index for the loop shouldn't be in the global space.
    lua_getglobal(L, "k");
    CHECK( lua_isnil(L, -1) != 0 );
    lua_getglobal(L, "v");
    CHECK( lua_isnil(L, -1) != 0 );

    lua_getglobal(L, "results");

    lua_rawgeti(L, -1, 1);
    CHECK( lua_isstring(L, -1) );
    CHECK( strcmp( lua_tostring(L, -1), "first" ) == 0 );
    lua_pop(L, 1);

    lua_rawgeti(L, -1, 2);
    CHECK( lua_isstring(L, -1) );
    CHECK( strcmp( lua_tostring(L, -1), "second" ) == 0 );
    lua_pop(L, 1);

    lua_close(L);

}
*/

TEST(ForLoop3)
{

    const char* code = 
        "values = { first=1, second=2 }\n"
        "for k,v in pairs(values) do\n"
        "end";

    lua_State* L = luaL_newstate();

    CHECK( DoString(L, code) );

    lua_close(L);

}

TEST(RepeatLoop)
{

    const char* code = 
        "index = 0\n"
        "repeat\n"
        "  index = index + 1\n"
        "until index == 10";

    lua_State* L = luaL_newstate();

    CHECK( DoString(L, code) );

    lua_getglobal(L, "index");
    CHECK( lua_type(L, -1) == LUA_TNUMBER );
    CHECK( lua_tointeger(L, -1) == 10 );

    lua_close(L);

}

TEST(WhileLoopBreak)
{

    const char* code = 
        "index = 0\n"
        "while true do\n"
        "  index = index + 1\n"
        "  break\n"
        "end";

    lua_State* L = luaL_newstate();

    CHECK( DoString(L, code) );

    lua_getglobal(L, "index");
    CHECK( lua_type(L, -1) == LUA_TNUMBER );
    CHECK( lua_tointeger(L, -1) == 1 );

    lua_close(L);

}

TEST(ForLoopBreak)
{

    const char* code = 
        "index = 0\n"
        "for i = 1,10 do\n"
        "  index = index + 1\n"
        "  break\n"
        "end";

    lua_State* L = luaL_newstate();

    CHECK( DoString(L, code) );

    lua_getglobal(L, "index");
    CHECK( lua_type(L, -1) == LUA_TNUMBER );
    CHECK( lua_tointeger(L, -1) == 1 );

    lua_close(L);

}

TEST(RepeatLoopBreak)
{

    const char* code = 
        "index = 0\n"
        "repeat\n"
        "  index = index + 1\n"
        "  break\n"
        "until index == 10";

    lua_State* L = luaL_newstate();

    CHECK( DoString(L, code) );

    lua_getglobal(L, "index");
    CHECK( lua_type(L, -1) == LUA_TNUMBER );
    CHECK( lua_tointeger(L, -1) == 1 );

    lua_close(L);

}

TEST(IllegalBreak)
{
    const char* code = 
        "print('test')\n"
        "break";
    lua_State* L = luaL_newstate();
    CHECK( luaL_loadstring(L, code) != 0 );
    lua_close(L);
}

TEST(FunctionCallStringArg)
{

    const char* code =
        "Function 'hello'";

    struct Locals
    {
        static int Function(lua_State* L)
        {
            Locals* locals = static_cast<Locals*>(lua_touserdata(L, lua_upvalueindex(1)));
            const char* arg = lua_tostring(L, 1);
            if (arg != NULL)
            {
                locals->passed = (strcmp(arg, "hello") == 0);
            }
            return 0;
        }
        bool passed;
    }
    locals;

    locals.passed = false;

    lua_State* L = luaL_newstate();

    lua_pushlightuserdata(L, &locals);
    lua_pushcclosure(L, Locals::Function, 1);
    lua_setglobal(L, "Function");

    CHECK( DoString(L, code) );
    CHECK( locals.passed );

    lua_close(L);

}

TEST(FunctionCallTableArgument)
{

    const char* code =
        "Function { }";

    struct Locals
    {
        static int Function(lua_State* L)
        {
            Locals* locals = static_cast<Locals*>(lua_touserdata(L, lua_upvalueindex(1)));
            if (lua_istable(L, 1))
            {
                locals->passed = true;
            }
            return 0;
        }
        bool passed;
    }
    locals;

    locals.passed = false;

    lua_State* L = luaL_newstate();

    lua_pushlightuserdata(L, &locals);
    lua_pushcclosure(L, Locals::Function, 1);
    lua_setglobal(L, "Function");

    CHECK( DoString(L, code) );
    CHECK( locals.passed );

    lua_close(L);

}

TEST(LengthOperator)
{

    const char* code =
        "t = { 1 }\n"
        "l = #t";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );
    
    lua_getglobal(L, "l");
    CHECK( lua_isnumber(L, -1) );
    CHECK( lua_tonumber(L, -1) == 1 );

    lua_close(L);

}

TEST(ConcatOperator)
{

    const char* code =
        "s = 'a' .. 'b' .. 'c'";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "s");
    const char* s = lua_tostring(L, -1);

    CHECK( s != NULL );
    CHECK( strcmp(s, "abc") == 0 );

    lua_close(L);

}

TEST(VarArg1)
{

    const char* code =
        "function g(a, b, ...)\n"
        "  w, x = ..., 5\n"
        "end\n"
        "g(1, 2, 3, 4)";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "w");
    CHECK( lua_tonumber(L, -1) == 3.0 );

    lua_getglobal(L, "x");
    CHECK( lua_tonumber(L, -1) == 5.0 );

    lua_close(L);

}

TEST(VarArg2)
{

    const char* code =
        "function g(a, b, ...)\n"
        "  w, x = ...\n"
        "end\n"
        "g(1, 2, 3, 4)";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "w");
    CHECK( lua_tonumber(L, -1) == 3.0 );

    lua_getglobal(L, "x");
    CHECK( lua_tonumber(L, -1) == 4.0 );

    lua_close(L);

}

TEST(VarArg3)
{

    const char* code =
        "function f(a, b, c)\n"
        "  x, y, z = a, b, c\n"
        "end\n"
        "function g(...)\n"
        "  f(...)\n"
        "end\n"
        "g(1, 2)";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "x");
    CHECK( lua_tonumber(L, -1) == 1.0 );

    lua_getglobal(L, "y");
    CHECK( lua_tonumber(L, -1) == 2.0 );

    lua_getglobal(L, "z");
    CHECK( lua_isnil(L, -1) );

    lua_close(L);

}

TEST(VarArg4)
{

    const char* code =
        "function f(a, b, c)\n"
        "  x, y, z = a, b, c\n"
        "end\n"
        "function g(...)\n"
        "  f(..., 3)\n"
        "end\n"
        "g(1, 2)";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "x");
    CHECK( lua_tonumber(L, -1) == 1.0 );

    lua_getglobal(L, "y");
    CHECK( lua_tonumber(L, -1) == 3.0 );

    lua_getglobal(L, "z");
    CHECK( lua_isnil(L, -1) );

    lua_close(L);

}

TEST(VarArg5)
{

    const char* code =
        "function f(a, b)\n"
        "  x = a\n"
        "  y = b\n"
        "end\n"
        "function g()\n"
        "  return 1, 2\n"
        "end\n"
        "f( g() )";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "x");
    CHECK( lua_tonumber(L, -1) == 1.0 );

    lua_getglobal(L, "y");
    CHECK( lua_tonumber(L, -1) == 2.0 );

    lua_close(L);

}

TEST(DoBlock)
{

    const char* code =
        "local _a = 1\n"
        "do\n"
        "  local _a, _b\n"
        "  _a = 2\n"
        "  _b = 3\n"
        "end\n"
        "a = _a\n"
        "b = _b";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 1.0 );

    lua_getglobal(L, "b");
    CHECK( lua_isnil(L, -1)  );

    lua_close(L);

}

TEST(LocalUpValue)
{

    const char* code =
        "local a = 1\n"
        "do\n"
        "  local p = 2\n"
        "  f = function() return p end\n"
        "end\n"
        "local b = 3\n"
        "c = f()\n";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "c");
    CHECK( lua_tonumber(L, -1) == 2.0 );

    lua_close(L);

}

TEST(ShadowLocalUpValue)
{

    const char* code =
        "local a = 5\n"
        "function f()\n"
        "  v = a\n"
        "end\n"
        "local a = 6\n"
        "f()";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "v");
    CHECK( lua_tonumber(L, -1) == 5.0 );

    lua_close(L);

}

TEST(EmptyStatement)
{

    const char* code =
        "function g() end\n"
        "local a = f ; (g)()\n";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_close(L);

}

TEST(CppCommentLine)
{

    const char* code =
        "// this is a comment\n"
        "a = 1";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );
    
    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 1.0 );

    lua_close(L);

}

TEST(CppCommentBlock)
{

    const char* code =
        "/* this is a comment\n"
        "that goes for multiple lines */\n"
        "a = 1\n";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 1.0 );

    lua_close(L);

}

TEST(LuaCommentBlock)
{

    const char* code =
        "--[[ this is a comment\n"
        "this is the second line ]]\n"
        "a = 1";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );
    
    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 1.0 );

    lua_close(L);

}

TEST(NotEqual)
{

    const char* code =
        "a = (5 ~= 6)\n"
        "b = (7 ~= 7)";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_isboolean(L, -1) );
    CHECK( lua_toboolean(L, -1) == 1 );

    lua_getglobal(L, "b");
    CHECK( lua_isboolean(L, -1) );
    CHECK( lua_toboolean(L, -1) == 0 );

    lua_close(L);

}

TEST(Number)
{

    const char* code =
        "a = 3\n"
        "b = 3.14\n"
        "c = -3.1416\n"
        "d = -.12\n"
        /*
        "e = 314.16e-2\n";
        "f = 0.31416E1\n";
        */
        "g = 0xff\n"
        "h = 0x56";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 3.0 );

    lua_getglobal(L, "b");
    CHECK_CLOSE( lua_tonumber(L, -1), 3.14 ); 

    lua_getglobal(L, "c");
    CHECK_CLOSE( lua_tonumber(L, -1), -3.1416 );

    lua_getglobal(L, "d");
    CHECK_CLOSE( lua_tonumber(L, -1), -0.12);

    /*
    lua_getglobal(L, "e");
    CHECK( lua_tonumber(L, -1) == 314.16e-2 );
    
    lua_getglobal(L, "f");
    CHECK( lua_tonumber(L, -1) == 0.31416e1 );
    */

    lua_getglobal(L, "g");
    CHECK_CLOSE( lua_tonumber(L, -1), 0xFF );

    lua_getglobal(L, "h");
    CHECK_CLOSE( lua_tonumber(L, -1), 0x56 );

    lua_close(L);

}

TEST(ElseIf)
{

    const char* code =
        "if false then\n"
        "elseif true then\n"
        "  success = true\n"
        "end";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "success");
    CHECK( lua_toboolean(L, -1) );

    lua_close(L);

}

TEST(DivideOperator)
{

    const char* code =
        "a = 10 / 2";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 5 );

    lua_close(L);

}

TEST(SubtractOperator)
{

    // Check that the subtraction symbol is properly parsed and
    // not treated as part of the 4.
    const char* code =
        "v = 6-4";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "v");
    CHECK( lua_tonumber(L, -1) == 2 );

    lua_close(L);

}

TEST(UnaryMinusOperator)
{

    const char* code =
        "local x = 5\n" 
        "y = -x";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "y");
    CHECK( lua_tonumber(L, -1) == -5 );

    lua_close(L);

}

TEST(UnaryMinusOperatorConstant)
{

    const char* code =
        "x = -5";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "x");
    CHECK( lua_tonumber(L, -1) == -5 );

    lua_close(L);

}

TEST(ModuloOperator)
{

    const char* code =
        "a = 10 % 3";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 1 );

    lua_close(L);

}

TEST(ExponentiationOperator)
{

    const char* code =
        "a = 2 ^ 3";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 8 );

    lua_close(L);

}

TEST(EscapeCharacters)
{

    const char* code =
        "b = '\\01a\\002\\a\\b\\f\\n\\r\\t\\v\\\"\\\''";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "b");
    CHECK( lua_isstring(L, -1) );
    const char* buffer = lua_tostring(L, -1); 

    CHECK( buffer[0] == 1 );
    CHECK( buffer[1] == 'a' );
    CHECK( buffer[2] == 2 );
    CHECK( buffer[3] == '\a' );
    CHECK( buffer[4] == '\b' );
    CHECK( buffer[5] == '\f' );
    CHECK( buffer[6] == '\n' );
    CHECK( buffer[7] == '\r' );
    CHECK( buffer[8] == '\t' );
    CHECK( buffer[9] == '\v' );
    CHECK( buffer[10] == '\"' );
    CHECK( buffer[11] == '\'' );

    lua_close(L);

}

TEST(InvalidEscapeCharacters)
{

    const char* code =
        "b = '\\xyz";

    lua_State* L = luaL_newstate();
    CHECK( luaL_loadstring(L, code) != 0 );
    lua_close(L);

}

TEST(NilConstant)
{

    const char* code =
        "c = nil\n"
        "if a == nil then\n"
        "  b = 5\n"
        "end\n";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "b");
    CHECK( lua_tonumber(L, -1) == 5 );

    lua_getglobal(L, "c");
    CHECK( lua_isnil(L, -1) );

    lua_close(L);

}

TEST(ContinuedString)
{

    const char* code =
        "a = 'one\\\ntwo'";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_isstring(L, -1) );
    CHECK_EQ( lua_tostring(L, -1), "one\ntwo" );

    lua_close(L);

}

TEST(LongString)
{

    const char* code =
        "a = [[one\ntwo]]";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_isstring(L, -1) );
    CHECK_EQ( lua_tostring(L, -1), "one\ntwo" );

    lua_close(L);

}

TEST(Closure)
{

    const char* code =
        "local l = 5\n"
        "function f()\n"
        "  v = l\n"
        "end\n"
        "f()";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "v");
    CHECK( lua_tonumber(L, -1) == 5 );

    lua_close(L);

}

TEST(ClosureInClosure)
{

    const char* code =
        "local l = 5\n"
        "function f()\n"
        "  local function g() v = l end\n"
        "  g()\n"
        "end\n"
        "f()";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "v");
    CHECK( lua_tonumber(L, -1) == 5 );

    lua_close(L);

}

TEST(ManyConstants)
{

    const char* code =
        "a = { }\n"
        "a[001]=001; a[002]=002; a[003]=003; a[004]=004; a[005]=005; a[006]=006\n"
        "a[007]=007; a[008]=008; a[009]=009; a[010]=010; a[011]=011; a[012]=012\n"
        "a[013]=013; a[014]=014; a[015]=015; a[016]=016; a[017]=017; a[018]=018\n"
        "a[019]=019; a[020]=020; a[021]=021; a[022]=022; a[023]=023; a[024]=024\n"
        "a[025]=025; a[026]=026; a[027]=027; a[028]=028; a[029]=029; a[030]=030\n"
        "a[031]=031; a[032]=032; a[033]=033; a[034]=034; a[035]=035; a[036]=036\n"
        "a[037]=037; a[038]=038; a[039]=039; a[040]=040; a[041]=041; a[042]=042\n"
        "a[043]=043; a[044]=044; a[045]=045; a[046]=046; a[047]=047; a[048]=048\n"
        "a[049]=049; a[050]=050; a[051]=051; a[052]=052; a[053]=053; a[054]=054\n"
        "a[055]=055; a[056]=056; a[057]=057; a[058]=058; a[059]=059; a[060]=060\n"
        "a[061]=061; a[062]=062; a[063]=063; a[064]=064; a[065]=065; a[066]=066\n"
        "a[067]=067; a[068]=068; a[069]=069; a[070]=070; a[071]=071; a[072]=072\n"
        "a[073]=073; a[074]=074; a[075]=075; a[076]=076; a[077]=077; a[078]=078\n"
        "a[079]=079; a[080]=080; a[081]=081; a[082]=082; a[083]=083; a[084]=084\n"
        "a[085]=085; a[086]=086; a[087]=087; a[088]=088; a[089]=089; a[090]=090\n"
        "a[091]=091; a[092]=092; a[093]=093; a[094]=094; a[095]=095; a[096]=096\n"
        "a[097]=097; a[098]=098; a[099]=099; a[100]=100; a[101]=101; a[102]=102\n"
        "a[103]=103; a[104]=104; a[105]=105; a[106]=106; a[107]=107; a[108]=108\n"
        "a[109]=109; a[110]=110; a[111]=111; a[112]=112; a[113]=113; a[114]=114\n"
        "a[115]=115; a[116]=116; a[117]=117; a[118]=118; a[119]=119; a[120]=120\n"
        "a[121]=121; a[122]=122; a[123]=123; a[124]=124; a[125]=125; a[126]=126\n"
        "a[127]=127; a[128]=128; a[129]=129; a[130]=130; a[131]=131; a[132]=132\n"
        "a[133]=133; a[134]=134; a[135]=135; a[136]=136; a[137]=137; a[138]=138\n"
        "a[139]=139; a[140]=140; a[141]=141; a[142]=142; a[143]=143; a[144]=144\n"
        "a[145]=145; a[146]=146; a[147]=147; a[148]=148; a[149]=149; a[150]=150\n"
        "a[151]=151; a[152]=152; a[153]=153; a[154]=154; a[155]=155; a[156]=156\n"
        "a[157]=157; a[158]=158; a[159]=159; a[160]=160; a[161]=161; a[162]=162\n"
        "a[163]=163; a[164]=164; a[165]=165; a[166]=166; a[167]=167; a[168]=168\n"
        "a[169]=169; a[170]=170; a[171]=171; a[172]=172; a[173]=173; a[174]=174\n"
        "a[175]=175; a[176]=176; a[177]=177; a[178]=178; a[179]=179; a[180]=180\n"
        "a[181]=181; a[182]=182; a[183]=183; a[184]=184; a[185]=185; a[186]=186\n"
        "a[187]=187; a[188]=188; a[189]=189; a[190]=190; a[191]=191; a[192]=192\n"
        "a[193]=193; a[194]=194; a[195]=195; a[196]=196; a[197]=197; a[198]=198\n"
        "a[199]=199; a[200]=200; a[201]=201; a[202]=202; a[203]=203; a[204]=204\n"
        "a[205]=205; a[206]=206; a[207]=207; a[208]=208; a[209]=209; a[210]=210\n"
        "a[211]=211; a[212]=212; a[213]=213; a[214]=214; a[215]=215; a[216]=216\n"
        "a[217]=217; a[218]=218; a[219]=219; a[220]=220; a[221]=221; a[222]=222\n"
        "a[223]=223; a[224]=224; a[225]=225; a[226]=226; a[227]=227; a[228]=228\n"
        "a[229]=229; a[230]=230; a[231]=231; a[232]=232; a[233]=233; a[234]=234\n"
        "a[235]=235; a[236]=236; a[237]=237; a[238]=238; a[239]=239; a[240]=240\n"
        "a[241]=241; a[242]=242; a[243]=243; a[244]=244; a[245]=245; a[246]=246\n"
        "a[247]=247; a[248]=248; a[249]=249; a[250]=250; a[251]=251; a[252]=252\n"
        "a[253]=253; a[254]=254; a[255]=255; a[256]=256";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );
    lua_close(L);

}

TEST(LargeArray)
{

    const char* code = 
        "t = {\n"
        "0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,\n"
        "0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,\n"
        "0x1f,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,\n"
        "0x2e,0x2f,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,\n"
        "0x3d,0x3e,0x3f,0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,\n"
        "0x4c,0x4d,0x4e,0x4f,0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,\n"
        "0x5b,0x5c,0x5d,0x5e,0x5f,0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,\n"
        "0x6a,0x6b,0x6c,0x6d,0x6e,0x6f,0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,\n"
        "0x79,0x7a,0x7b,0x7c,0x7d,0x7e,0x7f,0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,\n"
        "0x88,0x89,0x8a,0x8b,0x8c,0x8d,0x8e,0x8f,0x90,0x91,0x92,0x93,0x94,0x95,0x96,\n"
        "0x97,0x98,0x99,0x9a,0x9b,0x9c,0x9d,0x9e,0x9f,0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,\n"
        "0xa6,0xa7,0xa8,0xa9,0xaa,0xab,0xac,0xad,0xae,0xaf,0xb0,0xb1,0xb2,0xb3,0xb4,\n"
        "0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf,0xc0,0xc1,0xc2,0xc3,\n"
        "0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xcb,0xcc,0xcd,0xce,0xcf,0xd0,0xd1,0xd2,\n"
        "0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xdb,0xdc,0xdd,0xde,0xdf,0xe0,0xe1,\n"
        "0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xeb,0xec,0xed,0xee,0xef,0xf0,\n"
        "0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff\n"
        "}";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );
    
    lua_getglobal(L, "t");
    CHECK( lua_istable(L, -1) );

    for (int i = 1; i < 256; ++i)
    {
        lua_rawgeti(L, -1, i);
        CHECK( lua_tonumber(L, -1) == i );
        lua_pop(L, 1);
    }
    
    lua_close(L);

}

TEST(LocalInit)
{
    
    const char* code =
        "function f() end\n"
        "f(1, 2, 3, 4, 5, 6)\n"
        "do\n"
        "  local _a\n"
        "  a = _a\n"
        "end";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_isnil(L, -1) );
    
    lua_close(L);

}

TEST(OperatorPrecedence)
{
    
    const char* code =
        "a = 1 + 2 * 3\n"
        "b = 1 + 4 / 2\n"
        "c = 1 - 2 * 3\n"
        "d = 1 - 4 / 2\n"
        "e = 2 * -3 ^ 4 * 5\n";

    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK_CLOSE( lua_tonumber(L, -1), 7);

    lua_getglobal(L, "b");
    CHECK_CLOSE( lua_tonumber(L, -1),  3);

    lua_getglobal(L, "c");
    CHECK_CLOSE( lua_tonumber(L, -1),  -5);

    lua_getglobal(L, "d");
    CHECK_CLOSE( lua_tonumber(L, -1),  -1);

    lua_getglobal(L, "e");
    CHECK_CLOSE( lua_tonumber(L, -1),  -810);
    
    lua_close(L);

}

TEST(LocalTable)
{

    const char* code =
        "local _t = { 'one', 'two', 'three' }\n"
        "t = _t";
    
    lua_State* L = luaL_newstate();
    CHECK( DoString(L, code) );

    lua_getglobal(L, "t");
    CHECK( lua_istable(L, -1) );
    
    lua_rawgeti(L, -1, 1);
    CHECK_EQ( lua_tostring(L, -1), "one" );
    lua_pop(L, 1);

    lua_rawgeti(L, -1, 2);
    CHECK_EQ( lua_tostring(L, -1), "two" );
    lua_pop(L, 1);

    lua_rawgeti(L, -1, 3);
    CHECK_EQ( lua_tostring(L, -1), "three" );
    lua_pop(L, 1);

    lua_close(L);

}