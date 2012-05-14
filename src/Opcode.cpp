/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#include "Opcode.h"

const char* Opcode_GetAsText(Opcode opcode)
{
    switch (opcode)
    {
    case Opcode_Move:           return "move";
    case Opcode_LoadK:          return "loadk";
    case Opcode_LoadBool:       return "loadbool";
    case Opcode_LoadNil:        return "loadnil";
    case Opcode_GetUpVal:       return "getupval";
    case Opcode_GetGlobal:      return "getglobal";
    case Opcode_GetTable:       return "gettable";
    case Opcode_SetGlobal:      return "setglobal";
    case Opcode_SetUpVal:       return "setupval";
    case Opcode_SetTable:       return "settable";
    case Opcode_NewTable:       return "newtable";
    case Opcode_Self:           return "self";
    case Opcode_Add:            return "add";
    case Opcode_Sub:            return "sub";
    case Opcode_Mul:            return "mul";
    case Opcode_Div:            return "div";
    case Opcode_Mod:            return "mod";
    case Opcode_Pow:            return "pow";
    case Opcode_Unm:            return "unm";
    case Opcode_Not:            return "not";
    case Opcode_Len:            return "len";
    case Opcode_Concat:         return "concat";
    case Opcode_Jmp:            return "jmp";
    case Opcode_Eq:             return "eq";
    case Opcode_Lt:             return "lt";
    case Opcode_Le:             return "le";
    case Opcode_Test:           return "test";
    case Opcode_TestSet:        return "testset";
    case Opcode_Call:           return "call";
    case Opcode_TailCall:       return "tailcall";
    case Opcode_Return:         return "return";
    case Opcode_ForLoop:        return "forloop";
    case Opcode_ForPrep:        return "forprep";
    case Opcode_TForLoop:       return "tforloop";
    case Opcode_SetList:        return "setlist";
    case Opcode_Close:          return "close";
    case Opcode_Closure:        return "closure";
    case Opcode_VarArg:         return "vararg";
    case Opcode_GetTableRef:    return "gettableref";
    default:                    return "<unknown>";
    }
}

Instruction Opcode_EncodeAsBx(Opcode opcode, int a, int sbx)
{
    return opcode | (a << 6) | ((sbx + 131071) << 14);
}

Instruction Opcode_EncodeABC(Opcode opcode, int a, int b, int c)
{
    return opcode | (a << 6) | (b << 23) | (c << 14);
}