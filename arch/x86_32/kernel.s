[bits 32]

[global ProcessorReset]
[global ProcessorDebugOutputByte]
[global ProcessorIn16]
[global ProcessorIn32]
[global ProcessorIn8]
[global ProcessorOut16]
[global ProcessorOut32]
[global ProcessorOut8]
[global ProcessorHalt]
[global ProcessorDisableInterrupts]
[global ProcessorEnableInterrupts]
[global ProcessorAreInterruptsEnabled]
[global ProcessorSetLocalStorage]
[global ProcessorReadCR3]
[global ProcessorInvalidatePage]
[global ProcessorInvalidateAllPages]
[global ProcessorReadTimeStamp]
[global ProcessorSetThreadStorage]
[global ProcessorFakeTimerInterrupt]
[global ProcessorSetAddressSpace]
[global ProcessorFlushCodeCache]
[global GetLocalStorage]
[global GetCurrentThread]
[global ArchSwitchContext]
[global processorGDTR]
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

%define boot_stack_size 10000
boot_stack: resb boot_stack_size

%define idt_size 2048
idt_data: resb idt_size

%define cpu_local_storage_size (256 * 4 * 4)
cpu_local_storage: resb cpu_local_storage_size
cpu_local_index: resb 4

timeStampCounterSynchronizationValue: resb 8

[section .data]

processorGDTR: 
	times 8 db 0

idt:
	.limit: dw idt_size - 1
	.base:  dd idt_data
	align 4

gdt_data:
	.null_entry:	dq 0
	.code_entry:	dd 0xFFFF	; 0x0008
			db 0
			dw 0xCF9A
			db 0
	.data_entry:	dd 0xFFFF	; 0x0010
			db 0
			dw 0xCF92
			db 0
	.user_code:	dd 0xFFFF	; 0x001B
			db 0
			dw 0xCFFA
			db 0
	.user_data:	dd 0xFFFF	; 0x0023
			db 0
			dw 0xCFF2
			db 0
	.tss:		dd 0x68		; 0x0028
			db 0
			dw 0xE9
			db 0
			dq 0
	.local:		times 256 dq 0	; 0x0038 - 0x0838
	.gdt:		dw (gdt_data.gdt - gdt_data - 1)
	.gdt2:		dd gdt_data

[section .text]

[global _start]
_start:
	; Save the bootloader ID and information offset.
	mov	[bootloaderID],esi
	mov	[bootloaderInformationOffset],edi

	; The MBR bootloader does not know the address of the RSDP. 
	cmp	edi,0
	jne	.standard_acpi
	mov	[0x7FE8],edi
	.standard_acpi:

	; Install the boot stack.
	mov	esp,boot_stack + boot_stack_size

	; Load the installation ID.
	mov	eax,[edi + 0x7FF0]
	mov	[installationID + 0],eax
	mov	eax,[edi + 0x7FF4]
	mov	[installationID + 4],eax
	mov	eax,[edi + 0x7FF8]
	mov	[installationID + 8],eax
	mov	eax,[edi + 0x7FFC]
	mov	[installationID + 12],eax

	; Load the new GDT, saving the location of the bootstrap GDT.
	lgdt	[gdt_data.gdt]
	sgdt	[processorGDTR]

	; Move the identity paging the bootloader used to LOW_MEMORY_MAP_START.
	; Then, map the local APIC to LOCAL_APIC_BASE.
	mov	eax,[0xFFFFF000]
	mov	[0xFFFFFEC0],eax
	xor	eax,eax
	mov	[0xFFFFF000],eax
	mov	eax,0xFEE00103
	mov	[0xFFC00000 + (0xEC3FF << 2)],eax
	mov	eax,cr3
	mov	cr3,eax

	; Install the interrupt handlers
	mov	ebx,idt_data
%macro INSTALL_INTERRUPT_HANDLER 1
	mov	edx,InterruptHandler%1
	call	InstallInterruptHandler
	add	ebx,8
%endmacro
%assign i 0
%rep 256
	INSTALL_INTERRUPT_HANDLER i
%assign i i+1
%endrep

	; Setup the remaining things and call KernelMain.
	call	SetupProcessor1 ; Need to get SSE up before calling into C code.
	call	PCSetupCOM1
	call	PCDisablePIC
	call	PCProcessMemoryMap
	call	KernelMain

	; Fall-through.
