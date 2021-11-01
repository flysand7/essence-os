[bits 64]

[global ArchSwitchContext]
[global GetCurrentThread]
[global GetLocalStorage]
[global MMArchSafeCopy]
[global ProcessorAPStartup]
[global ProcessorAreInterruptsEnabled]
[global ProcessorDebugOutputByte]
[global ProcessorDisableInterrupts]
[global ProcessorEnableInterrupts]
[global ProcessorFakeTimerInterrupt]
[global ProcessorFlushCodeCache]
[global ProcessorGetRBP]
[global ProcessorGetRSP]
[global ProcessorHalt]
[global ProcessorIn16]
[global ProcessorIn32]
[global ProcessorIn8]
[global ProcessorInstallTSS]
[global ProcessorInvalidateAllPages]
[global ProcessorInvalidatePage]
[global ProcessorOut16]
[global ProcessorOut32]
[global ProcessorOut8]
[global ProcessorReadCR3]
[global ProcessorReadMXCSR]
[global ProcessorReadTimeStamp]
[global ProcessorReset]
[global ProcessorSetAddressSpace]
[global ProcessorSetLocalStorage]
[global ProcessorSetThreadStorage]
[global _KThreadTerminate]
[global _start]
[global gdt_data]
[global pagingNXESupport]
[global pagingPCIDSupport]
[global pagingSMEPSupport]
[global pagingTCESupport]
[global processorGDTR]
[global simdSSE3Support]
[global simdSSSE3Support]
[global timeStampCounterSynchronizationValue]

[extern ArchNextTimer]
[extern InterruptHandler]
[extern KThreadTerminate]
[extern KernelMain]
[extern PostContextSwitch]
[extern SetupProcessor2]
[extern Syscall]
[extern installationID]
[extern PCSetupCOM1]
[extern PCDisablePIC]
[extern PCProcessMemoryMap]
[extern bootloaderID]
[extern bootloaderInformationOffset]

[section .bss]

align 16

%define stack_size 16384
stack: resb stack_size

%define idt_size 4096
idt_data: resb idt_size

%define cpu_local_storage_size 8192
; Array of pointers to the CPU local states
cpu_local_storage: resb cpu_local_storage_size

[section .data]

idt:
	.limit: dw idt_size - 1
	.base:  dq idt_data

cpu_local_storage_index:
	dq 0

pagingNXESupport:
	dd 1
pagingPCIDSupport:
	dd 1
pagingSMEPSupport:
	dd 1
pagingTCESupport:
	dd 1
simdSSE3Support:
	dd 1
simdSSSE3Support:
	dd 1

align 16
processorGDTR:
	dq 0
	dq 0

[section .text]

_start:
	mov	rax,0x63
	mov	fs,ax
	mov	gs,ax

	; Save the bootloader ID.
	mov	rax,bootloaderID
	mov	[rax],rsi

	; The MBR bootloader does not know the address of the RSDP. 
	cmp	rdi,0
	jne	.standard_acpi
	mov	[0x7FE8],rdi
	.standard_acpi:

	; Save the bootloader information offset.
	mov	rax,bootloaderInformationOffset
	mov	[rax],rdi

	; Install a stack
	mov	rsp,stack + stack_size

	; Load the installation ID.
	mov	rbx,installationID
	mov	rax,[rdi + 0x7FF0]
	mov	[rbx],rax
	mov	rax,[rdi + 0x7FF8]
	mov	[rbx + 8],rax

	; Unmap the identity paging the bootloader used
	mov	rax,0xFFFFFF7FBFDFE000
	mov	qword [rax],0
	mov	rax,cr3
	mov	cr3,rax

	call	PCSetupCOM1
	call	PCDisablePIC
	call	PCProcessMemoryMap

	; Install the interrupt handlers
%macro INSTALL_INTERRUPT_HANDLER 1
	mov	rbx,(%1 * 16) + idt_data
	mov	rdx,InterruptHandler%1
	call	InstallInterruptHandler
%endmacro
%assign i 0
%rep 256
	INSTALL_INTERRUPT_HANDLER i
