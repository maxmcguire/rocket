.586
.XMM
.MODEL FLAT, C
.STACK
.DATA

dispatchLabel   dword   Opcode_Move         ; done
                dword   Opcode_LoadK        ; done
                dword   Opcode_LoadBool
                dword   Opcode_LoadNil
                dword   Opcode_GetUpVal
                dword   Opcode_GetGlobal    ; done
                dword   Opcode_GetTable
                dword   Opcode_SetGlobal    ; done
                dword   Opcode_SetUpVal
                dword   Opcode_SetTable
                dword   Opcode_NewTable     ; done
                dword   Opcode_Self
                dword   Opcode_Add
                dword   Opcode_Sub
                dword   Opcode_Mul
                dword   Opcode_Div
                dword   Opcode_Mod
                dword   Opcode_Pow
                dword   Opcode_Unm
                dword   Opcode_Not
                dword   Opcode_Len
                dword   Opcode_Concat
                dword   Opcode_Jmp
                dword   Opcode_Eq
                dword   Opcode_Lt
                dword   Opcode_Le
                dword   Opcode_Test
                dword   Opcode_TestSet
                dword   Opcode_Call         ; done
                dword   Opcode_TailCall
                dword   Opcode_Return       ; done (sort of)
                dword   Opcode_ForLoop
                dword   Opcode_ForPrep
                dword   Opcode_TForLoop
                dword   Opcode_SetList
                dword   Opcode_Close
                dword   Opcode_Closure
                dword   Opcode_VarArg

.CODE

offsetof_lua_State_stackBase    = 4
offsetof_LClosure_prototype     = 0
offsetof_Prototype_code         = 32
offsetof_Prototype_constant     = 40
offsetof_Prototype_numUpValues  = 44
offsetof_Prototype_prototype    = 52

sizeof_Value                    = 8

tag_Nil		                    = 0FFFFFFFFh
tag_Function                    = 0FFFFFFF9h
tag_Table                       = 0FFFFFFFAh

EXTRN       Vm_GetGlobal:NEAR
EXTRN       Vm_SetGlobal:NEAR
EXTRN       Vm_Call:NEAR
EXTRN       Table_Create:NEAR
EXTRN       Vm_SetTable:NEAR
EXTRN       Vm_GetTable:NEAR
EXTRN       Closure_Create:NEAR

GET_INST    macro inst, opcode
                mov         inst,       DWORD PTR [ ebx ]
                add         ebx,        4
                mov         opcode,     inst     ; get opcode
                and         opcode,     3Fh
            endm

; ebx should contain the current IP
; after executing:
;   ecx = packed instruction    
DISPATCH    macro
                GET_INST    ecx, eax
                jmp         DWORD PTR [dispatchLabel + eax * 4]
            endm
            
            
; Parser_EmitABC            
; Parser_EmitAB
; Parser_EmitABx
; Parser_EmitAsBx


; Unpacks an instruction encoded as A
; ecx should contain the instruction
; Result:
;   ecx = 'a'
UNPACK_A    macro
                shr     ecx,    6
                and     ecx,    0FFh
            endm   

; Unpacks an instruction encoded as A
; ecx should contain the instruction
; Result:
;   ecx = 'a'
;   edx = 'b'
UNPACK_AB   macro
                mov     edx,    ecx     ; get 'b'             
                shr     edx,    23
                and     edx,    1FFh
                shr     ecx,    6       ; get 'a'             
                and     ecx,    0FFh
            endm   


; Unpacks an instruction encoded as A Bx
; ecx should contain the instruction
; Result:
;   ecx = 'a'
;   edx = 'b'
UNPACK_ABx  macro
                mov     edx,    ecx     ; get 'bx'             
                shr     edx,    14
                and     edx,    3FFFFh
                shr     ecx,    6       ; get 'a'
                and     ecx,    0FFh
            endm       
            
; Unpacks an instruction encoded as A B C
; ecx should contain the instruction
; Result:
;   ecx = 'a'
;   edx = 'b'
;   eax = 'c'
UNPACK_ABC  macro
                mov     edx,    ecx     ; get 'b'
                shr     edx,    23
                and     edx,    1FFh
                mov     eax,    ecx     ; get 'c'
                shr     eax,    14
                and     eax,    1FFh
                shr     ecx,    6       ; get 'a'
                and     ecx,    0FFh
            endm      
            
