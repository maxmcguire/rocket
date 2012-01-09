/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */

#include "lua.h"
#include "State.h"
#include "BaseLib.h"
#include "Function.h"
#include "String.h"
#include "Table.h"
#include "Vm.h"
#include "Input.h"
#include "Code.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * Arguments that are passed into lua_load.
 */
struct ParseArgs
{
    lua_Reader      reader;
    void*           userdata;
    const char*     name;
};

static Value GetNil()
{
    Value nil;
    SetNil(&nil);
    return nil;
}

// Accepts negative and pseudo indices.
static Value* GetValueForIndex(lua_State* L, int index)
{
    static Value nil = GetNil();
    Value* result = NULL;
    if (index > 0)
    {
        result = L->stackBase + (index - 1);
        if (result >= L->stackTop)
        {
            result = &nil;
        }
    }
    else if (index > LUA_REGISTRYINDEX)
    {
        // ???
        result = L->stackTop + index;
    }
    else if (index == LUA_GLOBALSINDEX)
    {
        // Global.
        result = &L->globals;
    }
    else if (index == LUA_REGISTRYINDEX)
    {
        // Register.
        result = &L->registry;
    }
    else
    {
        // C up value.
        CallFrame* frame = State_GetCallFrame(L);
        Closure* closure = frame->function->closure;
        index = LUA_GLOBALSINDEX - index;
        if (index <= closure->cclosure.numUpValues)
        {
            return &closure->cclosure.upValue[index - 1];
        }
        return &nil;
    }
    assert(result != NULL);
    return result;
}

lua_State* lua_newstate(lua_Alloc alloc, void* userdata)
{
    lua_State* L = State_Create(alloc, userdata);
    OpenBaseLib(L);
    return L;
}

void lua_close(lua_State* L)
{
    State_Destroy(L);
}

static Prototype* LoadBinary(lua_State* L, Input* input, const char* name)
{
 
    struct Header
    {
        char            magic[4];
        unsigned char   version;
        unsigned char   format;
        unsigned char   endianness;
        unsigned char   intSize;
        unsigned char   sizetSize;
        unsigned char   instructionSize;
        unsigned char   numberSize;
        unsigned char   integralFlag;
    };
    assert( sizeof(Header) == 12 );

    Header header;
    size_t length = Input_ReadBlock(input, &header, sizeof(header));

    if (length < sizeof(Header))
    {
        return NULL;
    }

    // Check that the buffer is a compiled Lua chunk.
    if (header.magic[0] != '\033')
    {
        return NULL;
    }

    // Check for compatible platform
    if (header.endianness != 1 ||
        header.intSize != sizeof(int) ||
        header.sizetSize != sizeof(size_t) ||
        header.numberSize != sizeof(lua_Number))
    {
        return NULL;
    }

    char* data = Input_Read(input, &length);

    Prototype* prototype = Prototype_Create(L, data, length, name);
    Free(L, data, length);

    return prototype;

}

static int Parse(lua_State* L)
{

    ParseArgs* args = static_cast<ParseArgs*>(lua_touserdata(L, 1));

    Input input;
    Input_Initialize(L, &input, args->reader, args->userdata);

    Prototype* prototype = NULL;

    if (Input_PeekByte(&input) == '\033')
    {
        // The data is a pre-compiled binary.
        prototype = LoadBinary(L, &input, args->name);
    }
    else
    {
        prototype = Parse(L, &input, args->name);
    }

    assert(prototype != NULL);

    Closure* closure = Closure_Create(L, prototype);
    PushClosure(L, closure);

    return 1;

}

LUA_API int lua_load(lua_State* L, lua_Reader reader, void* userdata, const char* name)
{

    ParseArgs args;
    args.reader     = reader;
    args.userdata   = userdata;
    args.name       = name;

    // TODO: do this more directly rather than manipulating the Lua stack.
    lua_pushcfunction(L, Parse);
    lua_pushlightuserdata(L, &args);
    
    int result = lua_pcall(L, 1, 1, 0);
    if (result == LUA_ERRRUN)
    {
        result = LUA_ERRSYNTAX;
    }
    return result;

}

