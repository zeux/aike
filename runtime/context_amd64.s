.set C_RBX, 0
.set C_RBP, 8
.set C_RSP, 16
.set C_R12, 24
.set C_R13, 32
.set C_R14, 40
.set C_R15, 48
.set C_RIP, 56

.global contextCapture
.global contextResume
.global contextCreate

# %rdi = context
contextCapture:
	mov 0(%rsp), %rax # rip = return address
	lea 8(%rsp), %rcx # rsp was adjusted by the call insn
	mov %rbx, C_RBX(%rdi)
	mov %rbp, C_RBP(%rdi)
	mov %rcx, C_RSP(%rdi)
	mov %r12, C_R12(%rdi)
	mov %r13, C_R13(%rdi)
	mov %r14, C_R14(%rdi)
	mov %r15, C_R15(%rdi)
	mov %rax, C_RIP(%rdi)
	mov $1, %al
	ret

# %rdi = context
contextResume:
	xor %eax, %eax
	mov C_RBX(%rdi), %rbx
	mov C_RBP(%rdi), %rbp
	mov C_RSP(%rdi), %rsp
	mov C_R12(%rdi), %r12
	mov C_R13(%rdi), %r13
	mov C_R14(%rdi), %r14
	mov C_R15(%rdi), %r15
	jmp *C_RIP(%rdi)

# %rdi = context
# %rsi = entry
# %rdx = stack
# %rcx = stack size
contextCreate:
	xor %eax, %eax
	lea -8(%rcx, %rdx), %rdx # stack has to have offset 8 mod 16 in the function prologue
	mov %rax, 0(%rdx) # protect against entry returning
	mov %rax, C_RBX(%rdi)
	mov %rdx, C_RBP(%rdi)
	mov %rdx, C_RSP(%rdi)
	mov %rax, C_R12(%rdi)
	mov %rax, C_R13(%rdi)
	mov %rax, C_R14(%rdi)
	mov %rax, C_R15(%rdi)
	mov %rsi, C_RIP(%rdi)
	ret
