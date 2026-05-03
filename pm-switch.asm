; pm-switch.asm
[bits 16]
switch_to_pm:
    cli                      ; disable interrupts
    lgdt [gdt_descriptor]    ; load GDT

    mov eax, cr0
    or eax, 0x1              ; set protected mode bit
    mov cr0, eax

    jmp dword CODE_SEG:init_pm     ; far jump to clear pipeline

[bits 32]
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov ebp, 0x90000         ; new stack at top of free space
    mov esp, ebp

    call BEGIN_PM            ; call kernel entry point

BEGIN_PM:
    ; We'll link the kernel to start at 0x100000 (1 MiB) but loaded at 0x1000:0x0
    ; (physical 0x10000). We'll jump there.
    ; Our linker script sets KERNEL_OFFSET = 0x100000
    ; but the kernel was loaded at 0x10000. So we'll relocate or use position-independent code.
    ; For simplicity, we'll put kernel entry right after the bootloader at 0x7e00.
    ; Let's design: bootloader loads kernel starting at 0x1000 (segment) = 0x10000.
    ; Then we jump exactly to 0x10000.
    call 0x10000
    jmp $                    ; infinite loop if kernel returns
