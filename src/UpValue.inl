inline void UpValue_SetValue(lua_State* L, UpValue* upValue, const Value* value)
{
    Gc* gc = &L->gc;
    if (!UpValue_GetIsOpen(upValue))
    {
        Gc_IncrementReference(gc, upValue, value);            
        Gc_DecrementReference(gc, upValue->value);            
    }
    Value_Copy(upValue->value, value);
}

inline bool UpValue_GetIsOpen(UpValue* upValue)
{
    return &upValue->storage != upValue->value;
}