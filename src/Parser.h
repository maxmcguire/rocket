/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#ifndef ROCKETVM_PARSER_H
#define ROCKETVM_PARSER_H

#include "Opcode.h"
#include "State.h"
#include "Input.h"

struct Parser;
struct Table;
struct Prototype;
struct Lexer;
enum   Opcode;

struct Function : public Gc_Object
{

    Parser*         parser;         // Only valid while the function is being parsed.
    Function*       parent;

    int             numParams;
    bool            varArg;

    int             numRegisters;
    int             maxStackSize;

    Table*          constants;
    int             numConstants;

    Instruction*    code;
    int             codeSize;
    int             maxCodeSize;

    int*            sourceLine;
    int             maxSourceLines;

    String*         local[LUAI_MAXVARS];
    int             numLocals;
    int             numCommitedLocals;

    String*         upValue[LUAI_MAXUPVALUES];
    int             numUpValues;

    Function**      function;
    int             numFunctions;
    int             maxFunctions;

};

struct Block
{
    int             firstLocal;
    bool            breakable;
    int             firstBreakPos;
    int             firstLocalUpValue;
};

struct Parser
{
    lua_State*      L;
    Lexer*          lexer;  
    Function*       function;
    Block           block[LUAI_MAXCCALLS];
    int             numBlocks;
    int             lineNumber;
};

enum ExpressionType
{
    EXPRESSION_NONE,
    EXPRESSION_REGISTER,
    EXPRESSION_CONSTANT,
    EXPRESSION_GLOBAL,
    EXPRESSION_TABLE,
    EXPRESSION_LOCAL,
    EXPRESSION_NIL,
    EXPRESSION_FUNCTION,
    EXPRESSION_BOOLEAN,
    EXPRESSION_NUMBER,
    EXPRESSION_CALL,
    EXPRESSION_JUMP,
    EXPRESSION_UPVALUE,
    EXPRESSION_NOT,
    EXPRESSION_VARARG,
    EXPRESSION_TEMP,
};

/**
 * If type is EXPRESSION_TABLE:
 *  - index is the register index of the table
 *  - key is the register or constant index of the key
 *  - keyType specifies whether or not key is a EXPRESSION_REGISTER or CONSTANT
 *
 * If type is EXPRESSION_FUNCTION:
 *  - index is the index of the function in the 'function' array
 *
 * If type is EXPRESSION_GLOBAL:
 *  - index is the index of the name in the constant table.
 *
 * If type is EXPRESSION_BOOLEAN:
 *  - index is 1 or 0 depending on the value.
 *
 * If type is EXPRESSION_JUMP:
 *  - index is the location of the jump instruction.
 *
 * If type is EXPRESSION_UPVALUE:
 *  - index is the index of the up value in the up values array.
 *
 * If type is EXPRESSION_NOT:
 *  - index is the index of the register to negate.
 *
 * If type is EXPRESSION_CALL:
 *  - index is the index of the function.
 *  - numArgs is the number of arguments to the function.
 *
 * If type is EXPRESSION_TEMP:
 *  This represents a result stored in a temporary register 
 *  - index is address of the instruction which set the value in the temporary
 *    register. The 'A' parameter for the instruction can be safely updated to
 *    a new register location.
 *
 */
struct Expression
{
    Expression() { type = EXPRESSION_NONE; exitJump[0] = -1; exitJump[1] = -1; }
    ExpressionType          type;
    int                     exitJump[2];
    int                     index;
    union
    {
        lua_Number          number;
        int                 numArgs;
        struct
        {
            int             key;
            ExpressionType  keyType;
        };
    };
};

Function* Function_Create(lua_State* L);
void Function_Destroy(lua_State* L, Function* function);

void Parser_Initialize(Parser* parser, lua_State* L, Lexer* lexer);
void Parser_Destroy(Parser* parser);

/**
 * Disengages the function from the parser.
 */
Function* Parser_ReleaseFunction(Parser* parser);

/** Reports a formatted error. The error message will include information about
the location of the error in the chunk being parsed. This function raises a VM
error, so it will not return. */
void Parser_Error(Parser* parser, const char* fmt, ...);

bool Parser_Accept(Parser* parser, int token);
 
/** Expects the next token to be a specific token. If it's not the token, an
error is generated. */
bool Parser_Expect(Parser* parser, int token);

/** Expects the next token to be one of two tokens. If the token isn't one of
the tokens, an error is generated. */
bool Parser_Expect(Parser* parser, int token1, int token2);

/** Puts the current token back so that the next call to Parser_Accept or
Parser_Expect will process it again. */
void Parser_Unaccept(Parser* parser);

/** Returns the type of the current token. */
int Parser_GetToken(Parser* parser);

/** Returns the string for a name or string token. */
String* Parser_GetString(Parser* parser);

/** Returns the value for a number token. */
lua_Number Parser_GetNumber(Parser* parser);

int Parser_AddFunction(Parser* parser, Function* function);

/**
 * The local will not be visible until it is commited using the
 * Parser_CommitLocals function.
 */
int Parser_AddLocal(Parser* parser, String* name);

/**
 * Makes all of the locals that were added using Parser_AddLocal
 * visible to script.
 */
void Parser_CommitLocals(Parser* parser);

int Parser_GetLocalIndex(Parser* parser, String* name);
int Parser_AddUpValue(Parser* parser, String* name);

int Parser_AddConstant(Parser* parser, Value* value);
int Parser_AddConstant(Parser* parser, String* string);

/**
 * Reserves a new register and returns the index.
 */
int Parser_AllocateRegister(Parser* parser);

void Parser_SetLastRegister(Parser* parser, int reg);

/** Returns the number of registers that are currently in use. */
int Parser_GetNumRegisters(Parser* parser);

