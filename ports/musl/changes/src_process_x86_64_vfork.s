.global vfork
.type vfork,@function
vfork:
#nakst - redirect to OSMakeLinuxSystemCall
	mov $58,%edi
	mov $OSMakeLinuxSystemCall,%rax
	sub $8,%rsp # keep stack aligned
	call *%rax                 
	add $8,%rsp
	mov %rax,%rdi
	jmp __syscall_ret
