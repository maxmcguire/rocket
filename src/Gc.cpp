/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#include "Gc.h"
#include "State.h"
#include "Function.h"
#include "Table.h"
#include "UserData.h"
#include "Parser.h"
#include "UpValue.h"

#include <stdio.h>

namespace
{
    const size_t _gcThreshold = 1024 * 1024 * 5;
}

// Disables the garbage collector. This can be useful for debugging garbage
// collector issues, but is not something that you would want to do otherwise.
//#define GC_DISABLE

/**
 * Checks if the garbage collector needs to be run.
 */
static void Gc_Check(lua_State* L, Gc* gc)
{
    if (L->totalBytes >= gc->threshold)
    {
        if (gc->state == Gc_State_Paused)
        {
            gc->state = Gc_State_Young;
            Gc_Step(L, gc);
            ASSERT(gc->state == Gc_State_Paused);
            if (L->totalBytes < gc->threshold)
            {
                return;
            }
            // If the young collector didn't free up enough memory, start the
            // mark and sweep collector.
            gc->state = Gc_State_Start;
        }
        Gc_Step(L, gc);
    }
}

/**
 * Reclaims the memory for an object. The releaseRefs parameter is set to true
 * to indicate whether or not we need to decremement the reference count for
 * child objects. If the object is being freed as part of the mark and sweep,
 * we know children are unreachable and therefore don't need to be decremented,
 * since they will also be released by the mark and sweep.
 */
static void Gc_FreeObject(lua_State* L, Gc* gc, Gc_Object* object, bool releaseRefs)
{

    // Remove from the global object list.
    if (object->next != NULL)
    {
        object->next->prev = object->prev;
    }
    if (object->prev != NULL)
    {
        object->prev->next = object->next;
    }
    else
    {
        gc->first = object->next;
    }

    switch (object->type)
    {
    case LUA_TSTRING:
        StringPool_Remove( L, &L->stringPool, static_cast<String*>(object) );
        String_Destroy( L, static_cast<String*>(object) );
        break;
    case LUA_TTABLE:
        Table_Destroy( L, static_cast<Table*>(object), releaseRefs );
        break;
    case LUA_TFUNCTION:
        Closure_Destroy(L, static_cast<Closure*>(object), releaseRefs );
        break;
    case LUA_TPROTOTYPE:
        Prototype_Destroy(L, static_cast<Prototype*>(object), releaseRefs );
        break;
    case LUA_TFUNCTIONP:
        Function_Destroy(L, static_cast<Function*>(object), releaseRefs );
        break;
    case LUA_TUPVALUE:
        UpValue_Destroy(L, static_cast<UpValue*>(object), releaseRefs );
        break;
    case LUA_TUSERDATA:
        UserData_Destroy(L, static_cast<UserData*>(object), releaseRefs );
        break;
    default:
        ASSERT(0);
    }

#ifdef DEBUG
    --gc->_numObjects;
#endif

}

void Gc_Initialize(lua_State* L, Gc* gc)
{
    gc->first       = NULL;
    gc->firstGrey   = NULL;
    gc->state       = Gc_State_Paused;
    gc->threshold   = _gcThreshold;
    gc->scanMark    = 0;

    // Compute the size of the young object array so that it's unlikely we'll
    // overflow it before hitting our threshold for running the young collector.
    const size_t averageObjectPayload = 100;
    gc->maxYoungObjects = _gcThreshold / (sizeof(Gc_Object) + averageObjectPayload);
    gc->numYoungObjects = 0;
    gc->youngObject = static_cast<Gc_Object**>(Allocate(L, sizeof(Gc_Object*) * gc->maxYoungObjects));

#ifdef DEBUG
    gc->_numObjects  = 0;
#endif
}

void Gc_Shutdown(lua_State* L, Gc* gc)
{

    // Free all of the objects.
    Gc_Object* object =  gc->first;
    while (object != NULL)
    {
        Gc_Object* nextObject = object->next;
        Gc_FreeObject(L, gc, object, false);
        object = nextObject;
    }

    gc->first = NULL;
    gc->firstGrey = NULL;

    Free(L, gc->youngObject, sizeof(Gc_Object*) * gc->maxYoungObjects);
    gc->youngObject = NULL;

}

