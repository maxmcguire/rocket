/*
 * RocketVM
 * Copyright (c) 2011-2012 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#include "Function.h"
#include "String.h"
#include "Table.h"
#include "UpValue.h"

#include <string.h>
#include <malloc.h>
#include <stdio.h>

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

Prototype* Prototype_Create(lua_State* L)
{
    Prototype* prototype = static_cast<Prototype*>(Gc_AllocateObject(L, LUA_TPROTOTYPE, sizeof(Prototype)));
    if (prototype == NULL)
    {
        return NULL;
    }

    prototype->varArg               = 0;
    prototype->numParams            = 0;
    prototype->maxStackSize         = 0;
    prototype->codeSize             = 0;
    prototype->code                 = NULL;
    prototype->convertedCodeSize    = 0;
    prototype->convertedCode        = NULL;
    prototype->numConstants         = 0;
    prototype->constant             = NULL;
    prototype->numUpValues          = 0;
    prototype->maxUpValues          = 0; 
    prototype->upValue              = NULL;
    prototype->numPrototypes        = 0;
    prototype->prototype            = NULL;
    prototype->local                = NULL;
    prototype->numLocals            = 0;
    prototype->lineDefined          = 0;
    prototype->lastLineDefined      = 0;
    prototype->source               = NULL;
    prototype->sourceLine           = NULL;
    prototype->convertedSourceLine  = NULL;

    return prototype;
}

Prototype* Prototype_Create(lua_State* L, int codeSize, int convertedCodeSize, int numConstants, int numPrototypes, int numUpValues)
{
    
    Prototype* prototype = Prototype_Create(L);

    prototype->code                 = AllocateArray<Instruction>(L, codeSize);
    prototype->constant             = AllocateArray<Value>(L, numConstants);
    prototype->prototype            = AllocateArray<Prototype*>(L, numPrototypes);
    prototype->upValue              = AllocateArray<String*>(L, numUpValues);

    prototype->sourceLine           = AllocateArray<int>(L, codeSize);
 
    prototype->numUpValues          = numUpValues;
    prototype->maxUpValues          = numUpValues;
    prototype->codeSize             = codeSize;
    prototype->numConstants         = numConstants;
    prototype->numPrototypes        = numPrototypes;

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

static void ConvertInstruction(Instruction*& dst, const Instruction*& src)
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
        // The range of bx is larger than what we can encode in d, so
        // we break it into multiple opcodes which handle different
        // ranges of constants.
        if (bx >= 65536)
        {
            opcode = Opcode_LoadK2;
            *dst = EncodeAD(opcode, a, 0); 
            ++dst;
            *dst = bx;
        }
        else
        {
            ASSERT(bx < 65536);
            *dst = EncodeAD(opcode, a, bx); 
        }
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
        if (bx >= 65536)
        {
            opcode = Opcode_GetGlobal2;
            *dst = EncodeAD(opcode, a, 0); 
            ++dst;
            *dst = bx;
        }
        else
        {
            *dst = EncodeAD(opcode, a, bx);
        }
    #ifdef ROCKET_INLINE_CACHE_GLOBALS
        // Add an extra instruction slot for inline caching of the hint value for global
        // table lookups.
        ++dst;
        *dst = 0;
    #endif
        break;
    case Opcode_SetGlobal:
        if (bx >= 65536)
        {
            opcode = Opcode_SetGlobal2;
            *dst = EncodeAD(opcode, a, 0); 
            ++dst;
            *dst = bx;
        }
        else
        {
            *dst = EncodeAD(opcode, a, bx);
        }
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
        // TODO: We need to encode b and c differently since they can be
        // larger than a single byte. To avoid problems for the moment,
        // we just make them 0.
        *dst = EncodeABC(opcode, a, 0, 0); 
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
        *dst = EncodeAsD(opcode, a, sbx);
        break;
    case Opcode_TForLoop:
        *dst = EncodeAD(opcode, a, c);
        break;
    case Opcode_SetList:
        if (c == 0)
        {
            c = *(++src);
        }
        if (c > 255)
        {
            *(dst++) = EncodeABC(opcode, a, b, 0);
            *dst = c;
        }
        else
        {
            *dst = EncodeABC(opcode, a, b, c);
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

int Prototype_GetConvertedCodeSize(const Instruction* src, int codeSize)
{
    int size = 0;
    const Instruction* end = src + codeSize;
    while (src < end)
    {
        Instruction buffer[32];
        Instruction* dst = buffer;
        ConvertInstruction(dst, src);
        ASSERT(dst < buffer + 32);
        size += static_cast<int>(dst - buffer);
    }
    return size;
}

static int GetJumpAdjustment(const char* adjustment, int jump)
{
    int result = 0;
    if (jump > 0)
    {
        for (int i = 1; i <= jump; ++i)
        {
            result += adjustment[i];
        }
    }
    else if (jump < 0)
    {
        for (int i = -1; i > jump; --i)
        {
            result -= adjustment[i];
        }
    }
    return result;
}

static void AdjustJump(Instruction* dst, const char* adjustment)
{

    Opcode opcode = VM_GET_OPCODE(*dst); 
    Instruction inst = *dst;

    switch (opcode)
    {
    case Opcode_Jmp:
        {
            int sbx = VM_GET_sD(inst);
            sbx += GetJumpAdjustment(adjustment, sbx);
            *dst = EncodeAsD(opcode, 0, sbx);
        }
        break;
    case Opcode_ForLoop:
    case Opcode_ForPrep:
        {
            int a  = VM_GET_A(inst);
            int sd = VM_GET_sD(inst);
            sd += GetJumpAdjustment(adjustment, sd);
            *dst = EncodeAsD(opcode, a, sd);
        }
    }

}

/** Translates an instruction block from standard Lua opcodes to our own encoding.
 * src and dst can be the same. */
