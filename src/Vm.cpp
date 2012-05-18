/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

extern "C"
{
#include "lua.h"
#include "lualib.h"
}

#include "Opcode.h"
#include "Vm.h"
#include "State.h"
#include "Gc.h"
#include "String.h"
#include "Table.h"
#include "Function.h"
#include "UpValue.h"

#include <memory.h>

extern "C" int Vm_Execute(lua_State* L, LClosure* closure);

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

    const char* type = String_GetData( State_TypeName(L, Value_GetType(value)) );
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
    const char* type1 = String_GetData( State_TypeName(L, Value_GetType(arg1)) );
    const char* type2 = String_GetData( State_TypeName(L, Value_GetType(arg2)) );
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

    if (Value_GetIsNaN(key))
    {
        // NaN can't be used a table key, since we can't test if it's equal
        // to itself!
        Vm_Error(L, "table index is NaN");
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
        if (Value_GetIsClosure(method))
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
        if (Value_GetIsClosure(method))
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

FORCE_INLINE void Vm_UnaryMinus(lua_State* L, const Value* arg, Value* dst)
{
    lua_Number a;
    if (Vm_GetNumber(arg, &a))
    {
        SetValue( dst, -a );
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

FORCE_INLINE int Vm_Equal(lua_State* L, const Value* arg1, const Value* arg2)
{
    if ((Value_GetIsNumber(arg1) && Value_GetIsNumber(arg2)) || arg1->tag == arg2->tag)
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

FORCE_INLINE int Vm_Less(lua_State* L, const Value* arg1, const Value* arg2)
{
    if (Value_GetIsNumber(arg1) && Value_GetIsNumber(arg2))
    {
        return luai_numlt(arg1->number, arg2->number);
    }
    else if (arg1->tag == arg2->tag)
    {
        if (Value_GetIsString(arg1))
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

FORCE_INLINE int Vm_LessEqual(lua_State* L, const Value* arg1, const Value* arg2)
{
    if (Value_GetIsNumber(arg1) && Value_GetIsNumber(arg2))
    {
        return luai_numle(arg1->number, arg2->number);
    }
    else if (arg1->tag == arg2->tag)
    {
        if (Value_GetIsString(arg1))
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

        // TODO: Create a reusable buffer for all concatenation operations.
        char* buffer = static_cast<char*>( Allocate(L, length1 + length2) );
        memcpy(buffer, String_GetData(arg1->string), length1);
        memcpy(buffer + length1, String_GetData(arg2->string), length2);

        SetValue( dst, String_Create(L, buffer, length1 + length2) );
        Free(L, buffer, length1 + length2);

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
        return StringToNumber(string, result);
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

inline lua_Number Number_Add(lua_Number a, lua_Number b)
{
    return luai_numadd(a, b);
}

inline lua_Number Number_Sub(lua_Number a, lua_Number b)
{
    return luai_numsub(a, b);
}

inline lua_Number Number_Mul(lua_Number a, lua_Number b)
{
    return luai_nummul(a, b);
}

inline lua_Number Number_Div(lua_Number a, lua_Number b)
{
    return luai_numdiv(a, b);
}

inline lua_Number Number_Mod(lua_Number a, lua_Number b)
{
    return luai_nummod(a, b);
}

inline lua_Number Number_Pow(lua_Number a, lua_Number b)
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

/**
 * Setups up the stack and call frame for executing a function call. If the
 * function is a C function, the function to call is returned. Otherwise the
 * function to call is a Lua function which is read to be executed.
 */
static lua_CFunction PrepareCall(lua_State* L, Value* value, int& numArgs, int numResults)
{

    // Adjust the number of arguments if a variable number was supplied.
    if (numArgs == -1)
    {
        numArgs = static_cast<int>(L->stackTop - value) - 1;
    }

    // Prepares a value to be called as a function. If the value isn't a function,
    // it's call metamethod (if there is one) will replace the value on the stack.
  
    if (!Value_GetIsClosure(value))
    {
        // Try the "call" tag method.
        const Value* method = GetTagMethod(L, value, TagMethod_Call);
        if (method == NULL || !Value_GetIsClosure(method))
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

    frame->function   = value;
    frame->numResults = numResults;

    int result = 0;

    if (closure->c)
    {
    
        // Adjust the stack to begin with the first function argument and include
        // all of the arguments.

        L->stackBase = value + 1;
        L->stackTop  = L->stackBase + numArgs;

        // Store the stack information for debugging.
        frame->stackBase = L->stackBase;
        frame->stackTop  = L->stackTop;
        frame->ip        = NULL;

        return closure->cclosure.function;

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

        // Start location for initializing stack locations to nil.
        Value* initBase = NULL;

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
            initBase = dst;
        }
        else
        {
            L->stackBase = value + 1;
            if (numArgs < prototype->numParams)
            {
                // If we recieved fewer parameters than we expected, we'll need
                // to initialize the stack locations for the other parameters to
                // nil.
                initBase = L->stackBase + numArgs;
            }
            else
            {
                initBase = L->stackBase + prototype->numParams;
            }
            numArgs = prototype->numParams;
        }

        L->stackTop = L->stackBase + prototype->maxStackSize;

        // Store the stack information for debugging.
        frame->stackBase = L->stackBase;
        frame->stackTop  = L->stackTop;
        frame->ip        = prototype->code;

        SetRangeNil(initBase, L->stackTop);
        return NULL;

    }

}

static void ReturnFromCCall(lua_State* L, int result, int numResults)
{

    CallFrame* frame = L->callStackTop - 1;
    Value* firstValue = frame->function;

    result = MoveResults(L, firstValue, L->stackTop - result, result);

    if (result >= 0)
    {
        if (numResults != -1)
        {
            // If we want more results than were provided, fill in nil values.
            SetRangeNil(firstValue + result, firstValue + numResults);
            result = numResults;
        }
        L->stackTop = firstValue + result;
    }

    --L->callStackTop;
    L->stackBase = (L->callStackTop - 1)->stackBase;

}

static void ReturnFromLuaCall(lua_State* L, int result, int numResults)
{

    CallFrame* frame = L->callStackTop - 1;
    Value* firstValue = frame->function;

    if (result >= 0)
    {
        if (numResults != -1)
        {
            // If we want more results than were provided, fill in nil values.
            SetRangeNil(firstValue + result, firstValue + numResults);
            result = numResults;
        }
        L->stackTop = firstValue + result;
    }

    --L->callStackTop;
    L->stackBase = (L->callStackTop - 1)->stackBase;

}

/**
 * Executes the function on the top of the call stack.
 */
static int Execute(lua_State* L)
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

    int numEntries = 1; // Number of times we've "re-entered" this function.

Start:

    CallFrame* frame = State_GetCallFrame(L );
    Closure* closure = frame->function->closure;

    ASSERT( !closure->c );
    LClosure* lclosure = &closure->lclosure;

    Prototype* prototype = lclosure->prototype;

    register const Instruction* ip  = frame->ip;

    register Value* stackBase = L->stackBase;
    register Value* constant  = prototype->constant;

    while (1)
    {

    #ifdef DEBUG
        const char* _file = String_GetData(prototype->source);
        int         _line = prototype->sourceLine[ip - prototype->code];
    #endif

        Instruction inst = *ip;
        ++ip;

        Opcode opcode = GET_OPCODE(inst); 
        int a = GET_A(inst);

        switch (opcode)
        {
        case Opcode_Move:
            {
                int b = GET_B(inst);
                stackBase[a] = stackBase[b];
            }
            break;
        case Opcode_LoadK:
            {
                int bx = GET_Bx(inst);
                ASSERT(bx >= 0 && bx < prototype->numConstants);
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
                    ASSERT( key != &stackBase[a + 1] );
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
                    ASSERT(bx >= 0 && bx < prototype->numConstants);
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
                    ASSERT(bx >= 0 && bx < prototype->numConstants);
                    const Value* key = &constant[bx];
                    Value* dst = &stackBase[a];
                    Vm_GetGlobal(L, closure, key, dst);
                )
            }
            break;
        case Opcode_SetUpVal:
            {
                const Value* value = &stackBase[a];
                UpValue_SetValue(lclosure, GET_B(inst), value);                    
            }
            break;
        case Opcode_GetUpVal:
            {
                const Value* value = UpValue_GetValue(lclosure, GET_B(inst));
                stackBase[a] = *value;
            }
            break;
        case Opcode_GetTable:
            {
                PROTECT(
                    int b = GET_B(inst);
                    const Value* table = &stackBase[b];
                    const Value* key   = RESOLVE_RK( GET_C(inst) );
                    Vm_GetTable(L, table, key, &stackBase[a], false);
                )
            }
            break;
        case Opcode_GetTableRef:
            {
                PROTECT(
                    int b = GET_B(inst);
                    const Value* table = &stackBase[b];
                    const Value* key   = RESOLVE_RK( GET_C(inst) );
                    Vm_GetTable(L, table, key, &stackBase[a], true);
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

                frame->ip = ip;

                int numArgs     = GET_B(inst) - 1;
                int numResults  = GET_C(inst) - 1;
                Value* value    = &stackBase[a];
                
                lua_CFunction function = PrepareCall(L, value, numArgs, numResults);

                if (function != NULL)
                {
                    // Call the C function immediately.
                    int result = function(L);
                    ReturnFromCCall(L, result, numResults);

                    // Restore the top of the stack unless we're expecting a
                    // variable number of results (in which case the next
                    // instruction will restore it).
                    if (numResults >= 0)
                    {
                        L->stackTop = frame->stackTop;
                    }
                }
                else
                {
                    // "Re-enter" the function to start execution in the new Lua
                    // function.
                    ++numEntries;
                    goto Start;
                }

            }
            break;
        case Opcode_TailCall:
            {

                int numArgs     = GET_B(inst) - 1;
                Value* value    = &stackBase[a];
                
                lua_CFunction function = PrepareCall(L, value, numArgs, -1);

                if (function != NULL)
                {
                    // Call the C function immediately.
                    int result = function(L);
                    ReturnFromCCall(L, result, -1);
                }
                else
                {
                    // Since we're effectively returning from the current function
                    // with the tail call, we need to close the up values.
                    if (L->openUpValue != NULL)
                    {
                        CloseUpValues(L, stackBase);
                    }

                    CallFrame* newFrame = frame + 1;

                    // Reuse the stack from the previous call.
                    Value* dst = frame->function;
                    Value* src = newFrame->function;
                    while (src < newFrame->stackTop)
                    {
                        *dst = *src;
                        ++dst;
                        ++src;
                    }
                    frame->stackBase = frame->function + (newFrame->stackBase - newFrame->function);
                    frame->stackTop  = dst;

                    // Reuse the frame from the previous call. We preserve the
                    // number of results that the current call is expected to
                    // return since the return from the tail call will return
                    // from the current function as well.

                    // Note that we copied thenew function into the location of
                    // the old function, so we don't need to update the previous
                    // call frame.

                    frame->ip = newFrame->ip;
                    --L->callStackTop;

                    // "Re-enter" the function to start execution in the new Lua
                    // function.
                    L->stackBase = frame->stackBase;
                    L->stackTop  = frame->stackTop;
                    goto Start;
                }
             
            }
            break;
        case Opcode_Return:
            {
                if (L->openUpValue != NULL)
                {
                    CloseUpValues(L, stackBase);
                }       
                int numResults = GET_B(inst) - 1;
                numResults = MoveResults(L, frame->function, &stackBase[a], numResults);

                --numEntries;
                if (numEntries == 0)
                {
                    // We have exited out of all of the Lua functions that were
                    // called, so return.
                    return numResults;
                }
                else
                {
                    // Restart back at the execution point for the previous
                    // function.
                    ReturnFromLuaCall(L, numResults, frame->numResults);    

                    // Restore the top of the stack unless we're expecting a
                    // variable number of results (in which case the next
                    // instruction will restore it).
                    if (frame->numResults >= 0)
                    {
                        CallFrame* prevFrame = frame - 1;
                        L->stackTop = prevFrame->stackTop;
                    }
                    goto Start;
                }
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
                        c->lclosure.upValue[i] = UpValue_Create(L, &stackBase[b]);
                        Gc_WriteBarrier(L, c, c->lclosure.upValue[i]);
                    }
                    else
                    {
                        ASSERT( GET_OPCODE(inst) == Opcode_GetUpVal );
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

                ASSERT( Value_GetIsNumber(&stackBase[a + 2]) );
                ASSERT( Value_GetIsNumber(&stackBase[a + 1]) );
                
                lua_Number step  = stackBase[a + 2].number;
                lua_Number limit = stackBase[a + 1].number;
                
                iterator->number += step;

                // We need to alter the end test based on whether or not the step
                // is positive or negative.
                if (luai_numlt(0, step) ? luai_numle(iterator->number, limit) : luai_numle(limit, iterator->number))
                {
                    int sbx = GET_sBx(inst);
                    ip += sbx;
                    Value_Copy( &stackBase[a + 3], iterator );
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
                    ASSERT( Value_GetIsTable(dst) );
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
                        // Restore the top of the stack from the previous call.
                        L->stackTop = frame->stackTop;
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
                int numArgs    = static_cast<int>(frame->stackBase - frame->function) - 1;
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
            ASSERT(0);
        }
        
    }

    return 0;

}

int Vm_RunProtected(lua_State* L, ProtectedFunction function, Value* stackTop, void* userData, Value* errorFunc)
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

        if (result == LUA_ERRRUN)
        {
            // Call the error handler function with the error message.
            if (errorFunc != NULL)
            {
                PushValue(L, errorFunc);
                PushValue(L, L->stackTop - 2);
                if (Vm_ProtectedCall(L, L->stackTop - 2, 1, 1, NULL) != 0)
                {
                    SetValue( L->stackTop - 1, String_Create(L, "error in error handling") );
                    result = LUA_ERRERR;
                }
            }
        }
        else if (result == LUA_ERRMEM)
        {
            PushString( L, String_Create(L, "not enough memory") );
        }
        else
        {
            // The other error codes should never get to this point.
            ASSERT(0);
        }
    
        if (L->openUpValue != NULL)
        {
            CloseUpValues(L, oldBase);
        }

        // Move the error message to the top of the pre-call stack.
        Value_Copy(stackTop, L->stackTop - 1);
        L->stackTop = stackTop + 1;
        
        // Restore the pre-call state with the error message.
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

    return Vm_RunProtected(L, Call, value, &callArgs, errorFunc);

}

void Vm_Call(lua_State* L, Value* value, int numArgs, int numResults)
{
  
    lua_CFunction function = PrepareCall(L, value, numArgs, numResults);

    if (function != NULL)
    {
        int result = function(L);
        ReturnFromCCall(L, result, numResults);
    }
    else
    {
        int result = Execute(L);
        ReturnFromLuaCall(L, result, numResults);    
    }

}

int Vm_GetCallStackSize(lua_State* L)
{
    // Remove 1 element since the bottom of the call stack isn't a valid entry.
    return static_cast<int>(L->callStackTop - L->callStackBase) - 1;
}