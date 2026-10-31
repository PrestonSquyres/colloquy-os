; Colloquy boot stub: Multiboot2 header, 32-bit -> 64-bit long mode, call kmain.
section .multiboot2
align 8
mb2_start:
    dd 0xE85250D6                 ; multiboot2 magic
    dd 0                          ; architecture: i386 protected mode
    dd mb2_end - mb2_start
    dd -(0xE85250D6 + 0 + (mb2_end - mb2_start))
    dw 0, 0                       ; end tag
    dd 8
mb2_end:

section .bss
align 4096
pml4:   resb 4096
pdpt:   resb 4096
pd:     resb 4096
global boot_stack_top
boot_stack: resb 32768
boot_stack_top:
global mb_info_addr
mb_info_addr: resq 1
global mb_magic
mb_magic: resq 1

section .rodata
align 8
gdt64:
    dq 0
    dq 0x00AF9A000000FFFF         ; 0x08: 64-bit ring-0 code
    dq 0x00CF92000000FFFF         ; 0x10: ring-0 data
gdt64_ptr:
    dw $ - gdt64 - 1
    dq gdt64

section .text
bits 32
global _start
_start:
    cli
    mov esp, boot_stack_top
    mov [mb_info_addr], ebx
    mov [mb_magic], eax

    ; identity-map the first 1 GiB with 2 MiB pages
    mov eax, pdpt
    or eax, 3
    mov [pml4], eax
    mov eax, pd
    or eax, 3
    mov [pdpt], eax
    xor ecx, ecx
.map:
    mov eax, 0x200000
    mul ecx
    or eax, 0x83                  ; present | writable | huge
    mov [pd + ecx*8], eax
    inc ecx
    cmp ecx, 512
    jne .map

    mov eax, pml4
    mov cr3, eax
    mov eax, cr4
    or eax, 1 << 5                ; PAE
    mov cr4, eax
    mov ecx, 0xC0000080           ; EFER
    rdmsr
    or eax, 1 << 8                ; LME
    wrmsr
    mov eax, cr0
    or eax, 1 << 31               ; paging
    mov cr0, eax

    lgdt [gdt64_ptr]
    jmp 0x08:long_mode

bits 64
extern kmain
long_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov rsp, boot_stack_top
    call kmain
.hang:
    cli
    hlt
    jmp .hang
