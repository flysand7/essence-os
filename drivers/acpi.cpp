#define SIGNATURE_RSDP (0x2052545020445352)
#define SIGNATURE_RSDT (0x54445352)
#define SIGNATURE_XSDT (0x54445358)
#define SIGNATURE_MADT (0x43495041)
#define SIGNATURE_FADT (0x50434146)
#define SIGNATURE_HPET (0x54455048)

struct RootSystemDescriptorPointer {
	uint64_t signature;
	uint8_t checksum;
	char OEMID[6];
	uint8_t revision;
	uint32_t rsdtAddress;
	uint32_t length;
	uint64_t xsdtAddress;
	uint8_t extendedChecksum;
	uint8_t reserved[3];
};

struct ACPIDescriptorTable {
#define ACPI_DESCRIPTOR_TABLE_HEADER_LENGTH (36)
	uint32_t signature;
	uint32_t length;
	uint64_t id;
	uint64_t tableID;
	uint32_t oemRevision;
	uint32_t creatorID;
	uint32_t creatorRevision;
};

struct MultipleAPICDescriptionTable {
	uint32_t lapicAddress; 
	uint32_t flags;
};

struct ArchCPU {
	uint8_t processorID, kernelProcessorID;
	uint8_t apicID;
	bool bootProcessor;
	void **kernelStack;
	CPULocalStorage *local;
};

struct ACPIIoApic {
	uint8_t id;
	uint32_t volatile *address;
	uint32_t gsiBase;
};

struct ACPIInterruptOverride {
	uint8_t sourceIRQ;
	uint32_t gsiNumber;
	bool activeLow, levelTriggered;
};

struct ACPILapicNMI {
	uint8_t processor; // 0xFF for all processors
	uint8_t lintIndex;
	bool activeLow, levelTriggered;
};

struct ACPI {
	size_t processorCount;
	size_t ioapicCount;
	size_t interruptOverrideCount;
	size_t lapicNMICount;

	ArchCPU processors[256];
	ACPIIoApic ioApics[16];
	ACPIInterruptOverride interruptOverrides[256];
	ACPILapicNMI lapicNMIs[32];

	RootSystemDescriptorPointer *rsdp;
	ACPIDescriptorTable *madt;

	volatile uint32_t *lapicAddress;
	size_t lapicTicksPerMs;

	bool ps2ControllerUnavailable;
	bool vgaControllerUnavailable;
	uint8_t centuryRegisterIndex;

	volatile uint64_t *hpetBaseAddress;
	uint64_t hpetPeriod; // 10^-15 seconds.

	KDevice *computer;
};

ACPI acpi;

uint32_t ACPIIoApicReadRegister(ACPIIoApic *apic, uint32_t reg) {
	apic->address[0] = reg; 
	return apic->address[4];
}

void ACPIIoApicWriteRegister(ACPIIoApic *apic, uint32_t reg, uint32_t value) {
	apic->address[0] = reg; 
	apic->address[4] = value;
}

uint32_t ACPILapicReadRegister(uint32_t reg) {
	return acpi.lapicAddress[reg];
}

void ACPILapicWriteRegister(uint32_t reg, uint32_t value) {
	acpi.lapicAddress[reg] = value;
}

void ACPILapicNextTimer(size_t ms) {
	ACPILapicWriteRegister(0x320 >> 2, TIMER_INTERRUPT | (1 << 17)); 
	ACPILapicWriteRegister(0x380 >> 2, acpi.lapicTicksPerMs * ms); 
}

void ACPILapicEndOfInterrupt() {
	ACPILapicWriteRegister(0xB0 >> 2, 0);
}

void ACPICheckTable(const ACPIDescriptorTable *table) {
	if (!EsMemorySumBytes((uint8_t *) table, table->length)) {
		return;
	}

	KernelPanic("ACPICheckTable - ACPI table with signature %s had invalid checksum: "
			"length: %D, ID = %s, table = %s, OEM revision = %d, creator = %s, creator revision = %d.\n",
			4, &table->signature, table->length, 8, &table->id, 8, &table->tableID, 
			table->oemRevision, 4, &table->creatorID, table->creatorRevision);
}

#ifdef ARCH_X86_COMMON
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

RootSystemDescriptorPointer *ACPIFindRootSystemDescriptorPointer() {
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

				return rsdp;
			} else if (rsdp->revision == 2) {
				if (EsMemorySumBytes((uint8_t *) rsdp, sizeof(RootSystemDescriptorPointer))) {
					continue;
				}

				return rsdp;
			}
		}
	}

	return nullptr;
}
#endif

