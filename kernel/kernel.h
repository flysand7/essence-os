#ifndef IMPLEMENTATION

#define K_IN_CORE_KERNEL
#define K_PRIVATE
#include "module.h"

// ---------------------------------------------------------------------------------------------------------------
// Constants.
// ---------------------------------------------------------------------------------------------------------------

// TODO Determine the best values for these constants.

// Interval between write behinds. (Assuming no low memory conditions are in effect.)
#define CC_WAIT_FOR_WRITE_BEHIND                  (1000)                              

// Divisor of the modified list size for each write behind batch.
// That is, every CC_WAIT_FOR_WRITE_BEHIND ms, 1/CC_WRITE_BACK_DIVISORth of the modified list is written back.
#define CC_WRITE_BACK_DIVISOR                     (8)
                                                                                      
// Describes the virtual memory covering a section of a file.  
#define CC_ACTIVE_SECTION_SIZE                    ((EsFileOffset) 262144)             

// Maximum number of active sections on the modified list. If exceeded, writers will wait for it to drop before retrying.
// TODO This should based off the amount of physical memory.
#define CC_MAX_MODIFIED                           (67108864 / CC_ACTIVE_SECTION_SIZE) 

// The size at which the modified list is determined to be getting worryingly full;
// passing this threshold causes the write back thread to immediately start working.
#define CC_MODIFIED_GETTING_FULL                  (CC_MAX_MODIFIED * 2 / 3)
										      
// The size of the kernel's address space used for mapping active sections.
#if defined(ARCH_32)                                                                  
#define CC_SECTION_BYTES                          (ClampIntptr(0, 64L * 1024 * 1024, pmm.commitFixedLimit * K_PAGE_SIZE / 4)) 
#elif defined(ARCH_64)
#define CC_SECTION_BYTES                          (ClampIntptr(0, 1024L * 1024 * 1024, pmm.commitFixedLimit * K_PAGE_SIZE / 4)) 
#endif

// When we reach a critical number of pages, FIXED allocations start failing,
// and page faults are blocked, unless you are on a page generating thread (the modified page writer or the balancer).
#define MM_CRITICAL_AVAILABLE_PAGES_THRESHOLD     (1048576 / K_PAGE_SIZE)             

// The number of pages at which balancing starts.
#define MM_LOW_AVAILABLE_PAGES_THRESHOLD          (16777216 / K_PAGE_SIZE)            

// The number of pages past MM_LOW_AVAILABLE_PAGES_THRESHOLD to aim for when balancing.
#define MM_PAGES_TO_FIND_BALANCE                  (4194304 / K_PAGE_SIZE)             

// The number of pages in the zero list before signaling the page zeroing thread.
#define MM_ZERO_PAGE_THRESHOLD                    (16)                                

// The amount of commit reserved specifically for page generating threads.
#define MM_CRITICAL_REMAINING_COMMIT_THRESHOLD    (1048576 / K_PAGE_SIZE)             

// The number of objects that are trimmed from a MMObjectCache at a time.
#define MM_OBJECT_CACHE_TRIM_GROUP_COUNT          (1024)

// The current target maximum size for the object caches. (This uses the approximate sizes of objects.)
// We want to keep a reasonable amount of commit available at all times,
// since when the kernel is allocating memory it might not be able to wait for the caches to be trimmed without deadlock.
// So, try to keep the commit quota used by the object caches at most half the available space.
#define MM_NON_CACHE_MEMORY_PAGES()               (pmm.commitFixed + pmm.commitPageable - pmm.approximateTotalObjectCacheBytes / K_PAGE_SIZE)
#define MM_OBJECT_CACHE_PAGES_MAXIMUM()           ((pmm.commitLimit - MM_NON_CACHE_MEMORY_PAGES()) / 2)

#define PHYSICAL_MEMORY_MANIPULATION_REGION_PAGES (16)
#define POOL_CACHE_COUNT                          (16)

// ---------------------------------------------------------------------------------------------------------------
// Core definitions.
// ---------------------------------------------------------------------------------------------------------------

#ifdef ARCH_X86_64
#include "x86_64.cpp"
#endif

