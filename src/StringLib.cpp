/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#ifdef WIN32
#define snprintf _snprintf
#endif

static int StringLib_Char(lua_State* L)
{   
    
    int numArgs = lua_gettop(L);

    if (numArgs < 256)
    {
        char buffer[256];
        for (int i = 0; i < numArgs; ++i)
        {
            int c = luaL_checkinteger(L, i + 1);
            buffer[i] = c;
        }
        lua_pushlstring(L, buffer, numArgs);
    }
    else
    {
        assert(0);
        lua_pushnil(L);
    }

    return 1;

}

// Extracts for printf style formating specifiers for a field.
static const char* ScanFormat(lua_State *L, const char* strfrmt, char* form)
{
    const char* flags = "-+ #0";
    size_t flagsSize = 5;

    const char *p = strfrmt;
    // Skip flags.
    while (*p != '\0' && strchr(flags, *p) != NULL)
    {
        p++;
    }
    if (static_cast<size_t>(p - strfrmt) >= flagsSize)
    {
        luaL_error(L, "invalid format (repeated flags)");
    }
    if (isdigit(static_cast<unsigned char>(*p))) p++;  /* skip width */
    if (isdigit(static_cast<unsigned char>(*p))) p++;  /* (2 digits at most) */
    if (*p == '.')
    {
        p++;
        if (isdigit(static_cast<unsigned char>(*p))) p++;  /* skip precision */
        if (isdigit(static_cast<unsigned char>(*p))) p++;  /* (2 digits at most) */
    }
    if (isdigit(static_cast<unsigned char>(*p)))
    {
        luaL_error(L, "invalid format (width or precision too long)");
    }
    *(form++) = '%';
    strncpy(form, strfrmt, p - strfrmt + 1);
    form += p - strfrmt + 1;
    *form = '\0';
 
    return p;

}

static int StringLib_Format(lua_State* L)
{

    size_t length;
    const char* format = luaL_checklstring(L, 1, &length);
    const char* formatEnd = format + length;

    char buffer[1024];
    size_t bufferLength = 0;

    int arg = 2;

    while (format < formatEnd)
    {
        if (format[0] != '%')
        {
            buffer[bufferLength] = format[0];
            ++bufferLength;
            ++format;
        }
        else if (format[1] == '%')
        {
            buffer[bufferLength] = '%';
            ++bufferLength;
            format += 2;
        }
        else
        {

            ++format;

            // Extract the format for the field.
            char fieldFormat[256];
            format = ScanFormat(L, format, fieldFormat);

            // Compute the maximum number of characters we can write without
            // overflowing the buffer.
            size_t fieldLength = sizeof(buffer) - bufferLength;
            char* field = buffer + bufferLength;

            switch (format[0])
            {
            case 'd': case 'i':
                fieldLength = snprintf(field, fieldLength, fieldFormat, (int)luaL_checknumber(L, arg));
                break;
            case 'e': case 'E':
            case 'g': case 'G':
            case 'f':
                fieldLength = snprintf(field, fieldLength, fieldFormat, (double)luaL_checknumber(L, arg));
                break;
            default:
                luaL_error(L, "unexpected format specifier '%%%c'", format[0]);
                break;
            }
        
            bufferLength += fieldLength;
            ++format;
            ++arg;

        }
    }

    lua_pushlstring(L, buffer, bufferLength);
    return 1;

}

LUALIB_API int luaopen_string(lua_State *L)
{

    static const luaL_Reg functions[] =
        {
            { "char",   StringLib_Char      },
            { "format", StringLib_Format    },
            { NULL, NULL }
        };

    luaL_register(L, LUA_STRLIBNAME, functions);
    return 1;

}