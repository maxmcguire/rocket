/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#ifndef ROCKETVM_GLOBAL_H
#define ROCKETVM_GLOBAL_H

#ifdef __GNUC__
#define STATIC_ASSERT_HELPER(expr, msg) \
    (!!sizeof \ (struct { unsigned int STATIC_ASSERTION__##msg: (expr) ? 1 : -1; }))
#define STATIC_ASSERT(expr, msg) \
    extern int (*assert_function__(void)) [STATIC_ASSERT_HELPER(expr, msg)]
#else
    #define STATIC_ASSERT(expr, msg)   \
    extern char STATIC_ASSERTION__##msg[1]; \
    extern char STATIC_ASSERTION__##msg[(expr)?1:2]
#endif

#endif