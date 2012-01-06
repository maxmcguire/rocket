/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */

#include "Parser.h"
#include "Opcode.h"
#include "Lexer.h"
#include "String.h"
#include "State.h"
#include "Table.h"
#include "Function.h"
#include "Code.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>

void Parser_Initialize(Parser* parser, lua_State* L, Lexer* lexer, Function* parent)
{
    
    parser->L                   = L;
    parser->lexer               = lexer;
    parser->numBlocks           = 0;

    Function* function = static_cast<Function*>( Allocate( L, sizeof(Function) ) );

    function->parent            = parent;
    function->parser            = parser;

    function->numRegisters      = 0;
    function->maxStackSize      = 0;
    function->numParams         = 0;
    function->varArg            = false;

    function->numConstants      = 0;
    function->constants         = Table_Create(parser->L);

    function->code              = NULL;
    function->codeSize          = 0;
    function->maxCodeSize       = 0;

    function->sourceLine        = NULL;
    function->maxSourceLines    = 0;
  
    function->numLocals         = 0;
    function->numCommitedLocals = 0;
    function->numUpValues       = 0;

    function->function          = NULL;
    function->numFunctions      = 0;
    function->maxFunctions      = 0;
        
    parser->function            = function;

}

void Parser_Error(Parser* parser, const char* fmt, ...)
{

    PushFString(parser->L, "Error line %d: ", parser->lexer->lineNumber);

    va_list argp;
    va_start(argp, fmt);
    PushVFString(parser->L, fmt, argp);
    va_end(argp);

    Concat(parser->L, 2);
    
    State_Error(parser->L);

}

bool Parser_Accept(Parser* parser, int token)
{
    Lexer_NextToken(parser->lexer);
    if (Lexer_GetTokenType(parser->lexer) == token)
    {
        parser->lexer->haveToken = false;
        return true;
    }
    return false;
}
 
bool Parser_Expect(Parser* parser, int token)
{
    if (Parser_Accept(parser, token))
    {
        return true;
    }
    Parser_Error(parser, "unexpected token");
    return false;
}

bool Parser_Expect(Parser* parser, int token1, int token2)
{
    if (Parser_Accept(parser, token1) ||
        Parser_Accept(parser, token2))
    {
        return true;
    }
    Parser_Error(parser, "unexpected token");
    return false;
}

void Parser_Unaccept(Parser* parser)
{
    parser->lexer->haveToken = true;    
}

/**
 * Searches an array of strings in reverse order for a name and returns its
 * index (or -1  if it's not in the array).
 */
static int Parser_FindName(String* names[], int numNames, String* name)
{
    for (int i = numNames - 1; i >= 0; --i)
    {
        if (names[i] == name)
        {
            return i;
        }
    }
    return -1;
}

static int Parser_GetLocalIndex(Function* function, String* name)
{
    return Parser_FindName(function->local, function->numCommitedLocals, name);
}

static int Parser_GetUpValueIndex(Function* function, String* name)
{
    return Parser_FindName(function->upValue, function->numUpValues, name);
}

int Parser_GetLocalIndex(Parser* parser, String* name)
{
    return Parser_GetLocalIndex(parser->function, name);
}

static void Parser_MarkUpValue(Function* function, int local)
{

    Parser* parser = function->parser;
    assert(parser != NULL);

    // Find the block that contains the local.
    for (int i = parser->numBlocks - 1; i >= 0; --i)
    {
        Block* block = &parser->block[i];
        if (local >= block->firstLocal)
        {
            if (block->firstLocalUpValue == -1 || local < block->firstLocalUpValue)
            {
                block->firstLocalUpValue = local;
            }
            return;
        }
    }

    // Note, it's possible for no block to contain the local if it's declared
    // at the top level. That's ok since functions automatically close up values
    // when they return.

}

