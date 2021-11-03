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

void ACPICheckTable(const ACPIDescriptorTable *table) {
	if (!EsMemorySumBytes((uint8_t *) table, table->length)) {
		return;
	}

	KernelPanic("ACPICheckTable - ACPI table with signature %s had invalid checksum: "
			"length: %D, ID = %s, table = %s, OEM revision = %d, creator = %s, creator revision = %d.\n",
			4, &table->signature, table->length, 8, &table->id, 8, &table->tableID, 
			table->oemRevision, 4, &table->creatorID, table->creatorRevision);
}

void *ACPIMapPhysicalMemory(uintptr_t physicalAddress, size_t length) {
	return MMMapPhysical(kernelMMSpace, physicalAddress, length, MM_REGION_NOT_CACHEABLE);
}

void KPS2SafeToInitialise() {
	// This is only called when either:
	// - the PCI driver determines there are no USB controllers
	// - the USB controller disables USB emulation

	// TODO Qemu sets this to true?
#if 0
	if (acpi.ps2ControllerUnavailable) {
		return;
	}
#endif

	KThreadCreate("InitPS2", [] (uintptr_t) { 
		KDeviceAttachByName(acpi.computer, "PS2"); 
	});
}

void *ACPIGetRSDP() {
	return acpi.rsdp;
}

uint8_t ACPIGetCenturyRegisterIndex() {
	return acpi.centuryRegisterIndex;
}

void ACPIParseTables() {
	acpi.rsdp = (RootSystemDescriptorPointer *) MMMapPhysical(kernelMMSpace, ArchFindRootSystemDescriptorPointer(), 16384, ES_FLAGS_DEFAULT);

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

	MultipleAPICDescriptionTable *madt = (MultipleAPICDescriptionTable *) ((uint8_t *) madtHeader + ACPI_DESCRIPTOR_TABLE_HEADER_LENGTH);

	if (!madt) {
		KernelPanic("ACPIInitialise - Could not find the MADT table.\nThis is required to use the APIC.\n");
	}

	uintptr_t length = madtHeader->length - ACPI_DESCRIPTOR_TABLE_HEADER_LENGTH - sizeof(MultipleAPICDescriptionTable);
	uintptr_t startLength = length;
	uint8_t *data = (uint8_t *) (madt + 1);

#ifdef ES_ARCH_X86_64
	acpi.lapicAddress = (uint32_t volatile *) ACPIMapPhysicalMemory(madt->lapicAddress, 0x10000);
#endif

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
void ArchShutdown() {
	if (shutdownAction == SHUTDOWN_ACTION_RESTART) ProcessorReset();
	StartDebugOutput();
	EsPrint("\nIt's now safe to turn off your computer.\n");
	ProcessorDisableInterrupts();
	ProcessorHalt();
}

EsError KACPIObjectSetDeviceNotificationHandler(KACPIObject *, KACPINotificationHandler, EsGeneric) {
	return ES_ERROR_UNSUPPORTED_FEATURE;
}

EsError KACPIObjectEvaluateInteger(KACPIObject *, const char *, uint64_t *) {
	return ES_ERROR_UNSUPPORTED_FEATURE;
}

EsError KACPIObjectEvaluateMethodWithInteger(KACPIObject *, const char *, uint64_t) {
	return ES_ERROR_UNSUPPORTED_FEATURE;
}
#endif

void ACPIDeviceAttach(KDevice *parentDevice) {
	acpi.computer = KDeviceCreate("ACPI computer", parentDevice, sizeof(KDevice));

	KThreadCreate("InitACPI", [] (uintptr_t) { 
		KDeviceAttachByName(acpi.computer, "RTC");
#ifdef USE_ACPICA
		ACPICAInitialise(); 
#endif
#ifdef USE_SMP
		ArchStartupApplicationProcessors();
#endif
	});

	if (!acpi.vgaControllerUnavailable) {
		KDeviceAttachByName(acpi.computer, "SVGA");
	}

	KDeviceAttachByName(acpi.computer, "PCI");
}

KDriver driverACPI = {
	.attach = ACPIDeviceAttach,
};
