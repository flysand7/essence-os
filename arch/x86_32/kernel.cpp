#ifndef IMPLEMENTATION

struct MMArchVAS {
};

#define MM_KERNEL_SPACE_START (0xC0000000)
#define MM_KERNEL_SPACE_SIZE  (0xE0000000 - 0xC0000000)
#define MM_MODULES_START      (0xE0000000)
#define MM_MODULES_SIZE	      (0xE8000000 - 0xE0000000)
#define MM_CORE_REGIONS_START (0xE8000000)
#define MM_CORE_REGIONS_COUNT ((0xEC000000 - 0xE8000000) / sizeof(MMRegion))

#define ArchCheckBundleHeader() (header.mapAddress >= 0xC0000000ULL || header.mapAddress < 0x1000 || fileSize > 0x10000000ULL)
#define ArchCheckELFHeader()    (header->virtualAddress >= 0xC0000000ULL || header->virtualAddress < 0x1000 || header->segmentSize > 0x10000000ULL)

#define K_ARCH_STACK_GROWS_DOWN
#define K_ARCH_NAME "x86_32"

#endif

#ifdef IMPLEMENTATION

#define KERNEL_PANIC_IPI (0) // NMIs ignore the interrupt vector.
size_t ProcessorSendIPI(uintptr_t interrupt, bool nmi = false, int processorID = -1); // Returns the number of processors the IPI was *not* sent to.
void ProcessorReset();
uintptr_t ArchFindRootSystemDescriptorPointer();
void ArchStartupApplicationProcessors();
uint32_t bootloaderID;

#include <drivers/acpi.cpp>

size_t ProcessorSendIPI(uintptr_t interrupt, bool nmi, int processorID) {
	(void) interrupt;
	(void) nmi;
	(void) processorID;
	// TODO.
	return 0;
}

void ArchInitialise() {
	// TODO.
}

void ArchNextTimer(size_t ms) {
	// TODO.
}

uint64_t ArchGetTimeMs() {
	// TODO.
	return 0;
}

InterruptContext *ArchInitialiseThread(uintptr_t kernelStack, uintptr_t kernelStackSize, 
		Thread *thread, uintptr_t startAddress, uintptr_t argument1, 
		uintptr_t argument2, bool userland, uintptr_t stack, uintptr_t userStackSize) {
	// TODO.
	return nullptr;
}

void ArchSwitchContext(InterruptContext *context, MMArchVAS *virtualAddressSpace, uintptr_t threadKernelStack, 
		Thread *newThread, MMSpace *oldAddressSpace) {
	// TODO.
}

EsError ArchApplyRelocation(uintptr_t type, uint8_t *buffer, uintptr_t offset, uintptr_t result) {
	// TODO.
	return ES_ERROR_UNSUPPORTED_FEATURE;
}

bool MMArchMapPage(MMSpace *space, uintptr_t physicalAddress, uintptr_t virtualAddress, unsigned flags) {
	// TODO.
	return false;
}

void MMArchUnmapPages(MMSpace *space, uintptr_t virtualAddressStart, uintptr_t pageCount, unsigned flags, size_t unmapMaximum, uintptr_t *resumePosition) {
	// TODO.
}

bool MMArchMakePageWritable(MMSpace *space, uintptr_t virtualAddress) {
	// TODO.
	return false;
}

bool MMArchHandlePageFault(uintptr_t address, uint32_t flags) {
	// TODO.
	return false;
}

void MMArchInvalidatePages(uintptr_t virtualAddressStart, uintptr_t pageCount) {
	// TODO.
}

bool MMArchIsBufferInUserRange(uintptr_t baseAddress, size_t byteCount) {
	// TODO.
	return false;
}

bool MMArchSafeCopy(uintptr_t destinationAddress, uintptr_t sourceAddress, size_t byteCount) {
	// TODO.
	return false;
}

bool MMArchCommitPageTables(MMSpace *space, MMRegion *region) {
	// TODO.
	return false;
}

bool MMArchInitialiseUserSpace(MMSpace *space, MMRegion *firstRegion) {
	// TODO.
	return false;
}

void MMArchInitialise() {
	// TODO.
}

void MMArchFreeVAS(MMSpace *space) {
	// TODO.
}

void MMArchFinalizeVAS(MMSpace *space) {
	// TODO.
}

uintptr_t MMArchEarlyAllocatePage() {
	// TODO.
	return 0;
}

uint64_t MMArchPopulatePageFrameDatabase() {
	// TODO.
	return 0;
}

uintptr_t MMArchGetPhysicalMemoryHighest() {
	// TODO.
	return 0;
}

void ProcessorDisableInterrupts() {
	// TODO.
}

void ProcessorEnableInterrupts() {
	// TODO.
}

bool ProcessorAreInterruptsEnabled() {
	// TODO.
	return false;
}

void ProcessorHalt() {
	// TODO.
}

void ProcessorSendYieldIPI(Thread *thread) {
	// TODO.
}

void ProcessorFakeTimerInterrupt() {
	// TODO.
}

void ProcessorInvalidatePage(uintptr_t virtualAddress) {
	// TODO.
}

void ProcessorInvalidateAllPages() {
	// TODO.
}

void ProcessorFlushCodeCache() {
	// TODO.
}

void ProcessorFlushCache() {
	// TODO.
}

void ProcessorSetLocalStorage(CPULocalStorage *cls) {
	// TODO.
}

void ProcessorSetThreadStorage(uintptr_t tls) {
	// TODO.
}

void ProcessorSetAddressSpace(MMArchVAS *virtualAddressSpace) {
	// TODO.
}

uint64_t ProcessorReadTimeStamp() {
	// TODO.
	return 0;
}

CPULocalStorage *GetLocalStorage() {
	// TODO.
	return nullptr;
}

Thread *GetCurrentThread() {
	// TODO.
	return nullptr;
}

uintptr_t MMArchTranslateAddress(MMSpace *space, uintptr_t virtualAddress, bool writeAccess) {
	// TODO.
	return 0;
}

uint32_t KPCIReadConfig(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, int size) {
	// TODO.
	return 0;
}

void KPCIWriteConfig(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value, int size) {
	// TODO.
}

bool KRegisterIRQ(intptr_t interruptIndex, KIRQHandler handler, void *context, const char *cOwnerName, KPCIDevice *pciDevice) {
	// TODO.
	return false;
}

KMSIInformation KRegisterMSI(KIRQHandler handler, void *context, const char *cOwnerName) {
	// TODO.
	return {};
}

void KUnregisterMSI(uintptr_t tag) {
	// TODO.
}

void ProcessorOut8(uint16_t port, uint8_t value) {
	// TODO.
}

uint8_t ProcessorIn8(uint16_t port) {
	// TODO.
	return 0;
}

void ProcessorOut16(uint16_t port, uint16_t value) {
	// TODO.
}

uint16_t ProcessorIn16(uint16_t port) {
	// TODO.
	return 0;
}

void ProcessorOut32(uint16_t port, uint32_t value) {
	// TODO.
}

uint32_t ProcessorIn32(uint16_t port) {
	// TODO.
	return 0;
}

void ProcessorReset() {
	// TODO.
}

uintptr_t ArchFindRootSystemDescriptorPointer() {
	// TODO.
	return 0;
}

void ArchStartupApplicationProcessors() {
	// TODO.
}

uintptr_t GetBootloaderInformationOffset() {
	// TODO.
	return 0;
}

extern "C" void ProcessorDebugOutputByte(uint8_t byte) {
	// TODO.
}

#include <kernel/terminal.cpp>

#endif