void* Gc_AllocateObject(lua_State* L, int type, size_t size)
{

    Gc* gc = &L->gc;
    Gc_Check(L, gc);

    Gc_Object* object = static_cast<Gc_Object*>(Allocate(L, size));
    if (object == NULL)
    {

        // Emergency run of the garbage collector to free up memory.
        Gc_Collect(L, gc);

        object = static_cast<Gc_Object*>(Allocate(L, size));
        if (object == NULL)
        {
            // Out of memory!
            State_Error(L);
        }

    }

    object->refCount    = 0;
    object->young       = false;
    object->scanMark    = gc->scanMark;

    object->nextGrey    = NULL;
    object->type        = type;

    if (gc->first != NULL)
    {
        gc->first->prev = object;
    }

    object->next = gc->first;
    object->prev = NULL;

    gc->first = object;

#ifdef DEBUG
    ++gc->_numObjects;
#endif

    if (gc->state == Gc_State_Finish)
    {
        // If we've already finished marking but have not done the sweep, we need
        // to make the object black to prevent it from being collected.
        object->color = Color_Black;
    }
    else
    {
        // If we are not in a GC cycle or haven't finished propagating, then
        // we'll either color this object with a write barrier or when we rescan
        // the stack during finalization (or it will be garbage).
        object->color = Color_White;
        if (gc->state == Gc_State_Paused)
        {
            Gc_AddYoungObject(L, gc, object);
        }
    }

    return object;

}

void Gc_MarkObject(Gc* gc, Gc_Object* object)
{
    if (object->color == Color_White)
    {
        // Add to the grey list.
        object->color = Color_Grey;
        object->nextGrey = gc->firstGrey;
        gc->firstGrey = object;
    }
}

static void Gc_MarkValue(Gc* gc, Value* value)
{
    if (Value_GetIsObject(value))
    {
        Gc_Object* object = value->object;
        Gc_MarkObject(gc, object);
    }
}

static void Gc_MarkRoots(lua_State* L, Gc* gc)
{

    Value* stackTop = L->stackTop;

    // Mark the functions on the call stack.
    CallFrame* frame = L->callStackBase;
    CallFrame* callStackTop = L->callStackTop;
    while (frame < callStackTop)
    {
        Gc_MarkValue(gc, frame->function);
        if (frame->stackTop > stackTop)
        {
            stackTop = frame->stackTop;
        }
        ++frame;
    }

    // Mark the objects on the stack.
    Value* value = L->stack;
    while (value < stackTop)
    {
        Gc_MarkValue(gc, value);
        ++value;
    }
    
    // Mark the global tables.
    Gc_MarkValue(gc, &L->globals);
    Gc_MarkValue(gc, &L->registry);

    for (int i = 0; i < NUM_TYPES; ++i)
    {
        if (L->metatable[i] != NULL)
        {
            Gc_MarkObject(gc, L->metatable[i]);
        }
    }

}

static bool Gc_Propagate(Gc* gc)
{

    // When there are no more grey nodes, we're finished sweeping over all of
    // the objects.
    if (gc->firstGrey == NULL)
    {
        return false;
    }

    // Pop the next grey object from the list.
    Gc_Object* object = gc->firstGrey;
    gc->firstGrey = object->nextGrey;

    if (object->type == LUA_TTABLE)
    {

        Table* table = static_cast<Table*>(object);

        Value* element = table->element;
        for (int i = 0; i < table->numElements; ++i)
        {
            Gc_MarkValue(gc, element);
            ++element;
        }

        // Mark the key and values in the table.
        TableNode* node = table->nodes;
        TableNode* end  = node + table->numNodes;
        while (node < end)
        {
            if (!node->dead)
            {
                Gc_MarkValue(gc, &node->key);
                Gc_MarkValue(gc, &node->value);
            }
            else
            {
                Gc_MarkValue(gc, &node->key);
            }
            ++node;
        }

        if (table->metatable != NULL)
        {
            Gc_MarkObject(gc, table->metatable);
        }

    }
    else if (object->type == LUA_TFUNCTION)
    {

        Closure* closure = static_cast<Closure*>(object);

        if (closure->c)
        {
            // Mark the up values.
            Value* value = closure->cclosure.upValue;
            Value* end = value + closure->cclosure.numUpValues;
            while (value < end)
            {
                Gc_MarkValue(gc, value);
                ++value;
            }
        }
        else
        {

            // Mark the prototype.
            Gc_MarkObject(gc, closure->lclosure.prototype);

            // Mark the up values.
            UpValue** upValue = closure->lclosure.upValue;
            UpValue** end = upValue + closure->lclosure.numUpValues;
            while (upValue < end)
            {
                Gc_MarkObject(gc, *upValue);
                ++upValue;
            }

        }

        Gc_MarkObject(gc, closure->env);

    }
    else if (object->type == LUA_TPROTOTYPE)
    {

        Prototype* prototype = static_cast<Prototype*>(object);

        // Mark the children.
        Prototype** child = prototype->prototype;
        Prototype** endChild = child + prototype->numPrototypes;
        while (child < endChild)
        {
            if (*child != NULL)
            {
                Gc_MarkObject(gc, *child);
            }
            ++child;
        }

        // Mark the constants.
        Value* constant = prototype->constant;
        Value* endConstant = constant + prototype->numConstants;
        while (constant < endConstant)
        {
            Gc_MarkValue(gc, constant);
            ++constant;
        }

        // Mark the upvalues.
        String** upValue = prototype->upValue;
        String** endUpValue = upValue + prototype->numUpValues;
        while (upValue < endUpValue)
        {
            if (*upValue != NULL)
            {
                Gc_MarkObject(gc, *upValue);
            }
            ++upValue;
        }

        // Mark the debug information.
        Gc_MarkObject(gc, prototype->source);

    }
    else if (object->type == LUA_TUPVALUE)
    {

        UpValue* upValue = static_cast<UpValue*>(object);
        Gc_MarkValue(gc, upValue->value);

    }
    else if (object->type == LUA_TUSERDATA)
    {

        UserData* userData = static_cast<UserData*>(object);
        if (userData->metatable != NULL)
        {
            Gc_MarkObject(gc, userData->metatable);
        }
        Gc_MarkObject(gc, userData->env);

    }
    else if (object->type == LUA_TFUNCTIONP)
    {

        Function* function = static_cast<Function*>(object);

        if (function->parent != NULL)
        {
            Gc_MarkObject(gc, function->parent);
        }

        Gc_MarkObject(gc, function->constants);

        for (int i = 0; i < function->numLocals; ++i)
        {
            Gc_MarkObject(gc, function->local[i]);
        }

        for (int i = 0; i < function->numUpValues; ++i)
        {
            Gc_MarkObject(gc, function->upValue[i]);
        }

        for (int i = 0; i < function->numFunctions; ++i)
        {
            Gc_MarkObject(gc, function->function[i]);
        }
    
    }

    object->color = Color_Black;
    return true;

}

