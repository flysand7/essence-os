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
#define LOW_MEMORY_MAP_START (0xFFFFFE0000000000)
#define LOW_MEMORY_LIMIT (0x100000000) // The first 4GB is mapped here.

struct MSIHandler {
	KIRQHandler callback;
	void *context;
};

struct IRQHandler {
	KIRQHandler callback;
	void *context;
	intptr_t line;
	KPCIDevice *pciDevice;
	const char *cOwnerName;
};

extern PhysicalMemoryRegion *physicalMemoryRegions;
extern size_t physicalMemoryRegionsCount;
extern size_t physicalMemoryRegionsPagesCount;
extern size_t physicalMemoryOriginalPagesCount;
extern size_t physicalMemoryRegionsIndex;
extern uintptr_t physicalMemoryHighest;

uint8_t pciIRQLines[0x100 /* slots */][4 /* pins */];

MSIHandler msiHandlers[INTERRUPT_VECTOR_MSI_COUNT];
IRQHandler irqHandlers[0x40];
KSpinlock irqHandlersLock; // Also for msiHandlers.

KSpinlock ipiLock;

uint8_t coreL1Commit[(0xFFFF800200000000 - 0xFFFF800100000000) >> (/* ENTRIES_PER_PAGE_TABLE_BITS */ 9 + K_PAGE_BITS + 3)];

volatile uintptr_t tlbShootdownVirtualAddress;
volatile size_t tlbShootdownPageCount;

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

uint32_t LapicReadRegister(uint32_t reg) {
	return acpi.lapicAddress[reg];
}

void LapicWriteRegister(uint32_t reg, uint32_t value) {
	acpi.lapicAddress[reg] = value;
}

void LapicNextTimer(size_t ms) {
	LapicWriteRegister(0x320 >> 2, TIMER_INTERRUPT | (1 << 17)); 
	LapicWriteRegister(0x380 >> 2, acpi.lapicTicksPerMs * ms); 
}

void LapicEndOfInterrupt() {
	LapicWriteRegister(0xB0 >> 2, 0);
}

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

