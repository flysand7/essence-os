#ifndef IMPLEMENTATION

typedef struct ACPIProcessor ArchCPU;

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
// Note: IRQ_BASE is currently 0x50.
#define CALL_FUNCTION_ON_ALL_PROCESSORS_IPI (0xF0)
#define KERNEL_PANIC_IPI (0) // NMIs ignore the interrupt vector.

#define INTERRUPT_VECTOR_MSI_START (0x70)
#define INTERRUPT_VECTOR_MSI_COUNT (0x40)

struct InterruptContext {
	uint64_t cr2, ds;
	uint8_t  fxsave[512 + 16];
	uint64_t _check, cr8;
	uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
	uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
	uint64_t interruptNumber, errorCode;
	uint64_t rip, cs, flags, rsp, ss;
};

struct VirtualAddressSpaceData {
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

	// TODO Consider core/kernel mutex consistency? I think it's fine, but...
	KMutex mutex; // Acquire to modify the page tables.
};

#define VIRTUAL_ADDRESS_SPACE_DATA() VirtualAddressSpaceData data
#define VIRTUAL_ADDRESS_SPACE_IDENTIFIER(x) ((x)->data.cr3)

#define MM_CORE_SPACE_START   (0xFFFF800100000000)
#define MM_CORE_SPACE_SIZE    (0xFFFF8001F0000000 - 0xFFFF800100000000)
#define MM_CORE_REGIONS_START (0xFFFF8001F0000000)
#define MM_CORE_REGIONS_COUNT ((0xFFFF800200000000 - 0xFFFF8001F0000000) / sizeof(MMRegion))
#define MM_KERNEL_SPACE_START (0xFFFF900000000000)
#define MM_KERNEL_SPACE_SIZE  (0xFFFFF00000000000 - 0xFFFF900000000000)
#define MM_MODULES_START      (0xFFFFFFFF90000000)
#define MM_MODULES_SIZE	      (0xFFFFFFFFC0000000 - 0xFFFFFFFF90000000)
#define MM_USER_SPACE_START   (0x100000000000)
#define MM_USER_SPACE_SIZE    (0xF00000000000 - 0x100000000000)

#define ArchIsAddressInKernelSpace(x) ((uintptr_t) (x) >= 0xFFFF800000000000)

uint8_t coreL1Commit[(0xFFFF800200000000 - 0xFFFF800100000000) >> (/* ENTRIES_PER_PAGE_TABLE_BITS */ 9 + K_PAGE_BITS + 3)];

#endif

#ifdef IMPLEMENTATION

extern uintptr_t bootloaderInformationOffset;
extern "C" bool simdSSE3Support;
extern "C" bool simdSSSE3Support;

volatile uintptr_t tlbShootdownVirtualAddress;
volatile size_t tlbShootdownPageCount;

extern "C" uintptr_t _KThreadTerminate;

typedef void (*CallFunctionOnAllProcessorsCallbackFunction)();
volatile CallFunctionOnAllProcessorsCallbackFunction callFunctionOnAllProcessorsCallback;
volatile uintptr_t callFunctionOnAllProcessorsRemaining;

// Recursive page table mapping in slot 0x1FE, so that the top 2GB are available for mcmodel kernel.
#define PAGE_TABLE_L4 ((volatile uint64_t *) 0xFFFFFF7FBFDFE000)
#define PAGE_TABLE_L3 ((volatile uint64_t *) 0xFFFFFF7FBFC00000)
#define PAGE_TABLE_L2 ((volatile uint64_t *) 0xFFFFFF7F80000000)
#define PAGE_TABLE_L1 ((volatile uint64_t *) 0xFFFFFF0000000000)
#define ENTRIES_PER_PAGE_TABLE (512)
#define ENTRIES_PER_PAGE_TABLE_BITS (9)

bool MMArchCommitPageTables(MMSpace *space, MMRegion *region) {
	KMutexAssertLocked(&space->reserveMutex);

	VirtualAddressSpaceData *data = &space->data;

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

bool MMArchMakePageWritable(MMSpace *space, uintptr_t virtualAddress) {
	KMutexAcquire(&space->data.mutex);
	EsDefer(KMutexRelease(&space->data.mutex));

	virtualAddress  &= 0x0000FFFFFFFFF000;

	uintptr_t indexL4 = virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 3);
	uintptr_t indexL3 = virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 2);
	uintptr_t indexL2 = virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 1);
	uintptr_t indexL1 = virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 0);

	if ((PAGE_TABLE_L4[indexL4] & 1) == 0) return false;
	if ((PAGE_TABLE_L3[indexL3] & 1) == 0) return false;
	if ((PAGE_TABLE_L2[indexL2] & 1) == 0) return false;
	if ((PAGE_TABLE_L1[indexL1] & 1) == 0) return false;

	PAGE_TABLE_L1[indexL1] |= 2;
	return true;
}

