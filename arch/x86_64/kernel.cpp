// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

#ifndef IMPLEMENTATION

struct MMArchVAS {
	// NOTE Must be first in the structure. See ProcessorSetAddressSpace and ArchSwitchContext.
	uintptr_t cr3;

	// Each process has a 47-bit address space.
	// That's 2^35 pages.
	// That's 2^26 L1 page tables. 2^23 bytes of bitset.
	// That's 2^17 L2 page tables. 2^14 bytes of bitset.
	// That's 2^ 8 L3 page tables. 2^ 5 bytes of bitset.
	// Tracking of the committed L1 tables is done in l1Commit, a region of coreMMSpace.
	// 	(This array is committed as needed, tracked using l1CommitCommit.)
	// Tracking of the committed L2 tables is done in l2Commit.
	// Tracking of the committed L3 tables is done in l3Commit.
#define L1_COMMIT_SIZE_BYTES (1 << 23)
#define L1_COMMIT_COMMIT_SIZE_BYTES (1 << 8)
#define L2_COMMIT_SIZE_BYTES (1 << 14)
#define L3_COMMIT_SIZE_BYTES (1 << 5)
	uint8_t *l1Commit;
	uint8_t l1CommitCommit[L1_COMMIT_COMMIT_SIZE_BYTES];
	uint8_t l2Commit[L2_COMMIT_SIZE_BYTES];
	uint8_t l3Commit[L3_COMMIT_SIZE_BYTES];
	size_t pageTablesCommitted;
	size_t pageTablesActive;

	// TODO Consider core/kernel mutex consistency? I think it's fine, but...
	KMutex mutex; // Acquire to modify the page tables.
};

#define MM_CORE_REGIONS_START (0xFFFF8001F0000000)
#define MM_CORE_REGIONS_COUNT ((0xFFFF800200000000 - 0xFFFF8001F0000000) / sizeof(MMRegion))
#define MM_KERNEL_SPACE_START (0xFFFF900000000000)
#define MM_KERNEL_SPACE_SIZE  (0xFFFFF00000000000 - 0xFFFF900000000000)
#define MM_MODULES_START      (0xFFFFFFFF90000000)
#define MM_MODULES_SIZE	      (0xFFFFFFFFC0000000 - 0xFFFFFFFF90000000)

#define ArchCheckBundleHeader()       (header.mapAddress > 0x800000000000UL || header.mapAddress < 0x1000 || fileSize > 0x1000000000000UL)
#define ArchCheckELFHeader()          (header->virtualAddress > 0x800000000000UL || header->virtualAddress < 0x1000 || header->segmentSize > 0x1000000000000UL)

#define K_ARCH_STACK_GROWS_DOWN
#define K_ARCH_NAME "x86_64"

#endif

#ifdef IMPLEMENTATION

#define MM_CORE_SPACE_START   (0xFFFF800100000000)
#define MM_CORE_SPACE_SIZE    (0xFFFF8001F0000000 - 0xFFFF800100000000)
#define MM_USER_SPACE_START   (0x100000000000)
#define MM_USER_SPACE_SIZE    (0xF00000000000 - 0x100000000000)
#define LOW_MEMORY_MAP_START  (0xFFFFFE0000000000)
#define LOW_MEMORY_LIMIT      (0x100000000) // The first 4GB is mapped here.

// Recursive page table mapping in slot 0x1FE, so that the top 2GB are available for mcmodel kernel.
#define PAGE_TABLE_L4 ((volatile uint64_t *) 0xFFFFFF7FBFDFE000)
#define PAGE_TABLE_L3 ((volatile uint64_t *) 0xFFFFFF7FBFC00000)
#define PAGE_TABLE_L2 ((volatile uint64_t *) 0xFFFFFF7F80000000)
#define PAGE_TABLE_L1 ((volatile uint64_t *) 0xFFFFFF0000000000)
#define ENTRIES_PER_PAGE_TABLE (512)
#define ENTRIES_PER_PAGE_TABLE_BITS (9)