void *ACPIMapPhysicalMemory(uintptr_t physicalAddress, size_t length) {
#ifdef ARCH_X86_COMMON
	if ((uintptr_t) physicalAddress + (uintptr_t) length < (uintptr_t) LOW_MEMORY_LIMIT) {
		return (void *) (LOW_MEMORY_MAP_START + physicalAddress);
	}
#endif

	void *address = MMMapPhysical(kernelMMSpace, physicalAddress, length, MM_REGION_NOT_CACHEABLE);
	return address;
}

void KPS2SafeToInitialise() {
	// TODO Qemu sets this to true?
#if 0
	if (acpi.ps2ControllerUnavailable) {
		return;
	}
#endif

	// This is only called when either:
	// - the PCI driver determines there are no USB controllers
	// - the USB controller disables USB emulation
	KThreadCreate("InitPS2", [] (uintptr_t) { KDeviceAttachByName(acpi.computer, "PS2"); });
}

void *ACPIGetRSDP() {
	return acpi.rsdp;
}

uint8_t ACPIGetCenturyRegisterIndex() {
	return acpi.centuryRegisterIndex;
}

void ArchInitialise() {
	uint64_t uefiRSDP = *((uint64_t *) (LOW_MEMORY_MAP_START + GetBootloaderInformationOffset() + 0x7FE8));

	if (!uefiRSDP) {
		acpi.rsdp = ACPIFindRootSystemDescriptorPointer();
	} else {
		acpi.rsdp = (RootSystemDescriptorPointer *) MMMapPhysical(kernelMMSpace, (uintptr_t) uefiRSDP, 16384, ES_FLAGS_DEFAULT);
	}

	ACPIDescriptorTable *madtHeader = nullptr;
	ACPIDescriptorTable *sdt = nullptr; 
	bool isXSDT = false;

	if (acpi.rsdp) {
		if (acpi.rsdp->revision == 2 && acpi.rsdp->xsdtAddress) {
			isXSDT = true;
			sdt = (ACPIDescriptorTable *) acpi.rsdp->xsdtAddress;
		} else {
			isXSDT = false;
			sdt = (ACPIDescriptorTable *) (uintptr_t) acpi.rsdp->rsdtAddress;
		}

		sdt = (ACPIDescriptorTable *) MMMapPhysical(kernelMMSpace, (uintptr_t) sdt, 16384, ES_FLAGS_DEFAULT);
	} else {
		KernelPanic("ACPIInitialise - Could not find supported root system descriptor pointer.\nACPI support is required.\n");
	}

	if (((sdt->signature == SIGNATURE_XSDT && isXSDT) || (sdt->signature == SIGNATURE_RSDT && !isXSDT)) 
			&& sdt->length < 16384 && !EsMemorySumBytes((uint8_t *) sdt, sdt->length)) {
		// The SDT is valid.
	} else {
		KernelPanic("ACPIInitialise - Could not find a valid or supported system descriptor table.\nACPI support is required.\n");
	}

	size_t tablesCount = (sdt->length - sizeof(ACPIDescriptorTable)) >> (isXSDT ? 3 : 2);

	if (tablesCount < 1) {
		KernelPanic("ACPIInitialise - The system descriptor table contains an unsupported number of tables (%d).\n", tablesCount);
	} 

	uintptr_t tableListAddress = (uintptr_t) sdt + ACPI_DESCRIPTOR_TABLE_HEADER_LENGTH;

	KernelLog(LOG_INFO, "ACPI", "table count", "ACPIInitialise - Found %d tables.\n", tablesCount);

	for (uintptr_t i = 0; i < tablesCount; i++) {
		uintptr_t address;

		if (isXSDT) {
			address = ((uint64_t *) tableListAddress)[i];
		} else {
			address = ((uint32_t *) tableListAddress)[i];
		}

		ACPIDescriptorTable *header = (ACPIDescriptorTable *) MMMapPhysical(kernelMMSpace, address, sizeof(ACPIDescriptorTable), ES_FLAGS_DEFAULT);

		KernelLog(LOG_INFO, "ACPI", "table enumerated", "ACPIInitialise - Found ACPI table '%s'.\n", 4, &header->signature);

		if (header->signature == SIGNATURE_MADT) {
			madtHeader = (ACPIDescriptorTable *) MMMapPhysical(kernelMMSpace, address, header->length, ES_FLAGS_DEFAULT);
			ACPICheckTable(madtHeader);
		} else if (header->signature == SIGNATURE_FADT) {
			ACPIDescriptorTable *fadt = (ACPIDescriptorTable *) MMMapPhysical(kernelMMSpace, address, header->length, ES_FLAGS_DEFAULT);
			ACPICheckTable(fadt);
			
			if (header->length > 109) {
				acpi.centuryRegisterIndex = ((uint8_t *) fadt)[108];
				uint8_t bootArchitectureFlags = ((uint8_t *) fadt)[109];
				acpi.ps2ControllerUnavailable = ~bootArchitectureFlags & (1 << 1);
				acpi.vgaControllerUnavailable =  bootArchitectureFlags & (1 << 2);
				KernelLog(LOG_INFO, "ACPI", "FADT", "PS/2 controller is %z; VGA controller is %z.\n",
						acpi.ps2ControllerUnavailable ? "unavailble" : "present",
						acpi.vgaControllerUnavailable ? "unavailble" : "present");
			}

			MMFree(kernelMMSpace, fadt);
		} else if (header->signature == SIGNATURE_HPET) {
			ACPIDescriptorTable *hpet = (ACPIDescriptorTable *) MMMapPhysical(kernelMMSpace, address, header->length, ES_FLAGS_DEFAULT);
			ACPICheckTable(hpet);
			
			if (header->length > 52 && ((uint8_t *) header)[52] == 0) {
				uint64_t baseAddress;
				EsMemoryCopy(&baseAddress, (uint8_t *) header + 44, sizeof(uint64_t));
				KernelLog(LOG_INFO, "ACPI", "HPET", "Found primary HPET with base address %x.\n", baseAddress);
				acpi.hpetBaseAddress = (uint64_t *) MMMapPhysical(kernelMMSpace, baseAddress, 1024, ES_FLAGS_DEFAULT);

				if (acpi.hpetBaseAddress) {
					acpi.hpetBaseAddress[2] |= 1; // Start the main counter.

					acpi.hpetPeriod = acpi.hpetBaseAddress[0] >> 32;
					uint8_t revisionID = acpi.hpetBaseAddress[0] & 0xFF;
					uint64_t initialCount = acpi.hpetBaseAddress[30];

					KernelLog(LOG_INFO, "ACPI", "HPET", "HPET has period of %d fs, revision ID %d, and initial count %d.\n",
							acpi.hpetPeriod, revisionID, initialCount);
				}
			}

			MMFree(kernelMMSpace, hpet);
		}

		MMFree(kernelMMSpace, header);
	}

	// Set up the APIC.
	
	MultipleAPICDescriptionTable *madt = (MultipleAPICDescriptionTable *) ((uint8_t *) madtHeader + ACPI_DESCRIPTOR_TABLE_HEADER_LENGTH);

	if (!madt) {
		KernelPanic("ACPIInitialise - Could not find the MADT table.\nThis is required to use the APIC.\n");
	}

	uintptr_t length = madtHeader->length - ACPI_DESCRIPTOR_TABLE_HEADER_LENGTH - sizeof(MultipleAPICDescriptionTable);
	uintptr_t startLength = length;
	uint8_t *data = (uint8_t *) (madt + 1);

	acpi.lapicAddress = (uint32_t volatile *) ACPIMapPhysicalMemory(madt->lapicAddress, 0x10000);

	while (length && length <= startLength) {
		uint8_t entryType = data[0];
		uint8_t entryLength = data[1];

		switch (entryType) {
			case 0: {
				// A processor and its LAPIC.
				if ((data[4] & 1) == 0) goto nextEntry;
				ArchCPU *processor = acpi.processors + acpi.processorCount;
				processor->processorID = data[2];
				processor->apicID = data[3];
				acpi.processorCount++;
			} break;

			case 1: {
				// An I/O APIC.
				acpi.ioApics[acpi.ioapicCount].id = data[2];
				acpi.ioApics[acpi.ioapicCount].address = (uint32_t volatile *) ACPIMapPhysicalMemory(((uint32_t *) data)[1], 0x10000);
				ACPIIoApicReadRegister(&acpi.ioApics[acpi.ioapicCount], 0); // Make sure it's mapped.
				acpi.ioApics[acpi.ioapicCount].gsiBase = ((uint32_t *) data)[2];
				acpi.ioapicCount++;
			} break;

			case 2: {
				// An interrupt source override structure.
				acpi.interruptOverrides[acpi.interruptOverrideCount].sourceIRQ = data[3];
				acpi.interruptOverrides[acpi.interruptOverrideCount].gsiNumber = ((uint32_t *) data)[1];
				acpi.interruptOverrides[acpi.interruptOverrideCount].activeLow = (data[8] & 2) ? true : false;
				acpi.interruptOverrides[acpi.interruptOverrideCount].levelTriggered = (data[8] & 8) ? true : false;
				KernelLog(LOG_INFO, "ACPI", "interrupt override", "ACPIInitialise - Source IRQ %d is mapped to GSI %d%z%z.\n",
						acpi.interruptOverrides[acpi.interruptOverrideCount].sourceIRQ,
						acpi.interruptOverrides[acpi.interruptOverrideCount].gsiNumber,
						acpi.interruptOverrides[acpi.interruptOverrideCount].activeLow ? ", active low" : ", active high",
						acpi.interruptOverrides[acpi.interruptOverrideCount].levelTriggered ? ", level triggered" : ", edge triggered");
				acpi.interruptOverrideCount++;
			} break;

			case 4: {
				// A non-maskable interrupt.
				acpi.lapicNMIs[acpi.lapicNMICount].processor = data[2];
				acpi.lapicNMIs[acpi.lapicNMICount].lintIndex = data[5];
				acpi.lapicNMIs[acpi.lapicNMICount].activeLow = (data[3] & 2) ? true : false;
				acpi.lapicNMIs[acpi.lapicNMICount].levelTriggered = (data[3] & 8) ? true : false;
				acpi.lapicNMICount++;
			} break;

			default: {
				KernelLog(LOG_ERROR, "ACPI", "unrecognised MADT entry", "ACPIInitialise - Found unknown entry of type %d in MADT\n", entryType);
			} break;
		}

		nextEntry:
		length -= entryLength;
		data += entryLength;
	}

	if (acpi.processorCount > 256 || acpi.ioapicCount > 16 || acpi.interruptOverrideCount > 256 || acpi.lapicNMICount > 32) {
		KernelPanic("ACPIInitialise - Invalid number of processors (%d/%d), \n"
			    "                    I/O APICs (%d/%d), interrupt overrides (%d/%d)\n"
			    "                    and LAPIC NMIs (%d/%d)\n", 
			    acpi.processorCount, 256, acpi.ioapicCount, 16, acpi.interruptOverrideCount, 256, acpi.lapicNMICount, 32);
	}

	uint8_t bootstrapLapicID = (ACPILapicReadRegister(0x20 >> 2) >> 24); 

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
	ACPILapicWriteRegister(0x380 >> 2, (uint32_t) -1); 
	for (int i = 0; i < 8; i++) ArchDelay1Ms(); // Average over 8ms
	acpi.lapicTicksPerMs = ((uint32_t) -1 - ACPILapicReadRegister(0x390 >> 2)) >> 4;
	EsRandomAddEntropy(ACPILapicReadRegister(0x390 >> 2));
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

void ACPIStartupApplicationProcessors() {
#ifdef USE_SMP
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
		ACPILapicWriteRegister(0x310 >> 2, processor->apicID << 24);
		ACPILapicWriteRegister(0x300 >> 2, 0x4500);
		ProcessorEnableInterrupts();
		KEventWait(&delay, 10);

		// Send a startup IPI.
		ProcessorDisableInterrupts();
		ACPILapicWriteRegister(0x310 >> 2, processor->apicID << 24);
		ACPILapicWriteRegister(0x300 >> 2, 0x4600 | (AP_TRAMPOLINE >> K_PAGE_BITS));
		ProcessorEnableInterrupts();
		for (uintptr_t i = 0; i < 100 && *startupFlag == 0; i++) KEventWait(&delay, 1);

		if (*startupFlag) {
			// The processor started correctly.
		} else {
			// Send a startup IPI, again.
			ProcessorDisableInterrupts();
			ACPILapicWriteRegister(0x310 >> 2, processor->apicID << 24);
			ACPILapicWriteRegister(0x300 >> 2, 0x4600 | (AP_TRAMPOLINE >> K_PAGE_BITS));
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
#endif
}

size_t KGetCPUCount() {
	return acpi.processorCount;
}

CPULocalStorage *KGetCPULocal(uintptr_t index) {
	return acpi.processors[index].local;
}

#ifdef USE_ACPICA
#include "acpica.cpp"
#else
void ArchShutdown(uintptr_t action) {
	if (action == SHUTDOWN_ACTION_RESTART) ProcessorReset();
	StartDebugOutput();
	EsPrint("\nIt's now safe to turn off your computer.\n");
	ProcessorDisableInterrupts();
	ProcessorHalt();
}
#endif

void ACPIDeviceAttach(KDevice *parentDevice) {
	acpi.computer = KDeviceCreate("ACPI computer", parentDevice, sizeof(KDevice));

	KThreadCreate("InitACPI", [] (uintptr_t) { 
		KDeviceAttachByName(acpi.computer, "RTC");
#ifdef USE_ACPICA
		ACPICAInitialise(); 
#endif
		ACPIStartupApplicationProcessors();
	});

	if (!acpi.vgaControllerUnavailable) {
		KDeviceAttachByName(acpi.computer, "SVGA");
	}

	KDeviceAttachByName(acpi.computer, "PCI");
}

KDriver driverACPI = {
	.attach = ACPIDeviceAttach,
};
