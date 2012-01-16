/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#ifndef ROCKETVM_LEXER_H
#define ROCKETVM_LEXER_H

#include "Input.h"

struct String;

// In addition to these token values, single character values are also
// used as tokens. The order of these is significant and must match the
// order of reserved words inside Lexer_NextToken.
enum TokenType
{
    // Reserved words.
    TokenType_First     = 256,
    TokenType_And       = TokenType_First,
    TokenType_Break,
    TokenType_Do,
    TokenType_Else,
    TokenType_ElseIf,
    TokenType_End,
    TokenType_False,
    TokenType_For,
    TokenType_Function,
    TokenType_If,
    TokenType_In,
    TokenType_Local,
    TokenType_Nil,
    TokenType_Not,
    TokenType_Or,
    TokenType_Repeat,
    TokenType_Return,
    TokenType_Then,
    TokenType_True,
    TokenType_Until,
    TokenType_While,
    // Symbols.
    TokenType_LastReserved  = TokenType_While,
    TokenType_Concat,
    TokenType_Dots,
    TokenType_Eq,
    TokenType_Ge,
    TokenType_Le,
    TokenType_Ne,
    // Specials.
    TokenType_Number,
    TokenType_Name,
    TokenType_String,
    TokenType_EndOfStream,
};

struct Token
{
    int         type;
    String*     string;
    lua_Number  number;
};

const int Lexer_maxRestoreTokens = 4;

struct Lexer
{
    lua_State*  L;
    Input*      input;
    int         lineNumber;
    Token       token;
    bool        haveToken;
    Token       restoreToken[Lexer_maxRestoreTokens];
    int         numRestoreTokens;
};

const char* Token_GetString(TokenType token);

void Lexer_Initialize(Lexer* lexer, lua_State* L, Input* input);
void Lexer_NextToken(Lexer* lexer);

void Lexer_Error(Lexer* lexer, const char* fmt, ...);

int Lexer_GetTokenType(Lexer* lexer);

void Lexer_CaptureToken(Lexer* lexer, Token* token);
void Lexer_RestoreTokens(Lexer* lexer, const Token token[], int numTokens);

#endif