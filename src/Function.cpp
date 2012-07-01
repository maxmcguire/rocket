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
    prototype->code          = reinterpret_cast<Instruction*>(prototype + 1);
    prototype->codeSize      = codeSize;
    prototype->convertedCode = reinterpret_cast<Instruction*>(prototype->code + codeSize);
    prototype->codeSize      = codeSize;

    // Constants are stored after the code.
    prototype->constant = reinterpret_cast<Value*>(prototype->convertedCode + codeSize);
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

static Instruction EncodeAB(Opcode opcode, int a, int b)
{
    ASSERT( a >= 0 && a < 256 );
    ASSERT( b >= 0 && b < 256 );
    return opcode | (a << 8) | (b << 16); 
}

static Instruction EncodeABC(Opcode opcode, int a, int b, int c)
{
    ASSERT( a >= 0 && a < 256 );
    ASSERT( b >= 0 && b < 256 );
    ASSERT( c >= 0 && c < 256 );
    return opcode | (a << 8) | (b << 16) | (c << 24); 
}

static Instruction EncodeAD(Opcode opcode, int a, int d)
{
    ASSERT( a >= 0 && a < 256 );
    ASSERT( d >= 0 && d < 65536 );
    return opcode | (a << 8) | (d << 16); 
}

static Instruction EncodeAsD(Opcode opcode, int a, int d)
{
    ASSERT( a >= 0 && a < 256 );
    ASSERT( d >= -32767 && d <= 32767 );
    return opcode | (a << 8) | ((d + 32767) << 16); 
}

/** Translates an instruction block from standard Lua opcodes to our own encoding.
 * src and dst can be the same. */
