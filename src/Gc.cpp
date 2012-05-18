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

#define GCSTEPSIZE	1024u

/**
 * Checks if the garbage collector needs to be run.
 */
static void Gc_Check(lua_State* L, Gc* gc)
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

/**
 * Reclaims the memory for an object.
 */
static void Gc_FreeObject(lua_State* L, Gc_Object* object)
{
    switch (object->type)
    {
    case LUA_TTABLE:
        Table_Destroy( L, static_cast<Table*>(object) );
        break;
    case LUA_TFUNCTION:
        Closure_Destroy(L, static_cast<Closure*>(object) );
        break;
    case LUA_TPROTOTYPE:
        Prototype_Destroy(L, static_cast<Prototype*>(object) );
        break;
    case LUA_TFUNCTIONP:
        Function_Destroy(L, static_cast<Function*>(object) );
        break;
    case LUA_TUPVALUE:
        UpValue_Destroy(L, static_cast<UpValue*>(object));
        break;
    case LUA_TUSERDATA:
        UserData_Destroy(L, static_cast<UserData*>(object));
        break;
    default:
        ASSERT(0);
    }
}

void Gc_Initialize(Gc* gc)
{
    gc->first       = NULL;
    gc->firstGrey   = NULL;
    gc->state       = Gc_State_Paused;
    gc->threshold   = GCSTEPSIZE;
}

void Gc_Shutdown(lua_State* L, Gc* gc)
{

    // Free all of the objects.
    Gc_Object* object =  gc->first;
    while (object != NULL)
    {
        Gc_Object* nextObject = object->next;
        Gc_FreeObject(L, object);
        object = nextObject;
    }

    gc->first = NULL;
    gc->firstGrey = NULL;

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

    if (L->gc.state == Gc_State_Finish)
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
            Gc_FreeObject(L, object);
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

    // Mark the string constants since we never want to garbage collect them.
    for (int i = 0; i < TagMethod_NumMethods; ++i)
    {
        if (L->tagMethodName[i] != NULL)
        {
            Gc_MarkObject(gc, L->tagMethodName[i]);
        }
    }
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

    // Sweep the string pool. We don't mark the strings since the string pool
    // acts a weak reference.
    StringPool_SweepStrings(L, &L->stringPool);

}

bool Gc_Step(lua_State* L, Gc* gc)
{
    switch (gc->state)
    {
    case Gc_State_Start:
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
            gc->threshold = L->totalBytes + GCSTEPSIZE;

        }
        return true;
    }
    return false;
}

void Gc_Collect(lua_State* L, Gc* gc)
{
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

void Gc_WriteBarrier(lua_State* L, Gc_Object* parent, const Value* child)
{
    if (Value_GetIsObject(child))
    {
        Gc_WriteBarrier(L, parent, child->object);
    }
}