/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */

#include "Parser.h"
#include "Lexer.h"
#include "Opcode.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void Parser_Block(Parser* parser, int endToken);
static void Parser_Statement(Parser* parser);
static void Parser_Expression0(Parser* parser, Expression* dst, int regHint);

/**
 * Attempts to fold the two values together using the specified operator. If
 * the constants can not be folded, the method returns false.
 */
static bool Parser_FoldConstants(Opcode opcode, lua_Number& dst, lua_Number arg1, lua_Number arg2)
{
    switch (opcode)
    {
    case Opcode_Add:
        dst = luai_numadd(arg1, arg2);
        break;
    case Opcode_Sub:
        dst = luai_numsub(arg1, arg2);
        break;
    case Opcode_Mul:
        dst = luai_nummul(arg1, arg2);
        break;
    case Opcode_Div:
        dst = luai_numdiv(arg1, arg2);
        break;
    default:
        return false;
    }
    return true;
}

/**
 * regHint specifies the index of the register the result should be stored in if
 * the result will be in a register (or -1 if the caller does not require the
 * result to be a specific location).
 */
static void Parser_EmitArithmetic(Parser* parser, int op, Expression* dst, int regHint, Expression* arg1, Expression* arg2)
{

    assert(dst != arg1);
    assert(dst != arg2);

    Opcode opcode;
    switch (op)
    {
    case '+':
        opcode = Opcode_Add;
        break;
    case '-':
        opcode = Opcode_Sub;
        break;
    case '*':
        opcode = Opcode_Mul;
        break;
    case '/':
        opcode = Opcode_Div;
        break;
    }
    
    if (arg1->type == EXPRESSION_NUMBER &&
        arg2->type == EXPRESSION_NUMBER)
    {
        if (Parser_FoldConstants(opcode, dst->number, arg1->number, arg2->number))
        {
            dst->type = EXPRESSION_NUMBER;
            return;
        }
    }

    Parser_MoveToRegisterOrConstant(parser, arg1);
    Parser_MoveToRegisterOrConstant(parser, arg2);

    if (regHint != -1)
    {
        dst->index = regHint;
    }
    else
    {
        if (Parser_GetIsTemporaryRegister(parser, arg1))
        {
            dst->index = arg1->index;
        }
        else if (Parser_GetIsTemporaryRegister(parser, arg2))
        {
            dst->index = arg2->index;
        }
        else
        {
            dst->index = Parser_AllocateRegister(parser);
        }
    }
    dst->type = EXPRESSION_REGISTER;

    Parser_EmitABC(parser, opcode, dst->index, 
        Parser_EncodeRK(parser, arg1), 
        Parser_EncodeRK(parser, arg2));

}

static void Parser_EmitComparison(Parser* parser, int op, Expression* dst, int regHint, Expression* arg1, Expression* arg2)
{

    assert(dst != arg1);
    assert(dst != arg2);

    Parser_MoveToRegisterOrConstant(parser, arg1);
    Parser_MoveToRegisterOrConstant(parser, arg2);

    Opcode opcode;
    int    value;

    if (op == Token_Eq)
    {
        opcode = Opcode_Eq;
        value  = 0;
    }
    else if (op == Token_Ne)
    {
        opcode = Opcode_Eq;
        value  = 1;
    }
    else if (op == '<')
    {
        opcode = Opcode_Lt;
        value = 0;
    }
    else if (op == Token_Le)
    {
        opcode = Opcode_Le;
        value = 0;
    }
    else if (op == '>')
    {
        opcode = Opcode_Le;
        value = 1;
    }
    else if (op == Token_Ge)
    {
        opcode = Opcode_Lt;
        value = 1;
    }
    else
    {
        assert(0);
    }

    dst->type  = EXPRESSION_TEST;
    dst->index = Parser_EmitABC(parser, opcode, value, 
        Parser_EncodeRK(parser, arg1), 
        Parser_EncodeRK(parser, arg2));

    // Save a place for the jmp instruction.
    Parser_EmitInstruction(parser, 0);

}

