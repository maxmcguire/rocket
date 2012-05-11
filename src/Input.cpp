/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#include "Input.h"
#include "State.h"

#include <memory.h>

void Input_Initialize(lua_State* L, Input* input, lua_Reader reader, void* userdata)
{
    input->L        = L;
    input->buffer   = NULL;
    input->size     = 0;
    input->userdata = userdata;
    input->reader   = reader;
}

static bool Input_FillBuffer(Input* input)
{
    input->size = 0;
    input->buffer = input->reader( input->L, input->userdata, &input->size );
    return input->buffer != NULL && input->size > 0;
}

size_t Input_ReadBlock(Input* input, void* _buffer, size_t size)
{

    size_t result = 0;

    char* buffer = static_cast<char*>(_buffer);
    while (size > input->size)
    {
        memcpy(buffer, input->buffer, input->size);
        buffer += input->size;
        size   -= input->size;
        result += input->size;
        if ( !Input_FillBuffer(input) )
        {
            return result;
        }
    }

    if (size > input->size)
    {
        size = input->size;
    }

    memcpy(buffer, input->buffer, size);    
    input->buffer += size;
    input->size   -= size;
    result        += size;  

    return result;

}

char* Input_Read(Input* input, size_t* size)
{

    // First copy anything that we've buffered.
    char* result = static_cast<char*>( Allocate(input->L, input->size) );
    memcpy(result, input->buffer, input->size);
    *size = input->size;

    input->size = 0;

    // Now keep reading until we get to the end.

    while (1)
    {

        size_t bufferSize;
        const char* buffer = input->reader( input->L, input->userdata, &bufferSize );

        if (buffer == NULL || bufferSize == 0)
        {
            break;
        }

        size_t newSize = *size + bufferSize;
        result = static_cast<char*>( Reallocate(input->L, result, *size, newSize) );

        memcpy(result + *size, buffer, bufferSize);
        *size = newSize;

    }
    return result;

}

int Input_PeekByte(Input* input)
{
    if (input->size == 0)
    {
        if (!Input_FillBuffer(input))
        {
            return END_OF_STREAM;
        }
    }
    return static_cast<int>(input->buffer[0]);
}

int Input_ReadByte(Input* input)
{
    if (input->size == 0)
    {
        if (!Input_FillBuffer(input))
        {
            return END_OF_STREAM;
        }
    }
    int byte = static_cast<int>(input->buffer[0]);
    ++input->buffer;
    --input->size;
    return byte;
}
