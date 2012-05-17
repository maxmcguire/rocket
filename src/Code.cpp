/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#include "Parser.h"
#include "Lexer.h"
#include "Opcode.h"

#include <malloc.h>

static void Parser_Block(Parser* parser, int endToken);
static void Parser_Statement(Parser* parser);
static void Parser_Expression0(Parser* parser, Expression* dst, int regHint);
static bool Parser_Terminal(Parser* parser, Expression* dst, int regHint);
static void Parser_ExpressionUnary(Parser* parser, Expression* dst, int regHint);

/**
 * Attempts to fold the expression opcode arg and store the result in dst.
 * If the constant can not be folded, the method returns false.
 */
static bool Parser_FoldConstant(Opcode opcode, lua_Number& dst, lua_Number arg)
{
    switch (opcode)
    {
    case Opcode_Unm:
        dst = -arg;
        break;
    default:
        return false;
    }
    return true;
}

/**
 * Attempts to fold the expression arg1 opcode arg2 and store the result in dst.
 * If the constants can not be folded, the method returns false.
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
    case Opcode_Pow:
        dst = luai_numpow(arg1, arg2);
        break;
    case Opcode_Mod:
        dst = luai_nummod(arg1, arg2);
        break;
    default:
        return false;
    }
    return true;
}

static void Parser_ResolveJumpToEnd(Parser* parser, Expression* value)
{
    if (value->type == EXPRESSION_JUMP ||
        value->exitJump[0] != -1 ||
        value->exitJump[1] != -1)
    {
        int reg = Parser_AllocateRegister(parser);
        Parser_MoveToRegister(parser, value, reg); 
    }
}

static void Parser_PrepareForRK(Parser* parser, Expression* value)
{
    Parser_ResolveCall(parser, value, false, 1);
    Parser_ResolveJumpToEnd(parser, value);
    if (value->type == EXPRESSION_NOT || value->type == EXPRESSION_TEMP)
    {
        int reg = Parser_AllocateRegister(parser);
        Parser_MoveToRegister(parser, value, reg);
    }
}

/**
 * regHint specifies the index of the register the result should be stored in if
 * the result will be in a register (or -1 if the caller does not require the
 * result to be a specific location).
 */