LUA_API int lua_error(lua_State* L)
{
    State_Error(L);
    return 0;
}

LUA_API void lua_call(lua_State* L, int nargs, int nresults)
{
    Value* value = L->stackTop - (nargs + 1);
    Vm_Call(L, value, nargs, nresults);
}

LUA_API int lua_pcall(lua_State* L, int numArgs, int numResults, int errFunc)
{
    Value* value = L->stackTop - (numArgs + 1);
    return ProtectedCall(L, value, numArgs, numResults, errFunc);
}

LUA_API void lua_pushnil(lua_State* L)
{
    PushNil(L);
}

LUA_API void lua_pushnumber(lua_State *L, lua_Number n)
{
    PushNumber(L, n);
}

LUA_API void lua_pushinteger (lua_State *L, lua_Integer n)
{
    PushNumber( L, static_cast<lua_Number>(n) );
}

LUA_API void lua_pushlstring(lua_State *L, const char* data, size_t length)
{
    String* string = String_Create(L, data, length);
    PushString(L, string);
}

LUA_API void lua_pushstring(lua_State* L, const char* data)
{
    if (data == NULL)
    {
        lua_pushnil(L);
        return;
    }
    lua_pushlstring(L, data, strlen(data)); 
}

LUA_API const char* lua_pushfstring(lua_State* L, const char* fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    const char* result = lua_pushvfstring(L, fmt, argp);
    va_end(argp);
    return result;
}

LUA_API const char* lua_pushvfstring(lua_State *L, const char *fmt, va_list argp)
{
    PushVFString(L, fmt, argp);
    return GetString(L->stackTop - 1);
}

LUA_API void lua_pushcclosure(lua_State *L, lua_CFunction f, int n)
{
    Closure* closure = Closure_Create(L, f, L->stackTop - n, n);
    Pop(L, n);
    PushClosure(L, closure);
}

LUA_API void lua_pushcfunction(lua_State* L, lua_CFunction f)
{
    lua_pushcclosure(L, f, 0);
}

LUA_API void lua_pushboolean(lua_State* L, int b)
{
    PushBoolean(L, b != 0);
}

LUA_API void lua_pushlightuserdata (lua_State *L, void *p)
{
    PushLightUserdata(L, p);
}

LUA_API void lua_pushvalue(lua_State* L, int index)
{
    const Value* value = GetValueForIndex(L, index);
    PushValue(L, value);
}

LUA_API void lua_pop(lua_State *L, int num)
{
    Pop(L, num);
}

LUA_API void lua_remove(lua_State *L, int index)
{
    Value* p = GetValueForIndex(L, index);
    Value* stackTop = L->stackTop;
    while (++p < stackTop)
    {
        *(p - 1) = *p;
    }
    --L->stackTop;
}

void lua_register(lua_State *L, const char *name, lua_CFunction f)
{
    lua_pushcfunction(L, f);
    lua_setglobal(L, name);
}

void lua_setfield(lua_State* L, int index, const char* name)
{
    Value key;
    SetValue( &key, String_Create(L, name, strlen(name)) );

    Value* table = GetValueForIndex(L, index);
    Vm_SetTable( L, table, &key, L->stackTop - 1 );
    Pop(L, 1);
}

void lua_getfield(lua_State *L, int index, const char* name)
{
    
    Value key;
    SetValue( &key, String_Create(L, name, strlen(name)) );
    
    Value* table = GetValueForIndex( L, index );
    const Value* value = Vm_GetTable(L, table, &key);

    if (value == NULL)
    {
        PushNil(L);
    }
    else
    {
        PushValue(L, value);
    }

}

void lua_setglobal(lua_State* L, const char* name)
{
    lua_setfield(L, LUA_GLOBALSINDEX, name);
}

void lua_getglobal(lua_State* L, const char* name)
{
    lua_getfield(L, LUA_GLOBALSINDEX, name);
}

int lua_isnumber(lua_State* L, int index)
{
    return lua_type(L, index) == LUA_TNUMBER;
}

int lua_isstring(lua_State* L, int index)
{
    return lua_type(L, index) == LUA_TSTRING;
}

