/* Flat-binary link script for IOFW / sub-engine programs.
 * Load address comes from the command line: -Ttext=<base>.
 * crt0.o must be first on the link line; entry is its _start.
 */
ENTRY(_start)
SECTIONS
{
  .text : { *(.text*) *(.rodata*) }
  .data : { *(.data*) }
  . = ALIGN(2);
  __bss_start = .;
  .bss (NOLOAD) : { *(.bss*) *(COMMON) }
  . = ALIGN(2);
  __bss_end = .;
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
