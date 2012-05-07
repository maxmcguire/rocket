/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

extern "C"
{
#include "lua.h"
}

#include "Opcode.h"
#include "Vm.h"
#include "State.h"
#include "Gc.h"
#include "String.h"
#include "Table.h"
#include "Function.h"

#include <memory.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

struct CallArgs
{
    Value*  value;
    int     numArgs;
    int     numResults;
    Value*  errorFunc;
};

// Limit for table tag-method chains (to avoid loops)
#define MAXTAGLOOP	100

// Returns the line where we're currently executing in the function.
static int GetCurrentLine(CallFrame* frame)
{
    if (frame->function == NULL)
    {
        return -1;
    }
    Closure* closure = frame->function->closure;
    if (closure->c)
    {
        return -1;
    }
    Prototype* prototype = closure->lclosure.prototype;
    // Note, ip will be the next instruction we execute, so we need to
    // subtract one.
    int instruction = static_cast<int>(frame->ip - prototype->code - 1);
    return prototype->sourceLine[instruction];
}

Closure* Vm_GetCurrentFunction(lua_State* L)
{
    // The first element on the stack represents calling from C and has no
    // closure.
    if (L->callStackTop - 1 > L->callStackBase)
    {
        return (L->callStackTop - 1)->function->closure;
    }
    return NULL;    
}

void Vm_Error(lua_State* L, const char* format, ...)
{

    CallFrame* frame = State_GetCallFrame(L);

    // Add file:line information
    int line = GetCurrentLine(frame);
    if (line != -1)
    {
        char name[LUA_IDSIZE];
        Prototype_GetName(frame->function->closure->lclosure.prototype, name, LUA_IDSIZE);
        PushFString(L, "%s:%d ", name, line);
    }

    va_list argp;
    va_start(argp, format);
    PushVFString(L, format, argp);
    va_end(argp);

    if (line != -1)
    {
        // Concatenate the line info and the error message.
        Concat(L, 2);
    }

    State_Error(L);

}

static bool GetObjectName(lua_State* L, const CallFrame* frame, const Value* value, const char*& name, const char*& kind)
{

    if (frame->function == NULL || frame->function->closure->c)
    {
        // We only have information about Lua functions.
        return false;
    }

    // Check if the value is on the stack.
    if (value >= frame->stackBase && value < frame->stackTop)
    {
        int a = 0;
    }

    return false;

}

// Generates an error based on performing an operation on a value of an
// incorrect type. The op parameter is a string specifing the operation
// such as "call", "index", "perform arithmetic on", etc.
static void TypeError(lua_State* L, const Value* value, const char* op)
{

    const char* type = State_TypeName(L, Value_GetType(value));
    const char* name = NULL;
    const char* kind = NULL;

    if (GetObjectName(L, State_GetCallFrame(L), value, name, kind))
    {
        Vm_Error(L, "attempt to %s %s '%s' (a %s value)", op, kind, name, type);
    }
    else
    {
        Vm_Error(L, "attempt to %s a %s value", op, type);
    }

}

static void ArithmeticError(lua_State* L, const Value* arg1, const Value* arg2)
{
    const Value* arg = Value_GetIsNumber(arg1) ? arg2 : arg1;
    TypeError(L, arg, "perform arithmetic on");
}

static void ComparisonError(lua_State *L, const Value* arg1, const Value* arg2)
{
    const char* type1 = State_TypeName(L, Value_GetType(arg1));
    const char* type2 = State_TypeName(L, Value_GetType(arg2));
    if (type1 == type2)
    {
        Vm_Error(L, "attempt to compare two %s values", type1);
    }
    else
    {
        Vm_Error(L, "attempt to compare %s with %s", type1, type2);
    }
}

static void ConcatError(lua_State* L, const Value* arg1, const Value* arg2)
{
    if (Value_GetIsString(arg1) || Value_GetIsNumber(arg1))
    {
        arg1 = arg2;
    }
    TypeError(L, arg1, "concatenate");
}

static Value* GetTagMethod(lua_State* L, const Value* value, TagMethod method)
{
    Table* metatable = Value_GetMetatable(L, value);
    if (metatable != NULL)
    {
        return Table_GetTable(L, metatable, L->tagMethodName[method]);
    }
    return NULL;
}

/** Selects the tag method to used based on the two arguments to a binary operation. */
static Value* GetBinaryTagMethod(lua_State* L, const Value* arg1, const Value* arg2, TagMethod method)
{
    Value* result = GetTagMethod(L, arg1, method);
    if (result == NULL)
    {
        result = GetTagMethod(L, arg2, method);
    }
    return result;
}

static void CallTagMethod1Result(lua_State* L, const Value* method, const Value* arg1, Value* result)
{
    PushValue(L, method);
    PushValue(L, arg1);
    Vm_Call(L, L->stackTop - 2, 1, 1);
    *result = *(L->stackTop - 1);
    Pop(L, 1);
}

