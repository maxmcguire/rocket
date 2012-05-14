/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#ifndef ROCKETVM_OPCODE_H
#define ROCKETVM_OPCODE_H

// Number of list items to accumulate before a SETLIST instruction
#define LFIELDS_PER_FLUSH	50

// These macros are used to unpack the opcode and arguments of a
// 32-bit instruction.
#define GET_OPCODE(inst)    static_cast<Opcode>((inst) & 0x3F)
#define GET_A(inst)         static_cast<int>( ((inst) >> 6) & 0xFF )
#define GET_B(inst)         static_cast<int>( ((inst) >> 23) & 0x1FF )
#define GET_Bx(inst)        static_cast<int>( ((inst) >> 14) & 0x3FFFF )
#define GET_sBx(inst)       ( GET_Bx(inst) - 131071 )
#define GET_C(inst)         static_cast<int>( ((inst) >> 14) & 0x1FF )

typedef int Instruction;

enum Opcode
{
    Opcode_Move         = 0,
    Opcode_LoadK        = 1,
    Opcode_LoadBool     = 2,
    Opcode_LoadNil      = 3,
    Opcode_GetUpVal     = 4,
    Opcode_GetGlobal    = 5,
    Opcode_GetTable     = 6,
    Opcode_SetGlobal    = 7,
    Opcode_SetUpVal     = 8,
    Opcode_SetTable     = 9,
    Opcode_NewTable     = 10,
    Opcode_Self         = 11,
    Opcode_Add          = 12,
    Opcode_Sub          = 13,
    Opcode_Mul          = 14,
    Opcode_Div          = 15,
    Opcode_Mod          = 16,
    Opcode_Pow          = 17,
    Opcode_Unm          = 18,
    Opcode_Not          = 19,
    Opcode_Len          = 20,
    Opcode_Concat       = 21,
    Opcode_Jmp          = 22,
    Opcode_Eq           = 23,
    Opcode_Lt           = 24,
    Opcode_Le           = 25,
    Opcode_Test         = 26,
    Opcode_TestSet      = 27,
    Opcode_Call         = 28,
    Opcode_TailCall     = 29,
    Opcode_Return       = 30,
    Opcode_ForLoop      = 31,
    Opcode_ForPrep      = 32,
    Opcode_TForLoop     = 33,
    Opcode_SetList      = 34,
    Opcode_Close        = 35,
    Opcode_Closure      = 36,
    Opcode_VarArg       = 37,
    Opcode_GetTableRef  = 38,
};

const char* Opcode_GetAsText(Opcode opcode);

/**
 * Encodes a 2 argument instruction with args A sBx.
 */
Instruction Opcode_EncodeAsBx(Opcode opcode, int a, int sbx);

/**
 * Encodes a 3 argument instruction with args A B C.
 */
Instruction Opcode_EncodeABC(Opcode opcode, int a, int b, int c);

#endif