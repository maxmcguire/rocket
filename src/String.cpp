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
    // TODO: If a string is very long, don't hash all of the characters.
	// FNV-1a hash: http://isthe.com/chongo/tech/comp/fnv/
    unsigned long hash = 2166136261;
    for (size_t i = 0; i < length; ++i)
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

    for (int i = 0; i < stringPool->numNodes; ++i)
    {
        String* string = stringPool->node[i];
        while (string != NULL)
        {
            String* next = string->nextString;
            String_Destroy(L, string);
            string = next;
        }
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
            node[index] = string;

            string = next;
        }
    }

    FreeNodeArray(L, stringPool->node, stringPool->numNodes);

    stringPool->numNodes = numNodes;
    stringPool->node     = node;

}

String* StringPool_Insert(lua_State* L, StringPool* stringPool, const char* data, size_t length)
{

	unsigned int hash = HashString(data, length);
	
	int index = hash % stringPool->numNodes;
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

    if (string == NULL)
	{
		
		// Not already in the pool, so create a new object. To improve memory locality,
		// we store the data for the string immediately after the String structure.
		string = static_cast<String*>( Gc_AllocateObject(L, LUA_TSTRING, sizeof(String) + length + 1, false) );

		string->hash 		= hash;
		string->length		= length;
		string->nextString  = stringPool->node[index];

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
    else
    {
        Gc_MarkObject(&L->gc, string);
    }

	return string;

}

void StringPool_SweepStrings(lua_State* L, StringPool* stringPool)
{
    
    String** node = stringPool->node;

    for (int i = 0; i < stringPool->numNodes; ++i)
    {
        String* string = node[i];
        String* prev   = NULL;
        int j = 0;
        while (string != NULL)
        {
            String* next = string->nextString;
            if (string->color == Color_White)
            {
                if (prev == NULL)
                {
                    node[i] = next;
                }
                else
                {
                    prev->nextString = next;
                }
                String_Destroy(L, string);
                --stringPool->numStrings;
            }
            else
            {
                string->color = Color_White;
                prev = string;
            }
            string = next;
            ++j;
        }
    }
                    
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
    size_t size = sizeof(String) + string->length + 1;
    Free(L, string, size);
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