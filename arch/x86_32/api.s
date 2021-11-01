[section .text]

[global _APISyscall]
_APISyscall:
	; TODO
	ret

[global _EsCRTsetjmp]
_EsCRTsetjmp:
	; TODO
	ret

[global _EsCRTlongjmp]
_EsCRTlongjmp:
	; TODO
	ret

[global EsCRTsqrt]
EsCRTsqrt:
	sqrtsd	xmm0,xmm0
	ret

[global EsCRTsqrtf]
EsCRTsqrtf:
	sqrtss	xmm0,xmm0
	ret

[global ProcessorReadTimeStamp]
ProcessorReadTimeStamp:
	rdtsc
	ret

[global ProcessorCheckStackAlignment]
ProcessorCheckStackAlignment:
	ret

[global ProcessorTLSRead]
ProcessorTLSRead:
	; TODO
	ret

[global ProcessorTLSWrite]
ProcessorTLSWrite:
	; TODO
	ret