static void CallTagMethod2Result(lua_State* L, const Value* method, const Value* arg1, const Value* arg2, Value* result)
{
    PushValue(L, method);
    PushValue(L, arg1);
    PushValue(L, arg2);
    Vm_Call(L, L->stackTop - 3, 2, 1);
    *result = *(L->stackTop - 1);
    Pop(L, 1);
}

static void CallTagMethod3(lua_State* L, const Value* method, const Value* arg1, const Value* arg2, const Value* arg3)
{
    PushValue(L, method);
    PushValue(L, arg1);
    PushValue(L, arg2);
    PushValue(L, arg3);
    Vm_Call(L, L->stackTop - 4, 3, 0);
}

static void CallTagMethod3Result(lua_State* L, const Value* method, const Value* arg1, const Value* arg2, const Value* arg3, Value* result)
{
    PushValue(L, method);
    PushValue(L, arg1);
    PushValue(L, arg2);
    PushValue(L, arg3);
    Vm_Call(L, L->stackTop - 4, 3, 1);
    *result = *(L->stackTop - 1);
    Pop(L, 1);
}

void Vm_SetTable(lua_State* L, Value* dst, Value* key, Value* value)
{

    if (Value_GetIsNil(key))
    {
        Vm_Error(L, "table index is nil");
    }

    for (int i = 0; i < MAXTAGLOOP; ++i)
    {

        Value* method = NULL;

        if (Value_GetIsTable(dst))
        {

            Table* table = dst->table;
            if (Table_Update(L, table, key, value))
            {
                return;
            }
        
            // The key doesn't exist in the table, so we need to call the
            // __newindex metamethod (if it exist), or insert the key.
            method = GetTagMethod(L, dst, TagMethod_NewIndex);
            if (method == NULL)
            {
                if (!Value_GetIsNil(value))
                {
                    Table_Insert(L, table, key, value);
                }
                return;
            }

        }
        else
        {
            method = GetTagMethod(L, dst, TagMethod_NewIndex);
            if (method == NULL)
            {
                TypeError(L, dst, "newindex");
            }
        }

        // If __newindex is a function, call it.
        if (Value_GetIsFunction(method))
        {
            CallTagMethod3(L, method, dst, key, value);       
            return;
        }

        // Repeat with the tag method.
        dst = method;
    
    }

}

void Vm_GetTable(lua_State* L, const Value* value, const Value* key, Value* dst, bool ref)
{
    for (int i = 0; i < MAXTAGLOOP; ++i)
    {

        const Value* method = NULL;
        if (Value_GetIsTable(value))
        {
            const Value* result = Table_GetTable(L, value->table, key);
            if (result != NULL)
            {
                *dst = *result;
                return;
            }
        }
        method = GetTagMethod(L, value, TagMethod_Index);
        if (method == NULL)
        {
            if (!Value_GetIsTable(value))
            {
                // No metamethod.
                TypeError(L, value, "index");
            }
            break;
        }
        if (Value_GetIsFunction(method))
        {
            Value refValue;
            SetValue(&refValue, ref);
            CallTagMethod3Result(L, method, value, key, &refValue, dst);
            return;
        }
        else
        {
            // Assume the tag method is a table.
            value = method;
        }
    }
    SetNil(dst);
}

void Vm_GetGlobal(lua_State* L, Closure* closure, const Value* key, Value* dst)
{
    Value table;
    SetValue(&table, closure->env);
    Vm_GetTable(L, &table, key, dst, false);
}

void Vm_SetGlobal(lua_State* L, Closure* closure, Value* key, Value* value)
{
    Value table;
    SetValue(&table, closure->env);
    Vm_SetTable(L, &table, key, value);
}

// Overwrites the value of an element on the stack.
static inline void SetValue(lua_State* L, int index, const Value* value)
{
    if (value == NULL)
    {
        SetNil(L->stackBase + index);
    }
    else
    {
        L->stackBase[index] = *value;
    }
}

/**
 * Moves the results for a return operation to the base of the stack. Returns the
 * actual number of results that were returned.
 */
static int MoveResults(lua_State* L, Value* dst, Value* src, int numResults)
{
    // R(-1) = R(a), R(0) = R(start + 1), etc.
    if (numResults < 0)
    {
        numResults = static_cast<int>(L->stackTop - src);
    }
    for (int i = 0; i < numResults; ++i)
    {
        *dst = *src;
        ++src;
        ++dst;
    }
    return numResults;
}

