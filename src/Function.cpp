/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#include "Function.h"
#include "String.h"
#include "Compiler.h"
#include "Table.h"

#include <string.h>

void Prototype_GetName(Prototype* prototype, char *out, size_t bufflen)
{
    const char* source = String_GetData(prototype->source);
    if (*source == '=')
    {
        // Remove first char
        strncpy(out, source+1, bufflen);
        out[bufflen-1] = '\0';
    }
    else
    {  
        // out = "source", or "...source"
        if (*source == '@')
        {
            size_t l;
            source++; // skip the '@'
            bufflen -= sizeof(" '...' ");
            l = strlen(source);
            strcpy(out, "");
            if (l > bufflen)
            {
                // Get last part of file name
                source += (l-bufflen);
                strcat(out, "...");
            }
            strcat(out, source);
        }
        else 
        {  
            // out = [string "string"]
            size_t len = strcspn(source, "\n\r");  // Stop at first newline
            bufflen -= sizeof(" [string \"...\"] ");
            if (len > bufflen)
            {
                len = bufflen;
            }
            strcpy(out, "[string \"");
            if (source[len] != '\0')
            {  
                // Must truncate?
                strncat(out, source, len);
                strcat(out, "...");
            }
            else
            {
                strcat(out, source);
            }
            strcat(out, "\"]");
        }
    }
}

static size_t Prototype_GetSize(Prototype* prototype)
{
    size_t size = sizeof(Prototype);
    size += prototype->codeSize      * sizeof(Instruction);
    size += prototype->numConstants  * sizeof(Value);
    size += prototype->numPrototypes * sizeof(Prototype*);
    size += prototype->numUpValues   * sizeof(String*);  
    size += prototype->codeSize      * sizeof(int);
    return size;
}

Prototype* Prototype_Create(lua_State* L, int codeSize, int numConstants, int numPrototypes, int numUpValues)
{

    size_t size = sizeof(Prototype);
    size += codeSize      * sizeof(Instruction);
    size += numConstants  * sizeof(Value);
    size += numPrototypes * sizeof(Prototype*);
    size += numUpValues   * sizeof(String*);  
    size += codeSize      * sizeof(int);

    Prototype* prototype = static_cast<Prototype*>(Gc_AllocateObject(L, LUA_TPROTOTYPE, size));

    if (prototype == NULL)
    {
        return NULL;
    }
    
    prototype->varArg           = 0;
    prototype->numParams        = 0;
    prototype->maxStackSize     = 0;
    prototype->lineDefined      = 0;
    prototype->lastLineDefined  = 0;
    prototype->source           = NULL;

    // Code is stored immediately after the prototype structure in memory.
    prototype->code      = reinterpret_cast<Instruction*>(prototype + 1);
    prototype->codeSize  = codeSize;

    // Constants are stored after the code.
    prototype->constant = reinterpret_cast<Value*>(prototype->code + codeSize);
    prototype->numConstants = numConstants;
    for (int i = 0; i < numConstants; ++i)
    {
        SetNil(&prototype->constant[i]);
    }

    // Prototypes are stored after the constants.
    prototype->numPrototypes = numPrototypes;
    prototype->prototype     = reinterpret_cast<Prototype**>(prototype->constant + numConstants);
    memset(prototype->prototype, 0, sizeof(Prototype*) * numPrototypes);

    // Up values are is stored after the prototypes.
    prototype->numUpValues   = numUpValues;
    prototype->upValue       = reinterpret_cast<String**>(prototype->prototype + numPrototypes);
    memset(prototype->upValue, 0, sizeof(String*) * numUpValues);

    // Debug info is stored after the up values.
    prototype->sourceLine = reinterpret_cast<int*>(prototype->upValue + numUpValues);
    memset(prototype->sourceLine, 0, sizeof(int) * codeSize);

    ASSERT( size == Prototype_GetSize(prototype) );

    return prototype;

}

