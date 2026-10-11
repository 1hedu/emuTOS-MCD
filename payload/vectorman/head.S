| VECTOR.MDP -- the header the servant checks, and the jump into C.
|
| docs/payload.md is the contract. The servant hands over the ST screen
| in a0, the working space in a1, its flags in d0 and the bulk data's
| address and length in d1 and d2; vm_main in vm.c takes the same five
| as arguments. Everything else about this payload is in vm.c.

        .equ    WORKSPACE, 11264        | struct vm in vm.c, checked there

        .section .head,"ax"
        .globl  payload_start
payload_start:
        .ascii  "MDPL"                  | +0   magic
        .word   1                       | +4   version
        .word   0                       | +6   flags
        .long   entry - payload_start   | +8   entry offset
        .long   image_end - payload_start   | +12  image length
        .long   WORKSPACE               | +16  working space wanted
        .ascii  "VECTOR  "              | +20  name

        .text
        .globl  entry
entry:
        movem.l %d0-%d7/%a0-%a6,-(%sp)
        move.l  %d2,-(%sp)              | bulk length
        move.l  %d1,-(%sp)              | bulk address
        move.l  %d0,-(%sp)              | flags
        move.l  %a1,-(%sp)              | working space
        move.l  %a0,-(%sp)              | the ST screen
        jsr     vm_main
        lea     20(%sp),%sp
        movem.l (%sp)+,%d0-%d7/%a0-%a6
        rts