static void Gc_Sweep(lua_State* L, Gc* gc)
{

    ASSERT(gc->firstGrey == NULL);

    Gc_Object* object = gc->first;
    Gc_Object* prevObject = NULL;

    int numObjectsCollected = 0;

    while (object != NULL)
    {
            
        // White objects are garbage object.
        if (object->color == Color_White)
        {
            Gc_Object* nextObject = object->next;
            Gc_FreeObject(L, gc, object, false);
            object = nextObject;
            ++numObjectsCollected;
        }
        else
        {
            // Reset the for the next gc cycle.
            object->color = Color_White;
            object->young = false;

            // This is a candidate for deletion if there is no reference in the
            // stack, so put it in the young list for examination during the
            // next cycle of the garbage collector.
            if (object->refCount == 0)
            {
                Gc_AddYoungObject(L, gc, object);
            }
    
            // Advance to the next object in the list.
            prevObject = object;
            object = object->next;
        }

    }

}

static void Gc_Finish(lua_State* L, Gc* gc)
{

    // Mark the string constants since we never want to garbage collect them.
    for (int i = 0; i < NUM_TYPES; ++i)
    {
        if (L->typeName[i] != NULL)
        {
            Gc_MarkObject(gc, L->typeName[i]);
        }
    }

    // Sweep over the root objects again to make sure that if anything was
    // assigned to a root, we don't collect it.
    Gc_MarkRoots(L, gc);

    // If any of the roots were marked as grey, we need to continue propagating.
    while (Gc_Propagate(gc))
    {
    }

    Gc_Sweep(L, gc);

}

static void Gc_ScanMarkValue(Gc_Object* object, int scanMark)
{
    object->scanMark = scanMark;
}

static void Gc_ScanMarkValue(Value* value, int scanMark)
{
    if (Value_GetIsObject(value))
    {
        Gc_ScanMarkValue(value->object, scanMark);
    }
}

/** Scans the stack and mark the objects on it with a unique mark. */
static void Gc_ScanMarkRootObjects(lua_State* L, Gc* gc)
{

    ++gc->scanMark;
    int scanMark = gc->scanMark;

    Value* stackTop = L->stackTop;

    // Mark the functions on the call stack.
    CallFrame* frame = L->callStackBase;
    CallFrame* callStackTop = L->callStackTop;
    while (frame < callStackTop)
    {
        Gc_ScanMarkValue(frame->function, scanMark);
        if (frame->stackTop > stackTop)
        {
            stackTop = frame->stackTop;
        }
        ++frame;
    }

    // Mark the objects on the stack.
    Value* value = L->stack;
    while (value < stackTop)
    {
        Gc_ScanMarkValue(value, scanMark);
        ++value;
    }
    
    // Mark the global tables.
    Gc_ScanMarkValue(&L->globals, scanMark);
    Gc_ScanMarkValue(&L->registry, scanMark);

    for (int i = 0; i < NUM_TYPES; ++i)
    {
        if (L->metatable[i] != NULL)
        {
            Gc_ScanMarkValue(L->metatable[i], scanMark);
        }
    }

    // Mark the string constants.
    for (int i = 0; i < NUM_TYPES; ++i)
    {
        if (L->typeName[i] != NULL)
        {
            Gc_ScanMarkValue(L->typeName[i], scanMark);
        }
    }

}