void MMArchMapPage(MMSpace *space, uintptr_t physicalAddress, uintptr_t virtualAddress, unsigned flags) {
	// TODO Use the no-execute bit.

	if (physicalAddress & (K_PAGE_SIZE - 1)) {
		KernelPanic("MMArchMapPage - Physical address not page aligned.\n");
	}

	bool acquireFrameLock = !(flags & (MM_MAP_PAGE_NO_NEW_TABLES | MM_MAP_PAGE_FRAME_LOCK_ACQUIRED));
	if (acquireFrameLock) KMutexAcquire(&pmm.pageFrameMutex);
	EsDefer(if (acquireFrameLock) KMutexRelease(&pmm.pageFrameMutex););

	bool acquireSpaceLock = ~flags & MM_MAP_PAGE_NO_NEW_TABLES;
	if (acquireSpaceLock) KMutexAcquire(&space->data.mutex);
	EsDefer(if (acquireSpaceLock) KMutexRelease(&space->data.mutex));

	uintptr_t cr3 = space->data.cr3;

	if (!ArchIsAddressInKernelSpace(virtualAddress) 
			&& ProcessorReadCR3() != cr3) {
		KernelPanic("MMArchMapPage - Attempt to map page into other address space.\n");
	} else if (!physicalAddress) {
		KernelPanic("MMArchMapPage - Attempt to map physical page 0.\n");
	} else if (!virtualAddress) {
		KernelPanic("MMArchMapPage - Attempt to map virtual page 0.\n");
	}

	// EsPrint("\tMap, %x -> %x\n", virtualAddress, physicalAddress);

	uintptr_t oldVirtualAddress = virtualAddress;
	physicalAddress &= 0xFFFFFFFFFFFFF000;
	virtualAddress  &= 0x0000FFFFFFFFF000;

	uintptr_t indexL4 = virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 3);
	uintptr_t indexL3 = virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 2);
	uintptr_t indexL2 = virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 1);
	uintptr_t indexL1 = virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 0);

	if (space != coreMMSpace && space != kernelMMSpace /* Don't check the kernel's space since the bootloader's tables won't be committed. */) {
		if (!(space->data.l3Commit[indexL4 >> 3] & (1 << (indexL4 & 7)))) KernelPanic("MMArchMapPage - Attempt to map using uncommitted L3 page table.\n");
		if (!(space->data.l2Commit[indexL3 >> 3] & (1 << (indexL3 & 7)))) KernelPanic("MMArchMapPage - Attempt to map using uncommitted L2 page table.\n");
		if (!(space->data.l1Commit[indexL2 >> 3] & (1 << (indexL2 & 7)))) KernelPanic("MMArchMapPage - Attempt to map using uncommitted L1 page table.\n");
	}

	if ((PAGE_TABLE_L4[indexL4] & 1) == 0) {
		if (flags & MM_MAP_PAGE_NO_NEW_TABLES) KernelPanic("MMArchMapPage - NO_NEW_TABLES flag set, but a table was missing.\n");
		PAGE_TABLE_L4[indexL4] = MMPhysicalAllocate(MM_PHYSICAL_ALLOCATE_LOCK_ACQUIRED) | 7;
		ProcessorInvalidatePage((uintptr_t) (PAGE_TABLE_L3 + indexL3)); // Not strictly necessary.
		EsMemoryZero((void *) ((uintptr_t) (PAGE_TABLE_L3 + indexL3) & ~(K_PAGE_SIZE - 1)), K_PAGE_SIZE);
	}

	if ((PAGE_TABLE_L3[indexL3] & 1) == 0) {
		if (flags & MM_MAP_PAGE_NO_NEW_TABLES) KernelPanic("MMArchMapPage - NO_NEW_TABLES flag set, but a table was missing.\n");
		PAGE_TABLE_L3[indexL3] = MMPhysicalAllocate(MM_PHYSICAL_ALLOCATE_LOCK_ACQUIRED) | 7;
		ProcessorInvalidatePage((uintptr_t) (PAGE_TABLE_L2 + indexL2)); // Not strictly necessary.
		EsMemoryZero((void *) ((uintptr_t) (PAGE_TABLE_L2 + indexL2) & ~(K_PAGE_SIZE - 1)), K_PAGE_SIZE);
	}

	if ((PAGE_TABLE_L2[indexL2] & 1) == 0) {
		if (flags & MM_MAP_PAGE_NO_NEW_TABLES) KernelPanic("MMArchMapPage - NO_NEW_TABLES flag set, but a table was missing.\n");
		PAGE_TABLE_L2[indexL2] = MMPhysicalAllocate(MM_PHYSICAL_ALLOCATE_LOCK_ACQUIRED) | 7;
		ProcessorInvalidatePage((uintptr_t) (PAGE_TABLE_L1 + indexL1)); // Not strictly necessary.
		EsMemoryZero((void *) ((uintptr_t) (PAGE_TABLE_L1 + indexL1) & ~(K_PAGE_SIZE - 1)), K_PAGE_SIZE);
	}

	uintptr_t oldValue = PAGE_TABLE_L1[indexL1];
	uintptr_t value = physicalAddress | 3;

	if (flags & MM_MAP_PAGE_WRITE_COMBINING) value |= 16; // This only works because we modified the PAT in SetupProcessor1.
	if (flags & MM_MAP_PAGE_NOT_CACHEABLE) value |= 24;
	if (flags & MM_MAP_PAGE_USER) value |= 7; else value |= 0x100;
	if (flags & MM_MAP_PAGE_READ_ONLY) value &= ~2;
	if (flags & MM_MAP_PAGE_COPIED) value |= 1 << 9;

	if ((oldValue & 1) && !(flags & MM_MAP_PAGE_OVERWRITE)) {
		if (flags & MM_MAP_PAGE_IGNORE_IF_MAPPED) {
			return;
		}

		if ((oldValue & ~(K_PAGE_SIZE - 1)) != physicalAddress) {
			KernelPanic("MMArchMapPage - Attempt to map %x to %x that has already been mapped to %x.\n", 
					virtualAddress, physicalAddress, oldValue & (~(K_PAGE_SIZE - 1)));
		}

		if (oldValue == value) {
			KernelPanic("MMArchMapPage - Attempt to rewrite page translation.\n", 
					physicalAddress, virtualAddress, oldValue & (K_PAGE_SIZE - 1), value & (K_PAGE_SIZE - 1));
		} else if (!(oldValue & 2) && (value & 2)) {
			// The page has become writable.
		} else {
			KernelPanic("MMArchMapPage - Attempt to change flags mapping %x address %x from %x to %x.\n", 
					physicalAddress, virtualAddress, oldValue & (K_PAGE_SIZE - 1), value & (K_PAGE_SIZE - 1));
		}
	}

	PAGE_TABLE_L1[indexL1] = value;

	// We rely on this page being invalidated on this CPU in some places.
	ProcessorInvalidatePage(oldVirtualAddress);
}

