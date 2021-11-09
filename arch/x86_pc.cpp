// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

#include <arch/x86_pc.h>

extern "C" uint64_t ProcessorReadCR3();

extern "C" void gdt_data();
extern "C" void processorGDTR();
extern "C" void ProcessorAPStartup();

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

uint8_t pciIRQLines[0x100 /* slots */][4 /* pins */];

MSIHandler msiHandlers[INTERRUPT_VECTOR_MSI_COUNT];
IRQHandler irqHandlers[0x40];
KSpinlock irqHandlersLock; // Also for msiHandlers.

extern volatile uint64_t timeStampCounterSynchronizationValue;

PhysicalMemoryRegion *physicalMemoryRegions;
size_t physicalMemoryRegionsCount;
size_t physicalMemoryRegionsPagesCount;
size_t physicalMemoryOriginalPagesCount;
size_t physicalMemoryRegionsIndex;
uintptr_t physicalMemoryHighest;

uint32_t bootloaderID;
uintptr_t bootloaderInformationOffset;

// Spinlock since some drivers need to access it in IRQs (e.g. ACPICA).
KSpinlock pciConfigSpinlock; 

KSpinlock ipiLock;

const char *const exceptionInformation[] = {
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

uint32_t LapicReadRegister(uint32_t reg) {
#ifdef ES_ARCH_X86_64
	return acpi.lapicAddress[reg];
#else
	return ((volatile uint32_t *) LOCAL_APIC_BASE)[reg];
#endif
}

void LapicWriteRegister(uint32_t reg, uint32_t value) {
#ifdef ES_ARCH_X86_64
	acpi.lapicAddress[reg] = value;
#else
	((volatile uint32_t *) LOCAL_APIC_BASE)[reg] = value;
#endif
}

void LapicNextTimer(size_t ms) {
	LapicWriteRegister(0x320 >> 2, TIMER_INTERRUPT | (1 << 17)); 
	LapicWriteRegister(0x380 >> 2, acpi.lapicTicksPerMs * ms); 
}

void LapicEndOfInterrupt() {
	LapicWriteRegister(0xB0 >> 2, 0);
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

void ProcessorOut8Delayed(uint16_t port, uint8_t value) {
	ProcessorOut8(port, value);

	// Read an unused port to get a short delay.
	ProcessorIn8(IO_UNUSED_DELAY);
}

extern "C" void PCSetupCOM1() {
#ifdef COM_OUTPUT
	ProcessorOut8Delayed(IO_COM_1 + 1, 0x00);
	ProcessorOut8Delayed(IO_COM_1 + 3, 0x80);
	ProcessorOut8Delayed(IO_COM_1 + 0, 0x03);
	ProcessorOut8Delayed(IO_COM_1 + 1, 0x00);
	ProcessorOut8Delayed(IO_COM_1 + 3, 0x03);
	ProcessorOut8Delayed(IO_COM_1 + 2, 0xC7);
	ProcessorOut8Delayed(IO_COM_1 + 4, 0x0B);

	// Print a divider line.
	for (uint8_t i = 0; i < 10; i++) ProcessorDebugOutputByte('-');
	ProcessorDebugOutputByte('\r');
	ProcessorDebugOutputByte('\n');
#endif
}

extern "C" void PCDisablePIC() {
	// Remap the ISRs sent by the PIC to 0x20 - 0x2F.
	// Even though we'll mask the PIC to use the APIC, 
	// we have to do this so that the spurious interrupts are sent to a reasonable vector range.
	ProcessorOut8Delayed(IO_PIC_1_COMMAND, 0x11);
	ProcessorOut8Delayed(IO_PIC_2_COMMAND, 0x11);
	ProcessorOut8Delayed(IO_PIC_1_DATA, 0x20);
	ProcessorOut8Delayed(IO_PIC_2_DATA, 0x28);
	ProcessorOut8Delayed(IO_PIC_1_DATA, 0x04);
	ProcessorOut8Delayed(IO_PIC_2_DATA, 0x02);
	ProcessorOut8Delayed(IO_PIC_1_DATA, 0x01);
	ProcessorOut8Delayed(IO_PIC_2_DATA, 0x01);

	// Mask all interrupts.
	ProcessorOut8Delayed(IO_PIC_1_DATA, 0xFF);
	ProcessorOut8Delayed(IO_PIC_2_DATA, 0xFF);
}

extern "C" void PCProcessMemoryMap() {
	physicalMemoryRegions = (PhysicalMemoryRegion *) (LOW_MEMORY_MAP_START + 0x60000 + bootloaderInformationOffset);

	for (uintptr_t i = 0; physicalMemoryRegions[i].baseAddress; i++) {
		PhysicalMemoryRegion region = physicalMemoryRegions[i];
		uint64_t end = region.baseAddress + (region.pageCount << K_PAGE_BITS);
#ifdef ES_BITS_32
		if (end > 0x100000000) { region.pageCount = 0; continue; }
#endif
		physicalMemoryRegionsPagesCount += region.pageCount;
		if (end > physicalMemoryHighest) physicalMemoryHighest = end;
		physicalMemoryRegionsCount++;
	}

	physicalMemoryOriginalPagesCount = physicalMemoryRegions[physicalMemoryRegionsCount].pageCount;
}

uintptr_t GetBootloaderInformationOffset() {
	return bootloaderInformationOffset;
}

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

void MMArchUnmapPages(MMSpace *space, uintptr_t virtualAddressStart, uintptr_t pageCount, unsigned flags, size_t unmapMaximum, uintptr_t *resumePosition) {
	// We can't let anyone use the unmapped pages until they've been invalidated on all processors.
	// This also synchronises modified bit updating.
	KMutexAcquire(&pmm.pageFrameMutex);
	EsDefer(KMutexRelease(&pmm.pageFrameMutex));

	KMutexAcquire(&space->data.mutex);
	EsDefer(KMutexRelease(&space->data.mutex));

#ifdef ES_ARCH_X86_64
	uintptr_t tableBase = virtualAddressStart & 0x0000FFFFFFFFF000;
#else
	uintptr_t tableBase = virtualAddressStart & 0xFFFFF000;
#endif
	uintptr_t start = resumePosition ? *resumePosition : 0;

	// TODO Freeing newly empty page tables.
	// 	- What do we need to invalidate when we do this?

	for (uintptr_t i = start; i < pageCount; i++) {
		uintptr_t virtualAddress = (i << K_PAGE_BITS) + tableBase;

#ifdef ES_ARCH_X86_64
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
#endif

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

#ifdef ES_ARCH_X86_64
		uintptr_t physicalAddress = translation & 0x0000FFFFFFFFF000;
#else
		uintptr_t physicalAddress = translation & 0xFFFFF000;
#endif

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

bool MMArchMapPage(MMSpace *space, uintptr_t physicalAddress, uintptr_t virtualAddress, unsigned flags) {
	// TODO Use the no-execute bit.

	if ((physicalAddress | virtualAddress) & (K_PAGE_SIZE - 1)) {
		KernelPanic("MMArchMapPage - Address not page aligned.\n");
	}

	if (pmm.pageFrames && (physicalAddress >> K_PAGE_BITS) < pmm.pageFrameDatabaseCount) {
		if (pmm.pageFrames[physicalAddress >> K_PAGE_BITS].state != MMPageFrame::ACTIVE
				&& pmm.pageFrames[physicalAddress >> K_PAGE_BITS].state != MMPageFrame::UNUSABLE) {
			KernelPanic("MMArchMapPage - Physical page frame %x not marked as ACTIVE or UNUSABLE.\n", physicalAddress);
		}
	}

	if (!physicalAddress) {
		KernelPanic("MMArchMapPage - Attempt to map physical page 0.\n");
	} else if (!virtualAddress) {
		KernelPanic("MMArchMapPage - Attempt to map virtual page 0.\n");
#ifdef ES_ARCH_X86_64
	} else if (virtualAddress < 0xFFFF800000000000 && ProcessorReadCR3() != space->data.cr3) {
#else
	} else if (virtualAddress < 0xC0000000 && ProcessorReadCR3() != space->data.cr3) {
#endif
		KernelPanic("MMArchMapPage - Attempt to map page into other address space.\n");
	}

	bool acquireFrameLock = !(flags & (MM_MAP_PAGE_NO_NEW_TABLES | MM_MAP_PAGE_FRAME_LOCK_ACQUIRED));
	if (acquireFrameLock) KMutexAcquire(&pmm.pageFrameMutex);
	EsDefer(if (acquireFrameLock) KMutexRelease(&pmm.pageFrameMutex););

	bool acquireSpaceLock = ~flags & MM_MAP_PAGE_NO_NEW_TABLES;
	if (acquireSpaceLock) KMutexAcquire(&space->data.mutex);
	EsDefer(if (acquireSpaceLock) KMutexRelease(&space->data.mutex));

	// EsPrint("\tMap, %x -> %x\n", virtualAddress, physicalAddress);

	uintptr_t oldVirtualAddress = virtualAddress;
#ifdef ES_ARCH_X86_64
	physicalAddress &= 0xFFFFFFFFFFFFF000;
	virtualAddress  &= 0x0000FFFFFFFFF000;
#endif

#ifdef ES_ARCH_X86_64
	uintptr_t indexL4 = virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 3);
	uintptr_t indexL3 = virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 2);
#endif
	uintptr_t indexL2 = virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 1);
	uintptr_t indexL1 = virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 0);

	if (space != coreMMSpace && space != kernelMMSpace /* Don't check the kernel's space since the bootloader's tables won't be committed. */) {
#ifdef ES_ARCH_X86_64
		if (!(space->data.l3Commit[indexL4 >> 3] & (1 << (indexL4 & 7)))) KernelPanic("MMArchMapPage - Attempt to map using uncommitted L3 page table.\n");
		if (!(space->data.l2Commit[indexL3 >> 3] & (1 << (indexL3 & 7)))) KernelPanic("MMArchMapPage - Attempt to map using uncommitted L2 page table.\n");
#endif
		if (!(space->data.l1Commit[indexL2 >> 3] & (1 << (indexL2 & 7)))) KernelPanic("MMArchMapPage - Attempt to map using uncommitted L1 page table.\n");
	}

#ifdef ES_ARCH_X86_64
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
#endif

	if ((PAGE_TABLE_L2[indexL2] & 1) == 0) {
		if (flags & MM_MAP_PAGE_NO_NEW_TABLES) KernelPanic("MMArchMapPage - NO_NEW_TABLES flag set, but a table was missing.\n");
		PAGE_TABLE_L2[indexL2] = MMPhysicalAllocate(MM_PHYSICAL_ALLOCATE_LOCK_ACQUIRED) | 7;
		ProcessorInvalidatePage((uintptr_t) (PAGE_TABLE_L1 + indexL1)); // Not strictly necessary.
		EsMemoryZero((void *) ((uintptr_t) (PAGE_TABLE_L1 + indexL1) & ~(K_PAGE_SIZE - 1)), K_PAGE_SIZE);
		space->data.pageTablesActive++;
	}

	uintptr_t oldValue = PAGE_TABLE_L1[indexL1];
	uintptr_t value = physicalAddress | 3;

#ifdef ES_ARCH_X86_64
	if (flags & MM_MAP_PAGE_WRITE_COMBINING) value |= 16; // This only works because we modified the PAT in SetupProcessor1.
#else
	if (flags & MM_MAP_PAGE_WRITE_COMBINING) KernelPanic("MMArchMapPage - Write combining is unimplemented.\n"); // TODO.
#endif
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
	// TODO Should we be marking page tables as dirty/accessed? (Including those made by the 32-bit AND 64-bit bootloader and MMArchInitialise).
	// 	When page table trimming is implemented, we'll probably need to do this.
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

bool MMArchMakePageWritable(MMSpace *space, uintptr_t virtualAddress) {
	KMutexAcquire(&space->data.mutex);
	EsDefer(KMutexRelease(&space->data.mutex));

#ifdef ES_ARCH_X86_64
	virtualAddress &= 0x0000FFFFFFFFF000;
#else
	virtualAddress &= 0xFFFFF000;
#endif

#ifdef ES_ARCH_X86_64
	uintptr_t indexL4 = virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 3);
	if ((PAGE_TABLE_L4[indexL4] & 1) == 0) return false;
	uintptr_t indexL3 = virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 2);
	if ((PAGE_TABLE_L3[indexL3] & 1) == 0) return false;
