/* linker.ld */
ENTRY(start)
SECTIONS
{
    . = 0x10000;    /* kernel loaded right after 64K boundary */

    .text : { *(.text) }
    .data : { *(.data) }
    .bss  : { *(.bss) }
}
