/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#ifndef ROCKETVM_CODE_H
#define ROCKETVM_CODE_H

struct Parser;

Prototype* Parse(lua_State* L, Input* input, const char* name);

#endif