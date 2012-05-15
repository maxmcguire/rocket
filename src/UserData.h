/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#ifndef ROCKETVM_USERDATA_H
#define ROCKETVM_USERDATA_H

#include "Gc.h"

//
// Forward declarations.
//

struct Table;

struct UserData : public Gc_Object
{
    size_t      size;
    Table*      metatable;
    Table*      env;    // Environment table.
};

UserData* UserData_Create(lua_State* L, size_t size, Table* env);

void UserData_Destroy(lua_State* L, UserData* userData);

inline void* UserData_GetData(UserData* userData)
    { return (void*)(userData + 1); }

#endif