bool MMArchHandlePageFault(uintptr_t address, uint32_t flags) {
	// EsPrint("Fault %x\n", address);
	address &= ~(K_PAGE_SIZE - 1);
	bool forSupervisor = flags & MM_HANDLE_PAGE_FAULT_FOR_SUPERVISOR;

	if (!ProcessorAreInterruptsEnabled()) {
		KernelPanic("MMArchHandlePageFault - Page fault with interrupts disabled.\n");
	}

	if (address < K_PAGE_SIZE) {
	} else if (address >= LOW_MEMORY_MAP_START && address < LOW_MEMORY_MAP_START + 0x100000000 && forSupervisor) {
		// We want to access a physical page within the first 4GB.
		// This is used for device IO, so the page can't be cacheable.
		MMArchMapPage(kernelMMSpace, address - LOW_MEMORY_MAP_START, address, MM_MAP_PAGE_NOT_CACHEABLE | MM_MAP_PAGE_COMMIT_TABLES_NOW);
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

void MMArchInitialiseVAS() {
	coreMMSpace->data.cr3 = kernelMMSpace->data.cr3 = ProcessorReadCR3();
	coreMMSpace->data.l1Commit = coreL1Commit;

	for (uintptr_t i = 0x100; i < 0x200; i++) {
		if (PAGE_TABLE_L4[i] == 0) {
			// We don't need to commit anything because the PMM isn't ready yet.
			PAGE_TABLE_L4[i] = MMPhysicalAllocate(ES_FLAGS_DEFAULT) | 3; 
			EsMemoryZero((void *) (PAGE_TABLE_L3 + i * 0x200), K_PAGE_SIZE);
		}
	}
}

uintptr_t MMArchTranslateAddress(MMSpace *, uintptr_t virtualAddress, bool writeAccess) {
	// TODO I don't think this mutex was ever necessary?
	// space->data.mutex.Acquire();
	// EsDefer(space->data.mutex.Release());

	virtualAddress &= 0x0000FFFFFFFFF000;
	if ((PAGE_TABLE_L4[virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 3)] & 1) == 0) return 0;
	if ((PAGE_TABLE_L3[virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 2)] & 1) == 0) return 0;
	if ((PAGE_TABLE_L2[virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 1)] & 1) == 0) return 0;
	uintptr_t physicalAddress = PAGE_TABLE_L1[virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 0)];
	if (writeAccess && !(physicalAddress & 2)) return 0;
	return (physicalAddress & 1) ? (physicalAddress & 0x0000FFFFFFFFF000) : 0;
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

