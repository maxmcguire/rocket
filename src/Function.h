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

/* masks for new-style vararg */
#define VARARG_HASARG		1
#define VARARG_ISVARARG		2
#define VARARG_NEEDSARG		4

struct LocVar
{
    String* varname;
    int     startpc;    // First point where variable is active
    int     endpc;      // First point where variable is dead
};

struct Prototype : public Gc_Object
{

    int                 varArg;
    int                 numParams;
    int                 maxStackSize;
    int                 codeSize;
    Instruction*        code;
    int                 convertedCodeSize;
    Instruction*        convertedCode;
    int                 numConstants;
    Value*              constant;
    int                 numUpValues;
    int                 maxUpValues;
    String**            upValue;
    int                 numPrototypes;
    Prototype**         prototype;

    LocVar*             local;
    int                 numLocals;

    int                 lineDefined;
    int                 lastLineDefined;

    // Optional debug information.
    String*             source;
    int*                sourceLine;
    int*                convertedSourceLine;


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

Prototype* Prototype_Create(lua_State* L);

/**
 * Creates a prototype with the specified fields. The caller will fill in the
 * appropriate fields.
 */
Prototype* Prototype_Create(lua_State* L, int codeSize, int convertedCodeSize,
    int numConstants, int numPrototypes, int numUpValues);

/**
 * Creates a new function prototype from compiled Lua code.
 */
Prototype* Prototype_Create(lua_State* L, const char* data, size_t length, const char* name);

/**
 * Releases the memory for a prototype. This should only be called when you know
 * there are no remaining references to the prototpye (i.e. it should only be
 * called by the garbage collector).
 */
void Prototype_Destroy(lua_State* L, Prototype* prototype, bool releaseRefs);

/**
 * Copies the short name of the source of a function prototype into the buffer.
 */
void Prototype_GetName(Prototype* prototype, char* buffer, size_t bufferLength);

/**
 * Translates the code in a prototype from standard Lua opcodes to our own
 * encoding.
 */
void Prototype_ConvertCode(lua_State* L, Prototype* prototype);
int  Prototype_GetConvertedCodeSize(const Instruction* src, int codeSize);

extern "C" Closure* Closure_Create(lua_State* L, Prototype* prototype, Table* env);
Closure* Closure_Create(lua_State* L, lua_CFunction function, const Value upValue[], int numUpValues, Table* env);

void Closure_Destroy(lua_State* L, Closure* closure, bool releaseRefs);

#endif