uint8_t coreL1Commit[(0xFFFF800200000000 - 0xFFFF800100000000) >> (/* ENTRIES_PER_PAGE_TABLE_BITS */ 9 + K_PAGE_BITS + 3)];

extern "C" uintptr_t ProcessorGetRSP();
extern "C" uintptr_t ProcessorGetRBP();
extern "C" uint64_t ProcessorReadMXCSR();
extern "C" void ProcessorInstallTSS(uint32_t *gdt, uint32_t *tss);

extern "C" void SSSE3Framebuffer32To24Copy(volatile uint8_t *destination, volatile uint8_t *source, size_t pixelGroups);
extern "C" uintptr_t _KThreadTerminate;

extern bool pagingNXESupport;
extern bool pagingPCIDSupport;
extern bool pagingSMEPSupport;
extern bool pagingTCESupport;

extern "C" bool simdSSE3Support;
extern "C" bool simdSSSE3Support;

struct InterruptContext {
	uint64_t cr2, ds;
	uint8_t  fxsave[512 + 16];
	uint64_t _check, cr8;
	uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
	uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
	uint64_t interruptNumber, errorCode;
	uint64_t rip, cs, flags, rsp, ss;
};

#include <arch/x86_pc.h>
#include <drivers/acpi.cpp>
#include <arch/x86_pc.cpp>

volatile uintptr_t tlbShootdownVirtualAddress;
volatile size_t tlbShootdownPageCount;

typedef void (*CallFunctionOnAllProcessorsCallbackFunction)();
volatile CallFunctionOnAllProcessorsCallbackFunction callFunctionOnAllProcessorsCallback;
volatile uintptr_t callFunctionOnAllProcessorsRemaining;

void ArchSetPCIIRQLine(uint8_t slot, uint8_t pin, uint8_t line) {
	pciIRQLines[slot][pin] = line;
}

bool MMArchCommitPageTables(MMSpace *space, MMRegion *region) {
	KMutexAssertLocked(&space->reserveMutex);

	MMArchVAS *data = &space->data;

	uintptr_t base = (region->baseAddress - (space == coreMMSpace ? MM_CORE_SPACE_START : 0)) & 0x7FFFFFFFF000;
	uintptr_t end = base + (region->pageCount << K_PAGE_BITS);
	uintptr_t needed = 0;

	for (uintptr_t i = base; i < end; i += 1L << (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 3)) {
		uintptr_t indexL4 = i >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 3);
		if (!(data->l3Commit[indexL4 >> 3] & (1 << (indexL4 & 7)))) needed++;
		i = indexL4 << (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 3);
	}

	for (uintptr_t i = base; i < end; i += 1L << (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 2)) {
		uintptr_t indexL3 = i >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 2);
		if (!(data->l2Commit[indexL3 >> 3] & (1 << (indexL3 & 7)))) needed++;
		i = indexL3 << (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 2);
	}

	uintptr_t previousIndexL2I = -1;

	for (uintptr_t i = base; i < end; i += 1L << (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 1)) {
		uintptr_t indexL2 = i >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 1);
		uintptr_t indexL2I = indexL2 >> 15;
		if (!(data->l1CommitCommit[indexL2I >> 3] & (1 << (indexL2I & 7)))) needed += previousIndexL2I != indexL2I ? 2 : 1;
		else if (!(data->l1Commit[indexL2 >> 3] & (1 << (indexL2 & 7)))) needed++;
		previousIndexL2I = indexL2I;
		i = indexL2 << (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 1);
	}

	if (needed) {
		if (!MMCommit(needed * K_PAGE_SIZE, true)) {
			return false;
		}

		data->pageTablesCommitted += needed;
	}

	for (uintptr_t i = base; i < end; i += 1L << (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 3)) {
		uintptr_t indexL4 = i >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 3);
		data->l3Commit[indexL4 >> 3] |= (1 << (indexL4 & 7));
		i = indexL4 << (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 3);
	}

	for (uintptr_t i = base; i < end; i += 1L << (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 2)) {
		uintptr_t indexL3 = i >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 2);
		data->l2Commit[indexL3 >> 3] |= (1 << (indexL3 & 7));
		i = indexL3 << (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 2);
	}

	for (uintptr_t i = base; i < end; i += 1L << (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 1)) {
		uintptr_t indexL2 = i >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 1);
		uintptr_t indexL2I = indexL2 >> 15;
		data->l1CommitCommit[indexL2I >> 3] |= (1 << (indexL2I & 7));
		data->l1Commit[indexL2 >> 3] |= (1 << (indexL2 & 7));
		i = indexL2 << (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 1);
	}

	return true;
}

