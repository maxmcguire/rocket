/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */

#include "State.h"
#include "Table.h"
#include "String.h"

#include <memory.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
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
        assert(*mem == oldSize);
        mem = static_cast<size_t*>( L->alloc( L->userdata, mem, oldSize, newSize) );
    }
    else
    {
        assert(oldSize == 0);
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

    // Always include one call frame which will represent calling into the Lua
    // API from C.
    L->callStackTop->function   = NULL;
    L->callStackTop->ip         = NULL;
    L->callStackTop->stackBase  = L->stackTop;
    L->callStackTop->stackTop   = L->stackTop;
    ++L->callStackTop;

    memset(L->stringPoolEntry, 0, sizeof(L->stringPoolEntry));
    memset(L->tagMethodName, 0, sizeof(L->tagMethodName));

    Gc_Initialize(&L->gc);

    SetValue( &L->globals, Table_Create(L) );
    SetValue( &L->registry, Table_Create(L) );

    // Store the tag method names so we don't need to create new strings
    // every time we want to access them.
    const char* tagMethodName[] = { "__index", "__newindex", "__call" };
    for (int i = 0; i < TagMethod_NumMethods; ++i)
    {
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

    CopyValue(dst, start);

    Value* arg1 = dst;
    Value* arg2 = start + 1;

    while (arg2 <= end)
    {

        if ( (!Value_GetIsString(arg1) && Value_GetIsNumber(arg1)) || !ToString(L, arg2) )
        {
            // TODO: Call metamethod concat.
            assert(0);
        }
        else
        {

            ToString(L, arg1);

            size_t length1 = arg1->string->length;
            size_t length2 = arg2->string->length;

            char* buffer = static_cast<char*>( Allocate(L, length1 + length2) );
            memcpy(buffer, String_GetData(arg1->string), length1);
            memcpy(buffer + length1, String_GetData(arg2->string), length2);

            SetValue( dst, String_Create(L, buffer, length1 + length2) );

        }

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
        const char* message = lua_tostring(L, -1);
        fprintf(stderr, "PANIC: unprotected error in call to Lua API (%s)\n", message);
        exit(EXIT_FAILURE);
    }
}

const char* State_TypeName(lua_State* L, int type)
{
    switch (type)
    {
    case LUA_TNONE:     return "none";
    case LUA_TNIL:      return "nil";
    case LUA_TBOOLEAN:  return "boolean";
    case LUA_TNUMBER:   return "number";
    case LUA_TSTRING:   return "string";
    case LUA_TTABLE:    return "table";
    case LUA_TFUNCTION: return "function";
    case LUA_TLIGHTUSERDATA:
    case LUA_TUSERDATA:
        return "userdata";
    case LUA_TTHREAD:
        return "thread";
    }
    return "unknown";
}