#endif
	uintptr_t indexL2 = virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 1);
	if ((PAGE_TABLE_L2[indexL2] & 1) == 0) return false;
	uintptr_t indexL1 = virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 0);
	if ((PAGE_TABLE_L1[indexL1] & 1) == 0) return false;

	PAGE_TABLE_L1[indexL1] |= 2;
	return true;
}

void MMArchInitialise() {
	coreMMSpace->data.cr3 = kernelMMSpace->data.cr3 = ProcessorReadCR3();

	mmCoreRegions[0].baseAddress = MM_CORE_SPACE_START;
	mmCoreRegions[0].pageCount = MM_CORE_SPACE_SIZE / K_PAGE_SIZE;

#ifdef ES_ARCH_X86_64
	for (uintptr_t i = 0x100; i < 0x200; i++) {
		if (PAGE_TABLE_L4[i] == 0) {
			// We don't need to commit anything because the PMM isn't ready yet.
			PAGE_TABLE_L4[i] = MMPhysicalAllocate(ES_FLAGS_DEFAULT) | 3; 
			EsMemoryZero((void *) (PAGE_TABLE_L3 + i * 0x200), K_PAGE_SIZE);
		}
	}

	coreMMSpace->data.l1Commit = coreL1Commit;
	KMutexAcquire(&coreMMSpace->reserveMutex);
	kernelMMSpace->data.l1Commit = (uint8_t *) MMReserve(coreMMSpace, L1_COMMIT_SIZE_BYTES, MM_REGION_NORMAL | MM_REGION_NO_COMMIT_TRACKING | MM_REGION_FIXED)->baseAddress;
	KMutexRelease(&coreMMSpace->reserveMutex);
#endif
}

