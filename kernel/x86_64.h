#ifndef ARCH_X86_64_HEADER
#define ARCH_X86_64_HEADER

#define LOW_MEMORY_MAP_START (0xFFFFFE0000000000)
#define LOW_MEMORY_LIMIT (0x100000) // The first 1MB is mapped here.

// --------------------------------- Standardised IO ports.

#define IO_PIC_1_COMMAND		(0x0020)
#define IO_PIC_1_DATA			(0x0021)
#define IO_PIT_DATA			(0x0040)
#define IO_PIT_COMMAND			(0x0043)
#define IO_PS2_DATA			(0x0060)
#define IO_PC_SPEAKER			(0x0061)
#define IO_PS2_STATUS			(0x0064)
#define IO_PS2_COMMAND			(0x0064)
#define IO_RTC_INDEX 			(0x0070)
#define IO_RTC_DATA 			(0x0071)
#define IO_PIC_2_COMMAND		(0x00A0)
#define IO_PIC_2_DATA			(0x00A1)
#define IO_BGA_INDEX			(0x01CE)
#define IO_BGA_DATA			(0x01CF)
#define IO_ATA_1			(0x0170) // To 0x0177.
#define IO_ATA_2			(0x01F0) // To 0x01F7.
#define IO_COM_4			(0x02E8) // To 0x02EF.
#define IO_COM_2			(0x02F8) // To 0x02FF.
#define IO_ATA_3			(0x0376)
#define IO_VGA_AC_INDEX 		(0x03C0)
#define IO_VGA_AC_WRITE 		(0x03C0)
#define IO_VGA_AC_READ  		(0x03C1)
#define IO_VGA_MISC_WRITE 		(0x03C2)
#define IO_VGA_MISC_READ  		(0x03CC)
#define IO_VGA_SEQ_INDEX 		(0x03C4)
#define IO_VGA_SEQ_DATA  		(0x03C5)
#define IO_VGA_DAC_READ_INDEX  		(0x03C7)
#define IO_VGA_DAC_WRITE_INDEX 		(0x03C8)
#define IO_VGA_DAC_DATA        		(0x03C9)
#define IO_VGA_GC_INDEX 		(0x03CE)
#define IO_VGA_GC_DATA  		(0x03CF)
#define IO_VGA_CRTC_INDEX 		(0x03D4)
#define IO_VGA_CRTC_DATA  		(0x03D5)
#define IO_VGA_INSTAT_READ 		(0x03DA)
#define IO_COM_3			(0x03E8) // To 0x03EF.
#define IO_ATA_4			(0x03F6)
#define IO_COM_1			(0x03F8) // To 0x03FF.
#define IO_PCI_CONFIG 			(0x0CF8)
#define IO_PCI_DATA   			(0x0CFC)

// --------------------------------- Interrupt vectors.

// Interrupt vectors:
// 	0x00 - 0x1F: CPU exceptions
// 	0x20 - 0x2F: PIC (disabled, spurious)
// 	0x30 - 0x4F: Timers and low-priority IPIs.
// 	0x50 - 0x6F: APIC (standard)
// 	0x70 - 0xAF: MSI
// 	0xF0 - 0xFE: High-priority IPIs
// 	0xFF:        APIC (spurious interrupt)

#define TIMER_INTERRUPT (0x40)
#define YIELD_IPI (0x41)
#define IRQ_BASE (0x50)
#define CALL_FUNCTION_ON_ALL_PROCESSORS_IPI (0xF0)
#define KERNEL_PANIC_IPI (0) // NMIs ignore the interrupt vector.

#define INTERRUPT_VECTOR_MSI_START (0x70)
#define INTERRUPT_VECTOR_MSI_COUNT (0x40)

// --------------------------------- Forward declarations.

extern "C" void gdt_data();
extern "C" void processorGDTR();

extern "C" uint64_t ProcessorReadCR3();
extern "C" uintptr_t ProcessorGetRSP();
extern "C" uintptr_t ProcessorGetRBP();
extern "C" uint64_t ProcessorReadMXCSR();
extern "C" void ProcessorInstallTSS(uint32_t *gdt, uint32_t *tss);
extern "C" void ProcessorAPStartup();
extern "C" void ProcessorReset();

extern "C" void SSSE3Framebuffer32To24Copy(volatile uint8_t *destination, volatile uint8_t *source, size_t pixelGroups);
extern "C" uintptr_t _KThreadTerminate;

extern bool pagingNXESupport;
extern bool pagingPCIDSupport;
extern bool pagingSMEPSupport;
extern bool pagingTCESupport;

extern volatile uint64_t timeStampCounterSynchronizationValue;

extern "C" bool simdSSE3Support;
extern "C" bool simdSSSE3Support;

extern uintptr_t bootloaderInformationOffset;

struct NewProcessorStorage {
	struct CPULocalStorage *local;
	uint32_t *gdt;
};

NewProcessorStorage AllocateNewProcessorStorage(struct ArchCPU *archCPU);
extern "C" void SetupProcessor2(struct NewProcessorStorage *);
uintptr_t GetBootloaderInformationOffset();
void ArchDelay1Ms(); // Spin for approximately 1ms. Use only during initialisation. Not thread-safe.
uint64_t ArchGetTimeFromPITMs();
void *ACPIGetRSDP();
uint8_t ACPIGetCenturyRegisterIndex();
size_t ProcessorSendIPI(uintptr_t interrupt, bool nmi = false, int processorID = -1); // Returns the number of processors the IPI was *not* sent to.
void ArchSetPCIIRQLine(uint8_t slot, uint8_t pin, uint8_t line);

struct InterruptContext {
	uint64_t cr2, ds;
	uint8_t  fxsave[512 + 16];
	uint64_t _check, cr8;
	uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
	uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
	uint64_t interruptNumber, errorCode;
	uint64_t rip, cs, flags, rsp, ss;
};

#endif
