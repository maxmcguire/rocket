/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#include "Global.h"
#include "State.h"
#include "Table.h"
#include "String.h"
#include "Vm.h"

#include <memory.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int AlignOffset(void* p, int align)
{
    return static_cast<int>((align - reinterpret_cast<ptrdiff_t>(p) % align) % align);
}

void* Allocate(lua_State* L, size_t size)
{
    return Reallocate(L, NULL, 0, size);
}

void Free(lua_State* L, void* p, size_t oldSize)
{
    Reallocate(L, p, oldSize, 0);
}

void* Reallocate(lua_State* L, void* p, size_t oldSize, size_t newSize)
{

#ifdef _DEBUG

    // In debug mode, keep track of the actual allocation size of the block of
    // memory immediately before the memory we return. This allows us to detect
    // problems where the oldSize is not properly passed in.

    size_t* mem;

    if (oldSize != 0) oldSize += sizeof(size_t);
    if (newSize != 0) newSize += sizeof(size_t);

    if (p != NULL)
    {
        mem = static_cast<size_t*>(p) - 1;
        ASSERT(*mem == oldSize);
        mem = static_cast<size_t*>( L->alloc( L->userdata, mem, oldSize, newSize) );
    }
    else
    {
        ASSERT(oldSize == 0);
        mem = static_cast<size_t*>( L->alloc( L->userdata, NULL, 0, newSize) );
    }

    L->totalBytes -= oldSize;

    if (mem != NULL)
    {
        L->totalBytes += newSize;
        *mem = newSize;
        return mem + 1;
    }

    return NULL;

#else
    L->totalBytes += newSize - oldSize;
    return L->alloc( L->userdata, p, oldSize, newSize );
#endif

}

void* GrowArray(lua_State* L, void* p, int numElements, int* maxElements, size_t elementSize)
{
    if (numElements + 1 > *maxElements)
    {
        size_t oldSize = *maxElements * elementSize;
        size_t newSize = oldSize * 2 + 1;
        p = Reallocate(L, p, oldSize, newSize);
        *maxElements = *maxElements * 2;
    }
    return p;
}

lua_State* State_Create(lua_Alloc alloc, void* userdata)
{

    const int stackSize = LUAI_MAXCSTACK;

    size_t size = sizeof(lua_State) + sizeof(Value) * stackSize;
    lua_State* L = reinterpret_cast<lua_State*>( alloc(userdata, NULL, 0, size) );

    L->alloc        = alloc;
    L->panic        = NULL;
    L->hook         = NULL;
    L->hookMask     = 0;
    L->hookCount    = 0;
    L->gchook       = NULL;
    L->userdata     = userdata;
    L->stack        = reinterpret_cast<Value*>(L + 1);
    L->stackBase    = L->stack;
    L->stackTop     = L->stackBase;
    L->callStackTop = L->callStackBase;
    L->openUpValue  = NULL;
    L->errorHandler = NULL;
    L->totalBytes   = size;

    StringPool_Initialize(L, &L->stringPool);

    // Always include one call frame which will represent calling into the Lua
    // API from C.
    L->callStackTop->function   = NULL;
    L->callStackTop->ip         = NULL;
    L->callStackTop->stackBase  = L->stackTop;
    L->callStackTop->stackTop   = L->stackTop;
    ++L->callStackTop;

    memset(L->tagMethodName, 0, sizeof(L->tagMethodName));

    Gc_Initialize(&L->gc);

    SetNil(&L->dummyObject);

    SetValue( &L->globals, Table_Create(L) );
    SetValue( &L->registry, Table_Create(L) );

    for (int i = 0; i < NUM_TYPES; ++i)
    {
        L->metatable[i] = NULL;
    }

    // Store the names for the different types, so we don't have to create new
    // strings when we want to return them.
    String* unknownName = String_Create(L, "unknown");
    for (int i = 0; i < NUM_TYPES; ++i)
    {
        L->typeName[i] = unknownName;
    }

    L->typeName[LUA_TNONE]          = String_Create(L, "no value");
    L->typeName[LUA_TNIL]           = String_Create(L, "nil");
    L->typeName[LUA_TBOOLEAN]       = String_Create(L, "boolean");
    L->typeName[LUA_TNUMBER]        = String_Create(L, "number");
    L->typeName[LUA_TSTRING]        = String_Create(L, "string");
    L->typeName[LUA_TTABLE]         = String_Create(L, "table");
    L->typeName[LUA_TFUNCTION]      = String_Create(L, "function");
    L->typeName[LUA_TLIGHTUSERDATA] = String_Create(L, "userdata");
    L->typeName[LUA_TUSERDATA]      = L->typeName[LUA_TLIGHTUSERDATA];
    L->typeName[LUA_TTHREAD]        = String_Create(L, "thread");
    L->typeName[LUA_TUPVALUE]       = String_Create(L, "upval");
    L->typeName[LUA_TPROTOTYPE]     = String_Create(L, "proto");

    // Store the tag method names so we don't need to create new strings
    // every time we want to access them.
    const char* tagMethodName[] =
        {
            "__index",
            "__newindex",
            "__call",
            "__add",
            "__sub",
            "__mul",
            "__div",
            "__mod",
            "__pow",
            "__unm",
            "__lt",
            "__le",
            "__eq",
            "__concat",
        };
    for (int i = 0; i < TagMethod_NumMethods; ++i)
    {
        ASSERT( i < sizeof(tagMethodName) / sizeof(const char*) );
        L->tagMethodName[i] = String_Create(L, tagMethodName[i]);
    }

    return L;

}

