/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
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

TEST_FIXTURE(ToCFunction, LuaFixture)
{

    struct Locals
    {
        static int F(lua_State* L)
        {
            return 0;
        }
    };

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

}

TEST_FIXTURE(ConcatTest, LuaFixture)
{

    int top = lua_gettop(L);

    lua_pushstring(L, "Hello ");
    lua_pushnumber(L, 5.0);
    lua_pushstring(L, " goodbye");
    lua_concat(L, 3);

    const char* result = lua_tostring(L, -1);
    CHECK( strcmp(result, "Hello 5 goodbye") == 0 );
    CHECK( lua_gettop(L) - top == 1 );

}

TEST_FIXTURE(InsertTest, LuaFixture)
{

    int top = lua_gettop(L);

    lua_pushinteger(L, 1);
    lua_pushinteger(L, 3);
    lua_pushinteger(L, 2);
    lua_insert(L, -2);

    CHECK( lua_tointeger(L, -3) == 1 );
    CHECK( lua_tointeger(L, -2) == 2 );
    CHECK( lua_tointeger(L, -1) == 3 );
    
    CHECK( lua_gettop(L) - top == 3 );

}

TEST_FIXTURE(Replace, LuaFixture)
{

    int top = lua_gettop(L);

    lua_pushinteger(L, 1);
    lua_pushinteger(L, 3);
    lua_pushinteger(L, 2);
    lua_replace(L, -3);

    CHECK( lua_tointeger(L, -2) == 2 );
    CHECK( lua_tointeger(L, -1) == 3 );
    
    CHECK( lua_gettop(L) - top == 2 );

}

TEST_FIXTURE(RawEqual, LuaFixture)
{

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
    lua_pop(L, 1);

    lua_pushvalue(L, LUA_REGISTRYINDEX);
    CHECK( lua_rawequal(L, LUA_REGISTRYINDEX, lua_gettop(L)) == 1 );
    lua_pop(L, 1);

}

TEST_FIXTURE(Less, LuaFixture)
{

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

}

TEST_FIXTURE(PCallTest, LuaFixture)
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

    lua_pushstring(L, "dummy");
    int top = lua_gettop(L);
    lua_pushcfunction(L, Locals::ErrorFunction);
    CHECK( lua_pcall(L, 0, 0, 0) == LUA_ERRRUN );
    CHECK( strcmp( lua_tostring(L, -1), "Error message") == 0 );
    CHECK( lua_gettop(L) - top == 1 );

}

TEST_FIXTURE(ErrorRestore, LuaFixture)
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

}

TEST_FIXTURE(ErrorRestore2, LuaFixture)
{

    lua_pushstring(L, "dummy");
  
    int top = lua_gettop(L);
    int result = luaL_loadbuffer(L, "x", 1, NULL);
    CHECK( result != 0);
    CHECK (lua_gettop(L) - top == 1 );

}

TEST_FIXTURE(ErrorHandler, LuaFixture)
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

    lua_pushcfunction(L, Locals::ErrorHandler );
    int err = lua_gettop(L);

    int top = lua_gettop(L);
    lua_pushcfunction(L, Locals::ErrorFunction);
    lua_pushnumber(L, 1);
    lua_pushnumber(L, 2);
    CHECK( lua_pcall(L, 2, 0, err) == LUA_ERRRUN );
    CHECK( strcmp( lua_tostring(L, -1), "Error handler") == 0 );
    CHECK( lua_gettop(L) - top == 1 );

}

TEST_FIXTURE(ErrorHandlerError, LuaFixture)
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

    lua_pushcfunction(L, Locals::ErrorHandler );
    int err = lua_gettop(L);

    int top = lua_gettop(L);
    lua_pushcfunction(L, Locals::ErrorFunction);
    lua_pushnumber(L, 1);
    lua_pushnumber(L, 2);
    CHECK( lua_pcall(L, 2, 0, err) == LUA_ERRERR );
    CHECK( lua_isstring(L, -1) );
    CHECK( lua_gettop(L) - top == 1 );

}

TEST_FIXTURE(GetTable, LuaFixture)
{

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

}

TEST_FIXTURE(GetTableMetamethod, LuaFixture)
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

}

TEST_FIXTURE(UserDataGetTableMetamethod, LuaFixture)
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

    lua_newuserdata(L, 10);
    int object = lua_gettop(L);

    // Setup a metatable for the object.
    lua_newtable(L);
    lua_pushlightuserdata(L, &locals);
    lua_pushcclosure(L, Locals::Index, 1);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, object);

    lua_pushstring(L, "key");
    lua_gettable(L, object);
    CHECK_EQ( lua_tostring(L, -1), "value" );
    CHECK( locals.success );
    CHECK( locals.calls == 1 );

}

TEST_FIXTURE(CallMetamethod, LuaFixture)
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

}

TEST_FIXTURE(AddMetamethod, LuaFixture)
{

    struct Locals
    {
        static int Op(lua_State* L)
        {
            Locals* locals = static_cast<Locals*>(lua_touserdata(L, lua_upvalueindex(1)));
            if (lua_gettop(L) == 2
                && lua_touserdata(L, 1) == locals->userData
                && lua_tonumber(L, 2) == 5.0)
            {
                locals->success = true;
            }
            lua_pushstring(L, "result");
            return 1;
        }
        void*   userData;
        bool    success;
    };

    Locals locals;
    locals.success = false;
    
    // Test the __add metamethod.

    locals.userData = lua_newuserdata(L, 10);
    int object = lua_gettop(L);

    lua_newtable(L);
    int mt = lua_gettop(L);

    lua_pushlightuserdata(L, &locals);
    lua_pushcclosure(L, Locals::Op, 1);
    lua_setfield(L, mt, "__add");

    lua_setmetatable(L, object);

    lua_pushvalue(L, object);
    lua_setglobal(L, "ud");

    const char* code = "result = ud + 5";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "result");
    CHECK_EQ( lua_tostring(L, -1), "result" );

    CHECK( locals.success );

}

TEST_FIXTURE(UnaryMinusMetamethod, LuaFixture)
{

    struct Locals
    {
        static int Op(lua_State* L)
        {
            Locals* locals = static_cast<Locals*>(lua_touserdata(L, lua_upvalueindex(1)));
            if (lua_gettop(L) == 1
                && lua_touserdata(L, 1) == locals->userData)
            {
                locals->success = true;
            }
            lua_pushstring(L, "result");
            return 1;
        }
        void*   userData;
        bool    success;
    };

    Locals locals;
    locals.success = false;
    
    // Test the __unm metamethod.

    locals.userData = lua_newuserdata(L, 10);
    int object = lua_gettop(L);

    lua_newtable(L);
    int mt = lua_gettop(L);

    lua_pushlightuserdata(L, &locals);
    lua_pushcclosure(L, Locals::Op, 1);
    lua_setfield(L, mt, "__unm");

    lua_setmetatable(L, object);

    lua_pushvalue(L, object);
    lua_setglobal(L, "ud");

    const char* code = "result = -ud";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "result");
    CHECK_EQ( lua_tostring(L, -1), "result" );

    CHECK( locals.success );

}

TEST_FIXTURE(LessThanMetamethod, LuaFixture)
{

    struct Locals
    {
        static int Op(lua_State* L)
        {
            Locals* locals = static_cast<Locals*>(lua_touserdata(L, lua_upvalueindex(1)));
            if (lua_gettop(L) == 2
                && lua_touserdata(L, 1) == locals->userData
                && lua_touserdata(L, 1) == locals->userData)
            {
                locals->success = true;
            }
            lua_pushboolean(L, 1);
            return 1;
        }
        void*   userData;
        bool    success;
    };

    Locals locals;
    locals.success = false;
    
    // Test the __add metamethod.

    locals.userData = lua_newuserdata(L, 10);
    int object = lua_gettop(L);

    lua_newtable(L);
    int mt = lua_gettop(L);

    lua_pushlightuserdata(L, &locals);
    lua_pushcclosure(L, Locals::Op, 1);
    lua_setfield(L, mt, "__lt");

    lua_setmetatable(L, object);

    lua_pushvalue(L, object);
    lua_setglobal(L, "ud");

    const char* code = "result = ud < ud";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "result");
    CHECK( lua_toboolean(L, -1) );

    CHECK( locals.success );

}

TEST_FIXTURE(RawGetITest, LuaFixture)
{

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

}

