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

#include "Global.h"

//
// Forward declarations.
//

struct String;
struct Table;
struct Closure;
struct UserData;
struct Function;
struct Prototype;
struct Gc_Object;

/** Tag used to identify the type of a value. */
enum Tag
{
    Tag_Nil             = ~0u,
    Tag_None            = ~1u,
    Tag_Boolean         = ~2u,
    Tag_LightUserdata   = ~3u,
    Tag_String          = ~4u,
    Tag_Table           = ~5u,
    Tag_Closure         = ~6u,
    Tag_Userdata        = ~7u,
    Tag_Thread          = ~8u,
    Tag_Prototype       = ~9u,
    Tag_FunctionP       = ~10u,
};
STATIC_ASSERT( sizeof(Tag) == 4, TagMustBe32Bits );

#define LUA_TPROTOTYPE      9
#define LUA_TUPVALUE        10
#define LUA_TFUNCTIONP      11  // Used during parsing.

#define NUM_TYPES           12        

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
    TagMethod_Unm       = 9,
    TagMethod_Lt        = 10,
    TagMethod_Le        = 11,
    TagMethod_Eq        = 12,
    TagMethod_Concat    = 13,
    TagMethod_NumMethods,
};

union Value
{
    double              number;
    struct
    {
        union
        {
            int         boolean;
            void*       lightUserdata;
            String*     string;
            Table*      table;
            Closure*    closure;
            UserData*   userData;
            Function*   function;
            Prototype*  prototype;
            Gc_Object*  object;     // Alias for string, table, closure, etc.
        };
        Tag             tag;
    };
};
STATIC_ASSERT( offsetof(Value, tag) == 4, TagMustBeMSW );

/** Returns true if the value represents a number type. This function must be
 used in lieu of directly comparing the tag to the TAG_NUMBER value */
static FORCE_INLINE bool Value_GetIsNumber(const Value* value)
    { return value->tag <= 0xfff80000; }

/** Returns true if the value is a number representing the value NaN. */
static FORCE_INLINE bool Value_GetIsNaN(const Value* value)
    { return value->tag == 0xfff80000; }

static FORCE_INLINE bool Value_GetIsTable(const Value* value)
    { return value->tag == Tag_Table; }

static FORCE_INLINE bool Value_GetIsNil(const Value* value)
    { return value->tag == Tag_Nil; }

static FORCE_INLINE bool Value_GetIsString(const Value* value)
    { return value->tag == Tag_String; }

static FORCE_INLINE bool Value_GetIsBoolean(const Value* value)
    { return value->tag == Tag_Boolean; }

static FORCE_INLINE bool Value_GetIsClosure(const Value* value)
    { return value->tag == Tag_Closure; }

static FORCE_INLINE bool Value_GetIsLightUserData(const Value* value)
    { return value->tag == Tag_LightUserdata; }

static FORCE_INLINE bool Value_GetIsUserData(const Value* value)
    { return value->tag == Tag_Userdata; }

/** Returns true if the value is a type that is garbage collected. */
static FORCE_INLINE bool Value_GetIsObject(const Value* value)
    { 
        return !Value_GetIsNumber(value)  &&
               !Value_GetIsNil(value)     &&
               !Value_GetIsBoolean(value) &&
               !Value_GetIsLightUserData(value);
    }


/** Returns the Lua type (LUA_NIL, LUA_TNUMBER, etc.) for the value */
inline int Value_GetType(const Value* value)
{
    if (Value_GetIsNumber(value)) return LUA_TNUMBER;
    switch (value->tag)
    {
    case Tag_Nil:           return LUA_TNIL;
    case Tag_Boolean:       return LUA_TBOOLEAN;
    case Tag_LightUserdata: return LUA_TLIGHTUSERDATA;
    case Tag_String:        return LUA_TSTRING;
    case Tag_Table:         return LUA_TTABLE;
    case Tag_Closure:       return LUA_TFUNCTION;
    case Tag_Userdata:      return LUA_TUSERDATA;
    case Tag_Thread:        return LUA_TTHREAD;
    }
    return LUA_TNONE;
}

inline int Value_GetInteger(const Value* value)
    { 
        if (Value_GetIsNumber(value)) return static_cast<int>(value->number);
        return 0;
    }

inline void Value_Copy(Value* dst, Value* src)
    { *dst = *src; }

inline void SetNil(Value* value)
    { value->tag = Tag_Nil; }
inline void SetValue(Value* value, bool boolean)
    { value->tag = Tag_Boolean; value->boolean = boolean; }
inline void SetValue(Value* value, lua_Number number)
    { value->number = number; }
inline void SetValue(Value* value, int number)
    { value->number = static_cast<lua_Number>(number); }
inline void SetValue(Value* value, String* string)
    { value->tag = Tag_String; value->string = string; }
inline void SetValue(Value* value, Table* table)
    { value->tag = Tag_Table; value->table = table; }
inline void SetValue(Value* value, Closure* closure)
    { value->tag = Tag_Closure; value->closure = closure; }
inline void SetValue(Value* value, void* userdata)
    { value->tag = Tag_LightUserdata; value->lightUserdata = userdata; }
inline void SetValue(Value* value, UserData* userData)
    { value->tag = Tag_Userdata; value->userData = userData; }
inline void SetValue(Value* value, Function* function)
    { value->tag = Tag_FunctionP; value->function = function; }

inline void SetValue(Value* value, Prototype* prototype)
    { value->tag = Tag_Prototype; value->prototype = prototype; }


/**
 * Tests if two values are equal using a raw test (no metamethods).
 */
inline int Value_Equal(const Value* arg1, const Value* arg2)
{
    if (Value_GetIsNumber(arg1) && Value_GetIsNumber(arg2))
    {
        return luai_numeq(arg1->number, arg2->number);
    }
    else if (arg1->tag != arg2->tag)
    {
        return 0;
    }
    else if (Value_GetIsNil(arg1))
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

/** Converts a string representation of a number into the actual number. Returns
true if the conversion was successful. The string can contain leading and
trailing whitespace. This function can accomodate strings in any format that is
permissiable by the Lua language and uses the current locale settings. */
bool StringToNumber(const char* string, lua_Number* number);

#endif