// TODO Support for systems without MMX/SSE.
// TODO Support for systems without an APIC.

#ifndef IMPLEMENTATION

struct MMArchVAS {
	// NOTE Must be first in the structure. See ProcessorSetAddressSpace and ArchSwitchContext.
	uintptr_t cr3;

	// Each process has a 32-bit address space.
	// That's 2^20 pages.
	// That's 2^10 L1 page tables. 2^7 bytes of bitset.
	// Tracking of the committed L1 tables is done in l1Commit.
#define L1_COMMIT_SIZE_BYTES (1 << 7)
	uint8_t l1Commit[L1_COMMIT_SIZE_BYTES];
	size_t pageTablesCommitted;
	size_t pageTablesActive;

	// TODO Consider core/kernel mutex consistency? I think it's fine, but...
	KMutex mutex; // Acquire to modify the page tables.
};

#define MM_KERNEL_SPACE_START (0xC4000000)
#define MM_KERNEL_SPACE_SIZE  (0xE0000000 - 0xC4000000)
#define MM_MODULES_START      (0xE0000000)
#define MM_MODULES_SIZE	      (0xE6000000 - 0xE0000000)
#define MM_CORE_REGIONS_START (0xE6000000)
#define MM_CORE_REGIONS_COUNT ((0xE7000000 - 0xE6000000) / sizeof(MMRegion))
#define MM_CORE_SPACE_START   (0xE7000000)
#define MM_CORE_SPACE_SIZE    (0xEC000000 - 0xE7000000)
#define LOW_MEMORY_MAP_START  (0xEC000000)
#define LOW_MEMORY_LIMIT      (0x00400000) // The first 4MB - 4KB is mapped here; the last 4KB map to the local APIC.
#define LOCAL_APIC_BASE       (0xEC3FF000)

#define ArchCheckBundleHeader() (header.mapAddress >= 0xC0000000ULL || header.mapAddress < 0x1000 || fileSize > 0x10000000ULL)
#define ArchCheckELFHeader()    (header->virtualAddress >= 0xC0000000ULL || header->virtualAddress < 0x1000 || header->segmentSize > 0x10000000ULL)

#define K_ARCH_STACK_GROWS_DOWN
#define K_ARCH_NAME "x86_32"

#endif

#ifdef IMPLEMENTATION

// Recursive page table mapping in slot 0x1FF.
#define PAGE_TABLE_L2 ((volatile uint32_t *) 0xFFFFF000)
#define PAGE_TABLE_L1 ((volatile uint32_t *) 0xFFC00000)
#define ENTRIES_PER_PAGE_TABLE (1024)
#define ENTRIES_PER_PAGE_TABLE_BITS (10)

struct InterruptContext {
	uint32_t cr8, cr2, ds;
	uint8_t  fxsave[512 + 16];
	uint32_t ebp, edi, esi, edx, ecx, ebx, eax;
	uint32_t fromRing0, irq, errorCode;
	uint32_t eip, cs, flags, esp, ss;

	// Note that when ring 0 is interrupted the order is different:
	// 	esp, ss, irq, errorCode, eip, cs, flags.
	// However we fix it before and after interrupts in InterruptHandler.
};

volatile uintptr_t tlbShootdownVirtualAddress;
volatile size_t tlbShootdownPageCount;
volatile size_t tlbShootdownProcessorsRemaining;

#include <arch/x86_pc.h>
#include <drivers/acpi.cpp>
#include <arch/x86_pc.cpp>

InterruptContext *ArchInitialiseThread(uintptr_t kernelStack, uintptr_t kernelStackSize, 
		Thread *thread, uintptr_t startAddress, uintptr_t argument1, 
		uintptr_t argument2, bool userland, uintptr_t stack, uintptr_t userStackSize) {
	InterruptContext *context = ((InterruptContext *) (kernelStack + kernelStackSize - 12)) - 1;
	thread->kernelStack = kernelStack + kernelStackSize - 12;
	
	*((uintptr_t *) (kernelStack + kernelStackSize - 12)) = (uintptr_t) KThreadTerminate;
	*((uintptr_t *) (kernelStack + kernelStackSize -  8)) = argument1;
	*((uintptr_t *) (kernelStack + kernelStackSize -  4)) = argument2;

	context->fxsave[32] = 0x80;
	context->fxsave[33] = 0x1F;

	if (userland) {
		context->cs = 0x1B;
		context->ds = 0x23;
		context->ss = 0x23;
	} else {
		context->cs = 0x08;
		context->ds = 0x10;
		context->ss = 0x10;
		context->fromRing0 = true;
	}

	context->flags = 1 << 9; // Interrupt flag
	context->eip = startAddress;
	context->esp = stack + userStackSize;

	return context;
}

