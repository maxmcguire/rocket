/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#include "Table.h"
#include "String.h"

#include <stdlib.h>
#include <assert.h>
#include <memory.h>

template <class T>
static inline void Swap(T& a, T& b)
{
    T temp = a;
    a = b;
    b = temp;
}

Table* Table_Create(lua_State* L)
{
    Table* table = static_cast<Table*>( Gc_AllocateObject(L, LUA_TTABLE, sizeof(Table)) );
    table->numNodes     = 0;
    table->nodes        = NULL;
    table->metatable    = NULL;
    return table;
}

void Table_Destroy(lua_State* L, Table* table)
{
    Free(L, table->nodes, table->numNodes * sizeof(TableNode));
    Free(L, table, sizeof(Table));
}

static inline void HashCombine(unsigned int& seed, unsigned int value)
{
    seed ^= value + (seed<<6) + (seed>>2);
}

static inline unsigned int Hash(double v)
{
    unsigned int* ptr = reinterpret_cast<unsigned int*>(&v);
    unsigned int seed = *ptr++;
    HashCombine(seed, *ptr);
    return seed;
}

static inline unsigned int Hash(void* key)
{
    // From: http://www.concentric.net/~ttwang/tech/inthash.htm
    // Disable 64-bit portability warning. This function should be
    // completely changed for 64-bit keys.
#pragma warning( push )
#pragma warning( disable : 4311 )
    unsigned int a = reinterpret_cast<unsigned int>(key);
#pragma warning( pop ) 
    a = (a+0x7ed55d16) + (a<<12);
    a = (a^0xc761c23c) ^ (a>>19);
    a = (a+0x165667b1) + (a<<5);
    a = (a+0xd3a2646c) ^ (a<<9);
    a = (a+0xfd7046c5) + (a<<3);
    a = (a^0xb55a4f09) ^ (a>>16);
    return a;
}

static inline unsigned int Hash(const Value* key)
{
    if (Value_GetIsNumber(key))
    {
        return Hash( key->number );
    }
    else if (Value_GetIsString(key))
    {
        return key->string->hash;
    }
    else if (Value_GetIsBoolean(key))
    {
        return key->boolean ? 1 : 0;
    }
    return Hash(key->object);
}

static inline bool KeysEqual(const Value* key1, const Value* key2)
{
    if (key1->tag != key2->tag)
    {
        return false;
    }
    return key1->object == key2->object;
}

static inline size_t Table_GetMainIndex(const Table* table, const Value* key)
{
    return Hash(key) & (table->numNodes - 1);
}

static inline bool Table_NodeIsEmpty(const TableNode* node)
{
    return node->dead;
}

/**
 * Returns true if the node pointer is valid for the table. This is used
 * for debugging.
 */
static bool Table_GetIsValidNode(const Table* table, const TableNode* node)
{
    return (node >= table->nodes) && (node < table->nodes + table->numNodes);
}

/**
 * Checks various aspects of the table to make sure they are correct. Returns
 * true if everything in the table structure appears valid. This function is 
 * not fast and should only be used for debugging.
 */
static bool Table_CheckConsistency(const Table* table)
{

    for (int i = 0; i < table->numNodes; ++i)
    {
        const TableNode* node = &table->nodes[i]; 
        
        if (Table_NodeIsEmpty(node))
        {
            // Check that empty nodes do not have a next pointer set.
            if (node->next != NULL)
            {
                assert(0);
                return false;
            }
        }
        else
        {
            // Check that all of the "next" pointers point to a valid element
            if (!Table_GetIsValidNode(table, node))
            {
                assert(0);
                return false;
            }

            // Check the invariant that either this node is in its main index,
            // or the element in its main index is in its *own* main index.

            size_t mainIndex = Table_GetMainIndex(table, &node->key);
            const TableNode* collidingNode = &table->nodes[i];

            if (collidingNode != node)
            {
                if (Table_GetMainIndex(table, &collidingNode->key) != mainIndex)
                {
                    assert(0);
                    return false;
                }

                // Check that our node is somewhere in the chain from the
                // colliding node.
                const TableNode* n = collidingNode->next;
                while (n != NULL && n != node)
                {
                    n = n->next;
                }
                if (n != node)
                {
                    assert(0);
                    return false;
                }

            }

        }

    }

    return true;

}