int Parser_AddUpValue(Parser* parser, String* name)
{

    Function* function = parser->function;

    // Check to see if it's already an up value in our function.
    int index = Parser_GetUpValueIndex(function, name);
    if (index != -1)
    {
        return index;
    }

    // If not, and it's either a local or an up value in the parent, it should
    // become an up value of this function.
    Function* parent = function->parent;
    while (parent != NULL)
    {

        int index = Parser_GetLocalIndex(parent, name);
        if (index != -1)
        {
            // Mark the local as needing to be "closed" when the block its
            // declared in ends, since it will be used as an upvalue.
            Parser_MarkUpValue(parent, index);
        }

        if (index != -1 || Parser_GetUpValueIndex(parent, name) != -1)
        {
            if (function->numUpValues == LUAI_MAXUPVALUES)
            {
                Parser_Error(parser, "too many up values to function");
            }

            // TODO: Mark the block as needing the locals to be closed.

            int n =  function->numUpValues;
            function->upValue[n] = name;

            ++function->numUpValues;
            return n;
        }
        parent = parent->parent;
    }

    return -1;

}

int Parser_AddLocal(Parser* parser, String* name)
{

    // Lua 5.1 doesn't report this as an error, so neither will we.
    /*
    if (Parser_GetLocalIndex(parser, name) != -1)
    {
        Parser_Error(parser, "local already defined");
    }
    */

    Function* function = parser->function;

    function->local[function->numLocals] = name;
    ++function->numLocals;

    return function->numLocals - 1;

}

void Parser_CommitLocals(Parser* parser)
{

    Function* function = parser->function;
    function->numCommitedLocals = function->numLocals;

    if (function->numCommitedLocals > function->maxStackSize)
    {
        function->maxStackSize = function->numCommitedLocals;
    }
    if (function->numRegisters < function->numCommitedLocals)
    {
        function->numRegisters = function->numCommitedLocals;
    }
    
}

int Parser_AddConstant(Parser* parser, Value* value)
{

    Function* function = parser->function;
    const Value* result = Table_GetTable(parser->L, function->constants, value);
    if (result != NULL)
    {
        return Value_GetInteger(result);
    }

    Value index;
    SetValue(&index, function->numConstants);

    Table_SetTable(parser->L, function->constants, value, &index);
    ++function->numConstants;

    return Value_GetInteger(&index);

}

int Parser_EncodeRK(int index, int type)
{
    assert(index < 256);
    if (type == EXPRESSION_REGISTER)
    {
        return index;
    }
    else if (type == EXPRESSION_CONSTANT)
    {
        return index | 256;
    }
    return 0;
}

int Parser_EncodeRK(Parser* parser, Expression* location)
{
    assert(location->type == EXPRESSION_REGISTER ||
           location->type == EXPRESSION_CONSTANT);
    return Parser_EncodeRK(location->index, location->type);
}

int Parser_EmitInstruction(Parser* parser, Instruction inst)
{

    Function* function = parser->function;
    GrowArray(parser->L, function->code, function->codeSize, function->maxCodeSize);
    GrowArray(parser->L, function->sourceLine, function->codeSize, function->maxSourceLines);

    int n = function->codeSize;
    ++function->codeSize;

    function->code[n] = inst;
    function->sourceLine[n] = parser->lexer->lineNumber;

    return n;

}

void Parser_UpdateInstruction(Parser* parser, int pos, Instruction inst)
{
    Function* function = parser->function;
    function->code[pos] = inst;
}

Instruction Parser_GetInstruction(Parser* parser, int pos)
{
    Function* function = parser->function;
    return function->code[pos];
}

int Parser_GetInstructionCount(Parser* parser)
{
    Function* function = parser->function;
    return function->codeSize;
}

Instruction Parser_EncodeAsBx(Opcode opcode, int a, int sbx)
{
    return opcode | (a << 6) | ((sbx + 131071) << 14);
}

Instruction Parser_EncodeABC(Opcode opcode, int a, int b, int c)
{
    return opcode | (a << 6) | (b << 23) | (c << 14);
}

int Parser_EmitABC(Parser* parser, Opcode opcode, int a, int b, int c)
{
    return Parser_EmitInstruction(parser, Parser_EncodeABC(opcode, a, b, c));
}

void Parser_EmitAB(Parser* parser, Opcode opcode, int a, int b)
{
    Instruction inst = opcode | (a << 6) | (b << 23);
    Parser_EmitInstruction(parser, inst);
}