static void Gc_SweepYoungObjects(lua_State* L, Gc* gc)
{

    int scanMark = gc->scanMark;

    int numYoungObjects     = 0;
    int numObjectsCollected = 0;
    int numObjectsRemoved   = 0;

    int objectIndex = 0;
    Gc_Object** youngObject = gc->youngObject;

    while (objectIndex < gc->numYoungObjects)
    {

        ++numYoungObjects;

        Gc_Object* object = youngObject[objectIndex];

        // This object doesn't have any references on the heap or stack.
        bool unreachable = (object->refCount == 0 && object->scanMark != scanMark);

        // This object is referenced by something.
        bool referenced = object->refCount > 0;

        // Remove from the young list.
        if (unreachable || referenced)
        {

            ++numObjectsRemoved;
            
            // To quickly from the list, swap with the last object.
            --gc->numYoungObjects;
            youngObject[objectIndex] = youngObject[gc->numYoungObjects];
            object->young = false;

            if (unreachable)
            {
                Gc_FreeObject(L, gc, object, true);
                ++numObjectsCollected;
            }

        }
        else
        {
            ++objectIndex;
        }

    }

}

void Gc_CollectYoung(lua_State* L, Gc* gc)
{
    Gc_ScanMarkRootObjects(L, gc);
    Gc_SweepYoungObjects(L, gc);
}

bool Gc_Step(lua_State* L, Gc* gc)
{

#ifdef GC_DISABLE
    return false;
#endif

    Gc_State state = gc->state;

    if (L->gchook != NULL)
    {
        L->gchook(L, LUA_GCHOOK_STEP_START, state);
    }

    bool result = false;

    switch (gc->state)
    {
    case Gc_State_Young:
        Gc_CollectYoung(L, gc);
        gc->state = Gc_State_Paused;
        break;
    case Gc_State_Start:
        // Clear the young list so that we don't have to worry about deleting
        // something that is in it. We'll rebuild it when we do the sweep phase.
        gc->numYoungObjects = 0;
        Gc_MarkRoots(L, gc);
        gc->state = Gc_State_Propagate;
        break;
    case Gc_State_Propagate:
        if (!Gc_Propagate(gc))
        {
            gc->state = Gc_State_Finish;
        }
        break;
    case Gc_State_Finish:
        {
            Gc_Finish(L, gc);
            gc->state = Gc_State_Paused;
            // Setup the increment for the next time we run the garbage collector.
            gc->threshold = L->totalBytes + _gcThreshold;
        }
        result = true;
        break;
    }

    if (L->gchook != NULL)
    {
        L->gchook(L, LUA_GCHOOK_STEP_END, state);
    }

    return result;
}

void Gc_Collect(lua_State* L, Gc* gc)
{

#ifdef GC_DISABLE
    return;
#endif

    if (L->gchook != NULL)
    {
        L->gchook(L, LUA_GCHOOK_FULL_START, 0);
    }

    // Finish up any propagation stage.
    while (gc->state != Gc_State_Paused)
    {
        Gc_Step(L, gc);
    }

    // Start a new GC cycle.
    gc->state = Gc_State_Start;
    while (gc->state != Gc_State_Paused)
    {
        Gc_Step(L, gc);
    }

    if (L->gchook != NULL)
    {
        L->gchook(L, LUA_GCHOOK_FULL_END, 0);
    }

}

void Gc_AddYoungObject(lua_State* L, Gc* gc, Gc_Object* object)
{
    ASSERT( !object->young );

    if (gc->numYoungObjects == gc->maxYoungObjects)
    {
        if (gc->state == Gc_State_Paused)
        {
            Gc_CollectYoung(L, gc);
            if (gc->numYoungObjects == gc->maxYoungObjects)
            {
                // We need to run the mark and sweep collector since we have too
                // many young objects.
                return;
            }
        }
    }

    object->young = true;
    gc->youngObject[gc->numYoungObjects] = object;
    ++gc->numYoungObjects;
}