static void Prototype_ConvertCode(Instruction* dst, const void* _src, int codeSize)
{
    
#define RK_CONST(x) (x & 256)

    static const Opcode arithOp[] =
        {
            Opcode_Add,
            Opcode_AddRC,
            Opcode_AddCR,
            Opcode_AddCC,
            Opcode_Sub,
            Opcode_SubRC,
            Opcode_SubCR,
            Opcode_SubCC,
            Opcode_Mul,
            Opcode_MulRC,
            Opcode_MulCR,
            Opcode_MulCC,
            Opcode_Div,
            Opcode_DivRC,
            Opcode_DivCR,
            Opcode_DivCC,
            Opcode_Mod,
            Opcode_ModRC,
            Opcode_ModCR,
            Opcode_ModCC,
            Opcode_Pow,
            Opcode_PowRC,
            Opcode_PowCR,
            Opcode_PowCC,
        };

    static const Opcode logicOp[] =
        {
            Opcode_Eq,
            Opcode_EqRC,
            Opcode_EqCR,
            Opcode_EqCC,
            Opcode_Lt,
            Opcode_LtRC,
            Opcode_LtCR,
            Opcode_LtCC,
            Opcode_Le,
            Opcode_LeRC,
            Opcode_LeCR,
            Opcode_LeCC,
        };

    const int* src = reinterpret_cast<const int*>(_src);
    for (int i = 0; i < codeSize; ++i)
    {

        Opcode opcode = LUA_GET_OPCODE(*src);
        ASSERT( opcode >= 0 && opcode <= Opcode_GetTableRef );

        int a   = LUA_GET_A(*src);
        int b   = LUA_GET_B(*src);
        int bx  = LUA_GET_Bx(*src);
        int sbx = LUA_GET_sBx(*src);
        int c   = LUA_GET_C(*src);

        switch (opcode)
        {
        case Opcode_Move:
            *dst = EncodeAB(opcode, a, b); 
            break;
        case Opcode_LoadK:
            *dst = EncodeAD(opcode, a, bx); 
            break;
        case Opcode_LoadBool:
            *dst = EncodeABC(opcode, a, b, c); 
            break;
        case Opcode_LoadNil:
            *dst = EncodeAB(opcode, a, b); 
            break;
        case Opcode_GetUpVal:
        case Opcode_SetUpVal:
            *dst = EncodeAB(opcode, a, b); 
            break;
        case Opcode_GetGlobal:
        case Opcode_SetGlobal:
            *dst = EncodeAD(opcode, a, bx); 
            break;
        case Opcode_GetTable:
            *dst = EncodeABC(RK_CONST(c) ? Opcode_GetTableC : Opcode_GetTable, a, b, c & 255); 
            break;
        case Opcode_SetTable:
            if (RK_CONST(b))
            {
                *dst = EncodeABC(RK_CONST(c) ? Opcode_SetTableCC : Opcode_SetTableCR, a, b & 255, c & 255); 
            }
            else
            {
                *dst = EncodeABC(RK_CONST(c) ? Opcode_SetTableRC : Opcode_SetTable, a, b & 255, c & 255); 
            }
            break;
        case Opcode_NewTable:
            *dst = EncodeABC(opcode, a, b, c); 
            break;
        case Opcode_Self:
            *dst = EncodeABC(RK_CONST(c) ? Opcode_SelfC : Opcode_Self, a, b, c & 255); 
            break;
        case Opcode_Add:
        case Opcode_Sub:
        case Opcode_Mul:
        case Opcode_Div:
        case Opcode_Mod:
        case Opcode_Pow:
            {
                int index = (opcode - Opcode_Add) * 4;
                if (RK_CONST(b)) index += 2;
                if (RK_CONST(c)) index += 1;
                *dst = EncodeABC( arithOp[index], a, b & 255, c & 255 );
            }
            break;
        case Opcode_Unm:
        case Opcode_Not:
        case Opcode_Len:
            *dst = EncodeAB(opcode, a, b); 
            break;
        case Opcode_Concat:
            *dst = EncodeABC(opcode, a, b, c);
            break;
        case Opcode_Jmp:
            *dst = EncodeAsD(opcode, 0, sbx);
            break;
        case Opcode_Eq:
        case Opcode_Lt:
        case Opcode_Le:
            {
                int index = (opcode - Opcode_Eq) * 4;
                if (RK_CONST(b)) index += 2;
                if (RK_CONST(c)) index += 1;
                *dst = EncodeABC( logicOp[index], a, b & 255, c & 255 );
            }
            break;
        case Opcode_Test:
            *dst = EncodeAD(opcode, a, c);
            break;
        case Opcode_TestSet:
            *dst = EncodeABC(opcode, a, b, c);
            break;
        case Opcode_Call:
            *dst = EncodeABC(opcode, a, b, c);
            break;
        case Opcode_TailCall:
            *dst = EncodeAB(opcode, a, b);
            break;
        case Opcode_Return:
            *dst = EncodeAB(opcode, a, b);
            break;
        case Opcode_ForLoop:
        case Opcode_ForPrep:
            * dst = EncodeAsD(opcode, a, sbx);
            break;
        case Opcode_TForLoop:
            *dst = EncodeAD(opcode, a, c);
            break;
        case Opcode_SetList:
            *dst = EncodeABC(opcode, a, b, c);
            if (c == 0)
            {
                // TODO: this happens when c > 511, whcih is beyond what we can
                // encode with 8-bites.
                *(++dst) = (*++src);
            }
            break;
        case Opcode_Close:
            *dst = EncodeABC(opcode, a, 0, 0);
            break;
        case Opcode_Closure:
            *dst = EncodeAD(opcode, a, bx);
            break;
        case Opcode_VarArg:
            *dst = EncodeAB(opcode, a, b);
            break;
        case Opcode_GetTableRef:
            *dst = EncodeABC(RK_CONST(c) ? Opcode_GetTableRefC : Opcode_GetTableRef, a, b, c & 255); 
            break;
        default:
            ASSERT(0);
        }

        ++dst;
        ++src;
    }
}

void Prototype_ConvertCode(Prototype* prototype)
{
    Prototype_ConvertCode(prototype->convertedCode, prototype->code, prototype->codeSize);
    for (int i = 0; i < prototype->numPrototypes; ++i)
    {
        Prototype_ConvertCode(prototype->prototype[i]);
    }
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
            
        Gc_WriteBarrier(&L->gc, prototype, &prototype->constant[i]);

    }

    memcpy(prototype->code, code, sizeof(Instruction) * codeSize);

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
    Gc_WriteBarrier(&L->gc, closure, env);

    closure->lclosure.prototype = prototype;
    Gc_WriteBarrier(&L->gc, closure, prototype);

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