%assign i i+1
%endrep

	; Save the location of the bootstrap GDT
	mov	rcx,processorGDTR
	sgdt	[rcx]

	; First stage of processor initilisation
	call	SetupProcessor1

	; Call the KernelMain function
	and	rsp,~0xF
	call	KernelMain

ProcessorReady:
	; Set the timer and become this CPU's idle thread.
	mov	rdi,1
	call	ArchNextTimer
	jmp	ProcessorIdle

SetupProcessor1:
	.enable_cpu_features:
	; Enable no-execute support, if available
	mov	eax,0x80000001
	cpuid
	and	edx,1 << 20
	shr	edx,20
	mov	rax,pagingNXESupport
	and	[rax],edx
	cmp	edx,0
	je	.no_paging_nxe_support
	mov	ecx,0xC0000080
	rdmsr
	or	eax,1 << 11
	wrmsr
	.no_paging_nxe_support:

	; x87 FPU
	fninit
	mov	rax,.cw
	fldcw	[rax]
	jmp	.cwa
	.cw:	dw 0x037A
	.cwa:

	; Enable SMEP support, if available
	; This prevents the kernel from executing userland pages
	; TODO Test this: neither Bochs or Qemu seem to support it?
	xor	eax,eax
	cpuid
	cmp	eax,7
	jb	.no_smep_support
	mov	eax,7
	xor	ecx,ecx
	cpuid
	and	ebx,1 << 7
	shr	ebx,7
	mov	rax,pagingSMEPSupport
	and	[rax],ebx
	cmp	ebx,0
	je	.no_smep_support
	mov	word [rax],2
	mov	rax,cr4
	or	rax,1 << 20
	mov	cr4,rax
	.no_smep_support:

	; Enable PCID support, if available
	mov	eax,1
	xor	ecx,ecx
	cpuid
	and	ecx,1 << 17
	shr	ecx,17
	mov	rax,pagingPCIDSupport
	and	[rax],ecx
	cmp	ecx,0
	je	.no_pcid_support
	mov	rax,cr4
	or	rax,1 << 17
	mov	cr4,rax
	.no_pcid_support:

	; Enable global pages
	mov	rax,cr4
	or	rax,1 << 7
	mov	cr4,rax

	; Enable TCE support, if available
	mov	eax,0x80000001
	xor	ecx,ecx
	cpuid
	and	ecx,1 << 17
	shr	ecx,17
	mov	rax,pagingTCESupport
	and	[rax],ecx
	cmp	ecx,0
	je	.no_tce_support
	mov	ecx,0xC0000080
	rdmsr
	or	eax,1 << 15
	wrmsr
	.no_tce_support:

	; Enable write protect, so copy-on-write works in the kernel, and MMArchSafeCopy will page fault in read-only regions.
	mov	rax,cr0
	or	rax,1 << 16
	mov	cr0,rax

	; Enable MMX, SSE and SSE2
	; These features are all guaranteed to be present on a x86_64 CPU
	mov	rax,cr0
	mov	rbx,cr4
	and	rax,~4
	or	rax,2
	or	rbx,512 + 1024
	mov	cr0,rax
	mov	cr4,rbx

	; Detect SSE3 and SSSE3, if available.
	mov	eax,1
	cpuid
	test	ecx,1 << 0
	jnz	.has_sse3
	mov	rax,simdSSE3Support
	and	byte [rax],0
	.has_sse3:
	test	ecx,1 << 9
	jnz	.has_ssse3
	mov	rax,simdSSSE3Support
	and	byte [rax],0
	.has_ssse3:

	; Enable system-call extensions (SYSCALL and SYSRET).
	mov	ecx,0xC0000080
	rdmsr
	or	eax,1
	wrmsr
	add	ecx,1
	rdmsr
	mov	edx,0x005B0048
	wrmsr
	add	ecx,1
	mov	rdx,SyscallEntry
	mov	rax,rdx
	shr	rdx,32
	wrmsr
	add	ecx,2
	rdmsr
	mov	eax,(1 << 10) | (1 << 9) ; Clear direction and interrupt flag when we enter ring 0.
	wrmsr

	; Assign PAT2 to WC.
	mov	ecx,0x277
	xor	rax,rax
	xor	rdx,rdx
	rdmsr
	and	eax,0xFFF8FFFF
	or	eax,0x00010000
	wrmsr

	.setup_cpu_local_storage:
	mov	ecx,0xC0000101
	mov	rax,cpu_local_storage
	mov	rdx,cpu_local_storage
	shr	rdx,32
	mov	rdi,cpu_local_storage_index
	add	rax,[rdi]
	add	qword [rdi],32 ; Space for 4 8-byte values at gs:0 - gs:31
	wrmsr

	.load_idtr:
	; Load the IDTR
	mov	rax,idt
	lidt	[rax]
	sti

	.enable_apic:
	; Enable the APIC!
	; Since we're on AMD64, we know that the APIC will be present.
	mov	ecx,0x1B
	rdmsr
	or	eax,0x800
	wrmsr
	and	eax,~0xFFF
	mov	edi,eax

	; Set the spurious interrupt vector to 0xFF
	mov	rax,0xFFFFFE00000000F0 ; LOW_MEMORY_MAP_START + 0xF0
	add	rax,rdi
	mov	ebx,[rax]
	or	ebx,0x1FF
	mov	[rax],ebx

	; Use the flat processor addressing model
	mov	rax,0xFFFFFE00000000E0 ; LOW_MEMORY_MAP_START + 0xE0
	add	rax,rdi
	mov	dword [rax],0xFFFFFFFF

	; Make sure that no external interrupts are masked
	xor	rax,rax
	mov	cr8,rax

	ret

