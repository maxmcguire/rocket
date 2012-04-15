/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#include "Test.h"

int main(int argc, char* argv[])
{
    const char* pattern = 0;
    if (argc > 1)
    {
        pattern = argv[1];
    }
    Test_RunTests(pattern);
    return 0;
} 