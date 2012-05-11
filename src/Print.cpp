/*
 * RocketVM
 * Copyright (c) 2012-2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */

#include "Print.h"
#include "Value.h"
#include "Opcode.h"
#include "String.h"
#include "Function.h"

#include <stdio.h>
#include <string.h>

static const char* FormatConstant(const Value* value, char buffer[64])
{
    if (Value_GetIsNumber(value))
    {
        lua_number2str(buffer, value->number);
    }
    else if (Value_GetIsString(value))
    {
        return String_GetData(value->string);
    }
    else if (Value_GetIsBoolean(value))
    {
        strcpy(buffer, value->boolean ? "true" : "false");
    }
    else if (Value_GetIsNil(value))
    {
        strcpy(buffer, "nil");
    }
    else
    {
        // Some other type of constant was stored that can't be saved into
        // the file. If this happens, it means we've introduced some new type
        // of constant but haven't handled it here.
        ASSERT(0);
        return "Unknown";
    }
    return buffer;
}

static const char* FormatK(const Prototype* prototype, char buffer[64], int index)
{
    const Value* value = &prototype->constant[index];
    return FormatConstant(value, buffer);
}

static const char* FormatRK(const Prototype* prototype, char buffer[64], int index)
{
    if (index & 256)
    {
        const Value* value = &prototype->constant[index & 255];
        return FormatConstant(value, buffer);
    }
    else
    {
        sprintf(buffer, "r%d", index);
        return buffer;
    }
}

static void PrintConstants(Prototype* prototype)
{
    const int commentColumn = 18;

    for (int i = 0; i < prototype->numConstants; ++i)
    {

        const Value* value = &prototype->constant[i];
        
        char buffer[64];
        const char* data = FormatConstant(value, buffer);
        
        int length = printf(".const %s", data);

        // Indent before printing the comment.
        if (length < commentColumn)
        {
            length += printf("%*s", commentColumn - length, "");
        }

        printf("; %d\n", i);

    }
}

