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
#include <malloc.h>

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
    case '%':
        opcode = Opcode_Mod;
        break;
    case '^':
        opcode = Opcode_Pow;
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

    Parser_MakeRKEncodable(parser, arg1);
    Parser_MakeRKEncodable(parser, arg2);

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

    Parser_MakeRKEncodable(parser, arg1);
    Parser_MakeRKEncodable(parser, arg2);

    Opcode opcode;
    int    value;

    if (op == TokenType_Eq)
    {
        opcode = Opcode_Eq;
        value  = 0;
    }
    else if (op == TokenType_Ne)
    {
        opcode = Opcode_Eq;
        value  = 1;
    }
    else if (op == '<')
    {
        opcode = Opcode_Lt;
        value = 0;
    }
    else if (op == TokenType_Le)
    {
        opcode = Opcode_Le;
        value = 0;
    }
    else if (op == '>')
    {
        opcode = Opcode_Le;
        value = 1;
    }
    else if (op == TokenType_Ge)
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

/**
 * Atempts to parse the empty statement (a semicolon).
 */
static bool Parser_TryEmpty(Parser* parser)
{
    if (Parser_Accept(parser, ';'))
    {
        return true;
    }
    return false;
}

/**
 * If single is true, only a single argument will be parsed (used for special syntax
 * forms where a string or table argument is specified for a function).
 */
static int Parser_Arguments(Parser* parser, bool single = false)
{
    int  numArgs = 0;
    bool varArg  = false;

    while (1)
    {

        if (!single && Parser_Accept(parser, ')'))
        {
            break;
        }
        if (numArgs > 0)
        {
            Parser_Expect(parser, ',');
        }

        int reg = Parser_AllocateRegister(parser);

        Expression arg;
        Parser_Expression0(parser, &arg, reg);

        if (!single && Parser_Accept(parser, ')'))
        {
            Parser_Unaccept(parser);
            // Allow variable number of arguments for a function call which
            // is the last argument.
            if (Parser_ResolveCall(parser, &arg, -1) ||
                Parser_ResolveVarArg(parser, &arg, -1, reg))
            {
                varArg = true;
            }
        }

        // Make sure the result is stored in the register for the argument and
        // free up any temporary registers we used.
        Parser_MoveToRegister(parser, &arg, reg);
        Parser_SetLastRegister(parser, reg);

        ++numArgs;

        if (single)
        {
            break;
        }

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
        Parser_CommitLocals( &p );
    }

    // Parse the arguments.
    Parser_Expect(parser, '(');

    while (!Parser_Accept(parser, ')'))
    {
        if (p.function->numParams > 0)
        {
            Parser_Expect(parser, ',');
        }
        // Add the parameter as a local, since they have the same semantics.
        if (Parser_Accept(parser, TokenType_Dots))
        {
            Parser_AddLocal( &p, String_Create(p.L, "...") );
            Parser_CommitLocals( &p );
            Parser_Expect( &p, ')' );
            p.function->varArg = true;
            break;
        }
        else
        {
            Parser_Expect( &p, TokenType_Name );
            Parser_AddLocal( &p, Parser_GetString(parser) );
            Parser_CommitLocals( &p );
            ++p.function->numParams;
        }
    }

    if (method)
    {
        ++p.function->numParams;
    }

    // Parse the body.
    Parser_Block(&p, TokenType_End);
    Parser_EmitAB(&p, Opcode_Return, 0, 1);

    // Store in the result parent function.
    dst->type  = EXPRESSION_FUNCTION;
    dst->index = Parser_AddFunction(parser, p.function);

    p.function->parser = NULL;

}