bool MMArchIsBufferInUserRange(uintptr_t baseAddress, size_t byteCount) {
	if (baseAddress               & 0xFFFF800000000000) return false;
	if (byteCount                 & 0xFFFF800000000000) return false;
	if ((baseAddress + byteCount) & 0xFFFF800000000000) return false;
	return true;
}

bool MMArchHandlePageFault(uintptr_t address, uint32_t flags) {
	// EsPrint("Fault %x\n", address);
	address &= ~(K_PAGE_SIZE - 1);
	bool forSupervisor = flags & MM_HANDLE_PAGE_FAULT_FOR_SUPERVISOR;

	if (!ProcessorAreInterruptsEnabled()) {
		KernelPanic("MMArchHandlePageFault - Page fault with interrupts disabled.\n");
	}

	if (address < K_PAGE_SIZE) {
	} else if (address >= LOW_MEMORY_MAP_START && address < LOW_MEMORY_MAP_START + LOW_MEMORY_LIMIT && forSupervisor) {
		// We want to access a physical page within the first 4GB.
		MMArchMapPage(kernelMMSpace, address - LOW_MEMORY_MAP_START, address, MM_MAP_PAGE_COMMIT_TABLES_NOW);
		return true;
	} else if (address >= MM_CORE_REGIONS_START && address < MM_CORE_REGIONS_START + MM_CORE_REGIONS_COUNT * sizeof(MMRegion) && forSupervisor) {
		// This is where coreMMSpace stores its regions.
		// Allocate physical memory and map it.
		MMArchMapPage(kernelMMSpace, MMPhysicalAllocate(MM_PHYSICAL_ALLOCATE_ZEROED), address, MM_MAP_PAGE_COMMIT_TABLES_NOW);
		return true;
	} else if (address >= MM_CORE_SPACE_START && address < MM_CORE_SPACE_START + MM_CORE_SPACE_SIZE && forSupervisor) {
		return MMHandlePageFault(coreMMSpace, address, flags);
	} else if (address >= MM_KERNEL_SPACE_START && address < MM_KERNEL_SPACE_START + MM_KERNEL_SPACE_SIZE && forSupervisor) {
		return MMHandlePageFault(kernelMMSpace, address, flags);
	} else if (address >= MM_MODULES_START && address < MM_MODULES_START + MM_MODULES_SIZE && forSupervisor) {
		return MMHandlePageFault(kernelMMSpace, address, flags);
	} else {
		Thread *thread = GetCurrentThread();
		MMSpace *space = thread->temporaryAddressSpace;
		if (!space) space = thread->process->vmm;
		return MMHandlePageFault(space, address, flags);
	}

	return false;
}

void ArchCallFunctionOnAllProcessors(CallFunctionOnAllProcessorsCallbackFunction callback, bool includingThisProcessor) {
	KSpinlockAssertLocked(&ipiLock);

	if (KGetCPUCount() > 1) {
		callFunctionOnAllProcessorsCallback = callback;
		callFunctionOnAllProcessorsRemaining = KGetCPUCount();
		size_t ignored = ProcessorSendIPI(CALL_FUNCTION_ON_ALL_PROCESSORS_IPI);
		__sync_fetch_and_sub(&callFunctionOnAllProcessorsRemaining, ignored);
		while (callFunctionOnAllProcessorsRemaining);
		static volatile size_t totalIgnored = 0;
		totalIgnored += ignored;
	}

	if (includingThisProcessor) callback();
}

// TODO How should this be determined?
#define INVALIDATE_ALL_PAGES_THRESHOLD (1024)

