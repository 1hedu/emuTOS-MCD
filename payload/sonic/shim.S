| The GEOS the engine expects, reduced to what it actually calls.
|
| Fourteen symbols to begin with, and it was worth counting them before
| assuming this needed an operating system underneath it. Seven are left:
| three are the YM2612, three are the kernal's mouse and event plumbing,
| one is a VDP helper. Four were the Sonic 2 Z80 driver and are gone (see
| the note at the end); the fourteenth was a random number, and it went
| with the level generator that was the only thing asking. None of them
| was deep.
|
| The sound the engine writes itself -- the jump on the PSG, the
| signpost's fanfare on two FM channels -- needs only geos_fm_lock,
| geos_fm_unlock and geos_fm_write, and those are real below. What is
| still absent is the four Sonic 2 sounds the cartridge plays through its
| own Z80 driver; see the note further down and docs/sonic.md.

        .include "geos_map.inc"
        .include "geos_constants.inc"
        .include "geos_macros.inc"
        .include "port.inc"

        .equ    VDP_DATA,   0xC00000
        .equ    VDP_CTRL,   0xC00004

        .text

| ---- the VDP -------------------------------------------------------
| d0 = VRAM address. The engine calls this before every burst.
        .globl  vdp_set_vram_write
vdp_set_vram_write:
        movem.l %d0-%d1,-(%sp)
        andi.l  #0xFFFF,%d0
        move.l  %d0,%d1
        andi.l  #0x3FFF,%d1
        swap    %d1
        ori.l   #0x40000000,%d1
        lsr.l   #7,%d0
        lsr.l   #7,%d0
        andi.l  #3,%d0                  | bits 15-14 -> command bits 1-0
        or.l    %d0,%d1
        move.l  %d1,VDP_CTRL
        movem.l (%sp)+,%d0-%d1
        rts

| ---- the GEOS bits the engine leans on -----------------------------
| There is no random number here any more. The engine asked for one in
| exactly one place -- the level generator that laid out runs of stalled
| desktop columns -- and that went, so the LCG and its seed went with it.
| main.S no longer takes the servant's per-run value out of d0 either;
| the payload contract still carries it, for a payload that wants one.
|
| The pointer belongs to GEOS and there is no pointer here: the servant
| draws the ST screen's own, into the bitmap, and it is simply part of
| the desktop Sonic runs on.
        .globl  _MouseOff
        .globl  _MouseUp
_MouseOff:
_MouseUp:
        rts

| GEOS's per-frame event pump. The engine calls it to keep the desktop's
| clock going while it has the machine; nothing here has a clock to keep,
| so this is where the pad is read instead -- the engine takes it out of
| GEN_M3_PAD_STATE, which on GEOS the input layer would have filled in.
        .globl  _DoCheckButtons
_DoCheckButtons:
        movem.l %d0-%d2,-(%sp)
        move.b  #0x40,0xA10009          | TH high: the low half
        move.b  #0x40,0xA10003
        nop
        nop
        move.b  #0x00,0xA10003
        nop
        nop
        move.b  0xA10003,%d1            | S A 0 0 D U L R  (active low)
        move.b  #0x40,0xA10003
        nop
        nop
        move.b  0xA10003,%d0            | C B R L D U
        not.b   %d0
        not.b   %d1
        moveq   #0,%d2
        btst    #0,%d0
        beq.s   1f
        ori.w   #(1<<PAD_UP),%d2
1:      btst    #1,%d0
        beq.s   2f
        ori.w   #(1<<PAD_DOWN),%d2
2:      btst    #2,%d0
        beq.s   3f
        ori.w   #(1<<PAD_LEFT),%d2
3:      btst    #3,%d0
        beq.s   4f
        ori.w   #(1<<PAD_RIGHT),%d2
4:      btst    #4,%d0
        beq.s   5f
        ori.w   #(1<<PAD_B),%d2
5:      btst    #5,%d0
        beq.s   6f
        ori.w   #(1<<PAD_C),%d2
