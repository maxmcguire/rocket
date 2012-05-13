/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#ifndef ROCKETVM_TEST_H
#define ROCKETVM_TEST_H

#include <assert.h>

#define CHECK(x)            Test_Check(x, #x, __FILE__, __LINE__)
#define CHECK_EQ(x, y)      Test_Check( Test_Equal((x), (y)), #x" == "#y, __FILE__, __LINE__ )
#define CHECK_CLOSE(x, y)   Test_Check( Test_Close((x), (y)), #x" == "#y, __FILE__, __LINE__ )

class Test
{
public:
    explicit Test(const char* _name) : name(_name), next(0) { }
    virtual void Run() = 0;
    const char* name;
    Test*       next;
};

#define _TEST_BODY(name)                                \
    {                                                   \
    public:                                             \
        void Run();                                     \
    };                                                  \
    class _Test_##name : public Test {                  \
    public:                                             \
        _Test_##name() : Test(#name) { }                \
        virtual void Run() {                            \
            _Test_impl_##name test;                     \
            test.Run();                                 \
        }                                               \
    } _Test_instance_##name;                            \
    static TestRegisterer _Test_register_##name(&_Test_instance_##name); \
    void _Test_impl_##name::Run()

#define TEST(name)                                      \
    class _Test_impl_##name                             \
    _TEST_BODY(name)

#define TEST_FIXTURE(name, fixture)                     \
    class _Test_impl_##name : public fixture            \
    _TEST_BODY(name)

/**
 * Registers a test. Normally this will not be explicitly called, but will
 * automatically be called by the TEST macro.
 */
void Test_RegisterTest(Test* test);

/**
 * Runs all of the unit tests. If pattern is not NULL, only tests whose name
 * match the DOS style pattern will be run,
 */
void Test_RunTests(const char* pattern = 0);

/**
 * Checks that a value is true.
 */
void Test_Check(int value, const char* description, const char* fileName, int line);
void Test_Check(bool value, const char* description, const char* fileName, int line);

bool Test_Equal(double x, double y);
bool Test_Equal(const char* x, const char* y);
bool Test_Close(double x, double y);

/**
 * Helper struct used to register a test from file scope.
 */
struct TestRegisterer
{
    TestRegisterer(Test* test)
    {
        Test_RegisterTest(test);
    }
};

#endif