static int Parser_Arguments(Parser* parser)
{
    int  numArgs = 0;
    bool varArg  = false;

    while (1)
    {

        if (Parser_Accept(parser, ')'))
        {
            Parser_Next(parser);
            break;
        }
        if (numArgs > 0)
        {
            Parser_Expect(parser, ',');
            Parser_Next(parser);
        }

        int reg = Parser_AllocateRegister(parser);

        Expression arg;
        Parser_Expression0(parser, &arg, reg);

        bool last = Parser_Accept(parser, ')');

        if (last)
        {
            // Allow variable number of arguments for a function call which
            // is the last argument.
            varArg = Parser_ResolveCall(parser, &arg, -1);
        }

        // Make sure the result is stored in the register for the argument and
        // free up any temporary registers we used.
        Parser_MoveToRegister(parser, &arg, reg);
        Parser_SetLastRegister(parser, reg);

        ++numArgs;

    }

    if (varArg)
    {
        return -1;
    }
    return numArgs;

}

/**
 * Parses a function and stores it in dst. If method is true, a 'self' parameter
 * will be added to the function.
 */
static void Parser_Function(Parser* parser, Expression* dst, bool method)
{

    Parser p;
    Parser_Initialize(&p, parser->L, parser->lexer, parser->function);

    if (method)
    {
        Parser_AddLocal( &p, String_Create(parser->L, "self") );
    }

    // Parse the arguments.
    Parser_Expect(parser, '(');
    Parser_Next(parser);

    while (!Parser_Accept(parser, ')'))
    {
        if (p.function->numParams > 0)
        {
            Parser_Expect(parser, ',');
            Parser_Next(parser);
        }
        Parser_Expect(parser, Token_Name);
        // Add the parameter as a local, since they have the same semantics.
        Parser_AddLocal( &p, parser->lexer->string );
        ++p.function->numParams;
        Parser_Next(parser);
    }
    Parser_Next(parser);

    if (method)
    {
        ++p.function->numParams;
    }

    // Parse the body.
    Parser_Block(&p, Token_End);
    Parser_EmitAB(&p, Opcode_Return, 0, 1);

    // Store in the result parent function.
    dst->type  = EXPRESSION_FUNCTION;
    dst->index = Parser_AddFunction(parser, p.function);

}

static void Parser_Expression5(Parser* parser, Expression* dst, int regHint)
{
    if (Parser_Accept(parser, Token_Name))
    {
        int index = Parser_GetLocalIndex(parser, parser->lexer->string);
        if (index != -1)
        {
            dst->type  = EXPRESSION_LOCAL;
            dst->index = index;
        }
        else
        {
            // Check if this is an up value.
            index = Parser_AddUpValue(parser, parser->lexer->string);
            if (index != -1)
            {
                dst->type  = EXPRESSION_UPVALUE;
                dst->index = index;
            }
            else
            {
                dst->type  = EXPRESSION_GLOBAL;
                dst->index = Parser_AddConstant(parser, parser->lexer->string);
            }
        }
        Parser_Next(parser);
    }
    else if (Parser_Accept(parser, Token_String))
    {
        dst->type  = EXPRESSION_CONSTANT;
        dst->index = Parser_AddConstant(parser, parser->lexer->string);
        Parser_Next(parser);
    }    
    else if (Parser_Accept(parser, Token_Number))
    {
        dst->type   = EXPRESSION_NUMBER;
        dst->number = parser->lexer->number;
        Parser_Next(parser);
    }
    else if (Parser_Accept(parser, Token_True) ||
             Parser_Accept(parser, Token_False))
    {
        dst->type  = EXPRESSION_BOOLEAN;
        dst->index = (parser->lexer->token == Token_True) ? 1 : 0;
        Parser_Next(parser);
    }
    else if (Parser_Accept(parser, Token_Nil))
    {
        dst->type = EXPRESSION_NIL;
        Parser_Next(parser);
    }
    else if (Parser_Accept(parser, Token_Function))
    {
        Parser_Next(parser);
        Parser_Function(parser, dst, false);
    }
    else if (Parser_Accept(parser, '('))
	{
        Parser_Next(parser);
		Parser_Expression0(parser, dst, regHint);
		Parser_Expect(parser, ')');
        Parser_Next(parser);
	}
    else if (Parser_Accept(parser, '{'))
    {
        
        // Table constructor.
        Parser_Next(parser);
        Parser_SelectDstRegister(parser, dst, regHint);
        Parser_EmitABC(parser, Opcode_NewTable, dst->index, 0, 0);

        int num = 0;
        while (!Parser_Accept(parser, '}'))
        {
        }

        Parser_Next(parser);

    }
    else
    {
        Parser_Error(parser, "expected variable or constant");
    }
}