void Parser_EmitABx(Parser* parser, Opcode opcode, int a, int bx)
{
    Instruction inst = opcode | (a << 6) | (bx << 14);
    Parser_EmitInstruction(parser, inst);
}

void Parser_EmitAsBx(Parser* parser, Opcode opcode, int a, int sbx)
{
    Parser_EmitInstruction(parser, Parser_EncodeAsBx(opcode, a, sbx));
}

void Parser_BeginSkip(Parser* parser, int* id)
{
    *id = Parser_EmitInstruction(parser, 0);  // Placeholder for jump.
}

void Parser_EndSkip(Parser* parser, int* id)
{
    int jumpAmount = static_cast<int>(Parser_GetInstructionCount(parser) - *id - 1);
    Parser_UpdateInstruction(parser, *id, Parser_EncodeAsBx(Opcode_Jmp, 0, jumpAmount));
}

void Parser_BeginLoop(Parser* parser, int* id)
{
    *id = Parser_GetInstructionCount(parser);
}

void Parser_EndLoop(Parser* parser, int* id)
{
    int jumpAmount = static_cast<int>(*id - Parser_GetInstructionCount(parser) - 1);
    Parser_EmitAsBx(parser, Opcode_Jmp, 0, jumpAmount);
}

static int Parser_AddConstant(Parser* parser, lua_Number number)
{
    Value value;
    SetValue(&value, number);
    return Parser_AddConstant(parser, &value);
}

int Parser_AddConstant(Parser* parser, String* string)
{
    Value value;
    SetValue(&value, string);
    return Parser_AddConstant(parser, &value);
}

int Parser_AllocateRegister(Parser* parser)
{
    Function* function = parser->function;
    ++function->numRegisters;
    if (function->numRegisters > function->maxStackSize)
    {
        function->maxStackSize = function->numRegisters;
    }
    return function->numRegisters - 1;
}

int Parser_GetNumRegisters(Parser* parser)
{
    Function* function = parser->function;
    return function->numRegisters;
}

bool Parser_ConvertToBoolean(Parser* parser, Expression* value)
{
    if (value->type == EXPRESSION_NIL)
    {
        value->type  = EXPRESSION_BOOLEAN;
        value->index = 0;
        return true;
    }
    else if (value->type == EXPRESSION_NUMBER)
    {
        value->type  = EXPRESSION_BOOLEAN;
        value->index = 1;
        return true;
    }
    else if (value->type == EXPRESSION_CONSTANT)
    {
        // Note, the constant type should not be boolean or nil or it would
        // be stored in the value, so it must be true.
        value->type  = EXPRESSION_BOOLEAN;
        value->index = 1;
        return true;
    }
    return false;
}

static void Parser_ConvertLiteralToConstant(Parser* parser, Expression* value)
{
    if (value->type == EXPRESSION_NIL)
    {
        Value constant;
        SetNil(&constant);
        value->type = EXPRESSION_CONSTANT;
        value->index = Parser_AddConstant(parser, &constant);
    }
    else if (value->type == EXPRESSION_BOOLEAN)
    {
        Value constant;
        SetValue(&constant, value->index != 0);
        value->type = EXPRESSION_CONSTANT;
        value->index = Parser_AddConstant(parser, &constant);
    }
    else if (value->type == EXPRESSION_NUMBER)
    {
        Value constant;
        SetValue(&constant, value->number);
        value->type = EXPRESSION_CONSTANT;
        value->index = Parser_AddConstant(parser, &constant);
    }
}

bool Parser_ResolveCall(Parser* parser, Expression* value, int numResults)
{
    if (value->type == EXPRESSION_CALL)
    {
        Parser_EmitABC(parser, Opcode_Call, value->index, value->numArgs + 1, numResults + 1);
        value->type = EXPRESSION_REGISTER;
        Parser_SetLastRegister(parser, value->index + numResults - 1);
        return true;
    }
    return false;
}

bool Parser_ResolveVarArg(Parser* parser, Expression* value, int numResults, int regHint)
{
    if (value->type == EXPRESSION_VARARG)
    {
        if (regHint == -1)
        {
            regHint = Parser_AllocateRegister(parser);
        }
        Parser_EmitAB(parser, Opcode_VarArg, regHint, numResults + 1);
        value->type  = EXPRESSION_REGISTER;
        value->index = regHint;
        Parser_SetLastRegister(parser, value->index + numResults - 1);
        return true;
    }
    return false;
}

