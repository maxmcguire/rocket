/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#include "Parser.h"
#include "Lexer.h"
#include "String.h"
#include "State.h"
#include "Table.h"
#include "Function.h"
#include "Code.h"
#include "Print.h"

#include <memory.h>

Function* Function_Create(lua_State* L)
{

    Function* function = static_cast<Function*>( Gc_AllocateObject( L, LUA_TFUNCTIONP, sizeof(Function) ) );

    function->parent            = NULL;
    function->parser            = NULL;

    function->numRegisters      = 0;
    function->maxStackSize      = 0;
    function->numParams         = 0;
    function->varArg            = false;

    function->numConstants      = 0;
    function->constants         = NULL;

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

    // It's possible for creating the table to trigger garbage collection, so
    // make sure there's a reference to our function on the stack before we do
    // that.

    PushFunction(L, function);
    function->constants = Table_Create(L);
    Gc_WriteBarrier(L, function, function->constants);
    Pop(L, 1);

    return function;

}

void Function_Destroy(lua_State* L, Function* function)
{
    FreeArray(L, function->function, function->maxFunctions);
    FreeArray(L, function->code, function->maxCodeSize);
    FreeArray(L, function->sourceLine, function->maxSourceLines);
    Free(L, function, sizeof(Function));
}

void Parser_Initialize(Parser* parser, lua_State* L, Lexer* lexer)
{
    parser->L           = L;
    parser->lexer       = lexer;
    parser->numBlocks   = 0;
    parser->lineNumber  = lexer->lineNumber;
    parser->function    = NULL;
}

void Parser_Destroy(Parser* parser)
{
}

