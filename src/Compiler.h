/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#ifndef ROCKETVM_COMPILER_H
#define ROCKETVM_COMPILER_H

#include "State.h"

struct Prototype;
struct LClosure;

typedef int (*Compiler_Function)(lua_State* L, LClosure* closure);

/**
 * Compiles a Lua function into machine code which can be directly called.
 */
void Compiler_Compile(lua_State* L, Prototype* prototype);

#endif