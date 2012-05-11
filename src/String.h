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
	String*			nextString;	// For chaining in the string pool.
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
 * Releases the memory for a string. This should only be called when you know
 * there are no remaining references to the string (i.e. it should only be
 * called by the garbage collector).
 */
void String_Destroy(lua_State* L, String* string); 

// Return value is the same as strcmp.
int String_Compare(String* string1, String* string2);

void StringPool_Initialize(lua_State* L, StringPool* stringPool);
void StringPool_Shutdown(lua_State* L, StringPool* stringPool);

void StringPool_SweepStrings(lua_State* L, StringPool* stringPool);

#endif