ProcessorReady:
	; Set the timer and become this CPU's idle thread.
	push	1
	call	ArchNextTimer

	; Fall-through.
ProcessorIdle:
	sti
	hlt
	jmp	ProcessorIdle

SetupProcessor1:
	; x87 FPU.
	fninit
	fldcw	[.cw]
	jmp	.cwa
	.cw:	dw 0x037A
	.cwa:

	; Enable MMX, SSE and SSE2.
	; TODO Check these are actually present!
	mov	eax,cr0
	mov	ebx,cr4
	and	eax,~4
	or	eax,2
	or	ebx,512 + 1024
	mov	cr0,eax
	mov	cr4,ebx

	; Setup the local storage.
	; This creates a new data segment in the GDT pointing to a unique 16-byte block of cpu_local_storage,
	; and updates FS to use the data segment.
	mov	eax,[cpu_local_index]
	mov	ebx,eax
	shl	ebx,4
	add	ebx,cpu_local_storage
	mov	edx,ebx
	shl	ebx,16
	or	ebx,0x0000FFFF
	mov	ecx,edx
	shr	ecx,16
	and	edx,0xFF000000
	or	edx,0x00CF9200
	or	dl,cl
	mov	dword [gdt_data.local + eax * 8 + 0],ebx
	mov	dword [gdt_data.local + eax * 8 + 4],edx
	lea	eax,[0x0038 + eax * 8]
	mov	fs,ax
	inc	dword [cpu_local_index]

	; Enable global pages.
	mov	eax,cr4
	or	eax,1 << 7
	mov	cr4,eax

	; Enable write protect, so copy-on-write works in the kernel, and MMArchSafeCopy will page fault in read-only regions.
	mov	eax,cr0
	or	eax,1 << 16
	mov	cr0,eax

	; Load the IDTR.
	lidt	[idt]
	sti

	; Enable the APIC.
	; TODO Check it is actually present!
	mov	ecx,0x1B
	rdmsr
	or	eax,0x800
	wrmsr

	; Set the spurious interrupt vector to 0xFF
	mov	eax,0xEC3FF0F0
	mov	ebx,[eax]
	or	ebx,0x1FF
	mov	[eax],ebx

	; Use the flat processor addressing model
	mov	eax,0xEC3FF0E0
	mov	dword [eax],0xFFFFFFFF

	; Make sure that no external interrupts are masked
	xor	eax,eax
	mov	[0xEC3FF080],eax

	; TODO More feature detection and initialisation!

	ret

InstallInterruptHandler:
	mov	word [ebx + 0],dx
	mov	word [ebx + 2],0x0008
	mov	word [ebx + 4],0x8E00 
	shr	edx,16
	mov	word [ebx + 6],dx
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

	test	byte [esp + 12],3
	jnz	.have_esp
	; When ring 0 is interrupted, ESP and SS aren't pushed.
	; We push them ourselves here; we'll fix the order later.
	push	eax
	push	esp
	mov	eax,ss
	xchg	[esp + 4],eax
	push	1
	jmp	.fixed
	.have_esp:
	push	0
	.fixed:

	push	eax
	push	ebx
	push	ecx
	push	edx
	push	esi
	push	edi
	push	ebp

	mov	ebx,esp
	and	esp,~0xF
	fxsave	[esp - 512]
	mov	esp,ebx
	sub	esp,512 + 16
	
	mov	eax,ds
	push	eax
	mov	eax,0x10
	mov	ds,ax
	mov	eax,cr2
	push	eax

	mov	edx,[0xEC3FF080]
	push	edx
	mov	edx,0xF0
	mov	[0xEC3FF080],edx ; Mask all interrupts.
	sti                      ; ...so there's no need to have the interrupt flag clear.

	push	esp
	call	InterruptHandler
	add	esp,4
	xor	eax,eax

	.return:

	cli ; Must be done before restoring CR8.
	pop	edx
	mov	[0xEC3FF080],edx
	add	esp,4
	pop	ebx
	mov	ds,bx

	add	esp,512 + 16
	mov	ebx,esp
	and	ebx,~0xF
	fxrstor	[ebx - 512]

	or	al,al
	jz	.old_thread
	fninit ; New thread - initialise FPU.
	.old_thread:

	pop	ebp
	pop	edi
	pop	esi
	pop	edx
	pop	ecx
	pop	ebx
	pop	eax

	test	byte [esp],1
	jz	.need_esp
	; When returning to ring 0, ESP and SS aren't popped.
	; So we do it manually here.
	add	esp,8
	.need_esp:

	add	esp,12
	iret