TEST_FIXTURE(RawGetTest, LuaFixture)
{

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

}

TEST_FIXTURE(RawSetTest, LuaFixture)
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

}

TEST_FIXTURE(RawSetITest, LuaFixture)
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

}

TEST_FIXTURE(NextTest, LuaFixture)
{

    lua_newtable(L);
    int table = lua_gettop(L);

    lua_pushnumber(L, 1);
    lua_setfield(L, table, "first");

    lua_pushnumber(L, 2);
    lua_setfield(L, table, "second");

    lua_pushnumber(L, 3);
    lua_setfield(L, table, "third");

    int top = lua_gettop(L);

    lua_pushnil(L);

    int count[3] = { 0 };

    while (lua_next(L, table))
    {

        CHECK( lua_gettop(L) - top == 2 );

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

    CHECK( lua_gettop(L) - top == 0 );

    // Check each element was iterated exactly once.
    CHECK(count[0] == 1);
    CHECK(count[1] == 1);
    CHECK(count[2] == 1);

}

TEST_FIXTURE(RemoveTest, LuaFixture)
{

    lua_pushinteger(L, 1);
    int start = lua_gettop(L);
    lua_pushinteger(L, 2);
    lua_pushinteger(L, 3);
    lua_pushinteger(L, 4);

    lua_remove(L, start);
    CHECK( lua_tointeger(L, start) == 2 );

    lua_remove(L, -1);
    CHECK( lua_tointeger(L, -1) == 3 );

}

TEST_FIXTURE(Metatable, LuaFixture)
{

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

}

TEST_FIXTURE(NewMetatable, LuaFixture)
{

    CHECK( luaL_newmetatable(L, "test") == 1 );
    CHECK( lua_istable(L, -1) );
    lua_pop(L, 1);

    lua_getfield(L, LUA_REGISTRYINDEX, "test");
    CHECK( lua_istable(L, -1) );
    lua_pop(L, 1);

    CHECK( luaL_newmetatable(L, "test") == 0 );
    CHECK( lua_istable(L, -1) );
    lua_pop(L, 1);

}

TEST_FIXTURE(DefaultEnvTable, LuaFixture)
{
    // Check that the globals table is used as the default environment.
    lua_newuserdata(L, 10);
    lua_getfenv(L, -1);
    CHECK( lua_istable(L, -1) );
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    CHECK( lua_rawequal(L, -1, -2) );
    lua_pop(L, 3);
}

TEST_FIXTURE(EnvTable, LuaFixture)
{

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

}

TEST_FIXTURE(SetFEnv, LuaFixture)
{

    const char* code = 
        "A = 5\n"
        "function F() return A end\n"
        "setfenv(F, {A=12})\n"
        "B = F()";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "B");
    CHECK_EQ( lua_tonumber(L, -1), 12 );

}

TEST_FIXTURE(SetMetatableUserData, LuaFixture)
{

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

}

TEST_FIXTURE(SetMetatableNil, LuaFixture)
{

    lua_newuserdata(L, 10);
    int object = lua_gettop(L);

    int top = lua_gettop(L);
    lua_pushnil(L);
    lua_setmetatable(L, object);
    CHECK( lua_gettop(L) - top == 0);

    top = lua_gettop(L);
    CHECK( lua_getmetatable(L, object) == 0 );
    CHECK( lua_gettop(L) - top == 0 );

}

TEST_FIXTURE(CClosure, LuaFixture)
{

    struct Locals
    {
        static int Function(lua_State* L)
        {
            Locals* locals = static_cast<Locals*>(lua_touserdata(L, lua_upvalueindex(1)));
            lua_Integer a = lua_tointeger(L, lua_upvalueindex(2));
            lua_Integer b = lua_tointeger(L, lua_upvalueindex(3));
            CHECK( a == 10 );
            CHECK( b == 20 );
            CHECK( lua_isnone(L, lua_upvalueindex(4)) == 1 );
            locals->called = true;
            return 0;
        }
        bool called;
    };

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

}

TEST_FIXTURE(LightUserData, LuaFixture)
{

    void* p = reinterpret_cast<void*>(0x12345678);

    lua_pushlightuserdata(L, p);

    CHECK( lua_type(L, -1) == LUA_TLIGHTUSERDATA );
    CHECK( lua_touserdata(L, -1) == p );

    lua_pop(L, 1);

}

TEST_FIXTURE(UserData, LuaFixture)
{

    void* buffer = lua_newuserdata(L, 10);
    CHECK( buffer != NULL );

    CHECK( lua_type(L, -1) == LUA_TUSERDATA );
    CHECK( lua_touserdata(L, -1) == buffer );

    lua_pop(L, 1);

}

TEST_FIXTURE(NewIndexMetamethod, LuaFixture)
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

}

TEST_FIXTURE(GetUpValueCFunction, LuaFixture)
{

    struct Locals
    {
        static int F(lua_State* L)
        {
            return 0;
        }
    };

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

}

TEST_FIXTURE(GetUpValueLuaFunction, LuaFixture)
{

    const char* code =
        "local a = 'test1'\n"
        "local b = 'test2'\n"
        "function f()\n"
        "  local x = a\n"
        "  local y = b\n"
        "end";

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

}

TEST_FIXTURE(GetInfo, LuaFixture)
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

    Locals locals;

    lua_pushlightuserdata(L, &locals);
    lua_pushcclosure(L, &Locals::F, 1);
    lua_call(L, 0, 0);

}

TEST_FIXTURE(GetInfoF, LuaFixture)
{

    // Test the lua_getinfo function with the "f" parameter.

    struct Locals
    {
        static int F(lua_State* L)
        {
            Locals* locals = static_cast<Locals*>(lua_touserdata(L, lua_upvalueindex(1)));

            lua_Debug ar;

            // Top of the stack should be this function.
            if (!lua_getstack(L, 0, &ar))
            {
                locals->success = false;
                return 0;
            }
            lua_getinfo(L, "f", &ar);
            if (lua_tocfunction(L, -1) != F)
            {
                locals->success = false;
                return 0;
            }
            lua_pop(L, 1);

            // Next on the stack should be our Lua function.
            if (!lua_getstack(L, 1, &ar))
            {
                locals->success = false;
                return 0;
            }
            lua_getinfo(L, "f", &ar);
            lua_getglobal(L, "G");
            if (!lua_rawequal(L, -1, -2))
            {
                locals->success = false;
                return 0;
            }
            lua_pop(L, 1);

            locals->success = true;
            return 0;

        }
        bool success;
    };

    Locals locals;
    locals.success = false;

    lua_pushlightuserdata(L, &locals);
    lua_pushcclosure(L, &Locals::F, 1);
    lua_setglobal(L, "F");

    const char* code =
        "function G() F() end\n"
        "G()";

    CHECK( DoString(L, code) );
    CHECK( locals.success );

}

TEST_FIXTURE(MultipleAssignment, LuaFixture)
{

   const char* code =
        "a, b, c = 1, 2";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 1 );

    lua_getglobal(L, "b");
    CHECK( lua_tonumber(L, -1) == 2 );

    lua_getglobal(L, "c");
    CHECK( lua_isnil(L, -1) );

}

TEST_FIXTURE(MultipleAssignment2, LuaFixture)
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

    lua_register(L, "F", Locals::Function);
    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 1.0 );

    lua_getglobal(L, "b");
    CHECK( lua_tonumber(L, -1) == 2.0 );

    lua_getglobal(L, "c");
    CHECK( lua_tonumber(L, -1) == 3.0 );

}

TEST_FIXTURE(MultipleAssignment3, LuaFixture)
{

    const char* code =
        "local a = 10\n"
        "a, b = 5, a";
   
    CHECK( DoString(L, code) );

    lua_getglobal(L, "b");
    CHECK_EQ( lua_tonumber(L, -1), 10 );

}

TEST_FIXTURE(MultipleAssignment4, LuaFixture)
{

    const char* code =
        "local a = 10\n"
        "b, a = a, 5";
   
    CHECK( DoString(L, code) );

    lua_getglobal(L, "b");
    CHECK_EQ( lua_tonumber(L, -1), 10 );

}

TEST_FIXTURE(MultipleAssignment5, LuaFixture)
{
    const char* code =
        "x = 1, 2";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "x");
    CHECK_EQ( lua_tonumber(L, -1), 1 );
}

