/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */ 

#ifndef ROCKETVM_VM_H
#define ROCKETVM_VM_H

extern "C"
{
#include "lua.h"
}

#include "State.h"

/**
 * Type of a C function that can be run in a protected environment. by the
 * ProtectedCall function.
 */
typedef void (*ProtectedFunction)(lua_State *L, void* userData);

int Vm_ProtectedCall(lua_State* L, ProtectedFunction function, Value* restoreTop, void* userData, Value* errorHandler);
int Vm_ProtectedCall(lua_State* L, Value* value, int numArgs, int numResults, Value* errorHandler);
extern "C" void Vm_Call(lua_State* L, Value* value, int numArgs, int numResults);

// These trigger metamethods.
extern "C" void Vm_SetTable(lua_State* L, Value* table, Value* key, Value* value);
extern "C" void Vm_GetTable(lua_State* L, const Value* table, const Value* key, Value* dst, bool ref);

extern "C" void Vm_GetGlobal(lua_State* L, Closure* closure, const Value* key, Value* dst);
extern "C" void Vm_SetGlobal(lua_State* L, Closure* closure, Value* key, Value* value);

/** Returns 0 or 1 depending on the result of the comparions. This will call
metamethods. */
int Vm_Equal(lua_State* L, const Value* arg1, const Value* arg2);
int Vm_Less(lua_State* L, const Value* arg1, const Value* arg2);
int Vm_LessEqual(lua_State L, const Value* arg1, const Value* arg2);

/** Coerces a value into a number if possible. */
bool Vm_GetNumber(const Value* value, lua_Number* result);

/** Concatenates two values and stores the result in dst. The arguments may be
converted into strings. */
void Vm_Concat(lua_State* L, Value* dst, Value* arg1, Value* arg2);

// Coerces a value into a boolean. Booleans convert to their own value. nil
// converts to false. All other values convert to true.
int Vm_GetBoolean(const Value* value);

// Coerces a value into a string. Everything will return null except a string
// type.
const char* GetString(const Value* value);

static void ArithmeticError(lua_State* L, const Value* arg1, const Value* arg2);
void MoveResults(lua_State* L, int start, int numResults);

/**
 * Returns the number of entries in the current call stack.
 */
int Vm_GetCallStackSize(lua_State* L);

/** Reports an error with a message. */
void Vm_Error(lua_State* L, const char* format, ...);

/** Returns the function (if any) that we are currently executing. */
Closure* Vm_GetCurrentFunction(lua_State* L);

#endif