UNPACK_sBx  macro
                shr     ecx,    14
                ;and     ecx,    3FFFFh
                sub     ecx,    131071
            endm             
            
; Pushes a VM stack location or a constant location onto the x86 stack
; based on the encoded value of src
PUSH_RK     macro src
            LOCAL   Const
            LOCAL   Done
                test        src, 100h
                jne         Const
                lea         src, DWORD PTR [ esi + src * sizeof_Value ]
                jmp         Done  
            Const:
                and         src, 0FFh
                lea         src, DWORD PTR [ edi + src * sizeof_Value ]
            Done:    
                push        src         
            endm
            
; Parameters:
;   lua_State*
;   LClosure*
;
Vm_Execute PROC  USES esi edi ebx  L:DWORD, closure:DWORD

LOCAL           prototype : DWORD
    
    ; esi = stack base
    mov         eax,    L
    mov         esi,    DWORD PTR [ eax + offsetof_lua_State_stackBase ]

    ; Save prototype
    mov         eax,    closure
    mov         eax,    DWORD PTR [ eax + offsetof_LClosure_prototype ]
    mov         prototype, eax
    
    ; ebx = code
    ; edi = constants
    mov         eax,    prototype
    mov         ebx,    DWORD PTR [ eax + offsetof_Prototype_code ]
    mov         edi,    DWORD PTR [ eax + offsetof_Prototype_constant ]

    DISPATCH
    
;-------------------------------------------------------------------------------     
Opcode_Move::

    UNPACK_AB
    
    movsd       xmm0, QWORD PTR [ esi + edx * sizeof_Value ]
    movsd       QWORD PTR [ esi + ecx * sizeof_Value ], xmm0    

    DISPATCH
    
;-------------------------------------------------------------------------------    
Opcode_LoadK::

    UNPACK_ABx

    movsd       xmm0, QWORD PTR [ edi + edx * sizeof_Value ]
    movsd       QWORD PTR [ esi + ecx * sizeof_Value ], xmm0

    DISPATCH

;-------------------------------------------------------------------------------
Opcode_LoadBool::
    DISPATCH
    
;-------------------------------------------------------------------------------    
Opcode_LoadNil::
    DISPATCH
    
;-------------------------------------------------------------------------------    
Opcode_GetUpVal::
    DISPATCH
    
;-------------------------------------------------------------------------------
Opcode_GetGlobal::

    UNPACK_ABx
    
    push        ecx
    
    lea			eax, DWORD PTR [ esi + ecx * sizeof_Value ]
    push		eax
    lea         edx, DWORD PTR [ edi + edx * sizeof_Value ]
    push        edx
    push        DWORD PTR [ ebp + 8 ]   ; L
    call        Vm_GetGlobal
    add         esp, 3 * 4
    
    pop         ecx
    DISPATCH
    
;-------------------------------------------------------------------------------    
Opcode_GetTable::

    UNPACK_ABC
    
    ; Call the Vm_GetTable
    push        ecx
    PUSH_RK     eax  
    lea         edx, DWORD PTR [ esi + edx * sizeof_Value ]
    push        edx
    push        DWORD PTR [ ebp + 8 ]   ; L
    call        Vm_GetTable
    add         esp, 3 * 4
    pop         ecx
    
    ; Check for Vm_GetTable returning a NULL value
    cmp         eax, 0
    je          SetTable_Nil         
    
    movsd       xmm0, QWORD PTR [eax]
    movsd       QWORD PTR [ esi + ecx * sizeof_Value ], xmm0
    DISPATCH
    
SetTable_Nil:    
    mov         DWORD PTR [ esi + ecx * sizeof_Value ], tag_Nil
    DISPATCH
    
;-------------------------------------------------------------------------------    
Opcode_SetGlobal::

    UNPACK_ABx
    
    ; &stackBase[a]    
    lea         ecx, DWORD PTR [ esi + ecx * sizeof_Value ]
    push        ecx
    
    ; &constant[bx]    
    lea         edx, DWORD PTR [ edi + edx * sizeof_Value ]
    push        edx
    
    push        DWORD PTR [ ebp + 8 ]   ; L
    call        Vm_SetGlobal
    add         esp, 3 * 4 

    DISPATCH
    