void Parser_ConvertToTest(Parser* parser, Expression* value)
{
    if (value->type != EXPRESSION_TEST)
    {

        int test = 0;
        if (value->type == EXPRESSION_NOT)
        {
            // "Fold" the not into the test by negating it.
            test = 1;
            value->type = EXPRESSION_REGISTER;
        }

        Parser_MoveToRegister(parser, value, -1);
        value->index = Parser_EmitABC(parser, Opcode_Test, value->index, 0, test);
        value->type  = EXPRESSION_TEST;
        Parser_EmitInstruction(parser, 0);  // Placeholder for jump.

    }
}

void Parser_CloseTest(Parser* parser, Expression* value)
{
    Parser_CloseTest(parser, value, Parser_GetInstructionCount(parser));
}

void Parser_CloseTest(Parser* parser, Expression* value, int startPos)
{
    int testPos = value->index;
    int jumpAmount = static_cast<int>(startPos - testPos - 2);
    Parser_UpdateInstruction(parser, testPos + 1, Parser_EncodeAsBx(Opcode_Jmp, 0, jumpAmount));
}

static void Parser_EmitUpValueBinding(Parser* parser, Function* closure)
{
    Function* function = parser->function;
    for (int i = 0; i < closure->numUpValues; ++i)
    {
        String* name = closure->upValue[i];
        int index = Parser_GetLocalIndex(function, name);
        if (index != -1)
        {
            // The up value in the closure is a local variable in our function.
            Parser_EmitAB(parser, Opcode_Move, 0, index);
        }
        else
        {
            // The up value in the closure is an up value in our function.
            index = Parser_GetUpValueIndex(function, name);
            assert(index != -1);
            Parser_EmitAB(parser, Opcode_GetUpVal, 0, index);
        }
    }
}

int Parser_GetRegisterHint(Parser* parser, const Expression* value)
{
    if (value->type == EXPRESSION_LOCAL ||
        value->type == EXPRESSION_REGISTER)
    {
        return value->index;
    }
    return -1;
}

bool Parser_ConvertToRegister(Parser* parser, Expression* value)
{
    if (value->type == EXPRESSION_LOCAL)
    {
        value->type = EXPRESSION_REGISTER;
    }
    return value->type == EXPRESSION_REGISTER;
}

void Parser_MoveToRegister(Parser* parser, Expression* value, int reg)
{

    Parser_ResolveCall(parser, value, 1);
    Parser_ConvertToRegister(parser, value);

    if (value->type == EXPRESSION_REGISTER)
    {
        // The value is already in a register, so nothing to do.
        if (reg == -1 || value->index == reg)
        {
            return;
        }
    }

    // Select the desination register.
    if (reg == -1)
    {
        if (value->type == EXPRESSION_TABLE ||
            value->type == EXPRESSION_NOT)
        {
            // Reuse the register the table is stored in.
            reg = value->index;
        }
        else
        {
            reg = Parser_AllocateRegister(parser);
        }
    }
    
    // There are no special instructions to load numbers like there are nil and
    // bool, so first convert it to a constant slot and then load it as a regular
    // constant.
    if (value->type == EXPRESSION_NUMBER)
    {
        Parser_ConvertLiteralToConstant(parser, value);   
    }

    if (value->type == EXPRESSION_CONSTANT)
    {
        Parser_EmitABx(parser, Opcode_LoadK, reg, value->index);
    }
    else if (value->type == EXPRESSION_NIL)
    {
        Parser_EmitAB(parser, Opcode_LoadNil, reg, reg);
    }
    else if (value->type == EXPRESSION_BOOLEAN)
    {
        Parser_EmitABC(parser, Opcode_LoadBool, reg, value->index, 0);
    }
    else if (value->type == EXPRESSION_GLOBAL)
    {
        Parser_EmitABx(parser, Opcode_GetGlobal, reg, value->index);
    }
    else if (value->type == EXPRESSION_TABLE)
    {
        Parser_EmitABC(parser, Opcode_GetTable, reg, value->index,
            Parser_EncodeRK(value->key, value->keyType));
    }
    else if (value->type == EXPRESSION_FUNCTION)
    {
        
        Function* function = parser->function;
        Function* closure  = function->function[value->index]; 

        Parser_EmitABx(parser, Opcode_Closure, reg, value->index);
        Parser_EmitUpValueBinding(parser, closure);

    }
    else if (value->type == EXPRESSION_REGISTER)
    {
        Parser_EmitAB(parser, Opcode_Move, reg, value->index);
    }
    else if (value->type == EXPRESSION_TEST)
    {
        Parser_EmitABC(parser, Opcode_LoadBool, reg, 1, 1);
        Parser_CloseTest(parser, value);
        Parser_EmitABC(parser, Opcode_LoadBool, reg, 0, 0);
    }
    else if (value->type == EXPRESSION_UPVALUE)
    {
        Parser_EmitAB(parser, Opcode_GetUpVal, reg, value->index);
    }
    else if (value->type == EXPRESSION_NOT)
    {
        Parser_EmitAB(parser, Opcode_Not, reg, value->index);
    }
    else if (value->type == EXPRESSION_VARARG)
    {
        Parser_EmitAB(parser, Opcode_VarArg, reg, 2);
    }
    else
    {
        // Expression type not handled.
        assert(0);
    }
    value->type     = EXPRESSION_REGISTER;
    value->index    = reg;
}


