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

// Creates a new open up value, or returns an existing one matching the
// address of the value.
UpValue* UpValue_Create(lua_State* L, Value* value);

// "Closes" the up value so that it has its own storage.
void CloseUpValue(lua_State* L, UpValue* upValue);

// Closes all up values that refer to values >= value.
void CloseUpValues(lua_State* L, Value* value);

inline const Value* UpValue_GetValue(LClosure* closure, int index)
    { return closure->upValue[index]->value; }

inline void UpValue_SetValue(lua_State* L, LClosure* closure, int index, const Value* value)
    { 
        UpValue* upValue = closure->upValue[index];
        *upValue->value = *value;
        Gc_WriteBarrier(L, upValue, value);
    }

/**
 * Destroys an up value. This will automatically be called by the garbage
 * collector.
 */
void UpValue_Destroy(lua_State* L, UpValue* upValue);

#endif