TEST_FIXTURE(MultipleAssignment6, LuaFixture)
{
    const char* code = 
        "local a = { }\n"
        "a[1], a = 1, 1";
    CHECK( DoString(L, code) );
}

TEST_FIXTURE(AssignmentSideEffect, LuaFixture)
{

   const char* code =
        "function F() b = 2 end\n"
        "a = 1, F(), 3";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 1 );

    lua_getglobal(L, "b");
    CHECK( lua_tonumber(L, -1) == 2 );

}

TEST_FIXTURE(LocalMultipleAssignment, LuaFixture)
{

   const char* code =
        "local _a, _b, _c = 1, 2\n"
        "a = _a\n"
        "b = _b\n"
        "c = _c\n";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 1 );

    lua_getglobal(L, "b");
    CHECK( lua_tonumber(L, -1) == 2 );

    lua_getglobal(L, "c");
    CHECK( lua_isnil(L, -1) );

}

TEST_FIXTURE(LocalMultipleAssignment2, LuaFixture)
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

    lua_register(L, "F", Locals::Function);
    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 1.0 );

    lua_getglobal(L, "b");
    CHECK( lua_tonumber(L, -1) == 2.0 );

    lua_getglobal(L, "c");
    CHECK( lua_tonumber(L, -1) == 3.0 );

}

TEST_FIXTURE(LocalMultipleAssignment3, LuaFixture)
{

   const char* code =
        "local _a, _b = 1, _a\n"
        "a = _a\n"
        "b = _b";

    CHECK( DoString(L, code) );
    
    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 1.0 );

    // a should not be visible until after the declaration is complete.
    lua_getglobal(L, "b");
    CHECK( lua_isnil(L, -1) );

}

TEST_FIXTURE(AssignmentNoValue, LuaFixture)
{

    const char* code =
        "function g() end\n"
        "function f() return g() end\n"
        "a = f()";

    CHECK( DoString(L, code) );
    
    lua_getglobal(L, "a");
    CHECK( lua_isnil(L, -1) );

}

TEST_FIXTURE(TableConstructor, LuaFixture)
{
    const char* code =
        "t = { 'one', three = 3, 'two', [2 + 2] = 'four', (function () return 3 end)() }";

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

}

TEST_FIXTURE(TableConstructorVarArg, LuaFixture)
{

    const char* code =
        "function f(...)\n"
        "  t = { 'zero', ... }\n"
        "end\n"
        "f('one', 'two', 'three')";

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

}

TEST_FIXTURE(TableConstructorFunction1, LuaFixture)
{

    const char* code =
        "function f()\n"
        "  return 'one', 'two', 'three'\n"
        "end\n"
        "t = { 'zero', f() }";

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

}

TEST_FIXTURE(TableConstructorFunction2, LuaFixture)
{

    const char* code =
        "function f()\n"
        "  return 'success'\n"
        "end\n"
        "t = { f('test') }";

    CHECK( DoString(L, code) );
    
    lua_getglobal(L, "t");
    lua_rawgeti(L, -1, 1);
    CHECK_EQ( lua_tostring(L, -1), "success" );

}

TEST_FIXTURE(TableConstructorTrailingComma, LuaFixture)
{

    // Lua allows for a trailing comma in a table, even though it doesn't
    // actually syntatically make sense.

    const char* code =
        "t = { 'one', }";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "t");
    CHECK( lua_istable(L, -1) == 1 );

    lua_rawgeti(L, -1, 1);
    CHECK( lua_isstring(L, -1) );
    CHECK( strcmp(lua_tostring(L, -1), "one") == 0 );
    lua_pop(L, 1);

}

TEST_FIXTURE(TableConstructorFunctionTrailingComma, LuaFixture)
{

    const char* code =
        "function f()\n"
        "  return 'one', 'two', 'three'\n"
        "end\n"
        "t = { 'zero', f(), }";

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

}

TEST_FIXTURE(EmptyReturn, LuaFixture)
{

    // Test parsing of an empty return from a block

    const char* code =
        "do\n"
        "  return\n"
        "end\n";

    CHECK( DoString(L, code) );
    
}

TEST_FIXTURE(Return, LuaFixture)
{

    const char* code =
        "function Foo()\n"
        "  return 5\n"
        "end\n"
        "v = Foo()";

    CHECK( DoString(L, code) );
    
    lua_getglobal(L, "v");
    CHECK( lua_tonumber(L, -1) == 5.0 );
    
}

TEST_FIXTURE(ReturnMultiple, LuaFixture)
{

    const char* code =
        "function Foo()\n"
        "  return 5, 6\n"
        "end\n"
        "v1, v2 = Foo()";

    CHECK( DoString(L, code) );
    
    lua_getglobal(L, "v1");
    CHECK( lua_tonumber(L, -1) == 5.0 );

    lua_getglobal(L, "v2");
    CHECK( lua_tonumber(L, -1) == 6.0 );
    
}

TEST_FIXTURE(ReturnEmpty, LuaFixture)
{

    const char* code =
        "function Foo()\n"
        "  return ;\n"
        "end";

    CHECK( DoString(L, code) );

}

TEST_FIXTURE(ReturnTopLevel, LuaFixture)
{

    const char* code =
        "return 'result'";

    CHECK( luaL_loadstring(L, code) == 0);
    CHECK( lua_pcall(L, 0, 1, 0) == 0);
    CHECK_EQ( lua_tostring(L, -1), "result" );
    lua_pop(L, 1);

}

TEST_FIXTURE(FunctionStringArgument, LuaFixture)
{

    const char* code =
        "function Foo(arg1, arg2)\n"
        "  s = arg1\n"
        "  n = arg2\n"
        "end\n"
        "Foo 'test'";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "s");
    CHECK( lua_isstring(L, -1) );
    CHECK( strcmp(lua_tostring(L, -1), "test") == 0 );
    
    lua_getglobal(L, "n");
    CHECK( lua_isnil(L, -1) );

}

TEST_FIXTURE(FunctionTableArgument, LuaFixture)
{

    const char* code =
        "function Foo(arg1, arg2)\n"
        "  t = arg1\n"
        "  n = arg2\n"
        "end\n"
        "Foo { }";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "t");
    CHECK( lua_istable(L, -1) );
    
    lua_getglobal(L, "n");
    CHECK( lua_isnil(L, -1) );

}

TEST_FIXTURE(FunctionMethod, LuaFixture)
{

    const char* code =
        "result = false\n"
        "Foo = { }\n"
        "function Foo:Bar()\n"
        "  if self == Foo then result = true end\n"
        "end\n"
        "Foo:Bar()\n";

    CHECK( DoString(L, code) );
    
    // Check that the function was properly created.
    lua_getglobal(L, "Foo");
    lua_getfield(L, -1, "Bar");
    CHECK( lua_type(L, -1) == LUA_TFUNCTION );
    lua_pop(L, 2);

    // Check that the function was properly called.
    lua_getglobal(L, "result");
    CHECK( lua_toboolean(L, -1) == 1 );
    
}

TEST_FIXTURE(FunctionMethodStringArg, LuaFixture)
{

    const char* code =
        "result = false\n"
        "Foo = { }\n"
        "function Foo:Bar(_arg)\n"
        "  if self == Foo then result = true end\n"
        "  arg = _arg\n"
        "end\n"
        "Foo:Bar 'test'\n";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "result");
    CHECK( lua_toboolean(L, -1) == 1 );

    lua_getglobal(L, "arg");
    CHECK( lua_isstring(L, -1) );
    CHECK( strcmp(lua_tostring(L, -1), "test") == 0 );
    
}

TEST_FIXTURE(FunctionDefinition, LuaFixture)
{
    const char* code = "function Foo() end";

    CHECK( luaL_dostring(L, code) == 0 );

    lua_getglobal(L, "Foo");
    CHECK( lua_type(L, -1) == LUA_TFUNCTION );

}

TEST_FIXTURE(ScopedFunctionDefinition, LuaFixture)
{
    
    const char* code = 
        "Foo = { }\n"
        "Foo.Bar = { }\n"
        "function Foo.Bar.Baz() end";

    CHECK( luaL_dostring(L, code) == 0 );
    
    lua_getglobal(L, "Foo");
    lua_getfield(L, -1, "Bar");
    lua_getfield(L, -1, "Baz");
    
    CHECK( lua_type(L, -1) == LUA_TFUNCTION );
    
}