void Parser_MoveToRegisterOrConstant(Parser* parser, Expression* value, int reg)
{
    // If the value is one of our literal types, move it to a constant slot.
    Parser_ConvertLiteralToConstant(parser, value);
    // Anything else we move to a register.
    if (value->type != EXPRESSION_CONSTANT)
    {
        Parser_MoveToRegister(parser, value, reg);
    }
}

void Parser_MoveToStackTop(Parser* parser, Expression* value)
{
    Function* function = parser->function;
    Parser_MoveToRegister(parser, value);
    if (value->index != function->numRegisters - 1 || value->index < function->numCommitedLocals)
    {
        int reg = Parser_AllocateRegister(parser);
        Parser_MoveToRegister(parser, value, reg);
    }
}

bool Parser_GetIsTemporaryRegister(Parser* parser, const Expression* value)
{
    return value->type == EXPRESSION_REGISTER &&
           value->index >= parser->function->numCommitedLocals;
}

void Parser_SelectDstRegister(Parser* parser, Expression* dst, int regHint)
{
    if (regHint == -1)
    {
        regHint = Parser_AllocateRegister(parser);
    }
    dst->type  = EXPRESSION_REGISTER;
    dst->index = regHint;
}

void Parser_SetLastRegister(Parser* parser, int reg)
{
    Function* function = parser->function;
    function->numRegisters = reg + 1;
    if (function->numRegisters > function->maxStackSize)
    {
        function->maxStackSize = function->numRegisters;
    }
}

void Parser_FreeRegisters(Parser* parser)
{
    Function* function = parser->function;
    function->numRegisters = function->numCommitedLocals;
}

void Parser_FreeRegisters(Parser* parser, int num)
{
    Function* function = parser->function;
    function->numRegisters -= num;
}

int Parser_AddFunction(Parser* parser, Function* f)
{

    Function* function = parser->function;
    GrowArray(parser->L, function->function, function->numFunctions, function->maxFunctions);

    int index = function->numFunctions;

    function->function[index] = f;
    ++function->numFunctions;

    return index;

}

static void Function_Destroy(lua_State* L, Function* function)
{
    Free(L, function->code, function->maxCodeSize * sizeof(Instruction));
    Free(L, function, sizeof(Function));
}

void Parser_Destroy(Parser* parser)
{
    Function_Destroy(parser->L, parser->function);
}

