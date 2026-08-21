| Startup and system-call bindings for a plain TOS program.
|
| GEMDOS and the BIOS may clobber d0-d2/a0-a2, but the m68k C ABI makes
| d2 and a2 callee-saved, so every wrapper preserves them around the
| trap. Everything here is stack- and register-only, so it stays
| position independent and the .PRG needs no relocation table.

        .text
        .globl  _start

#ifdef ACC_STARTUP
| An accessory is not a program, and the difference is the stack.
|
| gotopgm() in the AES switches to user mode and jumps to the start of
| the TEXT segment with A0 = the basepage and no user stack at all --
| whatever the USP happened to hold is what the accessory gets, and at
| AES-init time that is nothing. A .PRG never has to think about this
| because TOS hands it one.
|
| Without this, SONIC.ACC's very first `bsr' wrote a return address to an
| address that is not memory, and the panic said so twice over: "USP:
| FFFFFF5C" and a program counter of 0x30, which is inside the exception
| vector table.
|
| The stack is the accessory's own, in its own uninitialised data.
|
| It cannot be the top of its memory, which was the obvious answer and
| the wrong one: load_one_acc() has already Mshrink'd the block to
| exactly the basepage plus text plus data plus bss, so the top of the
| block is the top of BSS -- and a stack starting there grows straight
| down through every variable the program has. That crashed on the first
| call, in a different place each time.
|
| Four kilobytes, because the AES is called from here and its user-side
| frames are shallow but not free. An accessory never returns: there is
| nobody to return to.
_start:
        lea     acc_stack_top(%pc),%sp
        bsr     pmain
1:      bra.s   1b

        .bss
        .align  4
acc_stack:
        .space  4096
acc_stack_top:
        .text
#else
_start:
        bsr     pmain                   | bsr: always PC-relative, unlike jsr
        clr.w   -(%sp)                  | Pterm0
        trap    #1
#endif

| void con_ws(const char *s)            GEMDOS 9
        .globl  con_ws
con_ws:
        movem.l %d2/%a2,-(%sp)
        move.l  12(%sp),-(%sp)
        move.w  #9,-(%sp)
        trap    #1
        addq.l  #6,%sp
        movem.l (%sp)+,%d2/%a2
        rts

| long con_in(void)                     GEMDOS 1
        .globl  con_in
con_in:
        movem.l %d2/%a2,-(%sp)
        move.w  #1,-(%sp)
        trap    #1
        addq.l  #2,%sp
        movem.l (%sp)+,%d2/%a2
        rts

| long dos_fcreate(const char *name, long attr)     GEMDOS 0x3C
        .globl  dos_fcreate
dos_fcreate:
        movem.l %d2/%a2,-(%sp)
        move.l  16(%sp),%d0             | attr
        move.w  %d0,-(%sp)
        move.l  14(%sp),-(%sp)          | name (12 + 2 just pushed)
        move.w  #0x3C,-(%sp)
        trap    #1
        lea     8(%sp),%sp
        movem.l (%sp)+,%d2/%a2
        rts

| long dos_fwrite(long handle, long count, const void *buf)  GEMDOS 0x40
        .globl  dos_fwrite
dos_fwrite:
        movem.l %d2/%a2,-(%sp)
        move.l  20(%sp),-(%sp)          | buf
        move.l  20(%sp),-(%sp)          | count (16 + 4 just pushed)
        move.l  20(%sp),%d0             | handle (12 + 8 just pushed)
        move.w  %d0,-(%sp)
        move.w  #0x40,-(%sp)
        trap    #1
        lea     12(%sp),%sp
        movem.l (%sp)+,%d2/%a2
        rts

| long dos_fclose(long handle)          GEMDOS 0x3E
        .globl  dos_fclose
dos_fclose:
        movem.l %d2/%a2,-(%sp)
        move.l  12(%sp),%d0
        move.w  %d0,-(%sp)
        move.w  #0x3E,-(%sp)
        trap    #1
        addq.l  #4,%sp
        movem.l (%sp)+,%d2/%a2
        rts

| long dos_cconis(void)                 GEMDOS 0x0B
        .globl  dos_cconis
dos_cconis:
        movem.l %d2/%a2,-(%sp)
        move.w  #0x0B,-(%sp)
        trap    #1
        addq.l  #2,%sp
        movem.l (%sp)+,%d2/%a2
        rts

| long dos_dfree(void *buf, long drv)   GEMDOS 0x36
        .globl  dos_dfree