TEST_FIXTURE(FunctionMethodDefinition, LuaFixture)
{

    const char* code =
        "Foo = { }\n"
        "function Foo:Bar() end";

    CHECK( luaL_dostring(L, code) == 0 );
    
    lua_getglobal(L, "Foo");
    lua_getfield(L, -1, "Bar");
    CHECK( lua_type(L, -1) == LUA_TFUNCTION );

}

TEST_FIXTURE(LocalFunctionDefinition, LuaFixture)
{

    const char* code =
        "local function Foo() end\n"
        "Bar = Foo";

    CHECK( luaL_dostring(L, code) == 0 );
    
    lua_getglobal(L, "Bar");
    CHECK( lua_type(L, -1) == LUA_TFUNCTION );

    lua_getglobal(L, "Foo");
    CHECK( lua_type(L, -1) == LUA_TNIL );

}

TEST_FIXTURE(LocalScopedFunctionDefinition, LuaFixture)
{
    
    const char* code = 
        "Foo = { }\n"
        "local function Foo.Bar() end";

    // Scoping makes no sense when we're defining a local.
    CHECK( luaL_dostring(L, code) == 1 );

}

TEST_FIXTURE(LocalMethod, LuaFixture)
{

    const char* code =
        "local t = { }\n"
        "function t:c() return { d = 5 } end\n"
        "a = t:c().d";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 5 );

}

TEST_FIXTURE(WhileLoop, LuaFixture)
{

    const char* code = 
        "result = 'failure'\n"
        "index = 0\n"
        "while index < 10 do\n"
        "  index = index + 1\n"
        "end\n"
        "result = 'success'";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "index");
    CHECK( lua_type(L, -1) == LUA_TNUMBER );
    CHECK( lua_tointeger(L, -1) == 10 );

    // Make sure we execute the line immediately after the loop
    lua_getglobal(L, "result");
    CHECK_EQ( lua_tostring(L, -1), "success" );

}

TEST_FIXTURE(ForLoop1, LuaFixture)
{

    const char* code = 
        "result = 'failure'\n"
        "index = 0\n"
        "for i = 1,10 do\n"
        "  index = index + 1\n"
        "end\n"
        "result = 'success'";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "index");
    CHECK( lua_type(L, -1) == LUA_TNUMBER );
    CHECK( lua_tointeger(L, -1) == 10 );

    // The index for the loop shouldn't be in the global space.
    lua_getglobal(L, "i");
    CHECK( lua_isnil(L, -1) != 0 );

    // Make sure we execute the line immediately after the loop
    lua_getglobal(L, "result");
    CHECK_EQ( lua_tostring(L, -1), "success" );

}

TEST_FIXTURE(ForLoop2, LuaFixture)
{

    const char* code = 
        "result = 'failure'\n"
        "index = 0\n"
        "for i = 1,10,2 do\n"
        "  index = index + 1\n"
        "end\n"
        "result = 'success'";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "index");
    CHECK( lua_type(L, -1) == LUA_TNUMBER );
    CHECK( lua_tointeger(L, -1) == 5 );

    // The index for the loop shouldn't be in the global space.
    lua_getglobal(L, "i");
    CHECK( lua_isnil(L, -1) != 0 );

    // Make sure we execute the line immediately after the loop
    lua_getglobal(L, "result");
    CHECK_EQ( lua_tostring(L, -1), "success" );

}

TEST_FIXTURE(ForLoop3, LuaFixture)
{

    const char* code = 
        "result = 'failure'\n"
        "values = { first=1, second=2 }\n"
        "results = { }\n"
        "index = 0\n"
        "for k,v in pairs(values) do\n"
        "  index = index + 1\n"
        "  results[v] = k\n"
        "end\n"
        "result = 'success'";

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

    // Make sure we execute the line immediately after the loop
    lua_getglobal(L, "result");
    CHECK_EQ( lua_tostring(L, -1), "success" );

}

TEST_FIXTURE(ForLoop4, LuaFixture)
{

    const char* code =
        "result = 'failure'\n"
        "_t = { 'one', 'two', 'three' }\n"
        "t = { }\n"
        "num = 1\n"
        "for i, v in ipairs(_t) do\n"
        "  if i ~= num then break end\n"
        "  t[num] = v\n"
        "  num = num + 1\n"
        "end\n"
        "result = 'success'";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "t");
    int t = lua_gettop(L);
    
    lua_rawgeti(L, t, 1);
    CHECK_EQ( lua_tostring(L, -1), "one" );

    lua_rawgeti(L, t, 2);
    CHECK_EQ( lua_tostring(L, -1), "two" );

    lua_rawgeti(L, t, 3);
    CHECK_EQ( lua_tostring(L, -1), "three" );

    // Make sure we execute the line immediately after the loop
    lua_getglobal(L, "result");
    CHECK_EQ( lua_tostring(L, -1), "success" );

}

TEST_FIXTURE(ForLoop5, LuaFixture)
{

    const char* code = 
        "result = 'failure'\n"
        "index = 0\n"
        "for i = 9,0,-1 do\n"
        "  index = index + 1\n"
        "end\n"
        "result = 'success'";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "index");
    CHECK( lua_type(L, -1) == LUA_TNUMBER );
    CHECK( lua_tointeger(L, -1) == 10 );

    // The index for the loop shouldn't be in the global space.
    lua_getglobal(L, "i");
    CHECK( lua_isnil(L, -1) != 0 );

    // Make sure we execute the line immediately after the loop
    lua_getglobal(L, "result");
    CHECK_EQ( lua_tostring(L, -1), "success" );

}

TEST_FIXTURE(ForLoopScope, LuaFixture)
{

    const char* code =
        "i = 5\n"
        "n = 0\n"
        "for i=i,10 do\n"
	    "    n = i\n"
	    "    break\n"
        "end\n";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "n");
    CHECK_EQ( lua_tonumber(L, -1), 5.0 );

}

TEST_FIXTURE(ForLoopEraseWhileIterating, LuaFixture)
{

    const char* code =
        "local t = { 1, 2, 3, 4, 5 }\n"
        "n = 0\n"
        "for k, v in pairs( t ) do\n"
        "   n = n+1\n"
        "   t[k] = nil\n"
        "end";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "n");
    CHECK_EQ( lua_tonumber(L, -1), 5.0 );

}

TEST_FIXTURE(RepeatLoop, LuaFixture)
{

    const char* code = 
        "result = 'failure'\n"
        "index = 0\n"
        "repeat\n"
        "  index = index + 1\n"
        "until index == 10\n"
        "result = 'success'";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "index");
    CHECK( lua_type(L, -1) == LUA_TNUMBER );
    CHECK( lua_tointeger(L, -1) == 10 );

    // Make sure we execute the line immediately after the loop
    lua_getglobal(L, "result");
    CHECK_EQ( lua_tostring(L, -1), "success" );

}

TEST_FIXTURE(WhileLoopBreak, LuaFixture)
{

    const char* code = 
        "result = 'failure'\n"
        "index = 0\n"
        "while true do\n"
        "  index = index + 1\n"
        "  break\n"
        "end\n"
        "result = 'success'";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "index");
    CHECK( lua_type(L, -1) == LUA_TNUMBER );
    CHECK( lua_tointeger(L, -1) == 1 );

    // Make sure we execute the line immediately after the loop
    lua_getglobal(L, "result");
    CHECK_EQ( lua_tostring(L, -1), "success" );

}

TEST_FIXTURE(ForLoopBreak, LuaFixture)
{

    const char* code = 
        "result = 'failure'\n"
        "index = 0\n"
        "for i = 1,10 do\n"
        "  index = index + 1\n"
        "  break\n"
        "end\n"
        "result = 'success'";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "index");
    CHECK( lua_type(L, -1) == LUA_TNUMBER );
    CHECK( lua_tointeger(L, -1) == 1 );

    // Make sure we execute the line immediately after the loop
    lua_getglobal(L, "result");
    CHECK_EQ( lua_tostring(L, -1), "success" );

}

TEST_FIXTURE(RepeatLoopBreak, LuaFixture)
{

    const char* code = 
        "index = 0\n"
        "repeat\n"
        "  index = index + 1\n"
        "  break\n"
        "until index == 10";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "index");
    CHECK( lua_type(L, -1) == LUA_TNUMBER );
    CHECK( lua_tointeger(L, -1) == 1 );

}

