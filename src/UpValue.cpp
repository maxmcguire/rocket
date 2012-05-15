/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#include "UpValue.h"

UpValue* UpValue_Create(lua_State* L)
{
    UpValue* upValue = static_cast<UpValue*>( Gc_AllocateObject( L, LUA_TUPVALUE, sizeof(UpValue) ) );
    upValue->value = &upValue->storage;
    SetNil(upValue->value);
    return upValue;
}

UpValue* UpValue_Create(lua_State* L, Value* value)
{

    // TODO: Insert in reverse sorted order for efficient closing.
    
    // Check to see if we already have an open up value for this address.
    UpValue* upValue = L->openUpValue;
    while (upValue != NULL && upValue->value != value)
    {
        upValue = upValue->nextUpValue;
    }

    // Create a new up value if necessary.
    if (upValue == NULL)
    {
        upValue = static_cast<UpValue*>( Gc_AllocateObject( L, LUA_TUPVALUE, sizeof(UpValue) ) );
        upValue->value = value;
        upValue->nextUpValue = L->openUpValue;
        upValue->prevUpValue = NULL;
        if (upValue->nextUpValue != NULL)
        {
            upValue->nextUpValue->prevUpValue = upValue;
        }
        L->openUpValue = upValue;
    }

    return upValue;

}

/**
 * Returns true if the up value is currently opened.
 */
bool UpValue_GetIsOpen(UpValue* upValue)
{
    return &upValue->storage != upValue->value;
}

/**
 * Removes an up value from the global list.
 */
static void UpValue_Unlink(lua_State* L, UpValue* upValue)
{
    ASSERT( UpValue_GetIsOpen(upValue) );
    if (upValue->nextUpValue != NULL)
    {
        upValue->nextUpValue->prevUpValue = upValue->prevUpValue;
    }
    if (upValue->prevUpValue != NULL)
    {
        upValue->prevUpValue->nextUpValue = upValue->nextUpValue;
    }
    else
    {
        L->openUpValue = upValue->nextUpValue;
    }
}

void UpValue_Destroy(lua_State* L, UpValue* upValue)
{
    if (UpValue_GetIsOpen(upValue))
    {
        UpValue_Unlink(L, upValue);
    }
    Free(L, upValue, sizeof(UpValue));
}

void CloseUpValue(lua_State* L, UpValue* upValue)
{
    UpValue_Unlink(L, upValue);
    // Copy over the value so we have our own storage.
    upValue->storage = *upValue->value;
    upValue->value   = &upValue->storage;
}

void CloseUpValues(lua_State* L, Value* value)
{
    UpValue* upValue = L->openUpValue;
    while (upValue != NULL)
    {
        UpValue* nextUpValue = upValue->nextUpValue;
        if (upValue->value >= value)
        {
            CloseUpValue(L, upValue);
        }
        upValue = nextUpValue;
    }
}