static void Parser_Expression4(Parser* parser, Expression* dst, int regHint)
{
    
    Parser_Expression5(parser, dst, regHint);

    while (Parser_Accept(parser, '(') ||
           Parser_Accept(parser, '.') ||
           Parser_Accept(parser, '['))
    {

        int op = parser->lexer->token;
        Parser_Next(parser);

        if (op == '(')
        {
            // Handle function calls.
            // TODO: Make sure the register is temporary!
            Parser_MoveToStackTop(parser, dst);
            dst->type    = EXPRESSION_CALL;
            dst->numArgs = Parser_Arguments(parser);
        }
        else if (op == '.')
        {

            // Handle table indexing (object form).
            
            Parser_Expect(parser, Token_Name);

            Parser_MoveToRegister(parser, dst, regHint);
            dst->type = EXPRESSION_TABLE;

            dst->keyType = EXPRESSION_CONSTANT;
            dst->key     = Parser_AddConstant(parser, parser->lexer->string);

            Parser_Next(parser);

        }
        else if (op == '[')
        {
            
            // Handle table indexing (general form).

            Parser_MoveToRegister(parser, dst);
            dst->type = EXPRESSION_TABLE;

            Expression key;
            Parser_Expression0(parser, &key, -1);

            // Table indexing must be done with a constant or a register.
            Parser_MoveToRegisterOrConstant(parser, &key);
            if (key.type == EXPRESSION_REGISTER)
            {
                dst->keyType = EXPRESSION_REGISTER;
                dst->key     = key.index;
            }
            else
            {
                dst->keyType = EXPRESSION_CONSTANT;
                dst->key     = key.index;
            }
            
            Parser_Expect(parser, ']');
            Parser_Next(parser);

        }
    
    }

}

static void Parser_ExpressionMethod(Parser* parser, Expression* dst, int regHint)
{

    Parser_Expression4(parser, dst, regHint);

    // Handle Foo:Bar()
    if (Parser_Accept(parser, ':'))
    {

        Parser_Next(parser);
        Parser_Expect(parser, Token_Name);

        Parser_MoveToRegister(parser, dst, -1);

        int reg     = Parser_AllocateRegister(parser);
        int method  = Parser_AddConstant(parser, parser->lexer->string);

        Parser_Next(parser);

        // TODO: Handle string/table only arguments.

        Parser_Expect(parser, '(');
        Parser_Next(parser);

        Parser_EmitABC(parser, Opcode_Self, reg, dst->index, Parser_EncodeRK(method, EXPRESSION_CONSTANT));

        dst->type    = EXPRESSION_CALL;
        dst->index   = reg;
        dst->numArgs = Parser_Arguments(parser);

        if (dst->numArgs != -1)
        {
            // If this isn't a vararg function we need to count the self parameter.
            ++dst->numArgs;
        }

    }

}

static void Parser_ExpressionUnary(Parser* parser, Expression* dst, int regHint)
{
    if (Parser_Accept(parser, Token_Not))
    {
        Parser_Next(parser);        
        Parser_ExpressionMethod(parser, dst, regHint);
        Parser_MoveToRegister(parser, dst, regHint);
        dst->type = EXPRESSION_NOT;
    }
    else
    {
        Parser_ExpressionMethod(parser, dst, regHint);
    }
}

static void Parser_Expression3(Parser* parser, Expression* dst, int regHint)
{
    Parser_ExpressionUnary(parser, dst, regHint);
	while (Parser_Accept(parser, '*') ||
           Parser_Accept(parser, '/'))
	{
		int op = parser->lexer->token;
		Parser_Next(parser);

        Expression arg1 = *dst;
        Parser_ResolveCall(parser, &arg1, 1);

        Expression arg2;
        Parser_Expression3(parser, &arg2, -1);
        Parser_EmitArithmetic(parser, op, dst, regHint, &arg1, &arg2);
	}
}

