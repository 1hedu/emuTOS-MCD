| SONIC.MDP -- the accessory, as a payload.
|
| Everything here is the join. The engine is GEOS-Genesis's and runs on a
| machine that no longer exists under it: no kernal, no 64KB image at
| $FF0000, no interrupt table it may write, and no fifty-two kilobytes of
| its own to sit in. This file is what it lands on instead.
|
| Four joins, and they are the four docs/payload.md and port.inc describe:
|
|   THE ANCHOR. A6 is logical $8000 on GEOS and every variable the engine
|   names is a displacement off it. The engine names sixty-eight of them
|   and they span $850A..$9F5A, so A6 points at eight kilobytes of the
|   payload's own workspace and the addressing is untouched.
|
|   THE GROUND. a0 at entry is the ST screen, in the window, bank 2.
|   sonic_pixel reads it and nothing else does.
|
|   THE ART. Fifty-two thousand bytes in PRG RAM above the ramdisk,
|   reached through the Mega CD's window. The servant hands over its
|   address and its bank -- nothing here knows the memory map -- and
|   sn_blob is where the engine's nine readers look. That bank stays
|   selected for the whole run; sonic_pixel borrows the screen's for
|   four words and gives it straight back.
|
|   THE CHIPS. The YM2612 lives in the Z80's address space and the
|   68000 may touch it only while it holds the Z80's bus, so the bus is
|   taken here and kept -- not just for the run but from the first run
|   onwards, which sn_sound_close explains at length. geos_fm_lock in
|   shim.S re-requests a bus that is already ours and geos_fm_unlock is
|   an rts. Everything is silenced on the way in, because what the
|   console's boot ROM left in the chips is not ours to inherit, and
|   again on the way out, because the servant restores video and knows
|   nothing about sound.
|
|   THE FRAME. GEOS ran the tile uploads out of a VBlank ISR and the
|   engine's loop said `stop #$2000'. The servant has no interrupt table
|   to hang one off -- it polls, because on a Mega CD the vectors are in
|   the boot ROM -- so sn_frame polls the VDP's own VBlank flag and calls
|   geos_sonic_vblank itself. Same order, same work, no vector.

        .include "geos_map.inc"
        .include "geos_constants.inc"
        .include "geos_macros.inc"
        .include "port.inc"

        .equ    VDP_DATA,   0xC00000
        .equ    VDP_CTRL,   0xC00004
        .equ    VDP_STATUS, 0xC00004
        .equ    VDP_ST_VBLANK, 0x0008

        .equ    Z80_BUSREQ, 0xA11100
        .equ    Z80_RESET,  0xA11200
        .equ    PSG,        0xC00011

        .equ    WORKSPACE,  8192        | logical $8000..$9FFF

        .section .head,"ax"
        .globl  payload_start

| ---- the header, as docs/payload.md defines it ---------------------
payload_start:
        .ascii  "MDPL"                  | +0   magic
        .word   1                       | +4   version
        .word   0                       | +6   flags
        .long   entry - payload_start   | +8   entry offset
        .long   image_end - payload_start   | +12  image length
        .long   WORKSPACE               | +16  working space wanted
        .ascii  "SONIC   "              | +20  name

        .text

| ---- entry ----------------------------------------------------------
| a0 = the ST screen (32000 bytes, planar, read-only)
| a1 = the working space, WORKSPACE bytes of it
| d0 = bit 0 Mode 1; bits 9-8 the bulk data's bank; bits 11-10 the
|      screen's bank. Bits 31-16 are the servant's per-run value, which
|      nothing here wants any more -- there is nothing random left in
|      this payload to seed.
| d1 = the bulk data in the window, or zero if there is none
| d2 = how many bytes of it were loaded
        .globl  entry
entry:
        movem.l %d0-%d7/%a0-%a6,-(%sp)
        move.l  %a0,sn_screen
        move.l  %d1,sn_blob

