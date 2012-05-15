/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#include "Test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

namespace
{
    int     _numFailed = 0;
    Test*   _currentTest = NULL;
}

static bool PatternMatch(const char* string, const char* pattern)
{

    // From: http://xoomer.alice.it/acantato/dev/wildcard/wildmatch.html#Final_choice

    int i;
    bool star = false;

loopStart:

    for (i = 0; string[i]; i++) {
      switch (pattern[i]) {
         case '?':
            if (string[i] == '.') goto starCheck;
            break;
         case '*':
            star = true;
            string += i, pattern += i;
            do { ++pattern; } while (*pattern == '*');
            if (!*pattern) return true;
            goto loopStart;
         default:
            if (string[i] != pattern[i])
               goto starCheck;
            break;
      } /* endswitch */
    } /* endfor */
    while (pattern[i] == '*')
    {
       ++i;
    }
    return (!pattern[i]);

starCheck:

   if (!star)
   {
       return false;
   }
   ++string;
   goto loopStart;

}

class TestList
{
public:
    
    TestList() : m_head(0), m_tail(0)
    {
    }

    void Add(Test* test)
    {
        if (m_head == 0)
        {
            m_head = test;
            m_tail = test;
        }
        else
        {
            m_tail->next = test;
            m_tail = test;
        }
    }
    
    int RunTests(const char* pattern)
    {

        _numFailed = 0;
        int numRun = 0;

        Test* test = m_head;
        while (test != 0)
        {
            if (pattern == NULL || PatternMatch(test->name, pattern))
            {
                _currentTest = test;
                test->Run();
                ++numRun;
            }
            test = test->next;
        }
        
        _currentTest = NULL;
        return numRun;

    }

private:

    Test*   m_head;
    Test*   m_tail;

};

static TestList& Test_GetList()
{
    static TestList testList;
    return testList;
}

void Test_RegisterTest(Test* test)
{
    TestList& list = Test_GetList();
    list.Add(test);
}

void Test_RunTests(const char* pattern)
{

    _numFailed = 0;

    TestList& list = Test_GetList();
    int numRun = list.RunTests(pattern);

    printf("%d tests run\n", numRun);
    printf("%d failed\n", _numFailed);

}

void Test_Check(int value, const char* description, const char* fileName, int line)
{
    Test_Check(value != 0, description, fileName, line);
}

void Test_Check(bool value, const char* description, const char* fileName, int line)
{
    if (!value)
    {
        printf("%s:%d : Unit test %s failed: %s\n", fileName, line, _currentTest->name, description);
        ++_numFailed;
        exit(0);
    }
}

bool Test_Equal(double x, double y)
{
    return x == y;
}

bool Test_Equal(const char* x, const char* y)
{
    if (x == NULL || y == NULL)
    {
        return x == y;
    }
    return strcmp(x, y) == 0;
}

bool Test_Close(double x, double y)
{
    const double fuzzyEpsilon = 0.0000001;
    return (fabs(x - y) <= fuzzyEpsilon * (fabs(x) + 1.0));
}