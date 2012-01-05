/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */

#include "Test.h"

extern "C" void clear();

int main(int argc, char* argv[])
{
    Test_RunTests("DoBlock");
    return 0;
}