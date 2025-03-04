MEMORY
{
    FLASH (rx) : ORIGIN = 0x0000A100, LENGTH = 0x7A000
    SRAM_LO_STACK (rwx) : ORIGIN = 0x1FFF0000, LENGTH = 32K
    SRAM_LO (rwx) : ORIGIN = 0x1FFFC000, LENGTH = 16K
    SRAM_HI (rwx) : ORIGIN = 0x20000000, LENGTH = 192K
}

SECTIONS
{
    .text :
    {
        _start_text = .;
        KEEP(*(.isr_vector))
        KEEP(*(.after_vectors*))
        *(.text*)
        *(.rodata*)
        *(.init*)
        *(.fini*)
        . = ALIGN(4);
        _end_text = .;
    } > FLASH
    
    .edidx :
    {
        . = ALIGN(4);
        *(.ARM.exidx*)
    } > FLASH

    _stored_data = .;

    .data : AT (_stored_data)
    {
        _start_data = .;
        __data_section_table = .;
        *(.data*)
        . = ALIGN(4);
        _end_data = .;
        __data_section_table_end = .;
    } > SRAM_HI

    .bss(NOLOAD) :
    {
        _start_bss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _end_bss = .;
        _end = .;
        __bss_section_table_end = .;
        . = ALIGN(4);
        *(.noinit_Heap5)
    } > SRAM_HI

    .lomem(NOLOAD) :
    {
        *(.noinit.*)
        . = ALIGN(4);
    } > SRAM_LO

}

_start_heap = _end;
__StackTop  = ORIGIN(SRAM_LO_STACK) + 48K;
_vStackTop = __StackTop;