6:      btst    #4,%d1
        beq.s   7f
        ori.w   #(1<<PAD_A),%d2
7:      btst    #5,%d1
        beq.s   8f
        ori.w   #(1<<PAD_START),%d2
8:      move.w  %d2,(GEN_M3_PAD_STATE-GEOS_BIAS)(%a6)
        movem.l (%sp)+,%d0-%d2
        rts

| ---- the sound chips ------------------------------------------------
| The YM2612 is in the Z80's address space, so the 68000 may only touch
| it while it holds the Z80's bus. That is what lock and unlock are, and
| they bracket a run of writes rather than each one -- sonic_fm_voice
| sends twenty-three registers inside one lock.
|
| Nothing here brings the Z80 out of reset or lets it run. main.S takes
| the bus at entry and keeps it, so these are a pair of counted calls
| against a bus that is already ours; the request and the wait are still
| written out, because the engine is entitled to a lock that locks.
        .equ    Z80_BUSREQ, 0xA11100
        .equ    Z80_RESET,  0xA11200
        .equ    YM_PART1,   0xA04000
        .equ    YM_PART2,   0xA04002

        .globl  geos_fm_lock
        .globl  geos_fm_unlock
geos_fm_lock:
        move.w  #0x0100,Z80_BUSREQ
        move.w  #-1,%d0
1:      btst    #0,Z80_BUSREQ           | 1 while the Z80 still has it
        beq.s   2f
        dbra    %d0,1b                  | counted, for the reason below
2:      rts
geos_fm_unlock:
        rts                             | main.S holds the bus for the run

| d0.w = register, bit 8 selecting part II.  d1.b = the value.
|
| The busy flag is bit 7 of the status byte, which every one of the four
| ports mirrors, and it has to be clear before the register write and
| again before the data write -- the chip is slower than the bus and a
| write during the busy window is dropped.
|
| Both waits are counted. A poll for a hardware flag that "cannot" stay
| set is a poll nobody has ever seen fail, and this one is reached
| hundreds of times a second from inside a payload that has the whole
| machine: if the 68000 does not actually hold the Z80's bus then
| $A04000 is not the YM2612 answering, it is open bus, and a stuck bit 7
| would stop the console dead with no way to tell why. Sixty-four
| thousand tries is far past any real BUSY and still under a millisecond.
        .globl  geos_fm_write
geos_fm_write:
        movem.l %d0-%d2/%a0,-(%sp)
        movea.l #YM_PART1,%a0
        btst    #8,%d0
        beq.s   1f
        movea.l #YM_PART2,%a0
1:      move.w  #-1,%d2
2:      btst    #7,YM_PART1
        beq.s   3f
        dbra    %d2,2b
3:      move.b  %d0,(%a0)               | the register
        move.w  #-1,%d2
4:      btst    #7,YM_PART1
        beq.s   5f
        dbra    %d2,4b
5:      move.b  %d1,1(%a0)              | and its value
        movem.l (%sp)+,%d0-%d2/%a0
        rts

| ---- what is not here ------------------------------------------------
| Nothing. The Z80 stayed idle in the end.
|
| Four of the engine's calls used to be Sonic 2's own Z80 driver -- load,
| stop, queue, service -- and the skid, the spindash rev and its release
| were played by queueing sound IDs to it. That is gone: the driver reads
| its scores through the Z80's window onto the 68000's bus, and the only
| memory this machine has there is the Mega CD's PRG-RAM window, which
| shows one bank at a time and which the payload switches several hundred
| times a frame. So those three sounds are rendered from the cartridge's
| own bytes at build time and played on this side, and the four calls do
| not exist any more. See docs/sonic.md.

| ---- the payload's own state ---------------------------------------
        .data
        .globl  sn_screen
        .globl  sn_blob
sn_screen:  .long   0                   | the ST screen, as the servant gave it
sn_blob:    .long   0                   | the art, in the window, bank 3
