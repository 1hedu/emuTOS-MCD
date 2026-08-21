/* GNU as macros for the byte-exact little-endian GEOS memory image.
 *
 * Every `off' and `base' argument below is a GEOS LOGICAL address -- the number
 * geos_map.inc holds -- and the anchor is taken off here, once, so that no call
 * site has to.  A6 is logical $8000 and a signed displacement off it reaches all
 * sixty-four kilobytes; that subtraction is the whole of the difference. */

/* THE FLAGS THIS LEAVES DESCRIBE THE LOW BYTE, NOT THE WORD.  The last
 * instruction is a `move.b' of the low half, so Z is set when the low byte is
 * zero whatever the high byte holds, and a `beq' written straight after it
 * reads $3400 as null.  Put an explicit `tst.w' before any branch on the
 * result; tools/verify_le16_flags.py checks that every one of them has it.
 *
 * WHAT THIS COST.  GEOS keeps its callbacks in sixteen-bit vectors and the
 * kernal guards every one of them with `LOAD_LE16 vec,%d0 / beq' -- appMain,
 * keyVector, mouseVector, inputVector, otherPressVec, RecoverVector,
 * IconDescVec, StringFaultVec, and DoMove's own done-vector.  So one address in
 * every two hundred and fifty-six silently did nothing, and which addresses
 * those are is decided by the linker.  Adding a routine to TopDesk moved
 * td_win_move_done onto $3400 and the interactive window move stopped working:
 * the frame armed, followed the pointer, and dropped, and the callback that
 * writes the new rectangle back was skipped by that `beq'.  Nothing else
 * changed and nothing pointed at the kernal.  tools/run_window_move_test.sh is
 * what caught it, and only because it reads the geometry rather than the
 * screen. */
.macro LOAD_LE16 off, reg
    moveq   #0,\reg
    move.b  (\off+1-GEOS_BIAS)(%a6),\reg
    lsl.w   #8,\reg
    move.b  (\off-GEOS_BIAS)(%a6),\reg
.endm

.macro STORE_LE16 reg, off, tmp
    move.b  \reg,(\off-GEOS_BIAS)(%a6)
    move.w  \reg,\tmp
    lsr.w   #8,\tmp
    move.b  \tmp,(\off+1-GEOS_BIAS)(%a6)
.endm

.macro LOAD_LE16_INDEXED base, idx, reg, tmp
    moveq   #0,\reg
    move.b  (\base+1-GEOS_BIAS)(%a6,\idx.w),\reg
    lsl.w   #8,\reg
    move.b  (\base-GEOS_BIAS)(%a6,\idx.w),\reg
.endm

.macro STORE_LE16_INDEXED reg, base, idx, tmp
    move.b  \reg,(\base-GEOS_BIAS)(%a6,\idx.w)
    move.w  \reg,\tmp
    lsr.w   #8,\tmp
    move.b  \tmp,(\base+1-GEOS_BIAS)(%a6,\idx.w)
.endm

.macro LOGICAL_TO_A logical_reg, addr_reg
    movea.l %a5,\addr_reg
    adda.l  \logical_reg,\addr_reg
.endm

.macro LOAD_LOGICAL_PTR off, data_reg, addr_reg
    LOAD_LE16 \off,\data_reg
    LOGICAL_TO_A \data_reg,\addr_reg
.endm

.macro SET_LE16_IMM off, value, reg
    move.w  #\value,\reg
    move.b  \reg,(\off-GEOS_BIAS)(%a6)
    lsr.w   #8,\reg
    move.b  \reg,(\off+1-GEOS_BIAS)(%a6)
.endm

/* Serialise a main-thread VDP transaction against the VBlank ISR.
 *
 * The VDP control port takes an address command as TWO words and latches
 * first/second internally.  geos_vdp_present_dirty runs inside vblank_isr and
 * programs the same port, so an interrupt taken between a main-thread
 * command's two words -- or anywhere inside the data burst that follows it --
 * desynchronises that latch: the ISR's first word completes the interrupted
 * command, its second starts a new one, and from there both transfers write to
 * arbitrary VRAM addresses.  The ISR cannot save and restore the address port
 * (it is write-only), so the main thread has to keep its transactions whole.
 *
 * Only the main thread needs this; the ISR already runs at a raised level. */
.macro VDP_ATOMIC_BEGIN
    move.w  %sr,-(%sp)
    move.w  #0x2700,%sr
.endm

.macro VDP_ATOMIC_END
    move.w  (%sp)+,%sr
.endm
