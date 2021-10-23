#ifndef IMPLEMENTATION

#define K_IN_CORE_KERNEL
#define K_PRIVATE
#include "module.h"

//////////////////////////////////

// TODO Determine the best values for these constants.

// Wait 1 second before running the write behind thread. (Ignored if MM_AVAILABLE_PAGES() is below the low threshold.)
#define CC_WAIT_FOR_WRITE_BEHIND                  (1000)                              
                                                                                      
// Describes the virtual memory covering a section of a file.  
#define CC_ACTIVE_SECTION_SIZE                    ((EsFileOffset) 262144)             

// Maximum number of active sections on the modified list. If exceeded, writers will wait for it to drop before retrying.
// TODO This should based off the amount of physical memory.
#define CC_MAX_MODIFIED                           (67108864 / CC_ACTIVE_SECTION_SIZE) 
										      
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

//////////////////////////////////

#include <shared/ini.h>

#define EsAssertionFailure(file, line) KernelPanic("%z:%d - EsAssertionFailure called.\n", file, line)

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

	ArchCPU *archCPU;

	// TODO Have separate interrupt task threads and system worker threads (with no task limit).
#define MAX_ASYNC_TASKS (256)
	volatile AsyncTask asyncTasks[MAX_ASYNC_TASKS];
	volatile uint8_t asyncTasksRead, asyncTasksWrite;
};

//////////////////////////////////

void KernelInitialise();
void KernelShutdown(uintptr_t action);

void ArchInitialise();
void ArchShutdown(uintptr_t action);

extern "C" void ArchResetCPU();
extern "C" void ArchSpeakerBeep();
extern "C" void ArchNextTimer(size_t ms); // Schedule the next TIMER_INTERRUPT.
extern "C" uint64_t ArchGetTimeMs(); // Called by the scheduler on the boot processor every context switch.
InterruptContext *ArchInitialiseThread(uintptr_t kernelStack, uintptr_t kernelStackSize, struct Thread *thread, 
		uintptr_t startAddress, uintptr_t argument1, uintptr_t argument2,
		 bool userland, uintptr_t stack, uintptr_t userStackSize);

void StartDebugOutput();

uint64_t timeStampTicksPerMs;

EsUniqueIdentifier installationID; // The identifier of this OS installation, given to us by the bootloader.
struct Process *desktopProcess;

KSpinlock ipiLock;

//////////////////////////////////

#endif

#include <shared/avl_tree.cpp>
#include <shared/bitset.cpp>
#include <shared/range_set.cpp>

#include "memory.cpp"

#ifndef IMPLEMENTATION
#include <shared/heap.cpp>
#include <shared/arena.cpp>
#else
#define ARRAY_IMPLEMENTATION_ONLY
#include <shared/array.cpp>
#include <shared/partitions.cpp>
#endif

#include "objects.cpp"
#include "syscall.cpp"
#include "scheduler.cpp"
#include "synchronisation.cpp"
#include "drivers.cpp"
#include "elf.cpp"
#include "graphics.cpp"
#include "cache.cpp"
#include "files.cpp"
#include "windows.cpp"
#include "networking.cpp"

#ifdef ENABLE_POSIX_SUBSYSTEM
#include "posix.cpp"
#endif

#include "terminal.cpp"

#ifdef IMPLEMENTATION

//////////////////////////////////

extern "C" uint64_t KGetTimeInMs() {
	if (!timeStampTicksPerMs) return 0;
	return scheduler.timeMs;
}

uint64_t KGetTimeStampTicksPerMs() {
	return timeStampTicksPerMs;
}

uint64_t KGetTimeStampTicksPerUs() {
	return timeStampTicksPerMs / 1000;
}

bool KInIRQ() {
	return GetLocalStorage()->inIRQ;
}

bool KBootedFromEFI() {
	extern uint32_t bootloaderID;
	return bootloaderID == 2;
}

void KSwitchThreadAfterIRQ() {
	GetLocalStorage()->irqSwitchThread = true; 
}

EsUniqueIdentifier KGetBootIdentifier() {
	return installationID;
}

//////////////////////////////////

#ifdef ARCH_X86_64
#include "x86_64.h"
#include <drivers/acpi.cpp>
#include "x86_64.cpp"
#endif

#endif
