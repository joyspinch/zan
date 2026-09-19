/* Linker script for booting a self-host-compiled ELF relocatable under
 * `qemu-system-aarch64 -M virt -kernel <image>`. The virt board's RAM
 * starts at 0x40000000; qemu's built-in DTB sits near its base, so the
 * image links above it. Provides __bss_start/__bss_end for stub.c's
 * boot-time clear. */
ENTRY(_start)
SECTIONS {
    . = 0x40200000;
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : {
        *(.data .data.*)
        *(.got .got.*)
        *(.got.plt)
    }
    . = ALIGN(16);
    __bss_start = .;
    .bss : { *(.bss .bss.*) *(COMMON) }
    . = ALIGN(16);
    __bss_end = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
