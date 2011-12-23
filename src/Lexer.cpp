/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */
#include "Lexer.h"
#include "String.h"
#include "State.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static const char* tokenName[] =
    {
        // Reserved words.
        "and",
        "break",
        "do",
        "else",
        "elseif",
        "end",
        "false",
        "for",
        "function",
        "if",
        "in",
        "local",
        "nil",
        "not",
        "or",
        "repeat",
        "return",
        "then",
        "true",
        "until",
        "while",
        // Symbols.
        "..",
        "=",
        ">=",
        "<=",
        "~=",
        // Specials.
        "number",
        "name",
        "string",
        "end of stream",
    };

void Lexer_Initialize(Lexer* lexer, lua_State* L, Input* input)
{
    lexer->L            = L;
    lexer->input        = input;
    lexer->lineNumber   = 1;
    lexer->string       = NULL;
    lexer->haveToken    = false;
    Lexer_NextToken(lexer);
}

static inline bool Lexer_IsSpace(int c)
{
    return c == ' ' || c == '\t';
}

static inline bool Lexer_IsNewLine(int c)
{
    return c == '\n' || c == '\r';
}

static inline bool Lexer_IsDigit(int c)
{
    return c >= '0' && c <= '9';
}

static void Lexer_ReadComment(Lexer* lexer)
{
    int c;
    do
    {
        c = Input_ReadByte(lexer->input);
    }
    while (c != END_OF_STREAM && !Lexer_IsNewLine(c));
    ++lexer->lineNumber;
}

const char* Token_GetString(Token token)
{
    return tokenName[token - Token_First];
}

void Lexer_NextToken(Lexer* lexer)
{

    lexer->haveToken = true;

    while (1)
    {

        int c = Input_ReadByte(lexer->input);

        switch (c)
        {
        case '\n':
        case '\r':
            ++lexer->lineNumber;
            break;
        case ' ':
        case '\t':
            break;
        case END_OF_STREAM:
            lexer->token = Token_EndOfStream;
            return;
        case '~':
            if (Input_PeekByte(lexer->input) == '=')
            {
                Input_ReadByte(lexer->input);
                lexer->token = Token_Ne;
            }
            else
            {
                lexer->token = '~';
            }
            return;
        case '=':
            if (Input_PeekByte(lexer->input) == '=')
            {
                Input_ReadByte(lexer->input);
                lexer->token = Token_Eq;
            }
            else
            {
                lexer->token = '=';
            }
            return;
        case '<':
            if (Input_PeekByte(lexer->input) == '=')
            {
                Input_ReadByte(lexer->input);
                lexer->token = Token_Le;
            }
            else
            {
                lexer->token = '<';
            }
            return;
        case '>':
            if (Input_PeekByte(lexer->input) == '=')
            {
                Input_ReadByte(lexer->input);
                lexer->token = Token_Ge;
            }
            else
            {
                lexer->token = '>';
            }
            return;
        case '-':
            if (Input_PeekByte(lexer->input) != '-')
            {
                lexer->token = '-';
                return;
            }
            Lexer_ReadComment(lexer);
            break;
        case '+':
        case '*':
        case '/':
        case '(':
        case ')':
        case '[':
        case ']':
        case '{':
        case '}':
        case ',':
        case ':':
        case '#':
            lexer->token = c;
            return;
        case '.':
            if (Input_PeekByte(lexer->input) == '.')
            {
                Input_ReadByte(lexer->input);
                lexer->token = Token_Concat;
                return;
            }
            lexer->token = c;
            return;
        case '"':
        case '\'':
            {
                int end = c;
                // Read the string literal.
                char buffer[1024];
                size_t length = 0;
                while (length < 1024)
                {
                    c = Input_ReadByte(lexer->input);
                    if (c == '\n')
                    {
                        State_Error(lexer->L);
                    }
                    if (c == end)
                    {
                        break;
                    }
                    if (c == '\\')
                    {
                        // Handle escape sequences.
                        c = Input_ReadByte(lexer->input);
                        switch (c)
                        {
                        case 'a': c = '\a'; break;
                        case 'b': c = '\b'; break;
                        case 'f': c = '\f'; break;
                        case 'n': c = '\n'; break;
                        case 'r': c = '\r'; break;
                        case 't': c = '\t'; break;
                        case 'v': c = '\v'; break;
                        case '\\':  c = '\\'; break;
                        case '\"':  c = '\"'; break;
                        case '\'':  c = '\''; break;
                        default:
                            State_Error(lexer->L);
                        }
                    }
                    buffer[length] = c;
                    ++length;
                }
                lexer->token = Token_String;
                lexer->string = String_Create(lexer->L, buffer, length);
            }
            return;
        default:
            if (Lexer_IsDigit(c))
            {
                lexer->number = 0.0f;
                while (1)
                {
                    lua_Number digit = static_cast<lua_Number>(c - '0');
                    lexer->number = lexer->number * 10.0f + digit;
                    c = Input_PeekByte(lexer->input);
                    if (Lexer_IsDigit(c))
                    {
                        Input_ReadByte(lexer->input);
                    }
                    else
                    {
                        break;
                    }
                }
                lexer->token = Token_Number;
            }
            else
            {
                char buffer[LUA_MAXNAME];
                size_t bufferLength = 1;
                buffer[0] = c;
                while (bufferLength < LUA_MAXNAME)
                {
                    c = Input_PeekByte(lexer->input);
                    if (c < 128 && (isalpha(c) || c == '_' || isdigit(c)))
                    {
                        Input_ReadByte(lexer->input);
                        buffer[bufferLength] = c;
                        ++bufferLength;
                    }
                    else
                    {
                        break;
                    }
                }

                // Check to see if this is one of the reserved words.
                const int numReserved = Token_LastReserved - Token_First + 1;
                for (int i = 0; i < numReserved; ++i)
                {
                    size_t length = strlen(tokenName[i]);
                    if (length == bufferLength && strncmp(buffer, tokenName[i], bufferLength) == 0)
                    {
                        lexer->token = Token_And + i;
                        return;
                    }
                }

                lexer->string = String_Create(lexer->L, buffer, bufferLength);
                lexer->token = Token_Name;

            }
            return;
        }
    }

}