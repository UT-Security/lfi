#define N_RTCALLS 256
#define PROC_REGS 32

.macro SAVE_PARTIAL_REGS
    mov lfi_myctx@gottpoff(%rip), %rcx
    movq %fs:(%rcx), %rcx
    mov %rsp, PROC_REGS+8*0(%rcx)
    mov %rax, PROC_REGS+8*1(%rcx)
    // rcx clobbered
    mov %rdx, PROC_REGS+8*3(%rcx)
    mov %rbx, PROC_REGS+8*4(%rcx)
    mov %rbp, PROC_REGS+8*5(%rcx)
    mov %rsi, PROC_REGS+8*6(%rcx)
    mov %rdi, PROC_REGS+8*7(%rcx)
    mov %r8,  PROC_REGS+8*8(%rcx)
    mov %r9,  PROC_REGS+8*9(%rcx)
    mov %r10, PROC_REGS+8*10(%rcx)
    mov %r11, PROC_REGS+8*11(%rcx)
    // TODO: save SIMD registers?
.endm

.p2align 4
.global lfi_ctx_internal
// This function assumes the return address is in r11
lfi_ctx_internal:
    mov lfi_myctx@gottpoff(%rip), %rax
    movq %fs:(%rax), %rax
    jmpq *%r11

.p2align 4
.global lfi_ctx_entry
lfi_ctx_entry:
    // save callee-saved registers to stack
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %rbx
    pushq %rbp
    // save stack to kstackp
    mov %rsp, (%rsi)
    jmp lfi_restore_regs

.p2align 4
.globl lfi_restore_regs
lfi_restore_regs:
    mov PROC_REGS+8*0(%rdi), %rsp
    mov PROC_REGS+8*1(%rdi), %rax
    mov PROC_REGS+8*2(%rdi), %rcx
    mov PROC_REGS+8*3(%rdi), %rdx
    mov PROC_REGS+8*4(%rdi), %rbx
    mov PROC_REGS+8*5(%rdi), %rbp
    mov PROC_REGS+8*6(%rdi), %rsi
    mov PROC_REGS+8*8(%rdi), %r8
    mov PROC_REGS+8*9(%rdi), %r9
    mov PROC_REGS+8*10(%rdi), %r10
    mov PROC_REGS+8*11(%rdi), %r11
    mov PROC_REGS+8*12(%rdi), %r12
    mov PROC_REGS+8*13(%rdi), %r13
    mov PROC_REGS+8*14(%rdi), %r14
    mov PROC_REGS+8*15(%rdi), %r15
    mov PROC_REGS+8*7(%rdi), %rdi
    jmpq *%r11

.p2align 4
.global lfi_asm_ctx_exit
lfi_asm_ctx_exit:
    movq %rdi, %rsp
    movq %rsi, %rax
    popq %rbp
    popq %rbx
    popq %r12
    popq %r13
    popq %r14
    popq %r15
    ret

// Accelerated return for library sandboxes.
.p2align 4
.global lfi_ret
lfi_ret:
	movq lfi_myctx@gottpoff(%rip), %r11
	movq %fs:(%r11), %r11

    movq 16(%r11), %r14
    test %r14, %r14
    jnz .lfi_ret_check_ok
    int3
.lfi_ret_check_ok:
    movq $0, 16(%r11)
    
	movq 0(%r11), %rsp // kstackp
    // restore user sp to its initial value
    popq %rbp
    movq %rbp, PROC_REGS(%r11)
    popq %rbp // pop dummy value
	// return value should already be in %rax
	popq %rbp
	popq %rbx
	popq %r12
	popq %r13
	popq %r14
	popq %r15
	ret
	int3

// Loads the sandbox's thread pointer into %rax. Does not clobber flags.
.p2align 4
.global lfi_get_tp
lfi_get_tp:
    // %rax = myproc
    mov lfi_myctx@gottpoff(%rip), %rax
    movq %fs:(%rax), %rax
    // %rax = tp
    movq 8(%rax), %rax
    jmp *%r11
    int3

// Sets the sandbox's thread pointer to %rax. This function is not used on
// x86-64, since libc will set the thread pointer via an archprctl system call
// rather than wrfsbase.
.p2align 4
.global lfi_set_tp
lfi_set_tp:
    mov lfi_myctx@gottpoff(%rip), %r14
    movq %fs:(%r14), %r14
    mov %rax, 8(%r14)
    mov PROC_REGS+8*14(%r14), %r14
    jmp *%r11
    int3

.p2align 4
.global lfi_debug_mask
lfi_debug_mask:
    // Load LFI context into %r14
    mov lfi_myctx@gottpoff(%rip), %r14
    mov %fs:(%r14), %r14

    // Swap to application stack
    mov %rsp, 24(%r14)
    mov (%r14), %rsp

    pushq %r11 // save return address
    pushfq // save flags
    pushq %r14 // save ctx

    movq %r15, %r11

    mov PROC_REGS+8*15(%r14), %r15
    mov PROC_REGS+8*14(%r14), %r14

    pushq %r11
    andq %r15, %r11
    orq %r14, %r11
    cmpq 0(%rsp), %r11
    je .lfi_debug_mask_ok
    int3

.lfi_debug_mask_ok:
    popq %r11 // pop pushed %r11
    popq %r14 // pop ctx
    popfq // restore flags
    popq %r11 // pop return address

    mov 24(%r14), %rsp // restore sandbox stack
    mov PROC_REGS+8*14(%r14), %r14 // restore sandbox base
    jmp *%r11
    int3

.p2align 4
.global lfi_debug_check
lfi_debug_check:
    mov lfi_myctx@gottpoff(%rip), %r14
    movq %fs:(%r14), %r14
    mov 16(%r14), %r14

    test %r14, %r14
    jnz .lfi_debug_check_ok
    int3

.lfi_debug_check_ok:
    mov lfi_myctx@gottpoff(%rip), %r14
    movq %fs:(%r14), %r14
    mov PROC_REGS+8*14(%r14), %r14
    jmp *%r11
    int3

.p2align 4
.global lfi_syscall_entry
lfi_syscall_entry:
    SAVE_PARTIAL_REGS
    // Proc* is now in %rcx
    mov (%rcx), %rsp // load stack
    pushq %rcx
    // syscall return address
    pushq %r11
    sub $8, %rsp
    mov %rcx, %rdi
    call lfi_syscall_handler
    add $8, %rsp
    popq %r11
    popq %rdi
    // fallthrough to restore_partial_regs

// Restore only caller-saved registers.
.globl lfi_restore_partial_regs
lfi_restore_partial_regs:
    mov  PROC_REGS+8*0(%rdi), %rsp
    mov  PROC_REGS+8*1(%rdi), %rax
    mov  $0, %rcx // clobbered
    mov  PROC_REGS+8*3(%rdi), %rdx
    mov  PROC_REGS+8*4(%rdi), %rbx
    mov  PROC_REGS+8*5(%rdi), %rbp
    mov  PROC_REGS+8*6(%rdi), %rsi
    mov  PROC_REGS+8*8(%rdi), %r8
    mov  PROC_REGS+8*9(%rdi), %r9
    mov  PROC_REGS+8*10(%rdi), %r10
    mov  PROC_REGS+8*7(%rdi), %rdi
    // TODO: restore SIMD regs
    jmpq *%r11
    int3

.section .note.GNU-stack,"",@progbits
