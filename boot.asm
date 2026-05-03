; boot.asm -------------------------------------------------------------
[org 0x7c00]
[bits 16]

KERNEL_OFFSET equ 0x1000          ; kernel loaded at 0x10000

start:
    mov [BOOT_DRIVE], dl
    mov bp, 0x9000
    mov sp, bp

    call load_kernel
    call switch_to_pm
    jmp $

load_kernel:
    mov ax, 0x1000
    mov es, ax
    mov bx, 0x0000
    mov dh, 40
    mov dl, [BOOT_DRIVE]
    call disk_load
    ret

%include "disk.asm"
%include "gdt.asm"
%include "pm-switch.asm"

BOOT_DRIVE db 0
times 510 - ($ - $$) db 0
dw 0xaa55
