/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#include "Buffer.h"
#include "State.h"

void Buffer_Initialize(lua_State* L, Buffer* buffer)
{
    buffer->data = NULL;
    buffer->length = 0;
    buffer->maxLength = 0;
}

void Buffer_Destroy(lua_State* L, Buffer* buffer)
{
    Free(L, buffer->data, buffer->maxLength);
    buffer->data = NULL;
}

void Buffer_Clear(lua_State* L, Buffer* buffer)
{
    buffer->length = 0;
}

void Buffer_Append(lua_State* L, Buffer* buffer, char c)
{
    if (buffer->length == buffer->maxLength)
    {
        size_t maxLength = buffer->maxLength + 64;
        buffer->data = (char*)Reallocate(L, buffer->data, buffer->maxLength, maxLength);
        buffer->maxLength = maxLength;
    }
    buffer->data[buffer->length] = c;
    ++buffer->length;
}