void MMArchUnmapPages(MMSpace *space, uintptr_t virtualAddressStart, uintptr_t pageCount, unsigned flags, size_t unmapMaximum, uintptr_t *resumePosition) {
	// We can't let anyone use the unmapped pages until they've been invalidated on all processors.
	// This also synchronises modified bit updating.
	KMutexAcquire(&pmm.pageFrameMutex);
	EsDefer(KMutexRelease(&pmm.pageFrameMutex));

	KMutexAcquire(&space->data.mutex);
	EsDefer(KMutexRelease(&space->data.mutex));

	uintptr_t tableBase = virtualAddressStart & 0x0000FFFFFFFFF000;
	uintptr_t start = resumePosition ? *resumePosition : 0;

	// TODO Freeing newly empty page tables.
	// 	- What do we need to invalidate when we do this?

	for (uintptr_t i = start; i < pageCount; i++) {
		// if (flags & MM_UNMAP_PAGES_BALANCE_FILE) EsPrint(",%d", i);

		uintptr_t virtualAddress = (i << K_PAGE_BITS) + tableBase;

		if ((PAGE_TABLE_L4[virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 3)] & 1) == 0) {
			i -= (virtualAddress >> K_PAGE_BITS) % (1 << (ENTRIES_PER_PAGE_TABLE_BITS * 3));
			i += (1 << (ENTRIES_PER_PAGE_TABLE_BITS * 3));
			continue;
		}

		if ((PAGE_TABLE_L3[virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 2)] & 1) == 0) {
			i -= (virtualAddress >> K_PAGE_BITS) % (1 << (ENTRIES_PER_PAGE_TABLE_BITS * 2));
			i += (1 << (ENTRIES_PER_PAGE_TABLE_BITS * 2));
			continue;
		}

		if ((PAGE_TABLE_L2[virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 1)] & 1) == 0) {
			i -= (virtualAddress >> K_PAGE_BITS) % (1 << (ENTRIES_PER_PAGE_TABLE_BITS * 1));
			i += (1 << (ENTRIES_PER_PAGE_TABLE_BITS * 1));
			continue;
		}

		uintptr_t indexL1 = virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 0);

		uintptr_t translation = PAGE_TABLE_L1[indexL1];
		if (!(translation & 1)) continue;
		bool copy = translation & (1 << 9);

		if (copy && (flags & MM_UNMAP_PAGES_BALANCE_FILE)) {
			// Ignore copied pages when balancing file mappings.
			// EsPrint("Ignore copied page %x\n", virtualAddress);
		} else {
			PAGE_TABLE_L1[indexL1] = 0;

			// NOTE MMArchInvalidatePages invalidates the page on all processors now,
			// 	which I think makes this unnecessary?
			// uint64_t invalidateAddress = (i << K_PAGE_BITS) + virtualAddressStart;
			// ProcessorInvalidatePage(invalidateAddress);

			if ((flags & MM_UNMAP_PAGES_FREE) || ((flags & MM_UNMAP_PAGES_FREE_COPIED) && copy)) {
				MMPhysicalFree(translation & 0x0000FFFFFFFFF000, true);
			} else if (flags & MM_UNMAP_PAGES_BALANCE_FILE) {
				// EsPrint("Balance %x\n", virtualAddress);

				// It's safe to do this before invalidation,
				// because the page fault handler is synchronisation with mutexes acquired above.

				if (MMUnmapFilePage((translation & 0x0000FFFFFFFFF000) >> K_PAGE_BITS)) {
					if (resumePosition) {
						if (!unmapMaximum--) {
							*resumePosition = i;
							break;
						}
					}
				}
			}
		}
	}

	MMArchInvalidatePages(virtualAddressStart, pageCount);
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

