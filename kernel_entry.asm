; kernel_entry.asm
[bits 32]
[extern kernel_main]
global start
start:
    mov esp, 0x90000        ; stack top (we can also reuse stack from bootloader)
    call kernel_main
    jmp $
