| The raster palette's line interrupt handler.
|
| An ST program that wants more than sixteen colours on screen -- a
| paint program's colour picker, a game's status panel -- reprograms the
| palette partway down the screen from MFP Timer B. The VDP has the same
| thing in its line interrupt (level 4, counter in register 10), and
| this is what runs on it: it works through a list of events that
| ras_follow() in main.c builds from the program's table, one event per
| interrupt, each one some CRAM writes and the next counter value.
|
| Register 10 is reloaded when the counter runs out, with whatever it
| holds at that moment, so a value written here sets the gap after the
| *next* interrupt, not after this one. The list is built for that:
| every frame starts with two empty events at lines 2 and 5 (the
| counter is 2 all through the vblank), which leaves room to have set
| the gap to the first real one in time, and ends with one at line 216
| that puts the ordinary palette back, sets 2 for the next vblank, and
| takes up a new list if there is one waiting. All of that happens in
| the borders, outside the 200 lines of picture.
|
| Event layout, word-aligned:
|   word  register 10 command (0x8A00 | value) to write after the CRAM
|   word  flags: bit 15 = the frame's last event; low byte = the line
|         the event is for
|   then runs, each: long CRAM write command, word count, count words;
|   a zero long ends them.
|
| The chain only holds if no interrupt is missed. One taken late -- the
| main loop masks them around its own VDP writes -- lets a reload go by
| with the wrong value, and from then on every event would fire on the
| wrong line, frame after frame. So each event knows its line and the
| handler checks it against the V counter; on a mismatch it does no
| colours, goes back to the first event and sets 2, which the vblank
| then reloads, so the next frame starts in step.
|
| The handler moves the VDP's address, and nothing can read it back.
| RAS_HIT says so; scd_tile, which writes VRAM all through the display,
| redoes a tile when it is set. Everything else that writes the VDP does
| it with interrupts off.

        .text
        .globl  ras_hint

VDP_DATA_PORT = 0xC00000
VDP_CTRL_PORT = 0xC00004
VDP_VCOUNT    = 0xC00008
RAS_PTR   = 0xFFED00
RAS_START = 0xFFED04
RAS_NEXT  = 0xFFED08
RAS_HIT   = 0xFFED0C
RAS_BASE  = 0xFFED10

ras_hint:
        movem.l %d0-%d1/%a0-%a2,-(%sp)
        st      RAS_HIT
        lea     VDP_CTRL_PORT,%a1
        lea     VDP_DATA_PORT,%a2
        move.l  RAS_PTR,%a0
        move.w  (%a0)+,%d1              | register 10, written last
        swap    %d1
        move.w  (%a0)+,%d1              | flags
        move.b  VDP_VCOUNT,%d0          | this line, or the next one
        sub.b   %d1,%d0
        cmp.b   #1,%d0
        bhi.s   lost
run:
        move.l  (%a0)+,%d0
        beq.s   runs_done
        move.l  %d0,(%a1)               | CRAM address
        move.w  (%a0)+,%d0              | words
        lsr.w   #1,%d0                  | longs; the odd one is in C
        bcc.s   even
        move.w  (%a0)+,(%a2)
even:
        subq.w  #1,%d0
        bmi.s   run
longs:
        move.l  (%a0)+,(%a2)
        dbra    %d0,longs
        bra.s   run
runs_done:
        tst.w   %d1
        bmi.s   last
        move.l  %a0,RAS_PTR
        swap    %d1
        move.w  %d1,(%a1)
        movem.l (%sp)+,%d0-%d1/%a0-%a2
        rte

| The bottom of the frame: the ordinary sixteen back, and entry 18 --
| plane B's copy of colour 0, the paper -- with them.
last:
        swap    %d1
        move.w  %d1,(%a1)
        move.l  #0xC0000000,(%a1)       | CRAM 0
        lea     RAS_BASE,%a0
        moveq   #7,%d0
base:
        move.l  (%a0)+,(%a2)
        dbra    %d0,base
        move.l  #0xC0240000,(%a1)       | CRAM entry 18
        move.w  RAS_BASE,(%a2)
        move.l  RAS_NEXT,%d0
        beq.s   same
        move.l  %d0,RAS_START
        clr.l   RAS_NEXT
same:
        move.l  RAS_START,RAS_PTR
        movem.l (%sp)+,%d0-%d1/%a0-%a2
        rte

lost:
        move.w  #0x8A02,(%a1)
        move.l  RAS_START,RAS_PTR
        movem.l (%sp)+,%d0-%d1/%a0-%a2
        rte