bool MMArchInitialiseUserSpace(MMSpace *space) {
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

void ArchCleanupVirtualAddressSpace(void *argument) {
	KernelLog(LOG_VERBOSE, "Arch", "remove virtual address space page", "Removing virtual address space page %x...\n", argument);
	MMPhysicalFree((uintptr_t) argument); 
	MMDecommit(K_PAGE_SIZE, true);
}

void MMFreeVAS(MMSpace *space) {
	for (uintptr_t i = 0; i < 256; i++) {
		if (!PAGE_TABLE_L4[i]) continue;

		for (uintptr_t j = i * 512; j < (i + 1) * 512; j++) {
			if (!PAGE_TABLE_L3[j]) continue;

			for (uintptr_t k = j * 512; k < (j + 1) * 512; k++) {
				if (!PAGE_TABLE_L2[k]) continue;
				MMPhysicalFree(PAGE_TABLE_L2[k] & (~0xFFF));
			}

			MMPhysicalFree(PAGE_TABLE_L3[j] & (~0xFFF));
		}

		MMPhysicalFree(PAGE_TABLE_L4[i] & (~0xFFF));
	}

	KMutexAcquire(&coreMMSpace->reserveMutex);
	MMUnreserve(coreMMSpace, MMFindRegion(coreMMSpace, (uintptr_t) space->data.l1Commit), true);
	KMutexRelease(&coreMMSpace->reserveMutex);
	MMDecommit(space->data.pageTablesCommitted * K_PAGE_SIZE, true);
}

void MMFinalizeVAS(MMSpace *space) {
	// Freeing the L4 page table has to be done in the kernel process, since it's the page CR3 currently points to!!
	// This function is called in an async task.
	MMPhysicalFree(space->data.cr3); 
	MMDecommit(K_PAGE_SIZE, true); 
}

void ArchCheckAddressInRange(int type, uintptr_t address) {
	if (type == 1) {
		if ((uintptr_t) address < 0xFFFF900000000000) {
			KernelPanic("ArchCheckAddressInRange - Address out of expected range.\n");
		}
	} else if (type == 2) {
		if ((uintptr_t) address < 0xFFFF8F8000000000 || (uintptr_t) address >= 0xFFFF900000000000) {
			KernelPanic("ArchCheckAddressInRange - Address out of expected range.\n");
		}
	} else {
		if ((uintptr_t) address >= 0xFFFF800000000000) {
			KernelPanic("ArchCheckAddressInRange - Address out of expected range.\n");
		}
	}
}

void ArchDelay1Ms() {
	ProcessorOut8(0x43, 0x30);
	ProcessorOut8(0x40, 0xA9);
	ProcessorOut8(0x40, 0x04);

	while (true) {
		ProcessorOut8(0x43, 0xE2);

		if (ProcessorIn8(0x40) & (1 << 7)) {
			break;
		}
	}
}

struct MSIHandler {
	KIRQHandler callback;
	void *context;
};

MSIHandler msiHandlers[INTERRUPT_VECTOR_MSI_COUNT];

void KUnregisterMSI(uintptr_t tag) {
	KSpinlockAcquire(&scheduler.lock);
	EsDefer(KSpinlockRelease(&scheduler.lock));
	msiHandlers[tag].callback = nullptr;
}

KMSIInformation KRegisterMSI(KIRQHandler handler, void *context, const char *cOwnerName) {
	KSpinlockAcquire(&scheduler.lock);
	EsDefer(KSpinlockRelease(&scheduler.lock));

	for (uintptr_t i = 0; i < INTERRUPT_VECTOR_MSI_COUNT; i++) {
		if (msiHandlers[i].callback) continue;
		msiHandlers[i] = { handler, context };

		// TODO Selecting the best target processor.
		// 	Currently this sends everything to processor 0.

		KernelLog(LOG_INFO, "Arch", "register MSI", "Register MSI with vector %X for '%z'.\n", 
				INTERRUPT_VECTOR_MSI_START + i, cOwnerName);

		return {
			.address = 0xFEE00000,
			.data = INTERRUPT_VECTOR_MSI_START + i,
			.tag = i,
		};
	}

	return {};
}

#define IRQ_BASE 0x50
KIRQHandler irqHandlers[0x20][0x10];
void *irqHandlerContext[0x20][0x10];
size_t usedIrqHandlers[0x20];

bool KRegisterIRQ(uintptr_t interrupt, KIRQHandler handler, void *context, const char *cOwnerName) {
	KSpinlockAcquire(&scheduler.lock);
	EsDefer(KSpinlockRelease(&scheduler.lock));

	// Work out which interrupt the IoApic will sent to the processor.
	// TODO Use the upper 4 bits for IRQ priority.
	uintptr_t thisProcessorIRQ = interrupt + IRQ_BASE;

	// Register the IRQ handler.
	if (interrupt > 0x20) KernelPanic("KRegisterIRQ - Unexpected IRQ %d\n", interrupt);
	if (usedIrqHandlers[interrupt] == 0x10) {
		// There are too many overloaded interrupts.
		return false;
	}
	irqHandlers[interrupt][usedIrqHandlers[interrupt]] = handler;
	irqHandlerContext[interrupt][usedIrqHandlers[interrupt]] = context;

	KernelLog(LOG_INFO, "Arch", "register IRQ", "KRegisterIRQ - Registering IRQ %d to '%z'.\n", 
			interrupt, cOwnerName);

	if (usedIrqHandlers[interrupt]) {
		// IRQ already registered.
		usedIrqHandlers[interrupt]++;
		return true;
	}

	usedIrqHandlers[interrupt]++;

	bool activeLow = false;
	bool levelTriggered = true;

	// If there was an interrupt override entry in the MADT table,
	// then we'll have to use that number instead.
	for (uintptr_t i = 0; i < acpi.interruptOverrideCount; i++) {
		ACPIInterruptOverride *interruptOverride = acpi.interruptOverrides + i;
		if (interruptOverride->sourceIRQ == interrupt) {
			interrupt = interruptOverride->gsiNumber;
			activeLow = interruptOverride->activeLow;
			levelTriggered = interruptOverride->levelTriggered;
			break;
		}
	}

	KernelLog(LOG_INFO, "Arch", "IRQ flags", "KRegisterIRQ - IRQ %d is active %z, %z triggered.\n",
			interrupt, activeLow ? "low" : "high", levelTriggered ? "level" : "edge");

	ACPIIoApic *ioApic;
	bool foundIoApic = false;

	// Look for the IoApic to which this interrupt is sent.
	for (uintptr_t i = 0; i < acpi.ioapicCount; i++) {
		ioApic = acpi.ioApics + i;
		if (interrupt >= ioApic->gsiBase 
				&& interrupt < (ioApic->gsiBase + (0xFF & (ioApic->ReadRegister(1) >> 16)))) {
			foundIoApic = true;
			interrupt -= ioApic->gsiBase;
			break;
		}
	}

	// We couldn't find the IoApic that handles this interrupt.
	if (!foundIoApic) {
		return false;
	}

	// A normal priority interrupt.
	uintptr_t redirectionTableIndex = interrupt * 2 + 0x10;
	uint32_t redirectionEntry = thisProcessorIRQ;
	if (activeLow) redirectionEntry |= (1 << 13);
	if (levelTriggered) redirectionEntry |= (1 << 15);

	// Mask the interrupt while we modify the entry.
	ioApic->WriteRegister(redirectionTableIndex, 1 << 16);

	// Send the interrupt to the processor that registered the interrupt.
	ioApic->WriteRegister(redirectionTableIndex + 1, GetLocalStorage()->archCPU->apicID << 24); 
	ioApic->WriteRegister(redirectionTableIndex, redirectionEntry);

	return true;
}

size_t ProcessorSendIPI(uintptr_t interrupt, bool nmi, int processorID) {
	// It's possible that another CPU is trying to send an IPI at the same time we want to send the panic IPI.
	// TODO What should we do in this case?
	if (interrupt != KERNEL_PANIC_IPI) KSpinlockAssertLocked(&ipiLock);

	// Note: We send IPIs at a special priority that ProcessorDisableInterrupts doesn't mask.

	size_t ignored = 0;

	for (uintptr_t i = 0; i < acpi.processorCount; i++) {
		ACPIProcessor *processor = acpi.processors + i;

		if (processorID != -1) {
			if (processorID != processor->kernelProcessorID) {
				ignored++;
				continue;
			}
		} else {
			if (processor == GetLocalStorage()->archCPU || !processor->local || !processor->local->schedulerReady) {
				ignored++;
				continue;
			}
		}

		uint32_t destination = acpi.processors[i].apicID << 24;
		uint32_t command = interrupt | (1 << 14) | (nmi ? 0x400 : 0);
		acpi.lapic.WriteRegister(0x310 >> 2, destination);
		acpi.lapic.WriteRegister(0x300 >> 2, command); 

		// Wait for the interrupt to be sent.
		while (acpi.lapic.ReadRegister(0x300 >> 2) & (1 << 12));
	}

	return ignored;
}

void ArchNextTimer(size_t ms) {
	while (!scheduler.started); 			// Wait until the scheduler is ready.
	GetLocalStorage()->schedulerReady = true; 	// Make sure this CPU can be scheduled.
	acpi.lapic.ArchNextTimer(ms); 			// Set the next timer.
}

NewProcessorStorage AllocateNewProcessorStorage(ACPIProcessor *archCPU) {
	NewProcessorStorage storage = {};
	storage.local = (CPULocalStorage *) EsHeapAllocate(sizeof(CPULocalStorage), true, K_FIXED);
	storage.gdt = (uint32_t *) MMMapPhysical(kernelMMSpace, MMPhysicalAllocate(MM_PHYSICAL_ALLOCATE_COMMIT_NOW), K_PAGE_SIZE, ES_FLAGS_DEFAULT);
	storage.local->archCPU = archCPU;
	archCPU->local = storage.local;
	scheduler.CreateProcessorThreads(storage.local);
	archCPU->kernelProcessorID = storage.local->processorID;
	return storage;
}

extern "C" void SetupProcessor2(NewProcessorStorage *storage) {
	// Setup the local interrupts for the current processor.
		
	for (uintptr_t i = 0; i < acpi.lapicNMICount; i++) {
		if (acpi.lapicNMIs[i].processor == 0xFF
				|| acpi.lapicNMIs[i].processor == storage->local->archCPU->processorID) {
			uint32_t registerIndex = (0x350 + (acpi.lapicNMIs[i].lintIndex << 4)) >> 2;
			uint32_t value = 2 | (1 << 10); // NMI exception interrupt vector.
			if (acpi.lapicNMIs[i].activeLow) value |= 1 << 13;
			if (acpi.lapicNMIs[i].levelTriggered) value |= 1 << 15;
			acpi.lapic.WriteRegister(registerIndex, value);
		}
	}

	acpi.lapic.WriteRegister(0x350 >> 2, acpi.lapic.ReadRegister(0x350 >> 2) & ~(1 << 16));
	acpi.lapic.WriteRegister(0x360 >> 2, acpi.lapic.ReadRegister(0x360 >> 2) & ~(1 << 16));
	acpi.lapic.WriteRegister(0x080 >> 2, 0);
	if (acpi.lapic.ReadRegister(0x30 >> 2) & 0x80000000) acpi.lapic.WriteRegister(0x410 >> 2, 0);
	acpi.lapic.EndOfInterrupt();

	// Configure the LAPIC's timer.

	acpi.lapic.WriteRegister(0x3E0 >> 2, 2); // Divisor = 16

	// Create the processor's local storage.

	ProcessorSetLocalStorage(storage->local);

	// Setup a GDT and TSS for the processor.

	uint32_t *gdt = storage->gdt;
	void *bootstrapGDT = (void *) (((uint64_t *) ((uint16_t *) processorGDTR + 1))[0]);
	EsMemoryCopy(gdt, bootstrapGDT, 2048);
	uint32_t *tss = (uint32_t *) ((uint8_t *) storage->gdt + 2048);
	storage->local->archCPU->kernelStack = (void **) (tss + 1);
	ProcessorInstallTSS(gdt, tss);
}

const char *exceptionInformation[] = {
	"0x00: Divide Error (Fault)",
	"0x01: Debug Exception (Fault/Trap)",
	"0x02: Non-Maskable External Interrupt (Interrupt)",
	"0x03: Breakpoint (Trap)",
	"0x04: Overflow (Trap)",
	"0x05: BOUND Range Exceeded (Fault)",
	"0x06: Invalid Opcode (Fault)",
	"0x07: x87 Coprocessor Unavailable (Fault)",
	"0x08: Double Fault (Abort)",
	"0x09: x87 Coprocessor Segment Overrun (Fault)",
	"0x0A: Invalid TSS (Fault)",
	"0x0B: Segment Not Present (Fault)",
	"0x0C: Stack Protection (Fault)",
	"0x0D: General Protection (Fault)",
	"0x0E: Page Fault (Fault)",
	"0x0F: Reserved/Unknown",
	"0x10: x87 FPU Floating-Point Error (Fault)",
	"0x11: Alignment Check (Fault)",
	"0x12: Machine Check (Abort)",
	"0x13: SIMD Floating-Point Exception (Fault)",
	"0x14: Virtualization Exception (Fault)",
	"0x15: Reserved/Unknown",
	"0x16: Reserved/Unknown",
	"0x17: Reserved/Unknown",
	"0x18: Reserved/Unknown",
	"0x19: Reserved/Unknown",
	"0x1A: Reserved/Unknown",
	"0x1B: Reserved/Unknown",
	"0x1C: Reserved/Unknown",
	"0x1D: Reserved/Unknown",
	"0x1E: Reserved/Unknown",
	"0x1F: Reserved/Unknown",
};

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
			GetLocalStorage()->panicContext = context;
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

#ifndef __OPTIMIZE__
			EsPrint("Attempting to make a stack trace...\n");

			{
				// TODO Temporary, may crash kernel.
				// Attempt to make a stack trace.
			
				uint64_t *rbp = (uint64_t *) context->rbp;
				int i = 0;

				while (rbp && i < 32) {
					EsPrint("\t%d: %x\n", ++i, rbp[1]);
					if (!rbp[1]) break;
					rbp = (uint64_t *) (rbp[0]);
				}
			}

			EsPrint("Stack trace complete.\n");
#else
			EsPrint("Disable optimisations to get a stack trace.\n");
#endif

			EsCrashReason crashReason;
			EsMemoryZero(&crashReason, sizeof(EsCrashReason));
			crashReason.errorCode = ES_FATAL_ERROR_PROCESSOR_EXCEPTION;
			crashReason.duringSystemCall = (EsSyscallType) -1;
			scheduler.CrashProcess(currentThread->process, &crashReason);

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

				{
					CPULocalStorage *storage = GetLocalStorage();

					if (storage && storage->spinlockCount && ((context->cr2 >= 0xFFFF900000000000 && context->cr2 < 0xFFFFF00000000000) 
								|| context->cr2 < 0x8000000000000000)) {
						KernelPanic("HandlePageFault - Page fault occurred in critical section at %x (S = %x, B = %x, LG = %x) (CR2 = %x).\n", 
								context->rip, context->rsp, context->rbp, storage->currentThread->lastKnownExecutionAddress, context->cr2);
					}
				}

				
				if (!MMArchHandlePageFault(context->cr2, MM_HANDLE_PAGE_FAULT_FOR_SUPERVISOR
							| ((context->errorCode & 2) ? MM_HANDLE_PAGE_FAULT_WRITE : 0))) {
					goto fault;
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

		acpi.lapic.EndOfInterrupt();
	} else if (interrupt >= INTERRUPT_VECTOR_MSI_START && interrupt < INTERRUPT_VECTOR_MSI_START + INTERRUPT_VECTOR_MSI_COUNT && local) {
		MSIHandler handler = msiHandlers[interrupt - INTERRUPT_VECTOR_MSI_START];
		local->irqSwitchThread = false;

		if (!handler.callback) {
			KernelLog(LOG_ERROR, "Arch", "unexpected MSI", "Unexpected MSI vector %X (no handler).\n", interrupt);
		} else {
			handler.callback(interrupt - INTERRUPT_VECTOR_MSI_START, handler.context);
		}


		acpi.lapic.EndOfInterrupt();

		if (local->irqSwitchThread && scheduler.started && local->schedulerReady) {
			scheduler.Yield(context);
			KernelPanic("InterruptHandler - Returned from Scheduler::Yield.\n");
		}
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

			size_t overloads = usedIrqHandlers[interrupt - IRQ_BASE];
			bool handledInterrupt = false;

			KernelLog(LOG_VERBOSE, "Arch", "IRQ start", "IRQ start %d.\n", interrupt - IRQ_BASE);

			for (uintptr_t i = 0; i < overloads; i++) {
				KIRQHandler handler = irqHandlers[interrupt - IRQ_BASE][i];

				if (handler(interrupt - IRQ_BASE, irqHandlerContext[interrupt - IRQ_BASE][i])) {
					handledInterrupt = true;
				}
			}

			KernelLog(LOG_VERBOSE, "Arch", "IRQ end", "IRQ end %d.\n", interrupt - IRQ_BASE);

			bool rejectedByAll = !handledInterrupt;

			if (rejectedByAll) {
				// TODO Now what?
				// KernelLog(LOG_ERROR, "Arch", "unhandled IRQ", 
				// 		"InterruptHandler - Unhandled IRQ %d, rejected by %d %z\n", 
				// 		interrupt, overloads, (overloads != 1) ? "overloads" : "overload");
			}

			GetLocalStorage()->inIRQ = false;
		}

		acpi.lapic.EndOfInterrupt();

		if (local->irqSwitchThread && scheduler.started && local->schedulerReady) {
			scheduler.Yield(context);
			KernelPanic("InterruptHandler - Returned from Scheduler::Yield.\n");
		}
	}

	// Sanity check.
	ContextSanityCheck(context);

	if (ProcessorAreInterruptsEnabled()) {
		KernelPanic("InterruptHandler - Interrupts were enabled while returning from an interrupt handler.\n");
	}
}