static void PrintConstants(Prototype* prototype)
{

    for (int i = 0; i < prototype->numConstants; ++i)
    {
        const Value* value = &prototype->constant[i];
        if (Value_GetIsNumber(value))
        {
            printf(".const %f ; %d\n", value->number, i);
        }
        else if (Value_GetIsString(value))
        {
            printf(".const '%s' ; %d\n", String_GetData(value->string), i );
        }
        else if (Value_GetIsBoolean(value))
        {
            printf(".const %s ; %d\n", value->boolean ? "true" : "false", i );
        }
        else if (Value_GetIsNil(value))
        {
            printf(".const nil; %d\n", i);
        }
        else
        {
            // Some other type of constant was stored that can't be saved into
            // the file. If this happens, it means we've introduced some new type
            // of constant but haven't handled it here.
            assert(0);
        }
    }
}

static void PrintOpcode(int line, Opcode opcode)
{

    const char* op = Opcode_GetAsText(opcode);

    char buffer[256];
    sprintf(buffer, "[%02d] %s", line, op);

    size_t indent = 20;
    size_t length = strlen(buffer);
    printf("%s", buffer);
    if (length < indent)
    {
        indent -= length;
        while (indent > 0)
        {
            printf(" ");
            --indent;
        }
    }

}

void PrintFunction(Prototype* prototype)
{

    enum Format
    {
        Format_None,
        Format_A,
        Format_AB,
        Format_ABC,
        Format_ABx,
        Format_AsBx,
    };

    static const Format format[] = 
        {
            Format_AB,      // Opcode_Move
            Format_ABx,     // Opcode_LoadK
            Format_ABC,     // Opcode_LoadBool
            Format_ABx,     // Opcode_LoadNil
            Format_AB,      // Opcode_GetUpVal
            Format_ABx,     // Opcode_GetGlobal
            Format_ABC,     // Opcode_GetTable
            Format_ABx,     // Opcode_SetGlobal
            Format_AB,      // Opcode_SetUpVal
            Format_ABC,     // Opcode_SetTable
            Format_ABC,     // Opcode_NewTable
            Format_ABC,     // Opcode_Self
            Format_ABC,     // Opcode_Add 
            Format_ABC,     // Opcode_Sub
            Format_ABC,     // Opcode_Mul
            Format_ABC,     // Opcode_Div
            Format_ABC,     // Opcode_Mod
            Format_ABC,     // Opcode_Pow
            Format_AB,      // Opcode_Unm
            Format_AB,      // Opcode_Not
            Format_AB,      // Opcode_Len
            Format_ABC,     // Opcode_Concat
            Format_AsBx,    // Opcode_Jmp
            Format_ABC,     // Opcode_Eq
            Format_ABC,     // Opcode_Lt
            Format_ABC,     // Opcode_Le
            Format_ABC,     // Opcode_Test
            Format_ABC,     // Opcode_TestSet
            Format_ABC,     // Opcode_Call
            Format_ABC,     // Opcode_TailCall
            Format_AB,      // Opcode_Return
            Format_AsBx,    // Opcode_ForLoop
            Format_AsBx,    // Opcode_ForPrep
            Format_ABC,     // Opcode_TForLoop
            Format_ABC,     // Opcode_SetList
            Format_A,       // Opcode_Close
            Format_ABx,     // Opcode_Closure
            Format_AB,      // Opcode_VarArg
        };

    printf("; function\n");
    printf("; %d upvalues, %d params, %d stacks\n",
        prototype->numUpValues, prototype->numParams, prototype->maxStackSize);

    PrintConstants(prototype);

    for (int i = 0; i < prototype->codeSize; ++i)
    {
        
        Instruction inst = prototype->code[i];
        
        Opcode opcode = GET_OPCODE(inst);
        PrintOpcode(i + 1, opcode);

        switch (format[opcode])
        {
        case Format_A:
            printf("%d", GET_A(inst));
            break;
        case Format_AB:
            printf("%d %d", GET_A(inst), GET_B(inst));
            break;
        case Format_ABC:
            printf("%d %d %d", GET_A(inst), GET_B(inst), GET_C(inst));
            break;
        case Format_ABx:
            printf("%d %d", GET_A(inst), GET_Bx(inst));
            break;
        case Format_AsBx:
            printf("%d %d", GET_A(inst), GET_sBx(inst));
            break;
        }

        printf("\n");

    }

    printf("; end of function\n\n");

}