static void Parser_Expression2(Parser* parser, Expression* dst, int regHint)
{
    Parser_Expression3(parser, dst, regHint);
	while (Parser_Accept(parser, '+') ||
           Parser_Accept(parser, '-'))
	{
		int op = parser->lexer->token;
		Parser_Next(parser);

        Expression arg1 = *dst;
        Parser_ResolveCall(parser, &arg1, 1);

        Expression arg2;
        Parser_Expression3(parser, &arg2, -1);
        Parser_EmitArithmetic(parser, op, dst, regHint, &arg1, &arg2);
	}
}

static void Parser_Expression1(Parser* parser, Expression* dst, int regHint)
{
    Parser_Expression2(parser, dst, regHint);
    while ( Parser_Accept(parser, Token_Eq) ||
            Parser_Accept(parser, Token_Ne) ||
            Parser_Accept(parser, Token_Le) ||
            Parser_Accept(parser, Token_Ge) ||
            Parser_Accept(parser, '<') || 
            Parser_Accept(parser, '>'))
    {
        int op = parser->lexer->token;
        Parser_Next(parser);

        Expression arg1 = *dst;
        Parser_ResolveCall(parser, &arg1, 1);

        Expression arg2;
        Parser_Expression2(parser, &arg2, -1);
        Parser_EmitComparison(parser, op, dst, regHint, &arg1, &arg2);
    }
}

static void Parser_ExpressionLogic(Parser* parser, Expression* dst, int regHint)
{

    Parser_Expression1(parser, dst, regHint);

    while ( Parser_Accept(parser, Token_And) ||
            Parser_Accept(parser, Token_Or) )
    {
        
        int op = parser->lexer->token;
        Parser_Next(parser);

        int cond = op == Token_Or ? 1 : 0;

        if (regHint == -1)
        {
            regHint = Parser_AllocateRegister(parser);
            Parser_MoveToRegister(parser, dst);
            Parser_EmitABC(parser, Opcode_TestSet, regHint, dst->index, cond);
        }
        else
        {
            Parser_MoveToRegister(parser, dst, regHint);
            Parser_EmitABC(parser, Opcode_Test, dst->index, 0, cond);
        }

        dst->type  = EXPRESSION_REGISTER;
        dst->index = regHint;

        int skipBlock;
        Parser_BeginSkip(parser, &skipBlock);

        // dst = arg2
        Expression arg2;
        Parser_Expression1(parser, &arg2, dst->index);
        Parser_MoveToRegister(parser, &arg2, dst->index);

        Parser_EndSkip(parser, &skipBlock);

    }

}

static void Parser_Expression0(Parser* parser, Expression* dst, int regHint)
{
    
    Parser_ExpressionLogic(parser, dst, regHint);

    if (Parser_Accept(parser, Token_Concat))
    {

        Parser_MoveToStackTop(parser, dst);
        int start = dst->index;
        int numOperands = 0;

        while (Parser_Accept(parser, Token_Concat))
        {
		    
            Parser_Next(parser);

            int reg = Parser_AllocateRegister(parser);

            Expression arg;
            Parser_ExpressionLogic(parser, &arg, reg);

            // Make sure the result is stored in our consecutive register and
            // free up any temporary registers we used.
            Parser_MoveToRegister(parser, &arg, reg);
            Parser_SetLastRegister(parser, reg);
             
            ++numOperands;

        }

        Parser_EmitABC(parser, Opcode_Concat, dst->index,
            start, start + numOperands);

    }

}

/**
 * Generates instructions to perform the operation: dst = value
 */
static void Parser_EmitSet(Parser* parser, Expression* dst, Expression* value)
{
    if (dst->type == EXPRESSION_LOCAL)
    {
        Parser_MoveToRegister(parser, value, dst->index);
    }
    else if (dst->type == EXPRESSION_GLOBAL)
    {
        Parser_MoveToRegister(parser, value);
        Parser_EmitABx(parser, Opcode_SetGlobal, value->index, dst->index);
    }
    else if (dst->type == EXPRESSION_TABLE)
    {
        Parser_MoveToRegisterOrConstant(parser, value);
        Parser_EmitABC(parser, Opcode_SetTable, dst->index,
            Parser_EncodeRK(dst->key, dst->keyType),
            Parser_EncodeRK(parser, value));
    }   
    else if (dst->type == EXPRESSION_UPVALUE)
    {
        Parser_MoveToRegister(parser, value);
        Parser_EmitAB(parser, Opcode_SetUpVal, dst->index, value->index);
    }
    else
    {
        Parser_Error(parser, "illegal assignment");
    }
}

