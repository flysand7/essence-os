#ifndef ARCH_X86_64_HEADER
#define ARCH_X86_64_HEADER

#ifndef ES_ARCH_X86_64
#error Included x86_64.h but not targeting x86_64.
#endif

#include "x86_pc.h"

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
void ArchDelay1Ms(); // Spin for approximately 1ms. Use only during initialisation. Not thread-safe.
uint64_t ArchGetTimeFromPITMs();
void *ACPIGetRSDP();
size_t ProcessorSendIPI(uintptr_t interrupt, bool nmi = false, int processorID = -1); // Returns the number of processors the IPI was *not* sent to.
void ArchSetPCIIRQLine(uint8_t slot, uint8_t pin, uint8_t line);
uintptr_t ArchFindRootSystemDescriptorPointer();
void ArchStartupApplicationProcessors();

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