bool MMArchMapPage(MMSpace *space, uintptr_t physicalAddress, uintptr_t virtualAddress, unsigned flags) {
	// TODO Use the no-execute bit.

	if (physicalAddress & (K_PAGE_SIZE - 1)) {
		KernelPanic("MMArchMapPage - Physical address not page aligned.\n");
	}

	if (pmm.pageFrames && (physicalAddress >> K_PAGE_BITS) < pmm.pageFrameDatabaseCount) {
		if (pmm.pageFrames[physicalAddress >> K_PAGE_BITS].state != MMPageFrame::ACTIVE
				&& pmm.pageFrames[physicalAddress >> K_PAGE_BITS].state != MMPageFrame::UNUSABLE) {
			KernelPanic("MMArchMapPage - Physical page frame %x not marked as ACTIVE or UNUSABLE.\n", physicalAddress);
		}
	}

	bool acquireFrameLock = !(flags & (MM_MAP_PAGE_NO_NEW_TABLES | MM_MAP_PAGE_FRAME_LOCK_ACQUIRED));
	if (acquireFrameLock) KMutexAcquire(&pmm.pageFrameMutex);
	EsDefer(if (acquireFrameLock) KMutexRelease(&pmm.pageFrameMutex););

	bool acquireSpaceLock = ~flags & MM_MAP_PAGE_NO_NEW_TABLES;
	if (acquireSpaceLock) KMutexAcquire(&space->data.mutex);
	EsDefer(if (acquireSpaceLock) KMutexRelease(&space->data.mutex));

	uintptr_t cr3 = space->data.cr3;

	if (virtualAddress < 0xFFFF800000000000 && ProcessorReadCR3() != cr3) {
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
		space->data.pageTablesActive++;
	}

	if ((PAGE_TABLE_L3[indexL3] & 1) == 0) {
		if (flags & MM_MAP_PAGE_NO_NEW_TABLES) KernelPanic("MMArchMapPage - NO_NEW_TABLES flag set, but a table was missing.\n");
		PAGE_TABLE_L3[indexL3] = MMPhysicalAllocate(MM_PHYSICAL_ALLOCATE_LOCK_ACQUIRED) | 7;
		ProcessorInvalidatePage((uintptr_t) (PAGE_TABLE_L2 + indexL2)); // Not strictly necessary.
		EsMemoryZero((void *) ((uintptr_t) (PAGE_TABLE_L2 + indexL2) & ~(K_PAGE_SIZE - 1)), K_PAGE_SIZE);
		space->data.pageTablesActive++;
	}

	if ((PAGE_TABLE_L2[indexL2] & 1) == 0) {
		if (flags & MM_MAP_PAGE_NO_NEW_TABLES) KernelPanic("MMArchMapPage - NO_NEW_TABLES flag set, but a table was missing.\n");
		PAGE_TABLE_L2[indexL2] = MMPhysicalAllocate(MM_PHYSICAL_ALLOCATE_LOCK_ACQUIRED) | 7;
		ProcessorInvalidatePage((uintptr_t) (PAGE_TABLE_L1 + indexL1)); // Not strictly necessary.
		EsMemoryZero((void *) ((uintptr_t) (PAGE_TABLE_L1 + indexL1) & ~(K_PAGE_SIZE - 1)), K_PAGE_SIZE);
		space->data.pageTablesActive++;
	}

	uintptr_t oldValue = PAGE_TABLE_L1[indexL1];
	uintptr_t value = physicalAddress | 3;

	if (flags & MM_MAP_PAGE_WRITE_COMBINING) value |= 16; // This only works because we modified the PAT in SetupProcessor1.
	if (flags & MM_MAP_PAGE_NOT_CACHEABLE) value |= 24;
	if (flags & MM_MAP_PAGE_USER) value |= 7;
	else value |= 1 << 8; // Global.
	if (flags & MM_MAP_PAGE_READ_ONLY) value &= ~2;
	if (flags & MM_MAP_PAGE_COPIED) value |= 1 << 9; // Ignored by the CPU.

	// When the CPU accesses or writes to a page, 
	// it will modify the table entry to set the accessed or dirty bits respectively,
	// but it uses its TLB entry as the assumed previous value of the entry.
	// When unmapping pages we can't atomically remove an entry and do the TLB shootdown.
	// This creates a race condition:
	// 1. CPU 0 maps a page table entry. The dirty bit is not set.
	// 2. CPU 1 reads from the page. A TLB entry is created with the dirty bit not set.
	// 3. CPU 0 unmaps the entry.
	// 4. CPU 1 writes to the page. As the TLB entry has the dirty bit cleared, it sets the entry to its cached entry ORed with the dirty bit.
	// 5. CPU 0 invalidates the entry.
	// That is, CPU 1 didn't realize the page was unmapped when it wrote out its entry, so the page becomes mapped again.
	// To prevent this, we mark all pages with the dirty and accessed bits when we initially map them.
	// (We don't use these bits for anything, anyway. They're basically useless on SMP systems, as far as I can tell.)
	// That said, a CPU won't overwrite and clear a dirty bit when writing out its accessed flag (tested on Qemu);
	// see here https://stackoverflow.com/questions/69024372/.
	// Tl;dr: if a CPU ever sees an entry without these bits set, it can overwrite the entry with junk whenever it feels like it.
	value |= (1 << 5) | (1 << 6);

	if ((oldValue & 1) && !(flags & MM_MAP_PAGE_OVERWRITE)) {
		if (flags & MM_MAP_PAGE_IGNORE_IF_MAPPED) {
			return false;
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

void MMArchInitialise() {
	coreMMSpace->data.cr3 = kernelMMSpace->data.cr3 = ProcessorReadCR3();
	coreMMSpace->data.l1Commit = coreL1Commit;

	mmCoreRegions[0].baseAddress = MM_CORE_SPACE_START;
	mmCoreRegions[0].pageCount = MM_CORE_SPACE_SIZE / K_PAGE_SIZE;

	for (uintptr_t i = 0x100; i < 0x200; i++) {
		if (PAGE_TABLE_L4[i] == 0) {
			// We don't need to commit anything because the PMM isn't ready yet.
			PAGE_TABLE_L4[i] = MMPhysicalAllocate(ES_FLAGS_DEFAULT) | 3; 
			EsMemoryZero((void *) (PAGE_TABLE_L3 + i * 0x200), K_PAGE_SIZE);
		}
	}

	KMutexAcquire(&coreMMSpace->reserveMutex);
	kernelMMSpace->data.l1Commit = (uint8_t *) MMReserve(coreMMSpace, L1_COMMIT_SIZE_BYTES, MM_REGION_NORMAL | MM_REGION_NO_COMMIT_TRACKING | MM_REGION_FIXED)->baseAddress;
	KMutexRelease(&coreMMSpace->reserveMutex);
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

		if (!(translation & 1)) {
			// The page wasn't mapped.
			continue;
		}

		bool copy = translation & (1 << 9);

		if (copy && (flags & MM_UNMAP_PAGES_BALANCE_FILE) && (~flags & MM_UNMAP_PAGES_FREE_COPIED)) {
			// Ignore copied pages when balancing file mappings.
			continue;
		}

		if ((~translation & (1 << 5)) || (~translation & (1 << 6))) {
			// See MMArchMapPage for a discussion of why these bits must be set.
			KernelPanic("MMArchUnmapPages - Page found without accessed or dirty bit set (virtualAddress: %x, translation: %x).\n", 
					virtualAddress, translation);
		}

		PAGE_TABLE_L1[indexL1] = 0;

		uintptr_t physicalAddress = translation & 0x0000FFFFFFFFF000;

		if ((flags & MM_UNMAP_PAGES_FREE) || ((flags & MM_UNMAP_PAGES_FREE_COPIED) && copy)) {
			MMPhysicalFree(physicalAddress, true);
		} else if (flags & MM_UNMAP_PAGES_BALANCE_FILE) {
			// It's safe to do this before page invalidation,
			// because the page fault handler is synchronised with the same mutexes acquired above.

			if (MMUnmapFilePage(physicalAddress >> K_PAGE_BITS)) {
				if (resumePosition) {
					if (!unmapMaximum--) {
						*resumePosition = i;
						break;
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

		for (uintptr_t j = i * 512; j < (i + 1) * 512; j++) {
			if (!PAGE_TABLE_L3[j]) continue;

			for (uintptr_t k = j * 512; k < (j + 1) * 512; k++) {
				if (!PAGE_TABLE_L2[k]) continue;
				MMPhysicalFree(PAGE_TABLE_L2[k] & (~0xFFF));
				space->data.pageTablesActive--;
			}

			MMPhysicalFree(PAGE_TABLE_L3[j] & (~0xFFF));
			space->data.pageTablesActive--;
		}

		MMPhysicalFree(PAGE_TABLE_L4[i] & (~0xFFF));
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

uint64_t ArchGetTimeFromPITMs() {
	// TODO This isn't working on real hardware, but ArchDelay1Ms is?

	// NOTE This will only work if called at least once every 50 ms.
	// 	(The PIT only stores a 16-bit counter, which is depleted every 50 ms.)

	static bool started = false;
	static uint64_t cumulative = 0, last = 0;

	if (!started) {
		ProcessorOut8(IO_PIT_COMMAND, 0x30);
		ProcessorOut8(IO_PIT_DATA, 0xFF);
		ProcessorOut8(IO_PIT_DATA, 0xFF);
		started = true;
		last = 0xFFFF;
		return 0;
	} else {
		ProcessorOut8(IO_PIT_COMMAND, 0x00);
		uint16_t x = ProcessorIn8(IO_PIT_DATA);
		x |= (ProcessorIn8(IO_PIT_DATA)) << 8;
		cumulative += last - x;
		if (x > last) cumulative += 0x10000;
		last = x;
		return cumulative * 1000 / 1193182;
	}
}

void ArchDelay1Ms() {
	ProcessorOut8(IO_PIT_COMMAND, 0x30);
	ProcessorOut8(IO_PIT_DATA, 0xA9);
	ProcessorOut8(IO_PIT_DATA, 0x04);

	while (true) {
		ProcessorOut8(IO_PIT_COMMAND, 0xE2);

		if (ProcessorIn8(IO_PIT_DATA) & (1 << 7)) {
			break;
		}
	}
}

bool SetupInterruptRedirectionEntry(uintptr_t _line) {
	KSpinlockAssertLocked(&scheduler.lock);

	static uint32_t alreadySetup = 0;

	if (alreadySetup & (1 << _line)) {
		return true;
	}

	// Work out which interrupt the IoApic will sent to the processor.
	// TODO Use the upper 4 bits for IRQ priority.

	uintptr_t line = _line;
	uintptr_t thisProcessorIRQ = line + IRQ_BASE;

	bool activeLow = false;
	bool levelTriggered = true;

	// If there was an interrupt override entry in the MADT table,
	// then we'll have to use that number instead.

	for (uintptr_t i = 0; i < acpi.interruptOverrideCount; i++) {
		ACPIInterruptOverride *interruptOverride = acpi.interruptOverrides + i;

		if (interruptOverride->sourceIRQ == line) {
			line = interruptOverride->gsiNumber;
			activeLow = interruptOverride->activeLow;
			levelTriggered = interruptOverride->levelTriggered;
			break;
		}
	}

	KernelLog(LOG_INFO, "Arch", "IRQ flags", "SetupInterruptRedirectionEntry - IRQ %d is active %z, %z triggered.\n",
			line, activeLow ? "low" : "high", levelTriggered ? "level" : "edge");

	ACPIIoApic *ioApic;
	bool foundIoApic = false;

	// Look for the IoApic to which this interrupt is sent.

	for (uintptr_t i = 0; i < acpi.ioapicCount; i++) {
		ioApic = acpi.ioApics + i;
		if (line >= ioApic->gsiBase && line < (ioApic->gsiBase + (0xFF & (ACPIIoApicReadRegister(ioApic, 1) >> 16)))) {
			foundIoApic = true;
			line -= ioApic->gsiBase;
			break;
		}
	}

	// We couldn't find the IoApic that handles this interrupt.

	if (!foundIoApic) {
		KernelLog(LOG_ERROR, "Arch", "no IOAPIC", "SetupInterruptRedirectionEntry - Could not find an IOAPIC handling interrupt line %d.\n", line);
		return false;
	}

	// A normal priority interrupt.

	uintptr_t redirectionTableIndex = line * 2 + 0x10;
	uint32_t redirectionEntry = thisProcessorIRQ;
	if (activeLow) redirectionEntry |= (1 << 13);
	if (levelTriggered) redirectionEntry |= (1 << 15);

	// Send the interrupt to the processor that registered the interrupt.

	ACPIIoApicWriteRegister(ioApic, redirectionTableIndex, 1 << 16); // Mask the interrupt while we modify the entry.
	ACPIIoApicWriteRegister(ioApic, redirectionTableIndex + 1, GetLocalStorage()->archCPU->apicID << 24); 
	ACPIIoApicWriteRegister(ioApic, redirectionTableIndex, redirectionEntry);

	alreadySetup |= 1 << _line;
	return true;
}

void KUnregisterMSI(uintptr_t tag) {
	KSpinlockAcquire(&irqHandlersLock);
	EsDefer(KSpinlockRelease(&irqHandlersLock));
	msiHandlers[tag].callback = nullptr;
}

KMSIInformation KRegisterMSI(KIRQHandler handler, void *context, const char *cOwnerName) {
	KSpinlockAcquire(&irqHandlersLock);
	EsDefer(KSpinlockRelease(&irqHandlersLock));

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

bool KRegisterIRQ(intptr_t line, KIRQHandler handler, void *context, const char *cOwnerName, KPCIDevice *pciDevice) {
	KSpinlockAcquire(&scheduler.lock);
	EsDefer(KSpinlockRelease(&scheduler.lock));

	if (line == -1 && !pciDevice) {
		KernelPanic("KRegisterIRQ - Interrupt line is %d, and pciDevice is %x.\n", line, pciDevice);
	}

	// Save the handler callback and context.

	if (line > 0x20 || line < -1) KernelPanic("KRegisterIRQ - Unexpected IRQ %d\n", line);
	bool found = false;

	KSpinlockAcquire(&irqHandlersLock);

	for (uintptr_t i = 0; i < sizeof(irqHandlers) / sizeof(irqHandlers[0]); i++) {
		if (!irqHandlers[i].callback) {
			found = true;
			irqHandlers[i].callback = handler;
			irqHandlers[i].context = context;
			irqHandlers[i].line = line;
			irqHandlers[i].pciDevice = pciDevice;
			irqHandlers[i].cOwnerName = cOwnerName;
			break;
		}
	}

	KSpinlockRelease(&irqHandlersLock);

	if (!found) {
		KernelLog(LOG_ERROR, "Arch", "too many IRQ handlers", "The limit of IRQ handlers was reached (%d), and the handler for '%z' was not registered.\n",
				sizeof(irqHandlers) / sizeof(irqHandlers[0]), cOwnerName);
		return false;
	}

	KernelLog(LOG_INFO, "Arch", "register IRQ", "KRegisterIRQ - Registered IRQ %d to '%z'.\n", line, cOwnerName);

	if (line != -1) {
		if (!SetupInterruptRedirectionEntry(line)) {
			return false;
		}
	} else {
		SetupInterruptRedirectionEntry(9);
		SetupInterruptRedirectionEntry(10);
		SetupInterruptRedirectionEntry(11);
	}

	return true;
}

size_t ProcessorSendIPI(uintptr_t interrupt, bool nmi, int processorID) {
	// It's possible that another CPU is trying to send an IPI at the same time we want to send the panic IPI.
	// TODO What should we do in this case?
	if (interrupt != KERNEL_PANIC_IPI) KSpinlockAssertLocked(&ipiLock);

	// Note: We send IPIs at a special priority that ProcessorDisableInterrupts doesn't mask.

	size_t ignored = 0;

	for (uintptr_t i = 0; i < acpi.processorCount; i++) {
		ArchCPU *processor = acpi.processors + i;

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
		LapicWriteRegister(0x310 >> 2, destination);
		LapicWriteRegister(0x300 >> 2, command); 

		// Wait for the interrupt to be sent.
		while (LapicReadRegister(0x300 >> 2) & (1 << 12));
	}

	return ignored;
}

void ProcessorSendYieldIPI(Thread *thread) {
	thread->receivedYieldIPI = false;
	KSpinlockAcquire(&ipiLock);
	ProcessorSendIPI(YIELD_IPI, false);
	KSpinlockRelease(&ipiLock);
	while (!thread->receivedYieldIPI); // Spin until the thread gets the IPI.
}

void ArchNextTimer(size_t ms) {
	while (!scheduler.started);               // Wait until the scheduler is ready.
	GetLocalStorage()->schedulerReady = true; // Make sure this CPU can be scheduled.
	LapicNextTimer(ms);                       // Set the next timer.
}

NewProcessorStorage AllocateNewProcessorStorage(ArchCPU *archCPU) {
	NewProcessorStorage storage = {};
	storage.local = (CPULocalStorage *) EsHeapAllocate(sizeof(CPULocalStorage), true, K_FIXED);
	storage.gdt = (uint32_t *) MMMapPhysical(kernelMMSpace, MMPhysicalAllocate(MM_PHYSICAL_ALLOCATE_COMMIT_NOW), K_PAGE_SIZE, ES_FLAGS_DEFAULT);
	storage.local->archCPU = archCPU;
	archCPU->local = storage.local;
	scheduler.CreateProcessorThreads(storage.local);
	archCPU->kernelProcessorID = storage.local->processorID;
	return storage;
}

void SetupProcessor2(NewProcessorStorage *storage) {
	// Setup the local interrupts for the current processor.
		
	for (uintptr_t i = 0; i < acpi.lapicNMICount; i++) {
		if (acpi.lapicNMIs[i].processor == 0xFF
				|| acpi.lapicNMIs[i].processor == storage->local->archCPU->processorID) {
			uint32_t registerIndex = (0x350 + (acpi.lapicNMIs[i].lintIndex << 4)) >> 2;
			uint32_t value = 2 | (1 << 10); // NMI exception interrupt vector.
			if (acpi.lapicNMIs[i].activeLow) value |= 1 << 13;
			if (acpi.lapicNMIs[i].levelTriggered) value |= 1 << 15;
			LapicWriteRegister(registerIndex, value);
		}
	}

	LapicWriteRegister(0x350 >> 2, LapicReadRegister(0x350 >> 2) & ~(1 << 16));
	LapicWriteRegister(0x360 >> 2, LapicReadRegister(0x360 >> 2) & ~(1 << 16));
	LapicWriteRegister(0x080 >> 2, 0);
	if (LapicReadRegister(0x30 >> 2) & 0x80000000) LapicWriteRegister(0x410 >> 2, 0);
	LapicEndOfInterrupt();

	// Configure the LAPIC's timer.

	LapicWriteRegister(0x3E0 >> 2, 2); // Divisor = 16

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

				if (local && local->spinlockCount && ((context->cr2 >= 0xFFFF900000000000 && context->cr2 < 0xFFFFF00000000000) 
							|| context->cr2 < 0x8000000000000000)) {
					KernelPanic("HandlePageFault - Page fault occurred in critical section at %x (S = %x, B = %x, LG = %x) (CR2 = %x).\n", 
							context->rip, context->rsp, context->rbp, local->currentThread->lastKnownExecutionAddress, context->cr2);
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

		LapicEndOfInterrupt();

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

		LapicEndOfInterrupt();

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

extern "C" bool PostContextSwitch(InterruptContext *context, MMSpace *oldAddressSpace) {
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

	LapicEndOfInterrupt();
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
	// (Another CPU can KillThread this once it's back in activeThreads.)
	KSpinlockRelease(&scheduler.lock, true);

	if (ProcessorAreInterruptsEnabled()) {
		KernelPanic("PostContextSwitch - Interrupts were enabled. (2)\n");
	}

	MMSpaceCloseReference(oldAddressSpace);

	return newThread;
}

extern "C" uintptr_t Syscall(uintptr_t argument0, uintptr_t argument1, uintptr_t argument2, 
		uintptr_t returnAddress, uintptr_t argument3, uintptr_t argument4, uintptr_t *userStackPointer) {
	(void) returnAddress;
	return DoSyscall((EsSyscallType) argument0, argument1, argument2, argument3, argument4, false, nullptr, userStackPointer);
}

uintptr_t GetBootloaderInformationOffset() {
	return bootloaderInformationOffset;
}

// Spinlock since some drivers need to access it in IRQs (e.g. ACPICA).
static KSpinlock pciConfigSpinlock; 

uint32_t KPCIReadConfig(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, int size) {
	KSpinlockAcquire(&pciConfigSpinlock);
	EsDefer(KSpinlockRelease(&pciConfigSpinlock));
	if (offset & 3) KernelPanic("KPCIReadConfig - offset is not 4-byte aligned.");
	ProcessorOut32(IO_PCI_CONFIG, (uint32_t) (0x80000000 | (bus << 16) | (device << 11) | (function << 8) | offset));
	if (size == 8) return ProcessorIn8(IO_PCI_DATA);
	if (size == 16) return ProcessorIn16(IO_PCI_DATA);
	if (size == 32) return ProcessorIn32(IO_PCI_DATA);
	KernelPanic("PCIController::ReadConfig - Invalid size %d.\n", size);
	return 0;
}

void KPCIWriteConfig(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value, int size) {
	KSpinlockAcquire(&pciConfigSpinlock);
	EsDefer(KSpinlockRelease(&pciConfigSpinlock));
	if (offset & 3) KernelPanic("KPCIWriteConfig - offset is not 4-byte aligned.");
	ProcessorOut32(IO_PCI_CONFIG, (uint32_t) (0x80000000 | (bus << 16) | (device << 11) | (function << 8) | offset));
	if (size == 8) ProcessorOut8(IO_PCI_DATA, value);
	else if (size == 16) ProcessorOut16(IO_PCI_DATA, value);
	else if (size == 32) ProcessorOut32(IO_PCI_DATA, value);
	else KernelPanic("PCIController::WriteConfig - Invalid size %d.\n", size);
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

uintptr_t MMArchEarlyAllocatePage() {
	uintptr_t i = physicalMemoryRegionsIndex;

	while (!physicalMemoryRegions[i].pageCount) {
		i++;

		if (i == physicalMemoryRegionsCount) {
			KernelPanic("MMArchEarlyAllocatePage - Expected more pages in physical regions.\n");
		}
	}

	PhysicalMemoryRegion *region = physicalMemoryRegions + i;
	uintptr_t returnValue = region->baseAddress;

	region->baseAddress += K_PAGE_SIZE;
	region->pageCount--;
	physicalMemoryRegionsPagesCount--;
	physicalMemoryRegionsIndex = i;

	return returnValue;
}

uint64_t MMArchPopulatePageFrameDatabase() {
	uint64_t commitLimit = 0;

	for (uintptr_t i = 0; i < physicalMemoryRegionsCount; i++) {
		uintptr_t base = physicalMemoryRegions[i].baseAddress >> K_PAGE_BITS;
		uintptr_t count = physicalMemoryRegions[i].pageCount;
		commitLimit += count;

		for (uintptr_t j = 0; j < count; j++) {
			MMPhysicalInsertFreePagesNext(base + j);
		}
	}

	physicalMemoryRegionsPagesCount = 0;
	return commitLimit;
}

uintptr_t MMArchGetPhysicalMemoryHighest() {
	return physicalMemoryHighest;
}

void ArchStartupApplicationProcessors() {
	// TODO How do we know that this address is usable?
#define AP_TRAMPOLINE 0x10000

	KEvent delay = {};

	uint8_t *startupData = (uint8_t *) (LOW_MEMORY_MAP_START + AP_TRAMPOLINE);

	// Put the trampoline code in memory.
	EsMemoryCopy(startupData, (void *) ProcessorAPStartup, 0x1000); // Assume that the AP trampoline code <=4KB.

	// Put the paging table location at AP_TRAMPOLINE + 0xFF0.
	*((uint64_t *) (startupData + 0xFF0)) = ProcessorReadCR3();

	// Put the 64-bit GDTR at AP_TRAMPOLINE + 0xFE0.
	EsMemoryCopy(startupData + 0xFE0, (void *) processorGDTR, 0x10);

	// Put the GDT at AP_TRAMPOLINE + 0x1000.
	EsMemoryCopy(startupData + 0x1000, (void *) gdt_data, 0x1000);

	// Put the startup flag at AP_TRAMPOLINE + 0xFC0
	uint8_t volatile *startupFlag = (uint8_t *) (LOW_MEMORY_MAP_START + AP_TRAMPOLINE + 0xFC0);

	// Temporarily identity map 2 pages in at 0x10000.
	MMArchMapPage(kernelMMSpace, AP_TRAMPOLINE, AP_TRAMPOLINE, MM_MAP_PAGE_COMMIT_TABLES_NOW);
	MMArchMapPage(kernelMMSpace, AP_TRAMPOLINE + 0x1000, AP_TRAMPOLINE + 0x1000, MM_MAP_PAGE_COMMIT_TABLES_NOW);

	for (uintptr_t i = 0; i < acpi.processorCount; i++) {
		ArchCPU *processor = acpi.processors + i;
		if (processor->bootProcessor) continue;

		// Allocate state for the processor.
		NewProcessorStorage storage = AllocateNewProcessorStorage(processor);

		// Clear the startup flag.
		*startupFlag = 0;

		// Put the stack at AP_TRAMPOLINE + 0xFD0, and the address of the NewProcessorStorage at AP_TRAMPOLINE + 0xFB0.
		void *stack = (void *) ((uintptr_t) MMStandardAllocate(kernelMMSpace, 0x1000, MM_REGION_FIXED) + 0x1000);
		*((void **) (startupData + 0xFD0)) = stack;
		*((NewProcessorStorage **) (startupData + 0xFB0)) = &storage;

		KernelLog(LOG_INFO, "ACPI", "starting processor", "Starting processor %d with local storage %x...\n", i, storage.local);

		// Send an INIT IPI.
		ProcessorDisableInterrupts(); // Don't be interrupted between writes...
		LapicWriteRegister(0x310 >> 2, processor->apicID << 24);
		LapicWriteRegister(0x300 >> 2, 0x4500);
		ProcessorEnableInterrupts();
		KEventWait(&delay, 10);

		// Send a startup IPI.
		ProcessorDisableInterrupts();
		LapicWriteRegister(0x310 >> 2, processor->apicID << 24);
		LapicWriteRegister(0x300 >> 2, 0x4600 | (AP_TRAMPOLINE >> K_PAGE_BITS));
		ProcessorEnableInterrupts();
		for (uintptr_t i = 0; i < 100 && *startupFlag == 0; i++) KEventWait(&delay, 1);

		if (*startupFlag) {
			// The processor started correctly.
		} else {
			// Send a startup IPI, again.
			ProcessorDisableInterrupts();
			LapicWriteRegister(0x310 >> 2, processor->apicID << 24);
			LapicWriteRegister(0x300 >> 2, 0x4600 | (AP_TRAMPOLINE >> K_PAGE_BITS));
			ProcessorEnableInterrupts();
			for (uintptr_t i = 0; i < 1000 && *startupFlag == 0; i++) KEventWait(&delay, 1); // Wait longer this time.

			if (*startupFlag) {
				// The processor started correctly.
			} else {
				// The processor could not be started.
				KernelLog(LOG_ERROR, "ACPI", "processor startup failure", 
						"ACPIInitialise - Could not start processor %d\n", processor->processorID);
				continue;
			}
		}

		// EsPrint("Startup flag 1 reached!\n");

		for (uintptr_t i = 0; i < 10000 && *startupFlag != 2; i++) KEventWait(&delay, 1);

		if (*startupFlag == 2) {
			// The processor started!
		} else {
			// The processor did not report it completed initilisation, worringly.
			// Don't let it continue.

			KernelLog(LOG_ERROR, "ACPI", "processor startup failure", 
					"ACPIInitialise - Could not initialise processor %d\n", processor->processorID);

			// TODO Send IPI to stop the processor.
		}
	}
	
	// Remove the identity pages needed for the trampoline code.
	MMArchUnmapPages(kernelMMSpace, AP_TRAMPOLINE, 2, ES_FLAGS_DEFAULT);
}

uint64_t ArchGetTimeMs() {
	// Update the time stamp counter synchronization value.
	timeStampCounterSynchronizationValue = ((timeStampCounterSynchronizationValue & 0x8000000000000000) 
			^ 0x8000000000000000) | ProcessorReadTimeStamp();

	if (acpi.hpetBaseAddress && acpi.hpetPeriod) {
		__int128 fsToMs = 1000000000000;
		__int128 reading = acpi.hpetBaseAddress[30];
		return (uint64_t) (reading * (__int128) acpi.hpetPeriod / fsToMs);
	} else {
		return ArchGetTimeFromPITMs();
	}
}

uintptr_t ArchFindRootSystemDescriptorPointer() {
	uint64_t uefiRSDP = *((uint64_t *) (LOW_MEMORY_MAP_START + GetBootloaderInformationOffset() + 0x7FE8));

	if (uefiRSDP) {
		return uefiRSDP;
	}

	PhysicalMemoryRegion searchRegions[2];

	searchRegions[0].baseAddress = (uintptr_t) (((uint16_t *) LOW_MEMORY_MAP_START)[0x40E] << 4) + LOW_MEMORY_MAP_START;
	searchRegions[0].pageCount = 0x400;
	searchRegions[1].baseAddress = (uintptr_t) 0xE0000 + LOW_MEMORY_MAP_START;
	searchRegions[1].pageCount = 0x20000;

	for (uintptr_t i = 0; i < 2; i++) {
		for (uintptr_t address = searchRegions[i].baseAddress;
				address < searchRegions[i].baseAddress + searchRegions[i].pageCount;
				address += 16) {
			RootSystemDescriptorPointer *rsdp = (RootSystemDescriptorPointer *) address;

			if (rsdp->signature != SIGNATURE_RSDP) {
				continue;
			}

			if (rsdp->revision == 0) {
				if (EsMemorySumBytes((uint8_t *) rsdp, 20)) {
					continue;
				}

				return (uintptr_t) rsdp - LOW_MEMORY_MAP_START;
			} else if (rsdp->revision == 2) {
				if (EsMemorySumBytes((uint8_t *) rsdp, sizeof(RootSystemDescriptorPointer))) {
					continue;
				}

				return (uintptr_t) rsdp - LOW_MEMORY_MAP_START;
			}
		}
	}

	return 0;
}

void ArchInitialise() {
	ACPIParseTables();

	uint8_t bootstrapLapicID = (LapicReadRegister(0x20 >> 2) >> 24); 

	ArchCPU *currentCPU = nullptr;

	for (uintptr_t i = 0; i < acpi.processorCount; i++) {
		if (acpi.processors[i].apicID == bootstrapLapicID) {
			// That's us!
			currentCPU = acpi.processors + i;
			currentCPU->bootProcessor = true;
			break;
		}
	}

	if (!currentCPU) {
		KernelPanic("ACPIInitialise - Could not find the bootstrap processor\n");
	}

	// Calibrate the LAPIC's timer and processor's timestamp counter.
	ProcessorDisableInterrupts();
	uint64_t start = ProcessorReadTimeStamp();
	LapicWriteRegister(0x380 >> 2, (uint32_t) -1); 
	for (int i = 0; i < 8; i++) ArchDelay1Ms(); // Average over 8ms
	acpi.lapicTicksPerMs = ((uint32_t) -1 - LapicReadRegister(0x390 >> 2)) >> 4;
	EsRandomAddEntropy(LapicReadRegister(0x390 >> 2));
	uint64_t end = ProcessorReadTimeStamp();
	timeStampTicksPerMs = (end - start) >> 3;
	ProcessorEnableInterrupts();
	// EsPrint("timeStampTicksPerMs = %d\n", timeStampTicksPerMs);

	// Finish processor initialisation.
	// This sets up interrupts, the timer, CPULocalStorage, the GDT and TSS,
	// and registers the processor with the scheduler.

	NewProcessorStorage storage = AllocateNewProcessorStorage(currentCPU);
	SetupProcessor2(&storage);
}

#endif