void PrintFunction(Prototype* prototype)
{

    enum Format
    {
        Format_None,
        Format_A,
        Format_AB,
        Format_ABC,
        Format_ABx,
        Format_AsBx,
        Format_AC,
        Format_sBx,
    };

    static const Format format[] = 
        {
            Format_AB,      // Opcode_Move
            Format_ABx,     // Opcode_LoadK
            Format_ABC,     // Opcode_LoadBool
            Format_AB,      // Opcode_LoadNil
            Format_AB,      // Opcode_GetUpVal
            Format_ABx,     // Opcode_GetGlobal
            Format_ABC,     // Opcode_GetTable
            Format_ABx,     // Opcode_SetGlobal
            Format_AB,      // Opcode_SetUpVal
            Format_ABC,     // Opcode_SetTable
            Format_ABC,     // Opcode_NewTable
            Format_ABC,     // Opcode_Self
            Format_ABC,     // Opcode_Add 
            Format_ABC,     // Opcode_Sub
            Format_ABC,     // Opcode_Mul
            Format_ABC,     // Opcode_Div
            Format_ABC,     // Opcode_Mod
            Format_ABC,     // Opcode_Pow
            Format_AB,      // Opcode_Unm
            Format_AB,      // Opcode_Not
            Format_AB,      // Opcode_Len
            Format_ABC,     // Opcode_Concat
            Format_sBx,     // Opcode_Jmp
            Format_ABC,     // Opcode_Eq
            Format_ABC,     // Opcode_Lt
            Format_ABC,     // Opcode_Le
            Format_AC,      // Opcode_Test
            Format_ABC,     // Opcode_TestSet
            Format_ABC,     // Opcode_Call
            Format_ABC,     // Opcode_TailCall
            Format_AB,      // Opcode_Return
            Format_AsBx,    // Opcode_ForLoop
            Format_AsBx,    // Opcode_ForPrep
            Format_ABC,     // Opcode_TForLoop
            Format_ABC,     // Opcode_SetList
            Format_A,       // Opcode_Close
            Format_ABx,     // Opcode_Closure
            Format_AB,      // Opcode_VarArg
        };

    printf("; function\n");
    printf("; %d upvalues, %d params, %d stack slots\n",
        prototype->numUpValues, prototype->numParams, prototype->maxStackSize);

    PrintConstants(prototype);

    const int commentColumn = 30;
    const int argsColumn    = 20;

    int lineNumberDigits = 0;
    int log = prototype->codeSize;
    while (log > 0)
    {
        log /= 10;
        ++lineNumberDigits;
    }

    for (int i = 0; i < prototype->codeSize; ++i)
    {
        
        Instruction inst = prototype->code[i];
        
        Opcode opcode = GET_OPCODE(inst);
        int line = i + 1;

        const char* op = Opcode_GetAsText(opcode);
        int length = printf("[%0*d] %s",  lineNumberDigits, line, op);

        // Indent before printing the arguments.
        if (length < argsColumn)
        {
            length += printf("%*s", argsColumn - length, "");
        }

        switch (format[opcode])
        {
        case Format_A:
            length += printf("%d", GET_A(inst));
            break;
        case Format_AB:
            length += printf("%d %d", GET_A(inst), GET_B(inst));
            break;
        case Format_ABC:
            length += printf("%d %d %d", GET_A(inst), GET_B(inst), GET_C(inst));
            break;
        case Format_ABx:
            length += printf("%d %d", GET_A(inst), GET_Bx(inst));
            break;
        case Format_AsBx:
            length += printf("%d %d", GET_A(inst), GET_sBx(inst));
            break;
        case Format_AC:
            length += printf("%d %d", GET_A(inst), GET_C(inst));
            break;
        case Format_sBx:
            length += printf("%d", GET_sBx(inst));
            break;
        }

        // Indent before printing the comment.
        if (length < commentColumn)
        {
            length += printf("%*s", commentColumn - length, "");
        }

        char buffer1[64];
        char buffer2[64];

        const char* arg1 = NULL;
        const char* arg2 = NULL;

        switch (opcode)
        {
        case Opcode_GetGlobal:
            arg1 = FormatK(prototype, buffer1, GET_Bx(inst));
            printf("; r%d = _G[%s]", GET_A(inst), arg1); 
            break;
        case Opcode_Jmp:
            printf("; goto [%0*d]", lineNumberDigits, line + GET_sBx(inst) + 1); 
            break;
        case Opcode_Test:
            if (GET_C(inst))
            {
                printf("; if not r%d then goto [%0*d]", GET_A(inst), lineNumberDigits, line + 2);
            }
            else
            {
                printf("; if r%d then goto [%0*d]", GET_A(inst), lineNumberDigits, line + 2);
            }
            break;
        case Opcode_TestSet:
            if (GET_C(inst))
            {
                printf("; if r%d then r%d = r%d else goto [%0*d]", GET_B(inst), GET_A(inst), GET_B(inst), lineNumberDigits, line + 2);
            }
            else
            {
                printf("; if not r%d then r%d = r%d else goto [%0*d]", GET_B(inst), GET_A(inst), GET_B(inst), lineNumberDigits, line + 2);
            }
            break;
        case Opcode_Eq:
            arg1 = FormatRK( prototype, buffer1, GET_B(inst) );
            arg2 = FormatRK( prototype, buffer2, GET_C(inst) );
            if (GET_A(inst))
            {
                printf("; if %s ~= %s then goto [%0*d]", arg1, arg2,
                    lineNumberDigits, line + 2);
            }
            else
            {
                printf("; if %s == %s then goto [%0*d]", arg1, arg2,
                    lineNumberDigits, line + 2);
            }
            break;
        case Opcode_Lt:
            arg1 = FormatRK( prototype, buffer1, GET_B(inst) );
            arg2 = FormatRK( prototype, buffer2, GET_C(inst) );
            if (GET_A(inst))
            {
                printf("; if not (%s < %s) then goto [%0*d]", arg1, arg2,
                    lineNumberDigits, line + 2);
            }
            else
            {
                printf("; if %s < %s then goto [%0*d]", arg1, arg2,
                    lineNumberDigits, line + 2);
            }
            break;
        case Opcode_Le:
            arg1 = FormatRK( prototype, buffer1, GET_B(inst) );
            arg2 = FormatRK( prototype, buffer2, GET_C(inst) );
            if (GET_A(inst))
            {
                printf("; if not (%s <= %s) then goto [%0*d]", arg1, arg2,
                    lineNumberDigits, line + 2);
            }
            else
            {
                printf("; if %s <= %s then goto [%0*d]", arg1, arg2,
                    lineNumberDigits, line + 2);
            }
            break;
        case Opcode_LoadK:
            arg1 = FormatK( prototype, buffer1, GET_Bx(inst) );
            printf("; r%d = %s", GET_A(inst), arg1);
            break;
        case Opcode_LoadBool:
            if (GET_C(inst))
            {
                printf("; r%d = %s; goto [%0*d]", GET_A(inst), GET_B(inst) ? "true" : "false",
                    lineNumberDigits, line + 2);
            }
            else
            {
                printf("; r%d = %s", GET_A(inst), GET_B(inst) ? "true" : "false");
            }
            break;
        case Opcode_Call:
            {
                int numArgs    = GET_B(inst) - 1;
                int numResults = GET_C(inst) - 1;
                printf("; ");
                if (numArgs >= 0)
                {
                    printf("%d arguments", numArgs);
                }
                else
                {
                    printf("variable arguments");
                }
                if (numResults >= 0)
                {
                    printf(", %d results", numResults);
                }
                else
                {
                    printf(", variable results");
                }
            }
            break;
        }

        printf("\n");

    }

    printf("; end of function\n\n");

}