ArchSwitchContext:
	cli
	mov	eax,[esp + 16]
	mov	[fs:8],eax
	mov	ebx,[esp + 12]
	mov	[fs:4],ebx
	mov	edi,[esp + 8]
	mov	ecx,[edi]
	mov	edx,cr3
	cmp	edx,ecx
	je	.cont
	mov	cr3,ecx
	.cont:
	mov	eax,[esp + 4]
	mov	ecx,[esp + 20]
	mov	esp,eax ; Put the stack just below the context.
	push	ecx
	push	eax
	call	PostContextSwitch
	add	esp,8
	jmp	ASMInterruptHandler.return

ProcessorDebugOutputByte:
%ifdef COM_OUTPUT
	mov	dx,0x3F8 + 5
	.WaitRead:
	in	al,dx
	and	al,0x20
	cmp	al,0
	je	.WaitRead
	mov	dx,0x3F8 + 0
	mov	eax,[esp + 4]
	out	dx,al
%endif
	ret

ProcessorOut8:
	mov	edx,[esp + 4]
	mov	eax,[esp + 8]
	out	dx,al
	ret

ProcessorIn8:
	mov	edx,[esp + 4]
	xor	eax,eax
	in	al,dx
	ret

ProcessorOut16:
	mov	edx,[esp + 4]
	mov	eax,[esp + 8]
	out	dx,ax
	ret

ProcessorIn16:
	mov	edx,[esp + 4]
	xor	eax,eax
	in	ax,dx
	ret

ProcessorOut32:
	mov	edx,[esp + 4]
	mov	eax,[esp + 8]
	out	dx,eax
	ret

ProcessorIn32:
	mov	edx,[esp + 4]
	xor	eax,eax
	in	eax,dx
	ret

ProcessorReset:
	in	al,0x64
	test	al,2
	jne	ProcessorReset
	mov	al,0xFE
	out	0x64,al

	; Fall-through.
ProcessorHalt:
	cli
	hlt
	jmp	ProcessorHalt

ProcessorDisableInterrupts:
	mov	eax,0xE0 ; Still allow important IPIs to go through.
	mov	[0xEC3FF080],eax
	ret

ProcessorEnableInterrupts:
	xor	eax,eax
	mov	[0xEC3FF080],eax
	ret

ProcessorAreInterruptsEnabled:
	xor	al,al
	mov	edx,[0xEC3FF080]
	or	edx,edx
	jnz	.done
	mov	al,1
	.done:
	ret

GetLocalStorage:
	mov	eax,[fs:0]
	ret

GetCurrentThread:
	mov	eax,[fs:8]
	ret

ProcessorSetLocalStorage:
	mov	eax,[esp + 4]
	mov	[fs:0],eax
	ret

ProcessorReadCR3:
	mov	eax,cr3
	ret

ProcessorInvalidatePage:
	mov	eax,[esp + 4]
	invlpg	[eax]
	ret

ProcessorInvalidateAllPages:
	; Toggle CR4.PGE to invalidate all TLB entries, including global entries.
	mov	eax,cr4
	and	eax,~(1 << 7)
	mov	cr4,eax
	or	eax,1 << 7
	mov	cr4,eax
	ret

ProcessorReadTimeStamp:
	rdtsc
	ret

ProcessorSetThreadStorage:
	; TODO.
	ret

ProcessorFakeTimerInterrupt:
	int	0x40
	ret

ProcessorSetAddressSpace:
	mov	eax,[esp + 4]
	mov	edx,[eax]
	mov	eax,cr3
	cmp	eax,edx
	je	.cont
	mov	cr3,edx
	.cont:
	ret

ProcessorFlushCodeCache:
	wbinvd
	ret