int lua_iscfunction(lua_State* L, int index)
{
    const Value* value = GetValueForIndex(L, index);
    return Value_GetIsFunction(value) && value->closure->c;
}

int lua_isuserdata(lua_State* L, int index)
{
    return lua_type(L, index) == LUA_TUSERDATA;
}

int lua_isfunction(lua_State* L, int n)
{
    return lua_type(L, n) == LUA_TFUNCTION;
}

int lua_istable(lua_State* L, int n)
{
    return lua_type(L, n) == LUA_TTABLE;
}

int lua_islightuserdata(lua_State* L, int n)
{
    return lua_type(L, n) == LUA_TLIGHTUSERDATA;
}

int lua_isnil(lua_State* L, int n)
{
    return lua_type(L, n) == LUA_TNIL;
}

int lua_isboolean(lua_State* L, int n)
{
    return lua_type(L, n) == LUA_TBOOLEAN;
}

int lua_isthread(lua_State* L, int n)
{
    return lua_type(L, n) == LUA_TTHREAD;
}

int lua_isnone(lua_State* L, int n)
{
    return lua_type(L, (n)) == LUA_TNONE;
}

int lua_isnoneornil(lua_State* L, int n)
{
    return lua_type(L, n) <= 0;
}

LUA_API lua_Number lua_tonumber(lua_State *L, int index)
{
    // TODO: Need to handle cooercion from a string.
    const Value* value = GetValueForIndex(L, index);
    if (Value_GetIsNumber(value))
    {
        return value->number;
    }
    return 0.0;
}

LUA_API lua_Integer lua_tointeger(lua_State *L, int index)
{
    lua_Number  d = lua_tonumber(L, index);
    lua_Integer i;
    __asm
    {
        fld     d
        fistp   i
    }
    return i;
}

LUA_API int lua_toboolean(lua_State* L, int index)
{
    const Value* value = GetValueForIndex(L, index);
    return GetBoolean(value);
}

LUA_API const char* lua_tolstring(lua_State *L, int index, size_t* length )
{
    Value* value = GetValueForIndex(L, index);
    if (ToString(L, value))
    {
        const String* string = value->string;
        if (length != NULL)
        {
            *length = string->length;
        }
        return String_GetData(string);
    }
    return NULL;
}

LUA_API const char* lua_tostring(lua_State* L, int index)
{
    return lua_tolstring(L, index, NULL);
}

LUA_API const void* lua_topointer(lua_State* L, int index)
{
    const Value* value = GetValueForIndex(L, index);
    switch (value->tag)
    {
    case TAG_LIGHTUSERDATA:
        return value->lightUserdata;
    case TAG_STRING:
    case TAG_TABLE:
    case TAG_FUNCTION:
    case TAG_THREAD:
    case TAG_USERDATA:
        return value->object;
    }
    return NULL;
}

LUA_API void* lua_touserdata(lua_State* L, int index)
{
    const Value* value = GetValueForIndex(L, index);
    if (value->tag == TAG_LIGHTUSERDATA)
    {
        return value->lightUserdata;
    }
    // TODO: Handle full userdata.
    return NULL;
}


LUA_API size_t lua_objlen(lua_State* L, int index)
{
    // Lua handles numbers specially, but the documentation doesn't mention that.
    const Value* value = GetValueForIndex(L, index);

    if (Value_GetIsString(value))
    {
        return value->string->length;
    }
    else if (Value_GetIsTable(value))
    {
        return Table_GetSize(L, value->table);
    }
    // TODO: implement userdata.
    return 0;
}

LUA_API void lua_rawget(lua_State* L, int index)
{

    Value* table = GetValueForIndex( L, index );
    assert( Value_GetIsTable(table) );

    const Value* key = GetValueForIndex(L, -1);
    const Value* value = Vm_GetTable(L, table, key);

    if (value == NULL)
    {
        PushNil(L);
    }
    else
    {
        PushValue(L, value);
    }

}

