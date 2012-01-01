/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */

#include "Test.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

extern "C" void clear();

int main(int argc, char* argv[])
{
    Test_RunTests("MultipleAssignment");
    return 0;
}