uintptr_t MMArchTranslateAddress(MMSpace *, uintptr_t virtualAddress, bool writeAccess) {
	// TODO This mutex will be necessary if we ever remove page tables.
	// space->data.mutex.Acquire();
	// EsDefer(space->data.mutex.Release());

#ifdef ES_ARCH_X86_64
	virtualAddress &= 0x0000FFFFFFFFF000;
	if ((PAGE_TABLE_L4[virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 3)] & 1) == 0) return 0;
	if ((PAGE_TABLE_L3[virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 2)] & 1) == 0) return 0;
#endif
	if ((PAGE_TABLE_L2[virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 1)] & 1) == 0) return 0;
	uintptr_t physicalAddress = PAGE_TABLE_L1[virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 0)];
	if (writeAccess && !(physicalAddress & 2)) return 0;
#ifdef ES_ARCH_X86_64
	return (physicalAddress & 1) ? (physicalAddress & 0x0000FFFFFFFFF000) : 0;
#else
	return (physicalAddress & 1) ? (physicalAddress & 0xFFFFF000) : 0;
#endif
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

uint64_t ArchGetTimeFromPITMs() {
	// TODO This isn't working on real hardware, but EarlyDelay1Ms is?

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

void EarlyDelay1Ms() {
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

NewProcessorStorage AllocateNewProcessorStorage(ArchCPU *archCPU) {
	NewProcessorStorage storage = {};
	storage.local = (CPULocalStorage *) EsHeapAllocate(sizeof(CPULocalStorage), true, K_FIXED);
#ifdef ES_ARCH_X86_64
	storage.gdt = (uint32_t *) MMMapPhysical(kernelMMSpace, MMPhysicalAllocate(MM_PHYSICAL_ALLOCATE_COMMIT_NOW), K_PAGE_SIZE, ES_FLAGS_DEFAULT);
#endif
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

#ifdef ES_ARCH_X86_64
	uint32_t *gdt = storage->gdt;
	void *bootstrapGDT = (void *) (((uint64_t *) ((uint16_t *) processorGDTR + 1))[0]);
	EsMemoryCopy(gdt, bootstrapGDT, 2048);
	uint32_t *tss = (uint32_t *) ((uint8_t *) storage->gdt + 2048);
	storage->local->archCPU->kernelStack = (void **) (tss + 1);
	ProcessorInstallTSS(gdt, tss);
#endif
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
		KernelPanic("ArchInitialise - Could not find the bootstrap processor\n");
	}

	// Calibrate the LAPIC's timer and processor's timestamp counter.
	ProcessorDisableInterrupts();
	uint64_t start = ProcessorReadTimeStamp();
	LapicWriteRegister(0x380 >> 2, (uint32_t) -1); 
	for (int i = 0; i < 8; i++) EarlyDelay1Ms(); // Average over 8ms
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

uint64_t ArchGetTimeMs() {
	// Update the time stamp counter synchronization value.
	timeStampCounterSynchronizationValue = ((timeStampCounterSynchronizationValue & 0x8000000000000000) 
			^ 0x8000000000000000) | ProcessorReadTimeStamp();

#ifdef ES_ARCH_X86_64
	if (acpi.hpetBaseAddress && acpi.hpetPeriod) {
		__int128 fsToMs = 1000000000000;
		__int128 reading = acpi.hpetBaseAddress[30];
		return (uint64_t) (reading * (__int128) acpi.hpetPeriod / fsToMs);
	}
#endif

	return ArchGetTimeFromPITMs();
}

extern "C" bool PostContextSwitch(InterruptContext *context, MMSpace *oldAddressSpace) {
	if (scheduler.dispatchSpinlock.interruptsEnabled) {
		KernelPanic("PostContextSwitch - Interrupts were enabled. (3)\n");
	}

	// We can only free the scheduler's spinlock when we are no longer using the stack
	// from the previous thread. See DoContextSwitch.
	// (Another CPU can KillThread this once it's back in activeThreads.)
	KSpinlockRelease(&scheduler.dispatchSpinlock, true);

	Thread *currentThread = GetCurrentThread();

#ifdef ES_ARCH_X86_64
	CPULocalStorage *local = GetLocalStorage();
	void *kernelStack = (void *) currentThread->kernelStack;
	*local->archCPU->kernelStack = kernelStack;
#endif

	bool newThread = currentThread->cpuTimeSlices == 1;
	LapicEndOfInterrupt();
	ContextSanityCheck(context);
	ProcessorSetThreadStorage(currentThread->tlsAddress);
	MMSpaceCloseReference(oldAddressSpace);

#ifdef ES_ARCH_X86_64
	KernelLog(LOG_VERBOSE, "Arch", "context switch", "Context switch to %zthread %x at %x\n", newThread ? "new " : "", currentThread, context->rip);
	currentThread->lastKnownExecutionAddress = context->rip;
#else
	KernelLog(LOG_VERBOSE, "Arch", "context switch", "Context switch to %zthread %x at %x\n", newThread ? "new " : "", currentThread, context->eip);
	currentThread->lastKnownExecutionAddress = context->eip;
#endif

	if (ProcessorAreInterruptsEnabled()) {
		KernelPanic("PostContextSwitch - Interrupts were enabled. (2)\n");
	}

	if (local->spinlockCount) {
		KernelPanic("PostContextSwitch - spinlockCount is non-zero (%x).\n", local);
	}

#ifdef ES_ARCH_X86_32
	if (context->fromRing0) {
		// Returning to a kernel thread; we need to fix the stack.
		uint32_t irq = context->esp;
		uint32_t errorCode = context->ss;
		context->ss = context->flags;
		context->esp = context->cs;
		context->flags = context->eip;
		context->cs = context->errorCode;
		context->eip = context->irq;
		context->irq = irq;
		context->errorCode = errorCode;
	}
#endif

	return newThread;
}

bool SetupInterruptRedirectionEntry(uintptr_t _line) {
	KSpinlockAssertLocked(&irqHandlersLock);

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

	bool result = true;

	if (!found) {
		KernelLog(LOG_ERROR, "Arch", "too many IRQ handlers", "The limit of IRQ handlers was reached (%d), and the handler for '%z' was not registered.\n",
				sizeof(irqHandlers) / sizeof(irqHandlers[0]), cOwnerName);
		result = false;
	} else {
		KernelLog(LOG_INFO, "Arch", "register IRQ", "KRegisterIRQ - Registered IRQ %d to '%z'.\n", line, cOwnerName);

		if (line != -1) {
			if (!SetupInterruptRedirectionEntry(line)) {
				result = false;
			}
		} else {
			SetupInterruptRedirectionEntry(9);
			SetupInterruptRedirectionEntry(10);
			SetupInterruptRedirectionEntry(11);
		}
	}

	KSpinlockRelease(&irqHandlersLock);

	return result;
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