void TLBShootdownCallback() {
	uintptr_t page = tlbShootdownVirtualAddress;

	// TODO How should this be determined?
#define INVALIDATE_ALL_PAGES_THRESHOLD (1024)
	if (tlbShootdownPageCount > INVALIDATE_ALL_PAGES_THRESHOLD) { 
		ProcessorInvalidateAllPages();
	} else {
		for (uintptr_t i = 0; i < tlbShootdownPageCount; i++, page += K_PAGE_SIZE) {
			ProcessorInvalidatePage(page);
		}
	}
}

void MMArchInvalidatePages(uintptr_t virtualAddressStart, uintptr_t pageCount) {
	KSpinlockAcquire(&ipiLock);
	tlbShootdownVirtualAddress = virtualAddressStart;
	tlbShootdownPageCount = pageCount;
	tlbShootdownProcessorsRemaining = KGetCPUCount();

	if (tlbShootdownProcessorsRemaining > 1) {
		size_t ignored = ProcessorSendIPI(TLB_SHOOTDOWN_IPI);
		__sync_fetch_and_sub(&tlbShootdownProcessorsRemaining, ignored);
		while (tlbShootdownProcessorsRemaining);
	}

	TLBShootdownCallback();
	KSpinlockRelease(&ipiLock);
}

bool MMArchIsBufferInUserRange(uintptr_t baseAddress, size_t byteCount) {
	// TODO.
	KernelPanic("Unimplemented!\n");
	return false;
}

bool MMArchSafeCopy(uintptr_t destinationAddress, uintptr_t sourceAddress, size_t byteCount) {
	// TODO.
	KernelPanic("Unimplemented!\n");
	return false;
}

bool MMArchCommitPageTables(MMSpace *space, MMRegion *region) {
	KMutexAssertLocked(&space->reserveMutex);

	MMArchVAS *data = &space->data;

	uintptr_t base = region->baseAddress - (space == coreMMSpace ? MM_CORE_SPACE_START : 0);
	uintptr_t end = base + (region->pageCount << K_PAGE_BITS);
	uintptr_t needed = 0;

	for (uintptr_t i = base; i < end; i += 1L << (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 1)) {
		uintptr_t indexL2 = i >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 1);
		if (!(data->l1Commit[indexL2 >> 3] & (1 << (indexL2 & 7)))) needed++;
		i = indexL2 << (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 1);
	}

	if (needed) {
		if (!MMCommit(needed * K_PAGE_SIZE, true)) {
			return false;
		}

		data->pageTablesCommitted += needed;
	}

	for (uintptr_t i = base; i < end; i += 1L << (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 1)) {
		uintptr_t indexL2 = i >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 1);
		data->l1Commit[indexL2 >> 3] |= (1 << (indexL2 & 7));
		i = indexL2 << (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 1);
	}

	return true;
}

bool MMArchInitialiseUserSpace(MMSpace *space, MMRegion *firstRegion) {
	// TODO.
	KernelPanic("Unimplemented!\n");
	return false;
}

void MMArchFreeVAS(MMSpace *space) {
	// TODO.
	KernelPanic("Unimplemented!\n");
}

void MMArchFinalizeVAS(MMSpace *space) {
	// TODO.
	KernelPanic("Unimplemented!\n");
}

void ArchStartupApplicationProcessors() {
	// TODO.
}

bool MMArchHandlePageFault(uintptr_t address, uint32_t flags) {
	address &= ~(K_PAGE_SIZE - 1);
	bool forSupervisor = flags & MM_HANDLE_PAGE_FAULT_FOR_SUPERVISOR;

	if (!ProcessorAreInterruptsEnabled()) {
		KernelPanic("MMArchHandlePageFault - Page fault with interrupts disabled.\n");
	}

	if (address >= MM_CORE_REGIONS_START && address < MM_CORE_REGIONS_START + MM_CORE_REGIONS_COUNT * sizeof(MMRegion) && forSupervisor) {
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
	} else if (address >= K_PAGE_SIZE) {
		Thread *thread = GetCurrentThread();
		return MMHandlePageFault(thread->temporaryAddressSpace ?: thread->process->vmm, address, flags);
	}

	return false;
}

void ContextSanityCheck(InterruptContext *context) {
	if (context->cs > 0x100 || context->ds > 0x100 || context->ss > 0x100 
			|| (context->eip < 0xC0000000 && context->cs == 0x08)) {
		KernelPanic("ContextSanityCheck - Corrupt context (%x/%x/%x/%x/%x/%x)\n", 
				context, context->cs, context->ds, context->ss, context->eip, context->esp);
	}
}

