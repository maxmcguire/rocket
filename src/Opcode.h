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
#define LUA_GET_OPCODE(inst)    static_cast<Opcode>((inst) & 0x3F)
#define LUA_GET_A(inst)         static_cast<int>( ((inst) >> 6) & 0xFF )
#define LUA_GET_B(inst)         static_cast<int>( ((inst) >> 23) & 0x1FF )
#define LUA_GET_Bx(inst)        static_cast<int>( ((inst) >> 14) & 0x3FFFF )
#define LUA_GET_sBx(inst)       ( LUA_GET_Bx(inst) - 131071 )
#define LUA_GET_C(inst)         static_cast<int>( ((inst) >> 14) & 0x1FF )

#define VM_GET_OPCODE(inst)     static_cast<Opcode>((inst) & 0xFF)
#define VM_GET_A(inst)          static_cast<int>( ((inst) >> 8)  & 0xFF )
#define VM_GET_B(inst)          static_cast<int>( ((inst) >> 16) & 0xFF )
#define VM_GET_C(inst)          static_cast<int>( ((inst) >> 24) & 0xFF )
#define VM_GET_D(inst)          static_cast<int>( ((inst) >> 16) & 0xFFFF )
#define VM_GET_sD(inst)         ( VM_GET_D(inst) - 32767 )

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

    Opcode_GetTableC    = 39,   // Key is a constant.
    Opcode_SetTableRC   = 40,   // Key is a register, value is constant.
    Opcode_SetTableCR   = 41,   // Key is a constant, value is register.
    Opcode_SetTableCC   = 42,   // Key is a constant, value is constant.

    Opcode_SelfC        = 43,   // Key is constant.

    Opcode_AddRC        = 44,
    Opcode_AddCR        = 45,
    Opcode_AddCC        = 46,

    Opcode_SubRC        = 47,
    Opcode_SubCR        = 48,
    Opcode_SubCC        = 49,

    Opcode_MulRC        = 50,
    Opcode_MulCR        = 51,
    Opcode_MulCC        = 52,

    Opcode_DivRC        = 53,
    Opcode_DivCR        = 54,
    Opcode_DivCC        = 55,

    Opcode_ModRC        = 56,
    Opcode_ModCR        = 57,
    Opcode_ModCC        = 58,

    Opcode_PowRC        = 59,
    Opcode_PowCR        = 60,
    Opcode_PowCC        = 61,

    Opcode_EqRC         = 62,
    Opcode_EqCR         = 63,
    Opcode_EqCC         = 64,

    Opcode_LtRC         = 65,
    Opcode_LtCR         = 66,
    Opcode_LtCC         = 67,

    Opcode_LeRC         = 68,
    Opcode_LeCR         = 69,
    Opcode_LeCC         = 70,

    Opcode_GetTableRefC = 71,

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