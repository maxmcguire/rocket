/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */
#ifndef ROCKETVM_PARSER_H
#define ROCKETVM_PARSER_H

#include "State.h"
#include "Input.h"

struct Table;
struct Prototype;
struct Lexer;
enum   Opcode;

struct Function
{

    Function*       parent;

    int             numParams;

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

    String*         upValue[LUAI_MAXUPVALUES];
    int             numUpValues;

    Function**      function;
    int             numFunctions;
    int             maxFunctions;

};

struct Block
{
    int             restoreNumLocals;
};

struct Parser
{
    lua_State*      L;
    Lexer*          lexer;  
    Function*       function;
    Block           block[LUAI_MAXCCALLS];
    int             numBlocks;
};

#define EXPRESSION_REGISTER     1
#define EXPRESSION_CONSTANT     2
#define EXPRESSION_GLOBAL       3
#define EXPRESSION_TABLE        4
#define EXPRESSION_LOCAL        5
#define EXPRESSION_NIL          6
#define EXPRESSION_FUNCTION     7
#define EXPRESSION_BOOLEAN      8
#define EXPRESSION_NUMBER       9
#define EXPRESSION_CALL         10
#define EXPRESSION_TEST         11
#define EXPRESSION_UPVALUE      12
#define EXPRESSION_NOT          13

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
 * If type is EXPRESSION_TEST:
 *  - index is the location of the jump instruction following a conditional test.
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
 */
struct Expression
{
    int             type;
    int             index;
    lua_Number      number;
    int             key;
    int             keyType;
    int             numArgs;
};

void Parser_Initialize(Parser* parser, lua_State* L, Lexer* lexer, Function* parent);
void Parser_Destroy(Parser* parser);

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

int Parser_AddLocal(Parser* parser, String* name);

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
 * Converts an expression into an open test if it isn't one.
 */
void Parser_ConvertToTest(Parser* parser, Expression* value);

/**
 * Updates an open test expression so that if the expression is false it will
 * jump to the current instruction location.
 */
void Parser_CloseTest(Parser* parser, Expression* value);

/**
 * Updates an open test expression so that if the expression is false it will
 * jump to the instruction specified by startPos.
 */
void Parser_CloseTest(Parser* parser, Expression* value, int startPos);

/**
 * Ensures that the location specifies a register. If reg specifies an index
 * then it will be moved to that register.
 */
void Parser_MoveToRegister(Parser* parser, Expression* value, int reg = -1);

/**
 * Ensures that the location specifies a register or a constant slot.
 */
void Parser_MoveToRegisterOrConstant(Parser* parser, Expression* value, int reg = -1);

/**
 * Moves the expression to the top of the register stack into a temporary
 * register.
 */
void Parser_MoveToStackTop(Parser* parser, Expression* value);

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
int Parser_EncodeRK(int index, int type);

/**
 * Encodes a 2 argument instruction with args A sBx.
 */
Instruction Parser_EncodeAsBx(Opcode opcode, int a, int sbx);

/**
 * Encodes a 3 argument instruction with args A B C.
 */
Instruction Parser_EncodeABC(Opcode opcode, int a, int b, int c);

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

void Parser_BeginBlock(Parser* parser);
void Parser_EndBlock(Parser* parser);

bool Parser_ConvertToBoolean(Parser* parser, Expression* value);

#endif