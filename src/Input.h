/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */
#ifndef ROCKETVM_INPUT_H
#define ROCKETVM_INPUT_H

#include "lua.h"

struct Input
{
    lua_State*      L;
    lua_Reader      reader;
    const char*     buffer;
    size_t          size;
    void*           userdata;
};

#define END_OF_STREAM	(-1)

void Input_Initialize(lua_State* L, Input* input, lua_Reader reader, void* userdata);

// Returns the next byte in the stream.
int Input_ReadByte(Input* input);

// Returns the next byte in the stream without consuming it.
int Input_PeekByte(Input* input);

// Reads a block of memory from the input steam up to a maximum length.
// Returns the length of the data that was read.
size_t Input_ReadBlock(Input* input, void* buffer, size_t size);

// Reads the rest of the input into a buffer. The returned buffer should be
// released using the Free function.
char* Input_Read(Input* input, size_t* size);

#endif