struct AsyncTask {
	KAsyncTaskCallback callback;
	void *argument;
	struct MMSpace *addressSpace;
};

struct CPULocalStorage {
	struct Thread *currentThread, 
		      *idleThread, 
		      *asyncTaskThread;

	struct InterruptContext *panicContext;

	bool irqSwitchThread, schedulerReady, inIRQ;

	unsigned processorID;
	size_t spinlockCount;

	struct ArchCPU *archCPU;

	// TODO Have separate interrupt task threads and system worker threads (with no task limit).
#define MAX_ASYNC_TASKS (256)
	volatile AsyncTask asyncTasks[MAX_ASYNC_TASKS];
	volatile uint8_t asyncTasksRead, asyncTasksWrite;
};

struct PhysicalMemoryRegion {
	uint64_t baseAddress;
	uint64_t pageCount;
};

void KernelInitialise();
void KernelShutdown(uintptr_t action);

uint64_t timeStampTicksPerMs;
EsUniqueIdentifier installationID; // The identifier of this OS installation, given to us by the bootloader.

// ---------------------------------------------------------------------------------------------------------------
// Architecture specific layer definitions.
// ---------------------------------------------------------------------------------------------------------------

extern "C" {
	void ArchInitialise();
	void ArchShutdown(uintptr_t action);
	void ArchNextTimer(size_t ms); // Schedule the next TIMER_INTERRUPT.
	uint64_t ArchGetTimeMs(); // Called by the scheduler on the boot processor every context switch.
	InterruptContext *ArchInitialiseThread(uintptr_t kernelStack, uintptr_t kernelStackSize, struct Thread *thread, 
			uintptr_t startAddress, uintptr_t argument1, uintptr_t argument2,
			bool userland, uintptr_t stack, uintptr_t userStackSize);
	void ArchSwitchContext(struct InterruptContext *context, struct MMArchVAS *virtualAddressSpace, uintptr_t threadKernelStack, 
			struct Thread *newThread, struct MMSpace *oldAddressSpace);
	EsError ArchApplyRelocation(uintptr_t type, uint8_t *buffer, uintptr_t offset, uintptr_t result);

	bool MMArchMapPage(MMSpace *space, uintptr_t physicalAddress, uintptr_t virtualAddress, unsigned flags); // Returns false if the page was already mapped.
	void MMArchUnmapPages(MMSpace *space, uintptr_t virtualAddressStart, uintptr_t pageCount, unsigned flags, size_t unmapMaximum = 0, uintptr_t *resumePosition = nullptr);
	void MMArchRemap(MMSpace *space, const void *virtualAddress, uintptr_t newPhysicalAddress); // Must be done with interrupts disabled; does not invalidate on other processors.
	bool MMArchMakePageWritable(MMSpace *space, uintptr_t virtualAddress);
	bool MMArchHandlePageFault(uintptr_t address, uint32_t flags);
	void MMArchInvalidatePages(uintptr_t virtualAddressStart, uintptr_t pageCount);
	bool MMArchIsBufferInUserRange(uintptr_t baseAddress, size_t byteCount);
	bool MMArchSafeCopy(uintptr_t destinationAddress, uintptr_t sourceAddress, size_t byteCount); // Returns false if a page fault occured during the copy.
	bool MMArchCommitPageTables(MMSpace *space, struct MMRegion *region);
	bool MMArchInitialiseUserSpace(MMSpace *space, struct MMRegion *firstRegion);
	void MMArchInitialise();
	void MMArchFreeVAS(MMSpace *space);
	void MMArchFinalizeVAS(MMSpace *space);
	uintptr_t MMArchEarlyAllocatePage();
	uint64_t MMArchPopulatePageFrameDatabase();
	uintptr_t MMArchGetPhysicalMemoryHighest();

	void ProcessorDisableInterrupts();
	void ProcessorEnableInterrupts();
	bool ProcessorAreInterruptsEnabled();
	void ProcessorHalt();
	void ProcessorSendYieldIPI(Thread *thread);
	void ProcessorFakeTimerInterrupt();
	void ProcessorInvalidatePage(uintptr_t virtualAddress);
	void ProcessorInvalidateAllPages();
	void ProcessorFlushCodeCache();
	void ProcessorFlushCache();
	void ProcessorSetLocalStorage(struct CPULocalStorage *cls);
	void ProcessorSetThreadStorage(uintptr_t tls);
	void ProcessorSetAddressSpace(struct MMArchVAS *virtualAddressSpace); // Need to call MMSpaceOpenReference/MMSpaceCloseReference if using this.
	uint64_t ProcessorReadTimeStamp();

	struct CPULocalStorage *GetLocalStorage();
	struct Thread *GetCurrentThread();

	// From module.h: 
	// uintptr_t MMArchTranslateAddress(MMSpace *space, uintptr_t virtualAddress, bool writeAccess); 
	// uint32_t KPCIReadConfig(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, int size);
	// void KPCIWriteConfig(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value, int size);
	// bool KRegisterIRQ(intptr_t interruptIndex, KIRQHandler handler, void *context, const char *cOwnerName, struct KPCIDevice *pciDevice);
	// KMSIInformation KRegisterMSI(KIRQHandler handler, void *context, const char *cOwnerName);
	// void KUnregisterMSI(uintptr_t tag);
	// size_t KGetCPUCount();
	// struct CPULocalStorage *KGetCPULocal(uintptr_t index);

	// The architecture layer must also define:
	// - MM_CORE_REGIONS_START and MM_CORE_REGIONS_COUNT.
	// - MM_KERNEL_SPACE_START and MM_KERNEL_SPACE_SIZE.
	// - MM_MODULES_START and MM_MODULES_SIZE.
	// - ArchCheckBundleHeader, ArchCheckELFHeader and ArchIsAddressInKernelSpace.
	// - K_ARCH_STACK_GROWS_DOWN or K_ARCH_STACK_GROWS_UP.
	// - K_ARCH_NAME.
}

