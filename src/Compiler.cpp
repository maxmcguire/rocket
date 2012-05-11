/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in COPYRIGHT
 */
/*
#include "Compiler.h"
#include "Opcode.h"
#include "Function.h"
#include "Vm.h"
#include "Table.h"

#include <AsmJit/Assembler.h>
#include <AsmJit/Logger.h>
#include <AsmJit/MemoryManager.h>

#include <assert.h>
#include <stddef.h>

#include <new>

using namespace AsmJit;

// Pushes the Lua state onto the system stack.
#define PUSH_L()        \
    as.push( dword_ptr(ebp, 8) );

// Pushes stackBase[a] onto the system stack.
#define PUSH_STACK(a)                                           \
    if ((a) == 0)                                               \
        as.push( esi );                                         \
    else {                                                      \
        as.lea( eax, dword_ptr(esi, (a) * sizeof(Value)) );    \
        as.push( eax );                                         \
    }

// Push constants[bx] onto the system stack,
#define PUSH_CONSTANT(bx)                                       \
    if ((bx) == 0)                                              \
        as.push( edi );                                         \
    else {                                                      \
        as.lea( eax, dword_ptr(edi, (bx) * sizeof(Value)) );   \
        as.push( eax );                                         \
    }

// Pushes RK encoded index on the system stack.
#define PUSH_RK(c)  \
    if ((c) & 256) { PUSH_CONSTANT( (c) & 255 ); } else { PUSH_STACK( c ); }

#define CALL(f, nargs)                  \
    {   as.call( (f) );                 \
        as.add( esp, (nargs) * 4 );     \
    }

#define ADDRESS_RK_32(c, offset)                                    \
    ( (c) & 256                                                     \
        ? dword_ptr(edi, ((c) & 255) * sizeof(Value) + (offset))         \
        : dword_ptr(esi, (c) * sizeof(Value) + (offset)) )

#define ADDRESS_RK_64(c, offset)                                    \
    ( (c) & 256                                                     \
        ? qword_ptr(edi, ((c) & 255) * sizeof(Value) + (offset))         \
        : qword_ptr(esi, (c) * sizeof(Value) + (offset)) )

#define ADDRESS_STACK_32(a, offset) \
    ( dword_ptr(esi, (a) * sizeof(Value) + (offset)) )

#define ADDRESS_STACK_64(a, offset) \
    ( qword_ptr(esi, (a) * sizeof(Value) + (offset)) )

#define UPDATE_IP()                                                             \
    {   int i = static_cast<int>(ip - prototype->code);                         \
        as.mov( eax, dword_ptr(ebp, 8) );                                       \
        as.mov( eax, dword_ptr(eax, offsetof(lua_State, callStackTop)) );       \
        as.mov( ebx, (sysint_t)(size_t)(ip) );                                  \
        as.mov( dword_ptr(eax, offsetof(CallFrame, ip) - sizeof(CallFrame)), ebx ); }

void Compiler_Compile(lua_State* L, Prototype* prototype)
{

    // Our generated code assumes the size of Value is 16 bytes and uses
    // 128-bit XMM registers to move values around.
    ASSERT( sizeof(Value) == 16 );

    Assembler as;

    //FileLogger logger(stderr);
    //logger.logFormat("Assembling closure\n");
    //as.setLogger(&logger);

    Label returnLabel = as.newLabel();

    // Function prolog
    as.push(ebp);
    as.mov(ebp, esp);

    // Save the registers.
    as.push(esi);
    as.push(edi);
    as.push(ebx);

    // esi = stack base
    as.mov( eax, dword_ptr(ebp, 8) );
    as.mov( esi, dword_ptr(eax, offsetof(lua_State, stackBase)) );

    // edi = constants
    as.mov( eax, dword_ptr(ebp, 12) );
    as.mov( eax, dword_ptr(eax, offsetof(Closure, prototype)) );
    as.mov( edi, dword_ptr(eax, offsetof(Prototype, constant)) );

    const Instruction* ip  = prototype->code;
    const Instruction* end = ip + prototype->codeSize;

    // Create a label for each instruction, which allows us to implement the
    // jump opcode.

    Label* instLabel = static_cast<Label*>( Allocate(L, sizeof(Label) * prototype->codeSize) );  
    for (int i = 0; i <  prototype->codeSize; ++i)
    {
        new (instLabel + i) Label();
        instLabel[i] = as.newLabel();
    }

    while (ip < end)
    {

        int i = static_cast<int>(ip - prototype->code);
        as.bind(instLabel[i]);

        Instruction inst = *ip;
        ++ip;

        Opcode opcode = GET_OPCODE(inst); 
        int a = GET_A(inst);

        switch (opcode)
        {
        case Opcode_Move:
            {
                int b = GET_B(inst);
                // stackBase[a] = stackBase[b]
                as.movdqa( xmm1, ADDRESS_STACK_64(b, 0) );
                as.movdqa( ADDRESS_STACK_64(a, 0), xmm1 );
            }
            break;
        case Opcode_LoadK:
            {

                int bx = GET_Bx(inst);
                ASSERT(bx >= 0 && bx < prototype->numConstants);

                // stackBase[a] = constants[bx]
                as.movdqa( xmm1, dword_ptr(edi, bx * sizeof(Value)) );
                as.movdqa( ADDRESS_STACK_32(a, 0), xmm1 );

            }
            break;
        case Opcode_LoadNil:
            {
                int b = GET_B(inst);
                for (int i = a; i <= b; ++i)
                {
                    as.mov( ADDRESS_STACK_32(i, offsetof(Value, type)), LUA_TNIL );
                }
            }
            break;
        case Opcode_LoadBool:
            {

                int value = GET_B(inst) != 0;

                // Store the result.
                as.mov( ADDRESS_STACK_32(a, offsetof(Value, type)), LUA_TBOOLEAN );
                as.mov( ADDRESS_STACK_32(a, offsetof(Value, value.boolean)), value );

                if (GET_C(inst))
                {
                    int i = static_cast<int>(ip - prototype->code);
                    as.jmp(instLabel[i + 1]);
                }

            }
            break;
        case Opcode_SetTable:
            {
                PUSH_RK( GET_C(inst) );
                PUSH_RK( GET_B(inst) );
                PUSH_STACK(a);
                PUSH_L();
                CALL( SetTable, 4 );
            }
            break;
        case Opcode_GetTable:
            {

                int b = GET_B(inst);

                PUSH_RK( GET_C(inst) );
                PUSH_STACK(b);
                PUSH_L();
                CALL( GetTable, 3 );
                
                // stackBase[a] = result;
                as.movdqa( xmm1, dword_ptr(eax) );
                as.movdqa( ADDRESS_STACK_32(a, 0), xmm1 );                

            }
            break;
        case Opcode_GetGlobal:
            {

                Label l1 = as.newLabel();
                Label l2 = as.newLabel();

                int bx = GET_Bx(inst);
                ASSERT(bx >= 0 && bx < prototype->numConstants);

                PUSH_CONSTANT(bx);
                PUSH_L();
                CALL( Vm_GetGlobal, 2 );

                // Check for the case where the global doesn't exist.
                as.cmp( eax, 0 );
                as.jne( l1 );
                as.mov( ADDRESS_STACK_32(a, offsetof(Value, type)), LUA_TNIL );
                as.jmp( l2 );

                // stackBase[a] = result;
                as.bind(l1);
                as.movdqa( xmm1, dword_ptr(eax) );
                as.movdqa( ADDRESS_STACK_32(a, 0), xmm1 );

                as.bind(l2);

            }
            break;
        case Opcode_SetGlobal:
            {
                int bx = GET_Bx(inst);
                ASSERT(bx >= 0 && bx < prototype->numConstants);

                PUSH_STACK( a );
                PUSH_CONSTANT( bx );
                PUSH_L();
                CALL( Vm_SetGlobal, 3);
            }
            break;
        case Opcode_Call:
            {
                
                int numArgs     = GET_B(inst) - 1;
                int numResults  = GET_C(inst) - 1;

                as.push( numResults );
                as.push( numArgs );
                PUSH_STACK( a );
                PUSH_L();
                CALL( Vm_Call, 4 );

            }
            break;
        case Opcode_Add:
        case Opcode_Sub:
        case Opcode_Mul:
        case Opcode_Div:
            {

                Label l1 = as.newLabel();
                Label l2 = as.newLabel();

                int b = GET_B(inst);
                int c = GET_C(inst);

                // Test that both parameters are numbers.
                as.mov( eax, ADDRESS_RK_32(b, offsetof(Value, type) ) );
                as.cmp( eax, LUA_TNUMBER );
                as.jne( l1 );
                as.cmp( eax, ADDRESS_RK_32(c, offsetof(Value, type) ) );
                as.jne( l1 );

                // Load the numbers in the floating point registers.
                as.fld( ADDRESS_RK_64(b, offsetof(Value, value.number) ) );
                as.fld( ADDRESS_RK_64(c, offsetof(Value, value.number) ) );

                // Perform the operation.
                if (opcode == Opcode_Add)
                    as.faddp();
                else if (opcode == Opcode_Sub)
                    as.fsubp();
                else if (opcode == Opcode_Mul)
                    as.fmulp();
                else if (opcode == Opcode_Div)
                    as.fdivp();
      
                // Store the result.
                as.mov( ADDRESS_STACK_32(a, offsetof(Value, type)), LUA_TNUMBER );
                as.fstp( ADDRESS_STACK_64(a, offsetof(Value, value.number)) );
                as.jmp(l2);

                // Error handler.
                as.bind(l1);
                UPDATE_IP();
                PUSH_RK( c );
                PUSH_RK( b );
                PUSH_L();
                CALL( ArithmeticError, 3 );

                as.bind(l2);

            }
            break;
        case Opcode_Eq:
            {

                int b = GET_B(inst);
                int c = GET_C(inst);

                PUSH_RK( c );
                PUSH_RK( b );
                CALL( Vm_ValuesEqual, 2 );

                int i = static_cast<int>(ip - prototype->code);
                as.cmp( eax, a );
                as.jne(instLabel[i + 1]);
         
            }
            break;
        case Opcode_Lt:
            {

                int b = GET_B(inst);
                int c = GET_C(inst);

                PUSH_RK( c );
                PUSH_RK( b );
                CALL( ValuesLess, 2 );

                as.cmp( eax, a );
                int i = static_cast<int>(ip - prototype->code);
                as.jne(instLabel[i + 1]);

            }
            break;
        case Opcode_Jmp:
            {
                int sbx = GET_sBx(inst);
                int i = static_cast<int>(ip - prototype->code);
                as.jmp(instLabel[i + sbx]);
            }
            break;
        case Opcode_Test:
            {

                int c = GET_C(inst);

                Label doneLabel  = as.newLabel();

                // We're relying on the fact that LUA_TNIL is 0.
                ASSERT(LUA_TNIL == 0);

                // ecx will be 0 for nil values and non zero otherwise.
                as.mov( ecx, ADDRESS_RK_32(c, offsetof(Value, type) ) );
                as.cmp( ecx, LUA_TNIL );
                as.je( doneLabel );

                as.cmp( ecx, LUA_TBOOLEAN );
                as.jne( doneLabel );
                as.mov( ecx, ADDRESS_RK_32(c, offsetof(Value, value.boolean) ) );
                
                as.bind(doneLabel);

                int i = static_cast<int>(ip - prototype->code);
                as.test( ecx, 0xFFFFFFFF );

                if (c)
                {
                    as.jz(instLabel[i + 1]);                
                }
                else
                {
                    as.jnz(instLabel[i + 1]);                
                }

            }
            break;
        case Opcode_Return:
            {

                int numResults = GET_B(inst) - 1;

                as.push( numResults );
                as.push( a );
                PUSH_L();
                CALL( MoveResults, 3 );

                as.push( esi );
                PUSH_L();
                CALL( CloseUpValues, 2 );

                as.mov(eax, numResults);
                as.jmp(returnLabel);

            }
            break;
        case Opcode_Closure:
            {

                int bx = GET_Bx(inst);

                as.mov( eax, dword_ptr(ebp, 12) );
                as.mov( eax, dword_ptr(eax, offsetof(Closure, prototype) ) );
                as.mov( eax, dword_ptr(eax, offsetof(Prototype, prototype) ) );
                as.push( dword_ptr(eax, bx * sizeof(Prototype*) ) );
                PUSH_L();
                CALL( NewClosure, 2 );

                Prototype* p = prototype->prototype[bx];

                if (p->numUpValues > 0)
                {

                    as.push( eax );
                    as.push( edi );

                    // ebx = new closure upValue array.
                    as.mov( ebx, dword_ptr(eax, offsetof(Closure, upValue)) );

                    // edi = parent closure upValue array.
                    as.mov( eax, dword_ptr(ebp, 12) );
                    as.mov( edi, dword_ptr(eax, offsetof(Closure, upValue)) );

                    for (int i = 0; i < p->numUpValues; ++i)
                    {
                        int inst = *ip;
                        ++ip;
                        int b = GET_B(inst);
                        if ( GET_OPCODE(inst) == Opcode_Move )
                        {
                            PUSH_STACK( b );
                            PUSH_L();
                            CALL( NewUpValue, 2 );
                            // c->upValue[i] = result
                            as.mov( dword_ptr(ebx, i * sizeof(UpValue*)), eax );
                        }
                        else
                        {
                            ASSERT( GET_OPCODE(inst) == Opcode_GetUpVal );
                            // c->upValue[i] = closure->upValue[b];
                            as.mov( eax, dword_ptr(edi, i * sizeof(UpValue*)) );
                            as.mov( dword_ptr(ebx, i * sizeof(UpValue*)), eax );
                        }
                    }

                    as.pop( edi );
                    as.pop( eax );
                
                }

                // Store the result.
                as.mov( ADDRESS_STACK_32(a, offsetof(Value, type)), LUA_TFUNCTION );
                as.mov( ADDRESS_STACK_32(a, offsetof(Value, value.closure)), eax );

            }
            break;
        case Opcode_NewTable:
            {

                PUSH_L();
                CALL( Table_Create, 1 );

                // Store the result.
                as.mov( ADDRESS_STACK_32(a, offsetof(Value, type)), LUA_TTABLE );
                as.mov( ADDRESS_STACK_32(a, offsetof(Value, value.table)), eax );
                
            }
            break;
        case Opcode_GetUpVal:
            {

                as.push( GET_B(inst) );
                as.push( dword_ptr(ebp, 12) );
                CALL( GetUpValue, 2 );

                // stackBase[a] = result;
                as.movdqa( xmm1, dword_ptr(eax) );
                as.movdqa( ADDRESS_STACK_32(a, 0), xmm1 );                

            }
            break;
        default:
            // Unimplemented opcode.
            ASSERT(0);
            break;

        }

    }

    as.bind(returnLabel);

    // Restore the registers.
    as.pop(ebx);
    as.pop(edi);
    as.pop(esi);

    // Function epilog.
    as.mov(esp, ebp);
    as.pop(ebp);
    as.ret();

    // Release the instruction labels.
    for (int i = 0; i < prototype->codeSize; ++i)
    {
        instLabel[i].~Label();
    }
    Free(L, instLabel);

    prototype->compiled = reinterpret_cast<Compiler_Function>( as.make() );

    for (int i = 0; i < prototype->numPrototypes; ++i)
    {
        Compiler_Compile(L, prototype->prototype[i]);
    }
}
*/
