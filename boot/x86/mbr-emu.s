; This file is part of the Essence operating system.
; It is released under the terms of the MIT license -- see LICENSE.md.
; Written by: nakst.

[bits 16]
[org 0x600]

start:
	; Setup segment registers and the stack
	cli
	mov	ax,0
	mov	ds,ax
	mov	es,ax
	mov	fs,ax
	mov	gs,ax
	mov	ss,ax
	mov	sp,0x7C00
	sti

	; Clear the screen
	mov	ax,0
	int	0x10
	mov	ax,3
	int	0x10

	; Relocate to 0x600
	cld
	mov	si,0x7C00
	mov	di,0x600
	mov	cx,0x200
	rep	movsb
	jmp	0x0:find_partition

find_partition:
	; Save the drive number
	mov	byte [drive_number],dl

	; Get drive parameters.
	mov	ah,0x08
	xor	di,di
	int	0x13
	mov	si,error_cannot_read_disk
	jc	error
	and	cx,63
	mov	[max_sectors],cx
	inc	dh
	shr	dx,8
	mov	[max_heads],dx
	mov	si,error_bad_geometry
	or	cx,cx
	jz	error
	or	dx,dx
	jz	error

	; Find the bootable flag (0x80)
	mov	bx,partition_entry_1
	cmp	byte [bx],0x80
	je	found_partition
	mov	bx,partition_entry_2
	cmp	byte [bx],0x80
	je	found_partition
	mov	bx,partition_entry_3
	cmp	byte [bx],0x80
	je	found_partition
	mov	bx,partition_entry_4
	cmp	byte [bx],0x80
	je	found_partition

	; No bootable partition
	mov	si,error_no_bootable_partition
	jmp	error

found_partition:
	; Load the first sector of the partition at 0x7C00
	push	bx
	mov	di,[bx + 8]
	mov	bx,0x7C00
	call	load_sector

	; Jump to the partition's boot sector
	mov	dl,[drive_number]
	pop	si
	mov	dh,0x01
	jmp	0x0:0x7C00

error:
	; Print an error message
	lodsb
	or	al,al
	jz	.break
	mov	ah,0xE
	int	0x10
	jmp	error

	; Break indefinitely
	.break:
	cli
	hlt

; di - LBA.
; es:bx - buffer
load_sector:
	; Calculate cylinder and head.
	mov	ax,di
	xor	dx,dx
	div	word [max_sectors]
	xor	dx,dx
	div	word [max_heads]
	push	dx ; remainder - head
	mov	ch,al ; quotient - cylinder
	shl	ah,6
	mov	cl,ah

	; Calculate sector.
	mov	ax,di
	xor	dx,dx
	div	word [max_sectors]
	inc	dx
	or	cl,dl

	; Load the sector.
	pop	dx
	mov	dh,dl
	mov	dl,[drive_number]
	mov	ax,0x0201
	int	0x13
	mov	si,error_cannot_read_disk
	jc	error

	ret

error_cannot_read_disk: db "Error: The disk could not be read. (MBR)",0
error_no_bootable_partition: db "Error: No bootable partition could be found on the disk.",0
error_bad_geometry: db 'Error: The BIOS reported invalid disk geometry.',0

drive_number: db 0
max_sectors:  dw 0
max_heads:    dw 0

times (0x1B4 - ($-$$)) nop

disk_identifier: times 10 db 0
partition_entry_1: times 16 db 0
partition_entry_2: times 16 db 0
partition_entry_3: times 16 db 0
partition_entry_4: times 16 db 0

dw 0xAA55