#endif

// ---------------------------------------------------------------------------------------------------------------
// Kernel components.
// ---------------------------------------------------------------------------------------------------------------

#include <shared/avl_tree.cpp>
#include <shared/bitset.cpp>
#include <shared/range_set.cpp>

#include "memory.cpp"
#include "objects.cpp"
#include "syscall.cpp"
#include "scheduler.cpp"
#include "synchronisation.cpp"
#include "cache.cpp"
#include "elf.cpp"
#include "graphics.cpp"
#include "files.cpp"
#include "windows.cpp"
#include "networking.cpp"
#include "drivers.cpp"

#ifdef ENABLE_POSIX_SUBSYSTEM
#include "posix.cpp"
#endif

#ifndef IMPLEMENTATION
#define ARRAY_IMPLEMENTATION_ONLY
#include <shared/array.cpp>
#include <shared/heap.cpp>
#include <shared/arena.cpp>
#include <shared/partitions.cpp>
#include <shared/ini.h>
#endif

// ---------------------------------------------------------------------------------------------------------------
// Miscellaneous.
// ---------------------------------------------------------------------------------------------------------------

#ifdef IMPLEMENTATION

uint64_t KGetTimeInMs() {
	return scheduler.timeMs;
}

bool KBootedFromEFI() {
	extern uint32_t bootloaderID;
	return bootloaderID == 2;
}

bool KInIRQ() {
	return GetLocalStorage()->inIRQ;
}

void KSwitchThreadAfterIRQ() {
	GetLocalStorage()->irqSwitchThread = true; 
}

EsUniqueIdentifier KGetBootIdentifier() {
	return installationID;
}

void EsAssertionFailure(const char *file, int line) {
	KernelPanic("%z:%d - EsAssertionFailure called.\n", file, line);
}

#endif

// ---------------------------------------------------------------------------------------------------------------
// Architecture specific layer implementation.
// ---------------------------------------------------------------------------------------------------------------

#if defined(ARCH_X86_64) && defined(IMPLEMENTATION)
#include "x86_64.h"
#include <drivers/acpi.cpp>
#include "x86_64.cpp"
#include "terminal.cpp"
#endif