/** Calls a comparison tag method and returns the result (0 for false, 1 for true).
If there is no appropriate tag method, the function returns -1 */
static int ComparisionTagMethod(lua_State* L, const Value* arg1, const Value* arg2, TagMethod tm)
{

    assert(arg1->tag == arg2->tag);
    
    const Value* method1 = GetTagMethod(L, arg1, tm);
    
    if (method1 == NULL)
    {
        return -1;
    }

    const Value* method2 = GetTagMethod(L, arg2, tm);
    if (method2 == NULL || !Value_Equal(method1, method2))
    {
        return -1;
    }
    
    PushValue(L, method1);
    PushValue(L, arg1);
    PushValue(L, arg2);
    Vm_Call(L, L->stackTop - 3, 2, 1);

    int result = Vm_GetBoolean(L->stackTop - 1);
    Pop(L, 1);    

    return result;

}

void Vm_UnaryMinus(lua_State* L, const Value* arg, Value* dst)
{
    if (Value_GetIsNumber(arg))
    {
        SetValue( dst, -arg->number );
    }
    else
    {
        const Value* method = GetTagMethod(L, arg, TagMethod_Unm);
        if (method == NULL)
        {
            ArithmeticError(L, arg, NULL);
        }
        CallTagMethod1Result(L, method, arg, dst);
    }
}

int Vm_Equal(lua_State* L, const Value* arg1, const Value* arg2)
{
    if (arg1->tag == arg2->tag)
    {
        if (Value_Equal(arg1, arg2))
        {
            return 1;
        }
        int result = ComparisionTagMethod(L, arg1, arg2, TagMethod_Eq);
        if (result != -1)
        {
            return result;
        }
    }
    return 0;
}

int Vm_Less(lua_State* L, const Value* arg1, const Value* arg2)
{
    if (arg1->tag == arg2->tag)
    {
        if (Value_GetIsNumber(arg1))
        {
            return arg1->number < arg2->number;
        }
        else if (Value_GetIsString(arg1))
        {
            return String_Compare(arg1->string, arg2->string) < 0;
        }
        int result = ComparisionTagMethod(L, arg1, arg2, TagMethod_Lt);
        if (result != -1)
        {
            return result;
        }
    }
    ComparisonError(L, arg1, arg2);
    return 0;
}

int Vm_LessEqual(lua_State* L, const Value* arg1, const Value* arg2)
{
    if (arg1->tag == arg2->tag)
    {
        if (Value_GetIsNumber(arg1))
        {
            return arg1->number <= arg2->number;
        }
        else if (Value_GetIsString(arg1))
        {
            return String_Compare(arg1->string, arg2->string) <= 0;
        }
        int result = ComparisionTagMethod(L, arg1, arg2, TagMethod_Le);
        if (result != -1)
        {
            return result;
        }
        result = ComparisionTagMethod(L, arg2, arg1, TagMethod_Lt);
        if (result != -1)
        {
            return !result;
        }
    }
    ComparisonError(L, arg1, arg2);
    return 0;
}

