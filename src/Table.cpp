/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#include "Table.h"
#include "String.h"

#include <stdio.h>

// This define will check that the table is in a correct state after each
// modification. It's helpful for debugging, but it's very slow.
//#define TABLE_CHECK_CONSISTENCY

static bool Table_WriteDot(const Table* table, const char* fileName);

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

FORCE_INLINE static unsigned int Hash(const Value* key)
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

FORCE_INLINE static bool KeysEqual(const Value* key1, const Value* key2)
{
    if (key1->tag != key2->tag)
    {
        return false;
    }
    return key1->object == key2->object;
}

FORCE_INLINE static size_t Table_GetMainIndex(const Table* table, const Value* key)
{
    return Hash(key) & (table->numNodes - 1);
}

FORCE_INLINE static bool Table_NodeIsEmpty(const TableNode* node)
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

        if (Value_GetIsNil(&node->key))
        {
            // If a node has a nil key it must be dead.
            if (!node->dead)
            {
                ASSERT(0);
                return false;
            }
        }
        else
        {

            // Check that all of the "next" pointers point to a valid element
            if (node->next != NULL && !Table_GetIsValidNode(table, node->next))
            {
                ASSERT(0);
                return false;
            }

            // Check that the "next" pointer is correct.
            if (node->next != NULL && node->next->dead)
            {
                if (!Table_GetIsValidNode(table, node->next->prev))
                {
                    ASSERT(0);
                    return false;
                }
                if (node->next->prev != node)
                {
                    ASSERT(0);
                    return false;
                }
            }

            // Check that the "prev" pointer is correct for a dead node.
            if (node->dead && node->prev != NULL)
            {
                if (!Table_GetIsValidNode(table, node->prev))
                {
                    ASSERT(0);
                    return false;
                }
                if (node->prev->next != node)
                {
                    ASSERT(0);
                    return false;
                }
            }

            // Check the invariant that either this node is in its main index,
            // or the element in its main index is in its *own* main index.

            size_t mainIndex = Table_GetMainIndex(table, &node->key);
            const TableNode* collidingNode = &table->nodes[i];

            if (collidingNode != node)
            {
                if (Table_GetMainIndex(table, &collidingNode->key) != mainIndex)
                {
                    ASSERT(0);
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
                    ASSERT(0);
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
        nodes[i].dead = true;
        nodes[i].next = NULL;
        nodes[i].prev = NULL;
    }
        
    // Rehash all of the nodes.

    Swap(table->numNodes, numNodes);
    Swap(table->nodes, nodes);

    for (int i = 0; i < numNodes; ++i)
    {
        if ( !Table_NodeIsEmpty(&nodes[i]) )
        {
            Table_Insert(L, table, &nodes[i].key, &nodes[i].value);
        }
    }

    Free(L, nodes, numNodes * sizeof(TableNode));
    
#ifdef TABLE_CHECK_CONSISTENCY
    ASSERT( Table_CheckConsistency(table) );
#endif

    return true;

}

static TableNode* Table_GetNodeIncludeDead(Table* table, const Value* key)
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

/**
 * Returns the node in the table that has the specified key, or NULL if the key
 * does not appear in the table.
 */
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

/**
 * Returns the node in the table that has the specified key, or NULL if the key
 * does not appear in the table. The node before that node in the linked chain
 * is stored in prevNode.
 */
static TableNode* Table_GetNode(Table* table, const Value* key, TableNode*& prevNode)
{

    if (table->numNodes == 0)
    {
        return NULL;
    }
  
    size_t index = Table_GetMainIndex(table, key);
    TableNode* node = &table->nodes[index];
    TableNode* prev = NULL;

    while ( node != NULL && (node->dead || !KeysEqual(&node->key, key)) )
    {
        prev = node;
        node = node->next;
    }

    prevNode = prev;
    return node;

}

static bool Table_Remove(Table* table, const Value* key)
{

    TableNode* prev = NULL;
    TableNode* node = Table_GetNode(table, key, prev);

    if (node == NULL)
    {
        return false;
    }

    node->dead = true;
    node->prev = prev;

#ifdef TABLE_CHECK_CONSISTENCY
    ASSERT( Table_CheckConsistency(table) );
#endif

    return true;

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
        return Table_Remove(table, key);
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

static TableNode* Table_UnlinkDeadNode(Table* table, TableNode* node)
{
    ASSERT(node->dead);

    if (node->prev != NULL)
    {
        // This node is in the middle of a list, so just unhook it from the
        // previous and next nodes.
        node->prev->next = node->next;
        if (node->next != NULL && node->next->dead)
        {
            node->next->prev = node->prev;
        }
    }
    else
    {
        // This is the head of the list. We can't unlink it from the chain since
        // nothing will point to the rest of the list, so move another node the
        // head of the list.
        TableNode* next = node->next;
        if (next != NULL)
        {
            *node = *next;
            if (node->dead)
            {
                node->prev = NULL;
            }
            if (node->next != NULL && node->next->dead)
            {
                node->next->prev = node;
            }
            node = next;
        }
    }

    return node;
}

void Table_Insert(lua_State* L, Table* table, Value* key, Value* value)
{

    ASSERT( !Value_GetIsNil(value) );

    if (table->numNodes == 0)
    {
        Table_Resize(L, table, 2);
    }

Start:

    Gc_WriteBarrier(L, table, key);
    Gc_WriteBarrier(L, table, value);

    size_t index = Table_GetMainIndex(table, key);
    TableNode* node = &table->nodes[index];

    if ( node->dead )
    {
        // If this node is in another list, we need to remove it from that list
        // since our new node shouldn't be part of that list.
        if (node->prev != NULL)
        {
            node->prev->next = node->next;
            if (node->next != NULL && node->next->dead)
            {
                node->next->prev = node->prev;
            }
            node->next = NULL;
        }
        node->dead  = false;
        node->key   = *key;
        node->value = *value;
    }
    else
    {

        ASSERT(!node->dead);

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
        freeNode = Table_UnlinkDeadNode(table, freeNode);

        if (freeNode == node)
        {
            // The thing we were colliding with was moved in the unlinking
            // process, so we can just insert our data into the free node.
            freeNode->key   = *key;
            freeNode->value = *value;
            freeNode->dead  = false;
            freeNode->next  = NULL;
        }
        else
        {

            // Something else is in our primary slot, check if it's in its
            // primary slot.
            size_t collisionIndex = Table_GetMainIndex(table, &node->key);
            if (index != collisionIndex)
            {

                // Update the previous node in the chain.
                TableNode* prevNode = &table->nodes[collisionIndex];
                while (prevNode->next != node)
                {
                    prevNode = prevNode->next;
                }
                prevNode->next = freeNode;

                // The object in its current spot is not it's primary index,
                // so we can freely move it somewhere else.
                *freeNode = *node;
                node->key   = *key;
                node->value = *value;
                node->next  = NULL;
                node->dead  = false;

                if (freeNode->next != NULL && freeNode->next->dead)
                {
                    freeNode->next->prev = freeNode;
                }

            }
            else
            {
                // The current slot is the primary index, so add our new key into a
                // the free slot and chain it to the other node.
                freeNode->key   = *key;
                freeNode->value = *value;
                freeNode->dead  = false;
                freeNode->next  = node->next;
                node->next      = freeNode;

                if (freeNode->next != NULL && freeNode->next->dead)
                {
                    freeNode->next->prev = freeNode;
                }
            }

        }

    }

#ifdef TABLE_CHECK_CONSISTENCY
    ASSERT( Table_CheckConsistency(table) );
#endif

}

void Table_SetTable(lua_State* L, Table* table, Value* key, Value* value)
{
    if (!Table_Update(L, table, key, value) && !Value_GetIsNil(value))
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
    ASSERT( !Table_NodeIsEmpty(node) );
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
        TableNode* node = Table_GetNodeIncludeDead(table, key);
        if (node == NULL)
        {
            return NULL;
        }
        // Start from the next slot after the last key we encountered.
        index = static_cast<int>(node - table->nodes) + 1;
    }

    int numNodes = table->numNodes;
    while (index < numNodes && table->nodes[index].dead)
    {
        ++index;
    }

    if (index < numNodes)
    {
        TableNode* node = &table->nodes[index];
        *key = node->key;
        return &node->value;
    }

    return NULL;

}

/**
 * Writes the table in the dot format that can be visualized by graphviz. This
 * is useful for debugging.
 */
static bool Table_WriteDot(const Table* table, const char* fileName)
{

    FILE* file = fopen(fileName, "wt");

    if (file == NULL)
    {
        return false;
    }

    fprintf(file, "digraph G {\n");
    fprintf(file, "nodesep=.05;\n");
    fprintf(file, "rankdir=LR;\n");
    fprintf(file, "node [shape=none, margin=0];\n");
    fprintf(file, "table [label=<");
    fprintf(file, "<table>\n");

    for (int i = 0; i < table->numNodes; ++i)
    {

        const TableNode* node = &table->nodes[i];

        char buffer[256];
        int  type = Value_GetType(&node->key);

        if (type == LUA_TLIGHTUSERDATA)
        {
            sprintf(buffer, "%p", node->key.lightUserdata);
        }
        else if (Value_GetIsObject(&node->key))
        {
            sprintf(buffer, "%p", node->key.object);
        }
        else if (Value_GetIsNil(&node->key))
        {
            sprintf(buffer, "nil");
        }
        else
        {
            sprintf(buffer, "???");
        }

        if (node->dead)
        {
            fprintf(file, "<tr><td port=\"f%d\" bgcolor=\"#FF0000\">%s</td></tr>\n", i, buffer);
        }
        else
        {
            fprintf(file, "<tr><td port=\"f%d\">%s</td></tr>\n", i, buffer);
        }

    }

    fprintf(file, "</table>");
    fprintf(file, ">, height=2.0];\n");

    bool prevLabeled = false;
    bool nextLabeled = false;

    for (int i = 0; i < table->numNodes; ++i)
    {

        const TableNode* node = &table->nodes[i];

        if (node->next != NULL)
        {
            const char* label = "";
            if (!nextLabeled)
            {
                label = "next";
                nextLabeled = true;
            }

            int j = static_cast<int>(node->next - table->nodes);
            fprintf(file, "\"table\":f%d:w -> \"table\":f%d:w [colorscheme=set17, color=%d, label=\"%s\"];\n", i, j, i % 10, label);
        }

        if (node->dead && node->prev != NULL)
        {
            const char* label = "";
            if (!prevLabeled)
            {
                label = "prev";
                prevLabeled = true;
            }
            int j = static_cast<int>(node->prev - table->nodes);
            fprintf(file, "\"table\":f%d:e -> \"table\":f%d:e [colorscheme=set17, color=%d, label=\"%s\"];\n", i, j, i % 10, label);
        }

    }

    fprintf(file, "}");
    fclose(file);

    return true;

}