TEST_FIXTURE(IllegalBreak, LuaFixture)
{
    const char* code = 
        "print('test')\n"
        "break";
    CHECK( luaL_loadstring(L, code) != 0 );
}

TEST_FIXTURE(FunctionCallStringArg, LuaFixture)
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

    lua_pushlightuserdata(L, &locals);
    lua_pushcclosure(L, Locals::Function, 1);
    lua_setglobal(L, "Function");

    CHECK( DoString(L, code) );
    CHECK( locals.passed );

}

TEST_FIXTURE(FunctionCallTableArgument, LuaFixture)
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

    lua_pushlightuserdata(L, &locals);
    lua_pushcclosure(L, Locals::Function, 1);
    lua_setglobal(L, "Function");

    CHECK( DoString(L, code) );
    CHECK( locals.passed );

}

TEST_FIXTURE(FunctionCallCompund, LuaFixture)
{

    const char* code =
        "function f(s)\n"
        "   a = s\n"
        "   return function(n) b = n end\n"
        "end\n"
        "f 'test' (5)";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK_EQ( lua_tostring(L, -1), "test");

    lua_getglobal(L, "b");
    CHECK( lua_tonumber(L, -1) == 5 );

}

TEST_FIXTURE(FunctionCallFromLocal, LuaFixture)
{

    // Check that calling a function doesn't corrupt the local variables.

    const char* code = 
        "local a, b, r = nil\n"
        "r = function() return 10, 20 end\n"
        "a, b = r()\n"
        "A, B = a, b";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "A");
    CHECK_EQ( lua_tonumber(L, -1), 10 );
    lua_pop(L, 1);
    
    lua_getglobal(L, "B");
    CHECK_EQ( lua_tonumber(L, -1), 20 );
    lua_pop(L, 1);

}

TEST_FIXTURE(LengthOperator, LuaFixture)
{

    const char* code =
        "t = { 1 }\n"
        "l = #t";

    CHECK( DoString(L, code) );
    
    lua_getglobal(L, "l");
    CHECK( lua_isnumber(L, -1) );
    CHECK( lua_tonumber(L, -1) == 1 );

}

TEST_FIXTURE(ConcatOperator, LuaFixture)
{

    const char* code =
        "s = 'a' .. 'b' .. 'c'";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "s");
    const char* s = lua_tostring(L, -1);

    CHECK( s != NULL );
    CHECK( strcmp(s, "abc") == 0 );

}

TEST_FIXTURE(ConcatOperatorNumber, LuaFixture)
{

    const char* code =
        "s = 4 .. 'h'";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "s");
    const char* s = lua_tostring(L, -1);

    CHECK( s != NULL );
    CHECK( strcmp(s, "4h") == 0 );

}

TEST_FIXTURE(VarArg1, LuaFixture)
{

    const char* code =
        "function g(a, b, ...)\n"
        "  w, x = ..., 5\n"
        "end\n"
        "g(1, 2, 3, 4)";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "w");
    CHECK( lua_tonumber(L, -1) == 3.0 );

    lua_getglobal(L, "x");
    CHECK( lua_tonumber(L, -1) == 5.0 );

}

TEST_FIXTURE(VarArg2, LuaFixture)
{

    const char* code =
        "function g(a, b, ...)\n"
        "  w, x = ...\n"
        "end\n"
        "g(1, 2, 3, 4)";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "w");
    CHECK( lua_tonumber(L, -1) == 3.0 );

    lua_getglobal(L, "x");
    CHECK( lua_tonumber(L, -1) == 4.0 );

}

TEST_FIXTURE(VarArg3, LuaFixture)
{

    const char* code =
        "function f(a, b, c)\n"
        "  x, y, z = a, b, c\n"
        "end\n"
        "function g(...)\n"
        "  f(...)\n"
        "end\n"
        "g(1, 2)";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "x");
    CHECK( lua_tonumber(L, -1) == 1.0 );

    lua_getglobal(L, "y");
    CHECK( lua_tonumber(L, -1) == 2.0 );

    lua_getglobal(L, "z");
    CHECK( lua_isnil(L, -1) );

}

TEST_FIXTURE(VarArg4, LuaFixture)
{

    const char* code =
        "function f(a, b, c)\n"
        "  x, y, z = a, b, c\n"
        "end\n"
        "function g(...)\n"
        "  f(..., 3)\n"
        "end\n"
        "g(1, 2)";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "x");
    CHECK( lua_tonumber(L, -1) == 1.0 );

    lua_getglobal(L, "y");
    CHECK( lua_tonumber(L, -1) == 3.0 );

    lua_getglobal(L, "z");
    CHECK( lua_isnil(L, -1) );

}

TEST_FIXTURE(VarArg5, LuaFixture)
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

    CHECK( DoString(L, code) );

    lua_getglobal(L, "x");
    CHECK( lua_tonumber(L, -1) == 1.0 );

    lua_getglobal(L, "y");
    CHECK( lua_tonumber(L, -1) == 2.0 );

}

TEST_FIXTURE(VarArg6, LuaFixture)
{

    // Test that ... can be used at file scope.

    const char* code =
        "a, b = ...";

    int result = luaL_loadstring(L, code);
    if (result != 0)
    {
        fprintf(stderr, "%s\n", lua_tostring(L, -1));
    }
    CHECK( result == 0 );

    lua_pushstring(L, "arg1");
    lua_pushstring(L, "arg2");
    CHECK( lua_pcall(L, 2, 0, 0) == 0 );

    lua_getglobal(L, "a");
    CHECK_EQ( lua_tostring(L, -1), "arg1" );

    lua_getglobal(L, "b");
    CHECK_EQ( lua_tostring(L, -1), "arg2" );

}

TEST_FIXTURE(VarArg7, LuaFixture)
{

    const char* code =
        "function f(...) return ... end\n"
        "local _x, _y = f(1, 2)\n"
        "x = _x\n"
        "y = _y";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "x");
    CHECK_EQ( lua_tonumber(L, -1), 1 );
    
    lua_getglobal(L, "y");
    CHECK_EQ( lua_tonumber(L, -1), 2 );

}

TEST_FIXTURE(DoBlock, LuaFixture)
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

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 1.0 );

    lua_getglobal(L, "b");
    CHECK( lua_isnil(L, -1)  );

}

TEST_FIXTURE(LocalUpValue, LuaFixture)
{

    const char* code =
        "local a = 1\n"
        "do\n"
        "  local p = 2\n"
        "  f = function() return p end\n"
        "end\n"
        "local b = 3\n"
        "c = f()\n";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "c");
    CHECK( lua_tonumber(L, -1) == 2.0 );

}

TEST_FIXTURE(ShadowLocalUpValue, LuaFixture)
{

    const char* code =
        "local a = 5\n"
        "function f()\n"
        "  v = a\n"
        "end\n"
        "local a = 6\n"
        "f()";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "v");
    CHECK( lua_tonumber(L, -1) == 5.0 );

}

TEST_FIXTURE(CloseUpValueOnReturn, LuaFixture)
{

    const char* code =
        "function f()\n"
	    "  local x = 'one'\n"
	    "  local g = function() return x end\n"
	    "  return g, 'two'\n" // second value will overwrite over x
        "end\n"
        "local g = f()\n"
        "n = g()";

    CHECK( DoString(L, code ) );

    lua_getglobal(L, "n");
    CHECK_EQ( lua_tostring(L, -1), "one" );

}

TEST_FIXTURE(EmptyStatement, LuaFixture)
{

    const char* code =
        "function g() end\n"
        "local a = f ; (g)()\n";

    CHECK( DoString(L, code) );

}

TEST_FIXTURE(CppCommentLine, LuaFixture)
{

    const char* code =
        "// this is a comment\n"
        "a = 1";

    CHECK( DoString(L, code) );
    
    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 1.0 );

}

TEST_FIXTURE(CppCommentBlock, LuaFixture)
{

    const char* code =
        "/* this is a comment\n"
        "that goes for multiple lines */\n"
        "a = 1\n";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 1.0 );

}

TEST_FIXTURE(LuaCommentBlock, LuaFixture)
{

    const char* code =
        "--[[ this is a comment\n"
        "this is the second line ]]\n"
        "a = 1";

    CHECK( DoString(L, code) );
    
    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 1.0 );

}

