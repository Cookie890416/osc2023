SECTIONS
{
    . = 0xffff000000000000; # kernel space
    . += 0x80000; #kernel load address
    _kernel_start = .;

    /* .text section: Contains executable code */
    .text : {
        /* KEEP directive ensures that the .text.boot section is kept */
        KEEP(*(.text.boot))

        /* Match any section name starting with .text, excluding linker-specific sections */
        *(.text .text.* .gnu.linkonce.t*)
    }

    /* .rodata section: Contains read-only data */
    .rodata : {
        /* Match any section name starting with .rodata, excluding linker-specific sections */
        *(.rodata .rodata.* .gnu.linkonce.r*)
    }

    /* Define the symbol _data with the current address */
    PROVIDE(_data = .);

    /* .data section: Contains initialized data */
    .data : {
        /* Match any section name starting with .data, excluding linker-specific sections */
        *(.data .data.* .gnu.linkonce.d*)
    }

    /* .bss section: Contains uninitialized (zero-initialized) data */
    .bss (NOLOAD) : {
        /* Align the current address to a 16-byte boundary */
        . = ALIGN(16);

        /* Define the symbol __bss_start with the current address */
        __bss_start = .;

        /* Match any section name starting with .bss, excluding linker-specific sections */
        *(.bss .bss.*)

        /* Match COMMON symbols (uninitialized global variables) */
        *(COMMON)

        /* Define the symbol __bss_end with the current address */
        __bss_end = .;
    }

    _kernel_end = .;
    _heap_start = .;

    . = 0xffff00002c000000;
    _stack_end = .;
    . = 0xffff00003c000000;
    _stack_top = .;

   /* Discard unnecessary sections */
    /DISCARD/ : {
        /* Discard sections with names containing .comment, .gnu, .note, and .eh_frame */
        *(.comment) *(.gnu*) *(.note*) *(.eh_frame*)
    }
}
__bss_size = (__bss_end - __bss_start) >> 3;
