/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#include "String.h"
#include "State.h"

#include <memory.h>
#include <stdlib.h>
#include <string.h>

static unsigned int HashString(const char* data, size_t length)
{
	// FNV-1a hash: http://isthe.com/chongo/tech/comp/fnv/
    unsigned long hash = 2166136261;

    // For long strings, don't hash all of the characters.
    size_t step = (length >> 5) + 1;
    for (size_t i = 0; i < length; i += step)
	{
    	hash ^= data[i];
    	hash *= 16777619;
    }

    return hash;
}

static String** CreateNodeArray(lua_State* L, int numNodes)
{
    size_t memSize = numNodes * sizeof(String*);
    String** node = static_cast<String**>(Allocate(L, memSize));
    memset(node, 0, memSize);
    return node;
}

static void FreeNodeArray(lua_State* L, String** node, int numNodes)
{
    Free(L, node, numNodes * sizeof(String*));
}

void StringPool_Initialize(lua_State* L, StringPool* stringPool)
{
    // This was chosen for the intial string pool size because it's
    // the size required to hold all of the strings after opening all
    // of the standard packages.
    const int initializeSize = 256;
    stringPool->numNodes    = initializeSize;    
    stringPool->node        = CreateNodeArray(L, stringPool->numNodes);
    stringPool->numStrings  = 0;
}

void StringPool_Shutdown(lua_State* L, StringPool* stringPool)
{
    // The strings should have already been collected by the garbage collector.
    for (int i = 0; i < stringPool->numNodes; ++i)
    {
        ASSERT( stringPool->node[i] == NULL );
    }
    FreeNodeArray(L, stringPool->node, stringPool->numNodes);
}

static void StringPool_Grow(lua_State* L, StringPool* stringPool, int numNodes)
{

    String** node = CreateNodeArray(L, numNodes);

    // Reinsert all of the strings into the new array.
    for (int i = 0; i < stringPool->numNodes; ++i)
    {
        String* string = stringPool->node[i];
        while (string != NULL)
        {
            String* next = string->nextString;

            int index = string->hash % numNodes;
            string->nextString = node[index];
            string->prevString = NULL;
            if (node[index] != NULL)
            {
                node[index]->prevString = string;
            }
            node[index] = string;

            string = next;
        }
    }

    FreeNodeArray(L, stringPool->node, stringPool->numNodes);

    stringPool->numNodes = numNodes;
    stringPool->node     = node;

}

/** Returns the number of bytes that must be allocated for a String object to
 * store a string of the specified size. */
static size_t String_GetStringObjectSize(size_t length)
{
    return sizeof(String) + length + 1;
}

static String* StringPool_FindInChain(StringPool* stringPool, int index, const char* data, size_t length)
{
	String* firstString = stringPool->node[index];	
	
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

    return string;
}

String* StringPool_Insert(lua_State* L, StringPool* stringPool, const char* data, size_t length)
{

	unsigned int hash = HashString(data, length);
	
	int index = hash % stringPool->numNodes;
    String* string = StringPool_FindInChain(stringPool, index, data, length);

    if (string == NULL)
	{
		
		// Not already in the pool, so create a new object. To improve memory locality,
		// we store the data for the string immediately after the String structure.
        size_t size = String_GetStringObjectSize(length);
		string = static_cast<String*>( Gc_AllocateObject(L, LUA_TSTRING, size) );

        String* nextString = stringPool->node[index];
        if (nextString != NULL)
        {
            nextString->prevString = string;
        }

		string->hash 		= hash;
		string->length		= length;
		string->nextString  = nextString;
        string->prevString  = NULL;

        char* stringData = reinterpret_cast<char*>(string + 1);

		memcpy( stringData, data, length );
		stringData[length] = 0;

#ifdef DEBUG
        string->_data = stringData;
#endif

        // Add to the pool.
		stringPool->node[index] = string;
        ++stringPool->numStrings;

        if (stringPool->numStrings >= stringPool->numNodes)
        {
            StringPool_Grow(L, stringPool, stringPool->numNodes * 2);
        }

	}

	return string;

}