static void Parser_EmitArithmetic(Parser* parser, int op, Expression* dst, Expression* arg1, Expression* arg2)
{

    ASSERT(dst != arg1);
    ASSERT(dst != arg2);

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
    default:
        ASSERT(0);
        return;
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

    dst->type  = EXPRESSION_TEMP;
    dst->index = Parser_EmitABC(parser, opcode, 0,  // Register will be assigned later. 
                    Parser_EncodeRK(parser, arg1), 
                    Parser_EncodeRK(parser, arg2));

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

        if (single)
        {
            // If we're only reading a single argument (i.e. not enclosed in
            // parenthesis), then we must parse it as a terminal, otherwise an
            // expression like f 'string' () is parsed incorrectly.
            Parser_Terminal(parser,  &arg, reg);
        }
        else
        {
            Parser_Expression0(parser, &arg, reg);
        }

        if (!single && Parser_Accept(parser, ')'))
        {
            Parser_Unaccept(parser);
            // Allow variable number of arguments for a function call which
            // is the last argument.
            if (Parser_ResolveCall(parser, &arg, false, -1) ||
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

    lua_State* L = parser->L;
    
    Function* function = Function_Create(L);
    PushFunction(L, function);
    
    function->parent = parser->function;

    Parser p;
    Parser_Initialize(&p, parser->L, parser->lexer);
    
    p.function = function;
    function->parser = &p;

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
    dst->index = Parser_AddFunction(parser, function);

    Parser_Destroy(&p);

    ASSERT( (L->stackTop - 1)->object == function );
    Pop(L, 1);

}

static void Parser_EmitSetList(Parser* parser, int reg, int numFields, int listSize)
{
    ASSERT(numFields <= LFIELDS_PER_FLUSH);
    int c = (listSize - 1) / LFIELDS_PER_FLUSH + 1;
    if (c <= 511)
    {
        Parser_EmitABC(parser, Opcode_SetList,  reg, numFields, c);
    }
    else
    {
        Parser_EmitABC(parser, Opcode_SetList,  reg, numFields, 0);
        Parser_EmitInstruction(parser, c);
    }
}

static bool Parser_TryTable(Parser* parser, Expression* dst, int regHint)
{

    if (!Parser_Accept(parser, '{'))
    {
        return false;
    }
    
    // Table constructor.

    Parser_SelectDstRegister(parser, dst, regHint);
    Parser_MoveToStackTop(parser, dst);

    int start = Parser_GetInstructionCount(parser);
    Parser_EmitInstruction(parser, 0);

    int listReg = Parser_AllocateRegister(parser);
    int listSize = 0;
    int hashSize = 0;
    int numFields = 0;

    bool varArg = false;
    bool hasSep = true;

    do
    {

        hasSep = false;

        if (Parser_Accept(parser, '}'))
        {
            break;
        }
        else if (Parser_Accept(parser, '['))
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
                    Parser_PrepareForRK(parser, &value);

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
                
                int reg = listReg + numFields;

                Expression exp;
                Parser_Expression0(parser, &exp, reg);

                hasSep = (Parser_Accept(parser, ';') || Parser_Accept(parser, ','));

                // Check if this is the last expression in the list.
                if (Parser_Accept(parser, '}'))
                {
                    Parser_Unaccept(parser);
                    if ( Parser_ResolveCall(parser, &exp, false, -1) ||
                         Parser_ResolveVarArg(parser, &exp, -1, reg))
                    {
                        varArg = true;
                    }
                }

                Parser_MoveToRegister(parser, &exp, reg);
                if (!varArg)
                {
                    ++listSize;
                    ++numFields;
                    if (numFields == LFIELDS_PER_FLUSH)
                    {
                        // We have a maximum number of fields to set with a single setlist
                        // opcode, so dispatch now.
                        Parser_EmitSetList(parser, dst->index, numFields, listSize);
                        numFields = 0;
                    }
                }

            }

        }
        
        Parser_SetLastRegister(parser, listReg + numFields - 1);

        if (!hasSep)
        {
            hasSep = (Parser_Accept(parser, ';') || Parser_Accept(parser, ','));
        }

    }
    while (hasSep || !Parser_Accept(parser, '}'));

    Instruction inst = Opcode_EncodeABC(Opcode_NewTable, dst->index, listSize, hashSize);
    Parser_UpdateInstruction( parser, start, inst );

    if (numFields > 0 || varArg)
    {
        Parser_EmitSetList(parser, dst->index,  varArg ? 0 : numFields, listSize);
    }

    return true;

}

/**
 * Returns true if the terminal that was parsed can be called as a function.
 */
static bool Parser_Terminal(Parser* parser, Expression* dst, int regHint)
{

    if (Parser_TryTable(parser, dst, regHint))
    {
        return false;
    }

    if (Parser_Accept(parser, TokenType_Name))
    {
        Parser_ResolveName(parser, dst, Parser_GetString(parser));
        return true;
    }
    else if (Parser_Accept(parser, TokenType_String))
    {
        dst->type  = EXPRESSION_CONSTANT;
        dst->index = Parser_AddConstant( parser, Parser_GetString(parser) );
        return false;
    }    
    else if (Parser_Accept(parser, TokenType_Number))
    {
        dst->type   = EXPRESSION_NUMBER;
        dst->number = Parser_GetNumber(parser);
        return false;
    }
    else if (Parser_Accept(parser, TokenType_True) ||
             Parser_Accept(parser, TokenType_False))
    {
        dst->type  = EXPRESSION_BOOLEAN;
        dst->index = (Parser_GetToken(parser) == TokenType_True) ? 1 : 0;
        return false;
    }
    else if (Parser_Accept(parser, TokenType_Nil))
    {
        dst->type = EXPRESSION_NIL;
        return false;
    }
    else if (Parser_Accept(parser, TokenType_Function))
    {
        Parser_Function(parser, dst, false);
        return true;
    }
    else if (Parser_Accept(parser, '('))
	{
		Parser_Expression0(parser, dst, regHint);
        // Placing a function call inside parentheses will adjust the number of
        // return values to 1.
        Parser_ResolveCall(parser, dst, false, 1);
		Parser_Expect(parser, ')');
        return true;
	}
    else if (Parser_Accept(parser, TokenType_Dots))
    {
        // Check that we're in a vararg function.
        if (!parser->function->varArg)
        {
            Parser_Error(parser, "cannot use '...' outside a vararg function");
        }
        dst->type = EXPRESSION_VARARG;
        return false;
    }
    else
    {
        Parser_Error(parser, "expected variable or constant");
    }
    return false;
}

