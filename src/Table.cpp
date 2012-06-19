/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#include "Table.h"
#include "String.h"

#include <stdio.h>
#include <malloc.h>

namespace
{
    // A 4 element array takes as much room as a single has table node, so
    // don't create an array of smaller size.
    const int _minArraySize = 4;
}

// This define will check that the table is in a correct state after each
// modification. It's helpful for debugging, but it's very slow.
//#define TABLE_CHECK_CONSISTENCY

// Enables array optimization for tables. Can be useful to disable to isolate
// problems.
#define TABLE_ARRAY

static void Table_InsertHash(lua_State* L, Table* table, Value* key, Value* value);
static bool Table_WriteDot(const Table* table, const char* fileName);

template <class T>
static inline void Swap(T& a, T& b)
{
    T temp = a;
    a = b;
    b = temp;
}

Table* Table_Create(lua_State* L, int numArray, int numHash)
{
    Table* table = static_cast<Table*>( Gc_AllocateObject(L, LUA_TTABLE, sizeof(Table)) );
    table->numNodes         = 0;
    table->nodes            = NULL;
    table->numElementsSet   = 0;
    table->maxElements      = 0;
    table->numElements      = 0;
    table->element          = NULL;
    table->minHashKey       = INT_MAX;
    table->metatable        = NULL;
    table->size             = 0;
    // TODO: Initialize the array and hash parts based on the parameters.
    return table;
}

void Table_Destroy(lua_State* L, Table* table)
{
    Free(L, table->nodes, table->numNodes * sizeof(TableNode));
    Free(L, table->element, table->maxElements * sizeof(Value));
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

        // Check that a key in the hash part doesn't overlap the array part.
        if (!node->dead)
        {
            int key;
            if (Value_GetIsInteger(&node->key, &key))
            {
                if (key >= 1 && key <= table->maxElements)
                {
                    ASSERT(0);
                    return false;
                }
            }
        }

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

    // Check the array.

    int numElementsSet = 0;
    for (int i = 0; i < table->numElements; ++i)
    {
        if (!Value_GetIsNil(&table->element[i]))
        {
            ++numElementsSet;
        }
    }
    if (numElementsSet != table->numElementsSet)
    {
        ASSERT(0);
        return false;
    }

    // Check the size
    if (table->size < 0 || table->size > table->numElements)
    {
        ASSERT(0);
        return false;
    }
    if (table->size > 0)
    {
        if (Value_GetIsNil(&table->element[table->size - 1]))
        {
            ASSERT(0);
            return false;
        }
        if (table->size < table->numElements && !Value_GetIsNil(&table->element[table->size]))
        {
            ASSERT(0);
            return false;
        }
    }

    return true;

}

/** Rounds up to the next power of 2. */
static unsigned int RoundUp2(unsigned int v)
{

    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;

    return v;

}

/** Returns the log base 2 rounded down. */
static int Log2(unsigned int v)
{
    static const int multiplyDeBruijnBitPosition[32] = 
        {
            0, 9, 1, 10, 13, 21, 2, 29, 11, 14, 16, 18, 22, 25, 3, 30,
            8, 12, 20, 28, 15, 17, 24, 7, 19, 27, 23, 6, 26, 5, 4, 31
        };

    v |= v >> 1; // first round down to one less than a power of 2 
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;

    return multiplyDeBruijnBitPosition[(unsigned int)(v * 0x07C4ACDDU) >> 27];
}

static void Table_AllocateArray(lua_State* L, Table* table, int maxElements)
{
    size_t oldSize = table->maxElements * sizeof(Value);
    size_t newSize = maxElements * sizeof(Value);

    table->element = static_cast<Value*>(Reallocate(L, table->element, oldSize, newSize));
    table->maxElements = maxElements;
}

/**
 * If force is true, the hash will be rebuilt regardless of whether or not the
 * number of nodes has changed. This can be used to clear out dead nodes.
 */
static bool Table_ResizeHash(lua_State* L, Table* table, int numNodes, bool force)
{

    if (table->numNodes == numNodes &&!force)
    {
        return true;
    }
    
    size_t size = numNodes * sizeof(TableNode);
    TableNode* nodes = static_cast<TableNode*>( Allocate(L, size) );

    if (nodes == NULL && numNodes != 0)
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

    if (table->numNodes != 0)
    {
        for (int i = 0; i < numNodes; ++i)
        {
            if ( !Table_NodeIsEmpty(&nodes[i]) )
            {
                Table_InsertHash(L, table, &nodes[i].key, &nodes[i].value);
            }
        }
    }

    Free(L, nodes, numNodes * sizeof(TableNode));
    
#ifdef TABLE_CHECK_CONSISTENCY
    ASSERT( Table_CheckConsistency(table) );
#endif

    return true;

}