SyscallEntry:
	mov	rsp,[gs:8]
	sti

	mov	ax,0x50
	mov	ds,ax
	mov	es,ax

	; Preserve RCX, R11, R12 and RBX.
	push	rcx
	push	r11
	push	r12
	mov	rax,rsp
	push	rbx
	push	rax

	; Arguments in RDI, RSI, RDX, R8, R9. (RCX contains return address).
	; Return value in RAX.
	mov	rbx,rsp
	and	rsp,~0xF
	call	Syscall
	mov	rsp,rbx

	; Disable maskable interrupts.
	cli

	; Return to long mode. (Address in RCX).
	add	rsp,8
	push	rax
	mov	ax,0x63
	mov	ds,ax
	mov	es,ax
	pop	rax
	pop	rbx
	pop	r12 ; User RSP
	pop	r11
	pop	rcx ; Return address
	db	0x48 
	sysret

ProcessorFakeTimerInterrupt:
	int	0x40
	ret

ProcessorDisableInterrupts:
	mov	rax,14 ; Still allow important IPIs to go through.
	mov	cr8,rax
	sti ; TODO Where is this necessary? Is is a performance issue?
	ret

ProcessorEnableInterrupts:
	; WARNING: Changing this mechanism also requires update in x86_64.cpp, when deciding if we should re-enable interrupts on exception.
	mov	rax,0
	mov	cr8,rax
	sti ; TODO Where is this necessary? Is is a performance issue?
	ret

ProcessorAreInterruptsEnabled:
	pushf	
	pop	rax
	and	rax,0x200
	shr	rax,9

	mov	rdx,cr8
	cmp	rdx,0
	je	.done
	mov	rax,0
	.done:

	; pushf
	; pop	rax
	; and	rax,0x200
	; shr	rax,9
	ret

ProcessorHalt:
	cli
	hlt
	jmp	ProcessorHalt

ProcessorOut8:
	mov	rdx,rdi
	mov	rax,rsi
	out	dx,al
	ret

ProcessorIn8:
	mov	rdx,rdi
	xor	rax,rax
	in	al,dx
	ret

ProcessorOut16:
	mov	rdx,rdi
	mov	rax,rsi
	out	dx,ax
	ret

ProcessorIn16:
	mov	rdx,rdi
	xor	rax,rax
	in	ax,dx
	ret

ProcessorOut32:
	mov	rdx,rdi
	mov	rax,rsi
	out	dx,eax
	ret

ProcessorIn32:
	mov	rdx,rdi
	xor	rax,rax
	in	eax,dx
	ret

ProcessorInvalidatePage:
	invlpg	[rdi]
	ret