static bool Parser_TryTable(Parser* parser, Expression* dst, int regHint)
{

    if (!Parser_Accept(parser, '{'))
    {
        return false;
    }
    
    // Table constructor.

    Parser_SelectDstRegister(parser, dst, regHint);
    int start = Parser_GetInstructionCount(parser);
    Parser_EmitInstruction(parser, 0);

    int listReg = Parser_AllocateRegister(parser);
    int listSize = 0;
    int hashSize = 0;

    int num = 0;
    bool varArg = false;

    while (!Parser_Accept(parser, '}'))
    {

        if (num > 0)
        {
            Parser_Expect(parser, ',', ';');
            // Allow the table constructor to end with a separator token for
            // convenience to the programmer.
            if (Parser_Accept(parser, '}'))
            {
                break;
            }
        }

        if (Parser_Accept(parser, '['))
        {

            // Handle the form: [x] = y
            
            Expression key;
            Parser_Expression0(parser, &key, -1);
            Parser_MakeRKEncodable(parser, &key);
            Parser_Expect(parser, ']');

            Parser_Expect(parser, '=');

            Expression value;
            Parser_Expression0(parser, &value, -1);
            Parser_MakeRKEncodable(parser, &value);

            Parser_EmitABC(parser, Opcode_SetTable, dst->index,
                Parser_EncodeRK(parser, &key),
                Parser_EncodeRK(parser, &value));

            ++hashSize;

        }
        else
        {

            bool accepted = false;

            if (Parser_Accept(parser, TokenType_Name))
            {

                // TODO: Change to Parser_UnacceptToken

                Token token;
                Lexer_CaptureToken(parser->lexer, &token);

                Expression key;
                key.index = Parser_AddConstant(parser, Parser_GetString(parser));
                key.type = EXPRESSION_CONSTANT;

                if (Parser_Accept(parser, '='))
                {

                    // Handle the form: x = y

                    Expression value;
                    Parser_Expression0(parser, &value, -1);

                    Parser_MakeRKEncodable(parser, &value);
                    Parser_MakeRKEncodable(parser, &key);

                    Parser_EmitABC(parser, Opcode_SetTable, dst->index,
                        Parser_EncodeRK(parser, &key),
                        Parser_EncodeRK(parser, &value));

                    accepted = true;
                    ++hashSize;

                }
                else
                {
                    Lexer_RestoreTokens(parser->lexer, &token, 1);
                }

            }

            if (!accepted)
            {

                Expression exp;
                Parser_Expression0(parser, &exp, listReg + listSize);

                // Check if this is the last expression in the list.
                if (Parser_Accept(parser, '}'))
                {
                    Parser_Unaccept(parser);
                    if ( Parser_ResolveCall(parser, &exp, -1) ||
                         Parser_ResolveVarArg(parser, &exp, -1, listReg + listSize))
                    {
                        varArg = true;
                    }
                }

                Parser_MoveToRegister(parser, &exp, listReg + listSize);
                if (!varArg)
                {
                    ++listSize;
                }

            }

        }
        ++num;
        Parser_SetLastRegister(parser, listReg + listSize - 1);

    }
    
    Instruction inst = Parser_EncodeABC(Opcode_NewTable, dst->index, listSize, hashSize);
    Parser_UpdateInstruction( parser, start, inst );

    if (listSize > 0)
    {
        Parser_EmitABC(parser, Opcode_SetList,  dst->index, varArg ? 0 : listSize, 1);
    }

    return true;

}

static void Parser_Expression5(Parser* parser, Expression* dst, int regHint)
{

    if (Parser_TryTable(parser, dst, regHint))
    {
        return;
    }

    if (Parser_Accept(parser, TokenType_Name))
    {
        Parser_ResolveName(parser, dst, Parser_GetString(parser));
    }
    else if (Parser_Accept(parser, TokenType_String))
    {
        dst->type  = EXPRESSION_CONSTANT;
        dst->index = Parser_AddConstant( parser, Parser_GetString(parser) );
    }    
    else if (Parser_Accept(parser, TokenType_Number))
    {
        dst->type   = EXPRESSION_NUMBER;
        dst->number = Parser_GetNumber(parser);
    }
    else if (Parser_Accept(parser, TokenType_True) ||
             Parser_Accept(parser, TokenType_False))
    {
        dst->type  = EXPRESSION_BOOLEAN;
        dst->index = (Parser_GetToken(parser) == TokenType_True) ? 1 : 0;
    }
    else if (Parser_Accept(parser, TokenType_Nil))
    {
        dst->type = EXPRESSION_NIL;
    }
    else if (Parser_Accept(parser, TokenType_Function))
    {
        Parser_Function(parser, dst, false);
    }
    else if (Parser_Accept(parser, '('))
	{
		Parser_Expression0(parser, dst, regHint);
		Parser_Expect(parser, ')');
	}
    else if (Parser_Accept(parser, TokenType_Dots))
    {
        // Check that we're in a vararg function.
        if (!parser->function->varArg)
        {
            Lexer_Error(parser->lexer, "cannot use '...' outside a vararg function");
        }
        dst->type = EXPRESSION_VARARG;
    }
    else
    {
        Lexer_Error(parser->lexer, "expected variable or constant");
    }
}

