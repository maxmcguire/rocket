/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#ifndef ROCKETVM_BUFFER_H
#define ROCKETVM_BUFFER_H

#include <stdlib.h>

struct lua_State;

/**
 * Basic growable buffer.
 */
struct Buffer
{
    char*       data;
    size_t      length;
    size_t      maxLength;
};

void Buffer_Initialize(lua_State* L, Buffer* buffer);
void Buffer_Destroy(lua_State* L, Buffer* buffer);

void Buffer_Clear(lua_State* L, Buffer* buffer);

/**
 * Appends a character to the end of the buffer, growing it if necessary.
 */
void Buffer_Append(lua_State* L, Buffer* buffer, char c);

#endif
