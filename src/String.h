/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */
#ifndef ROCKETVM_STRING_H
#define ROCKETVM_STRING_H

#include "Gc.h"

struct Table;
union  Value;

struct String : public Gc_Object
{
	unsigned int    hash;
	size_t 			length;
	String*			nextString;	// Next chained string in the string pool.
    String*         prevString; // Pevious chained string in the string pool.
#ifdef DEBUG
    // Useful for viewing in the watch window, but not necessary since the
    // string data is immediately after the structure in memory.
    const char*     _data;
#endif
};

struct StringPool
{
    String**        node;
    int             numStrings;
    int             numNodes;
};

inline const char* String_GetData(const String* string)
    { return reinterpret_cast<const char*>(string + 1); }

/**
 * Allocates a new string. If a string with identical data already exists,
 * that string will be returned instead of allocating a new one.
 */
String* String_Create(lua_State* L, const char* data);
String* String_Create(lua_State* L, const char* data, size_t length);

/**
 * Allocates an array of strings which exist outside of the garbage collector.
 * The strings must be explicitly released with DestroyUnmanagedArray. The
 * memory for the strings is allocated in a contiguous block so that a string
 * s can be tested to see if it's inside the array by:
 * string[0] >= s && s <= string[numStrings - 1]
 */
void String_CreateUnmanagedArray(lua_State* L, String* string[], const char* data[], int numStrings);
void String_DestroyUnmanagedArray(lua_State* L, String* string[], int numStrings);

/**
 * Releases the memory for a string. This should only be called when you know
 * there are no remaining references to the string (i.e. it should only be
 * called by the garbage collector).
 */
void String_Destroy(lua_State* L, String* string); 

// Return value is the same as strcmp.
int String_Compare(String* string1, String* string2);

void StringPool_Initialize(lua_State* L, StringPool* stringPool);
void StringPool_Shutdown(lua_State* L, StringPool* stringPool);

/**
 * Removes a string from the string pool. This does not destroy the string,
 * which is done with String_Destroy. Strings do not need to be manually
 * removed from the string pool or destroyed. This is done automatically by the
 * garbage collector when they are no longer needed.
 */
void StringPool_Remove(lua_State* L, StringPool* stringPool, String* string);

#endif