static bool Parser_TryIf(Parser* parser)
{

    if (Parser_Accept(parser, Token_If))
    {
        Parser_Next(parser);
        
        // Parse the condition to test for the if statement.
        Expression test;
        Parser_Expression0(parser, &test, -1);

        Parser_Expect(parser, Token_Then);
        Parser_Next(parser);

        // TODO: Peform "constant folding" for the test.
        Parser_ConvertToTest(parser, &test);
        Parser_BeginBlock(parser);

        // Parse the "if" part of the conditional.
        while (!Parser_Accept(parser, Token_End) &&
               !Parser_Accept(parser, Token_Else))
        {
            Parser_Statement(parser);
        }

        Parser_EndBlock(parser);

        int type = parser->lexer->token;
        Parser_Next(parser);

        if (type == Token_Else)
        {

            int elseJump;
            Parser_BeginBlock(parser);
            Parser_BeginSkip(parser, &elseJump);
            Parser_CloseTest(parser, &test);

            // Parse the "else" part of the conditional.
            while (!Parser_Accept(parser, Token_End))
            {
                Parser_Statement(parser);
            }

            Parser_Next(parser);
            Parser_EndSkip(parser, &elseJump);
            Parser_EndBlock(parser);

        }
        else
        {
            Parser_CloseTest(parser, &test);
        }

        return true;

    }

    return false;

}

static bool Parser_TryReturn(Parser* parser)
{
    if (Parser_Accept(parser, Token_Return))
    {
        
        Parser_Next(parser);

        int numResults = 0;
        int reg        = -1;

        Expression arg;

        while (!Parser_Accept(parser, Token_End) &&
               !Parser_Accept(parser, Token_Else) &&
               !Parser_Accept(parser, Token_ElseIf))
        {

            if (numResults == 0)
            {
                // The first result is handleded specially so that if we're only
                // returning a single argument and it's already in a register we
                // don't need to move it to the top of the stack.
                Parser_Expression0(parser, &arg, -1);
            }
            else
            {
                Parser_Expect(parser, ',');
                Parser_Next(parser);

                if (numResults == 1)
                {
                    // If the first argument wasn't in the final register on the
                    // stack, we need to move it there now so we can have all of
                    // the results in a row on the stack.
                    Parser_MoveToStackTop(parser, &arg);
                    reg = arg.index;
                }

                Parser_Expression0(parser, &arg, reg + numResults);
                Parser_MoveToRegister(parser, &arg, reg + numResults);
                Parser_SetLastRegister(parser, reg + numResults);
            }

            ++numResults;

        }

        if (reg == -1)
        {
            Parser_MoveToRegister(parser, &arg);
            reg = arg.index;
        }

        Parser_EmitAB(parser, Opcode_Return, reg, numResults + 1);
        Parser_FreeRegisters(parser);
        return true;

    }
    return false;
}

/**
 * Handles combined function declare, assignment:
 *    function Foo.Bar() end
 * If the key word 'local' appears before the declaration, the local parameter
 * should be set to true.
 */
static bool Parser_TryFunction(Parser* parser, bool local)
{

    if (!Parser_Accept(parser, Token_Function))
    {
        return false;
    }
        
    Parser_Next(parser);
    Parser_Expect(parser, Token_Name);

    Expression dst;

    // Methods are functions which have an implicit self parameter.
    bool method = false;

    if (local)
    {
        dst.index = Parser_AddLocal(parser, parser->lexer->string);
        dst.type  = EXPRESSION_LOCAL;
        Parser_Next(parser);
    }
    else
    {
        dst.index = Parser_AddConstant(parser, parser->lexer->string);
        dst.type  = EXPRESSION_GLOBAL;
        Parser_Next(parser);

        // Check if we are of the form function A.B.C:D()
        while (Parser_Accept(parser, '.') ||
               Parser_Accept(parser, ':'))
        {

            int token = parser->lexer->token;

            Parser_Next(parser);
            Parser_Expect(parser, Token_Name);

            Parser_MoveToRegister(parser, &dst);
            dst.type = EXPRESSION_TABLE;

            dst.keyType = EXPRESSION_CONSTANT;
            dst.key     = Parser_AddConstant(parser, parser->lexer->string);

            Parser_Next(parser);

            if (token == ':')
            {
                // Nothing else can come after the ':name'
                method = true;
                break;
            }

        }
    
    }
    
    Expression arg;
    Parser_Function(parser, &arg, method);
    Parser_EmitSet(parser, &dst, &arg);
    
    Parser_FreeRegisters(parser);

    return true;

}

