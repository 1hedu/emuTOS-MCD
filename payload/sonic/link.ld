/* SONIC.MDP's image, laid out for $FF7000.
 *
 * A payload is a flat file with its header at the front, so the only
 * thing this has to guarantee is that .head comes first and that
 * nothing lands in a hole -- image_end is what the header's length
 * field is, and the loader refuses a file whose length is not its size.
 *
 * There is no .bss: an uninitialised section would be a hole in a flat
 * file, and the payload's workspace is where uninitialised bytes go.
 * The ASSERT says so out loud rather than letting one appear quietly.
 */
OUTPUT_ARCH(m68k)
ENTRY(payload_start)

SECTIONS
{
    . = 0xFF7000;

    .head : { *(.head) }
    .text : { *(.text) *(.text.*) }
    . = ALIGN(2);
    .rodata : { *(.rodata) *(.rodata.*) *(.blobdata) }
    . = ALIGN(2);
    .data : { *(.data) *(.data.*) }
    . = ALIGN(2);
    image_end = .;

    .bss (NOLOAD) : { *(.bss) *(COMMON) }
    ASSERT(SIZEOF(.bss) == 0, "payload has a .bss -- put it in the workspace")

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame) }
}