void State_Destroy(lua_State* L)
{
    L->alloc( L->userdata, L, 0, 0 );
}

void PushFString(lua_State* L, const char* fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    PushVFString(L, fmt, argp);
    va_end(argp);
}

void PushVFString(lua_State* L, const char* fmt, va_list argp)
{
    int n = 1;
    PushString(L, "" );
    while (1)
    {
        const char* e = strchr(fmt, '%');
        if (e == NULL)
        {
            break;
        }
        PushString( L, String_Create(L, fmt, e - fmt) );
        switch (*(e+1))
        {
        case 's':
            {
                const char* s = va_arg(argp, char*);
                PushString(L, s ? s : "(null)");
            }
            break;
        case 'c':
            {
                char buff[2];
                buff[0] = static_cast<char>(va_arg(argp, int));
                buff[1] = '\0';
                PushString(L, buff);
            }
            break;
        case 'd':
            PushNumber( L, static_cast<lua_Number>(va_arg(argp, int)) );
            break;
        case 'f':
            PushNumber( L, static_cast<lua_Number>( va_arg(argp, double)) );
            break;
        case 'p':
            {
                char buff[4*sizeof(void *) + 8]; // Should be enough space for a `%p'
                sprintf(buff, "%p", va_arg(argp, void *));
                PushString(L, buff);
            }
            break;
        case '%':
            PushString(L, "%");
            break;
        default:
            {
                char buff[3];
                buff[0] = '%';
                buff[1] = *(e+1);
                buff[2] = '\0';
                PushString(L, buff);
            }
            break;
        }
        n += 2;
        fmt = e+2;
    }
    PushString(L, fmt);
    Concat( L, L->stackTop - n - 1, L->stackTop - n - 1, L->stackTop - 1 ); 
    Pop(L, n);
}

void Concat(lua_State* L, int n)
{
    if (n >= 2)
    {
        // TODO: this looks like it will be problematic for the garbage collector.
        Value dst;
        Concat(L, &dst, L->stackTop - n, L->stackTop - 1);
        Pop(L, n);
        PushValue(L, &dst);
    }
    else if (n == 0)
    {
        // Push an empty string.
        PushString(L, String_Create(L, "", 0));
    }
}

void Concat(lua_State* L, Value* dst, Value* start, Value* end)
{

    Value_Copy(dst, start);

    Value* arg1 = dst;
    Value* arg2 = start + 1;

    while (arg2 <= end)
    {
        Vm_Concat(L, dst, arg1, arg2);
        ++arg2;
    }

}

bool ToString(lua_State* L, Value* value)
{
    if (Value_GetIsString(value))
    {
        return true;
    }
    if (Value_GetIsNumber(value))
    {
        // Convert numbers to strings.
        char temp[32];
        sprintf(temp, "%.14g", value->number);
        SetValue( value, String_Create(L, temp) );
        return true;
    }
    else
    {
        // TODO: Call __tostring metamethod.
    }
    return false;
}

void State_Error(lua_State* L)
{
    if (L->errorHandler != NULL)
    {
        longjmp(L->errorHandler->jump, LUA_ERRRUN);
    }
    else
    {
        // Unprotected error.
        if (L->panic != NULL)
        {
            L->panic(L);
        }
        exit(EXIT_FAILURE);
    }
}

String* State_TypeName(lua_State* L, int type)
{
    return L->typeName[type];
}