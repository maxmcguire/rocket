/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#ifndef ROCKETVM_STATE_H
#define ROCKETVM_STATE_H

extern "C"
{
#include "lua.h"
}

#include "String.h"
#include "Value.h"
#include "Opcode.h"

#include <setjmp.h>

struct Gc_Object;
struct Closure;
struct String;
struct Table;
struct UserData;
struct UpValue;

#define LUAI_MAXCCALLS      200

struct ErrorHandler
{
    jmp_buf         jump;
};

struct CallFrame
{
    Value*              function; 
    const Instruction*  ip;
    Value*              stackTop;
    Value*              stackBase;
    int                 numResults; // Expected number of results from the call.
};

struct lua_State
{
    Value           dummyObject;    // Used when we need to refer to an object that doesn't exist.
    Value*          stack;
    Value*          stackBase;
    Value*          stackTop;       // Points to the next free spot on the stack.
    UpValue*        openUpValue;
    CallFrame*      callStackTop;
    lua_Alloc       alloc;
    lua_CFunction   panic;
    lua_Hook        hook;
    int             hookMask;
    int             hookCount;
    lua_GCHook      gchook;
    void*           userdata;
    ErrorHandler*   errorHandler;
    Value           globals;
    Value           registry;
    Value           env;            // Temporary storage for the env table for a function.
    Gc              gc;
    size_t          totalBytes;
    Table*          metatable[NUM_TYPES];   // Metatables for basic types.
    String*         typeName[NUM_TYPES];
    String*         tagMethodName[TagMethod_NumMethods];
    CallFrame       callStackBase[LUAI_MAXCCALLS];
    StringPool      stringPool;
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

inline void PushUserData(lua_State* L, UserData* userData)
{
    SetValue( L->stackTop, userData );
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

inline void PushFunction(lua_State* L, Function* function)
{
    SetValue( L->stackTop, function );
    ++L->stackTop;
}

inline void PushPrototype(lua_State* L, Prototype* prototype)
{
    SetValue( L->stackTop, prototype );
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

/**
 * Removes the element pointed to by value from the stack.
 */
inline void State_Remove(lua_State* L, Value* value)
{
    Value* stackTop = L->stackTop;
    while (++value < stackTop)
    {
        *(value - 1) = *value;
    }
    --L->stackTop;
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
String* State_TypeName(lua_State* L, int type);

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