LUA_API void lua_rawgeti(lua_State *L, int index, int n)
{

    Value* table = GetValueForIndex(L, index);
    assert( Value_GetIsTable(table) );    

    const Value* value = Table_GetTable(L, table->table, n);
    if (value == NULL)
    {
        PushNil(L);
    }
    else
    {
        PushValue(L, value);
    }

}

LUA_API void lua_rawseti(lua_State* L, int index, int n)
{

    Value* table = GetValueForIndex(L, index);
    assert( Value_GetIsTable(table) );    

    Value* value = GetValueForIndex(L, -1);
    Table_SetTable(L, table->table, n, value);
    Pop(L, 1);

}

LUA_API void lua_settable(lua_State* L, int index)
{
    Value* key   = GetValueForIndex(L, -2);
    Value* value = GetValueForIndex(L, -1);
    Value* table = GetValueForIndex(L, index);
    Vm_SetTable( L, table, key, value );
    Pop(L, 2);
}

LUA_API int lua_type(lua_State* L, int index)
{
    const Value* value = GetValueForIndex(L, index);
    return Value_GetType(value);
}

LUA_API const char* lua_typename(lua_State* L, int type)
{
    return State_TypeName(L, type);
}

LUA_API int lua_gettop(lua_State* L)
{
    return static_cast<int>(L->stackTop - L->stackBase);
}

LUA_API void lua_settop(lua_State *L, int index)
{
    Value* value = L->stackTop;
    Value* top   = L->stackBase + index;
    while (value < top)
    {
        SetNil(value);
        ++value;
    }
    L->stackTop = top;
}

LUA_API void lua_insert(lua_State *L, int index)
{
    Value* p = GetValueForIndex(L, index);
    for (Value* q = L->stackTop; q > p; q--)
    {
        *q = *(q - 1);
    }
    *p = *L->stackTop;
}

int lua_checkstack(lua_State *L, int size)
{
    // lua_checkstack just reserves space for us on the stack, and since we're
    // not checking for stack overflow, we don't need to do anything.
    return 1;
}

void lua_newtable(lua_State* L)
{
    Value value;
    SetValue( &value, Table_Create(L) );
    PushValue( L, &value );
}

void lua_concat(lua_State *L, int n)
{
    Concat(L, n);
}

int lua_getstack(lua_State *L, int level, lua_Debug *ar)
{
    return 0;
}

int lua_getinfo(lua_State *L, const char *what, lua_Debug *ar)
{
    return 0;
}

int lua_next(lua_State* L, int index)
{

    Value* table = GetValueForIndex(L, index);
    assert( Value_GetIsTable(table) );
    
    Value* key = GetValueForIndex(L, -1);

    const Value* value = Table_Next(table->table, key);
    if (value == NULL)
    {
        return 0;
    }
    PushValue(L, value);
    return 1;

}

int lua_setmetatable(lua_State* L, int index)
{

    Value* object = GetValueForIndex(L, index);
    
    Value* metatable = GetValueForIndex(L, -1);
    assert( Value_GetIsTable(metatable) );

    if (Value_GetIsTable(object))
    {
        object->table->metatable = metatable->table;
        Gc_WriteBarrier(L, object->table, metatable);
    }
    else
    {
        // TODO: Set the metatable for the userdata or the common type.
        assert(0);
    }

    Pop(L, 1);
    return 1;

}

int lua_getmetatable(lua_State* L, int index)
{

    const Value* object = GetValueForIndex(L, index);
    Table* metatable = Value_GetMetatable(L, object);

    if (metatable == NULL)
    {
        return 0;
    }

    PushTable(L, metatable);
    return 1;

}

int lua_gc(lua_State* L, int what, int data)
{
    if (what == LUA_GCCOLLECT)
    {
        Gc_Collect(L, &L->gc);
        return 1;
    }
    else if (what == LUA_GCSTEP)
    {
        if (Gc_Step(L, &L->gc))
        {
            return 1;
        }
    }
    else if (what == LUA_GCCOUNT)
    {
        return static_cast<int>(L->totalBytes / 1024);
    }
    else if (what == LUA_GCCOUNTB)
    {
        return static_cast<int>(L->totalBytes % 1024);
    }
    return 0;
}