static bool Parser_TryFunctionArguments(Parser* parser, Expression* dst, int regHint)
{

    // Standard function call like (arg1, arg2, ...)
    if (Parser_Accept(parser, '('))
    {
        Parser_MoveToStackTop(parser, dst, regHint);
        dst->type    = EXPRESSION_CALL;
        dst->numArgs = Parser_Arguments(parser);
        return true;
    }

    // Function call with a single string or table argument.
    if (Parser_Accept(parser, TokenType_String) ||
        Parser_Accept(parser, '{'))
    {
        Parser_Unaccept(parser);
        Parser_MoveToStackTop(parser, dst, regHint);
        dst->type    = EXPRESSION_CALL;
        dst->numArgs = Parser_Arguments(parser, true);
        return true;
    }
    
    return false;

}

static bool Parser_TryIndex(Parser* parser, Expression* dst, int regHint)
{

    if (!Parser_Accept(parser, '.') &&
        !Parser_Accept(parser, '[') &&
        !Parser_Accept(parser, ':'))
    {
        return false;
    }


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
    else if (op == ':')
    {

        // Handle Foo:Bar()
  
        Parser_Expect(parser, TokenType_Name);
        Parser_MoveToRegister(parser, dst, -1);

        Expression method;
        method.index = Parser_AddConstant( parser, Parser_GetString(parser) );
        method.type  = EXPRESSION_CONSTANT;
        Parser_MakeRKEncodable(parser, &method);
        int c = Parser_EncodeRK(parser, &method);

        // It's important we allocate the register after making the key encodable
        // so that if it was a constant that was moved into a register, it does
        // not overlap with the registers that will be written by the 'self'
        // instruction.
        int reg = regHint;
        if (reg == -1 || (reg + 1 == c))
        {
            reg = Parser_AllocateRegister(parser);
        }

        Parser_EmitABC(parser, Opcode_Self, reg, dst->index, c);

        // This is a bit of a hack. Since TryFunctionArguments will put the
        // expression onto the top of the stack, we just set it up to be a
        // register since our Self opcode has already setup the stack.
        Parser_SetLastRegister(parser, reg + 1);
        dst->type  = EXPRESSION_REGISTER;
        dst->index = reg + 1;

        if (!Parser_TryFunctionArguments(parser, dst, -1))
        {
            Parser_Error(parser, "function arguments expected");
        }
        
        ASSERT(dst->type == EXPRESSION_CALL);

        // Since we have the extra self paramter, we need to adjust the register
        // where the function is located.
        dst->index = reg;
        if (dst->numArgs != -1)
        {
            // If this isn't a vararg function we need to count the self parameter.
            ++dst->numArgs;
        }

    }

    return true;

}

static void Parser_Expression4(Parser* parser, Expression* dst, int regHint)
{
    if (Parser_Terminal(parser, dst, regHint))
    {
        while (Parser_TryIndex(parser, dst, regHint) ||
               Parser_TryFunctionArguments(parser, dst, regHint))
        {
        }
    }
}