void TLBShootdownCallback() {
	uintptr_t page = tlbShootdownVirtualAddress;

	if (tlbShootdownPageCount > INVALIDATE_ALL_PAGES_THRESHOLD) { 
		ProcessorInvalidateAllPages();
	} else {
		for (uintptr_t i = 0; i < tlbShootdownPageCount; i++, page += K_PAGE_SIZE) {
			ProcessorInvalidatePage(page);
		}
	}
}

void MMArchInvalidatePages(uintptr_t virtualAddressStart, uintptr_t pageCount) {
	// This must be done with spinlock acquired, otherwise this processor could change.

	// TODO Only send the IPI to the processors that are actually executing threads with the virtual address space.
	// 	Currently we only support the kernel's virtual address space, so this'll apply to all processors.
	// 	If we use Intel's PCID then we may have to send this to all processors anyway.
	// 	And we'll probably also have to be careful with shared memory regions.
	//	...actually I think we might not bother doing this.

	KSpinlockAcquire(&ipiLock);
	tlbShootdownVirtualAddress = virtualAddressStart;
	tlbShootdownPageCount = pageCount;
	ArchCallFunctionOnAllProcessors(TLBShootdownCallback, true);
	KSpinlockRelease(&ipiLock);
}

InterruptContext *ArchInitialiseThread(uintptr_t kernelStack, uintptr_t kernelStackSize, Thread *thread, 
		uintptr_t startAddress, uintptr_t argument1, uintptr_t argument2,
		bool userland, uintptr_t stack, uintptr_t userStackSize) {
	InterruptContext *context = ((InterruptContext *) (kernelStack + kernelStackSize - 8)) - 1;
	thread->kernelStack = kernelStack + kernelStackSize - 8;
	
	// Terminate the thread when the outermost function exists.
	*((uintptr_t *) (kernelStack + kernelStackSize - 8)) = (uintptr_t) &_KThreadTerminate;

	context->fxsave[32] = 0x80;
	context->fxsave[33] = 0x1F;

	if (userland) {
		context->cs = 0x5B;
		context->ds = 0x63;
		context->ss = 0x63;
	} else {
		context->cs = 0x48;
		context->ds = 0x50;
		context->ss = 0x50;
	}

	context->_check = 0x123456789ABCDEF; // Stack corruption detection.
	context->flags = 1 << 9; // Interrupt flag
	context->rip = startAddress;
	context->rsp = stack + userStackSize - 8; // The stack should be 16-byte aligned before the call instruction.
	context->rdi = argument1;
	context->rsi = argument2;

	return context;
}

bool MMArchInitialiseUserSpace(MMSpace *space, MMRegion *region) {
	region->baseAddress = MM_USER_SPACE_START; 
	region->pageCount = MM_USER_SPACE_SIZE / K_PAGE_SIZE;

	if (!MMCommit(K_PAGE_SIZE, true)) {
		return false;
	}

	space->data.cr3 = MMPhysicalAllocate(ES_FLAGS_DEFAULT);

	KMutexAcquire(&coreMMSpace->reserveMutex);
	MMRegion *l1Region = MMReserve(coreMMSpace, L1_COMMIT_SIZE_BYTES, MM_REGION_NORMAL | MM_REGION_NO_COMMIT_TRACKING | MM_REGION_FIXED);
	if (l1Region) space->data.l1Commit = (uint8_t *) l1Region->baseAddress;
	KMutexRelease(&coreMMSpace->reserveMutex);

	if (!space->data.l1Commit) {
		return false;
	}

	uint64_t *pageTable = (uint64_t *) MMMapPhysical(kernelMMSpace, (uintptr_t) space->data.cr3, K_PAGE_SIZE, ES_FLAGS_DEFAULT);
	EsMemoryZero(pageTable + 0x000, K_PAGE_SIZE / 2);
	EsMemoryCopy(pageTable + 0x100, (uint64_t *) (PAGE_TABLE_L4 + 0x100), K_PAGE_SIZE / 2);
	pageTable[512 - 2] = space->data.cr3 | 3;
	MMFree(kernelMMSpace, pageTable);

	return true;
}

