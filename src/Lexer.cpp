/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#include "Lexer.h"
#include "String.h"
#include "State.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

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
        "...",
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
    lexer->L                = L;
    lexer->input            = input;
    lexer->lineNumber       = 1;
    lexer->token.string     = NULL;
    lexer->haveToken        = false;
    lexer->numRestoreTokens = 0;
    Buffer_Initialize(L, &lexer->buffer);
    Lexer_NextToken(lexer);
}

void Lexer_Destroy(Lexer* lexer)
{
    Buffer_Destroy(lexer->L, &lexer->buffer);
}

void Lexer_Error(Lexer* lexer, const char* fmt, ...)
{

    PushFString(lexer->L, "Error line %d: ", lexer->lineNumber);

    va_list argp;
    va_start(argp, fmt);
    PushVFString(lexer->L, fmt, argp);
    va_end(argp);

    Concat(lexer->L, 2);
    
    State_Error(lexer->L);

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

static inline bool Lexer_IsAlphaNumeric(int c)
{
    return Lexer_IsDigit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static void Lexer_ReadComment(Lexer* lexer, int c)
{
    while (c != END_OF_STREAM && !Lexer_IsNewLine(c))
    {
        c = Input_ReadByte(lexer->input);
    }
    if (c == '\n')
    {
        ++lexer->lineNumber;
    }
}

/**
 * Reads from the input until the end of a CPP style block comment is reached.
 */
static void Lexer_ReadBlockComment(Lexer* lexer)
{
    int c;
    do
    {
        c = Input_ReadByte(lexer->input);
        if (c == '*' && Input_PeekByte(lexer->input) == '/')
        {
            Input_ReadByte(lexer->input);
            break;
        }
        if (c == '\n')
        {
            ++lexer->lineNumber;
        }
    }
    while (c != END_OF_STREAM);
}

const char* Token_GetString(TokenType token)
{
    return tokenName[token - TokenType_First];
}

static bool Lexer_IsNumberTerminal(int c)
{
    return !Lexer_IsAlphaNumeric(c) && c != '.';
}

static bool Lexer_IsHexDigit(int c)
{
    return Lexer_IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int Lexer_GetHexValue(int c)
{
    c = tolower(c);
    if (Lexer_IsDigit(c))
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    return 0;
}

static lua_Number Lexer_ReadDecimalDigits(Lexer* lexer, int& c)
{

    lua_Number number = 0.0;

    while (Lexer_IsDigit(c))
    {
        lua_Number digit = static_cast<lua_Number>(c - '0');
        number = number * 10.0 + digit;
        if (Lexer_IsNumberTerminal(Input_PeekByte(lexer->input)))
        {
            return number;
        }
        c = Input_ReadByte(lexer->input);
    }

    if (c == '.')
    {
        if (Lexer_IsNumberTerminal(Input_PeekByte(lexer->input)))
        {
            return number;
        }
        c = Input_ReadByte(lexer->input);
        lua_Number frac = 10.0;
        while (Lexer_IsDigit(c))
        {
            lua_Number digit = static_cast<lua_Number>(c - '0');
            number += digit / frac;
            frac *= 10.0;
            if (Lexer_IsNumberTerminal(Input_PeekByte(lexer->input)))
            {
                return number;
            }
            c = Input_ReadByte(lexer->input);
        }
    }

    return number;

}

static bool Lexer_ReadNumber(Lexer* lexer, int c)
{

    if (c == '.')
    {
        int n = Input_PeekByte(lexer->input);
        if (!Lexer_IsDigit(n))
        {
            return false;
        }
    }
    else if (c == '0')
    {
        int n = Input_PeekByte(lexer->input);
        if (n == 'x')
        {
            
            // Hexademical number.

            c = Input_ReadByte(lexer->input);
            c = Input_ReadByte(lexer->input);

            if (!Lexer_IsHexDigit(c))
            {
                Lexer_Error(lexer, "malformed number");
            }

            lexer->token.type   = TokenType_Number;
            lexer->token.number = 0.0;

            while (Lexer_IsHexDigit(c))
            {
                lua_Number digit = static_cast<lua_Number>(Lexer_GetHexValue(c));
                lexer->token.number = lexer->token.number * 16.0 + digit;
                if (Lexer_IsNumberTerminal(Input_PeekByte(lexer->input)))
                {
                    return true;
                }
                c = Input_ReadByte(lexer->input);
            }

            if (!Lexer_IsNumberTerminal(Input_PeekByte(lexer->input)))
            {
                // malformed number.
                Lexer_Error(lexer, "malformed number");
            }
            return true;

        }
    }
    else if (!Lexer_IsDigit(c))
    {
        return false;
    }

    lexer->token.type   = TokenType_Number;
    lexer->token.number = Lexer_ReadDecimalDigits(lexer, c);

    if (c == 'e' || c == 'E')
    {

        bool negative = false;

        c = Input_ReadByte(lexer->input);
        if (c == '-')
        {
            negative = true;
            c = Input_ReadByte(lexer->input);
        }

        lua_Number exponent = Lexer_ReadDecimalDigits(lexer, c);
        if (negative)
        {
            exponent = -exponent;
        }
        lexer->token.number *= luai_numpow(10, exponent);
        
    }

    if (!Lexer_IsNumberTerminal(Input_PeekByte(lexer->input)))
    {
        // malformed number.
        Lexer_Error(lexer, "malformed number");
    }
    return true;

}

/**
 * If store is false, the long block will be parsed, but it will not be captured
 * as a token.
 */
static bool Lexer_ReadLongBlock(Lexer* lexer, int c, bool store)
{

    if (c != '[')
    {
        return false;
    }

    // Long blocks start with the sequence: [====[ where the number of =
    // indicates the "level" of the block.

    if (Input_PeekByte(lexer->input) != '[' &&
        Input_PeekByte(lexer->input) != '=')
    {
        return false;
    }

    int level = 0;
    c = Input_ReadByte(lexer->input);

    while (c != '[')
    {
        if (c != '=')
        {
            Lexer_Error(lexer, "expected '='");
            return false;
        }
        ++level;
        c = Input_ReadByte(lexer->input);
    }

    // Lua ignores an initial new line in a long string.
    if (Input_PeekByte(lexer->input) == '\n')
    {
        ++lexer->lineNumber;
        Input_ReadByte(lexer->input);
    }

    Buffer_Clear(lexer->L, &lexer->buffer);

    while (1)
    {
        c = Input_ReadByte(lexer->input);
        if (c == '\n')
        {
            ++lexer->lineNumber;
        }
        if (c == END_OF_STREAM)
        {
            Lexer_Error(lexer, "unfinished long string");
        }
        else if (c == ']')
        {

            if (store)
            {
                Buffer_Append(lexer->L, &lexer->buffer, c);
            }

            int testLevel = 0;
            c = Input_PeekByte(lexer->input);
            while (c == '=')
            {
                Input_ReadByte(lexer->input);
                if (store)
                {
                    Buffer_Append(lexer->L, &lexer->buffer, c);
                }
                ++testLevel;
                c = Input_PeekByte(lexer->input);
            }

            if (c == ']' && testLevel == level)
            {
                Input_ReadByte(lexer->input);
                break;
            }

        }
        else if (store)
        {
            Buffer_Append(lexer->L, &lexer->buffer, c);
        }
    }

    if (store)
    {
        lexer->token.type   = TokenType_String;
        lexer->token.string = String_Create(lexer->L, lexer->buffer.data, lexer->buffer.length - (1 + level));
    }

    return true;
    
}

void Lexer_NextToken(Lexer* lexer)
{

    if (lexer->haveToken)
    {
        return;
    }

    if (lexer->numRestoreTokens > 0)
    {
        --lexer->numRestoreTokens;
        lexer->token     = lexer->restoreToken[lexer->numRestoreTokens];
        lexer->haveToken = true;
        return;
    }

    lexer->haveToken = true;
    
    while (1)
    {

        int c = Input_ReadByte(lexer->input);

        if (Lexer_ReadNumber(lexer, c) ||
            Lexer_ReadLongBlock(lexer, c, true))
        {
            return;
        }

        switch (c)
        {
        case '\n':
            ++lexer->lineNumber;
            break;
        case ' ':
        case '\t':
        case '\r':
            break;
        case END_OF_STREAM:
            lexer->token.type = TokenType_EndOfStream;
            return;
        case '~':
            if (Input_PeekByte(lexer->input) == '=')
            {
                Input_ReadByte(lexer->input);
                lexer->token.type = TokenType_Ne;
            }
            else
            {
                lexer->token.type = '~';
            }
            return;
        case '=':
            if (Input_PeekByte(lexer->input) == '=')
            {
                Input_ReadByte(lexer->input);
                lexer->token.type = TokenType_Eq;
            }
            else
            {
                lexer->token.type = '=';
            }
            return;
        case '<':
            if (Input_PeekByte(lexer->input) == '=')
            {
                Input_ReadByte(lexer->input);
                lexer->token.type = TokenType_Le;
            }
            else
            {
                lexer->token.type = '<';
            }
            return;
        case '>':
            if (Input_PeekByte(lexer->input) == '=')
            {
                Input_ReadByte(lexer->input);
                lexer->token.type = TokenType_Ge;
            }
            else
            {
                lexer->token.type = '>';
            }
            return;
        case '-':
            {
                int n = Input_PeekByte(lexer->input);
                if (n == '-')
                {
                    Input_ReadByte(lexer->input);
                    c = Input_ReadByte(lexer->input);
                    if (!Lexer_ReadLongBlock(lexer, c, false))
                    {
                        Lexer_ReadComment(lexer, c);
                    }
                    break;
                }
                lexer->token.type = '-';
            }
            return;
        case '/':
            if (Input_PeekByte(lexer->input) == '*')
            {
                Input_ReadByte(lexer->input);
                Lexer_ReadBlockComment(lexer);
            }
            else if (Input_PeekByte(lexer->input) == '/')
            {
                Input_ReadByte(lexer->input);
                c = Input_ReadByte(lexer->input);
                Lexer_ReadComment(lexer, c);
            }
            else
            {
                lexer->token.type = c;
                return;
            }
            break;
        case '+':
        case '*':
        case '%':
        case '^':
        case '(':
        case ')':
        case '[':
        case ']':
        case '{':
        case '}':
        case ',':
        case ':':
        case '#':
        case ';':
            lexer->token.type = c;
            return;
        case '.':
            if (Input_PeekByte(lexer->input) == '.')
            {
                Input_ReadByte(lexer->input);
                if (Input_PeekByte(lexer->input) == '.')
                {
                    Input_ReadByte(lexer->input);
                    lexer->token.type = TokenType_Dots;
                }
                else
                {
                    lexer->token.type = TokenType_Concat;
                }
                return;
            }
            lexer->token.type = c;
            return;
        case '"':
        case '\'':
            {
                int end = c;
                // Read the string literal.
                Buffer_Clear(lexer->L, &lexer->buffer);
                while (true)
                {
                    c = Input_ReadByte(lexer->input);
                    if (Lexer_IsNewLine(c))
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
                        if (Lexer_IsNewLine(c))
                        {
                        }
                        else if (Lexer_IsDigit(c))
                        {
                            int value = 0;
                            int i = 0;
                            while (true)
                            {
                                value = (value * 10) + c - '0';
                                c = Input_PeekByte(lexer->input);
                                if (!Lexer_IsDigit(c) || i >= 2)
                                {
                                    break;
                                }
                                Input_ReadByte(lexer->input);
                                ++i;
                            }
                            c = value;
                        }
                        else
                        {
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
                                Lexer_Error(lexer, "invalid escape sequence");
                            }
                        }
                    }
                    Buffer_Append(lexer->L, &lexer->buffer, c);
                }
                lexer->token.type = TokenType_String;
                lexer->token.string = String_Create(lexer->L, lexer->buffer.data, lexer->buffer.length);
            }
            return;
        default:
            {
                char buffer[LUAI_MAXNAME];
                size_t bufferLength = 1;
                buffer[0] = c;
                while (bufferLength < LUAI_MAXNAME)
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
                const int numReserved = TokenType_LastReserved - TokenType_First + 1;
                for (int i = 0; i < numReserved; ++i)
                {
                    size_t length = strlen(tokenName[i]);
                    if (length == bufferLength && strncmp(buffer, tokenName[i], bufferLength) == 0)
                    {
                        lexer->token.type = TokenType_And + i;
                        return;
                    }
                }

                lexer->token.string = String_Create(lexer->L, buffer, bufferLength);
                lexer->token.type = TokenType_Name;

            }
            return;
        }
    }

}

int Lexer_GetTokenType(Lexer* lexer)
{
    return lexer->token.type;
}

void Lexer_CaptureToken(Lexer* lexer, Token* token)
{
    *token = lexer->token;
}

void Lexer_RestoreTokens(Lexer* lexer, const Token token[], int numTokens)
{

    // If we have a token queued we need to store that as well since otherwise
    // it will be lost.
    if (lexer->haveToken)
    {
        assert( lexer->numRestoreTokens + 1 <= Lexer_maxRestoreTokens );
        lexer->restoreToken[ lexer->numRestoreTokens ] = lexer->token;
        ++lexer->numRestoreTokens;
        lexer->haveToken = false;
    }

    assert( lexer->numRestoreTokens + numTokens <= Lexer_maxRestoreTokens );
    memcpy( lexer->restoreToken + lexer->numRestoreTokens, token, numTokens * sizeof(Token) );
    lexer->numRestoreTokens += numTokens;    

}
