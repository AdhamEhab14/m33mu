/* STM32H563 secure flash and RAM; no .data or .bss */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x0C000000, LENGTH = 0x00200000
    RAM   (rwx) : ORIGIN = 0x30000000, LENGTH = 0x000A0000
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
    .text : { *(.text*) *(.rodata*) } > FLASH
    .bss (NOLOAD) : { *(.bss*) *(COMMON) } > RAM
}
