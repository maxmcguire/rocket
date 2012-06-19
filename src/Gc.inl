/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in README
 */

FORCE_INLINE void Gc_WriteBarrier(Gc* gc, Gc_Object* parent, Gc_Object* child)
{
    if (parent->color == Color_Black && child->color == Color_White)
    {
        Gc_MarkObject(gc, child);
    }
}

FORCE_INLINE void Gc_WriteBarrier(Gc* gc, Gc_Object* parent, const Value* child)
{
    if (Value_GetIsObject(child))
    {
        Gc_WriteBarrier(gc, parent, child->object);
    }
}