void MMArchFreeVAS(MMSpace *space) {
	for (uintptr_t i = 0; i < 256; i++) {
		if (!PAGE_TABLE_L4[i]) continue;

		for (uintptr_t j = i * ENTRIES_PER_PAGE_TABLE; j < (i + 1) * ENTRIES_PER_PAGE_TABLE; j++) {
			if (!PAGE_TABLE_L3[j]) continue;

			for (uintptr_t k = j * ENTRIES_PER_PAGE_TABLE; k < (j + 1) * ENTRIES_PER_PAGE_TABLE; k++) {
				if (!PAGE_TABLE_L2[k]) continue;
				MMPhysicalFree(PAGE_TABLE_L2[k] & ~(K_PAGE_SIZE - 1));
				space->data.pageTablesActive--;
			}

			MMPhysicalFree(PAGE_TABLE_L3[j] & ~(K_PAGE_SIZE - 1));
			space->data.pageTablesActive--;
		}

		MMPhysicalFree(PAGE_TABLE_L4[i] & ~(K_PAGE_SIZE - 1));
		space->data.pageTablesActive--;
	}

	if (space->data.pageTablesActive) {
		KernelPanic("MMArchFreeVAS - Space %x still has %d page tables active.\n", space, space->data.pageTablesActive);
	}

	KMutexAcquire(&coreMMSpace->reserveMutex);
	MMRegion *l1CommitRegion = MMFindRegion(coreMMSpace, (uintptr_t) space->data.l1Commit);
	MMArchUnmapPages(coreMMSpace, l1CommitRegion->baseAddress, l1CommitRegion->pageCount, MM_UNMAP_PAGES_FREE);
	MMUnreserve(coreMMSpace, l1CommitRegion, false /* we manually unmap pages above, so we can free them */);
	KMutexRelease(&coreMMSpace->reserveMutex);
	MMDecommit(space->data.pageTablesCommitted * K_PAGE_SIZE, true);
}

void MMArchFinalizeVAS(MMSpace *space) {
	if (!space->data.cr3) return;
	// Freeing the L4 page table has to be done in the kernel process, since it's the page CR3 would points to!
	// Therefore, this function only is called in an async task.
	if (space->data.cr3 == ProcessorReadCR3()) KernelPanic("MMArchFinalizeVAS - Space %x is active.\n", space);
	PMZero(&space->data.cr3, 1, true); // Fail as fast as possible if someone's still using this page.
	MMPhysicalFree(space->data.cr3); 
	MMDecommit(K_PAGE_SIZE, true); 
}

void ContextSanityCheck(InterruptContext *context) {
	if (!context || context->cs > 0x100 || context->ds > 0x100 || context->ss > 0x100 
			|| (context->rip >= 0x1000000000000 && context->rip < 0xFFFF000000000000)
			|| (context->rip < 0xFFFF800000000000 && context->cs == 0x48)) {
		KernelPanic("ContextSanityCheck - Corrupt context (%x/%x/%x/%x)\nRIP = %x, RSP = %x\n", context, context->cs, context->ds, context->ss, context->rip, context->rsp);
	}
}