static bool Parser_TryLocal(Parser* parser)
{
    
    if (!Parser_Accept(parser, Token_Local))
    {
        return false;
    }
    Parser_Next(parser);

    if (Parser_TryFunction(parser, true))
    {
        return true;
    }

    Parser_Expect(parser, Token_Name);

    char name[LUA_MAXNAME];
    strcpy( name, String_GetData(parser->lexer->string) );

    Parser_Next(parser);
    
    if (Parser_Accept(parser, '='))
    {
    
        Parser_Next(parser);

        Expression dst;
        Parser_Expression0(parser, &dst, -1);
        
        int local = Parser_AddLocal(parser, String_Create(parser->L, name));
        Parser_MoveToRegister(parser, &dst, local);
        dst.type = EXPRESSION_LOCAL;

    }
    else
    {
        Parser_AddLocal(parser, String_Create(parser->L, name));
    }

    return true;

}

static bool Parser_TryDo(Parser* parser)
{

    if (!Parser_Accept(parser, Token_Do))
    {
        return false;
    }
    Parser_Next(parser);

    Parser_BeginBlock(parser);
    Parser_Block(parser, Token_End);
    Parser_EndBlock(parser);

    return true;

}

static bool Parser_TryWhile(Parser* parser)
{

    if (!Parser_Accept(parser, Token_While))
    {
        return false;
    }
    Parser_Next(parser);

    int loop;
    Parser_BeginLoop(parser, &loop);

    Expression test;
    Parser_Expression0(parser, &test, -1);

    Parser_Expect(parser, Token_Do);
    Parser_Next(parser);

    Parser_ConvertToTest(parser, &test);

    Parser_BeginBlock(parser);
    Parser_Block(parser, Token_End);
    Parser_EndBlock(parser);
    Parser_EndLoop(parser, &loop);

    Parser_CloseTest(parser, &test);

    return true;

}

static void Parser_ExpressionList(Parser* parser, int reg, int num)
{
    Expression dst;
    Parser_Expression0(parser, &dst, reg);
    Parser_MoveToRegister(parser, &dst, reg);
}