ProcessorInvalidateAllPages:
	; Toggle CR4.PGE to invalidate all TLB entries, including global entries.
	mov	rax,cr4
	and	rax,~(1 << 7)
	mov	cr4,rax
	or	rax,1 << 7
	mov	cr4,rax
	ret

ProcessorIdle:
	sti
	hlt
	jmp	ProcessorIdle

GetLocalStorage:
	mov	rax,[gs:0]
	ret

GetCurrentThread:
	mov	rax,[gs:16]
	ret

ProcessorSetLocalStorage:
	mov	[gs:0],rdi
	ret

ProcessorSetThreadStorage:
	push	rdx
	push	rcx
	mov	rcx,0xC0000100 ; set fs base
	mov	rdx,rdi
	mov	rax,rdi
	shr	rdx,32
	wrmsr		       ; to edx:eax (from rdi)
	pop	rcx
	pop	rdx
	ret

InstallInterruptHandler:
	mov	word [rbx + 0],dx
	mov	word [rbx + 2],0x48
	mov	word [rbx + 4],0x8E00 
	shr	rdx,16
	mov	word [rbx + 6],dx
	shr	rdx,16
	mov	qword [rbx + 8],rdx

	ret

%macro INTERRUPT_HANDLER 1
InterruptHandler%1:
	push	dword 0 ; A fake error code
	push	dword %1 ; The interrupt number
	jmp	ASMInterruptHandler
%endmacro

%macro INTERRUPT_HANDLER_EC 1
InterruptHandler%1:
	; The CPU already pushed an error code
	push	dword %1 ; The interrupt number
	jmp	ASMInterruptHandler
%endmacro

INTERRUPT_HANDLER 0
INTERRUPT_HANDLER 1
INTERRUPT_HANDLER 2
INTERRUPT_HANDLER 3
INTERRUPT_HANDLER 4
INTERRUPT_HANDLER 5
INTERRUPT_HANDLER 6
INTERRUPT_HANDLER 7
INTERRUPT_HANDLER_EC 8
INTERRUPT_HANDLER 9
INTERRUPT_HANDLER_EC 10
INTERRUPT_HANDLER_EC 11
INTERRUPT_HANDLER_EC 12
INTERRUPT_HANDLER_EC 13
INTERRUPT_HANDLER_EC 14
INTERRUPT_HANDLER 15
INTERRUPT_HANDLER 16
INTERRUPT_HANDLER_EC 17
INTERRUPT_HANDLER 18
INTERRUPT_HANDLER 19
INTERRUPT_HANDLER 20
INTERRUPT_HANDLER 21
INTERRUPT_HANDLER 22
INTERRUPT_HANDLER 23
INTERRUPT_HANDLER 24
INTERRUPT_HANDLER 25
INTERRUPT_HANDLER 26
INTERRUPT_HANDLER 27
INTERRUPT_HANDLER 28
INTERRUPT_HANDLER 29
INTERRUPT_HANDLER 30
INTERRUPT_HANDLER 31

%assign i 32
%rep 224
INTERRUPT_HANDLER i
%assign i i+1
%endrep

ASMInterruptHandler:
	cld

	push	rax
	push	rbx
	push	rcx
	push	rdx
	push	rsi
	push	rdi
	push	rbp
	push	r8
	push	r9
	push	r10
	push	r11
	push	r12
	push	r13
	push	r14
	push	r15

	mov	rax,cr8
	push	rax

	mov	rax,0x123456789ABCDEF
	push	rax

	mov	rbx,rsp
	and	rsp,~0xF
	fxsave	[rsp - 512]
	mov	rsp,rbx
	sub	rsp,512 + 16
	
	xor	rax,rax
	mov	ax,ds
	push	rax
	mov	ax,0x10
	mov	ds,ax
	mov	es,ax
	mov	rax,cr2
	push	rax

	mov	rdi,rsp
	mov	rbx,rsp
	and	rsp,~0xF
	call	InterruptHandler
	mov	rsp,rbx
	xor	rax,rax