extern "C" void InterruptHandler(InterruptContext *context) {
	if (scheduler.panic && context->interruptNumber != 2) {
		return;
	}

	if (ProcessorAreInterruptsEnabled()) {
		KernelPanic("InterruptHandler - Interrupts were enabled at the start of an interrupt handler.\n");
	}

	CPULocalStorage *local = GetLocalStorage();
	uintptr_t interrupt = context->interruptNumber;

	if (local && local->spinlockCount && context->cr8 != 0xE) {
		KernelPanic("InterruptHandler - Local spinlockCount is %d but interrupts were enabled (%x/%x).\n", local->spinlockCount, local, context);
	}

#if 0
#ifdef EARLY_DEBUGGING
#ifdef VGA_TEXT_MODE
	if (local) {
		TERMINAL_ADDRESS[local->processorID] += 0x1000;
	}
#else
	if (graphics.target && graphics.target->debugPutBlock) {
		graphics.target->debugPutBlock(local->processorID * 3 + 3, 3, true);
		graphics.target->debugPutBlock(local->processorID * 3 + 4, 3, true);
		graphics.target->debugPutBlock(local->processorID * 3 + 3, 4, true);
		graphics.target->debugPutBlock(local->processorID * 3 + 4, 4, true);
	}
#endif
#endif
#endif

	if (interrupt < 0x20) {
		// If we received a non-maskable interrupt, halt execution.
		if (interrupt == 2) {
			local->panicContext = context;
			ProcessorHalt();
		}

		bool supervisor = (context->cs & 3) == 0;

		if (!supervisor) {
			// EsPrint("User interrupt: %x/%x/%x\n", interrupt, context->cr2, context->errorCode);

			if (context->cs != 0x5B && context->cs != 0x6B) {
				KernelPanic("InterruptHandler - Unexpected value of CS 0x%X\n", context->cs);
			}

			if (GetCurrentThread()->isKernelThread) {
				KernelPanic("InterruptHandler - Kernel thread executing user code. (1)\n");
			}

			// User-code exceptions are *basically* the same thing as system calls.
			Thread *currentThread = GetCurrentThread();
			ThreadTerminatableState previousTerminatableState;
			previousTerminatableState = currentThread->terminatableState;
			currentThread->terminatableState = THREAD_IN_SYSCALL;

			if (local && local->spinlockCount) {
				KernelPanic("InterruptHandler - User exception occurred with spinlock acquired.\n");
			}

			// Re-enable interrupts during exception handling.
			ProcessorEnableInterrupts();

			if (interrupt == 14) {
				bool success = MMArchHandlePageFault(context->cr2, (context->errorCode & 2) ? MM_HANDLE_PAGE_FAULT_WRITE : 0);

				if (success) {
					goto resolved;
				}
			}

			if (interrupt == 0x13) {
				EsPrint("ProcessorReadMXCSR() = %x\n", ProcessorReadMXCSR());
			}

			// TODO Usermode exceptions and debugging.
			KernelLog(LOG_ERROR, "Arch", "unhandled userland exception", 
					"InterruptHandler - Exception (%z) in userland process (%z).\nRIP = %x (CPU %d)\nRSP = %x\nX86_64 error codes: [err] %x, [cr2] %x\n", 
					exceptionInformation[interrupt], 
					currentThread->process->cExecutableName,
					context->rip, local->processorID, context->rsp, context->errorCode, context->cr2);

			EsPrint("Attempting to make a stack trace...\n");

			{
				uint64_t rbp = context->rbp;
				int traceDepth = 0;

				while (rbp && traceDepth < 32) {
					uint64_t value;
					if (!MMArchIsBufferInUserRange(rbp, 16)) break;
					if (!MMArchSafeCopy((uintptr_t) &value, rbp + 8, sizeof(uint64_t))) break;
					EsPrint("\t%d: %x\n", ++traceDepth, value);
					if (!value) break;
					if (!MMArchSafeCopy((uintptr_t) &rbp, rbp, sizeof(uint64_t))) break;
				}
			}

			EsPrint("Stack trace complete.\n");

			EsCrashReason crashReason;
			EsMemoryZero(&crashReason, sizeof(EsCrashReason));
			crashReason.errorCode = ES_FATAL_ERROR_PROCESSOR_EXCEPTION;
			crashReason.duringSystemCall = (EsSyscallType) -1;
			ProcessCrash(currentThread->process, &crashReason);

			resolved:;

			if (currentThread->terminatableState != THREAD_IN_SYSCALL) {
				KernelPanic("InterruptHandler - Thread changed terminatable status during interrupt.\n");
			}

			currentThread->terminatableState = previousTerminatableState;

			if (currentThread->terminating || currentThread->paused) {
				ProcessorFakeTimerInterrupt();
			}

			// Disable interrupts when we're done.
			ProcessorDisableInterrupts();

			// EsPrint("User interrupt complete.\n", interrupt, context->cr2);
		} else {
			if (context->cs != 0x48) {
				KernelPanic("InterruptHandler - Unexpected value of CS 0x%X\n", context->cs);
			}

			if (interrupt == 14) {
				// EsPrint("PF: %x\n", context->cr2);

				if ((context->errorCode & (1 << 3))) {
					goto fault;
				}

				if ((context->flags & 0x200) && context->cr8 != 0xE) {
					ProcessorEnableInterrupts();
				}

				if (local && local->spinlockCount && ((context->cr2 >= 0xFFFF900000000000 && context->cr2 < 0xFFFFF00000000000) 
							|| context->cr2 < 0x8000000000000000)) {
					KernelPanic("HandlePageFault - Page fault occurred with spinlocks active at %x (S = %x, B = %x, LG = %x, CR2 = %x, local = %x).\n", 
							context->rip, context->rsp, context->rbp, local->currentThread->lastKnownExecutionAddress, context->cr2, local);
				}
				
				if (!MMArchHandlePageFault(context->cr2, MM_HANDLE_PAGE_FAULT_FOR_SUPERVISOR
							| ((context->errorCode & 2) ? MM_HANDLE_PAGE_FAULT_WRITE : 0))) {
					if (local->currentThread->inSafeCopy && context->cr2 < 0x8000000000000000) {
						context->rip = context->r8; // See definition of MMArchSafeCopy.
					} else {
						goto fault;
					}
				}

				ProcessorDisableInterrupts();
			} else {
				fault:
				KernelPanic("Unresolvable processor exception encountered in supervisor mode.\n%z\nRIP = %x (CPU %d)\nX86_64 error codes: [err] %x, [cr2] %x\n"
						"Stack: [rsp] %x, [rbp] %x\nRegisters: [rax] %x, [rbx] %x, [rsi] %x, [rdi] %x.\nThread ID = %d\n", 
						exceptionInformation[interrupt], context->rip, local ? local->processorID : -1, context->errorCode, context->cr2, 
						context->rsp, context->rbp, context->rax, context->rbx, context->rsi, context->rdi, 
						local && local->currentThread ? local->currentThread->id : -1);
			}
		}
	} else if (interrupt == 0xFF) {
		// Spurious interrupt (APIC), ignore.
	} else if (interrupt >= 0x20 && interrupt < 0x30) {
		// Spurious interrupt (PIC), ignore.
	} else if (interrupt >= 0xF0 && interrupt < 0xFE) {
		// IPI.
		// Warning: This code executes at a special IRQL! Do not acquire spinlocks!!

		if (interrupt == CALL_FUNCTION_ON_ALL_PROCESSORS_IPI) {
			if (!callFunctionOnAllProcessorsRemaining) KernelPanic("InterruptHandler - callFunctionOnAllProcessorsRemaining is 0 (a).\n");
			callFunctionOnAllProcessorsCallback();
			if (!callFunctionOnAllProcessorsRemaining) KernelPanic("InterruptHandler - callFunctionOnAllProcessorsRemaining is 0 (b).\n");
			__sync_fetch_and_sub(&callFunctionOnAllProcessorsRemaining, 1);
		}

		LapicEndOfInterrupt();
	} else if (interrupt >= INTERRUPT_VECTOR_MSI_START && interrupt < INTERRUPT_VECTOR_MSI_START + INTERRUPT_VECTOR_MSI_COUNT && local) {
		KSpinlockAcquire(&irqHandlersLock);
		MSIHandler handler = msiHandlers[interrupt - INTERRUPT_VECTOR_MSI_START];
		KSpinlockRelease(&irqHandlersLock);
		local->irqSwitchThread = false;

		if (!handler.callback) {
			KernelLog(LOG_ERROR, "Arch", "unexpected MSI", "Unexpected MSI vector %X (no handler).\n", interrupt);
		} else {
			handler.callback(interrupt - INTERRUPT_VECTOR_MSI_START, handler.context);
		}

		if (local->irqSwitchThread && scheduler.started && local->schedulerReady) {
			scheduler.Yield(context); // LapicEndOfInterrupt is called in PostContextSwitch.
			KernelPanic("InterruptHandler - Returned from Scheduler::Yield.\n");
		}

		LapicEndOfInterrupt();
	} else if (local) {
		// IRQ.

		local->irqSwitchThread = false;

		if (interrupt == TIMER_INTERRUPT) {
			local->irqSwitchThread = true;
		} else if (interrupt == YIELD_IPI) {
			local->irqSwitchThread = true;
			GetCurrentThread()->receivedYieldIPI = true;
		} else if (interrupt >= IRQ_BASE && interrupt < IRQ_BASE + 0x20) {
			GetLocalStorage()->inIRQ = true;

			uintptr_t line = interrupt - IRQ_BASE;
			KernelLog(LOG_VERBOSE, "Arch", "IRQ start", "IRQ start %d.\n", line);
			KSpinlockAcquire(&irqHandlersLock);

			for (uintptr_t i = 0; i < sizeof(irqHandlers) / sizeof(irqHandlers[0]); i++) {
				IRQHandler handler = irqHandlers[i];
				if (!handler.callback) continue;

				if (handler.line == -1) {
					// Before we get the actual IRQ line information from ACPI (which might take it a while),
					// only test that the IRQ is in the correct range for PCI interrupts.
					// This is a bit slower because we have to dispatch the interrupt to more drivers,
					// but it shouldn't break anything because they're all supposed to handle overloading anyway.
					// This is mess. Hopefully all modern computers will use MSIs for anything important.

					if (line != 9 && line != 10 && line != 11) {
						continue;
					} else {
						uint8_t mappedLine = pciIRQLines[handler.pciDevice->slot][handler.pciDevice->interruptPin - 1];

						if (mappedLine && line != mappedLine) {
							continue;
						}
					}
				} else {
					if ((uintptr_t) handler.line != line) {
						continue;
					}
				}

				KSpinlockRelease(&irqHandlersLock);
				handler.callback(interrupt - IRQ_BASE, handler.context);
				KSpinlockAcquire(&irqHandlersLock);
			}

			KSpinlockRelease(&irqHandlersLock);
			KernelLog(LOG_VERBOSE, "Arch", "IRQ end", "IRQ end %d.\n", line);

			GetLocalStorage()->inIRQ = false;
		}

		if (local->irqSwitchThread && scheduler.started && local->schedulerReady) {
			scheduler.Yield(context); // LapicEndOfInterrupt is called in PostContextSwitch.
			KernelPanic("InterruptHandler - Returned from Scheduler::Yield.\n");
		}

		LapicEndOfInterrupt();
	}

	// Sanity check.
	ContextSanityCheck(context);

	if (ProcessorAreInterruptsEnabled()) {
		KernelPanic("InterruptHandler - Interrupts were enabled while returning from an interrupt handler.\n");
	}
}