TEST_FIXTURE(Equal, LuaFixture)
{

    const char* code =
        "a = (5 == 6)\n"
        "b = (7 == 7)";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_isboolean(L, -1) );
    CHECK( lua_toboolean(L, -1) == 0 );
    
    lua_getglobal(L, "b");
    CHECK( lua_isboolean(L, -1) );
    CHECK( lua_toboolean(L, -1) == 1 );

}

TEST_FIXTURE(NotEqual, LuaFixture)
{

    const char* code =
        "a = (5 ~= 6)\n"
        "b = (7 ~= 7)";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_isboolean(L, -1) );
    CHECK( lua_toboolean(L, -1) == 1 );

    lua_getglobal(L, "b");
    CHECK( lua_isboolean(L, -1) );
    CHECK( lua_toboolean(L, -1) == 0 );

}

TEST_FIXTURE(Number, LuaFixture)
{

    const char* code =
        "a = 3\n"
        "b = 3.14\n"
        "c = -3.1416\n"
        "d = -.12\n"
        "e = 314.16e-2\n"
        "f = 0.31416E1\n"
        "g = 0xff\n"
        "h = 0x56";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 3.0 );

    lua_getglobal(L, "b");
    CHECK_CLOSE( lua_tonumber(L, -1), 3.14 ); 

    lua_getglobal(L, "c");
    CHECK_CLOSE( lua_tonumber(L, -1), -3.1416 );

    lua_getglobal(L, "d");
    CHECK_CLOSE( lua_tonumber(L, -1), -0.12);

    lua_getglobal(L, "e");
    CHECK_CLOSE( lua_tonumber(L, -1), 314.16e-2 );

    lua_getglobal(L, "f");
    CHECK_CLOSE( lua_tonumber(L, -1), 0.31416e1 );

    lua_getglobal(L, "g");
    CHECK_CLOSE( lua_tonumber(L, -1), 0xFF );

    lua_getglobal(L, "h");
    CHECK_CLOSE( lua_tonumber(L, -1), 0x56 );

}

TEST_FIXTURE(ElseIf, LuaFixture)
{

    const char* code =
        "if false then\n"
        "elseif true then\n"
        "  success = true\n"
        "end";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "success");
    CHECK( lua_toboolean(L, -1) );

}

TEST_FIXTURE(DivideOperator, LuaFixture)
{

    const char* code =
        "a = 10 / 2";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 5 );

}

TEST_FIXTURE(SubtractOperator, LuaFixture)
{

    // Check that the subtraction symbol is properly parsed and
    // not treated as part of the 4.
    const char* code =
        "v = 6-4";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "v");
    CHECK( lua_tonumber(L, -1) == 2 );

}

TEST_FIXTURE(UnaryMinusOperator, LuaFixture)
{

    const char* code =
        "local x = 5\n" 
        "y = -x";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "y");
    CHECK( lua_tonumber(L, -1) == -5 );

}

TEST_FIXTURE(UnaryMinusOperator2, LuaFixture)
{

    const char* code =
        "local x = 5\n" 
        "y = - -x";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "y");
    CHECK( lua_tonumber(L, -1) == 5 );

}

TEST_FIXTURE(UnaryMinusOperatorConstant, LuaFixture)
{

    const char* code =
        "x = -5";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "x");
    CHECK( lua_tonumber(L, -1) == -5 );

}

TEST_FIXTURE(ModuloOperator, LuaFixture)
{

    const char* code =
        "a = 10 % 3";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 1 );

}

TEST_FIXTURE(ExponentiationOperator, LuaFixture)
{

    const char* code =
        "a = 2 ^ 3";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 8 );

}

TEST_FIXTURE(EscapeCharacters, LuaFixture)
{

    const char* code =
        "b = '\\01a\\002\\a\\b\\f\\n\\r\\t\\v\\\"\\\'\\0001'";

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
    CHECK( buffer[12] == '\0' );
    CHECK( buffer[13] == '1' );

}

TEST_FIXTURE(InvalidEscapeCharacters, LuaFixture)
{

    const char* code =
        "b = '\\xyz";

    CHECK( luaL_loadstring(L, code) != 0 );

}

TEST_FIXTURE(NilConstant, LuaFixture)
{

    const char* code =
        "c = nil\n"
        "if a == nil then\n"
        "  b = 5\n"
        "end\n";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "b");
    CHECK( lua_tonumber(L, -1) == 5 );

    lua_getglobal(L, "c");
    CHECK( lua_isnil(L, -1) );

}

TEST_FIXTURE(ContinuedString, LuaFixture)
{

    const char* code =
        "a = 'one\\\ntwo'";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_isstring(L, -1) );
    CHECK_EQ( lua_tostring(L, -1), "one\ntwo" );

}

TEST_FIXTURE(LongString1, LuaFixture)
{

    const char* code =
        "a = [[one\ntwo]]";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_isstring(L, -1) );
    CHECK_EQ( lua_tostring(L, -1), "one\ntwo" );

}

TEST_FIXTURE(LongString2, LuaFixture)
{

    const char* code =
        "a = [=[one\ntwo]=]";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_isstring(L, -1) );
    CHECK_EQ( lua_tostring(L, -1), "one\ntwo" );

}

TEST_FIXTURE(LongStringNested, LuaFixture)
{

    const char* code =
        "a = [=[one\n[==[embed]==]two]=]";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_isstring(L, -1) );
    CHECK_EQ( lua_tostring(L, -1), "one\n[==[embed]==]two" );

}

TEST_FIXTURE(LongStringInitialNewLine, LuaFixture)
{

    const char* code =
        "a = [[\ntest]]";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_isstring(L, -1) );
    CHECK_EQ( lua_tostring(L, -1), "test" );

}

TEST_FIXTURE(LongStringIgnoreClose, LuaFixture)
{

    const char* code =
        "a = [===[]=]===]";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_isstring(L, -1) );
    CHECK_EQ( lua_tostring(L, -1), "]=" );

}

TEST_FIXTURE(Closure, LuaFixture)
{

    const char* code =
        "local l = 5\n"
        "function f()\n"
        "  v = l\n"
        "end\n"
        "f()";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "v");
    CHECK( lua_tonumber(L, -1) == 5 );

}

TEST_FIXTURE(ClosureInClosure, LuaFixture)
{

    const char* code =
        "local l = 5\n"
        "function f()\n"
        "  local function g() v = l end\n"
        "  g()\n"
        "end\n"
        "f()";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "v");
    CHECK( lua_tonumber(L, -1) == 5 );

}

TEST_FIXTURE(ManyConstants, LuaFixture)
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

    CHECK( DoString(L, code) );

}

TEST_FIXTURE(LocalInit, LuaFixture)
{
    
    const char* code =
        "function f() end\n"
        "f(1, 2, 3, 4, 5, 6)\n"
        "do\n"
        "  local _a\n"
        "  a = _a\n"
        "end";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_isnil(L, -1) );
    
}

TEST_FIXTURE(LocalInit2, LuaFixture)
{

    const char* code = 
        "function f()\n"
        "  local x\n"
        "  return x\n"
        "end\n"
        "a = f(4)";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_isnil(L, -1) );

}

TEST_FIXTURE(OperatorPrecedence, LuaFixture)
{
    
    const char* code =
        "a = 1 + 2 * 3\n"
        "b = 1 + 4 / 2\n"
        "c = 1 - 2 * 3\n"
        "d = 1 - 4 / 2\n"
        "e = 2 * -3 ^ 4 * 5\n"
        "f = 'a' .. 'b' == 'ab'\n"
        "g = false and true or true";

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

    lua_getglobal(L, "f");
    CHECK( lua_toboolean(L, -1) );

    lua_getglobal(L, "g");
    CHECK( lua_toboolean(L, -1) );

}

TEST_FIXTURE(OperatorPrecedenceRightAssociative, LuaFixture)
{
    const char* code =
        "assert( true or false and nil )";
    CHECK( DoString(L, code) );
}

TEST_FIXTURE(CallPreserveStack, LuaFixture)
{

    // This test checks that calling a function doesn't corrupt the stack
    // downstream.

    const char* code =     
        "function F()\n"
        "end\n"
        "local t = { }\n"
        "local mt = { __newindex = F  }\n"
        "setmetatable(t, mt)\n"
        "F()\n"
        "local _a = 1\n"
        "t.test = 5\n"
        "a = _a";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_tonumber(L, -1) == 1.0 );

}

