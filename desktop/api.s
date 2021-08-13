[section .text]

[global _APISyscall]
_APISyscall:
	push	rbp
	push	rbx
	push	r15
	push	r14
	push	r13
	push	r12
	push	r11
	push	rcx
	mov	r12,rsp
	syscall
	mov	rsp,r12
	pop	rcx
	pop	r11
	pop	r12
	pop	r13
	pop	r14
	pop	r15
	pop	rbx
	pop	rbp
	ret

[global _EsCRTsetjmp]
_EsCRTsetjmp:
	mov	[rdi + 0x00],rsp
	mov	[rdi + 0x08],rbp
	mov	[rdi + 0x10],rbx
	mov	[rdi + 0x18],r12
	mov	[rdi + 0x20],r13
	mov	[rdi + 0x28],r14
	mov	[rdi + 0x30],r15
	mov	rax,[rsp]
	mov	[rdi + 0x38],rax
	xor	rax,rax
	ret

[global _EsCRTlongjmp]
_EsCRTlongjmp:
	mov	rsp,[rdi + 0x00]
	mov	rbp,[rdi + 0x08]
	mov	rbx,[rdi + 0x10]
	mov	r12,[rdi + 0x18]
	mov	r13,[rdi + 0x20]
	mov	r14,[rdi + 0x28]
	mov	r15,[rdi + 0x30]
	mov	rax,[rdi + 0x38]
	mov	[rsp],rax
	mov	rax,rsi
	cmp	rax,0
	jne	.return
	mov	rax,1
	.return:
	ret

[global EsTimeStamp]
EsTimeStamp:
	rdtsc
	shl	rdx,32
	or	rax,rdx
	ret

[global EsCRTsqrt]
EsCRTsqrt:
	sqrtsd	xmm0,xmm0
	ret

[global EsCRTsqrtf]
EsCRTsqrtf:
	sqrtss	xmm0,xmm0
	ret

[global ProcessorCheckStackAlignment]
ProcessorCheckStackAlignment:
	mov	rax,rsp
	and	rax,15
	cmp	rax,8
	jne	$
	ret

[global ProcessorTLSRead]
ProcessorTLSRead:
	mov	rax,[fs:rdi]
	ret

[global ProcessorTLSWrite]
ProcessorTLSWrite:
	mov	[fs:rdi],rsi
	ret

[global __cyg_profile_func_enter]
__cyg_profile_func_enter:
	ret

[global __cyg_profile_func_exit]
__cyg_profile_func_exit:
	ret
