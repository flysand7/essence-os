#ifndef ARCH_X86_64_HEADER
#define ARCH_X86_64_HEADER

#define LOW_MEMORY_MAP_START (0xFFFFFE0000000000)
#define LOW_MEMORY_LIMIT (0x100000) // The first 1MB is mapped here.

extern "C" uint64_t ProcessorReadCR3();
extern "C" void gdt_data();

extern "C" void SSSE3Framebuffer32To24Copy(volatile uint8_t *destination, volatile uint8_t *source, size_t pixelGroups);

extern bool pagingNXESupport;
extern bool pagingPCIDSupport;
extern bool pagingSMEPSupport;
extern bool pagingTCESupport;

extern "C" void processorGDTR();
extern "C" void SetupProcessor2(struct NewProcessorStorage *);
extern "C" void ProcessorInstallTSS(uint32_t *gdt, uint32_t *tss);

bool HasSSSE3Support();
uintptr_t GetBootloaderInformationOffset();

void ArchDelay1Ms(); // Spin for approximately 1ms. Use only during initialisation. Not thread-safe.

struct NewProcessorStorage {
	struct CPULocalStorage *local;
	uint32_t *gdt;
};

NewProcessorStorage AllocateNewProcessorStorage(struct ACPIProcessor *archCPU);

#endif