extern "C" uintptr_t Syscall(uintptr_t argument0, uintptr_t argument1, uintptr_t argument2, 
		uintptr_t returnAddress, uintptr_t argument3, uintptr_t argument4, uintptr_t *userStackPointer) {
	(void) returnAddress;
	return DoSyscall((EsSyscallType) argument0, argument1, argument2, argument3, argument4, false, nullptr, userStackPointer);
}

EsError ArchApplyRelocation(uintptr_t type, uint8_t *buffer, uintptr_t offset, uintptr_t result) {
	if (type == 0) {}
	else if (type == 10 /* R_X86_64_32 */)    *((uint32_t *) (buffer + offset)) = result; 
	else if (type == 11 /* R_X86_64_32S */)   *((uint32_t *) (buffer + offset)) = result; 
	else if (type == 1  /* R_X86_64_64 */)    *((uint64_t *) (buffer + offset)) = result;
	else if (type == 2  /* R_X86_64_PC32 */)  *((uint32_t *) (buffer + offset)) = result - ((uint64_t) buffer + offset);
	else if (type == 24 /* R_X86_64_PC64 */)  *((uint64_t *) (buffer + offset)) = result - ((uint64_t) buffer + offset);
	else if (type == 4  /* R_X86_64_PLT32 */) *((uint32_t *) (buffer + offset)) = result - ((uint64_t) buffer + offset);
	else return ES_ERROR_UNSUPPORTED_FEATURE;
	return ES_SUCCESS;
}

#include <kernel/terminal.cpp>

#endif
