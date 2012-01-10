/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */ 
#ifndef ROCKETVM_VM_H
#define ROCKETVM_VM_H

#include "lua.h"
#include "State.h"

int ProtectedCall(lua_State* L, Value* value, int numArgs, int numResults, int errorHandler);
extern "C" void Vm_Call(lua_State* L, Value* value, int numArgs, int numResults);

// These trigger metamethods.
extern "C" void Vm_SetTable(lua_State* L, Value* table, Value* key, Value* value);
extern "C" void Vm_GetTable(lua_State* L, const Value* table, const Value* key, Value* dst);

extern "C" void Vm_GetGlobal(lua_State* L, const Value* key, Value* dst);
extern "C" void Vm_SetGlobal(lua_State* L, Value* key, Value* value);

int Vm_ValuesEqual(const Value* arg1, const Value* arg2);
int Vm_Less(const Value* arg1, const Value* arg2);
int ValuesLessEqual(const Value* arg1, const Value* arg2);

// Coerces a value into a boolean. Booleans convert to their own value. nil
// converts to false. All other values convert to true.
int GetBoolean(const Value* value);

// Coerces a value into a string. Everything will return null except a string
// type.
const char* GetString(const Value* value);

void ArithmeticError(lua_State* L, const Value* arg1, const Value* arg2);
void MoveResults(lua_State* L, int start, int numResults);

#endif