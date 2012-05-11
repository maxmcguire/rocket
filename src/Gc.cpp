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

#define GCSTEPSIZE	1024u

void Gc_Initialize(Gc* gc)
{
    gc->first       = NULL;
    gc->firstGrey   = NULL;
    gc->state       = Gc_State_Paused;
    gc->threshold   = GCSTEPSIZE;
}

void* Gc_AllocateObject(lua_State* L, int type, size_t size, bool link)
{

    Gc_Check(L, &L->gc);

    Gc_Object* object = static_cast<Gc_Object*>(Allocate(L, size));
    if (object == NULL)
    {

        // Emergency run of the garbage collector to free up memory.
        Gc_Collect(L, &L->gc);

        object = static_cast<Gc_Object*>(Allocate(L, size));
        if (object == NULL)
        {
            // Out of memory!
            State_Error(L);
        }

    }

    object->nextGrey    = NULL;
    object->type        = type;

    if (L->gc.state == Gc_State_Propagate)
    {
        // If we're propagating, then we'll either color this object
        // with a write barrier or when we rescan the stack during
        // finalization (or it will be garbage).
        object->color = Color_White;
    }
    else
    {
        object->color = Color_Black;
    }

    if (link)
    {
        object->next = L->gc.first;
        L->gc.first = object;
    }
    else
    {
        object->next = NULL;
    }

    return object;

}

static void Gc_MarkObject(Gc* gc, Gc_Object* object)
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

    // Mark the objects on the stack.
    Value* value = L->stack;
    Value* stackTop = L->stackTop;
    while (value < stackTop)
    {
        Gc_MarkValue(gc, value);
        ++value;
    }
    
    // Mark the functions on the call stack.
    CallFrame* frame = L->callStackBase;
    CallFrame* callStackTop = L->callStackTop;
    while (frame < callStackTop)
    {
        Gc_MarkValue(gc, frame->function);
        ++frame;
    }

    // Mark the globals table.
    Gc_MarkValue(gc, &L->globals);

}

static void Gc_Start(lua_State* L, Gc* gc)
{
    ASSERT( gc->firstGrey == NULL );
    Gc_MarkRoots(L, gc);
    gc->state = Gc_State_Propagate;
}

static void Gc_Propagate(Gc* gc)
{

    // When there are no more grey nodes, we're finished sweeping over all of
    // the objects.
    if (gc->firstGrey == NULL)
    {
        gc->state = Gc_State_Finish;
        return;
    }

    // Pop the next grey object from the list.
    Gc_Object* object = gc->firstGrey;
    gc->firstGrey = object->nextGrey;

    if (object->type == LUA_TTABLE)
    {

        Table* table = static_cast<Table*>(object);

        // Mark the key and values in the table.
        TableNode* node = table->nodes;
        TableNode* end  = node + table->numNodes;
        while (node < end)
        {
            if (!Value_GetIsNil(&node->key))
            {
                Gc_MarkValue(gc, &node->key);
                Gc_MarkValue(gc, &node->value);
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

    }
    else if (object->type == LUA_TPROTOTYPE)
    {

        Prototype* prototype = static_cast<Prototype*>(object);

        // Mark the children.
        Prototype** child = prototype->prototype;
        Prototype** endChild = child + prototype->numPrototypes;
        while (child < endChild)
        {
            Gc_MarkObject(gc, *child);
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

        // Mark the debug information.
        Gc_MarkObject(gc, prototype->source);

    }

    object->color = Color_Black;

}

static void Gc_Sweep(lua_State* L, Gc* gc)
{

    Gc_Object* object = gc->first;
    Gc_Object* prevObject = NULL;

    while (object != NULL)
    {
            
        // White objects are garbage object.
        if (object->color == Color_White)
        {

            // Strings should never be collected from the global list; they
            // are referenced from the string pool and are collected when we
            // sweep the strings.
            ASSERT(object->type != LUA_TSTRING);

            // Remove from the global object list.
            if (prevObject != NULL)
            {
                prevObject->next = object->next;
            }
            else
            {
                gc->first = object->next;
            }

            Gc_Object* nextObject = object->next;

            if (object->type == LUA_TTABLE)
            {
                Table_Destroy( L, static_cast<Table*>(object) );
            }
            else if (object->type == LUA_TFUNCTION)
            {
                Closure_Destroy(L, static_cast<Closure*>(object) );
            }

            object = nextObject;

        }
        else
        {
            // Reset the color for the next gc cycle.
            object->color = Color_White;
    
            // Advance to the next object in the list.
            prevObject = object;
            object = object->next;
        }

    }

}

static void Gc_Finish(lua_State* L, Gc* gc)
{

    // Mark the tag methods since we never want to garbage collect them.
    for (int i = 0; i < TagMethod_NumMethods; ++i)
    {
        if (L->tagMethodName[i] != NULL)
        {
            Gc_MarkObject(gc, L->tagMethodName[i]);
        }
    }

    // Sweep over the root objects again to make sure that if anything was
    // assigned to a root, we don't collect it.
    // TODO: Should really mark them black.
    Gc_MarkRoots(L, gc);

    Gc_Sweep(L, gc);

    // Sweep the string pool. We don't mark the strings since the string pool
    // acts a weak reference.
    StringPool_SweepStrings(L, &L->stringPool);

    // Return to the start state.
    gc->state = Gc_State_Paused;

    // Setup the increment for the next time we run the garbage collector.
    gc->threshold = L->totalBytes + GCSTEPSIZE;
    gc->firstGrey = NULL;

}

void Gc_Check(lua_State* L, Gc* gc)
{
    if (L->totalBytes > gc->threshold)
    {
        if (gc->state == Gc_State_Paused)
        {
            gc->state = Gc_State_Start;
        }
        Gc_Step(L, gc);
    }
}

bool Gc_Step(lua_State* L, Gc* gc)
{
    // GC currently disabled.
    return false;
    switch (gc->state)
    {
    case Gc_State_Start:
        Gc_MarkRoots(L, gc);
        gc->state = Gc_State_Propagate;
        break;
    case Gc_State_Propagate:
        Gc_Propagate(gc);
        break;
    case Gc_State_Finish:
        Gc_Finish(L, gc);
        return true;
    }
    return false;
}

void Gc_Collect(lua_State* L, Gc* gc)
{
    // GC currently disabled.
    return;
    if (gc->state == Gc_State_Paused)
    {
        gc->state = Gc_State_Start;
    }
    while (gc->state != Gc_State_Paused)
    {
        Gc_Step(L, gc);
    }
}

void Gc_WriteBarrier(lua_State* L, Gc_Object* parent, Gc_Object* child)
{
    if (parent->color == Color_Black)
    {
        if (child->color == Color_White)
        {
            Gc_MarkObject(&L->gc, child);
        }
    }
}

void Gc_WriteBarrier(lua_State* L, Gc_Object* parent, Value* child)
{
    if (Value_GetIsObject(child))
    {
        Gc_WriteBarrier(L, parent, child->object);
    }
}