void StringPool_Remove(lua_State* L, StringPool* stringPool, String* string)
{
    if (string->nextString != NULL)
    {
        string->nextString->prevString = string->prevString;
    }
    if (string->prevString != NULL )
    {
        string->prevString->nextString = string->nextString;
    }
    else
    {
        int index = string->hash % stringPool->numNodes;
        stringPool->node[index] = string->nextString;
    }
    string->nextString = NULL;
    string->prevString = NULL;
    --stringPool->numStrings;
}

String* String_Create(lua_State* L, const char* data)
{
    return String_Create(L, data, strlen(data));
}

String* String_Create(lua_State* L, const char* data, size_t length)
{
    return StringPool_Insert(L, &L->stringPool, data, length);
}

void String_Destroy(lua_State* L, String* string)
{
    ASSERT( string->prevString == NULL );
    ASSERT( string->nextString == NULL );
    size_t size = String_GetStringObjectSize(string->length);
    Free(L, string, size);
}

void String_CreateUnmanagedArray(lua_State* L, String* string[], const char* data[], int numStrings)
{

    size_t size = 0;
    for (int i = 0; i < numStrings; ++i)
    {
        size_t length = strlen(data[i]);
        size += String_GetStringObjectSize(length);
    }

    String* result = static_cast<String*>(Allocate(L, size));
    memset(result, 0, size);

    StringPool* stringPool = &L->stringPool;
    
    for (int i = 0; i < numStrings; ++i)
    {

        string[i] = result;

        size_t length       = strlen(data[i]);

        result->type        = LUA_TSTRING;
		result->hash 		= HashString(data[i], length);
		result->length		= length;

        char* stringData = reinterpret_cast<char*>(result + 1);

		memcpy( stringData, data[i], length );
		stringData[length] = 0;

#ifdef DEBUG
        result->_data = stringData;
#endif        

        // Add to the pool so that we don't end up with duplicated strings. Note
        // this must be done before the string is inserted into the table otherwise.

        int index = result->hash % stringPool->numNodes;
        ASSERT( StringPool_FindInChain(stringPool, index, data[i], length) == NULL );

        String* nextString = stringPool->node[index];
        if (nextString != NULL)
        {
            nextString->prevString = result;
        }
    
        result->nextString  = nextString;
        result->prevString  = NULL;

        stringPool->node[index] = result;
        ++stringPool->numStrings;

        if (stringPool->numStrings >= stringPool->numNodes)
        {
            StringPool_Grow(L, stringPool, stringPool->numNodes * 2);
        }

        // Advance to the next string in memory.
        size_t size = String_GetStringObjectSize(length);
        result = reinterpret_cast<String*>(reinterpret_cast<char*>(result) + size);

    }

}

void String_DestroyUnmanagedArray(lua_State* L, String* string[], int numStrings)
{
    if (numStrings > 0)
    {
        StringPool* stringPool = &L->stringPool;
        size_t size = 0;
        for (int i = 0; i < numStrings; ++i)
        {
            size_t length = string[i]->length;
            size += String_GetStringObjectSize(length);
            StringPool_Remove(L, stringPool, string[i]);
        }
        Free(L, string[0], size);
    }
}

int String_Compare(String* string1, String* string2)
{
    const char *l = String_GetData(string1);
    size_t ll = string1->length;
    
    const char *r = String_GetData(string2);
    size_t lr = string2->length;

    while (true)
    {
        int temp = strcoll(l, r);
        if (temp != 0)
        {
            return temp;
        }
        else
        {
            // Strings are equal up to an embedded '\0'
            size_t len = strlen(l);
            if (len == lr)  
            {
                return (len == ll) ? 0 : 1;
            }
            else if (len == ll)
            {
                // l is smaller than r (because r is not finished)
                return -1;
            }
            // Both strings longer than len; go on comparing (after the '\0')
            len++;
            l += len;
            ll -= len;
            r += len;
            lr -= len;
        }
    }
}