;-------------------------------------------------------------------------------
Opcode_SetUpVal::
    DISPATCH
    
;-------------------------------------------------------------------------------    
Opcode_SetTable::

    UNPACK_ABC
    
    PUSH_RK eax     ; value               
    PUSH_RK edx     ; key               
    ; table
    lea         ecx, DWORD PTR [ esi + ecx * sizeof_Value ]
    push        ecx
    push        DWORD PTR [ ebp + 8 ]   ; L
    call        Vm_SetTable
    add         esp, 4 * 4
    
    DISPATCH

;-------------------------------------------------------------------------------    
Opcode_NewTable::
    
    push        ecx
    push        DWORD PTR [ ebp + 8 ]   ; L
    call        Table_Create
    add         esp, 1 * 4
    pop         ecx
    
    UNPACK_A

    ; stackBase[a] = { tag_Table, table }
    mov         DWORD PTR [ esi + ecx * sizeof_Value ], tag_Table
    mov         DWORD PTR [ esi + ecx * sizeof_Value + 4 ], eax    

    DISPATCH
    
;-------------------------------------------------------------------------------    
Opcode_Self::
    DISPATCH
    
Opcode_Add::
    DISPATCH
    
Opcode_Sub::
    DISPATCH
    
Opcode_Mul::
    DISPATCH
    
Opcode_Div::
    DISPATCH
    
Opcode_Mod::
    DISPATCH
    
Opcode_Pow::
    DISPATCH
    
Opcode_Unm::
    DISPATCH

Opcode_Not::
    DISPATCH

Opcode_Len::
    DISPATCH

Opcode_Concat::
    DISPATCH

;-------------------------------------------------------------------------------    
Opcode_Jmp::

    UNPACK_sBx
    add         ebx, ecx
    DISPATCH

;-------------------------------------------------------------------------------    
Opcode_Eq::
    DISPATCH

Opcode_Lt::
    DISPATCH

Opcode_Le::
    DISPATCH

Opcode_Test::
    DISPATCH

;-------------------------------------------------------------------------------
Opcode_TestSet::

    DISPATCH

;-------------------------------------------------------------------------------
Opcode_Call::

    UNPACK_ABC

    dec         eax     ; numResults
    push        eax
    dec         edx     ; numArgs
    push        edx
    ; &stackBase[a]
    lea         ecx, DWORD PTR [ esi + ecx * sizeof_Value ]
    push        ecx    
    push        DWORD PTR [ ebp + 8 ]   ; L
    call        Vm_Call
    add         esp, 4 * 4

    DISPATCH

Opcode_TailCall::
    DISPATCH

Opcode_Return::
    jmp Done

Opcode_ForLoop::
    DISPATCH

Opcode_ForPrep::
    DISPATCH

Opcode_TForLoop::
    DISPATCH

Opcode_SetList::
    DISPATCH

Opcode_Close::
    DISPATCH

Opcode_Closure::

    UNPACK_ABx
    
    ; a = ecx
    ; bx = edx

    push        ecx ; store a for later
    mov         eax, DWORD PTR [ prototype ]
    
    ; Get the new prototype
    mov         eax, DWORD PTR [ eax + offsetof_Prototype_prototype ]
    mov         eax, DWORD PTR [ eax + edx * 4 ]
    push        eax ; Store for later

    ; Create the closure
    push        eax
    push        L
    call        Closure_Create
    add         esp, 2 * 4
    
    ; Handle the up-values
    pop         ecx ; new prototype
    mov         ecx, DWORD PTR [ ecx + offsetof_Prototype_numUpValues ]
    
    Closure_Loop:
    
        GET_INST    eax, edx
        
        
        
        
    Closure_Loop_End:    
        loop        Closure_Loop
    
    ; TODO!!!!!!!!

    ; stackBase[a] = { tag_Function, closure }
    pop         ecx ; a
    mov         DWORD PTR [ esi + ecx * sizeof_Value ], tag_Function
    mov         DWORD PTR [ esi + ecx * sizeof_Value + 4 ], eax    
    
    DISPATCH
    
Opcode_VarArg::
    DISPATCH

Done:

    ; Return value
    mov     eax,    0

    ret
    
Vm_Execute ENDP

END