static void Prototype_ConvertCode(lua_State* L, Instruction* _dst, const Instruction* src, int* dstLine, const int* srcLine, int dstCodeSize, int codeSize)
{

    // Track how much jumps needs to be adjusted based on the change
    // in size of instructions during our conversion process.
    char* adjustment = AllocateArray<char>(L, codeSize);
    memset( adjustment, 0, sizeof(char) * codeSize );

    char* dstSizes = AllocateArray<char>(L, dstCodeSize);
    memset( dstSizes, 0, sizeof(char) * dstCodeSize );

    char* srcSizes = AllocateArray<char>(L, codeSize);
    memset( srcSizes, 0, sizeof(char) * codeSize );

    int dstIp = 0;
    int srcIp = 0;

    Instruction* dst = _dst;
    const Instruction* end = src + codeSize;

    while (src < end)
    {
        
        const Instruction* s = src;
        const Instruction* d = dst;

        ConvertInstruction(dst, src);

        int srcSize = static_cast<int>(src - s);
        int dstSize = static_cast<int>(dst - d);

        adjustment[srcIp] = dstSize - srcSize;
        
        dstSizes[dstIp] = dstSize;
        srcSizes[srcIp] = srcSize;

        for (int i = 0; i < dstSize; ++i)
        {
            dstLine[i] = *srcLine;
        }

        srcLine += srcSize;
        dstLine += dstSize;

        dstIp += dstSize;
        srcIp += srcSize;

    }

    // Adjust any jumps.

    dstIp = 0;
    srcIp = 0;

    while (dstIp < dstCodeSize)
    {
        int srcSize = srcSizes[srcIp];
        int dstSize = dstSizes[dstIp];

        AdjustJump(_dst + dstIp, adjustment + srcIp);

        dstIp += dstSize;
        srcIp += srcSize;
    }

    FreeArray<char>(L, adjustment, codeSize);
    FreeArray<char>(L, dstSizes, dstCodeSize);
    FreeArray<char>(L, srcSizes, codeSize);

}

void Prototype_ConvertCode(lua_State* L, Prototype* prototype)
{
    
    ASSERT(prototype->convertedCode == NULL);

    int convertedCodeSize = Prototype_GetConvertedCodeSize(prototype->code, prototype->codeSize);
    prototype->convertedCode = AllocateArray<Instruction>(L, convertedCodeSize);
    prototype->convertedCodeSize = convertedCodeSize;

    if (prototype->sourceLine)
    {
        prototype->convertedSourceLine = AllocateArray<int>(L, convertedCodeSize);
    }

    Prototype_ConvertCode(L,
        prototype->convertedCode,
        prototype->code,
        prototype->convertedSourceLine,
        prototype->sourceLine,
        prototype->convertedCodeSize,
        prototype->codeSize);
    
    for (int i = 0; i < prototype->numPrototypes; ++i)
    {
        Prototype_ConvertCode(L, prototype->prototype[i]);
    }

}