ReturnFromInterruptHandler:
	add	rsp,8
	pop	rbx
	mov	ds,bx
	mov	es,bx

	add	rsp,512 + 16
	mov	rbx,rsp
	and	rbx,~0xF
	fxrstor	[rbx - 512]

	cmp	al,0
	je	.oldThread
	fninit ; New thread - initialise FPU.
	.oldThread:

	pop	rax
	mov	rbx,0x123456789ABCDEF
	cmp	rax,rbx
	jne	$

	cli	
	pop	rax
	mov	cr8,rax

	pop	r15
	pop	r14
	pop	r13
	pop	r12
	pop	r11
	pop	r10
	pop	r9
	pop	r8
	pop	rbp
	pop	rdi
	pop	rsi
	pop	rdx
	pop	rcx
	pop	rbx
	pop	rax

	add	rsp,16
	iretq

ProcessorSetAddressSpace:
	mov	rdi,[rdi]
	mov	rax,cr3
	cmp	rax,rdi
	je	.cont
	mov	cr3,rdi
	.cont:
	ret

ProcessorGetRSP:
	mov	rax,rsp
	ret

ProcessorGetRBP:
	mov	rax,rbp
	ret

ArchSwitchContext:
	cli
	mov	[gs:16],rcx
	mov	[gs:8],rdx
	mov	rsi,[rsi]
	mov	rax,cr3
	cmp	rax,rsi
	je	.cont
	mov	cr3,rsi
	.cont:
	mov	rsp,rdi
	mov	rsi,r8
	call	PostContextSwitch
	jmp	ReturnFromInterruptHandler

ProcessorReadCR3:
	mov	rax,cr3
	ret

ProcessorDebugOutputByte:
%ifdef COM_OUTPUT
	mov	dx,0x3F8 + 5
	.WaitRead:
	in	al,dx
	and	al,0x20
	cmp	al,0
	je	.WaitRead
	mov	dx,0x3F8 + 0
	mov	rax,rdi
	out	dx,al
%endif
	ret

ProcessorReadTimeStamp:
	rdtsc
	shl	rdx,32
	or	rax,rdx
	ret

ProcessorFlushCodeCache:
	wbinvd
	ret

ProcessorReadMXCSR:
	mov	rax,.buffer
	stmxcsr	[rax]
	mov	rax,.buffer
	mov	rax,[rax]
	ret
	.buffer: dq 0

ProcessorInstallTSS:
	push	rbx

	; Set the location of the TSS in the GDT.
	mov	rax,rdi
	mov	rbx,rsi
	mov	[rax + 56 + 2],bx
	shr	rbx,16
	mov	[rax + 56 + 4],bl
	shr	rbx,8
	mov	[rax + 56 + 7],bl
	shr	rbx,8
	mov	[rax + 56 + 8],rbx

	; Flush the GDT.
	mov	rax,gdt_data.gdt2
	mov	rdx,[rax]
	mov	[rax],rdi
	mov	rdi,gdt_data.gdt
	lgdt	[rdi]
	mov	[rax],rdx

	; Flush the TSS.
	mov	ax,0x38
	ltr	ax

	pop	rbx
	ret

MMArchSafeCopy:
	call	GetCurrentThread
	mov	byte [rax + 0],1 ; see definition of Thread
	mov	rcx,rdx
	mov	r8,.error ; where to jump to if we get a page fault
	rep	movsb
	mov	byte [rax + 0],0
	mov	al,1
	ret
	.error: ; we got a page fault in a user address, return false
	mov	byte [rax + 0],0
	mov	al,0
	ret

ProcessorReset:
	in	al,0x64
	test	al,2
	jne	ProcessorReset
	mov	al,0xFE
	out	0x64,al
	jmp	$

_KThreadTerminate:
	sub	rsp,8
	jmp	KThreadTerminate

SynchronizeTimeStampCounter:
	mov	rdx,[timeStampCounterSynchronizationValue]
	mov	rcx,0x8000000000000000
	.loop:
	mov	rbx,rdx
	mov	rax,[timeStampCounterSynchronizationValue]
	xor	rbx,rax
	test	rbx,rcx
	jz	.loop
	sub	rcx,1
	and	rax,rcx
	mov	ecx,0x10
	mov	rdx,rax
	shr	rdx,32
	wrmsr
	ret
	timeStampCounterSynchronizationValue: dq 0