TEST_FIXTURE(FunctionUpValue, LuaFixture)
{

    const char* code =
        "local a = 1\n"
        "function F()\n"
        "  _b = a\n"
        "  a = 2\n"
        "end\n"
        "F()\n"
        "_a = a";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "_a");
    CHECK( lua_tonumber(L, -1) == 2.0 );

    lua_getglobal(L, "_b");
    CHECK( lua_tonumber(L, -1) == 1.0 );

}

TEST_FIXTURE(Not0, LuaFixture)
{
    const char* code = "a = not 0";
    CHECK( DoString(L, code) );
    lua_getglobal(L, "a");
    CHECK( lua_isboolean(L, -1) );
    CHECK( lua_toboolean(L, -1) == 0 );
}

TEST_FIXTURE(NotNumber, LuaFixture)
{
    const char* code = "a = not 1";
    CHECK( DoString(L, code) );
    lua_getglobal(L, "a");
    CHECK( lua_isboolean(L, -1) );
    CHECK( lua_toboolean(L, -1) == 0 );
}

TEST_FIXTURE(NotNil, LuaFixture)
{
    const char* code = "a = not nil";
    CHECK( DoString(L, code) );
    lua_getglobal(L, "a");
    CHECK( lua_isboolean(L, -1) );
    CHECK( lua_toboolean(L, -1) == 1);
}

TEST_FIXTURE(NotString, LuaFixture)
{
    const char* code = "a = not 'test'";
    CHECK( DoString(L, code) );
    lua_getglobal(L, "a");
    CHECK( lua_isboolean(L, -1) );
    CHECK( lua_toboolean(L, -1) == 0);
}

TEST_FIXTURE(NotFalse, LuaFixture)
{
    const char* code = "a = not false";
    CHECK( DoString(L, code) );
    lua_getglobal(L, "a");
    CHECK( lua_isboolean(L, -1) );
    CHECK( lua_toboolean(L, -1) == 1);
}

TEST_FIXTURE(NotTrue, LuaFixture)
{
    const char* code = "a = not true";
    CHECK( DoString(L, code) );
    lua_getglobal(L, "a");
    CHECK( lua_isboolean(L, -1) );
    CHECK( lua_toboolean(L, -1) == 0);
}

TEST_FIXTURE(NotAnd1, LuaFixture)
{
    const char* code =
        "local t\n"
        "a = not t and 'test'";
    CHECK( DoString(L, code) );
    lua_getglobal(L, "a");
    CHECK_EQ( lua_tostring(L, -1), "test" );
}

TEST_FIXTURE(NotAnd2, LuaFixture)
{
    const char* code =
        "a = not true and true";
    CHECK( DoString(L, code) );
    lua_getglobal(L, "a");
    CHECK( !lua_toboolean(L, -1) );
}

TEST_FIXTURE(EqualAndTrue, LuaFixture)
{
    const char* code =
        "a = (1 == 2) and true";
    CHECK( DoString(L, code) );
    lua_getglobal(L, "a");
    CHECK( !lua_toboolean(L, -1) );
}

TEST_FIXTURE(EqualAndFalse, LuaFixture)
{
    const char* code =
        "a = (1 == 1) and false";
    CHECK( DoString(L, code) );
    lua_getglobal(L, "a");
    CHECK( !lua_toboolean(L, -1) );
}

TEST_FIXTURE(AndEqual, LuaFixture)
{
    const char* code =
        "a = false and (1 == 1)";
    CHECK( DoString(L, code) );
    lua_getglobal(L, "a");
    CHECK( !lua_toboolean(L, -1) );
}

TEST_FIXTURE(NotOr1, LuaFixture)
{
    const char* code =
        "local t\n"
        "a = not t or 'test'";
    CHECK( DoString(L, code) );
    lua_getglobal(L, "a");
    CHECK( lua_isboolean(L, -1) );
    CHECK( lua_toboolean(L, -1) == 1 );
}

TEST_FIXTURE(NegativeOr, LuaFixture)
{
    const char* code =
        "a = -(1 or 2)";
    CHECK( DoString(L, code) );
    lua_getglobal(L, "a");
    CHECK_EQ( lua_tonumber(L, -1), -1 );
}

TEST_FIXTURE(OrLhs, LuaFixture)
{
    const char* code =
        "local a\n"
        "t = { } ;\n"
        "(a or t).x = 5";
    CHECK( DoString(L, code) );
    lua_getglobal(L, "t");
    lua_getfield(L, -1, "x");
    CHECK( lua_tonumber(L, -1) == 5 );
}

TEST_FIXTURE(PrefixExp, LuaFixture)
{
    // This should not be treated as a function call.
    const char* code =
        "local a = nil\n"
        "(function () end)()";
    CHECK( DoString(L, code) );
}

TEST_FIXTURE(Dump, LuaFixture)
{

    struct Buffer
    {
        char    data[1024];
        size_t  length;
    };

    struct Locals
    {
        static int Writer(lua_State* L, const void* p, size_t sz, void* ud)
        {
            Buffer* buffer = static_cast<Buffer*>(ud);
            char* dst = buffer->data + buffer->length;
            buffer->length += sz;
            if (buffer->length > sizeof(buffer->data))
            {
                return 1;
            }
            memcpy( dst, p, sz );
            return 0;
        }
    };

    Buffer buffer;
    buffer.length = 0;

    luaL_loadstring(L, "a = 'test'");
    int top = lua_gettop(L);
    CHECK( lua_dump(L, &Locals::Writer, &buffer) == 0 );
    CHECK( lua_gettop(L) == top );

    lua_pop(L, 1);
    CHECK( luaL_loadbuffer(L, buffer.data, buffer.length, "mem") == 0 );
    CHECK( lua_pcall(L, 0, 0, 0) == 0 );

    lua_getglobal(L, "a");
    CHECK_EQ( lua_tostring(L, -1), "test" );
    lua_pop(L, 1);

}

TEST_FIXTURE(TableFromUnpack, LuaFixture)
{
    const char* code =
        "local x, a\n"
        "a = { 1, 2, 3 }\n"
        "x = { unpack(a) }"
        "n = #x";

    CHECK( DoString(L, code) );
    
    lua_getglobal(L, "n");
    CHECK_EQ( lua_tonumber(L, -1), 3 );
}

TEST_FIXTURE(ReturnVarFunction, LuaFixture)
{

    // Check that a properly return a variable number of return values
    // from a function.

    const char* code =
        "function g()\n"
        "  return 2, 3, 4\n"
        "end\n"
        "function f()\n"
        "  return 1, g()\n"
        "end\n"
        "a, b, c, d = f()\n";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK_EQ( lua_tonumber(L, -1), 1 );

    lua_getglobal(L, "b");
    CHECK_EQ( lua_tonumber(L, -1), 2 );

    lua_getglobal(L, "c");
    CHECK_EQ( lua_tonumber(L, -1), 3 );

    lua_getglobal(L, "d");
    CHECK_EQ( lua_tonumber(L, -1), 4 );

}

TEST_FIXTURE(ReturnLogic, LuaFixture)
{

    const char* code =
        "function f(a,b)\n"
	    "  return a or b\n"
        "end\n"
        "a = f(true, false)";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK( lua_toboolean(L, -1) );

}

TEST_FIXTURE(ReturnStackCleanup, LuaFixture)
{

    // This function requires the stack to be cleaned up between each of the
    // return values.

    const char* code =
        "function f()\n"
        "  local x = 1\n"
        "  local y = 2\n"
        "  return x * 2 + y * 3, 4\n"
        "end\n"
        "a, b = f()";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK_EQ( lua_tonumber(L, -1), 8 );

    lua_getglobal(L, "b");
    CHECK_EQ( lua_tonumber(L, -1), 4 );

}

TEST_FIXTURE(Assign2, LuaFixture)
{

    const char* code =
        "function f() return 1,2,30,4 end\n"
        "function ret2(a,b) return a,b end\n"
        "local _a, _b, _c, _d\n"
        "_a, _b, _c, _d = ret2(f()), ret2(f())\n"
        "a = _a\n"
        "b = _b\n"
        "c = _c\n"
        "d = _d\n";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK_EQ( lua_tonumber(L, -1), 1 );

    lua_getglobal(L, "b");
    CHECK_EQ( lua_tonumber(L, -1), 1 );

    lua_getglobal(L, "c");
    CHECK_EQ( lua_tonumber(L, -1), 2 );

    lua_getglobal(L, "d");
    CHECK( lua_isnil(L, -1) );

}

