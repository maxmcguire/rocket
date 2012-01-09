/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */
#ifndef ROCKETVM_STATE_H
#define ROCKETVM_STATE_H

#include "lua.h"
#include "String.h"

#include <setjmp.h>

struct Gc_Object;
struct Closure;
struct String;
struct Table;
struct UpValue;

#define LUAI_MAXCSTACK	    8000
#define LUAI_MAXCCALLS      200
#define LUAI_MAXSTRINGPOOL  256

#define TAG_NIL		        (~0u)
#define TAG_NONE		    (~1u)
#define TAG_BOOLEAN		    (~2u)
#define TAG_LIGHTUSERDATA	(~3u)
#define TAG_STRING		    (~4u)
#define TAG_TABLE		    (~5u)
#define TAG_FUNCTION		(~6u)
#define TAG_USERDATA		(~7u)
#define TAG_THREAD		    (~8u)
#define TAG_NUMBER		    (~9u)

typedef unsigned long UInt32;

typedef int Instruction;

enum TagMethod
{
    TagMethod_Index     = 0,
    TagMethod_NewIndex  = 1,
    TagMethod_Call      = 2,
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
            Gc_Object*  object;         // Alias for string, table, closure.
        };
    };
};

struct ErrorHandler
{
    jmp_buf         jump;
};

struct CallFrame
{
    Value*              function; 
    Value*              stackBase;
    Value*              stackTop;
    const Instruction*  ip;
};

struct lua_State
{
    Value*          stack;
    Value*          stackBase;
    Value*          stackTop;       // Points to the next free spot on the stack.
    UpValue*        openUpValue;
    CallFrame*      callStackTop;
    lua_Alloc       alloc;
    lua_GCHook      gchook;
    void*           userdata;
    ErrorHandler*   errorHandler;
    Value           globals;
    Value           registry;
    Gc              gc;
    size_t          totalBytes;
    String*         tagMethodName[TagMethod_NumMethods];
    CallFrame       callStackBase[LUAI_MAXCCALLS];
    String*         stringPoolEntry[LUAI_MAXSTRINGPOOL];
};

/**
 * Computes the number of bytes to offset a pointer to achieve the desired
 * alignemnt.
 */
int AlignOffset(void* p, int align);

void* Allocate(lua_State* L, size_t size);
void* Reallocate(lua_State* L, void* p, size_t oldSize, size_t newSize);

/**
 * Reserves space for one additional element in the array.
 */
template <class T>
void GrowArray(lua_State* L, T*& p, int numElements, int& maxElements)
{
    if (numElements + 1 > maxElements)
    {
        size_t oldSize = maxElements * sizeof(T);
        maxElements = maxElements * 2 + 1;
        size_t newSize = maxElements * sizeof(T);
        p = (T*)Reallocate(L, p, oldSize, newSize);
    }
}

/**
 * Releases memory previously allocated with Allocate or Reallocate. The
 * size of the memory block must be provided so that the system can properly
 * track the amount of memory in use.
 */
void Free(lua_State* L, void* p, size_t size);

lua_State* State_Create(lua_Alloc alloc, void* userdata);
void State_Destroy(lua_State* L);

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

/**
 * Returns the metatable for the value, or NULL if it doesn't have a
 * metatable.
 */
Table* Value_GetMetatable(lua_State* L, const Value* value);

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

inline void PushTable(lua_State* L, Table* table)
{
    SetValue( L->stackTop, table );
    ++L->stackTop;
}

inline void PushClosure(lua_State* L, Closure* closure)
{
    SetValue( L->stackTop, closure );
    ++L->stackTop;
}

inline void PushBoolean(lua_State* L, bool boolean)
{
    SetValue( L->stackTop, boolean );
    ++L->stackTop;
}

inline void PushLightUserdata(lua_State* L, void* userdata)
{
    SetValue( L->stackTop, userdata );
    ++L->stackTop;
}

inline void PushNumber(lua_State* L, lua_Number number)
{
    SetValue( L->stackTop, number);
    ++L->stackTop;
}

inline void PushString(lua_State* L, String* string)
{
    SetValue( L->stackTop, string );
    ++L->stackTop;
}
inline void PushString(lua_State* L, const char* string)
{
    PushString( L, String_Create(L, string) );
}

void PushFString(lua_State* L, const char* format, ...);
void PushVFString(lua_State* L, const char* fmt,  va_list argp);

inline void PushNil(lua_State* L)
{
    SetNil(L->stackTop);
    ++L->stackTop;
}

inline void PushValue(lua_State* L, const Value* value)
{
    *L->stackTop = *value;
    ++L->stackTop;
}

inline void Pop(lua_State* L, int num)
{
    L->stackTop -= num;
}

// Replaces the n values on the top of the stack with their concatenation.
void Concat(lua_State* L, int n);

// Concatenates a range of values between start and end.
void Concat(lua_State* L, Value* dst, Value* start, Value* end);

// Converts the value to a string; if the conversion was successful the function
// returns true.
bool ToString(lua_State* L, Value* value);

void State_Error(lua_State* L);

// Returns a human readable type name.
const char* State_TypeName(lua_State* L, int type);

inline CallFrame* State_GetCallFrame(lua_State* L)
    { return L->callStackTop - 1; }

// Sets the range of values between base and top to nil (doesn't set top).
inline void SetRangeNil(Value* base, Value* top)
{
    for (Value* dst = base; dst < top; ++dst)
    {
        SetNil(dst);
    }
}

#endif