static bool Table_Resize(lua_State* L, Table* table, int numNodes)
{

    if (table->numNodes == numNodes)
    {
        return true;
    }
    
    size_t size = numNodes * sizeof(TableNode);
    TableNode* nodes = static_cast<TableNode*>( Allocate(L, size) );

    if (nodes == NULL)
    {
        return false;
    }

    for (int i = 0; i < numNodes; ++i)
    {
        SetNil(&nodes[i].key);
        nodes[i].next = NULL;
        nodes[i].dead = true;
    }
        
    // Rehash all of the nodes.

    Swap(table->numNodes, numNodes);
    Swap(table->nodes, nodes);

    for (int i = 0; i < numNodes; ++i)
    {
        if ( !Table_NodeIsEmpty(&nodes[i]) )
        {
            Table_SetTable(L, table, &nodes[i].key, &nodes[i].value);
        }
    }

    Free(L, nodes, numNodes * sizeof(TableNode));
    
    assert( Table_CheckConsistency(table) );
    return true;

}

/** Finds a node in the table with the specified key. */
static TableNode* Table_GetNode(Table* table, const Value* key)
{

    if (table->numNodes == 0)
    {
        return NULL;
    }
  
    size_t index = Table_GetMainIndex(table, key);
    TableNode* node = &table->nodes[index];

    while ( node != NULL && (node->dead || !KeysEqual(&node->key, key)) )
    {
        node = node->next;
    }

    return node;

}

/** Finds a node in the table with the specified key, including nodes that are
marked as dead. */
static TableNode* Table_GetNodeIncludingDead(Table* table, const Value* key)
{

    if (table->numNodes == 0)
    {
        return NULL;
    }
  
    size_t index = Table_GetMainIndex(table, key);
    TableNode* node = &table->nodes[index];

    while ( node != NULL && !KeysEqual(&node->key, key) )
    {
        node = node->next;
    }

    return node;

}

static bool Table_Remove(Table* table, const Value* key)
{

    TableNode* node = Table_GetNode(table, key);

    if (node == NULL)
    {
        return false;
    }

    node->dead = true;
    return true;

    // This implementation is based around moving nodes around, which doesn't
    // work for iterating. This code will be necessary during garbage collection
    // however, so it's kept here in preparation for that.
    /*
    if (table->numNodes == 0)
    {
        return false;
    }

    size_t index = Table_GetMainIndex(table, key);
    TableNode* node = &table->nodes[index];

    if ( Table_NodeIsEmpty(node) )
    {
        return false;
    }

    if ( KeysEqual(&node->key, key) )
    {

        // The node is in its main index, which means it is the first element
        // in the chain (if there were any collisions).

        TableNode* nextNode = node->next;
        if (nextNode != NULL)
        {
            *node = *nextNode;
            nextNode->dead = true;
            nextNode->next = NULL;
        }
        else
        {
            node->dead = true;
        }

    }
    else
    {

        TableNode* prevNode = node;
        TableNode* nextNode = node->next;
        
        while ( nextNode != NULL && !KeysEqual(&nextNode->key, key) )
        {
            prevNode = nextNode;
            nextNode = nextNode->next;
        }
        if (nextNode == NULL)
        {
            // Not in the table.
            return false;
        }

        prevNode->next = nextNode->next;
        nextNode->dead = true;
        nextNode->next = NULL;

    }

    assert( Table_CheckConsistency(table) );
    return true;
    */

}

static TableNode* Table_GetFreeNode(Table* table)
{
    for (int i = 0; i < table->numNodes; ++i)
    {
        if ( Table_NodeIsEmpty(&table->nodes[i]) )
        {
            return &table->nodes[i];
        }
    }
    return NULL;
}

void Table_SetTable(lua_State* L, Table* table, int key, Value* value)
{
    Value k;
    SetValue( &k, key );
    Table_SetTable(L, table, &k, value);
}

void Table_SetTable(lua_State* L, Table* table, const char* key, Value* value)
{
    Value k;
    SetValue( &k, String_Create(L, key) );
    Table_SetTable(L, table, &k, value);
}

bool Table_Update(lua_State* L, Table* table, Value* key, Value* value)
{

    if (Value_GetIsNil(value))
    {
        Table_Remove(table, key);
        return true;
    }

    TableNode* node = Table_GetNode(table, key);
    if (node == NULL)
    {
        return false;
    }

    node->value = *value;
    Gc_WriteBarrier(L, table, value);
    return true;

}