static void Table_InitializeArrayElements(Table* table, int numElements)
{
    Value* start = table->element + table->numElements;
    Value* end   = table->element + numElements;
    Value_SetRangeNil(start, end);
    table->numElements = numElements;
}

/**
 * Expands the array if necessary to include elements that were previously
 * in the hash part of the array.
 */
static void Table_RebuildArray(lua_State* L, Table* table, int maxElements)
{

    // count[i] stores the number of integer keys between 2^i and 2^(i+1).
    int count[LUAI_BITSINT] = { 0 };

    // Count the array elements.
    Value* element = table->element;
    for (int i = 0; i < table->numElements; ++i)
    {
        if (!Value_GetIsNil(element))
        {
            ++count[Log2(i + 1)];
        }
        ++element;
    }

    // Count the hash elements.

    const int maxStackNodes = 1024;
    bool stackAllocation = table->numNodes < maxStackNodes;
    
    int* nodeKey;
    if (stackAllocation)
    {
        nodeKey = static_cast<int*>(alloca( table->numNodes * sizeof(int) ));
    }
    else
    {
        nodeKey = static_cast<int*>(Allocate(L, table->numNodes * sizeof(int) ));
    }

    TableNode* node = table->nodes;
    int numNodes = 0;
    for (int i = 0; i < table->numNodes; ++i)
    {
        int key;
        if (!Table_NodeIsEmpty(node))
        {
            // Use & instead of && to reduce branching.
            if (Value_GetIsInteger(&node->key, &key) & (key > 0))
            {
                ++count[Log2(key)];
                nodeKey[i] = key;
            }
            else
            {
                nodeKey[i] = -1;
            }
            ++numNodes;
        }
        else
        {
            nodeKey[i] = -1;
        }
        ++node;
    }

    int numElementsSet = 0;

    for (int i = 0; i < LUAI_BITSINT; ++i)
    {
        numElementsSet += count[i];
        int size = 1 << (i + 1);
        if (size > maxElements)
        {
            if (size > numElementsSet * 2)
            {
                break;
            }
            maxElements = size;
        }
    }

    Table_AllocateArray(L, table, maxElements);
    Table_InitializeArrayElements(table, maxElements);

    // Move hash elements which fall into the array and recompute the min hash
    // key for the elements that don't.

    table->minHashKey = INT_MAX;
    node = table->nodes;

    int numNodesMoved = 0;

    for (int i = 0; i < table->numNodes; ++i)
    {
        int key = nodeKey[i];
        if (key > 0)
        {
            if (key <= maxElements)
            {
                Value* dst = &table->element[key - 1];
                ASSERT( Value_GetIsNil(dst) );
                *dst = node->value;
                if (key > table->size)
                {
                    table->size = key;
                }
                // Note, just setting the node to dead will leave the table in an
                // invalid state, but that's ok because we're going to immediately
                // rehash it which doesn't rely on it being in a valid state.
                node->dead = true;
                ++numNodesMoved;
            }
            else if (key < table->minHashKey)
            {
                table->minHashKey = key;
            }
        }
        ++node;
    }    

    table->numElementsSet += numNodesMoved;
    
    if (!stackAllocation)
    {
        Free(L, nodeKey, table->numNodes * sizeof(int));
    }

    numNodes = RoundUp2(numNodes - numNodesMoved);
    Table_ResizeHash(L, table, numNodes, true);

#ifdef TABLE_CHECK_CONSISTENCY
    Table_CheckConsistency(table);
#endif

}

/**
 * Resizes the array portion of the table to be big enough to include the
 * specified number of elements. 
 */
static void Table_ResizeArray(lua_State* L, Table* table, int numElements)
{

    // Adjust the number of elements to a power of 2 to prevent constant
    // resizing of an array when we're appending.
    int maxElements = RoundUp2(numElements);

    if (maxElements < _minArraySize)
    {
        maxElements = _minArraySize;
    }

    // If we resized the array to overlap keys that are stored in the hash part
    // we need to move them into the array part.
    if (maxElements >= table->minHashKey)
    {
        Table_RebuildArray(L, table, maxElements);
    }
    else
    {
        Table_AllocateArray(L, table, maxElements);
    }

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

static bool Table_RemoveHash(Table* table, const Value* key)
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
    Table_CheckConsistency(table);
#endif

    return true;

}

