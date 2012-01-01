/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */

#include "String.h"
#include "State.h"

#include <memory.h>
#include <stdlib.h>
#include <string.h>

unsigned int HashString(const char* data, size_t length)
{
	// FNV-1a hash: http://isthe.com/chongo/tech/comp/fnv/
    unsigned long hash = 2166136261;
    for (size_t i = 0; i < length; ++i)
	{
    	hash ^= data[i];
    	hash *= 16777619;
    }
    return hash;
}

String* String_Create(lua_State* L, const char* data)
{
    return String_Create(L, data, strlen(data));
}

String* String_Create(lua_State* L, const char* data, size_t length)
{

	unsigned int hash = HashString(data, length);
	
	unsigned int index = hash % LUAI_MAXSTRINGPOOL;
	String* firstString = L->stringPoolEntry[index];	
	
	// Search for the exact string in the string pool.
	String* string = firstString;
	while (string != NULL)
	{
		if (string->length == length && memcmp(String_GetData(string), data, length) == 0)
		{
			break;
		}
		string = string->nextString;
	}

	if (string == NULL)
	{
		
		// Not already in the pool, so create a new object. To improve memory locality,
		// we store the data for the string immediately after the String structure.
		string = static_cast<String*>( Gc_AllocateObject(L, LUA_TSTRING, sizeof(String) + length + 1, false) );
		
		string->hash 		 = hash;
		string->length		 = length;
		string->nextString 	 = firstString;
		L->stringPoolEntry[index] = string;

        char* stringData = reinterpret_cast<char*>(string + 1);

		memcpy( stringData, data, length );
		stringData[length] = 0;

#ifdef _DEBUG
        string->_data = stringData;
#endif

	}

	return string;
	
}

void String_Destroy(lua_State* L, String* string)
{
    size_t size = sizeof(String) + string->length + 1;
    Free(L, string, size);
}

int String_Compare(String* string1, String* string2)
{
    return strcmp( String_GetData(string1), String_GetData(string2) );
}