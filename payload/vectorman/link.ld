/* VECTOR.MDP's image, laid out for $FF7000. The same rules as
 * payload/sonic/link.ld: the header first, nothing in a hole, and no
 * .bss -- vm.c keeps everything that changes in the working space the
 * servant hands it, and is built so that an initialised static is .data. */
OUTPUT_ARCH(m68k)
ENTRY(payload_start)

SECTIONS
{
    . = 0xFF7000;

    .head : { *(.head) }
    .text : { *(.text) *(.text.*) }
    . = ALIGN(2);
    .rodata : { *(.rodata) *(.rodata.*) }
    . = ALIGN(2);
    .data : { *(.data) *(.data.*) }
    . = ALIGN(2);
    image_end = .;

    .bss (NOLOAD) : { *(.bss) *(.bss.*) *(COMMON) }
    ASSERT(SIZEOF(.bss) == 0, "payload has a .bss -- put it in the workspace")

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame) }
}
