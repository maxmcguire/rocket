/*
 * RocketVM
 * Copyright (c) 2011-2012 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#ifndef ROCKETVM_UP_VALUE_H
#define ROCKETVM_UP_VALUE_H

#include "Gc.h"
#include "Value.h"
#include "Function.h"

struct UpValue : public Gc_Object
{
    Value*         value;           // Location of the value for the up value.
    union
    {
        Value      storage;         // Storage for a closed up value.
        struct
        {
        UpValue*    nextUpValue;    // Next open up value in the global list.
        UpValue*    prevUpValue;    // Previous open up value in the global list.
        };
    };
};

/**
 * Creates a new closed up value with the value set to nil.
 */
UpValue* UpValue_Create(lua_State* L);

/**
 * Creates a new open up value, or returns an existing one matching the
 * address of the value. The value should be on the stack.
 */
UpValue* UpValue_Create(lua_State* L, Value* value);

// "Closes" the up value so that it has its own storage.
void UpValue_Close(lua_State* L, UpValue* upValue);

// Closes all up values that refer to values >= value.
void UpValue_CloseUpValues(lua_State* L, Value* value);

/**
 * Destroys an up value. This will automatically be called by the garbage
 * collector.
 */
void UpValue_Destroy(lua_State* L, UpValue* upValue, bool releaseRefs);

/**
 * Returns true if the up value is currently opened.
 */
bool UpValue_GetIsOpen(UpValue* upValue);
/**
 * Assigns the value stored in the up value. The value is copied into the
 * storage location for the up value.
 */
void UpValue_SetValue(lua_State* L, UpValue* upValue, const Value* value);

#include "UpValue.inl"


#endif