static Prototype* Prototype_Create(lua_State* L, Prototype* parent, const char* data, size_t& length)
{

    // A description of the binary format for a compiled chunk can be found here:
    // http://luaforge.net/docman/view.php/83/98/ANoFrillsIntroToLua51VMInstructions.pdf

    const char* start = data;

    size_t nameLength = *reinterpret_cast<const size_t*>(data);
    data += sizeof(size_t);
    const char* name = data;
    data += nameLength;

    int lineDefined = *reinterpret_cast<const int*>(data);
    data += sizeof(int);
    int lastLineDefined = *reinterpret_cast<const int*>(data);
    data += sizeof(int);

    int numUpValues = static_cast<int>(data[0]);
    ++data;
    int numParams = static_cast<int>(data[0]);
    ++data;
    int varArg = static_cast<int>(data[0]);
    ++data;
    int maxStackSize = static_cast<int>(data[0]);
    ++data;

    // Instructions.
    int codeSize = *reinterpret_cast<const int*>(data);
    data += sizeof(int);
    const char* code = data;
    data += codeSize * sizeof(Instruction);

    // Constants.
    int numConstants = *reinterpret_cast<const int*>(data);
    data += sizeof(int);
    const char* constants = data;

    // Skip over the constants data. Unfortunately we have to read the data
    // to determine the total size of the constants data.
    for (int i = 0; i < numConstants; ++i)
    {
        int type = data[0];
        ++data;
        if (type == LUA_TSTRING)
        {
            size_t length = *reinterpret_cast<const size_t*>(data);
            data += sizeof(size_t);
            data += length;
        }
        else if (type == LUA_TNUMBER)
        {
            data += sizeof(lua_Number);
        }
        else if (type == LUA_TBOOLEAN)
        {
            ++data;
        }
        else if (type != LUA_TNIL)
        {
            PushFString(L, "invalid binary format");
            State_Error(L);
        }
    }

    // Prototypes.
    int numPrototypes = *reinterpret_cast<const int*>(data);
    data += sizeof(int);
    const char* prototypes = data;

    // Create the function object.
    Prototype* prototype = Prototype_Create(L, codeSize, numConstants, numPrototypes, numUpValues);
    if (prototype == NULL)
    {
        return NULL;
    }

    // Store on the stack to prevent garbage collection.
    PushPrototype(L, prototype);

    prototype->lineDefined      = lineDefined;
    prototype->lastLineDefined  = lastLineDefined;

    if (nameLength == 0 && parent != NULL)
    {
        prototype->source = parent->source;
    }
    else
    {
        prototype->source = String_Create(L, name, nameLength);
    }

    memcpy(prototype->code, code, codeSize * sizeof(Instruction));

    for (int i = 0; i < numConstants; ++i)
    {
        
        int type = constants[0];
        ++constants;

        if (type == LUA_TNIL)
        {
            SetNil(  &prototype->constant[i] );
        }
        else if (type == LUA_TSTRING)
        {
            size_t length = *reinterpret_cast<const size_t*>(constants);
            constants += sizeof(size_t);
            SetValue( &prototype->constant[i], String_Create(L, constants, length - 1) );
            constants += length;
        }
        else if (type == LUA_TNUMBER)
        {
            SetValue( &prototype->constant[i], *reinterpret_cast<const lua_Number*>(constants) );
            constants += sizeof(lua_Number);
        }
        else if (type == LUA_TBOOLEAN)
        {
            SetValue( &prototype->constant[i], constants[0] != 0 );
            ++constants;
        }
            
        Gc_WriteBarrier(L, prototype, &prototype->constant[i]);

    }

    for (int i = 0; i < numPrototypes; ++i)
    {
        size_t length = 0;
        prototype->prototype[i] = Prototype_Create(L, prototype, prototypes, length);
        prototypes += length;
    }

    data = prototypes;

    // Source line debug info.
    int numSourceLines = *reinterpret_cast<const int*>(data);
    ASSERT(numSourceLines == 0 || numSourceLines == codeSize);
    data += sizeof(int);
    for (int i = 0; i < numSourceLines; ++i)
    {
        prototype->sourceLine[i] = *reinterpret_cast<const int*>(data);
        data += sizeof(int);
    }

    // Locals debug info.
    int localsListSize = *reinterpret_cast<const int*>(data);
    data += sizeof(int);
    for (int i = 0; i < localsListSize; ++i)
    {
        size_t length = *reinterpret_cast<const size_t*>(data);
        data += sizeof(size_t) + length + sizeof(int) * 2;
    }

    // Upvalue debug info.
    int upvalueListSize = *reinterpret_cast<const int*>(data);
    data += sizeof(int);
    for (int i = 0; i < upvalueListSize; ++i)
    {
        size_t length = *reinterpret_cast<const size_t*>(data);
        data += sizeof(size_t);
        prototype->upValue[i] = String_Create(L, data, length - 1);
        data += length;
    }

    prototype->varArg = (varArg & 2) != 0;
    prototype->numParams = numParams;
    prototype->maxStackSize = maxStackSize;

    length = data - start;

    ASSERT( (L->stackTop - 1)->object == prototype );
    Pop(L, 1);

    return prototype;

}

Prototype* Prototype_Create(lua_State* L, const char* data, size_t length, const char* name)
{
    Prototype* prototype = Prototype_Create(L, NULL, data, length);
    return prototype;
}

void Prototype_Destroy(lua_State* L, Prototype* prototype)
{
    size_t size = Prototype_GetSize(prototype);
    Free(L, prototype, size);
}

Closure* Closure_Create(lua_State* L, Prototype* prototype, Table* env)
{

    ASSERT(env != NULL);

    size_t size = sizeof(Closure);
    size += prototype->numUpValues * sizeof(UpValue*);

    Closure* closure = static_cast<Closure*>(Gc_AllocateObject(L, LUA_TFUNCTION, size));
    closure->c = false;
    
    closure->env = env;
    Gc_WriteBarrier(L, closure, env);

    closure->lclosure.prototype = prototype;
    Gc_WriteBarrier(L, closure, prototype);

    closure->lclosure.upValue   = reinterpret_cast<UpValue**>(closure + 1);
    closure->lclosure.numUpValues = prototype->numUpValues;
    memset(closure->lclosure.upValue, 0, sizeof(UpValue*) * prototype->numUpValues);

    return closure;

}

Closure* Closure_Create(lua_State* L, lua_CFunction function, const Value upValue[], int numUpValues, Table* env)
{

    ASSERT(env != NULL);

    size_t size = sizeof(Closure);
    size += numUpValues * sizeof(Value);

    Closure* closure = static_cast<Closure*>(Gc_AllocateObject(L, LUA_TFUNCTION, size));

    closure->env = env;
    closure->c = true;
    closure->cclosure.function      = function;

    closure->cclosure.upValue       = reinterpret_cast<Value*>(closure + 1);
    closure->cclosure.numUpValues   = numUpValues;
    memcpy(closure->cclosure.upValue, upValue, numUpValues * sizeof(Value));

    return closure;

}

void Closure_Destroy(lua_State* L, Closure* closure)
{
    size_t size = sizeof(Closure);
    if (closure->c)
    {
        size += closure->cclosure.numUpValues * sizeof(Value);
    }
    else
    {
        size += closure->lclosure.numUpValues * sizeof(UpValue*);
    }
    Free(L, closure, size);
}