static bool Parser_TryFunctionArguments(Parser* parser, Expression* dst, int regHint)
{

    // Standard function call like (arg1, arg2, ...)
    if (Parser_Accept(parser, '('))
    {
        Parser_MoveToRegister(parser, dst, regHint);
        Parser_MoveToStackTop(parser, dst);
        dst->type    = EXPRESSION_CALL;
        dst->numArgs = Parser_Arguments(parser);
        return true;
    }

    // Function call with a single string or table argument.
    if (Parser_Accept(parser, TokenType_String) ||
        Parser_Accept(parser, '{'))
    {
        Parser_Unaccept(parser);
        Parser_MoveToRegister(parser, dst, regHint);
        Parser_MoveToStackTop(parser, dst);
        dst->type    = EXPRESSION_CALL;
        dst->numArgs = Parser_Arguments(parser, true);
        return true;
    }
    
    return false;

}

static void Parser_Expression4(Parser* parser, Expression* dst, int regHint)
{
    
    Parser_Expression5(parser, dst, regHint);

    bool done = false;

    while (!done)
    {

        if (Parser_TryFunctionArguments(parser, dst, regHint))
        {
        }
        else if (Parser_Accept(parser, '.') || Parser_Accept(parser, '['))
        {

            int op = Parser_GetToken(parser);

            if (op == '.')
            {

                // Handle table indexing (object form).
                
                Parser_Expect(parser, TokenType_Name);

                Parser_MoveToRegister(parser, dst, regHint);
                dst->type = EXPRESSION_TABLE;

                dst->keyType = EXPRESSION_CONSTANT;
                dst->key     = Parser_AddConstant( parser, Parser_GetString(parser) );

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

            }

        }
        else if (Parser_Accept(parser, ':'))
        {

            // Handle Foo:Bar()

            Parser_Expect(parser, TokenType_Name);
            Parser_MoveToRegister(parser, dst, -1);

            int reg = Parser_AllocateRegister(parser);

            Expression method;
            method.index = Parser_AddConstant( parser, Parser_GetString(parser) );
            method.type  = EXPRESSION_CONSTANT;
            Parser_MakeRKEncodable(parser, &method);

            Parser_EmitABC(parser, Opcode_Self, reg, dst->index, Parser_EncodeRK(parser, &method));

            // This is a bit of a hack. Since TryFunctionArguments will put the
            // expression onto the top of the stack, we just set it up to be a
            // register since our Self opcode has already setup the stack.
            dst->type  = EXPRESSION_REGISTER;
            dst->index = Parser_AllocateRegister(parser);

            if (!Parser_TryFunctionArguments(parser, dst, -1))
            {
                Lexer_Error(parser->lexer, "function arguments expected");
            }
            
            assert(dst->type == EXPRESSION_CALL);

            // Since we have the extra self paramter, we need to adjust the register
            // where the function is located.
            dst->index = reg;
            if (dst->numArgs != -1)
            {
                // If this isn't a vararg function we need to count the self parameter.
                ++dst->numArgs;
            }

        }
        else
        {
            done = true;
        }

    }

}

static void Parser_ExpressionUnary(Parser* parser, Expression* dst, int regHint)
{
    if (Parser_Accept(parser, TokenType_Not))
    {
        Parser_Expression0(parser, dst, regHint);
        Parser_MoveToRegister(parser, dst, regHint);
        dst->type = EXPRESSION_NOT;
    }
    else if (Parser_Accept(parser, '#') || Parser_Accept(parser, '-'))
    {
        // Handle unary minus and length.

        Opcode opcode;
        switch (Parser_GetToken(parser))
        {
        case '#':
            opcode = Opcode_Len;
            break;
        case '-':
            opcode = Opcode_Unm;
            break;
        }

        Parser_Expression0(parser, dst, regHint);
        if (!Parser_ConvertToRegister(parser, dst))
        {
            Parser_MoveToRegister(parser, dst, regHint);
        }
        if (regHint == -1)
        {
            regHint = dst->index;
        }

        Parser_EmitAB(parser, opcode, regHint, dst->index);
        dst->index = regHint;
        dst->type  = EXPRESSION_REGISTER;
    }
    else
    {
        Parser_Expression4(parser, dst, regHint);
    }
}