dos_dfree:
        movem.l %d2/%a2,-(%sp)
        move.l  16(%sp),%d0
        move.w  %d0,-(%sp)
        move.l  14(%sp),-(%sp)
        move.w  #0x36,-(%sp)
        trap    #1
        lea     8(%sp),%sp
        movem.l (%sp)+,%d2/%a2
        rts

| long dos_dsetpath(const char *path)   GEMDOS 0x3B
        .globl  dos_dsetpath
dos_dsetpath:
        movem.l %d2/%a2,-(%sp)
        move.l  12(%sp),-(%sp)
        move.w  #0x3B,-(%sp)
        trap    #1
        addq.l  #6,%sp
        movem.l (%sp)+,%d2/%a2
        rts

| long dos_fsetdta(void *dta)           GEMDOS 0x1A
        .globl  dos_fsetdta
dos_fsetdta:
        movem.l %d2/%a2,-(%sp)
        move.l  12(%sp),-(%sp)
        move.w  #0x1A,-(%sp)
        trap    #1
        addq.l  #6,%sp
        movem.l (%sp)+,%d2/%a2
        rts

| long dos_fsfirst(const char *spec, long attr)   GEMDOS 0x4E
        .globl  dos_fsfirst
dos_fsfirst:
        movem.l %d2/%a2,-(%sp)
        move.l  16(%sp),%d0
        move.w  %d0,-(%sp)
        move.l  14(%sp),-(%sp)
        move.w  #0x4E,-(%sp)
        trap    #1
        lea     8(%sp),%sp
        movem.l (%sp)+,%d2/%a2
        rts

| long dos_fopen(const char *name, long mode)     GEMDOS 0x3D
        .globl  dos_fopen
dos_fopen:
        movem.l %d2/%a2,-(%sp)
        move.l  16(%sp),%d0
        move.w  %d0,-(%sp)
        move.l  14(%sp),-(%sp)
        move.w  #0x3D,-(%sp)
        trap    #1
        lea     8(%sp),%sp
        movem.l (%sp)+,%d2/%a2
        rts

| long dos_fread(long handle, long count, void *buf)   GEMDOS 0x3F
        .globl  dos_fread
dos_fread:
        movem.l %d2/%a2,-(%sp)
        move.l  20(%sp),-(%sp)
        move.l  20(%sp),-(%sp)
        move.l  20(%sp),%d0
        move.w  %d0,-(%sp)
        move.w  #0x3F,-(%sp)
        trap    #1
        lea     12(%sp),%sp
        movem.l (%sp)+,%d2/%a2
        rts

| long bios_bconout(long dev, long c)   BIOS 3
        .globl  bios_bconout
bios_bconout:
        move.w  10(%sp),-(%sp)
        move.w  6(%sp),-(%sp)
        move.w  #3,-(%sp)
        trap    #13
        lea     6(%sp),%sp
        rts

| long bios_setexc(long num, long vec) BIOS 5
        .globl  bios_setexc
bios_setexc:
        movem.l %d2/%a2,-(%sp)
        move.l  16(%sp),-(%sp)          | vector
        move.l  16(%sp),%d0             | num (12 + 4 just pushed)
        move.w  %d0,-(%sp)
        move.w  #5,-(%sp)
        trap    #13
        lea     8(%sp),%sp
        movem.l (%sp)+,%d2/%a2
        rts

| long critic_fail(void)
|
| A critical-error handler that fails the operation and tells nobody.
| call_etv_critic pushes (error << 16) | device and jsrs the vector, so
| 4(sp) is that longword and its high word is the error code. Returning
| it sign-extended is precisely what EmuTOS's own default handler does
| -- bios/vectors.S, _default_etv_critic -- and this is a copy of it.
|
| The AES replaces that default with one that puts a modal alert on the
| screen. That is right for a person who mistyped a drive letter and
| wrong for a program whose entire purpose is to provoke the errors:
| CDTEST asks GEMDOS four questions about D: every round, and on a
| machine with the desktop running each failure stopped the diagnostic
| dead behind an alert that had to be dismissed with a d-pad.
        .globl  critic_fail
critic_fail:
        move.w  4(%sp),%d0
        ext.l   %d0
        rts

| long xbios_supexec(long func)        XBIOS 38
        .globl  xbios_supexec
xbios_supexec:
        movem.l %d2/%a2,-(%sp)
        move.l  12(%sp),-(%sp)
        move.w  #38,-(%sp)
        trap    #14
        addq.l  #6,%sp
        movem.l (%sp)+,%d2/%a2
        rts

| long bios_getbpb(long dev)           BIOS 7
        .globl  bios_getbpb
bios_getbpb:
        movem.l %d2/%a2,-(%sp)
        move.l  12(%sp),%d0
        move.w  %d0,-(%sp)
        move.w  #7,-(%sp)
        trap    #13
        addq.l  #4,%sp
        movem.l (%sp)+,%d2/%a2
        rts