void Table_Insert(lua_State* L, Table* table, Value* key, Value* value)
{

    if (table->numNodes == 0)
    {
        Table_Resize(L, table, 2);
    }

Start:

    Gc_WriteBarrier(L, table, key);
    Gc_WriteBarrier(L, table, value);

    size_t index = Table_GetMainIndex(table, key);
    TableNode* node = &table->nodes[index];

    if ( Table_NodeIsEmpty(node) )
    {
        node->key   = *key;
        node->value = *value;
        node->next  = NULL;
        node->dead  = false;
    }
    else
    {

        // Need to insert a new node into the table.
        TableNode* freeNode = Table_GetFreeNode(table);
        if (freeNode == NULL)
        {
            // Table is full, so resize.
            if (Table_Resize(L, table, table->numNodes * 2))
            {
                goto Start;
            }
        }

        // Something else is in our primary slot, check if it's in its
        // primary slot.
        size_t collisionIndex = Table_GetMainIndex(table, &node->key);
        if (index != collisionIndex)
        {

            // Update the previous node in the chain. 
            TableNode* prevNode = &table->nodes[collisionIndex];
            assert( !Table_NodeIsEmpty(prevNode) );
            if (prevNode->next == node)
            {
                prevNode->next = freeNode;
            }

            // The object in its current spot is not it's primary index,
            // so we can freely move it somewhere else.
            *freeNode = *node;
            node->key   = *key;
            node->value = *value;
            node->next  = freeNode;
            node->dead  = false;
            assert(node->next != node);

        }
        else
        {
            // The current slot is the primary index, so add our new key into a
            // the free slot and chain it to the other node.
            freeNode->key   = *key;
            freeNode->value = *value;
            freeNode->dead  = false;
            assert(node->next != freeNode);
            freeNode->next  = node->next;
            node->next      = freeNode;
            assert(node->next != node);
        }

    }

    assert( Table_CheckConsistency(table) );

}

void Table_SetTable(lua_State* L, Table* table, Value* key, Value* value)
{
    if (!Table_Update(L, table, key, value))
    {
        Table_Insert(L, table, key, value);
    }
}

Value* Table_GetTable(lua_State* L, Table* table, String* key)
{
    Value k;
    SetValue(&k, key);
    return Table_GetTable(L, table, &k);
}

Value* Table_GetTable(lua_State* L, Table* table, const Value* key)
{
    TableNode* node = Table_GetNode(table, key);
    if (node == NULL)
    {
        return NULL;
    }
    assert( !Table_NodeIsEmpty(node) );
    return &node->value;
}

Value* Table_GetTable(lua_State* L, Table* table, int key)
{
    // TODO: Implement fast array access.
    Value value;
    SetValue( &value, key );
    return Table_GetTable(L, table, &value);
}

int Table_GetSize(lua_State* L, Table* table)
{

    // Find min, max such that min is non-nil and max is nil. These will
    // bracket our length.
    
    int min = 0;
    int max = 1;

    while (1)
    {
        // Check if max is non-nil. If it is, then move min up to max and
        // advance max.
        const Value* value = Table_GetTable(L, table, max);
        if (value == NULL || Value_GetIsNil(value))
        {
            break;
        }
        else
        {
            min = max;
            max *= 2;
        }
    }

    // Binary search between min and max to find the actual length.
    while (max > min + 1)
    {
        int mid = (min + max) / 2;
        const Value* value = Table_GetTable(L, table, mid);
        if (value == NULL || Value_GetIsNil(value)) 
        {
            max = mid;
        }
        else
        {
            min = mid;
        }
    }

    return min;

}

const Value* Table_Next(Table* table, Value* key)
{
    
    int index = 0;
    if (!Value_GetIsNil(key))
    {
        
        // Note, we compare even for dead keys so that we can continue iterating
        // after deleting a key from the table.
        TableNode* node = Table_GetNodeIncludingDead(table, key);
        if (node == NULL)
        {
            return NULL;
        }

        // Start from the next slot after the last key we encountered.
        index = static_cast<int>(node - table->nodes) + 1;
    }

    while (index < table->numNodes && Value_GetIsNil(&table->nodes[index].key))
    {
        ++index;
    }

    if (index < table->numNodes)
    {
        TableNode* node = &table->nodes[index];
        *key = node->key;
        return &node->value;
    }

    return NULL;

}