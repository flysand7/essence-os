; This file is part of the Essence operating system.
; It is released under the terms of the MIT license -- see LICENSE.md.
; Written by: nakst.

%macro FilesystemInitialise 0
%define superblock 0x8000
%define kernel_file_entry 0x8800
%endmacro

%macro FilesystemGetKernelSize 0
load_kernel:
	; Load the superblock.
	mov	eax,16
	mov	edi,superblock
	mov	cx,1
	call	load_sectors
	mov	ax,superblock / 16
	mov	fs,ax

	; Check the signature.
	mov	eax,[fs:0]
	cmp	eax,0x73734521
	mov	si,ErrorBadFilesystem
	jne	error

	; Check the read version.
	mov	ax,[fs:48]
	cmp	ax,10
	mov	si,ErrorBadFilesystem
	jg	error

	; Save the OS installation identifier.
	mov	eax,[fs:152]
	mov	[os_installation_identifier + 0],eax
	mov	eax,[fs:156]
	mov	[os_installation_identifier + 4],eax
	mov	eax,[fs:160]
	mov	[os_installation_identifier + 8],eax
	mov	eax,[fs:164]
	mov	[os_installation_identifier + 12],eax

	; Load the kernel's file entry.
	mov	eax,[fs:184]		; Get the block containing the kernel.
	mul	dword [fs:64] 		; Multiply by block size.
	shr	eax,9			; Divide by sector size.
	mov	cx,2 			; 2 sectors - 1024 bytes.
	mov	edi,[fs:192]		; The offset into the block.
	shr	edi,9			; Divide by sector size.
	add	eax,edi			; Add to the first sector.
	mov	edi,kernel_file_entry	
	call	load_sectors

	; Find the data stream.
FindDataStreamLoop:
	mov	bx,[KernelDataStreamPosition]
	mov	eax,[fs:bx]
	cmp	ax,1
	je	SaveKernelSize
	shr	eax,16
	add	[KernelDataStreamPosition],ax
	cmp	bx,0xC00
	mov	si,ErrorUnexpectedFileProblem
	jge	error
	jmp	FindDataStreamLoop

	; Save the size of the kernel.
SaveKernelSize:
	mov	eax,[fs:0x838]
	mov	[kernel_size],eax
	mov	si,error_kernel_too_large
	cmp	eax,0x300000
	jg	error
%endmacro

%macro FilesystemLoadKernelIntoBuffer 0
	; Check that the kernel is stored as DATA_INDIRECT file.
CheckForDataIndirect:
	mov	ax,superblock / 16
	mov	fs,ax
	mov	bx,[KernelDataStreamPosition]
	mov	ax,[fs:bx+4]
	mov	si,ErrorUnexpectedFileProblem
	cmp	al,2
	jne	error
	
	; Load each extent from the file.
LoadEachExtent:
	mov	edi,[kernel_buffer]
	mov	si,[fs:bx+6]
	add	bx,32

	; TODO More than 1 extent. (We don't do the offset correctly).
	push	si
	cmp	si,1
	mov	si,ErrorUnexpectedFileProblem
	jne	error
	pop	si

	; Load the blocks.
	.ExtentLoop:

	; TODO Other extents than offset = 1, count = 2.
	mov	al,[fs:bx]
	cmp	al,0x08
	push	si
	mov	si,ErrorUnexpectedFileProblem
	jne	error
	pop	si

	mov	eax,[fs:64]	
	shr	eax,9		; EAX = Sectors per block.
	xor	ecx,ecx
	mov	cx,[fs:bx+2]
	xchg	ch,cl
	mul	cx
	mov	cx,ax		; CX = Count.

	xor	eax,eax
	mov	al,[fs:bx+1]
	mul	dword [fs:64]
	shr	eax,9		; EAX = Block.

	xchg	bx,bx

	call	load_sectors
	add	bx,4

	; Go to the next extent.
	sub	si,1
	cmp	si,0
	jne	.ExtentLoop
%endmacro

%macro FilesystemSpecificCode 0
KernelDataStreamPosition: dw 0x860
ErrorBadFilesystem: db "Invalid boot EsFS volume.",0
ErrorUnexpectedFileProblem: db "The kernel file could not be loaded.",0
%endmacro