| long bios_rwabs(long rw, void *buf, long count, long recno, long dev)
| BIOS 4: Rwabs(rw.w, buf.l, count.w, recno.w, dev.w, lrecno.l)
        .globl  bios_rwabs
bios_rwabs:
        movem.l %d2-%d4/%a2,-(%sp)      | 16 bytes of saves
        move.l  20(%sp),%d0             | rw
        move.l  24(%sp),%a0             | buf
        move.l  28(%sp),%d1             | count
        move.l  32(%sp),%d2             | recno
        move.l  36(%sp),%d3             | dev
        clr.l   -(%sp)                  | lrecno, unused: recno is valid
        move.w  %d3,-(%sp)
        move.w  %d2,-(%sp)
        move.w  %d1,-(%sp)
        move.l  %a0,-(%sp)
        move.w  %d0,-(%sp)
        move.w  #4,-(%sp)
        trap    #13
        lea     18(%sp),%sp
        movem.l (%sp)+,%d2-%d4/%a2
        rts

| void *xbios_physbase(void)           XBIOS 2
| Where the ST screen actually is. SHOW.PRG writes a picture straight
| into it, and the palette block it must also write lives just past its
| end -- see the servant's pump.
        .globl  xbios_physbase
xbios_physbase:
        movem.l %d2/%a2,-(%sp)
        move.w  #2,-(%sp)
        trap    #14
        addq.l  #2,%sp
        movem.l (%sp)+,%d2/%a2
        rts

| long dos_fsnext(void)                GEMDOS 0x4F
| Fsfirst was already here; without its partner a program can find one
| matching file and no more, which is no way to list a directory.
        .globl  dos_fsnext
dos_fsnext:
        movem.l %d2/%a2,-(%sp)
        move.w  #0x4F,-(%sp)
        trap    #1
        addq.l  #2,%sp
        movem.l (%sp)+,%d2/%a2
        rts

| long dos_dgetdrv(void)               GEMDOS 0x19   0 = A:
        .globl  dos_dgetdrv
dos_dgetdrv:
        movem.l %d2/%a2,-(%sp)
        move.w  #0x19,-(%sp)
        trap    #1
        addq.l  #2,%sp
        movem.l (%sp)+,%d2/%a2
        rts

| long aes_call(void *pb)              trap #2, d0 = 0xC8
|
| The AES has one entry point and one argument: a six-pointer block of
| arrays, in D1, with the opcode inside it. Everything a desk accessory
| does goes through here.
        .globl  aes_call
aes_call:
        movem.l %d2/%a2,-(%sp)
        move.l  12(%sp),%d1
        move.w  #0xC8,%d0
        trap    #2
        movem.l (%sp)+,%d2/%a2
        rts

| long dos_dsetdrv(long drv)           GEMDOS 0x0E   0 = A:
        .globl  dos_dsetdrv
dos_dsetdrv:
        movem.l %d2/%a2,-(%sp)
        move.w  14(%sp),-(%sp)
        move.w  #0x0E,-(%sp)
        trap    #1
        addq.l  #4,%sp
        movem.l (%sp)+,%d2/%a2
        rts

| long dos_fdelete(const char *name)   GEMDOS 0x41
        .globl  dos_fdelete
dos_fdelete:
        movem.l %d2/%a2,-(%sp)
        move.l  12(%sp),-(%sp)
        move.w  #0x41,-(%sp)
        trap    #1
        addq.l  #6,%sp
        movem.l (%sp)+,%d2/%a2
        rts

| long dos_fseek(long off, long handle, long mode)  GEMDOS 0x42
        .globl  dos_fseek
dos_fseek:
        movem.l %d2/%a2,-(%sp)
        move.l  20(%sp),%d0             | mode
        move.l  16(%sp),%d1             | handle
        move.w  %d0,-(%sp)
        move.w  %d1,-(%sp)
        move.l  16(%sp),-(%sp)          | off (stack grew by 4)
        move.w  #0x42,-(%sp)
        trap    #1
        lea     10(%sp),%sp
        movem.l (%sp)+,%d2/%a2
        rts

| long dos_malloc(long bytes)          GEMDOS 0x48
| The editor's buffer. -1L asks how much is free.
        .globl  dos_malloc
dos_malloc:
        movem.l %d2/%a2,-(%sp)
        move.l  12(%sp),-(%sp)
        move.w  #0x48,-(%sp)
        trap    #1
        addq.l  #6,%sp
        movem.l (%sp)+,%d2/%a2
        rts