extern "C" void InterruptHandler(InterruptContext *context) {
	if (context->fromRing0) {
		uint32_t esp = context->irq;
		uint32_t ss = context->errorCode;
		context->irq = context->eip;
		context->errorCode = context->cs;
		context->eip = context->flags;
		context->cs = context->esp;
		context->flags = context->ss;
		context->esp = esp;
		context->ss = ss;
	}

	CPULocalStorage *local = GetLocalStorage();

	if (scheduler.panic && context->irq != 2) {
		goto end;
	}

	if (context->irq < 0x20) {
		// Processor exception.

		ProcessorEnableInterrupts();

		if ((context->cs & 3) == 0) {
			bool handled = false;

			if (context->irq == 0x0E && (~context->errorCode & (1 << 3))) {
				if (MMArchHandlePageFault(context->cr2, MM_HANDLE_PAGE_FAULT_FOR_SUPERVISOR
							| ((context->errorCode & 2) ? MM_HANDLE_PAGE_FAULT_WRITE : 0))) {
					handled = true;
				}
			} 

			if (!handled) {
				KernelPanic("Unresolvable processor exception encountered in supervisor mode.\n%z\nEIP = %x (CPU %d)\nX86 error codes: [err] %x, [cr2] %x\n"
						"Stack: [esp] %x, [ebp] %x\nRegisters: [eax] %x, [ebx] %x, [esi] %x, [edi] %x.\nThread ID = %d\n", 
						exceptionInformation[context->irq], context->eip, local ? local->processorID : -1, context->errorCode, context->cr2, 
						context->esp, context->ebp, context->eax, context->ebx, context->esi, context->edi, 
						local && local->currentThread ? local->currentThread->id : -1);
			}
		} else {
			// TODO User exceptions.
		}
	} else if (context->irq == 0xFF) {
		// Spurious interrupt (APIC), ignore.
	} else if (context->irq >= 0x20 && context->irq < 0x30) {
		// Spurious interrupt (PIC), ignore.
	} else if (context->irq == TLB_SHOOTDOWN_IPI) {
		TLBShootdownCallback();
		if (!tlbShootdownProcessorsRemaining) KernelPanic("InterruptHandler - tlbShootdownProcessorsRemaining is 0.\n");
		__sync_fetch_and_sub(&tlbShootdownProcessorsRemaining, 1);
	} else if (context->irq == TIMER_INTERRUPT) {
		if (local && scheduler.started && local->schedulerReady) {
			scheduler.Yield(context);
		}
	} else if (context->irq >= INTERRUPT_VECTOR_MSI_START && context->irq < INTERRUPT_VECTOR_MSI_START + INTERRUPT_VECTOR_MSI_COUNT && local) {
		KSpinlockAcquire(&irqHandlersLock);
		MSIHandler handler = msiHandlers[context->irq - INTERRUPT_VECTOR_MSI_START];
		KSpinlockRelease(&irqHandlersLock);
		local->irqSwitchThread = false;

		if (!handler.callback) {
			KernelLog(LOG_ERROR, "Arch", "unexpected MSI", "Unexpected MSI vector %X (no handler).\n", context->irq);
		} else {
			handler.callback(context->irq - INTERRUPT_VECTOR_MSI_START, handler.context);
		}

		if (local->irqSwitchThread && scheduler.started && local->schedulerReady) {
			scheduler.Yield(context); // LapicEndOfInterrupt is called in PostContextSwitch.
		}

		LapicEndOfInterrupt();
	} else if (context->irq >= IRQ_BASE && context->irq < IRQ_BASE + 0x20 && local) {
		// See InterruptHandler in arch/x86_64/kernel.cpp for a discussion of what this is doing.

		local->irqSwitchThread = false;
		local->inIRQ = true;

		uintptr_t line = context->irq - IRQ_BASE;
		KSpinlockAcquire(&irqHandlersLock);

		for (uintptr_t i = 0; i < sizeof(irqHandlers) / sizeof(irqHandlers[0]); i++) {
			IRQHandler handler = irqHandlers[i];
			if (!handler.callback) continue;

			if (handler.line == -1) {
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
			handler.callback(context->irq - IRQ_BASE, handler.context);
			KSpinlockAcquire(&irqHandlersLock);
		}

		KSpinlockRelease(&irqHandlersLock);
		local->inIRQ = false;

		if (local->irqSwitchThread && scheduler.started && local->schedulerReady) {
			scheduler.Yield(context);
		}

		LapicEndOfInterrupt();
	}

	if (context->irq >= 0x30 && context->irq != 0xFF) {
		LapicEndOfInterrupt();
	}

	end:;

	ContextSanityCheck(context);

	if (context->fromRing0) {
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
}

#include <kernel/terminal.cpp>

#endif
