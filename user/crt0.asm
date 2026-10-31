; Entry point for Colloquy user programs: call main, then exit with its result.
bits 64
section .text
global _start
extern main
_start:
    xor rbp, rbp
    and rsp, -16
    call main
    mov rdi, rax
    xor eax, eax        ; SYS_EXIT
    int 0x80
.hang:
    jmp .hang
