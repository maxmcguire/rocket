/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in README
 */

#ifndef ROCKETVM_GC_H
#define ROCKETVM_GC_H

#include <stdlib.h>

struct lua_State;
union  Value;

enum Gc_State
{
    Gc_State_Start,
    Gc_State_Propagate,
    Gc_State_Finish,
    Gc_State_Paused,
};

/** "Colors" for marking nodes during garbage collection */
enum Color
{
    Color_White,            // Not yet examined or unreachable
    Color_Black,            // Proven reachable
    Color_Grey,             // Proven reachable, but children not examined
    Color_Alignment = 0xFFFFFFFF
};

/** The base for all garbage collectable objects. */
struct Gc_Object
{
    int         type;
    Color       color;      // "color" used for garbabe collection.
    Gc_Object*  next;       // Next object in the global list.
    Gc_Object*  nextGrey;   // If grey, this points to the next grey object.
};

/** Stores the current state of the garbage collector */
struct Gc
{
    Gc_State    state;
    Gc_Object*  first;      // First object in the global list.
    Gc_Object*  firstGrey;  // First grey object during gc.
    size_t      threshold;
};

void Gc_Initialize(Gc* gc);

/**
 * Runs a full garbage collection cycle.
 */
void Gc_Collect(lua_State* L, Gc* gc);

/**
 * Runs a single step of the incremental garbage collector. Returns true
 * if the garbage collector finished a cycle.
 */
bool Gc_Step(lua_State* L, Gc* gc);

/**
 * If link is false, the object will not be included in the global garbage
 * collection list. This should only be used in rare instance where a pointer
 * to the object will be maintained elsewhere (like the string pool).
 */
void* Gc_AllocateObject(lua_State* L, int type, size_t size, bool link = true);

/** 
 * Should be called when parent becomes an owner of child.
 */
void Gc_WriteBarrier(lua_State* L, Gc_Object* parent, Gc_Object* child);
void Gc_WriteBarrier(lua_State* L, Gc_Object* parent, Value* child);


void Gc_MarkObject(Gc* gc, Gc_Object* object);

#endif