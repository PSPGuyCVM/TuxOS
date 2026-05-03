; disk.asm
[bits 16]
disk_load:
    push dx                  ; save number of sectors
    mov ah, 0x02             ; BIOS read sector function
    mov al, dh               ; number of sectors
    mov ch, 0x00             ; cylinder 0
    mov dh, 0x00             ; head 0
    mov cl, 0x02             ; start at sector 2 (sector 1 is bootloader)
    int 0x13
    jc disk_error
    pop dx
    cmp al, dh               ; sectors read == requested?
    jne disk_error
    ret

disk_error:
    mov si, ERROR_MSG
    call print_string
    jmp $

print_string:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0e
    int 0x10
    jmp print_string
.done:
    ret

ERROR_MSG db "Disk read error", 0