void Parser_FreeRegisters(Parser* parser);
void Parser_FreeRegisters(Parser* parser, int num);

/**
 * Converts a function call (if value specifies one) into a register.
 */
bool Parser_ResolveCall(Parser* parser, Expression* value, int numResults);

/**
 * Converts a vararg call (if value specifies one) into a register.
 */
bool Parser_ResolveVarArg(Parser* parser, Expression* value, int numResults, int regHint = -1);

/**
 * Converts an expression into an open test if it isn't one. If reg is not -1
 * then the value being tested will also be stored in that register. The test
 * value indicates the condition under which the jump is taken.
 */
void Parser_ConvertToTest(Parser* parser, Expression* value, int test, int reg = -1);

/**
 * Updates an open test expression so that if the expression is false it will
 * jump to the instruction specified by startPos.
 */
void Parser_CloseJump(Parser* parser, Expression* value, int startPos = -1);

/**
 * Opens a new jump by emitting an jump instruction. The location of the jump
 * is determined when the jump is closed by calling Parser_CloseTest.
 */
void Parser_OpenJump(Parser* parser, Expression* dst);

/**
 * Adds a jump instruction (at jumpPos) to the list which will be updated to
 * jump to the current position immediately after the value is moved to a
 * register.
 */
void Parser_AddExitJump(Parser* parser, Expression* value, int test, int jumpPos);

void Parser_FinalizeExitJump(Parser* parser, Expression* value, int cond, int reg);

/**
 * Returns the index of the register occupied by the value, or -1 if the
 * expression is not in a register.
 */
int Parser_GetRegisterHint(Parser* parser, const Expression* value);

/**
 * Changes the expression into a register value if it can be directly converted
 * (for example if it's a local variable). Otherwise the function returns false
 * and the value is unchanged.
 */
bool Parser_ConvertToRegister(Parser* parser, Expression* value);

/**
 * Ensures that the location specifies a register. If reg specifies an index
 * then it will be moved to that register. Returns the index of the register
 * where the value was stored.
 */
int Parser_MoveToRegister(Parser* parser, Expression* value, int reg = -1);

/**
 * Ensures that the location specifies a register or a constant slot.
 */
void Parser_MoveToRegisterOrConstant(Parser* parser, Expression* value, int reg = -1);

/**
 * Ensures that the expression specifies a location that can be encoded using 
 * Parser_EncodeRK. If a register or a constant index is too high, this function
 * will move it to a location that allows it to be encoded.
 */
void Parser_MakeRKEncodable(Parser* parser, Expression* value);

/**
 * Moves the expression to the top of the register stack into a temporary
 * register. If regHint is the top of the stack, that position will be used.
 */
void Parser_MoveToStackTop(Parser* parser, Expression* value, int regHint = -1);

/**
 * Returns true if the expression is a temporary register location (i.e. not
 * a local).
 */
bool Parser_GetIsTemporaryRegister(Parser* parser, const Expression* value);

/**
 * Chooses a register location to write a temporary value. If regHit is not -1
 * then that register index will be chosen. Otherwise a new one will be
 * allocated.
 */
void Parser_SelectDstRegister(Parser* parser, Expression* dst, int regHint);

/**
 * Appends a new instruction to the function.
 */
int Parser_EmitInstruction(Parser* parser, Instruction inst);

/**
 * Returns the position where the next instruction will be written.
 */
int Parser_GetInstructionCount(Parser* parser);

/**
 * Overwrites the instruction at the specified position.
 */
void Parser_UpdateInstruction(Parser* parser, int pos, Instruction inst);

/**
 * Returns the instruction at the specified position.
 */
Instruction Parser_GetInstruction(Parser* parser, int pos);

/**
 * Emits a 3 argument instruction with args A B C.
 */
int Parser_EmitABC(Parser* parser, Opcode opcode, int a, int b, int c);

/**
 * Emits a 2 argument instruction with args A B.
 */
void Parser_EmitAB(Parser* parser, Opcode opcode, int a, int b);

/**
 * Emits a 2 argument instruction with args A Bx.
 */
void Parser_EmitABx(Parser* parser, Opcode opcode, int a, int bx);

/**
 * Emits a 2 argument instruction with args A sBx.
 */
void Parser_EmitAsBx(Parser* parser, Opcode opcode, int a, int sbx);

/**
 * Encodes a register or constant slot for packing in an instruction which can
 * accept either. The location must be either a register or constant.
 */
int Parser_EncodeRK(Parser* parser, Expression* location);

/**
 * Begin/EndSkip are used to delininate a set of instructions which will
 * be skipped over such as:
 *     jmp  end
 *     <block>
 *   end:
 */
void Parser_BeginSkip(Parser* parser, int* id);
void Parser_EndSkip(Parser* parser, int* id);

/**
 * Begin/EndLoop are used to delininate a set of instructions which will
 * be repeated over such as:
 *   start:
 *     <block>
 *     jmp  start
 */
void Parser_BeginLoop(Parser* parser, int* id);
void Parser_EndLoop(Parser* parser, int* id);

Prototype* Function_CreatePrototype(lua_State* L, Function* function, String* source);

/**
 * The breakable flag indicates if the block can be exited with a break
 * statement.
 */
void Parser_BeginBlock(Parser* parser, bool breakable);
void Parser_EndBlock(Parser* parser);

void Parser_BreakBlock(Parser* parser);

bool Parser_ConvertToBoolean(Parser* parser, Expression* value);

/**
 * Looks up a variable name and sets the expression as either a global, a
 * local or an upvalue.
 */
void Parser_ResolveName(Parser* parser, Expression* dst, String* name);

bool Parser_GetHasUpValues(Parser* parser);
void Parser_CloseUpValues(Parser* parser);

#endif