| Which bank of the window each of them is in. The servant computes
| both -- it is the side that knows where the screen is and it is told
| where the art is -- so nothing here has an address in it.
        move.w  %d0,%d3
        lsr.w   #8,%d3
        andi.w  #3,%d3
        lsl.b   #6,%d3                  | into the register's own bits
        move.b  %d3,sn_bank_bulk
        move.w  %d0,%d3
        lsr.w   #8,%d3
        lsr.w   #2,%d3
        andi.w  #3,%d3
        lsl.b   #6,%d3
        move.b  %d3,sn_bank_scr

| No art, no Sonic. NATIVE.PRG refuses before it gets here, so this is
| the belt to that braces -- but jumping into fifty kilobytes of
| whatever the last program left in PRG RAM is not a failure worth
| having twice.
        tst.l   %d1
        beq     .out
        cmpi.l  #SN_BLOB_MIN,%d2
        blo     .out

| The anchor, and a floor under it: the engine initialises most of what
| it reads but not all of it -- the sign post's spin timer and the dust's
| four puffs are read before anything writes them -- and a payload's
| workspace is whatever the last payload left there.
        movea.l %a1,%a6
        movea.l %a1,%a2
        move.w  #((WORKSPACE/4)-1),%d1
2:      clr.l   (%a2)+
        dbra    %d1,2b

| Bank 3, for the run. The art is read every frame and the screen only
| inside sonic_pixel, so this is the resting state and bank 2 is the
| borrowed one.
        bsr     sn_bank3

| The auto-increment. Every burst in the engine assumes two, and says so
| where it changes it (sonic_fill_col steps a plane row and puts it
| back). The servant leaves it at two as well, but the engine is entitled
| to be told rather than to inherit.
        move.w  #0x8F02,VDP_CTRL

        bsr     sn_sound_open
        jsr     geos_sonic_run
        bsr     sn_sound_close

.out:
        bsr     sn_bank2                | the servant's pump reads the screen
        movem.l (%sp)+,%d0-%d7/%a0-%a6
        rts

| ---- the sound chips -------------------------------------------------
| Take the Z80's bus and keep it. The YM2612 answers on the Z80's side
| of the machine and the 68000 reaches it only as bus master; the engine
| writes fifty registers in a signpost fanfare and would otherwise be
| requesting and releasing around every one.
|
| A known state is the only kind this payload can start from, because
| what the Mega CD's boot ROM left in the sound chips is not written down
| anywhere -- so sn_silence keys every FM channel off and attenuates
| every PSG one, which is a known state that costs nine register writes.
| It used to pulse the shared Z80/YM2612 reset line as well. That is
| where the second run died; see sn_sound_close.
sn_sound_open:
        move.w  #0x0100,Z80_BUSREQ      | ask for the bus
        move.w  #-1,%d0
1:      btst    #0,Z80_BUSREQ           | 1 while the Z80 still has it
        beq.s   2f
        dbra    %d0,1b
2:      bra.s   sn_silence

| Silence, and keep the bus.
|
| THIS IS WHERE THE SECOND RUN DIED. The first version put the Z80 back
| in reset here and released the bus, on the reasoning that reset is the
| quietest state there is. It is -- and a Z80 held in reset does not
| answer a bus request. So the *next* run asked for the bus and spun in
| a two-instruction loop waiting for a grant that was never coming, with
| the servant inside the payload's jsr and the sub CPU bus-held behind
| it: the whole machine stopped, silently, before a single sprite was
| written. On the console it looked exactly like "Sonic never came".
|
| It never showed here because gpgx grants the bus whether or not the
| Z80 is in reset.
|
| So: out of reset, and the bus stays ours. The Z80 cannot run and the
| next open finds the grant already in hand. Nothing else on this machine
| uses it -- the servant's only noise is the Mega CD's PCM chip, on the
| other CPU entirely.
|
| And the wait above is bounded now, because a poll for a hardware
| handshake that cannot fail is a poll that has never been tested. If it
| ever does fail the cost is the sound, not the machine.
sn_sound_close:
        bsr     sn_silence
        move.w  #0x0100,Z80_BUSREQ
        rts

