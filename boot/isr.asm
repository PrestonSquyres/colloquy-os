; Interrupt entry stubs. Every vector lands in isr_common, which hands the
; saved frame to C and resumes whatever frame C returns (that is how the
; scheduler switches tasks).
bits 64
section .text
extern isr_dispatch

%macro ISR_NOERR 1
isr%1:
    push 0
    push %1
    jmp isr_common
%endmacro
%macro ISR_ERR 1
isr%1:
    push %1
    jmp isr_common
%endmacro

%assign i 0
%rep 256
  %if i = 8 || i = 10 || i = 11 || i = 12 || i = 13 || i = 14 || i = 17 || i = 21 || i = 29 || i = 30
    ISR_ERR i
  %else
    ISR_NOERR i
  %endif
  %assign i i+1
%endrep

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rdi, rsp
    call isr_dispatch
    mov rsp, rax
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16
    iretq

section .rodata
global isr_table
isr_table:
%assign i 0
%rep 256
    dq isr %+ i
  %assign i i+1
%endrep