extern "C" bool PostContextSwitch(InterruptContext *context) {
	CPULocalStorage *local = GetLocalStorage();
	Thread *currentThread = GetCurrentThread();

	void *kernelStack = (void *) currentThread->kernelStack;
	*local->archCPU->kernelStack = kernelStack;

	bool newThread = currentThread->cpuTimeSlices == 1;

	KernelLog(LOG_VERBOSE, "Arch", "context switch", "Context switch to thread %x at %x\n", currentThread, context->rip);

	if (newThread) {
		ContextSanityCheck(context);
		KernelLog(LOG_VERBOSE, "Arch", "executing new thread", "Executing new thread %x at %x\n", currentThread, context->rip);
	}

	acpi.lapic.EndOfInterrupt();
	ContextSanityCheck(context);

	if (ProcessorAreInterruptsEnabled()) {
		KernelPanic("PostContextSwitch - Interrupts were enabled. (1)\n");
	}

	currentThread->lastKnownExecutionAddress = context->rip;

	if (scheduler.lock.interruptsEnabled) {
		KernelPanic("PostContextSwitch - Interrupts were enabled. (3)\n");
	}

	ProcessorSetThreadStorage(currentThread->tlsAddress);

	// We can only free the scheduler's spinlock when we are no longer using the stack
	// from the previous thread. See DoContextSwitch in x86_64.s.
	KSpinlockRelease(&scheduler.lock, true);

	if (ProcessorAreInterruptsEnabled()) {
		KernelPanic("PostContextSwitch - Interrupts were enabled. (2)\n");
	}

	return newThread;
}

extern "C" uintptr_t Syscall(uintptr_t argument0, uintptr_t argument1, uintptr_t argument2, 
		uintptr_t returnAddress, uintptr_t argument3, uintptr_t argument4, uintptr_t *userStackPointer) {
	(void) returnAddress;
	return DoSyscall((EsSyscallType) argument0, argument1, argument2, argument3, argument4, false, nullptr, userStackPointer);
}

bool HasSSSE3Support() {
	return simdSSSE3Support;
}

uintptr_t GetBootloaderInformationOffset() {
	return bootloaderInformationOffset;
}

#endif