| Four PSG channels to full attenuation, and all six FM channels keyed
| off. The engine silences the two it uses; these are the rest, which it
| never touched and cannot know about.
sn_silence:
        movem.l %d0-%d2/%a0,-(%sp)
        move.b  #0x9F,PSG               | latch, channel 0, attenuation 15
        move.b  #0xBF,PSG
        move.b  #0xDF,PSG
        move.b  #0xFF,PSG
        lea     sn_keys(%pc),%a0
        moveq   #5,%d2
1:      move.w  #0x28,%d0               | key on/off, part I whatever the
        moveq   #0,%d1                  | channel: it is bits 0-2 of the data
        move.b  (%a0)+,%d1
        bsr     geos_fm_write
        dbra    %d2,1b
        move.w  #0x2B,%d0               | DAC off, or channel 6 is a speaker
        moveq   #0,%d1
        bsr     geos_fm_write
        move.w  #0x27,%d0               | timers off, normal mode
        moveq   #0,%d1
        bsr     geos_fm_write
        move.w  #0x22,%d0               | LFO off
        moveq   #0,%d1
        bsr     geos_fm_write
        movem.l (%sp)+,%d0-%d2/%a0
        rts

        .section .rodata
sn_keys:    .byte   0, 1, 2, 4, 5, 6
        .align  2
        .text

| ---- one frame ------------------------------------------------------
| PORTED, and it replaces `stop #$2000' at the top of the engine's loop.
|
| GEOS took a VBlank interrupt and geos_sonic_vblank was its handler. The
| vector table on this machine is in the Mega CD's boot ROM -- or in the
| cartridge, on a Mode 1 boot -- and neither is ours to write, so there
| is nothing to hang a handler off and the servant does not run one
| either: it polls this same flag. So this waits for the flag to rise and
| then does the handler's work inline, which puts the tile uploads and
| the SAT and the scroll register inside the blanking period exactly as
| the interrupt did.
|
| Waiting for the flag to fall first is what makes it one frame rather
| than a hundred: entering with the flag already set would otherwise
| return at once.
        .globl  sn_frame
sn_frame:
        move.l  %d0,-(%sp)
1:      move.w  VDP_STATUS,%d0
        andi.w  #VDP_ST_VBLANK,%d0
        bne.s   1b                      | wait out the blanking we are in
2:      move.w  VDP_STATUS,%d0
        andi.w  #VDP_ST_VBLANK,%d0
        beq.s   2b                      | and for the next one to start
        move.l  (%sp)+,%d0
        bra     geos_sonic_vblank       | its work, at the top of the blank

| ---- the window ------------------------------------------------------
| One byte, read-modify-write: the low bits of GA_MEMMODE are the word
| RAM's mode, DMNA and RET, and clearing them would take word RAM away
| from whichever side has it. Only bits 7-6 are ours.
        .globl  sn_bank2
        .globl  sn_bank3
sn_bank2:
        move.l  %d0,-(%sp)
        move.b  GA_MEMMODE_A,%d0
        andi.b  #0x3F,%d0
        or.b    sn_bank_scr,%d0
        move.b  %d0,GA_MEMMODE_A
        move.l  (%sp)+,%d0
        rts
sn_bank3:
        move.l  %d0,-(%sp)
        move.b  GA_MEMMODE_A,%d0
        andi.b  #0x3F,%d0
        or.b    sn_bank_bulk,%d0
        move.b  %d0,GA_MEMMODE_A
        move.l  (%sp)+,%d0
        rts

        .data
| Pre-shifted into the register's own bit positions, so the switch is
| an OR and not a shift on a path that runs a hundred times a frame.
sn_bank_scr:    .byte   (2<<6)
sn_bank_bulk:   .byte   (3<<6)
