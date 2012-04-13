/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#ifndef ROCKETVM_VALUE_H
#define ROCKETVM_VALUE_H

extern "C"
{
#include "lua.h"
}

//
// Forward declarations.
//

struct String;
struct Table;
struct Closure;
struct UserData;
struct Gc_Object;

#define TAG_NIL		        (~0u)   // 0xffffffff
#define TAG_NONE		    (~1u)   // 0xfffffffe
#define TAG_BOOLEAN		    (~2u)   // 0xfffffffd
#define TAG_LIGHTUSERDATA	(~3u)   // 0xfffffffc
#define TAG_STRING		    (~4u)   // 0xfffffffb
#define TAG_TABLE		    (~5u)   // 0xfffffffa
#define TAG_FUNCTION		(~6u)   // 0xfffffff9
#define TAG_USERDATA		(~7u)   // 0xfffffff8
#define TAG_THREAD		    (~8u)   // 0xfffffff7
#define TAG_NUMBER		    (~9u)   // 0xfffffff6

#define LUA_TPROTOTYPE      9
//#define LUA_TUPVAL	        10
//#define LUA_TDEADKEY	    11

#define NUM_TYPES           10        

typedef unsigned long UInt32;

enum TagMethod
{
    TagMethod_Index     = 0,
    TagMethod_NewIndex  = 1,
    TagMethod_Call      = 2,
    TagMethod_Add       = 3,
    TagMethod_Sub       = 4,
    TagMethod_Mul       = 5,
    TagMethod_Div       = 6,
    TagMethod_Mod       = 7,
    TagMethod_Pow       = 8,
    TagMethod_NumMethods,
};

union Value
{
    double              number;
    struct
    {
        UInt32          tag;
        union
        {
            int         boolean;
            void*       lightUserdata;
            String*     string;
            Table*      table;
            Closure*    closure;
            UserData*   userData;
            Gc_Object*  object;         // Alias for string, table, closure.
        };
    };
};

/** Returns true if the value represents a number type. This function must be
 used in lieu of directly comparing the tag to the TAG_NUMBER value */
inline bool Value_GetIsNumber(const Value* value)
    { return (value->tag & 0xfff80000) != 0xfff80000; }

inline bool Value_GetIsTable(const Value* value)
    { return value->tag == TAG_TABLE; }

inline bool Value_GetIsNil(const Value* value)
    { return value->tag == TAG_NIL; }

inline bool Value_GetIsString(const Value* value)
    { return value->tag == TAG_STRING; }

inline bool Value_GetIsBoolean(const Value* value)
    { return value->tag == TAG_BOOLEAN; }

inline bool Value_GetIsFunction(const Value* value)
    { return value->tag == TAG_FUNCTION; }

inline bool Value_GetIsObject(const Value* value)
    { return Value_GetIsString(value) || Value_GetIsTable(value) || Value_GetIsFunction(value); }

/** Returns the Lua type (LUA_NIL, LUA_TNUMBER, etc.) for the value */
inline int Value_GetType(const Value* value)
{
    if (Value_GetIsNumber(value)) return LUA_TNUMBER;
    switch (value->tag)
    {
    case TAG_NIL:           return LUA_TNIL;
    case TAG_BOOLEAN:       return LUA_TBOOLEAN;
    case TAG_LIGHTUSERDATA: return LUA_TLIGHTUSERDATA;
    case TAG_STRING:        return LUA_TSTRING;
    case TAG_TABLE:         return LUA_TTABLE;
    case TAG_FUNCTION:      return LUA_TFUNCTION;
    case TAG_USERDATA:      return LUA_TUSERDATA;
    case TAG_THREAD:        return LUA_TTHREAD;
    }
    return LUA_TNONE;
}

inline int Value_GetInteger(const Value* value)
    { 
        if (Value_GetIsNumber(value)) return static_cast<int>(value->number);
        return 0;
    }

inline void CopyValue(Value* dst, Value* src)
    { *dst = *src; }

inline void SetNil(Value* value)
    { value->tag = TAG_NIL; }
inline void SetValue(Value* value, bool boolean)
    { value->tag = TAG_BOOLEAN; value->boolean = boolean; }
inline void SetValue(Value* value, lua_Number number)
    { value->number = number; }
inline void SetValue(Value* value, int number)
    { value->number = static_cast<lua_Number>(number); }
inline void SetValue(Value* value, String* string)
    { value->tag = TAG_STRING; value->string = string; }
inline void SetValue(Value* value, Table* table)
    { value->tag = TAG_TABLE; value->table = table; }
inline void SetValue(Value* value, Closure* closure)
    { value->tag = TAG_FUNCTION; value->closure = closure; }
inline void SetValue(Value* value, void* userdata)
    { value->tag = TAG_LIGHTUSERDATA; value->lightUserdata = userdata; }
inline void SetValue(Value* value, UserData* userData)
    { value->tag = TAG_USERDATA; value->userData = userData; }

/**
 * Tests if two values are equal using a raw test (no metamethods).
 */
inline int Value_Equal(const Value* arg1, const Value* arg2)
{
    if (arg1->tag != arg2->tag)
    {
        return 0;
    }
    if (Value_GetIsNil(arg1))
    {
        return 1;
    }
    return arg1->object == arg2->object;
}

/**
 * Sets the metatable table for the value. 
 */
void Value_SetMetatable(lua_State* L, Value* value, Table* table);

/**
 * Retursn the metatable for the value.
 */
Table* Value_GetMetatable(lua_State* L, const Value* value);

/**
 * Sets the environment table for the value. If the value is not a function
 * a thread or a userdata, the function does nothing and returns false.
 */
int Value_SetEnv(lua_State* L, Value* value, Table* table);

/**
 * Returns the environment table for the object if there is one.
 */
Table* Value_GetEnv(const Value* value);

#endif