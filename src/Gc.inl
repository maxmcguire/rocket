/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in README
 */

FORCE_INLINE void Gc_IncrementReference(Gc* gc, Gc_Object* parent, Gc_Object* child)
{
    ASSERT(child != NULL);
    ++child->refCount;
    if (parent->color == Color_Black && child->color == Color_White)
    {
        Gc_MarkObject(gc, child);
    }
}

FORCE_INLINE void Gc_IncrementReference(Gc* gc, Gc_Object* parent, const Value* child)
{
    if (Value_GetIsObject(child))
    {
        Gc_IncrementReference(gc, parent, child->object);
    }
}

FORCE_INLINE void Gc_DecrementReference(Gc* gc, Gc_Object* child)
{
    ASSERT(child != NULL);
    
    --child->refCount;
    ASSERT(child->refCount >= 0);

    // We don't update the young list while we're in the mark and sweep phase
    // of the garbage collector since we rebuild the young list during the sweep.
    if (gc->state == Gc_State_Paused)
    {
        if (child->refCount == 0 && !child->young)
        {
            // There are either no references to this object, or the only references
            // are on the stack, so add it to the young list.
            Gc_AddYoungObject(gc, child);
        }
    }
    
}

FORCE_INLINE void Gc_DecrementReference(Gc* gc, const Value* child)
{
    if (Value_GetIsObject(child))
    {
        Gc_DecrementReference(gc, child->object);
    }
}