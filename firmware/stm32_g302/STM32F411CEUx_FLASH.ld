/**
 * Linker script for STM32F411CEU6 (512 KB Flash, 128 KB SRAM)
 */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

ENTRY(Reset_Handler)

_stack_size  = 0x1000;
_heap_size   = 0x200;

_stack_end   = ORIGIN(RAM) + LENGTH(RAM);
_stack_start = _stack_end - _stack_size;

SECTIONS
{
    .vectors : ALIGN(4)
    {
        KEEP(*(.vectors))
        . = ALIGN(4);
    } > FLASH

    .text : ALIGN(4)
    {
        *(.text .text.* .gnu.linkonce.t.*)
        . = ALIGN(4);
    } > FLASH

    .rodata : ALIGN(4)
    {
        *(.rodata .rodata.* .gnu.linkonce.r.*)
        . = ALIGN(4);
    } > FLASH

    .preinit_array : ALIGN(4)
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH

    .init_array : ALIGN(4)
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH

    .fini_array : ALIGN(4)
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(.fini_array*))
        KEEP(*(SORT(.fini_array.*)))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH

    /DISCARD/ :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    }

    .data : ALIGN(4)
    {
        _data_start = .;
        *(.data .data.* .gnu.linkonce.d.*)
        . = ALIGN(4);
        _data_end = .;
    } > RAM AT > FLASH

    _data_load = LOADADDR(.data);

    .bss : ALIGN(4)
    {
        _bss_start = .;
        *(.bss .bss.* .gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > RAM

    .heap : ALIGN(8)
    {
        _heap_start = .;
        . = . + _heap_size;
        . = ALIGN(8);
        _heap_end = .;
    } > RAM
}