void Vm_Concat(lua_State* L, Value* dst, Value* arg1, Value* arg2)
{

    if ( (!Value_GetIsString(arg1) && !Value_GetIsNumber(arg1)) || !ToString(L, arg2) )
    {
        Value* method = GetBinaryTagMethod(L, arg1, arg2, TagMethod_Concat);
        if (method == NULL)
        {
            ConcatError(L, arg1, arg2);
        }
        CallTagMethod2Result(L, method, arg1, arg2, dst);       
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

}

int Vm_GetBoolean(const Value* value)
{
    if (Value_GetIsNil(value) || (Value_GetIsBoolean(value) && !value->boolean))
    {
        return 0;
    }
    return 1;
}

const char* GetString(const Value* value)
{
    if (Value_GetIsString(value))
    {
        return String_GetData(value->string);
    }
    return NULL;
}

bool Vm_GetNumber(const Value* value, lua_Number* result)
{
    if (Value_GetIsNumber(value))
    {
        *result = value->number;
        return true;
    }
    else if (Value_GetIsString(value))
    {
        const char* string = String_GetData(value->string);
        
        char* end;
        *result = lua_str2number(string, &end);

        if (end == string)
        {
            // The conversion failed.
            return false;
        }
        if (*end == 'x' || *end == 'X')
        {
            // Try converting as a hexadecimal number.
            *result = strtoul(string, &end, 16);
        }
        // Allow trailing spaces.
        while (isspace(*end))
        {
            ++end;
        }
        if (*end == '\0')
        {
            // Converted the entire string.
            return true;
        }
    }
    return false;
}

/** Converts a value inplace to a number. */
bool Vm_ToNumber(Value* value)
{
    if (!Value_GetIsNumber(value))
    {
        lua_Number result;
        if (!Vm_GetNumber(value, &result))
        {
            return false;
        }
        SetValue(value, result);
    }
    return true;
}

static lua_Number GetValueLength(lua_State* L, const Value* value)
{
    if (Value_GetIsString(value))
    {
        return static_cast<lua_Number>( value->string->length );
    }
    else if (Value_GetIsTable(value))
    {
        return static_cast<lua_Number>( Table_GetSize(L, value->table) );  
    }
    return 0.0;
}

static inline lua_Number Number_Add(lua_Number a, lua_Number b)
{
    return luai_numadd(a, b);
}

static inline lua_Number Number_Sub(lua_Number a, lua_Number b)
{
    return luai_numsub(a, b);
}

static inline lua_Number Number_Mul(lua_Number a, lua_Number b)
{
    return luai_nummul(a, b);
}

static inline lua_Number Number_Div(lua_Number a, lua_Number b)
{
    return luai_numdiv(a, b);
}

static inline lua_Number Number_Mod(lua_Number a, lua_Number b)
{
    return luai_nummod(a, b);
}

static inline lua_Number Number_Pow(lua_Number a, lua_Number b)
{
    return luai_numpow(a, b);
}

/**
 * Performs an arithmetic operation between two values calling a tag method if
 * necessary.
 */
template <lua_Number (*Op)(lua_Number, lua_Number), TagMethod tag>
static void Arithmetic(lua_State* L, Value* dst, const Value* arg1, const Value* arg2)
{
    lua_Number a, b;
    if (Vm_GetNumber(arg1, &a) && Vm_GetNumber(arg2, &b))
    {
        SetValue(dst, Op(a, b));
    }
    else
    {
        Value* method = GetBinaryTagMethod(L, arg1, arg2, tag);
        if (method == NULL)
        {
            ArithmeticError(L, arg1, arg2);
        }
        CallTagMethod2Result(L, method, arg1, arg2, dst);
    }
    
}

extern "C" int Vm_Execute(lua_State* L, LClosure* closure);

/**
 * Executes the function on the top of the call stack.
 */
static int Execute(lua_State* L, int numArgs)
{

    // Assembly language VM.
    /*
    {
        CallFrame* frame = State_GetCallFrame(L);
        LClosure* closure = &frame->function->closure->lclosure;
        return Vm_Execute(L, closure);
    }
    */

    // Resolves a pseudoindex into a constant or a s5tack value.
    #define RESOLVE_RK(c)   \
        ((c) & 256) ? &constant[(c) & 255] : &stackBase[(c)]

    // Anything inside this function that can generate an error should be
    // wrapped in this macro which synchronizes the cached local variables.
    #define PROTECT(x) \
        frame->ip = ip; { x; }

    // Form of arithmetic operators.
    #define ARITHMETIC(dst, arg1, arg2, op, tag)                                \
        if (Value_GetIsNumber((arg1)) && Value_GetIsNumber((arg2)))             \
        {                                                                       \
            lua_Number a = (arg1)->number;                                      \
            lua_Number b = (arg2)->number;                                      \
            (dst)->number = op(a, b);                                           \
        }                                                                       \
        else                                                                    \
        {                                                                       \
            PROTECT(                                                            \
                (Arithmetic<op, tag>(L, dst, arg1, arg2));                      \
            )                                                                   \
        }

    CallFrame* frame = State_GetCallFrame(L );
    Closure* closure = frame->function->closure;

    assert( !closure->c );
    LClosure* lclosure = &closure->lclosure;

    Prototype* prototype = lclosure->prototype;

    const Instruction* ip  = prototype->code;
    const Instruction* end = ip + prototype->codeSize;

    register Value* stackBase = L->stackBase;
    register Value* constant  = prototype->constant;

    while (ip < end)
    {

        Instruction inst = *ip;
        ++ip;

        Opcode opcode = GET_OPCODE(inst); 
        int a = GET_A(inst);

        switch (opcode)
        {
        case Opcode_Move:
            {
                int b = GET_B(inst);
                stackBase[a] = L->stackBase[b];
            }
            break;
        case Opcode_LoadK:
            {
                int bx = GET_Bx(inst);
                assert(bx >= 0 && bx < prototype->numConstants);
                const Value* value = &constant[bx];
                stackBase[a] = *value;
            }
            break;
        case Opcode_LoadNil:
            {
                int b = GET_B(inst);
                for (int i = a; i <= b; ++i)
                {
                    SetNil(stackBase + i);
                }
            }
            break;
        case Opcode_LoadBool:
            {
                SetValue( &stackBase[a], GET_B(inst) != 0 );
                ip += GET_C(inst);
            }
            break;
        case Opcode_Self:
            {
                PROTECT(
                    int b = GET_B(inst);
                    const Value* key = RESOLVE_RK( GET_C(inst) );
                    stackBase[a + 1] = stackBase[b];
                    Vm_GetTable(L, &stackBase[b], key, &stackBase[a], false);
                )
            }
            break;
        case Opcode_Jmp:
            {
                int sbx = GET_sBx(inst);
                ip += sbx;
            }
            break;
        case Opcode_SetGlobal:
            {
                PROTECT(
                    int bx = GET_Bx(inst);
                    assert(bx >= 0 && bx < prototype->numConstants);
                    Value* key = &constant[bx];
                    Value* value = &stackBase[a];
                    Vm_SetGlobal(L, closure, key, value);
                )
            }
            break;
        case Opcode_GetGlobal:
            {
                PROTECT(
                    int bx = GET_Bx(inst);
                    assert(bx >= 0 && bx < prototype->numConstants);
                    const Value* key = &constant[bx];
                    Value* dst = &L->stackBase[a];
                    Vm_GetGlobal(L, closure, key, dst);
                )
            }
            break;
        case Opcode_SetUpVal:
            {
                const Value* value = &stackBase[a];
                SetUpValue(lclosure, GET_B(inst), value);                    
            }
            break;
        case Opcode_GetUpVal:
            {
                const Value* value = GetUpValue(lclosure, GET_B(inst));
                SetValue(L, a, value);                    
            }
            break;
        case Opcode_GetTable:
            {
                PROTECT(
                    int b = GET_B(inst);
                    const Value* table = &stackBase[b];
                    const Value* key   = RESOLVE_RK( GET_C(inst) );
                    Vm_GetTable(L, table, key, &L->stackBase[a], false);
                )
            }
            break;
        case Opcode_GetTableRef:
            {
                PROTECT(
                    int b = GET_B(inst);
                    const Value* table = &stackBase[b];
                    const Value* key   = RESOLVE_RK( GET_C(inst) );
                    Vm_GetTable(L, table, key, &L->stackBase[a], true);
                )
            }
            break;
        case Opcode_SetTable:
            {
                PROTECT(
                    Value* table = &stackBase[a];
                    Value* key   = RESOLVE_RK( GET_B(inst) );
                    Value* value = RESOLVE_RK( GET_C(inst) );
                    Vm_SetTable(L, table, key, value);
                )
            }
            break;
        case Opcode_Call:
            {
                PROTECT(
                    int numArgs     = GET_B(inst) - 1;
                    int numResults  = GET_C(inst) - 1;
                    Value* value   = &stackBase[a];
                    if (numArgs >= 0)
                    {
                        L->stackTop = value + numArgs;
                    }
                    Vm_Call(L, value, numArgs, numResults);
                    if (numResults >= 0)
                    {
                        // If we are expecting a specific number of results, then
                        // we have already prepared the destination registers in
                        // such a way that we don't want to adjust the stack top.
                        L->stackTop = frame->stackTop; 
                    }
                )
            }
            break;
        case Opcode_TailCall:
            {
                PROTECT(
                    // TODO: Implement as an actual tail call (more efficient).
                    int numArgs     = GET_B(inst) - 1;
                    Value* value   = &stackBase[a];
                    if (numArgs == -1)
                    {
                        // Use all of the values on the stack as arguments.
                        numArgs = static_cast<int>(L->stackTop - value) - 1;
                    }
                    Vm_Call(L, value, numArgs, -1);
                    int numResults = static_cast<int>(L->stackTop - value);
                    numResults = MoveResults(L, frame->function, &stackBase[a], numResults);
                    if (L->openUpValue != NULL)
                    {
                        CloseUpValues(L, stackBase);
                    }
                )
            }
            break;
        case Opcode_Return:
            {
                int numResults = GET_B(inst) - 1;
                numResults = MoveResults(L, frame->function, &stackBase[a], numResults);
                if (L->openUpValue != NULL)
                {
                    CloseUpValues(L, stackBase);
                }
                return numResults;
            }
            break;
        case Opcode_Add:
            {
                Value* dst         = &stackBase[a];
                const Value* arg1  = RESOLVE_RK( GET_B(inst) );
                const Value* arg2  = RESOLVE_RK( GET_C(inst) );
                ARITHMETIC( dst, arg1, arg2, Number_Add, TagMethod_Add );
            }
            break;
        case Opcode_Sub:
            {
                Value* dst         = &stackBase[a];
                const Value* arg1  = RESOLVE_RK( GET_B(inst) );
                const Value* arg2  = RESOLVE_RK( GET_C(inst) );
                ARITHMETIC( dst, arg1, arg2, Number_Sub, TagMethod_Sub );
            }
            break;
        case Opcode_Mul:
            {
                Value* dst         = &stackBase[a];
                const Value* arg1  = RESOLVE_RK( GET_B(inst) );
                const Value* arg2  = RESOLVE_RK( GET_C(inst) );
                ARITHMETIC( dst, arg1, arg2, Number_Mul, TagMethod_Mul );
            }
            break;
        case Opcode_Div:
            {
                Value* dst         = &stackBase[a];
                const Value* arg1  = RESOLVE_RK( GET_B(inst) );
                const Value* arg2  = RESOLVE_RK( GET_C(inst) );
                ARITHMETIC( dst, arg1, arg2, Number_Div, TagMethod_Div );
            }
            break;
        case Opcode_Mod:
            {
                Value* dst         = &stackBase[a];
                const Value* arg1  = RESOLVE_RK( GET_B(inst) );
                const Value* arg2  = RESOLVE_RK( GET_C(inst) );
                ARITHMETIC( dst, arg1, arg2, Number_Mod, TagMethod_Mod );
            }
            break;
        case Opcode_Pow:
            {
                Value* dst         = &stackBase[a];
                const Value* arg1  = RESOLVE_RK( GET_B(inst) );
                const Value* arg2  = RESOLVE_RK( GET_C(inst) );
                ARITHMETIC( dst, arg1, arg2, Number_Pow, TagMethod_Pow );
            }
            break;
        case Opcode_Unm:
            {
                PROTECT(
                    int b = GET_B(inst);
                    Value* dst         = &stackBase[a];
                    const Value* src   = &stackBase[b];
                    Vm_UnaryMinus(L, src, dst);
                )
            }
            break;
        case Opcode_Eq:
            {
                PROTECT(
                    const Value* arg1 = RESOLVE_RK( GET_B(inst) );
                    const Value* arg2 = RESOLVE_RK( GET_C(inst) );
                    if (Vm_Equal(L, arg1, arg2) != a)
                    {
                        ++ip;
                    }
                )
            }
            break;
        case Opcode_Lt:
            {
                PROTECT(
                    const Value* arg1 = RESOLVE_RK( GET_B(inst) );
                    const Value* arg2 = RESOLVE_RK( GET_C(inst) );
                    if (Vm_Less(L, arg1, arg2) != a)
                    {
                        ++ip;
                    }
                )
            }
            break;
        case Opcode_Le:
            {
                PROTECT(
                    const Value* arg1 = RESOLVE_RK( GET_B(inst) );
                    const Value* arg2 = RESOLVE_RK( GET_C(inst) );
                    if (Vm_LessEqual(L, arg1, arg2) != a)
                    {
                        ++ip;
                    }
                )
            }
            break;
        case Opcode_NewTable:
            {
                SetValue( &stackBase[a], Table_Create(L) );
            }
            break;
        case Opcode_Closure:
            {

                int bx = GET_Bx(inst);

                Prototype* p = prototype->prototype[bx];
                Closure* c = Closure_Create(L, p, frame->function->closure->env);

                for (int i = 0; i < p->numUpValues; ++i)
                {
                    int inst = *ip;
                    ++ip;
                    int b = GET_B(inst);
                    if ( GET_OPCODE(inst) == Opcode_Move )
                    {
                        c->lclosure.upValue[i] = NewUpValue(L, &stackBase[b]);
                    }
                    else
                    {
                        assert( GET_OPCODE(inst) == Opcode_GetUpVal );
                        c->lclosure.upValue[i] = lclosure->upValue[b];
                    }
                }

                SetValue( &stackBase[a], c );

            }
            break;
        case Opcode_Close:
            {
                CloseUpValues(L, &stackBase[a]);
            }
            break;
        case Opcode_ForPrep:
            {
                // Make sure the initial value, limit and step are all numbers
                if (!Vm_ToNumber(&stackBase[a]))
                {
                    Vm_Error(L, "initial value must be a number");
                }
                if (!Vm_ToNumber(&stackBase[a + 1]))
                {
                    Vm_Error(L, "limit must be a number");
                }
                if (!Vm_ToNumber(&stackBase[a + 2]))
                {
                    Vm_Error(L, "step must be a number");
                }
                int sbx = GET_sBx(inst);
                stackBase[a].number -= stackBase[a + 2].number;
                ip += sbx;
            }
            break;
        case Opcode_ForLoop:
            {
                Value* iterator = &stackBase[a];

                assert( Value_GetIsNumber(&stackBase[a + 2]) );
                assert( Value_GetIsNumber(&stackBase[a + 1]) );
                
                lua_Number step  = stackBase[a + 2].number;
                lua_Number limit = stackBase[a + 1].number;
                
                iterator->number += step;

                // We need to alter the end test based on whether or not the step
                // is positive or negative.
                if (luai_numlt(0, step) ? luai_numle(iterator->number, limit) : luai_numle(limit, iterator->number))
                {
                    int sbx = GET_sBx(inst);
                    ip += sbx;
                    CopyValue( &stackBase[a + 3], iterator );
                }
            }
            break;
        case Opcode_TForLoop:
            {
                PROTECT(
                    int numResults = GET_C(inst);
                    Value* base = &stackBase[a + 3];

                    // Move the function and parameters into place.
                    base[0] = stackBase[a];     // Iterator function.
                    base[1] = stackBase[a + 1]; // State.
                    base[2] = stackBase[a + 2]; // Enumeration index.

                    Value* top = L->stackTop;
                    L->stackTop = base + 3;
                    
                    Vm_Call(L, base, 2, numResults);
                    L->stackTop = top;

                    if (!Value_GetIsNil(base))
                    {
                        stackBase[a + 2] = stackBase[a + 3];
                    }
                    else
                    {
                        ++ip;
                    }
                )
            }
            break;
        case Opcode_Test:
            {
                int c = GET_C(inst);
                const Value* value = &stackBase[a];
                if ( Vm_GetBoolean( value ) != c )
                {
                    ++ip;
                }
            }
            break;
        case Opcode_TestSet:
            {
                int b = GET_B(inst);
                int c = GET_C(inst);
                const Value* value = &stackBase[b];
                if ( Vm_GetBoolean( value ) != c )
                {
                    ++ip;
                }
                else
                {
                    stackBase[a] = *value;
                }
            }
            break;
        case Opcode_Not:
            {
                int b = GET_B(inst);
                Value* dst         = &stackBase[a];
                const Value* src   = &stackBase[b];
                SetValue( dst, Vm_GetBoolean(src) == 0 );
            }
            break;
        case Opcode_Concat:
            {
                PROTECT(
                    int b = GET_B(inst);
                    int c = GET_C(inst);
                    Value* dst     = &stackBase[a];
                    Value* start   = &stackBase[b];
                    Value* end     = &stackBase[c];
                    Concat( L, dst, start, end );
                )
            }
            break;
        case Opcode_SetList:
            {
                PROTECT(
                    Value* dst = &stackBase[a];
                    assert( Value_GetIsTable(dst) );
                    Table* table = dst->table;
                    int b = GET_B(inst);
                    int c = GET_C(inst);
                    if (c == 0)
                    {
                        c = *static_cast<const int*>(ip);
                        ++ip;
                    }
                    int offset = (c - 1) * LFIELDS_PER_FLUSH;
                    if (b == 0)
                    {
                        // Initialize will all of the elements on the stack.
                        b = static_cast<int>(L->stackTop - stackBase) - a - 1;
                    }
                    for (int i = 1; i <= b; ++i)
                    {
                        Value* value = &stackBase[a + i];
                        Table_SetTable(L, table, i + offset, value);
                    }
                )
            }
            break;
        case Opcode_Len:
            {
                PROTECT(
                    int b = GET_B(inst);
                    Value* dst         = &stackBase[a];
                    const Value* arg   = &stackBase[b];
                    SetValue( dst, GetValueLength(L, arg) );
                )
            }
            break;
        case Opcode_VarArg:
            {
                int numVarArgs = numArgs - prototype->numParams;
                int num = GET_B(inst) - 1;
                if (num < 0)
                {
                    num = numVarArgs;
                    L->stackTop = stackBase + a + num;
                }
                Value* dst = &stackBase[a];
                Value* src = stackBase - numVarArgs;
                for (int i = 0; i < num; ++i)
                {
                    *dst = *src;
                    ++dst;
                    ++src;
                }
            }
            break;
        default:
            // Unimplemented opcode!
            assert(0);
        }
        
    }

    return 0;

}

int Vm_ProtectedCall(lua_State* L, ProtectedFunction function, void* userData)
{

    ErrorHandler* oldErrorHandler = L->errorHandler;
    
    ErrorHandler errorHandler;
    L->errorHandler = &errorHandler;

    // Save off the pre-call state so we can restore it in the case of an error.
    CallFrame* oldFrame = L->callStackTop;
    Value*     oldBase  = L->stackBase;

    int result = setjmp(errorHandler.jump);

    if (result != 0)
    {
        // An error occured.
        // Restore the pre-call state.
        L->stackBase    = oldBase;
        L->callStackTop = oldFrame;
    }
    else
    {
        function(L, userData);
    }

    // Restore any previously set error handler.
    L->errorHandler = oldErrorHandler;
    return result;

}

static void Call(lua_State* L, void* userData)
{
    CallArgs* args = static_cast<CallArgs*>(userData);
    Vm_Call(L, args->value, args->numArgs, args->numResults);
}

int Vm_ProtectedCall(lua_State* L, Value* value, int numArgs, int numResults, Value* errorFunc)
{

    CallArgs callArgs;

    callArgs.value      = value;
    callArgs.numArgs    = numArgs;
    callArgs.numResults = numResults;
    callArgs.errorFunc  = errorFunc;

    int result = Vm_ProtectedCall(L, Call, &callArgs);

    if (result != 0)
    {

        // An error occured.

        // For certain types of errors, there will be an error message on the
        // top of the stack.
        Value* errorMessage = L->stackTop - 1;

        L->stackTop = value;

        if (errorFunc != NULL)
        {
            PushValue(L, errorFunc);
            errorFunc = L->stackTop - 1;
        }

        Value message;
        switch (result)
        {
        case LUA_ERRMEM:
            SetValue( &message, String_Create(L, "not enough memory") );
            break;
        case LUA_ERRRUN:
        case LUA_ERRSYNTAX:
            message = *errorMessage;
            break;
        default:
            // LUA_ERRERR should only be generated by this function.
            assert( result != LUA_ERRERR );
            SetNil(&message);
        }
        PushValue(L, &message);

        if (errorFunc != NULL)
        {
            // Call the error function.
            if (Vm_ProtectedCall(L, errorFunc, 1, 1, NULL) != 0)
            {
                SetValue( L->stackTop - 1, String_Create(L, "error in error handling") );
                result = LUA_ERRERR;
            }
        }

    }

    return result;

}

// Calls the specified value. The value should be on the stack with its
// arguments following it. Returns the number of results from the funtion
// which are on the stack starting at the position where the function was.
void Vm_Call(lua_State* L, Value* value, int numArgs, int numResults)
{

    if (numArgs == -1)
    {
        numArgs = static_cast<int>(L->stackTop - value) - 1;
    }

    if (!Value_GetIsFunction(value))
    {
        // Try the "call" tag method.
        const Value* method = GetTagMethod(L, value, TagMethod_Call);
        if (method == NULL || !Value_GetIsFunction(method))
        {
            TypeError(L, (method ? method : value), "call");
        }
        // Move the method onto the stack before the function so we can
        // call it.
        for (Value* q = value + numArgs + 1; q > value; q--)
        {
            *q = *(q - 1);
        }
        *value = *method;
        ++L->stackTop;
        ++numArgs;
    }

    Closure* closure = value->closure;

    // Push into the call stack.
    if (L->callStackTop - L->callStackBase >= LUAI_MAXCCALLS)
    {
        Vm_Error(L, "call stack overflow");
    }
    CallFrame* frame = L->callStackTop;
    ++L->callStackTop;

    frame->function = value;

    int result = 0;
    
    // Adjust the stack to begin with the first function argument and include
    // all of the arguments.

    Value* oldBase = L->stackBase;
    Value* oldTop  = L->stackTop;

    if (closure->c)
    {

        L->stackBase = value + 1;
        L->stackTop  = L->stackBase + numArgs;

        // Store the stack information for debugging.
        frame->stackBase = L->stackBase;
        frame->stackTop  = L->stackTop;

        result = closure->cclosure.function(L);
        result = MoveResults(L, value, L->stackTop - result, result);

    }
    else
    {

        // The Lua stack is setup like this when the function accepts a variable
        // number of arguments:

        //  +------------+
        //  |  function  |
        //  +------------+
        //  |            |
        //  . fixed args .
        //  |            |
        //  +------------+
        //  |            |
        //  .  var args  .
        //  |            |
        //  +------------+ <--- base
        //  |            |
        //  . fixed args .
        //  |            |
        //  +------------+
        //  |            |
        //  .   locals   .
        //  |            |
        //  +------------+

        // The fixed arguments are duplicated when we have a vararg function
        // so that the register locations used by the arguments are deterministic
        // regardless of the number of arguments. The varargs are not accessed
        // directly by register, but through the VARARG instruction which will
        // copy them from before the stack location before the base pointer to
        // the target registers.

        Prototype* prototype = closure->lclosure.prototype;

        if (prototype->varArg)
        {
            // Duplicate the fixed arguments.
            Value* arg = value + 1;
            Value* dst = value + 1 + numArgs;
            L->stackBase = dst;
            for (int i = 0; i < prototype->numParams; ++i)
            {
                *dst = *arg;
                ++dst;
                ++arg;
            }
        }
        else
        {
            L->stackBase = value + 1;
            // If fewer arguments were supplied than the function expects, fill
            // in with nil values.
            if (numArgs < prototype->numParams)
            {
                SetRangeNil(L->stackBase + numArgs, L->stackBase + prototype->numParams);
            }
            numArgs = prototype->numParams;
        }

        L->stackTop = L->stackBase + prototype->maxStackSize;

        // Store the stack information for debugging.
        frame->stackBase = L->stackBase;
        frame->stackTop  = L->stackTop;

        SetRangeNil(L->stackBase + numArgs, L->stackTop);
        result = Execute(L, numArgs);

    }

    if (result >= 0)
    {
        if (numResults == -1)
        {
            numResults = result;
        }
        else
        {
            // If we want more results than were provided, fill in nil values.
            Value* firstResult = L->stackBase - 1;
            SetRangeNil(firstResult + result, firstResult + numResults);
            result = numResults;
        }
    }

    L->stackBase = oldBase;
    L->stackTop  = oldTop;

    if (result >= 0)
    {
        L->stackTop = value + result;
    }

    --L->callStackTop;

}

int Vm_GetCallStackSize(lua_State* L)
{
    return static_cast<int>(L->callStackTop - L->callStackBase);
}