[bits 16]
ProcessorAPStartup: ; This function must be less than 4KB in length (see drivers/acpi.cpp)
	mov	ax,0x1000
	mov	ds,ax
	mov	byte [0xFC0],1 ; Indicate we've started.
	mov	eax,[0xFF0]
	mov	cr3,eax
	lgdt	[0x1000 + gdt_data.gdt - gdt_data]
	mov	eax,cr0
	or	eax,1
	mov	cr0,eax
	jmp	0x8:dword (.pmode - ProcessorAPStartup + 0x10000)
[bits 32]
	.pmode:
	mov	eax,cr4
	or	eax,32
	mov	cr4,eax
	mov	ecx,0xC0000080
	rdmsr
	or	eax,256
	wrmsr
	mov	eax,cr0
	or	eax,0x80000000
	mov	cr0,eax
	jmp	0x48:(.start_64_bit_mode - ProcessorAPStartup + 0x10000)
[bits 64]
	.start_64_bit_mode:
	mov	rax,.start_64_bit_mode2
	jmp	rax
	.start_64_bit_mode2:
	mov	rax,0x50
	mov	ds,rax
	mov	es,rax
	mov	ss,rax
	mov	rax,0x63
	mov	fs,rax
	mov	gs,rax
	lgdt	[0x10FE0]
	mov	rsp,[0x10FD0]
	call	SetupProcessor1
	call	SynchronizeTimeStampCounter
	mov	rdi,[0x10FB0]
	call	SetupProcessor2
	mov	byte [0x10FC0],2 ; Indicate the BSP can start the next processor.
	and	rsp,~0xF
	jmp	ProcessorReady

gdt_data:
	.null_entry:	dq 0
	.code_entry:	dd 0xFFFF	; 0x08
			db 0
			dw 0xCF9A
			db 0
	.data_entry:	dd 0xFFFF	; 0x10
			db 0
			dw 0xCF92
			db 0
	.code_entry_16:	dd 0xFFFF	; 0x18
			db 0
			dw 0x0F9A
			db 0
	.data_entry_16:	dd 0xFFFF	; 0x20
			db 0
			dw 0x0F92
			db 0
	.user_code:	dd 0xFFFF	; 0x2B
			db 0
			dw 0xCFFA
			db 0
	.user_data:	dd 0xFFFF	; 0x33
			db 0
			dw 0xCFF2
			db 0
	.tss:		dd 0x68		; 0x38
			db 0
			dw 0xE9
			db 0
			dq 0
	.code_entry64:	dd 0xFFFF	; 0x48
			db 0
			dw 0xAF9A
			db 0
	.data_entry64:	dd 0xFFFF	; 0x50
			db 0
			dw 0xAF92
			db 0
	.user_code64:	dd 0xFFFF	; 0x5B
			db 0
			dw 0xAFFA
			db 0
	.user_data64:	dd 0xFFFF	; 0x63
			db 0
			dw 0xAFF2
			db 0
	.user_code64c:	dd 0xFFFF	; 0x6B
			db 0
			dw 0xAFFA
			db 0
	.gdt:		dw (gdt_data.gdt - gdt_data - 1)
	.gdt2:		dq 0x11000

%macro CALL_REGISTER_INDIRECT 1
[global __x86_indirect_thunk_%1]
__x86_indirect_thunk_%1:
	jmp	%1
%endmacro

CALL_REGISTER_INDIRECT rax
CALL_REGISTER_INDIRECT rbx
CALL_REGISTER_INDIRECT rcx
CALL_REGISTER_INDIRECT rdx
CALL_REGISTER_INDIRECT rsi
CALL_REGISTER_INDIRECT rdi
CALL_REGISTER_INDIRECT rbp
CALL_REGISTER_INDIRECT r8
CALL_REGISTER_INDIRECT r9
CALL_REGISTER_INDIRECT r10
CALL_REGISTER_INDIRECT r11
CALL_REGISTER_INDIRECT r12
CALL_REGISTER_INDIRECT r13
CALL_REGISTER_INDIRECT r14
CALL_REGISTER_INDIRECT r15