static void Parser_ExpressionPow(Parser* parser, Expression* dst, int regHint)
{
    Parser_ExpressionUnary(parser, dst, regHint);
	while (Parser_Accept(parser, '^'))
	{
		int op = Parser_GetToken(parser);

        Expression arg1 = *dst;
        Parser_ResolveCall(parser, &arg1, 1);

        Expression arg2;
        Parser_ExpressionUnary(parser, &arg2, -1);
        Parser_EmitArithmetic(parser, op, dst, regHint, &arg1, &arg2);
	}
}

static void Parser_Expression3(Parser* parser, Expression* dst, int regHint)
{
    Parser_ExpressionPow(parser, dst, regHint);
	while (Parser_Accept(parser, '*') ||
           Parser_Accept(parser, '/') ||
           Parser_Accept(parser, '%'))
	{
		int op = Parser_GetToken(parser);

        Expression arg1 = *dst;
        Parser_ResolveCall(parser, &arg1, 1);

        Expression arg2;
        Parser_ExpressionPow(parser, &arg2, -1);
        Parser_EmitArithmetic(parser, op, dst, regHint, &arg1, &arg2);
	}
}

static void Parser_Expression2(Parser* parser, Expression* dst, int regHint)
{
    Parser_Expression3(parser, dst, regHint);
	while (Parser_Accept(parser, '+') ||
           Parser_Accept(parser, '-'))
	{
		int op = Parser_GetToken(parser);

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
    while ( Parser_Accept(parser, TokenType_Eq) ||
            Parser_Accept(parser, TokenType_Ne) ||
            Parser_Accept(parser, TokenType_Le) ||
            Parser_Accept(parser, TokenType_Ge) ||
            Parser_Accept(parser, '<') || 
            Parser_Accept(parser, '>'))
    {
        int op = Parser_GetToken(parser);

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

    while ( Parser_Accept(parser, TokenType_And) ||
            Parser_Accept(parser, TokenType_Or) )
    {
        
        int op = Parser_GetToken(parser);

        int cond = op == TokenType_Or ? 1 : 0;

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

    if (Parser_Accept(parser, TokenType_Concat))
    {

        Parser_MoveToStackTop(parser, dst);
        int start = dst->index;
        int numOperands = 0;

        do
        {
		    
            int reg = Parser_AllocateRegister(parser);

            Expression arg;
            Parser_ExpressionLogic(parser, &arg, reg);

            // Make sure the result is stored in our consecutive register and
            // free up any temporary registers we used.
            Parser_MoveToRegister(parser, &arg, reg);
            Parser_SetLastRegister(parser, reg);
             
            ++numOperands;

        }
        while (Parser_Accept(parser, TokenType_Concat));

        Parser_EmitABC(parser, Opcode_Concat, dst->index,
            start, start + numOperands);

    }

}

/**
 * Generates instructions to perform the operation: dst = value
 */
static void Parser_EmitSet(Parser* parser, const Expression* dst, Expression* value)
{
    if (dst->type == EXPRESSION_REGISTER ||
        dst->type == EXPRESSION_LOCAL)
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

        Expression key;
        key.type  = dst->keyType;
        key.index = dst->key;

        Parser_MakeRKEncodable(parser, value);
        Parser_MakeRKEncodable(parser, &key);

        Parser_EmitABC(parser, Opcode_SetTable, dst->index,
            Parser_EncodeRK(parser, &key),
            Parser_EncodeRK(parser, value));

    }   
    else if (dst->type == EXPRESSION_UPVALUE)
    {
        Parser_MoveToRegister(parser, value);
        Parser_EmitAB(parser, Opcode_SetUpVal, dst->index, value->index);
    }
    else
    {
        Lexer_Error(parser->lexer, "illegal assignment");
    }
}

/**
 * Parses the conditional part after an if or an elseif token.
 */
static void Parser_Conditional(Parser* parser)
{

    // Parse the condition to test for the if statement.
    Expression test;
    Parser_Expression0(parser, &test, -1);

    Parser_Expect(parser, TokenType_Then);

    // TODO: Peform "constant folding" for the test.
    Parser_ConvertToTest(parser, &test);
    Parser_BeginBlock(parser, false);

    // Parse the "if" part of the conditional.
    while (!Parser_Accept(parser, TokenType_End) &&
           !Parser_Accept(parser, TokenType_Else) &&
           !Parser_Accept(parser, TokenType_ElseIf))
    {
        Parser_Statement(parser);
    }

    Parser_EndBlock(parser);

    int type = Parser_GetToken(parser);

    if (type == TokenType_Else)
    {

        int elseJump;
        Parser_BeginBlock(parser, false);
        Parser_BeginSkip(parser, &elseJump);
        Parser_CloseTest(parser, &test);

        // Parse the "else" part of the conditional.
        while (!Parser_Accept(parser, TokenType_End))
        {
            Parser_Statement(parser);
        }

        Parser_EndSkip(parser, &elseJump);
        Parser_EndBlock(parser);

    }
    else if (type == TokenType_ElseIf)
    {
        Parser_Conditional(parser);
    }
    else
    {
        Parser_CloseTest(parser, &test);
    }

}

static bool Parser_TryIf(Parser* parser)
{
    if (Parser_Accept(parser, TokenType_If))
    {
        Parser_Conditional(parser);
        return true;
    }
    return false;
}

static bool Parser_TryReturn(Parser* parser)
{
    if (Parser_Accept(parser, TokenType_Return))
    {
        
        int numResults = 0;
        int reg        = -1;

        Expression arg;

        while (!Parser_Accept(parser, TokenType_End) &&
               !Parser_Accept(parser, TokenType_Else) &&
               !Parser_Accept(parser, TokenType_ElseIf))
        {

            if (Parser_TryEmpty(parser))
            {
                // Lua allows a single empty statement to follow a return statement. 
                if (Parser_Accept(parser, TokenType_End) ||
                    Parser_Accept(parser, TokenType_Else) ||
                    Parser_Accept(parser, TokenType_ElseIf))
                {
                    break;
                }
                Lexer_Error(parser->lexer, "unexpected token");
            }

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

        // Put the end token back so that we can process is elsewhere.
        Parser_Unaccept(parser);

        if (reg == -1)
        {
            if (numResults > 0)
            {
                Parser_MoveToRegister(parser, &arg);
                reg = arg.index;
            }
            else
            {
                reg = 0;
            }
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

    if (!Parser_Accept(parser, TokenType_Function))
    {
        return false;
    }
        
    Parser_Expect(parser, TokenType_Name);

    Expression dst;

    // Methods are functions which have an implicit self parameter.
    bool method = false;

    if (local)
    {
        dst.index = Parser_AddLocal( parser, Parser_GetString(parser)  );
        dst.type  = EXPRESSION_LOCAL;
        Parser_CommitLocals( parser );
    }
    else
    {

        Parser_ResolveName( parser, &dst, Parser_GetString(parser) );

        // Check if we are of the form function A.B.C:D()
        while (Parser_Accept(parser, '.') ||
               Parser_Accept(parser, ':'))
        {

            int token = Parser_GetToken(parser);

            Parser_Expect(parser, TokenType_Name);

            Parser_MoveToRegister(parser, &dst);
            dst.type = EXPRESSION_TABLE;

            dst.keyType = EXPRESSION_CONSTANT;
            dst.key     = Parser_AddConstant( parser, Parser_GetString(parser) );

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

/**
 * Parses a list of expressions exp1, exp2, exp3, etc. and assigns the values
 * to the destination expressions specified by the dst array (of size numVars).
 * If more values are specified than numVars, the additional expressions will
 * be parsed but their values will be discarded. If fewer expressions are
 * supplied than numVars, the renamining dst expressions will be set to nil.
 */
static void Parser_AssignExpressionList(Parser* parser, const Expression dst[], int numVars)
{

    int  numValues = 0;
    bool done = false;

    while (!done && numValues < numVars)
    {

        int regHint = Parser_GetRegisterHint(parser, &dst[numValues]);
        
        Expression value;
        Parser_Expression0(parser, &value, regHint);

        // Check if we've reached the end of the list.
        if (!Parser_Accept(parser, ','))
        {
            done = true;
            // If the final expression is a function call or vararg, adjust the number
            // of return values to match the remaining number of variables.
            int numResults = numVars - numValues;
            if (Parser_ResolveCall(parser, &value, numResults) ||
                Parser_ResolveVarArg(parser, &value, numResults, regHint))
            {
                assert(value.type == EXPRESSION_REGISTER);
                for (int i = 0; i < numResults; ++i)
                {
                    Parser_EmitSet(parser, &dst[numValues + i], &value);
                    ++value.index;
                }
                return;
            }
        }

        Parser_EmitSet(parser, &dst[numValues], &value);

        // Make sure we don't reuse the registers occupied by locals that
        // we've assigned.
        if (dst[numValues].type == EXPRESSION_LOCAL)
        {
            int reg = dst[numValues].index;
            if (reg <= Parser_GetNumRegisters(parser))
            {
                Parser_SetLastRegister(parser, reg);
            }
        }

        ++numValues;

    }

    // If enough values weren't specified, substitute nils.
    while (numValues < numVars)
    {
        Expression value;
        value.type = EXPRESSION_NIL;
        Parser_EmitSet(parser, &dst[numValues], &value);
        ++numValues;
    }

    if (!done)
    {
        do
        {
            Expression value;
            Parser_Expression0(parser, &value, -1);
            // Move to a register so that we get any side effects.
            Parser_MoveToRegister(parser, &value);
        }
        while (Parser_Accept(parser, ','));
    }

}

static void Parser_AssignLocals(Parser* parser, int firstLocal, int numLocals)
{
    Expression* dst = (Expression*)alloca(numLocals * sizeof(Expression));
    for (int i = 0; i < numLocals; ++i)
    {
        dst[i].type  = EXPRESSION_LOCAL;
        dst[i].index = firstLocal + i;
    }
    Parser_AssignExpressionList(parser, dst, numLocals);
}

static bool Parser_TryLocal(Parser* parser)
{
    
    if (!Parser_Accept(parser, TokenType_Local))
    {
        return false;
    }

    if (Parser_TryFunction(parser, true))
    {
        return true;
    }

    int reg;
    int numVars = 0;
    do
    {
        Parser_Expect(parser, TokenType_Name);
        int local = Parser_AddLocal(parser, Parser_GetString(parser));
        if (numVars == 0)
        {
            // Store off the first register where the locals will be stored.
            reg = local;
        }
        ++numVars;
    }
    while (Parser_Accept(parser, ','));

    if (Parser_Accept(parser, '='))
    {
        Parser_AssignLocals(parser, reg, numVars);
    }
    Parser_CommitLocals(parser);
    
    return true;

}

static bool Parser_TryDo(Parser* parser)
{

    if (!Parser_Accept(parser, TokenType_Do))
    {
        return false;
    }

    Parser_BeginBlock(parser, false);
    Parser_Block(parser, TokenType_End);
    Parser_EndBlock(parser);

    return true;

}

static bool Parser_TryWhile(Parser* parser)
{

    if (!Parser_Accept(parser, TokenType_While))
    {
        return false;
    }

    int loop;
    Parser_BeginLoop(parser, &loop);

    Expression test;
    Parser_Expression0(parser, &test, -1);

    Parser_Expect(parser, TokenType_Do);

    Parser_ConvertToTest(parser, &test);

    Parser_BeginBlock(parser, true);
    Parser_Block(parser, TokenType_End);
    Parser_EndBlock(parser);
    Parser_EndLoop(parser, &loop);

    Parser_CloseTest(parser, &test);

    return true;

}

static bool Parser_TryRepeat(Parser* parser)
{

    if (!Parser_Accept(parser, TokenType_Repeat))
    {
        return false;
    }

    int loop = Parser_GetInstructionCount(parser);

    Parser_BeginBlock(parser, true);
    Parser_Block(parser, TokenType_Until);

    Expression test;
    Parser_Expression0(parser, &test, -1);
    Parser_ConvertToTest(parser, &test);
    Parser_CloseTest(parser, &test, loop);

    Parser_EndBlock(parser);

    return true;

}

static bool Parser_TryFor(Parser* parser)
{

    if (!Parser_Accept(parser, TokenType_For))
    {
        return false;
    }

    Parser_Expect(parser, TokenType_Name);

    Parser_BeginBlock(parser, true);

    // For a numeric loop, a=index, b=limit, c=step
    // For a generic loop, a=generator, b=state, c=control
    int internalIndexReg = Parser_AddLocal( parser, String_Create(parser->L, "(for a)") );
    int limitReg         = Parser_AddLocal( parser, String_Create(parser->L, "(for b)") );
    int incrementReg     = Parser_AddLocal( parser, String_Create(parser->L, "(for c)") );

    int externalIndexReg = Parser_AddLocal( parser, Parser_GetString(parser) );

    if (Parser_Accept(parser, '='))
    {
        
        // Numeric for loop.
        
        Parser_CommitLocals( parser );

        // Start value.
        Expression start;
        Parser_Expression0(parser, &start, internalIndexReg);

        Parser_Expect(parser, ',');

        // End value.
        Expression limit;
        Parser_Expression0(parser, &limit, limitReg);

        // Increment value.
        Expression increment;
        if (Parser_Accept(parser, ','))
        {
            Parser_Expression0(parser, &increment, incrementReg);
        }
        else
        {
            increment.type   = EXPRESSION_NUMBER;
            increment.number = 1.0f;
        }

        Parser_Expect(parser, TokenType_Do);

        Parser_MoveToRegister(parser, &start, internalIndexReg);
        Parser_MoveToRegister(parser, &limit, limitReg);
        Parser_MoveToRegister(parser, &increment, incrementReg);

        // Reserve space for the forprep instruction since we don't know the skip
        // amount until after we parse the body.
        int loop = Parser_EmitInstruction(parser, 0);

        Parser_Block(parser, TokenType_End);

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
        while (Parser_Accept(parser, ','))
        {
            Parser_Expect(parser, TokenType_Name);
            Parser_AddLocal( parser, Parser_GetString(parser) );
            ++numArgs;
        }

        Parser_Expect(parser, TokenType_In);

        Parser_AssignLocals(parser, internalIndexReg, 3);
        Parser_CommitLocals( parser );

        Parser_Expect(parser, TokenType_Do);

        // Reserve space for the jmp instruction since we don't know the skip
        // amount until after we parse the body.
        int loop = Parser_EmitInstruction(parser, 0);
        
        Parser_Block(parser, TokenType_End);

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

static bool Parser_TryBreak(Parser* parser)
{
    if (!Parser_Accept(parser, TokenType_Break))
    {
        return false;
    }
    Parser_BreakBlock(parser);
    // Note, unlike in vanialla Lua we don't require break to be the final
    // statement in a block (since it just makes the parser more complex).
    return true;
}

static void Parser_Statement(Parser* parser)
{

    if (Parser_TryEmpty(parser))
    {
        return;
    }
    if (Parser_TryDo(parser))
    {
        return;
    }
    if (Parser_TryReturn(parser))
    {
        return;
    }
    if (Parser_TryBreak(parser))
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
    if (Parser_TryRepeat(parser))
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
    
    // Handle expression statements.

    Expression dst[LUA_MAXASSIGNS];
    Parser_Expression0(parser, &dst[0], -1);
    
    if (!Parser_ResolveCall(parser, &dst[0], 0))
    {
        
        int numVars = 1;
        while (numVars < LUA_MAXASSIGNS && Parser_Accept(parser, ','))
        {
            Parser_Expression0(parser, &dst[numVars], -1);
            ++numVars;
        }

        if (numVars == LUA_MAXASSIGNS)
        {
            // Because we use a fixed sized array, we impose a limit on the
            // maximum number of assignments for a single statement. This
            // shouldn't be a factor for most code, but if it is the statement
            // can easily be broken into two statements.
            Lexer_Error(parser->lexer, "maximum number of assignments for a single statment (%d) reached",
                LUA_MAXASSIGNS);
        }

        Parser_Expect(parser, '=');
        Parser_AssignExpressionList(parser, dst, numVars);

    }

    // After each statement we can reuse all of the temporary registers.
    Parser_FreeRegisters(parser);

}

/**
 * endToken specifies the token which is expected to close the block (typically
 * TokenType_EndOfStream or TokenType_End.
 */
void Parser_Block(Parser* parser, int endToken)
{
    Function* function = parser->function;
    while (!Parser_Accept(parser, endToken))
    {
        Parser_Statement(parser);
    }
}

Prototype* Parse(lua_State* L, Input* input, const char* name)
{

    Lexer lexer;
    Lexer_Initialize(&lexer, L, input);

    Parser parser;
    Parser_Initialize(&parser, L, &lexer, NULL);

    Parser_Block(&parser, TokenType_EndOfStream);
    Parser_EmitAB(&parser, Opcode_Return, 0, 1);

    String* source = String_Create(L, name);
    Prototype* prototype = Function_CreatePrototype(parser.L, parser.function, source);

    Parser_Destroy(&parser);

    return prototype;

}