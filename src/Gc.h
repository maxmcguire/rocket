/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in README
 */

#ifndef ROCKETVM_GC_H
#define ROCKETVM_GC_H

#include <stdlib.h>

#include "Value.h"

enum Gc_State
{
    Gc_State_Start,
    Gc_State_StartMarkAndSweep,
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

    // Data used by the mark and sweep garbage collector.
    Color       color;      // "color" used for garbabe collection.
    Gc_Object*  next;       // Next object in the global list.
    Gc_Object*  prev;       // Previous object in the global list.
    Gc_Object*  nextGrey;   // If grey, this points to the next grey object.

    // Data is used by the reference counting garbage collector.
    int         refCount;
    Gc_Object*  nextYoung;  // Next object in the "young" list.
    bool        young;      // True if the object is in the "young" list.
    int         scanMark;

};

/** Stores the current state of the garbage collector */
struct Gc
{
    Gc_State    state;
    Gc_Object*  first;      // First object in the global list.
    Gc_Object*  firstGrey;  // First grey object during gc.
    size_t      threshold;
    Gc_Object*  firstYoung; // First object in the young list.
    int         scanMark;
};

void Gc_Initialize(Gc* gc);

/**
 * Frees all of the objects in the garbage collector.
 */
void Gc_Shutdown(lua_State* L, Gc* gc);

/**
 * Runs a full garbage collection cycle.
 */
void Gc_Collect(lua_State* L, Gc* gc);

/**
 * Runs a single step of the incremental garbage collector. Returns true
 * if the garbage collector finished a cycle.
 */
bool Gc_Step(lua_State* L, Gc* gc);

void* Gc_AllocateObject(lua_State* L, int type, size_t size);

/** 
 * Should be called when parent becomes an owner of child.
 */
void Gc_IncrementReference(Gc* gc, Gc_Object* parent, Gc_Object* child);
void Gc_IncrementReference(Gc* gc, Gc_Object* parent, const Value* child);

void Gc_DecrementReference(Gc* gc, Gc_Object* child);
void Gc_DecrementReference(Gc* gc, const Value* child);

void Gc_MarkObject(Gc* gc, Gc_Object* object);

void Gc_AddYoungObject(Gc* gc, Gc_Object* object);

#include "Gc.inl"

#endif