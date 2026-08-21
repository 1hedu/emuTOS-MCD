/* Flat layout for a TOS .PRG: text and read-only data run together,
   then data, then bss. Linked at 0; tools/mkprg.py adds the header.

   ALIGN on every boundary, because a 68000 takes an address error on a
   word or long access to an odd address and nothing here would stop one
   arising. CDTEST.PRG crashed exactly this way: a thirteen-byte string
   in .data left .bss starting on an odd address, so every static in the
   program was misaligned and the first long written to one killed it --
   `move.l d0,-(a0)` with a0 = 0x1a9b5. The panic even printed
   `bss=0001a7b1`, which says it outright once you think to look.

   Four rather than two: a long wants four to be quick, the waste is at
   most three bytes a section, and word alignment alone would leave the
   same class of bug one struct layout away. */
OUTPUT_FORMAT("elf32-m68k")
SECTIONS
{
  . = 0;
  .text : { *(.text) *(.text.*) *(.rodata) *(.rodata.*) . = ALIGN(4); }
  .data : { *(.data) *(.data.*) . = ALIGN(4); }
  .bss  : { *(.bss) *(.bss.*) *(COMMON) . = ALIGN(4); }
}