TEST_FIXTURE( AdjustReturn, LuaFixture )
{

    // Placing an expression that generates multiple values in parentheses
    // will adjust it to a single value.

    const char* code =
        "function f() return 1, 2 end\n"
        "a, b = (f())";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "a");
    CHECK_EQ( lua_tonumber(L, -1), 1 );

    lua_getglobal(L, "b");
    CHECK( lua_isnil(L, -1) );
    
}

TEST_FIXTURE( AdjustReturnLogic, LuaFixture )
{

    // When calling a function as part of a logic expression, the number of
    // return values should be adjusted to 1.

    const char* code =
        "function f () return 1,2,3 end\n"
        "x1, x2 = true and f()\n"
        "y1, y2 = f() or false\n"
        "z1, z2 = false or f()";

    CHECK( DoString(L, code) );
    
    lua_getglobal(L, "x1");
    CHECK_EQ( lua_tonumber(L, -1), 1.0 );

    lua_getglobal(L, "x2");
    CHECK( lua_isnil(L, -1) );

    lua_getglobal(L, "y1");
    CHECK_EQ( lua_tonumber(L, -1), 1.0 );

    lua_getglobal(L, "y2");
    CHECK( lua_isnil(L, -1) );

    lua_getglobal(L, "z1");
    CHECK_EQ( lua_tonumber(L, -1), 1.0 );

    lua_getglobal(L, "z2");
    CHECK( lua_isnil(L, -1) );

}

TEST_FIXTURE( LoadTermination, LuaFixture )
{

    // Check that a reader setting the size to 0 (but returning a non-nil
    // value) will terminate loading.

    struct Locals
    {
        static const char* Reader(lua_State* L, void* data, size_t* size)
        {
            *size = 0;
            return "a = 'fail'";
        }
    };

    CHECK( lua_load(L, Locals::Reader, NULL, "test") == 0 );

}

TEST_FIXTURE( SetUpValue, LuaFixture )
{

    const char* code =
        "local a = 1\n"
        "function f() return a + 1 end";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "f");
    int funcindex = lua_gettop(L);

    int top = lua_gettop(L);
    lua_pushnumber(L, 10);
    const char* name = lua_setupvalue(L, funcindex, 1);
    CHECK( lua_gettop(L) == top );

    CHECK_EQ( name, "a" );

    lua_pushvalue(L, funcindex);
    lua_pcall(L, 0, 1, 0);
    CHECK_EQ( lua_tonumber(L, -1), 11 );
    lua_pop(L, 1);

}

TEST_FIXTURE( StringComparison, LuaFixture )
{

    const char* code =
        "t1 = 'alo' < 'alo1'\n"
        "t2 = '' < 'a'\n"
        "t3 = 'alo\\0alo' < 'alo\\0b'\n"
        "t4 = 'alo\\0alo\\0\\0' > 'alo\\0alo\\0'\n"
        "t5 = 'alo' < 'alo\\0'\n"
        "t6 = 'alo\\0' > 'alo'\n"
        "t7 = '\\0' < '\\1'\n"
        "t8 = '\\0\\0' < '\\0\\1'";

    CHECK( DoString(L, code) );

    lua_getglobal(L, "t1");
    CHECK( lua_toboolean(L, -1) );

    lua_getglobal(L, "t2");
    CHECK( lua_toboolean(L, -1) );

    lua_getglobal(L, "t3");
    CHECK( lua_toboolean(L, -1) );

    lua_getglobal(L, "t4");
    CHECK( lua_toboolean(L, -1) );

    lua_getglobal(L, "t5");
    CHECK( lua_toboolean(L, -1) );

    lua_getglobal(L, "t6");
    CHECK( lua_toboolean(L, -1) );

    lua_getglobal(L, "t7");
    CHECK( lua_toboolean(L, -1) );

    lua_getglobal(L, "t8");
    CHECK( lua_toboolean(L, -1) );

}

TEST_FIXTURE(ToNumber, LuaFixture)
{
    int top = lua_gettop(L);

    lua_pushnumber(L, 10.3 );
    CHECK( lua_isnumber(L, -1) );
    CHECK_EQ( lua_tonumber(L, -1), 10.3 );
    lua_pop(L, 1);
    CHECK( lua_gettop(L) == top );

    lua_pushstring(L, "10.3");
    CHECK( lua_isnumber(L, -1) );
    CHECK_EQ( lua_tonumber(L, -1), 10.3 );
    lua_pop(L, 1);
    CHECK( lua_gettop(L) == top );

    lua_pushboolean(L, 1);
    CHECK( !lua_isnumber(L, -1) );
    CHECK_EQ( lua_tonumber(L, -1), 0 );
    lua_pop(L, 1);
    CHECK( lua_gettop(L) == top );

    lua_pushlightuserdata(L, (void*)0x12345678);
    CHECK( !lua_isnumber(L, -1) );
    CHECK_EQ( lua_tonumber(L, -1), 0 );
    lua_pop(L, 1);
    CHECK( lua_gettop(L) == top );
}

TEST_FIXTURE(ToNumberFromString, LuaFixture)
{

    lua_pushstring(L, "10.3");
    CHECK( lua_isnumber(L, -1) );
    CHECK_EQ( lua_tonumber(L, -1), 10.3 );
    lua_pop(L, 1);

    lua_pushstring(L, "10.3 456");
    CHECK( !lua_isnumber(L, -1) );
    CHECK_EQ( lua_tonumber(L, -1), 0 );
    lua_pop(L, 1);

    lua_pushstring(L, "  10.3  ");
    CHECK( lua_isnumber(L, -1) );
    CHECK_EQ( lua_tonumber(L, -1), 10.3 );
    lua_pop(L, 1);

    lua_pushstring(L, "0x123");
    CHECK( lua_isnumber(L, -1) );
    CHECK_EQ( lua_tonumber(L, -1), 0x123 );
    lua_pop(L, 1);

    lua_pushstring(L, "  0x123  ");
    CHECK( lua_isnumber(L, -1) );
    CHECK_EQ( lua_tonumber(L, -1), 0x123 );
    lua_pop(L, 1);

    lua_pushstring(L, "123x");
    CHECK( !lua_isnumber(L, -1) );
    CHECK_EQ( lua_tonumber(L, -1), 0 );
    lua_pop(L, 1);

    lua_pushstring(L, "abcd");
    CHECK( !lua_isnumber(L, -1) );
    CHECK_EQ( lua_tonumber(L, -1), 0 );
    lua_pop(L, 1);

}

TEST_FIXTURE(StringNumberCoercion, LuaFixture)
{
    lua_pushnumber(L, 10);
    CHECK( lua_isstring(L, -1) );
}

TEST_FIXTURE(StringNumberCoercionArithmetic, LuaFixture)
{
    const char* code =
        "a = '1' + 2";
    CHECK( DoString(L, code) );
    lua_getglobal(L, "a");
    CHECK_EQ( lua_tonumber(L, -1), 3 );
}

TEST_FIXTURE(StringNumberCoercionForLoop, LuaFixture)
{
    const char* code =
        "a = 0\n"
        "for i='10','1','-2' do\n"
	    "    a = a + 1\n"
        "end\n";
    CHECK( DoString(L, code) );
    lua_getglobal(L, "a");
    CHECK_EQ( lua_tonumber(L, -1), 5 );
}

TEST_FIXTURE(Objlen, LuaFixture)
{

    lua_newtable(L);
    lua_pushstring(L, "one");
    lua_rawseti(L, -2, 1);
    lua_pushstring(L, "two");
    lua_rawseti(L, -2, 2);
    lua_pushstring(L, "three");
    lua_rawseti(L, -2, 3);

    CHECK( lua_objlen(L, -1) == 3 );
    lua_pop(L, 1);

    lua_newuserdata(L, 100);
    CHECK( lua_objlen(L, -1) == 100 );
    lua_pop(L, 1);

    lua_pushstring(L, "this is a test");
    CHECK( lua_objlen(L, -1) == 14 );
    lua_pop(L, 1);

    lua_pushnumber(L, 12);
    CHECK( lua_objlen(L, -1) == 2 );
    CHECK( lua_isstring(L, -1) );
    lua_pop(L, 1);

}