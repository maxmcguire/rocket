/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */
#ifndef ROCKETVM_TABLE_H
#define ROCKETVM_TABLE_H

#include "State.h"
#include "Gc.h"

/**
 * To facilitate iterating over a table whilst removing elements, a nodes are
 * marked as dead. When a node is dead, the key should be treated as nil for
 * all purposes except iterating. If the node is marked as dead, then references
 * to the key should not prevent the key from being collected.
 */
struct TableNode
{
    bool            dead;
    Value           key;
    TableNode*      next;
    union
    {
        Value       value;  // Valid when the node is alive.
        TableNode*  prev;   // Valid when the node is dead.
    };
};

/**
 * A table is implemented as a union of an array and a hash table.
 */
struct Table : public Gc_Object
{
    int             numNodes;
    TableNode*      nodes;          // Hash nodes.
    Value*          element;        // Array elements.
    int             minHashKey;     // Mimumum integer key that appears in the hash.
    int             maxElements;    // Number of array slots allocated.
    int             numElements;    // Number of array slots initialized to valid values.
    int             numElementsSet; // Number of non-nil slots in the array.
    int             size;           // Size of the array (using Lua definition).
    Table*          metatable;
};

extern "C" Table* Table_Create(lua_State* L, int numArray, int numHash);
void   Table_Destroy(lua_State* L, Table* table);

/**
 * Updates the value for the key in the table. If the key does not exist
 * in the table, the function has no effect and returns false. If the value
 * is nil, 
 */
bool Table_Update(lua_State* L, Table* table, Value* key, Value* value);

/**
 * Inserts a new key, value pair into the table. The key is assumed to not
 * exist in the table already.
 */
void Table_Insert(lua_State* L, Table* table, int key, Value* value);
void Table_Insert(lua_State* L, Table* table, Value* key, Value* value);

void Table_SetTable(lua_State* L, Table* table, int key, Value* value);
void Table_SetTable(lua_State* L, Table* table, const char* key, Value* value);
void Table_SetTable(lua_State* L, Table* table, Value* key, Value* value);

/**
 * Returns the value for the key in the table. If the value is not in the table,
 * the function will return a nil value.
 */
Value* Table_GetTable(lua_State* L, Table* table, const Value* key);
Value* Table_GetTable(lua_State* L, Table* table, int key);
Value* Table_GetTable(lua_State* L, Table* table, String* key);

/**
 * For a hash table the size is t[n] is non-nil and t[n+1] is nil.
 */
int Table_GetSize(lua_State* L, Table* table);

/**
 * Returns the next value in the table after the specified key. The key will be
 * updated to the next key. The key can have previously been deleted from the
 * table as long as the table was not resized.
 */
const Value* Table_Next(lua_State* L, Table* table, Value* key);

#endif