/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#ifndef ROCKETVM_FUNCTION_H
#define ROCKETVM_FUNCTION_H

#include "Gc.h"
#include "State.h"
#include "Compiler.h"

struct Prototype : public Gc_Object
{

    int                 varArg;
    int                 numParams;
    int                 maxStackSize;
    int                 codeSize;
    Instruction*        code;
    int                 numConstants;
    Value*              constant;
    int                 numUpValues;
    String**            upValue;
    int                 numPrototypes;
    Prototype**         prototype;

    int                 lineDefined;
    int                 lastLineDefined;

    // Optional debug information.
    String*             source;
    int*                sourceLine;

};

struct UpValue : public Gc_Object
{
    Value*         value;          // Value in the stack.
    union
    {
        Value      storage;        // Storage for a closed up value.
        struct
        {
        UpValue*    nextUpValue;    // Next open up value in the global list.
        UpValue*    prevUpValue;    // Previous open up value in the global list.
        };
    };
};

/** C function closure */
struct CClosure
{
    lua_CFunction   function;
    int             numUpValues;
    Value*          upValue;
};

/** Lua function closure */
struct LClosure
{
    Prototype*      prototype;
    int             numUpValues;
    UpValue**       upValue;
};

struct Closure : public Gc_Object
{
    bool            c;      // Whether or not the function is implemented in C.
    Table*          env;    // Environment table.
    union
    {
        CClosure    cclosure;
        LClosure    lclosure;
    };
};

/**
 * Creates a prototype with the specified fields. The caller will fill in the
 * appropriate fields.
 */
Prototype* Prototype_Create(lua_State* L, int codeSize, int numConstants, int numPrototypes, int numUpValues);

/**
 * Creates a new function prototype from compiled Lua code.
 */
Prototype* Prototype_Create(lua_State* L, const char* data, size_t length, const char* name);

/**
 * Releases the memory for a prototype. This should only be called when you know
 * there are no remaining references to the prototpye (i.e. it should only be
 * called by the garbage collector).
 */
void Prototype_Destroy(lua_State* L, Prototype* prototype);

extern "C" Closure* Closure_Create(lua_State* L, Prototype* prototype, Table* env);
Closure* Closure_Create(lua_State* L, lua_CFunction function, const Value upValue[], int numUpValues, Table* env);

void Closure_Destroy(lua_State* L, Closure* closure);

/**
 * Creates a new closed up value with the value set to nil.
 */
UpValue* NewUpValue(lua_State* L);

// Creates a new open up value, or returns an existing one matching the
// address of the value.
UpValue* NewUpValue(lua_State* L, Value* value);

// "Closes" the up value so that it has its own storage.
void CloseUpValue(lua_State* L, UpValue* upValue);

// Closes all up values that refer to values >= value.
void CloseUpValues(lua_State* L, Value* value);

// Copies the short name of the source of a function prototype into the buffer.
void Prototype_GetName(Prototype* prototype, char* buffer, size_t bufferLength);

inline const Value* GetUpValue(LClosure* closure, int index)
    { return closure->upValue[index]->value; }

inline void SetUpValue(LClosure* closure, int index, const Value* value)
    { *closure->upValue[index]->value = *value; }

#endif