static bool Table_Remove(Table* table, int key)
{

    if (key > 0 && key <= table->maxElements)
    {

        Value* dst = table->element + key - 1;
        if (key > table->numElements || Value_GetIsNil(dst))
        {
            return false;
        }
        SetNil(dst);
        --table->numElementsSet;

        // If we removed the last non-nil element in the array, we need to
        // update the size.
        if (key == table->size)
        {
            int size = table->size;
            while (size > 0 && Value_GetIsNil(&table->element[size - 1]))
            {
                --size;
            }
            table->size = size;
        }

    #ifdef TABLE_CHECK_CONSISTENCY
        Table_CheckConsistency(table);
    #endif

        return true;
    }

    Value k;
    SetValue(&k, key);
    return Table_RemoveHash(table, &k);

}

static bool Table_Remove(Table* table, const Value* key)
{
    int index;
    if (Value_GetIsInteger(key, &index))
    {
        return Table_Remove(table, index);
    }
    return Table_RemoveHash(table, key);
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

/**
 * Updates a key in the hash part of the table.
 */
bool Table_UpdateHash(lua_State* L, Table* table, Value* key, Value* value)
{

    TableNode* node = Table_GetNode(table, key);
    if (node == NULL)
    {
        return false;
    }

    node->value = *value;
    Gc_WriteBarrier(&L->gc, table, value);
    return true;
}

FORCE_INLINE bool Table_Update(lua_State* L, Table* table, int key, Value* value)
{

    if (Value_GetIsNil(value))
    {
        Value k;
        SetValue(&k, key);
        return Table_Remove(table, &k);
    }

#ifdef TABLE_ARRAY
    // Check if we're updating a value in the array part. Use of & instead of &&
    // to reduce branching.
    if ((key > 0) & (key <= table->maxElements))
    {

        // Check if the element is beyond the initialized range, and therefore
        // should have a nil value.
        if (key > table->numElements)
        {
            return false;
        }

        Value* dst = table->element + key - 1;
        if (Value_GetIsNil(dst))
        {
            return false;
        }

        *dst = *value;
        Gc_WriteBarrier(&L->gc, table, value);
        return true;

    }
#endif

    // Fall back to the hash part. If there is no hash part, this will simply
    // return false.
    Value k;
    SetValue(&k, key);
    return Table_UpdateHash(L, table, &k, value);

}

bool Table_Update(lua_State* L, Table* table, Value* key, Value* value)
{

    int index;
    if (Value_GetIsInteger(key, &index))
    {
        return Table_Update(L, table, index, value);;
    }

    if (Value_GetIsNil(value))
    {
        return Table_Remove(table, key);
    }

    return Table_UpdateHash(L, table, key, value);

}

void Table_SetTable(lua_State* L, Table* table, int key, Value* value)
{
    if (!Table_Update(L, table, key, value) && !Value_GetIsNil(value))
    {
        Table_Insert(L, table, key, value);
    }
}

void Table_SetTable(lua_State* L, Table* table, const char* key, Value* value)
{
    Value k;
    SetValue( &k, String_Create(L, key) );
    Table_SetTable(L, table, &k, value);
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

static void Table_InsertHash(lua_State* L, Table* table, Value* key, Value* value)
{

    ASSERT( !Value_GetIsNil(value) );

    if (table->numNodes == 0)
    {
        Table_ResizeHash(L, table, 2, false);
    }

Start:

    Gc_WriteBarrier(&L->gc, table, key);
    Gc_WriteBarrier(&L->gc, table, value);

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
            if (Table_ResizeHash(L, table, table->numNodes * 2, false))
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
    Table_CheckConsistency(table);
#endif

}

/** Assigns a value to a previously unassigned element in the array. */
FORCE_INLINE static void Table_AssignArray(lua_State* L, Table* table, int index, Value* value)
{

    Value* element = table->element;
    ASSERT( index < table->maxElements );

    if (index == table->numElements)
    {
        // Fast case: appending an element to the array.
        table->numElements = index + 1;
    }
    else if (index > table->numElements)
    {
        // Slower case: skipping some elements in the array.
        Table_InitializeArrayElements(table, index + 1);
    }

    if (index >= table->size)
    {
        table->size = index + 1;
    }

    element[index] = *value;
    Gc_WriteBarrier(&L->gc, table, value);

    ++table->numElementsSet;

#ifdef TABLE_CHECK_CONSISTENCY
    Table_CheckConsistency(table);
#endif

}

void Table_Insert(lua_State* L, Table* table, int key, Value* value)
{

#ifdef TABLE_ARRAY
	if (key > 0)
	{

        int index = key - 1;
        if (index < table->maxElements)
		{
            Table_AssignArray(L, table, index, value);
			return;
		}

		// Check if we can resize the array to include this element, and still
        // have fewer than half of the array elements be empty.
		if (index <= table->numElementsSet * 2)
		{
			Table_ResizeArray(L, table, key);
            Table_AssignArray(L, table, index, value);
            return;
		}

	}
#endif
	
	// The index is out of bounds for storing in the arary, so store in the hash.
    Value temp;
    SetValue(&temp, key);
    Table_InsertHash(L, table, &temp, value);

    if ((key > 0) && (key < table->minHashKey))
    {
        table->minHashKey = key;
    }
	
}

void Table_Insert(lua_State* L, Table* table, Value* key, Value* value)
{
    // If we have an integer type, use the specialized integer version.
    int index;
    if (Value_GetIsInteger(key, &index))
    {
        Table_Insert(L, table, index, value);
    }
    else
    {
        Table_InsertHash(L, table, key, value);
    }
}

void Table_SetTable(lua_State* L, Table* table, Value* key, Value* value)
{
    if (!Table_Update(L, table, key, value) && !Value_GetIsNil(value))
    {
        Table_Insert(L, table, key, value);
    }
}

/**
 * Looks up a key in the hash part of the table. Returns a nil object if the
 * key was not found.
 */
FORCE_INLINE static Value* Table_GetTableHash(lua_State* L, Table* table, const Value* key)
{

    TableNode* node = Table_GetNode(table, key);
    if (node == NULL)
    {
        // Return the nil object.
        return &L->dummyObject;
    }
    ASSERT( !Table_NodeIsEmpty(node) );
    return &node->value;

}

Value* Table_GetTable(lua_State* L, Table* table, String* key)
{
    Value k;
    SetValue(&k, key);
    return Table_GetTableHash(L, table, &k);
}

Value* Table_GetTable(lua_State* L, Table* table, int key)
{

    int index = key - 1;
    if (index >= 0 && index < table->numElements)
    {
        return table->element + index;
    }

    // If the index greater than numElements but less than maxElements
    // we know it is nil and don't need to check the hash, but probably not a 
    // common case so it's not worth incurring additional overhead in the more
    // common case of a sparse table.
    
    Value k;
    SetValue( &k, key );
    return Table_GetTableHash(L, table, &k);

}

Value* Table_GetTable(lua_State* L, Table* table, const Value* key)
{
    int index;
    if (Value_GetIsInteger(key, &index))
    {
        return Table_GetTable(L, table, index);
    }
    return Table_GetTableHash(L, table, key);
}

int Table_GetSize(lua_State* L, Table* table)
{

    if (table->numElements > 0)
    {
        // If we have an array part, just use the size of that.
        return table->size;
    }

    // Find min, max such that min is non-nil and max is nil. These will
    // bracket our length.
    
    int min = 0;
    int max = 1;

    while (1)
    {
        // Check if max is non-nil. If it is, then move min up to max and
        // advance max.
        const Value* value = Table_GetTable(L, table, max);
        if (Value_GetIsNil(value))
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
        if (Value_GetIsNil(value)) 
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

static int Table_GetIterationIndex(lua_State* L, Table* table, const Value* key)
{

    if (Value_GetIsNil(key))
    {
        // Start at the beginning.
        return -1;
    }

    // Check if we're in the array part.
    int index;
    if (Value_GetIsInteger(key, &index) && index > 0 && index <= table->numElements)
    {
        return index - 1;
    }

    // Check if we're in the hash part.
    TableNode* node = Table_GetNodeIncludeDead(table, key);
    if (node == NULL)
    {
        // If we didn't find anything, that the key passed in was not from an
        // earlier call to Table_Next.
        State_Error(L, "invalid key to next");
    }
    return static_cast<int>(node - table->nodes) + table->numElements;

}

const Value* Table_Next(lua_State* L, Table* table, Value* key)
{
    
    // Start from the next index after the last one we encountered.
    int index = Table_GetIterationIndex(L, table, key) + 1;
      
    // Try iterating in the array part.
    int numElements = table->numElements;
    while (index < numElements)
    {
        Value* value = table->element + index;
        if (!Value_GetIsNil(value))
        {
            SetValue(key, index + 1);
            return value;
        }
        ++index;
    }
    
    // Try iterating in the hash part.
    int numNodes = table->numNodes;
    index -= table->numElements;
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

    // Finished iterating.
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