static void Parser_ExpressionPow(Parser* parser, Expression* dst, int regHint)
{
    Parser_Expression4(parser, dst, regHint);
	while (Parser_Accept(parser, '^'))
	{
		int op = Parser_GetToken(parser);

        Parser_PrepareForRK(parser, dst);

        Expression arg1 = *dst;
        Expression arg2;
        Parser_ExpressionUnary(parser, &arg2, -1);
        
        Parser_EmitArithmetic(parser, op, dst, &arg1, &arg2);
	}
}

static void Parser_ExpressionUnary(Parser* parser, Expression* dst, int regHint)
{
    if (Parser_Accept(parser, TokenType_Not))
    {
        Parser_ExpressionUnary(parser, dst, regHint);
        Parser_ResolveJumpToEnd(parser, dst);
        // Don't generate an extra move if the expression we're negating
        // is already stored in a register (but do move to the hint register
        // if it's not).
        if (!Parser_ConvertToRegister(parser, dst))
        {
            Parser_MoveToRegister(parser, dst, regHint);
        }
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
        default:
            ASSERT(0);
            return;
        }

        Parser_ExpressionUnary(parser, dst, regHint);

        // Jumps must be resolved before we can perform constant folding.
        Parser_ResolveJumpToEnd(parser, dst);

        // Perform constant folding.
        if (dst->type == EXPRESSION_NUMBER)
        {
            if (Parser_FoldConstant(opcode, dst->number, dst->number))
            {
                dst->type = EXPRESSION_NUMBER;
                return;
            }
        }

        if (!Parser_ConvertToRegister(parser, dst))
        {
            Parser_MoveToRegister(parser, dst, regHint);
        }
        if (regHint == -1)
        {
            regHint = Parser_AllocateRegister(parser);
        }

        Parser_EmitAB(parser, opcode, regHint, dst->index);
        dst->index = regHint;
        dst->type  = EXPRESSION_REGISTER;

    }
    else
    {
        Parser_ExpressionPow(parser, dst, regHint);
    }
}

static void Parser_Expression3(Parser* parser, Expression* dst, int regHint)
{
    Parser_ExpressionUnary(parser, dst, regHint);
	while (Parser_Accept(parser, '*') ||
           Parser_Accept(parser, '/') ||
           Parser_Accept(parser, '%'))
	{
		int op = Parser_GetToken(parser);

        Parser_PrepareForRK(parser, dst);

        Expression arg1 = *dst;
        Expression arg2;
        Parser_ExpressionUnary(parser, &arg2, -1);

        Parser_EmitArithmetic(parser, op, dst, &arg1, &arg2);
	}
}

static void Parser_Expression2(Parser* parser, Expression* dst, int regHint)
{
    Parser_Expression3(parser, dst, regHint);
	while (Parser_Accept(parser, '+') ||
           Parser_Accept(parser, '-'))
	{
		int op = Parser_GetToken(parser);

        Parser_PrepareForRK(parser, dst);

        Expression arg1 = *dst;
        Expression arg2;
        Parser_Expression3(parser, &arg2, -1);

        Parser_EmitArithmetic(parser, op, dst, &arg1, &arg2);
	}
}