Prototype* Function_CreatePrototype(lua_State* L, Function* function, String* source)
{

    Prototype* prototype = Prototype_Create(
        L,
        function->codeSize,
        function->numConstants,
        function->numFunctions);

    // Store the code.
    memcpy(prototype->code, function->code, function->codeSize * sizeof(Instruction));

    // Store the source information.
    prototype->source = source;
    memcpy(prototype->sourceLine, function->sourceLine, function->codeSize * sizeof(int));

    // Store the functions.
    for (int i = 0; i < function->numFunctions; ++i)
    {
        prototype->prototype[i] = Function_CreatePrototype(L, function->function[i], source);
    }

    // Store the constants.

    Value key;
    SetNil(&key);

    Table* constants = function->constants;
    const Value* value;

    // Since we can't store nil in a table, any slot which is unset in the table
    // should be a constant.
    SetRangeNil(prototype->constant, prototype->constant + prototype->numConstants);

    while (value = Table_Next(constants, &key))
    {
        assert(Value_GetIsNumber(value));
        int i = static_cast<int>(value->number);
        prototype->constant[i] = key;
    }
    
    prototype->varArg       = function->varArg;
    prototype->numParams    = function->numParams;
    prototype->maxStackSize = function->maxStackSize;
    prototype->numUpValues  = function->numUpValues;

//    PrintFunction(prototype);

    return prototype;

}

void Parser_BeginBlock(Parser* parser, bool breakable)
{
    if (parser->numBlocks == LUAI_MAXCCALLS)
    {
        Parser_Error(parser, "too many block levels");
    }

    // Locals must be commited before starting a block.
    assert( parser->function->numLocals == parser->function->numCommitedLocals );

    Block* block = &parser->block[parser->numBlocks];
    block->firstLocal           = parser->function->numLocals;
    block->breakable            = breakable;
    block->firstBreakPos        = -1;
    block->firstLocalUpValue    = -1;
    ++parser->numBlocks;
}

void Parser_EndBlock(Parser* parser)
{

    assert(parser->numBlocks > 0);
    Block* block = &parser->block[parser->numBlocks - 1]; 
    
    // Update the break instructions.
    
    int breakPos = block->firstBreakPos;
    int currentPos = Parser_GetInstructionCount(parser);
    
    while (breakPos != -1)
    {
        int nextBreakPos = Parser_GetInstruction(parser, breakPos);
        int jumpAmount = currentPos - breakPos;
        Instruction inst = Parser_EncodeAsBx(Opcode_Jmp, 0, jumpAmount);
        Parser_UpdateInstruction(parser, breakPos, inst);
        breakPos = nextBreakPos;
    }

    if (block->firstLocalUpValue != -1)
    {
        // Close an local up values.
        Parser_EmitAB(parser, Opcode_Close, block->firstLocalUpValue, 0);
    }
    
    --parser->numBlocks;
    parser->function->numLocals = parser->block[parser->numBlocks].firstLocal;
    parser->function->numCommitedLocals = parser->function->numLocals;


}

int Parser_GetToken(Parser* parser)
{
    return parser->lexer->token.type;
}

String* Parser_GetString(Parser* parser)
{
    assert( parser->lexer->token.type == TokenType_Name ||
            parser->lexer->token.type == TokenType_String );
    return parser->lexer->token.string;
}

lua_Number Parser_GetNumber(Parser* parser)
{
    assert( parser->lexer->token.type == TokenType_Number );
    return parser->lexer->token.number;
}

void Parser_BreakBlock(Parser* parser)
{

    int blockIndex = parser->numBlocks - 1;
    while (blockIndex >= 0)
    {
        if (parser->block[blockIndex].breakable)
        {
            break;
        }
        --blockIndex;
    }

    if (blockIndex < 0)
    {
        Parser_Error(parser, "no loop to break");
    }

    Block* block = &parser->block[blockIndex];

    // Reserve a location in the instruction stream for the jump since we
    // don't know how far we'll need to jump yet. In this location we store
    // the position of the next break out of the block as a form of inline
    // linked list.
    int pos = Parser_EmitInstruction(parser, block->firstBreakPos);
    block->firstBreakPos = pos;

}