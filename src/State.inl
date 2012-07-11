/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in README
 */

inline bool State_GetIsTagMethodName(lua_State* L, const Value* key)
{
    // Note, the key doesn't have to be a string for this to work since
    if (Value_GetIsString(key))
    {
        return key->string >= L->tagMethodName[0] &&
               key->string <= L->tagMethodName[TagMethod_NumMethods - 1];
    }
    return false;
}

inline TagMethod State_GetTagMethod(lua_State* L, const String* name)
{
    for (int i = 0; i < TagMethod_NumMethods; ++i)
    {
        if (L->tagMethodName[i] == name)
        {
            return static_cast<TagMethod>(i);
        }
    }
    ASSERT(0);
    return TagMethod_Index;
}