static Prototype* Prototype_Create(lua_State* L, Prototype* parent, const char* data, size_t& length)
{

    Gc* gc = &L->gc;

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

    int convertedCodeSize = Prototype_GetConvertedCodeSize(reinterpret_cast<const Instruction*>(code), codeSize);

    // Create the function object.
    Prototype* prototype = Prototype_Create(L, codeSize, convertedCodeSize, numConstants, numPrototypes, numUpValues);
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
    Gc_IncrementReference(gc, prototype, prototype->source);

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
            
        Gc_IncrementReference(gc, prototype, &prototype->constant[i]);

    }

    memcpy(prototype->code, code, sizeof(Instruction) * codeSize);

    for (int i = 0; i < numPrototypes; ++i)
    {
        size_t length = 0;
        prototype->prototype[i] = Prototype_Create(L, prototype, prototypes, length);
        Gc_IncrementReference(gc, prototype, prototype->prototype[i]);
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
        Gc_IncrementReference(gc, prototype, prototype->upValue[i]);
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

void Prototype_Destroy(lua_State* L, Prototype* prototype, bool releaseRefs)
{

    if (releaseRefs)
    {
        Gc* gc = &L->gc;
        for (int i = 0; i < prototype->numConstants; ++i)
        {
            Gc_DecrementReference(L, gc, &prototype->constant[i]);
        }
        for (int i = 0; i < prototype->numUpValues; ++i)
        {
            Gc_DecrementReference(L, gc, prototype->upValue[i]);
        }
        for (int i = 0; i < prototype->numPrototypes; ++i)
        {
            Gc_DecrementReference(L, gc, prototype->prototype[i]);
        }
        if (prototype->source != NULL)
        {
            Gc_DecrementReference(L, gc, prototype->source);
        }
    }

    Free(L, prototype->code, prototype->codeSize * sizeof(Instruction));
    Free(L, prototype->convertedCode, prototype->convertedCodeSize * sizeof(Instruction));
    Free(L, prototype->constant, prototype->numConstants * sizeof(Value));
    Free(L, prototype->upValue, prototype->maxUpValues * sizeof(String*));
    Free(L, prototype->prototype, prototype->numPrototypes * sizeof(Prototype*));
    Free(L, prototype->local, prototype->numLocals * sizeof(LocVar));

    if (prototype->sourceLine != NULL)
    {
        Free(L, prototype->sourceLine, prototype->codeSize * sizeof(int));
    }
    if (prototype->convertedSourceLine != NULL)
    {
        Free(L, prototype->convertedSourceLine, prototype->convertedCodeSize * sizeof(int));
    }

    Free(L, prototype, sizeof(Prototype));
}

Closure* Closure_Create(lua_State* L, Prototype* prototype, Table* env)
{

    ASSERT(env != NULL);

    Gc* gc = &L->gc;

    size_t size = sizeof(Closure);
    size += prototype->numUpValues * sizeof(UpValue*);

    Closure* closure = static_cast<Closure*>(Gc_AllocateObject(L, LUA_TFUNCTION, size));
    closure->c = false;
    
    Gc_IncrementReference(gc, closure, env);
    closure->env = env;

    Gc_IncrementReference(gc, closure, prototype);
    closure->lclosure.prototype = prototype;

    closure->lclosure.upValue   = reinterpret_cast<UpValue**>(closure + 1);
    closure->lclosure.numUpValues = prototype->numUpValues;
    memset(closure->lclosure.upValue, 0, sizeof(UpValue*) * prototype->numUpValues);

    return closure;

}

Closure* Closure_Create(lua_State* L, lua_CFunction function, const Value upValue[], int numUpValues, Table* env)
{

    ASSERT(env != NULL);

    Gc* gc = &L->gc;

    size_t size = sizeof(Closure);
    size += numUpValues * sizeof(Value);

    Closure* closure = static_cast<Closure*>(Gc_AllocateObject(L, LUA_TFUNCTION, size));

    Gc_IncrementReference(gc, closure, env);
    closure->env = env;

    closure->c = true;
    closure->cclosure.function      = function;

    closure->cclosure.upValue       = reinterpret_cast<Value*>(closure + 1);
    closure->cclosure.numUpValues   = numUpValues;
    memcpy(closure->cclosure.upValue, upValue, numUpValues * sizeof(Value));

    for (int i = 0; i < numUpValues; ++i)
    {
        Gc_IncrementReference(gc, closure, &upValue[i]);
    }

    return closure;

}

void Closure_Destroy(lua_State* L, Closure* closure, bool releaseRefs)
{

    Gc* gc = &L->gc;

    if (releaseRefs)
    {
        // Release the environment table.
        Gc_DecrementReference(L, gc, closure->env);

        // Release the up values.
        if (closure->c)
        {
            Value* upValue = closure->cclosure.upValue;
            for (int i = 0; i < closure->cclosure.numUpValues; ++i)
            {
                Gc_DecrementReference(L, gc, upValue);
                ++upValue;
            }
        }
        else
        {
            UpValue** upValue = closure->lclosure.upValue;
            for (int i = 0; i < closure->lclosure.numUpValues; ++i)
            {
                Gc_DecrementReference(L, gc, *upValue);
                ++upValue;
            }
        }

    }

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