static void Parser_ExpressionConcat(Parser* parser, Expression* dst, int regHint)
{

    Parser_Expression2(parser, dst, regHint);

    if (Parser_Accept(parser, TokenType_Concat))
    {

        Parser_MoveToStackTop(parser, dst);
        int start = dst->index;
        int numOperands = 0;

        do
        {
		    
            int reg = Parser_AllocateRegister(parser);

            Expression arg;
            Parser_Expression2(parser, &arg, reg);

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

static void Parser_Expression1(Parser* parser, Expression* dst, int regHint)
{
    Parser_ExpressionConcat(parser, dst, regHint);
    while ( Parser_Accept(parser, TokenType_Eq) ||
            Parser_Accept(parser, TokenType_Ne) ||
            Parser_Accept(parser, TokenType_Le) ||
            Parser_Accept(parser, TokenType_Ge) ||
            Parser_Accept(parser, '<') || 
            Parser_Accept(parser, '>'))
    {

        int op = Parser_GetToken(parser);

        Expression arg1 = *dst;
        Parser_PrepareForRK(parser, &arg1);
        Parser_MakeRKEncodable(parser, &arg1);

        Expression arg2;
        Parser_ExpressionConcat(parser, &arg2, -1);
        Parser_MakeRKEncodable(parser, &arg2);

        Opcode opcode;

        bool swapArgs = false;
        int  test     = 1;

        switch (op)
        {
        case TokenType_Eq:
            opcode = Opcode_Eq;
            break;
        case TokenType_Ne:
            opcode = Opcode_Eq;
            test = 0;
            break;
        case '<':
            opcode = Opcode_Lt;
            break;
        case TokenType_Le:
            opcode = Opcode_Le;
            break;
        case '>':
            opcode = Opcode_Lt;
            swapArgs = true;
            break;
        case TokenType_Ge:
            opcode = Opcode_Le;
            swapArgs = true;
            break;
        default:
            ASSERT(0);
            return;
        }

        if (swapArgs)
        {
            Parser_EmitABC(parser, opcode, test,
                Parser_EncodeRK(parser, &arg2),
                Parser_EncodeRK(parser, &arg1));
        }
        else
        {
            Parser_EmitABC(parser, opcode, test,
                Parser_EncodeRK(parser, &arg1),
                Parser_EncodeRK(parser, &arg2));
        }

        Expression result;
        Parser_OpenJump(parser, &result);

        *dst = result;

    }
}

static void Parser_ExpressionAnd(Parser* parser, Expression* dst, int regHint)
{ 

    Parser_Expression1(parser, dst, regHint);

    if ( Parser_Accept(parser, TokenType_And) )
    {

        Parser_ConvertToTest(parser, dst, 0, regHint);
        Parser_FinalizeExitJump(parser, dst, 1, regHint);

        Expression arg2;
        Parser_ExpressionAnd(parser, &arg2, regHint);
        Parser_AddExitJump(parser, &arg2, 0, dst->exitJump[0]);

        // If the second argument in a logic expression is a function call, we
        // adjust the number of return values to 1.
        Parser_ResolveCall(parser, &arg2, false, 1);
        *dst = arg2;

    }

}

static void Parser_ExpressionOr(Parser* parser, Expression* dst, int regHint)
{ 

    Parser_ExpressionAnd(parser, dst, regHint);

    if ( Parser_Accept(parser, TokenType_Or) )
    {

        Parser_ConvertToTest(parser, dst, 1, regHint);
        Parser_FinalizeExitJump(parser, dst, 0, regHint);

        Expression arg2;
        Parser_ExpressionOr(parser, &arg2, regHint);
        Parser_AddExitJump(parser, &arg2, 1, dst->exitJump[1]);

        // If the second argument in a logic expression is a function call, we
        // adjust the number of return values to 1.
        Parser_ResolveCall(parser, &arg2, false, 1);
        *dst = arg2;

    }

}

static void Parser_Expression0(Parser* parser, Expression* dst, int regHint)
{
    // Expression parsing is implemented as a recursive descent parser. The
    // farther down the call chain an expression type is parsed, the higher
    // precedence it has (i.e. binds more tightly).
    Parser_ExpressionOr(parser, dst, regHint);
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
        Parser_EmitAB(parser, Opcode_SetUpVal, value->index, dst->index);
    }
    else
    {
        Parser_Error(parser, "illegal assignment");
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
    Parser_ConvertToTest(parser, &test, 0);
    Parser_FinalizeExitJump(parser, &test, 1, -1);

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
        Parser_FinalizeExitJump(parser, &test, 0, -1);

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
        Parser_FinalizeExitJump(parser, &test, 0, -1);
        Parser_Conditional(parser);
    }
    else
    {
        Parser_FinalizeExitJump(parser, &test, 0, -1);
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

/**
 * Parses a non-empty list of a expressions separated by commas. If there is
 * more than one expression, the expressions are put into subsequent registers
 * on the top of the stack. The final expression in the list is stored in dst.
 */
static int Parser_ExpressionList(Parser* parser, Expression* dst)
{
    int firstReg = Parser_GetNumRegisters(parser);
    int numValues = 1;
    Parser_Expression0(parser, dst, -1);
    while (Parser_Accept(parser, ','))
    {
        int index = firstReg + numValues - 1;
        Parser_MoveToRegister(parser, dst, index);
        Parser_SetLastRegister(parser, index);
        Parser_Expression0(parser, dst, index + 1);
        ++numValues;
    }
    return numValues;
}

static bool Parser_TryReturn(Parser* parser)
{

    if (!Parser_Accept(parser, TokenType_Return))
    {
        return false;
    }

    int reg       = 0;
    int numValues = 0;

    if (!Parser_TryEmpty(parser)                      &&
        !Parser_Accept(parser, TokenType_EndOfStream) &&
        !Parser_Accept(parser, TokenType_End)         &&
        !Parser_Accept(parser, TokenType_Else)        &&
        !Parser_Accept(parser, TokenType_ElseIf))
    {

        // Return values will go onto the top of the available stack.
        reg = Parser_GetNumRegisters(parser);

        Expression arg;
        numValues = Parser_ExpressionList(parser, &arg);

        bool tail = (numValues == 1);

        // The final expression can result in a variable number of values.
        if (Parser_ResolveCall(parser,   &arg, tail, -1) ||
            Parser_ResolveVarArg(parser, &arg, -1))
        {
            if (numValues == 1)
            {
                reg = arg.index;
            }
            numValues = -1;
        }
        else if (numValues != 1)
        {
            // The first result is handled specially so that if we're only
            // returning a single argument and it's already in a register we
            // don't need to move it to the top of the stack.
            int index = reg + numValues - 1;
            Parser_MoveToRegister(parser, &arg, index);
        }
        else
        {
            Parser_MoveToRegister(parser, &arg);
            reg = arg.index;
        }

    }
    else
    {
        // If we had an empty block, put back the end token so we can process
        // it at a higher level.
        Parser_Unaccept(parser);
    }

    Parser_EmitAB(parser, Opcode_Return, reg, numValues + 1);
    Parser_FreeRegisters(parser);

    return true;
    
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

    int numRegisters = Parser_GetNumRegisters(parser);

    while (!done && numValues < numVars)
    {

        Expression value;
        Parser_Expression0(parser, &value, -1);

        int regHint = Parser_GetRegisterHint(parser, &dst[numValues]);
        
        // Check if we've reached the end of the list.
        if (!Parser_Accept(parser, ','))
        {
            done = true;
            // If the final expression is a function call or vararg, adjust the number
            // of return values to match the remaining number of variables.
            int numResults = numVars - numValues;
            if (Parser_ResolveCall(parser, &value, false, numResults) ||
                Parser_ResolveVarArg(parser, &value, numResults, regHint))
            {
                ASSERT(value.type == EXPRESSION_REGISTER);
                for (int i = 0; i < numResults; ++i)
                {
                    Expression src = value;
                    Parser_EmitSet(parser, &dst[numValues + i], &src);
                    ++value.index;
                }
                return;
            }
        }

        Parser_EmitSet(parser, &dst[numValues], &value);

        int lastRegister = numRegisters;
        if (dst[numValues].index > numRegisters)
        {
            lastRegister = dst[numValues].index;
        }

        Parser_SetLastRegister(parser, lastRegister);

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

    int maxStackSize = parser->function->maxStackSize;

    int reg     = 0;
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
    else
    {   

        // We have to assign local variables nil if they use registers that
        // we've already used as temporary storage (if they haven't been used
        // as temporaries, then the VM will set them to nil).

        Expression value;
        value.type = EXPRESSION_NIL;

        for (int i = reg; i < maxStackSize && i < reg + numVars; ++i)
        {
            Expression dst;
            dst.type  = EXPRESSION_REGISTER;
            dst.index = i;
            Parser_EmitSet(parser, &dst, &value);
        }        

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

    Parser_ConvertToTest(parser, &test, 0);

    Parser_BeginBlock(parser, true);
    
    // This inner block is used for lexical scoping.
    Parser_BeginBlock(parser, false);
    Parser_Block(parser, TokenType_End);
    Parser_EndBlock(parser);
    
    Parser_EndLoop(parser, &loop);
    Parser_EndBlock(parser);

    Parser_CloseJump(parser, &test);

    return true;

}

static bool Parser_TryRepeat(Parser* parser)
{

    if (!Parser_Accept(parser, TokenType_Repeat))
    {
        return false;
    }

    int loop;
    Parser_BeginLoop(parser, &loop);

    Parser_BeginBlock(parser, true);
    
    // This inner block is used for lexical scoping.
    Parser_BeginBlock(parser, false);
    Parser_Block(parser, TokenType_Until);

    Expression test;
    Parser_Expression0(parser, &test, -1);
    Parser_ConvertToTest(parser, &test, 0);

    if (Parser_GetHasUpValues(parser))
    {

        Parser_FinalizeExitJump(parser, &test, 1, -1);
        Parser_CloseUpValues(parser);

        int skip;
        Parser_BeginSkip(parser, &skip);

        Parser_FinalizeExitJump(parser, &test, 0, -1);
        Parser_EndBlock(parser);
        Parser_EndLoop(parser, &loop);

        Parser_EndSkip(parser, &skip);
    
    }
    else
    {
        Parser_EndBlock(parser);
        Parser_CloseJump(parser, &test, loop);
    }

    Parser_EndBlock(parser);

    return true;

}

static bool Parser_TryFor(Parser* parser)
{

    if (!Parser_Accept(parser, TokenType_For))
    {
        return false;
    }

    lua_State* L = parser->L;

    Parser_Expect(parser, TokenType_Name);

    PushString(L, Parser_GetString(parser));

    Parser_BeginBlock(parser, true);

    // This inner block is used for lexical scoping.
    Parser_BeginBlock(parser, false);

    // For a numeric loop, a=index, b=limit, c=step
    // For a generic loop, a=generator, b=state, c=control
    int internalIndexReg = Parser_AddLocal( parser, String_Create(parser->L, "(for a)") );
    int limitReg         = Parser_AddLocal( parser, String_Create(parser->L, "(for b)") );
    int incrementReg     = Parser_AddLocal( parser, String_Create(parser->L, "(for c)") );

    String* name = (L->stackTop - 1)->string;
    int externalIndexReg = Parser_AddLocal( parser, name );
    Pop(L, 1);

    if (Parser_Accept(parser, '='))
    {
        
        // Numeric for loop.

        // Start value.
        Expression start;
        Parser_Expression0(parser, &start, internalIndexReg);
        Parser_MoveToRegister(parser, &start, internalIndexReg);

        Parser_Expect(parser, ',');

        // End value.
        Expression limit;
        Parser_Expression0(parser, &limit, limitReg);
        Parser_MoveToRegister(parser, &limit, limitReg);

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
        Parser_MoveToRegister(parser, &increment, incrementReg);
        
        Parser_CommitLocals( parser );
        Parser_Expect(parser, TokenType_Do);

        // Reserve space for the forprep instruction since we don't know the skip
        // amount until after we parse the body.
        int loop = Parser_EmitInstruction(parser, 0);

        Parser_Block(parser, TokenType_End);
        Parser_EndBlock(parser);

        // Close the loop and update the forprep instruction with the correct
        // skip amount.
        int skipAmount = static_cast<int>(Parser_GetInstructionCount(parser) - loop - 1);
        Parser_UpdateInstruction( parser, loop, Opcode_EncodeAsBx(Opcode_ForPrep, internalIndexReg, skipAmount) );
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
        Parser_EndBlock(parser);

        // Close the loop and update the forprep instruction with the correct
        // skip amount.
        int skipAmount = static_cast<int>(loop - Parser_GetInstructionCount(parser) - 1);
        Parser_UpdateInstruction( parser, loop, Opcode_EncodeAsBx(Opcode_Jmp, 0, -skipAmount - 2) );
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

static int Parser_MoveToFreshRegister(Parser* parser, int oldReg)
{
    int newReg = Parser_AllocateRegister(parser);
    Parser_EmitAB(parser, Opcode_Move, newReg, oldReg);
    return newReg;
}

static int Parser_AssignmentList(Parser* parser, int numExps = 1)
{

    Expression dst;
    Parser_Expression0(parser, &dst, -1);

    // If this is a function call, then it's a complete expression.
    if (numExps == 1 && Parser_ResolveCall(parser, &dst, false,  0))
    {
        return -1;
    }

    int reg;
    Expression exp;

    if (Parser_Accept(parser, ','))
    {

        // If the target is a table and we reassign one of the registers that
        // is being referenced by oru target expression, the assignment will not
        // work properly. So, copy to fresh registers.
        if (dst.type == EXPRESSION_TABLE)
        {
            if (dst.keyType == EXPRESSION_REGISTER)
            {
                dst.key = Parser_MoveToFreshRegister(parser, dst.key);
            }
            dst.index = Parser_MoveToFreshRegister(parser, dst.index);
        }

        reg = Parser_AssignmentList(parser, numExps + 1);
        exp.type  = EXPRESSION_REGISTER;
        exp.index = reg + numExps - 1;

    }
    else
    {

        Parser_Expect(parser, '=');
        
        reg = Parser_GetNumRegisters(parser);
        int numValues = Parser_ExpressionList(parser, &exp);

        // If the final expression can generate a variable number of results,
        // adjust the number of results it generates based on how many we need.
        int numResults = numExps - numValues + 1;
        if (numResults < 0)
        {
            numResults = 0;
        }
        if (Parser_ResolveCall(parser, &exp, false, numResults) ||
            Parser_ResolveVarArg(parser, &exp, numResults))
        {
            if (numValues == 1)
            {
                reg = exp.index;
            }
            if (numResults > 0)
            {
                // Adjust to the last register.
                ASSERT(exp.type == EXPRESSION_REGISTER);
                exp.index += numResults - 1;
                numValues = numExps;
            }
        }

        // Not enough values were supplied, pad out with nils.
        if (numValues < numExps)
        {
            Parser_MoveToRegister(parser, &exp, reg + numValues - 1);
            Parser_EmitAB(parser, Opcode_LoadNil, reg + numValues, reg + numExps - 1);
            exp.type  = EXPRESSION_REGISTER;
            exp.index = reg + numExps - 1;
            numValues = numExps;
        }

        // Too many values were supplied.
        if (numValues > numExps)
        {
            exp.type  = EXPRESSION_REGISTER;
            exp.index = reg + numExps - 1;
            numValues = numExps;
        }

    }

    Parser_EmitSet(parser, &dst, &exp);
    return reg;

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

    Parser_AssignmentList(parser);

    // After each statement we can reuse all of the temporary registers.
    Parser_FreeRegisters(parser);

}

/**
 * endToken specifies the token which is expected to close the block (typically
 * TokenType_EndOfStream or TokenType_End.
 */
void Parser_Block(Parser* parser, int endToken)
{
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
    Parser_Initialize(&parser, L, &lexer);

    // Keep the function on the stack so that it's not garbage collected.
    Function* function = Function_Create(L);
    PushFunction(L, function);

    // Top level block accepts a variable number of arguments.
    function->varArg = true;

    parser.function = function;
    function->parser = &parser;

    Parser_Block(&parser, TokenType_EndOfStream);
    Parser_EmitAB(&parser, Opcode_Return, 0, 1);

    String* source = String_Create(L, name);
    Prototype* prototype = Function_CreatePrototype(parser.L, parser.function, source);

    Parser_Destroy(&parser);
    Lexer_Destroy(&lexer);

    ASSERT( (L->stackTop - 1)->object == function );
    Pop(L, 1);

    return prototype;

}