void Parser_Error(Parser* parser, const char* fmt, ...)
{

    PushFString(parser->L, "Error line %d: ", parser->lineNumber);

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
        parser->lineNumber = parser->lexer->lineNumber;
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
    ASSERT(parser != NULL);

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

static int Parser_AddUpValue(Parser* parser, Function* function, String* name)
{

    // Check to see if it's already an up value in our function.
    int index = Parser_GetUpValueIndex(function, name);
    if (index != -1)
    {
        return index;
    }

    // If not, and it's either a local or an up value in the parent, it should
    // become an up value of this function.
    Function* parent = function->parent;

    if (parent != NULL)
    {

        int index = Parser_GetLocalIndex(parent, name);
        if (index != -1)
        {
            // Mark the local as needing to be "closed" when the block its
            // declared in ends, since it will be used as an upvalue.
            Parser_MarkUpValue(parent, index);
        }

        if (index != -1 || Parser_AddUpValue(parser, parent, name) != -1)
        {
            if (function->numUpValues == LUAI_MAXUPVALUES)
            {
                Parser_Error(parser, "too many up values to function");
            }

            int n =  function->numUpValues;
            function->upValue[n] = name;

            ++function->numUpValues;
            return n;
        }
        
    }

    return -1;

}

int Parser_AddUpValue(Parser* parser, String* name)
{
    Function* function = parser->function;
    return Parser_AddUpValue(parser, function, name);
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

    if (function->numLocals == LUAI_MAXVARS)
    {
        Parser_Error(parser, "too many local variables (limit is %d)", LUAI_MAXVARS);
    }

    function->local[function->numLocals] = name;
    Gc_WriteBarrier(parser->L, function, name);
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
    ASSERT(function->numConstants < 262144);

    // We can't store a nil value in a table, so if it's a nil value we need
    // to use some other value to indicate that. We use the constant table
    // itself since this will never appear otherwise.
    Value _value;
    if (Value_GetIsNil(value))
    {
        SetValue(&_value, function->constants);
        value = &_value;
    }

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

int Parser_EncodeRK(Parser* parser, Expression* location)
{
    ASSERT(location->type == EXPRESSION_REGISTER ||
           location->type == EXPRESSION_CONSTANT);
    ASSERT(location->index < 256);

    if (location->type == EXPRESSION_REGISTER)
    {
        return location->index;
    }
    else if (location->type == EXPRESSION_CONSTANT)
    {
        return location->index | 256;
    }

    return 0;
}

int Parser_EmitInstruction(Parser* parser, Instruction inst)
{

    Function* function = parser->function;
    GrowArray(parser->L, function->code, function->codeSize, function->maxCodeSize);
    GrowArray(parser->L, function->sourceLine, function->codeSize, function->maxSourceLines);

    int n = function->codeSize;
    ++function->codeSize;

    function->code[n] = inst;
    function->sourceLine[n] = parser->lineNumber;

    return n;

}

void Parser_UpdateInstruction(Parser* parser, int pos, Instruction inst)
{
    Function* function = parser->function;
    ASSERT( pos >= 0 && pos < function->codeSize );
    function->code[pos] = inst;
}

Instruction Parser_GetInstruction(Parser* parser, int pos)
{
    Function* function = parser->function;
    ASSERT( pos >= 0 && pos < function->codeSize );
    return function->code[pos];
}

int Parser_GetInstructionCount(Parser* parser)
{
    Function* function = parser->function;
    return function->codeSize;
}

int Parser_EmitABC(Parser* parser, Opcode opcode, int a, int b, int c)
{
    return Parser_EmitInstruction(parser, Opcode_EncodeABC(opcode, a, b, c));
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
    Parser_EmitInstruction(parser, Opcode_EncodeAsBx(opcode, a, sbx));
}

void Parser_BeginSkip(Parser* parser, int* id)
{
    *id = Parser_EmitInstruction(parser, 0);  // Placeholder for jump.
}

void Parser_EndSkip(Parser* parser, int* id)
{
    int jumpAmount = static_cast<int>(Parser_GetInstructionCount(parser) - *id - 1);
    Parser_UpdateInstruction(parser, *id, Opcode_EncodeAsBx(Opcode_Jmp, 0, jumpAmount));
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

/** Returns true of the opcode is a comparison (==, ~=, >=, etc.) */
static bool GetIsComparison(Opcode opcode)
{
    return opcode == Opcode_Eq ||
           opcode == Opcode_Le ||
           opcode == Opcode_Lt;
}

/** Returns true if the opcode is a test instruction */
static bool GetIsTest(Opcode opcode)
{
    return opcode == Opcode_Test ||
           opcode == Opcode_TestSet;
}

static int Parser_GetJumpList(Parser* parser, int jumpPos, int jump[256])
{
    int numJumps = 0;
    while (jumpPos != -1)
    {
        ASSERT(numJumps < 256);
        jump[numJumps] = jumpPos;
        ++numJumps;
        jumpPos = Parser_GetInstruction(parser, jumpPos);
    }
    return numJumps;
}

static int SortJumpDescending(const void * a, const void * b)
{
    return *(int*)b - *(int*)a;
}

static void Parser_UpdateJumpChain(Parser* parser, int jumpPos, int value, int reg, int startPos = -1)
{

    int jump[256];
    int numJumps = Parser_GetJumpList(parser, jumpPos, jump);

    // Sort the jumps in reverse order so that we process the last one in the
    // instruction stream first.
    qsort(jump, numJumps, sizeof(int), SortJumpDescending);

    bool emitBool = false;

    for (int i = 0; i < numJumps; ++i)
    {

        int jumpPos = jump[i];

        Instruction inst = Parser_GetInstruction(parser, jumpPos - 1);
        Opcode opcode = GET_OPCODE(inst);

        if (startPos == -1)
        {
            startPos = Parser_GetInstructionCount(parser);
        }

        if (reg != -1)
        {

            // If B is set for a test instruction, that means that it has a not folded
            // into it, which requires the value to be coerced into a boolean.
            if (GetIsComparison(opcode) || (opcode == Opcode_Test && GET_B(inst)))
            {
                if (!emitBool)
                {
                    // Since we have a logic test, we'll need to output a boolean at the
                    // end of the test.
                    if (startPos == jumpPos + 1)
                    {
                        Parser_EmitABC(parser, Opcode_LoadBool, reg, 1 - value, 1);
                    }
                    else
                    {
                        Parser_EmitAsBx(parser, Opcode_Jmp, 0, 1);
                    }
                    ++startPos;
                    
                    // Jump to the loadbool instruction.
                    Parser_EmitABC(parser, Opcode_LoadBool, reg, value, 0);
                    emitBool = true;
                }
            }
            else if (opcode == Opcode_Test && reg != GET_A(inst))
            {
                // Update the instruction to a testset so that we have a value in
                // the "true" case.
                inst = Opcode_EncodeABC(Opcode_TestSet, reg, GET_A(inst), GET_C(inst));
                Parser_UpdateInstruction(parser, jumpPos - 1, inst);
            }

        }

        // Update the jump instruction with the actual amount to jump.
        int jumpAmount = static_cast<int>(startPos - jumpPos - 1);
        Parser_UpdateInstruction(parser, jumpPos, Opcode_EncodeAsBx(Opcode_Jmp, 0, jumpAmount));
        
    }

}

void Parser_FinalizeExitJump(Parser* parser, Expression* value, int cond, int reg)
{
    Parser_UpdateJumpChain(parser, value->exitJump[cond], 0, reg, -1);
    value->exitJump[cond] = -1;
}

static void Parser_FinalizeExitJumps(Parser* parser, Expression* value, int reg, int startPos = -1)
{
    Parser_UpdateJumpChain(parser, value->exitJump[1], 1, reg, startPos);
    value->exitJump[1] = -1;
    Parser_UpdateJumpChain(parser, value->exitJump[0], 0, reg, startPos);
    value->exitJump[0] = -1;
}

bool Parser_ResolveCall(Parser* parser, Expression* value, int numResults)
{
    if (value->type == EXPRESSION_CALL)
    {
        Parser_EmitABC(parser, Opcode_Call, value->index, value->numArgs + 1, numResults + 1);
        value->type = EXPRESSION_REGISTER;
        if (numResults != -1)
        {
            Parser_SetLastRegister(parser, value->index + numResults - 1);
        }
        Parser_FinalizeExitJumps(parser, value, value->index);
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
        if (numResults != -1)
        {
            Parser_SetLastRegister(parser, value->index + numResults - 1);
        }
        return true;
    }
    return false;
}

void Parser_OpenJump(Parser* parser, Expression* dst)
{
    dst->index   = Parser_EmitInstruction(parser, -1);
    dst->type    = EXPRESSION_JUMP;
}

void Parser_AddExitJump(Parser* parser, Expression* jump, int test, int jumpPos)
{

    int tail = jump->exitJump[test];
    jump->exitJump[test] = jumpPos;

    // Since jumpPos might be the start of a linked list of jumps, find the end
    // of that list and point to the old list.
    while (Parser_GetInstruction(parser, jumpPos) != -1)
    {
        jumpPos = Parser_GetInstruction(parser, jumpPos);
    }
    Parser_UpdateInstruction(parser, jumpPos, tail);

}

/** Update the parity of the test for the instruction before the jump (if there
is a test before the jump). **/
static void Parser_InvertTest(Parser* parser, Expression* value)
{
    ASSERT(value->type == EXPRESSION_JUMP);
    int pos = value->index - 1;
    if (pos >= 0)
    {
        Instruction inst = Parser_GetInstruction(parser, pos);
        Opcode op = GET_OPCODE(inst);

        if (GetIsComparison(op))
        {
            int cond = GET_A(inst);
            inst = Opcode_EncodeABC( op, !cond, GET_B(inst), GET_C(inst) );
            Parser_UpdateInstruction(parser, pos, inst);
        }
        else if (GetIsTest(op))
        {
            int cond = GET_C(inst);
            inst = Opcode_EncodeABC( op, GET_A(inst), GET_B(inst), !cond );
            Parser_UpdateInstruction(parser, pos, inst);
        }

    }
}

void Parser_ConvertToTest(Parser* parser, Expression* value, int test, int reg)
{

    if (value->type == EXPRESSION_JUMP)
    {
        if (!test)
        {
            Parser_InvertTest(parser, value);
        }
    }
    else if (value->type == EXPRESSION_NOT)
    {
        // Note, we set the B value here to indicate that the value for this test
        // must be coerced to a boolean. The parser will take this into account.
        Parser_EmitABC(parser, Opcode_Test, value->index, 1, !test);
        Parser_OpenJump(parser, value);
    }
    else
    {
        if (!Parser_ConvertToRegister(parser, value))
        {
            Parser_MoveToRegister(parser, value, reg);
        }
        Parser_EmitABC(parser, Opcode_Test, value->index, 0, test);
        Parser_OpenJump(parser, value);
    }

    // Close up the jumps for the other branch.
    Parser_FinalizeExitJump(parser, value, 1 - test, reg);

    Parser_AddExitJump(parser, value, test, value->index);

    // This type of expression is only used as an intermediate, so change it's
    // type so that we can catch and problems if it's not used that way.
    value->type = EXPRESSION_NONE;

}

void Parser_CloseJump(Parser* parser, Expression* value, int startPos)
{
    Parser_FinalizeExitJumps(parser, value, -1, startPos);
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
            ASSERT(index != -1);
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

void Parser_ResolveName(Parser* parser, Expression* dst, String* name)
{
    int index = Parser_GetLocalIndex( parser, name );
    if (index != -1)
    {
        dst->type  = EXPRESSION_LOCAL;
        dst->index = index;
    }
    else
    {
        // Check if this is an up value.
        index = Parser_AddUpValue( parser, name );
        if (index != -1)
        {
            dst->type  = EXPRESSION_UPVALUE;
            dst->index = index;
        }
        else
        {
            dst->type  = EXPRESSION_GLOBAL;
            dst->index = Parser_AddConstant( parser, name );
        }
   }
}

static bool GetHasExitJumps(Expression* value)
{
    return value->exitJump[0] != -1 || value->exitJump[1] != -1;
}

static void Parser_UpdateTempLocation(Parser* parser, Expression* value, int reg)
{
    ASSERT(value->type == EXPRESSION_TEMP);
        
    Instruction inst = Parser_GetInstruction(parser, value->index);

    int b = GET_B(inst);
    int c = GET_C(inst);
    
    inst = Opcode_EncodeABC( GET_OPCODE(inst), reg, b, c );
    Parser_UpdateInstruction( parser, value->index, inst );

    value->type = EXPRESSION_REGISTER;
    value->index = reg;
}

int Parser_MoveToRegister(Parser* parser, Expression* value, int reg)
{

    if (reg != -1 && reg > Parser_GetNumRegisters(parser))
    {
        Parser_SetLastRegister(parser, reg);
    }

    Parser_ResolveCall(parser, value, 1);
    Parser_ConvertToRegister(parser, value);

    if (value->type == EXPRESSION_REGISTER)
    {
        // The value is already in a register, so nothing to do.
        if ((reg == -1 && !GetHasExitJumps(value)) || value->index == reg)
        {
            if (GetHasExitJumps(value))
            {
                Parser_UpdateJumpChain(parser, value->exitJump[1], 1, value->index);
                value->exitJump[1] = -1;

                int skip;
                Parser_BeginSkip(parser, &skip);
        
                Parser_UpdateJumpChain(parser, value->exitJump[0], 0, value->index);
                value->exitJump[0] = -1;

                Parser_EndSkip(parser, &skip);
            }
            return value->index;
        }
    }

    // Select the desination register.
    if (reg == -1)
    {
        reg = Parser_AllocateRegister(parser);
    }

    if (value->type == EXPRESSION_TEMP)
    {
        Parser_UpdateTempLocation(parser, value, reg);
        return value->index;
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

        Expression key;
        key.type  = value->keyType;
        key.index = value->key;

        Parser_MakeRKEncodable(parser, &key);
        Parser_EmitABC(parser, Opcode_GetTable, reg, value->index,
            Parser_EncodeRK(parser, &key));

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
    else if (value->type == EXPRESSION_JUMP)
    {
        // The jump should be after a comparison.
        Parser_AddExitJump(parser, value, 1, value->index);
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
        ASSERT(0);
    }

    value->type     = EXPRESSION_REGISTER;
    value->index    = reg;

    Parser_FinalizeExitJumps(parser, value, reg);

    return reg;

}

void Parser_MoveToRegisterOrConstant(Parser* parser, Expression* value, int reg)
{
    // If the value is one of our literal types, move it to a constant slot.
    Parser_ConvertLiteralToConstant(parser, value);
    // Anything else we move to a register.
    // If we have jumps, then we also need to move to a register so we properly
    // output the value.
    if (value->type != EXPRESSION_CONSTANT || GetHasExitJumps(value))
    {
        Parser_MoveToRegister(parser, value, reg);
    }
}

void Parser_MakeRKEncodable(Parser* parser, Expression* value)
{
    Parser_MoveToRegisterOrConstant(parser, value);
    if (value->index >= 256)
    {
        // Values greater than 255 can't be RK encoded, so we need to move to a
        // register which has a smaller index.
        int reg = Parser_AllocateRegister(parser);
        if (reg >= 256)
        {
            // This means we've used too many registers.
            Parser_Error(parser, "internal error RK encoding");
        }
        Parser_MoveToRegister(parser, value, reg);
    }
}

void Parser_MoveToStackTop(Parser* parser, Expression* value, int regHint)
{
    Function* function = parser->function;

    int reg = -1;
    if (regHint != -1 && regHint == function->numRegisters - 1)
    {
        reg = regHint;
    }
    Parser_MoveToRegister(parser, value, reg);

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

Prototype* Function_CreatePrototype(lua_State* L, Function* function, String* source)
{

    Prototype* prototype = Prototype_Create(
        L,
        function->codeSize,
        function->numConstants,
        function->numFunctions,
        function->numUpValues);

    PushPrototype(L, prototype);

    // Store the up values.
    memcpy(prototype->upValue, function->upValue, sizeof(String*) * function->numUpValues);

    // Store the code.
    memcpy(prototype->code, function->code, function->codeSize * sizeof(Instruction));

    // Store the source information.
    prototype->source = source;
    memcpy(prototype->sourceLine, function->sourceLine, function->codeSize * sizeof(int));

    // Store the functions.
    for (int i = 0; i < function->numFunctions; ++i)
    {
        prototype->prototype[i] = Function_CreatePrototype(L, function->function[i], source);
        Gc_WriteBarrier(L, prototype, prototype->prototype[i]);
    }

    // Store the constants.

    Value key;
    SetNil(&key);

    Table* constants = function->constants;
    const Value* value;

    while (value = Table_Next(constants, &key))
    {
        ASSERT(Value_GetIsNumber(value));
        int i = static_cast<int>(value->number);
        if (Value_GetIsTable(&key) && key.table == constants)
        {
            // The table itself is used to indicate nil values since nil values
            // cannot be saved in the table.
            SetNil(&prototype->constant[i]);
        }
        else
        {
            prototype->constant[i] = key;
        }
        Gc_WriteBarrier(L, prototype, &prototype->constant[i]);
    }
    
    prototype->varArg       = function->varArg;
    prototype->numParams    = function->numParams;
    prototype->maxStackSize = function->maxStackSize;
    prototype->numUpValues  = function->numUpValues;

    prototype->lineDefined      = 0;
    prototype->lastLineDefined  = 0;

    //PrintFunction(prototype);

    ASSERT( (L->stackTop - 1)->object == prototype );
    Pop(L, 1);

    return prototype;

}

void Parser_BeginBlock(Parser* parser, bool breakable)
{
    if (parser->numBlocks == LUAI_MAXCCALLS)
    {
        Parser_Error(parser, "too many block levels");
    }

    // Locals must be commited before starting a block.
    ASSERT( parser->function->numLocals == parser->function->numCommitedLocals );

    Block* block = &parser->block[parser->numBlocks];
    block->firstLocal           = parser->function->numLocals;
    block->breakable            = breakable;
    block->firstBreakPos        = -1;
    block->firstLocalUpValue    = -1;
    ++parser->numBlocks;
}

bool Parser_GetHasUpValues(Parser* parser)
{
    ASSERT(parser->numBlocks > 0);
    Block* block = &parser->block[parser->numBlocks - 1]; 
    return block->firstLocalUpValue != -1;
}

static void Parser_CloseUpValues(Parser* parser, Block* block)
{
    if (block->firstLocalUpValue != -1)
    {
        // Close an local up values.
        Parser_EmitAB(parser, Opcode_Close, block->firstLocalUpValue, 0);
    }
}

void Parser_CloseUpValues(Parser* parser)
{
    Block* block = &parser->block[parser->numBlocks - 1]; 
    Parser_CloseUpValues(parser, block);
}

void Parser_EndBlock(Parser* parser)
{

    ASSERT(parser->numBlocks > 0);
    Block* block = &parser->block[parser->numBlocks - 1]; 
    
    // Update the break instructions.
    
    int breakPos = block->firstBreakPos;
    int currentPos = Parser_GetInstructionCount(parser);

    while (breakPos != -1)
    {
        int nextBreakPos = Parser_GetInstruction(parser, breakPos);
        int jumpAmount = currentPos - breakPos - 1;
        Instruction inst = Opcode_EncodeAsBx(Opcode_Jmp, 0, jumpAmount);
        Parser_UpdateInstruction(parser, breakPos, inst);
        breakPos = nextBreakPos;
    }

    Parser_CloseUpValues(parser, block);

    --parser->numBlocks;
    parser->function->numLocals = parser->block[parser->numBlocks].firstLocal;
    parser->function->numCommitedLocals = parser->function->numLocals;

    Parser_FreeRegisters(parser);

}

int Parser_GetToken(Parser* parser)
{
    return parser->lexer->token.type;
}

String* Parser_GetString(Parser* parser)
{
    ASSERT( parser->lexer->token.type == TokenType_Name ||
            parser->lexer->token.type == TokenType_String );
    return parser->lexer->token.string;
}

lua_Number Parser_GetNumber(Parser* parser)
{
    ASSERT( parser->lexer->token.type == TokenType_Number );
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
        Parser_CloseUpValues(parser, &parser->block[blockIndex]);
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