static bool Parser_TryFor(Parser* parser)
{

    if (!Parser_Accept(parser, Token_For))
    {
        return false;
    }
    Parser_Next(parser);

    Parser_Expect(parser, Token_Name);

    Parser_BeginBlock(parser);

    // TODO: Lua creates these as named local variables. Is that important?
    int internalIndexReg = Parser_AllocateRegister(parser);
    int limitReg         = Parser_AllocateRegister(parser);
    int incrementReg     = Parser_AllocateRegister(parser);

    int externalIndexReg = Parser_AddLocal(parser, parser->lexer->string);
    Parser_Next(parser);

    if (Parser_Accept(parser, '='))
    {

        // Numeric for loop.

        Parser_Next(parser);

        // Start value.
        Expression start;
        Parser_Expression0(parser, &start, internalIndexReg);

        Parser_Expect(parser, ',');
        Parser_Next(parser);

        // End value.
        Expression limit;
        Parser_Expression0(parser, &limit, limitReg);

        // Increment value.
        Expression increment;
        if (Parser_Accept(parser, ','))
        {
            Parser_Next(parser);
            Parser_Expression0(parser, &increment, incrementReg);
        }
        else
        {
            increment.type   = EXPRESSION_NUMBER;
            increment.number = 1.0f;
        }

        Parser_Expect(parser, Token_Do);
        Parser_Next(parser);

        Parser_MoveToRegister(parser, &start, internalIndexReg);
        Parser_MoveToRegister(parser, &limit, limitReg);
        Parser_MoveToRegister(parser, &increment, incrementReg);

        // Reserve space for the forprep instruction since we don't know the skip
        // amount until after we parse the body.
        int loop = Parser_EmitInstruction(parser, 0);

        Parser_Block(parser, Token_End);

        // Close the loop and update the forprep instruction with the correct
        // skip amount.
        int skipAmount = static_cast<int>(Parser_GetInstructionCount(parser) - loop - 1);
        Parser_UpdateInstruction( parser, loop, Parser_EncodeAsBx(Opcode_ForPrep, internalIndexReg, skipAmount) );
        Parser_EmitAsBx(parser, Opcode_ForLoop, internalIndexReg, -skipAmount - 1);
    
    }
    else
    {

        // Generic for loop.

        int numArgs = 1;

        if (Parser_Accept(parser, ','))
        {
            Parser_Next(parser);
            Parser_Expect(parser, Token_Name);
            Parser_AddLocal(parser, parser->lexer->string);
            Parser_Next(parser);
            ++numArgs;
        }

        Parser_Expect(parser, Token_In);
        Parser_Next(parser);

        Parser_ExpressionList(parser, internalIndexReg, 3);

        Parser_Expect(parser, Token_Do);
        Parser_Next(parser);

        // Reserve space for the jmp instruction since we don't know the skip
        // amount until after we parse the body.
        int loop = Parser_EmitInstruction(parser, 0);
        
        Parser_Block(parser, Token_End);

        // Close the loop and update the forprep instruction with the correct
        // skip amount.
        int skipAmount = static_cast<int>(loop - Parser_GetInstructionCount(parser) - 1);
        Parser_UpdateInstruction( parser, loop, Parser_EncodeAsBx(Opcode_Jmp, 0, -skipAmount - 2) );
        Parser_EmitABC(parser, Opcode_TForLoop, internalIndexReg, 0, numArgs);
        Parser_EmitAsBx(parser, Opcode_Jmp, 0, skipAmount);

    }

    Parser_EndBlock(parser);

    return true;


}

static void Parser_Statement(Parser* parser)
{

    if (Parser_TryDo(parser))
    {
        return;
    }
    if (Parser_TryReturn(parser))
    {
        return;
    }
    if (Parser_TryIf(parser))
    {
        return;
    }
    if (Parser_TryLocal(parser))
    {
        return;
    }
    if (Parser_TryWhile(parser))
    {
        return;
    }
    if (Parser_TryFor(parser))
    {
        return;
    }
    if (Parser_TryFunction(parser, false))
    {
        return;
    }
    
    Expression dst;

    // Handle expression statements.
    Parser_Expression0(parser, &dst, -1);
    Parser_ResolveCall(parser, &dst, 0);

    // Handle assignment.
    while (Parser_Accept(parser, '='))
    {

        int reg = -1;

        if (dst.type == EXPRESSION_REGISTER ||
            dst.type == EXPRESSION_LOCAL)
        {
            reg = dst.index;
        }

        Parser_Next(parser);
        Expression arg2;
        Parser_Expression0(parser, &arg2, reg);
        Parser_EmitSet(parser, &dst, &arg2);

    }

    // After each statement we can reuse all of the temporary registers.
    Parser_FreeRegisters(parser);

}

/**
 * endToken specifies the token which is expected to close the block (typically
 * Token_EndOfStream or Token_End.
 */
void Parser_Block(Parser* parser, int endToken)
{

    Function* function = parser->function;

    while (!Parser_Accept(parser, endToken))
    {
        Parser_Statement(parser);
    }
    Parser_Next(parser);

}


Prototype* Parse(lua_State* L, Input* input, const char* name)
{

    Lexer lexer;
    Lexer_Initialize(&lexer, L, input);

    Parser parser;
    Parser_Initialize(&parser, L, &lexer, NULL);

    Parser_Block(&parser, Token_EndOfStream);
    Parser_EmitAB(&parser, Opcode_Return, 0, 1);

    String* source = String_Create(L, name);
    Prototype* prototype = Function_CreatePrototype(parser.L, parser.function, source);

    Parser_Destroy(&parser);

    return prototype;

}