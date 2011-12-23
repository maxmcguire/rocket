/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */
#ifndef ROCKETVM_LEXER_H
#define ROCKETVM_LEXER_H

#include "Input.h"

struct String;

// In addition to these token values, single character values are also
// used as tokens. The order of these is significant and must match the
// order of reserved words inside Lexer_NextToken.
enum Token
{
    // Reserved words.
    Token_First     = 256,
    Token_And       = Token_First,
    Token_Break,
    Token_Do,
    Token_Else,
    Token_ElseIf,
    Token_End,
    Token_False,
    Token_For,
    Token_Function,
    Token_If,
    Token_In,
    Token_Local,
    Token_Nil,
    Token_Not,
    Token_Or,
    Token_Repeat,
    Token_Return,
    Token_Then,
    Token_True,
    Token_Until,
    Token_While,
    // Symbols.
    Token_LastReserved  = Token_While,
    Token_Concat,
    Token_Dots,
    Token_Eq,
    Token_Ge,
    Token_Le,
    Token_Ne,
    // Specials.
    Token_Number,
    Token_Name,
    Token_String,
    Token_EndOfStream,
};

struct Lexer
{
    lua_State*  L;
    Input*      input;
    int         lineNumber;
    int         token;
    String*     string;
    lua_Number  number;
    bool        haveToken;
};

const char* Token_GetString(Token token);

void Lexer_Initialize(Lexer* lexer, lua_State* L, Input* input);
void Lexer_NextToken(Lexer* lexer);

#endif