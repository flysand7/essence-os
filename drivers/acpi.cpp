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
#define ACPI_DESCRIPTOR_TABLE_HEADER_LENGTH 36
	uint32_t signature;
	uint32_t length;
	uint64_t id;
	uint64_t tableID;
	uint32_t oemRevision;
	uint32_t creatorID;
	uint32_t creatorRevision;

	void Check() {
		if (!EsMemorySumBytes((uint8_t *) this, length)) return;

		KernelPanic("ACPI::Initialise - ACPI table with signature %s had invalid checksum: "
				"length: %D, ID = %s, table = %s, OEM revision = %d, creator = %s, creator revision = %d.\n",
				4, &signature, length, 8, &id, 8, &tableID, 
				oemRevision, 4, &creatorID, creatorRevision);
	}
};

struct MultipleAPICDescriptionTable {
	uint32_t lapicAddress; 
	uint32_t flags;
};

struct ACPIProcessor {
	uint8_t processorID, kernelProcessorID;
	uint8_t apicID;
	bool bootstrapProcessor;
	void **kernelStack;
	CPULocalStorage *local;
};

struct ACPIIoApic {
	uint32_t ReadRegister(uint32_t reg);
	void WriteRegister(uint32_t reg, uint32_t value);

	uint8_t id;
	uint32_t volatile *address;
	uint32_t gsiBase;
};

uint32_t ACPIIoApic::ReadRegister(uint32_t reg) {
	address[0] = reg; 
	return address[4];
}

void ACPIIoApic::WriteRegister(uint32_t reg, uint32_t value) {
	address[0] = reg; 
	address[4] = value;
}

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

struct ACPILapic {
	uint32_t ReadRegister(uint32_t reg);
	void EndOfInterrupt();
	void WriteRegister(uint32_t reg, uint32_t value);
	void ArchNextTimer(size_t ms);

	volatile uint32_t *address;
	size_t ticksPerMs;
};

struct ACPI {
	void Initialise();
	void FindRootSystemDescriptorPointer();
	void StartupApplicationProcessors();

	size_t processorCount;
	size_t ioapicCount;
	size_t interruptOverrideCount;
	size_t lapicNMICount;

	ACPIProcessor processors[256]; // TODO Make this a DS_ARRAY.
	ACPIProcessor *bootstrapProcessor;
	ACPIIoApic ioApics[16];
	ACPIInterruptOverride interruptOverrides[256];
	ACPILapicNMI lapicNMIs[32];
	ACPILapic lapic;

	RootSystemDescriptorPointer *rsdp;
	ACPIDescriptorTable *sdt; bool isXSDT;
	ACPIDescriptorTable *madt;

	bool ps2ControllerUnavailable, vgaControllerUnavailable;
	uint8_t centuryRegisterIndex;

	volatile uint64_t *hpetBaseAddress;
	uint64_t hpetPeriod; // 10^-15 seconds.

	KDevice *computer;
};

ACPI acpi;

void ACPILapic::ArchNextTimer(size_t ms) {
	WriteRegister(0x320 >> 2, TIMER_INTERRUPT | (1 << 17)); 
	WriteRegister(0x380 >> 2, ticksPerMs * ms); 
}

void ACPILapic::EndOfInterrupt() {
	WriteRegister(0xB0 >> 2, 0);
}

uint32_t ACPILapic::ReadRegister(uint32_t reg) {
	return address[reg];
}

void ACPILapic::WriteRegister(uint32_t reg, uint32_t value) {
	address[reg] = value;
}

#ifdef ARCH_X86_COMMON
uint64_t ArchGetTimeMs() {
	if (acpi.hpetBaseAddress && acpi.hpetPeriod) {
		__int128 fsToMs = 1000000000000;
		__int128 reading = acpi.hpetBaseAddress[30];
		return (uint64_t) (reading * (__int128) acpi.hpetPeriod / fsToMs);
	} else {
		return ArchGetTimeFromPITMs();
	}
}

void ACPI::FindRootSystemDescriptorPointer() {
	PhysicalMemoryRegion searchRegions[2];

	searchRegions[0].baseAddress = (uintptr_t) (((uint16_t *) LOW_MEMORY_MAP_START)[0x40E] << 4) + LOW_MEMORY_MAP_START;
	searchRegions[0].pageCount = 0x400;
	searchRegions[1].baseAddress = (uintptr_t) 0xE0000 + LOW_MEMORY_MAP_START;
	searchRegions[1].pageCount = 0x20000;

	for (uintptr_t i = 0; i < 2; i++) {
		for (uintptr_t address = searchRegions[i].baseAddress;
				address < searchRegions[i].baseAddress + searchRegions[i].pageCount;
				address += 16) {
			rsdp = (RootSystemDescriptorPointer *) address;

			if (rsdp->signature != SIGNATURE_RSDP) {
				continue;
			}

			if (rsdp->revision == 0) {
				if (EsMemorySumBytes((uint8_t *) rsdp, 20)) {
					continue;
				}

				return;
			} else if (rsdp->revision == 2) {
				if (EsMemorySumBytes((uint8_t *) rsdp, sizeof(RootSystemDescriptorPointer))) {
					continue;
				}

				return;
			}
		}
	}

	// We didn't find the RSDP.
	rsdp = nullptr;
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

#ifdef USE_ACPICA

// TODO Warning: Not all of the OSL has been tested.

extern "C"  {
#pragma GCC diagnostic ignored "-Wunused-parameter" push
#include <ports/acpica/include/acpi.h>
#pragma GCC diagnostic pop
}

bool acpiOSLayerActive = false;

ES_EXTERN_C ACPI_STATUS AcpiOsInitialize() {
	if (acpiOSLayerActive) KernelPanic("AcpiOsInitialize - ACPI has already been initialised.\n");
	acpiOSLayerActive = true;
	KernelLog(LOG_INFO, "ACPI", "initialise ACPICA", "AcpiOsInitialize - Initialising ACPICA OS layer...\n");
	return AE_OK;
}

ES_EXTERN_C ACPI_STATUS AcpiOsTerminate() {
	if (!acpiOSLayerActive) KernelPanic("AcpiOsTerminate - ACPI has not been initialised.\n");
	acpiOSLayerActive = false;
	KernelLog(LOG_INFO, "ACPI", "terminate ACPICA", "AcpiOsTerminate - Terminating ACPICA OS layer...\n");
	return AE_OK;
}

ES_EXTERN_C ACPI_PHYSICAL_ADDRESS AcpiOsGetRootPointer() {
	ACPI_PHYSICAL_ADDRESS address = 0;

	uint64_t uefiRSDP = *((uint64_t *) (LOW_MEMORY_MAP_START + GetBootloaderInformationOffset() + 0x7FE8));

	if (uefiRSDP) {
		return uefiRSDP;
	}

	AcpiFindRootPointer(&address);
	return address;
}

ES_EXTERN_C ACPI_STATUS AcpiOsPredefinedOverride(const ACPI_PREDEFINED_NAMES *predefinedObject, ACPI_STRING *newValue) {
	(void) predefinedObject;
	*newValue = nullptr;
	return AE_OK;
}

ES_EXTERN_C ACPI_STATUS AcpiOsTableOverride(ACPI_TABLE_HEADER *existingTable, ACPI_TABLE_HEADER **newTable) {
	(void) existingTable;
	*newTable = nullptr;
	return AE_OK;
}

ES_EXTERN_C ACPI_STATUS AcpiOsPhysicalTableOverride(ACPI_TABLE_HEADER *existingTable, ACPI_PHYSICAL_ADDRESS *newAddress, uint32_t *newTableLength) {
	(void) existingTable;
	*newAddress = 0;
	*newTableLength = 0;
	return AE_OK;
}

ES_EXTERN_C void *AcpiOsMapMemory(ACPI_PHYSICAL_ADDRESS physicalAddress, ACPI_SIZE length) {
	return ACPIMapPhysicalMemory(physicalAddress, length);
}

ES_EXTERN_C void AcpiOsUnmapMemory(void *address, ACPI_SIZE length) {
#ifdef ARCH_X86_COMMON
	if ((uintptr_t) address - (uintptr_t) LOW_MEMORY_MAP_START < (uintptr_t) LOW_MEMORY_LIMIT) {
		return;
	}
#endif

	(void) length;
	MMFree(kernelMMSpace, address);
}

ES_EXTERN_C ACPI_STATUS AcpiOsGetPhysicalAddress(void *virtualAddress, ACPI_PHYSICAL_ADDRESS *physicalAddress) {
	if (!virtualAddress || !physicalAddress) {
		return AE_BAD_PARAMETER;
	}

	*physicalAddress = MMArchTranslateAddress(kernelMMSpace, (uintptr_t) virtualAddress);
	return AE_OK;
}

ES_EXTERN_C void *AcpiOsAllocate(ACPI_SIZE size) {
	return EsHeapAllocate(size, false, K_FIXED);
}

ES_EXTERN_C void AcpiOsFree(void *memory) {
	EsHeapFree(memory, 0, K_FIXED);
}

ES_EXTERN_C BOOLEAN AcpiOsReadable(void *memory, ACPI_SIZE length) {
	(void) memory;
	(void) length;
	// This is only used by the debugger, which we don't use...
	return TRUE;
}

ES_EXTERN_C BOOLEAN AcpiOsWritable(void *memory, ACPI_SIZE length) {
	(void) memory;
	(void) length;
	// This is only used by the debugger, which we don't use...
	return TRUE;
}

ES_EXTERN_C ACPI_THREAD_ID AcpiOsGetThreadId() {
	return GetCurrentThread()->id + 1;
}

Thread *acpiEvents[256];
size_t acpiEventCount;

struct ACPICAEvent {
	ACPI_OSD_EXEC_CALLBACK function;
	void *context;
};

void RunACPICAEvent(void *e) {
	ACPICAEvent *event = (ACPICAEvent *) e;
	event->function(event->context);
	EsHeapFree(event, 0, K_FIXED);
	scheduler.TerminateThread(GetCurrentThread());
}

ES_EXTERN_C ACPI_STATUS AcpiOsExecute(ACPI_EXECUTE_TYPE type, ACPI_OSD_EXEC_CALLBACK function, void *context) {
	(void) type;

	if (!function) return AE_BAD_PARAMETER;

	ACPICAEvent *event = (ACPICAEvent *) EsHeapAllocate(sizeof(ACPICAEvent), true, K_FIXED);
	event->function = function;
	event->context = context;

	Thread *thread = scheduler.SpawnThread("ACPICAEvent", (uintptr_t) RunACPICAEvent, (uintptr_t) event);

	if (acpiEventCount == 256) {
		KernelPanic("AcpiOsExecute - Exceeded maximum event count, 256.\n");
	}

	if (thread) {
		acpiEvents[acpiEventCount++] = thread;
		return AE_OK;
	} else {
		return AE_NO_MEMORY;
	}
}

ES_EXTERN_C void AcpiOsSleep(UINT64 ms) {
	KEvent event = {};
	KEventWait(&event, ms);
}

ES_EXTERN_C void AcpiOsStall(UINT32 mcs) {
	(void) mcs;
	uint64_t start = ProcessorReadTimeStamp();
	uint64_t end = start + mcs * (timeStampTicksPerMs / 1000);
	while (ProcessorReadTimeStamp() < end);
}

ES_EXTERN_C void AcpiOsWaitEventsComplete() {
	for (uintptr_t i = 0; i < acpiEventCount; i++) {
		Thread *thread = acpiEvents[i];
		KEventWait(&thread->killedEvent, ES_WAIT_NO_TIMEOUT);
		CloseHandleToObject(thread, KERNEL_OBJECT_THREAD);
	}

	acpiEventCount = 0;
}

ES_EXTERN_C ACPI_STATUS AcpiOsCreateSemaphore(UINT32 maxUnits, UINT32 initialUnits, ACPI_SEMAPHORE *handle) {
	if (!handle) return AE_BAD_PARAMETER;

	KSemaphore *semaphore = (KSemaphore *) EsHeapAllocate(sizeof(KSemaphore), true, K_FIXED);
	KSemaphoreReturn(semaphore, initialUnits);
	semaphore->_custom = maxUnits;
	*handle = semaphore;
	return AE_OK;
}

ES_EXTERN_C ACPI_STATUS AcpiOsDeleteSemaphore(ACPI_SEMAPHORE handle) {
	if (!handle) return AE_BAD_PARAMETER;
	EsHeapFree(handle, sizeof(KSemaphore), K_FIXED);
	return AE_OK;
}

ES_EXTERN_C ACPI_STATUS AcpiOsWaitSemaphore(ACPI_SEMAPHORE handle, UINT32 units, UINT16 timeout) {
	(void) timeout;
	if (!handle) return AE_BAD_PARAMETER;
	KSemaphore *semaphore = (KSemaphore *) handle;

	if (KSemaphoreTake(semaphore, units, timeout == (UINT16) -1 ? ES_WAIT_NO_TIMEOUT : timeout)) {
		return AE_OK;
	} else {
		return AE_TIME;
	}
}

ES_EXTERN_C ACPI_STATUS AcpiOsSignalSemaphore(ACPI_SEMAPHORE handle, UINT32 units) {
	if (!handle) return AE_BAD_PARAMETER;
	KSemaphore *semaphore = (KSemaphore *) handle;
	if (semaphore->units + units > semaphore->_custom) return AE_LIMIT;
	KSemaphoreReturn(semaphore, units);
	return AE_OK;
}

ES_EXTERN_C ACPI_STATUS AcpiOsCreateLock(ACPI_SPINLOCK *handle) {
	if (!handle) return AE_BAD_PARAMETER;
	KSpinlock *spinlock = (KSpinlock *) EsHeapAllocate(sizeof(KSpinlock), true, K_FIXED);
	*handle = spinlock;
	return AE_OK;
}

ES_EXTERN_C void AcpiOsDeleteLock(ACPI_HANDLE handle) {
	EsHeapFree(handle, sizeof(KSpinlock), K_FIXED);
}

ES_EXTERN_C ACPI_CPU_FLAGS AcpiOsAcquireLock(ACPI_SPINLOCK handle) {
	KSpinlock *spinlock = (KSpinlock *) handle;
	KSpinlockAcquire(spinlock);
	return 0;
}

ES_EXTERN_C void AcpiOsReleaseLock(ACPI_SPINLOCK handle, ACPI_CPU_FLAGS flags) {
	(void) flags;
	KSpinlock *spinlock = (KSpinlock *) handle;
	KSpinlockRelease(spinlock);
}

// TODO Can these arrays be made smaller?
ACPI_OSD_HANDLER acpiInterruptHandlers[256];
void *acpiInterruptContexts[256];

bool ACPIInterrupt(uintptr_t interruptIndex, void *) {
	if (acpiInterruptHandlers[interruptIndex]) {
		return ACPI_INTERRUPT_HANDLED == acpiInterruptHandlers[interruptIndex](acpiInterruptContexts[interruptIndex]);
	} else {
		return false;
	}
}

ES_EXTERN_C ACPI_STATUS AcpiOsInstallInterruptHandler(UINT32 interruptLevel, ACPI_OSD_HANDLER handler, void *context) {
	if (interruptLevel > 256 || !handler) return AE_BAD_PARAMETER;
	
	if (acpiInterruptHandlers[interruptLevel]) {
		return AE_ALREADY_EXISTS;
	}

	acpiInterruptHandlers[interruptLevel] = handler;
	acpiInterruptContexts[interruptLevel] = context;

	return KRegisterIRQ(interruptLevel, ACPIInterrupt, nullptr, "ACPICA") ? AE_OK : AE_ERROR;
}

ES_EXTERN_C ACPI_STATUS AcpiOsRemoveInterruptHandler(UINT32 interruptNumber, ACPI_OSD_HANDLER handler) {
	if (interruptNumber > 256 || !handler) return AE_BAD_PARAMETER;

	if (!acpiInterruptHandlers[interruptNumber]) {
		return AE_NOT_EXIST;
	}

	if (handler != acpiInterruptHandlers[interruptNumber]) {
		return AE_BAD_PARAMETER;
	}

	acpiInterruptHandlers[interruptNumber] = nullptr;

	return AE_OK;
}

uint8_t acpicaPageBuffer[K_PAGE_SIZE];
KMutex acpicaPageBufferMutex;

ES_EXTERN_C ACPI_STATUS AcpiOsReadMemory(ACPI_PHYSICAL_ADDRESS address, UINT64 *value, UINT32 width) {
	KMutexAcquire(&acpicaPageBufferMutex);
	EsDefer(KMutexRelease(&acpicaPageBufferMutex));

	uintptr_t page = (uintptr_t) address & ~(K_PAGE_SIZE - 1);
	uintptr_t offset = (uintptr_t) address & (K_PAGE_SIZE - 1);

	PMRead(page, acpicaPageBuffer, 1);
	
	if (width == 64) {
		*value = *((uint64_t *) (acpicaPageBuffer + offset));
	} else if (width == 32) {
		*value = *((uint32_t *) (acpicaPageBuffer + offset));
	} else if (width == 16) {
		*value = *((uint16_t *) (acpicaPageBuffer + offset));
	} else {
		*value = acpicaPageBuffer[offset];
	}

	return AE_OK;
}

ES_EXTERN_C ACPI_STATUS AcpiOsWriteMemory(ACPI_PHYSICAL_ADDRESS address, UINT64 value, UINT32 width) {
	KMutexAcquire(&acpicaPageBufferMutex);
	EsDefer(KMutexRelease(&acpicaPageBufferMutex));

	uintptr_t page = (uintptr_t) address & ~(K_PAGE_SIZE - 1);
	uintptr_t offset = (uintptr_t) address & (K_PAGE_SIZE - 1);

	PMRead(page, acpicaPageBuffer, 1);
	
	if (width == 64) {
		*((uint64_t *) (acpicaPageBuffer + offset)) = value;
	} else if (width == 32) {
		*((uint32_t *) (acpicaPageBuffer + offset)) = value;
	} else if (width == 16) {
		*((uint16_t *) (acpicaPageBuffer + offset)) = value;
	} else {
		*((uint8_t *) (acpicaPageBuffer + offset)) = value;
	}

	PMCopy(page, acpicaPageBuffer, 1);

	return AE_OK;
}

ES_EXTERN_C ACPI_STATUS AcpiOsReadPort(ACPI_IO_ADDRESS address, UINT32 *value, UINT32 width) {
	// EsPrint("AcpiOsReadPort - %x, %d", address, width);
		
	if (width == 8) {
		*value = ProcessorIn8(address);
	} else if (width == 16) {
		*value = ProcessorIn16(address);
	} else if (width == 32) {
		*value = ProcessorIn32(address);
	} else {
		return AE_ERROR;
	}

	// EsPrint(" - %x\n", *value);

	return AE_OK;
}

ES_EXTERN_C ACPI_STATUS AcpiOsWritePort(ACPI_IO_ADDRESS address, UINT32 value, UINT32 width) {
	// EsPrint("AcpiOsWritePort - %x, %x, %d", address, value, width);

	if (width == 8) {
		ProcessorOut8(address, (uint8_t) value);
	} else if (width == 16) {
		ProcessorOut16(address, (uint16_t) value);
	} else if (width == 32) {
		ProcessorOut32(address, (uint32_t) value);
	} else {
		return AE_ERROR;
	}

	// EsPrint(" - ;;\n");

	return AE_OK;
}

ES_EXTERN_C ACPI_STATUS AcpiOsReadPciConfiguration(ACPI_PCI_ID *address, UINT32 reg, UINT64 *value, UINT32 width) {
	if (width == 64) {
		uint64_t x = (uint64_t) KPCIReadConfig(address->Bus, address->Device, address->Function, reg)
			| ((uint64_t) KPCIReadConfig(address->Bus, address->Device, address->Function, reg + 4) << 32);
		*value = x;
	} else {
		uint32_t x = KPCIReadConfig(address->Bus, address->Device, address->Function, reg & ~3);
		x >>= (reg & 3) * 8;

		if (width == 8) x &= 0xFF;
		if (width == 16) x &= 0xFFFF;

		*value = x;
	}

	return AE_OK;
}

ES_EXTERN_C ACPI_STATUS AcpiOsWritePciConfiguration(ACPI_PCI_ID *address, UINT32 reg, UINT64 value, UINT32 width) {
	if (width == 64) {
		KPCIWriteConfig(address->Bus, address->Device, address->Function, reg, value);
		KPCIWriteConfig(address->Bus, address->Device, address->Function, reg + 4, value >> 32);
	} else if (width == 32) {
		KPCIWriteConfig(address->Bus, address->Device, address->Function, reg, value);
	} else {
		uint32_t x = KPCIReadConfig(address->Bus, address->Device, address->Function, reg & ~3);
		uint32_t o = reg & 3;

		if (width == 16) {
			if (o == 2) {
				x = (x & ~0xFFFF0000) | (value << 16);
			} else {
				x = (x & ~0x0000FFFF) | (value << 0);
			}
		} else if (width == 8) {
			if (o == 3) {
				x = (x & ~0xFF000000) | (value << 24);
			} else if (o == 2) {
				x = (x & ~0x00FF0000) | (value << 16);
			} else if (o == 1) {
				x = (x & ~0x0000FF00) | (value << 8);
			} else {
				x = (x & ~0x000000FF) | (value << 0);
			}
		}

		KPCIWriteConfig(address->Bus, address->Device, address->Function, reg & ~3, x);
	}

	return AE_OK;
}

char acpiPrintf[4096];

#if 1
#define ENABLE_ACPICA_OUTPUT
#endif

ES_EXTERN_C void AcpiOsPrintf(const char *format, ...) {
	va_list arguments;
	va_start(arguments, format);
	int x = stbsp_vsnprintf(acpiPrintf, sizeof(acpiPrintf), format, arguments);
#ifdef ENABLE_ACPICA_OUTPUT
	EsPrint("%s", x, acpiPrintf);
#else 
	(void) x;
#endif
	va_end(arguments);
}

ES_EXTERN_C void AcpiOsVprintf(const char *format, va_list arguments) {
	int x = stbsp_vsnprintf(acpiPrintf, sizeof(acpiPrintf), format, arguments);
#ifdef ENABLE_ACPICA_OUTPUT
	EsPrint("%s", x, acpiPrintf);
#else 
	(void) x;
#endif
}

ES_EXTERN_C UINT64 AcpiOsGetTimer() {
	uint64_t tick = ProcessorReadTimeStamp();
	uint64_t ticksPerMs = timeStampTicksPerMs;
	uint64_t ticksPer100Ns = ticksPerMs / 1000 / 10;
	if (ticksPer100Ns == 0) return tick;
	return tick / ticksPer100Ns;
}

ES_EXTERN_C ACPI_STATUS AcpiOsSignal(UINT32 function, void *information) {
	(void) function;
	(void) information;
	KernelPanic("AcpiOsSignal - ACPI requested kernel panic.\n");
	return AE_OK;
}

ES_EXTERN_C ACPI_STATUS AcpiOsEnterSleep(UINT8 sleepState, UINT32 registerAValue, UINT32 registerBValue) {
	(void) sleepState;
	(void) registerAValue;
	(void) registerBValue;
	return AE_OK;
}

UINT32 ACPIPowerButtonPressed(void *) {
	KRegisterAsyncTask([] (EsGeneric) {
		_EsMessageWithObject m = { nullptr, ES_MSG_POWER_BUTTON_PRESSED };
		if (scheduler.shutdown) return;
		if (desktopProcess) desktopProcess->messageQueue.SendMessage(&m);
	}, nullptr, false);

	return 0;
}

int32_t ACPIFindIRQ(ACPI_HANDLE object) {
	ACPI_BUFFER buffer = {};
	ACPI_STATUS status = AcpiGetCurrentResources(object, &buffer);
	if (status != AE_BUFFER_OVERFLOW) return -1;
	buffer.Pointer = EsHeapAllocate(buffer.Length, false, K_FIXED);
	EsDefer(EsHeapFree(buffer.Pointer, buffer.Length, K_FIXED));
	if (!buffer.Pointer) return -1;
	status = AcpiGetCurrentResources(object, &buffer);
	if (status != AE_OK) return -1;
	ACPI_RESOURCE *resource = (ACPI_RESOURCE *) buffer.Pointer;

	while (resource->Type != ACPI_RESOURCE_TYPE_END_TAG) {
		if (resource->Type == ACPI_RESOURCE_TYPE_IRQ) {
			if (resource->Data.Irq.InterruptCount) {
				return resource->Data.Irq.Interrupts[0];
			}
		} else if (resource->Type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ) {
			if (resource->Data.ExtendedIrq.InterruptCount) {
				return resource->Data.ExtendedIrq.Interrupts[0];
			}
		}

		resource = (ACPI_RESOURCE *) ((uint8_t *) resource + resource->Length);
	}

	return -1;
}

void ACPIEnumeratePRTEntries(ACPI_HANDLE pciBus) {
	// TODO Other PCI buses.
	// TODO Is this always bus 0?

	ACPI_BUFFER buffer = {};
	ACPI_STATUS status = AcpiGetIrqRoutingTable(pciBus, &buffer);
	if (status != AE_BUFFER_OVERFLOW) return;
	buffer.Pointer = EsHeapAllocate(buffer.Length, false, K_FIXED);
	EsDefer(EsHeapFree(buffer.Pointer, buffer.Length, K_FIXED));
	if (!buffer.Pointer) return;
	status = AcpiGetIrqRoutingTable(pciBus, &buffer);
	if (status != AE_OK) return;
	ACPI_PCI_ROUTING_TABLE *table = (ACPI_PCI_ROUTING_TABLE *) buffer.Pointer;

	while (table->Length) {
		ACPI_HANDLE source;

		if (AE_OK == AcpiGetHandle(pciBus, table->Source, &source)) {
			int32_t irq = ACPIFindIRQ(source);

			if (irq != -1) {
				KernelLog(LOG_INFO, "ACPI", "PRT entry", "Pin: %d; PCI slot: %X; IRQ: %d\n",
						table->Pin, (table->Address >> 16) & 0xFF, irq);

				if (irq != 9 && irq != 10 && irq != 11) {
					KernelLog(LOG_ERROR, "ACPI", "unexpected IRQ", "IRQ %d was unexpected; expected values are 9, 10 or 11.\n", irq);
				} else if ((table->Address >> 16) > 0xFF) {
					KernelLog(LOG_ERROR, "ACPI", "unexpected address", "Address %x was larger than expected.\n", table->Address);
				} else if (table->Pin > 3) {
					KernelLog(LOG_ERROR, "ACPI", "unexpected pin", "Pin %d was larger than expected.\n", table->Pin);
				} else {
					pciIRQLines[table->Address >> 16][table->Pin] = irq;
				}
			}
		}

		table = (ACPI_PCI_ROUTING_TABLE *) ((uint8_t *) table + table->Length);
	}
}

struct KACPIObject : KDevice {
	ACPI_HANDLE handle;
	KACPINotificationHandler notificationHandler;
	EsGeneric notificationHandlerContext;
};

void ACPINotificationHandler(ACPI_HANDLE, uint32_t value, void *context) {
	KernelLog(LOG_INFO, "ACPI", "notification", "Received a notification with value %X.\n", value);
	KACPIObject *object = (KACPIObject *) context;
	object->notificationHandler(object, value, object->notificationHandlerContext);
}

EsError KACPIObjectSetDeviceNotificationHandler(KACPIObject *object, KACPINotificationHandler handler, EsGeneric context) {
	object->notificationHandler = handler;
	object->notificationHandlerContext = context;
	ACPI_STATUS status = AcpiInstallNotifyHandler(object->handle, ACPI_DEVICE_NOTIFY, ACPINotificationHandler, object);
	if (status == AE_OK) return ES_SUCCESS;
	else if (status == AE_NO_MEMORY) return ES_ERROR_INSUFFICIENT_RESOURCES;
	else return ES_ERROR_UNKNOWN;
}

EsError KACPIObjectEvaluateInteger(KACPIObject *object, const char *pathName, uint64_t *_integer) {
	ACPI_BUFFER buffer = {};
	buffer.Length = ACPI_ALLOCATE_BUFFER;

	ACPI_STATUS status = AcpiEvaluateObject(object->handle, (char *) pathName, nullptr, &buffer);
	EsError error = ES_SUCCESS;

	if (status == AE_OK) {
		ACPI_OBJECT *result = (ACPI_OBJECT *) buffer.Pointer;

		if (result->Type == ACPI_TYPE_INTEGER) {
			if (_integer) {
				*_integer = result->Integer.Value;
			}
		} else {
			error = ES_ERROR_UNKNOWN;
		}

		ACPI_FREE(buffer.Pointer);
	} else if (status == AE_NO_MEMORY) {
		error = ES_ERROR_INSUFFICIENT_RESOURCES;
	} else if (status == AE_NOT_FOUND) {
		error = ES_ERROR_FILE_DOES_NOT_EXIST;
	} else {
		error = ES_ERROR_UNKNOWN;
	}

	return error;
}

EsError KACPIObjectEvaluateMethodWithInteger(KACPIObject *object, const char *pathName, uint64_t integer) {
	ACPI_OBJECT argument = {};
	argument.Type = ACPI_TYPE_INTEGER;
	argument.Integer.Value = integer;
	ACPI_OBJECT_LIST argumentList = {};
	argumentList.Count = 1;
	argumentList.Pointer = &argument;
	ACPI_STATUS status = AcpiEvaluateObject(object->handle, (char *) pathName, &argumentList, nullptr);
	if (status == AE_OK) return ES_SUCCESS;
	else if (status == AE_NO_MEMORY) return ES_ERROR_INSUFFICIENT_RESOURCES;
	else if (status == AE_NOT_FOUND) return ES_ERROR_FILE_DOES_NOT_EXIST;
	else return ES_ERROR_UNKNOWN;
}

ACPI_STATUS ACPIWalkNamespaceCallback(ACPI_HANDLE object, uint32_t depth, void *, void **) {
	ACPI_DEVICE_INFO *information;
	AcpiGetObjectInfo(object, &information);

	char name[5];
	EsMemoryCopy(name, &information->Name, 4);
	name[4] = 0;

	if (information->Type == ACPI_TYPE_DEVICE) {
		KernelLog(LOG_INFO, "ACPI", "device object", "Found device object '%z' at depth %d with HID '%z', UID '%z' and address %x.\n",
				name, depth,
				(information->Valid & ACPI_VALID_HID) ? information->HardwareId.String : "??",
				(information->Valid & ACPI_VALID_UID) ? information->UniqueId.String : "??",
				(information->Valid & ACPI_VALID_ADR) ? information->Address : 0);
	}

	if (information->Type == ACPI_TYPE_THERMAL) {
		KACPIObject *device = (KACPIObject *) KDeviceCreate("ACPI object", acpi.computer, sizeof(KACPIObject));

		if (device) {
			device->handle = object;
			KDeviceAttachByName(device, "ACPIThermal");
		}
	}

	ACPI_FREE(information);
	return AE_OK;
}
#endif

void ACPIInitialise2() {
#ifdef USE_ACPICA
	AcpiInitializeSubsystem();
	AcpiInitializeTables(nullptr, 256, true);
	AcpiLoadTables();
	AcpiEnableSubsystem(ACPI_FULL_INITIALIZATION);
	AcpiInitializeObjects(ACPI_FULL_INITIALIZATION);

	if (AE_OK == AcpiEnableEvent(ACPI_EVENT_POWER_BUTTON, 0)
			&& AE_OK == AcpiInstallFixedEventHandler(ACPI_EVENT_POWER_BUTTON, ACPIPowerButtonPressed, nullptr)) {
		KDeviceCreate("ACPI power button", acpi.computer, sizeof(KDevice));
	}

	void *result;
	AcpiWalkNamespace(ACPI_TYPE_ANY, ACPI_ROOT_OBJECT, 10, ACPIWalkNamespaceCallback, nullptr, nullptr, &result);

	ACPI_HANDLE pciBus;
	char pciBusPath[] = "\\_SB_.PCI0";

	if (AE_OK == AcpiGetHandle(nullptr, pciBusPath, &pciBus)) {
		ACPIEnumeratePRTEntries(pciBus);
	}
#endif

	acpi.StartupApplicationProcessors();
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

static void DeviceAttach(KDevice *parentDevice) {
	acpi.computer = KDeviceCreate("ACPI computer", parentDevice, sizeof(KDevice));

#ifndef SERIAL_STARTUP
	KThreadCreate("InitACPI", [] (uintptr_t) { ACPIInitialise2(); });
#else
	ACPIInitialise2();
#endif

	if (!acpi.vgaControllerUnavailable) {
		KDeviceAttachByName(acpi.computer, "SVGA"); // TODO Remove from the startup critical path.
	}

	KDeviceAttachByName(acpi.computer, "PCI");
	KDeviceAttachByName(acpi.computer, "RTC"); // TODO Remove from the startup critical path.
}

KDriver driverACPI = {
	.attach = DeviceAttach,
};

void *ACPIGetRSDP() {
	return acpi.rsdp;
}

uint8_t ACPIGetCenturyRegisterIndex() {
	return acpi.centuryRegisterIndex;
}

inline void ArchInitialise() {
	acpi.Initialise();
}

#ifdef USE_ACPICA
void ArchShutdown(uintptr_t action) {
	if (action == SHUTDOWN_ACTION_RESTART) ArchResetCPU();
	AcpiEnterSleepStatePrep(5);
	ProcessorDisableInterrupts();
	AcpiEnterSleepState(5);
}
#else
void ArchShutdown(uintptr_t action) {
	if (action == SHUTDOWN_ACTION_RESTART) ArchResetCPU();
	StartDebugOutput();
	EsPrint("\nIt's now safe to turn off your computer.\n");
	ProcessorDisableInterrupts();
	ProcessorHalt();
}
#endif

void ACPI::Initialise() {
	uint64_t uefiRSDP = *((uint64_t *) (LOW_MEMORY_MAP_START + GetBootloaderInformationOffset() + 0x7FE8));

	if (!uefiRSDP) {
#ifdef USE_ACPICA
		AcpiFindRootPointer((ACPI_PHYSICAL_ADDRESS *) &uefiRSDP);
		rsdp = (RootSystemDescriptorPointer *) MMMapPhysical(kernelMMSpace, (uintptr_t) uefiRSDP, 16384, ES_FLAGS_DEFAULT);
#else
		FindRootSystemDescriptorPointer();
#endif
	} else {
		rsdp = (RootSystemDescriptorPointer *) MMMapPhysical(kernelMMSpace, (uintptr_t) uefiRSDP, 16384, ES_FLAGS_DEFAULT);
	}

	if (rsdp) {
		if (rsdp->revision == 2 && rsdp->xsdtAddress) {
			isXSDT = true;
			sdt = (ACPIDescriptorTable *) rsdp->xsdtAddress;
		} else {
			isXSDT = false;
			sdt = (ACPIDescriptorTable *) (uintptr_t) rsdp->rsdtAddress;
		}

		sdt = (ACPIDescriptorTable *) MMMapPhysical(kernelMMSpace, (uintptr_t) sdt, 16384, ES_FLAGS_DEFAULT);
	} else {
		KernelPanic("ACPI::Initialise - Could not find supported root system descriptor pointer.\nACPI support is required.\n");
	}

	if (((sdt->signature == SIGNATURE_XSDT && isXSDT) || (sdt->signature == SIGNATURE_RSDT && !isXSDT)) 
			&& sdt->length < 16384 && !EsMemorySumBytes((uint8_t *) sdt, sdt->length)) {
		size_t tablesCount = (sdt->length - sizeof(ACPIDescriptorTable)) >> (isXSDT ? 3 : 2);

		if (tablesCount < 1) {
			KernelPanic("ACPI::Initialise - The system descriptor table contains an unsupported number of tables (%d).\n", tablesCount);
		} 

		uintptr_t tableListAddress = (uintptr_t) sdt + ACPI_DESCRIPTOR_TABLE_HEADER_LENGTH;

		KernelLog(LOG_INFO, "ACPI", "table count", "ACPI::Initialise - Found %d tables.\n", tablesCount);

		for (uintptr_t i = 0; i < tablesCount; i++) {
			uintptr_t address;

			if (isXSDT) {
				address = ((uint64_t *) tableListAddress)[i];
			} else {
				address = ((uint32_t *) tableListAddress)[i];
			}

			ACPIDescriptorTable *header = (ACPIDescriptorTable *) MMMapPhysical(kernelMMSpace, address, sizeof(ACPIDescriptorTable), ES_FLAGS_DEFAULT);

			KernelLog(LOG_INFO, "ACPI", "table enumerated", "ACPI::Initialise - Found ACPI table '%s'.\n", 4, &header->signature);

			if (header->signature == SIGNATURE_MADT) {
				madt = (ACPIDescriptorTable *) MMMapPhysical(kernelMMSpace, address, header->length, ES_FLAGS_DEFAULT);
				madt->Check();
			} else if (header->signature == SIGNATURE_FADT) {
				ACPIDescriptorTable *fadt = (ACPIDescriptorTable *) MMMapPhysical(kernelMMSpace, address, header->length, ES_FLAGS_DEFAULT);
				fadt->Check();
				
				if (header->length > 109) {
					centuryRegisterIndex = ((uint8_t *) fadt)[108];
					uint8_t bootArchitectureFlags = ((uint8_t *) fadt)[109];
					ps2ControllerUnavailable = ~bootArchitectureFlags & (1 << 1);
					vgaControllerUnavailable =  bootArchitectureFlags & (1 << 2);
					KernelLog(LOG_INFO, "ACPI", "FADT", "PS/2 controller is %z; VGA controller is %z.\n",
							ps2ControllerUnavailable ? "unavailble" : "present",
							vgaControllerUnavailable ? "unavailble" : "present");
				}

				MMFree(kernelMMSpace, fadt);
			} else if (header->signature == SIGNATURE_HPET) {
				ACPIDescriptorTable *hpet = (ACPIDescriptorTable *) MMMapPhysical(kernelMMSpace, address, header->length, ES_FLAGS_DEFAULT);
				hpet->Check();
				
				if (header->length > 52 && ((uint8_t *) header)[52] == 0) {
					uint64_t baseAddress;
					EsMemoryCopy(&baseAddress, (uint8_t *) header + 44, sizeof(uint64_t));
					KernelLog(LOG_INFO, "ACPI", "HPET", "Found primary HPET with base address %x.\n", baseAddress);
					hpetBaseAddress = (uint64_t *) MMMapPhysical(kernelMMSpace, baseAddress, 1024, ES_FLAGS_DEFAULT);

					if (hpetBaseAddress) {
						hpetBaseAddress[2] |= 1; // Start the main counter.

						hpetPeriod = hpetBaseAddress[0] >> 32;
						uint8_t revisionID = hpetBaseAddress[0] & 0xFF;
						uint64_t initialCount = hpetBaseAddress[30];

						KernelLog(LOG_INFO, "ACPI", "HPET", "HPET has period of %d fs, revision ID %d, and initial count %d.\n",
								hpetPeriod, revisionID, initialCount);
					}
				}

				MMFree(kernelMMSpace, hpet);
			}

			MMFree(kernelMMSpace, header);
		}
	} else {
		KernelPanic("ACPI::Initialise - Could not find a valid or supported system descriptor table.\nACPI support is required.\n");
	}

	// Set up the APIC.
	
	ACPIDescriptorTable *header = this->madt;
	MultipleAPICDescriptionTable *madt = (MultipleAPICDescriptionTable *) ((uint8_t *) this->madt + ACPI_DESCRIPTOR_TABLE_HEADER_LENGTH);

	if (!madt) {
		KernelPanic("ACPI::Initialise - Could not find the MADT table.\nThis is required to use the APIC.\n");
	}

	uintptr_t length = header->length - ACPI_DESCRIPTOR_TABLE_HEADER_LENGTH - sizeof(MultipleAPICDescriptionTable);
	uintptr_t startLength = length;
	uint8_t *data = (uint8_t *) (madt + 1);

	lapic.address = (uint32_t volatile *) ACPIMapPhysicalMemory(madt->lapicAddress, 0x10000);

	while (length && length <= startLength) {
		uint8_t entryType = data[0];
		uint8_t entryLength = data[1];

		switch (entryType) {
			case 0: {
				// A processor and its LAPIC.
				if ((data[4] & 1) == 0) goto nextEntry;
				ACPIProcessor *processor = processors + processorCount;
				processor->processorID = data[2];
				processor->apicID = data[3];
				processorCount++;
			} break;

			case 1: {
				// An I/O APIC.
				ioApics[ioapicCount].id = data[2];
				ioApics[ioapicCount].address = (uint32_t volatile *) ACPIMapPhysicalMemory(((uint32_t *) data)[1], 0x10000);
				ioApics[ioapicCount].ReadRegister(0); // Make sure it's mapped.
				ioApics[ioapicCount].gsiBase = ((uint32_t *) data)[2];
				ioapicCount++;
			} break;

			case 2: {
				// An interrupt source override structure.
				interruptOverrides[interruptOverrideCount].sourceIRQ = data[3];
				interruptOverrides[interruptOverrideCount].gsiNumber = ((uint32_t *) data)[1];
				interruptOverrides[interruptOverrideCount].activeLow = (data[8] & 2) ? true : false;
				interruptOverrides[interruptOverrideCount].levelTriggered = (data[8] & 8) ? true : false;
				KernelLog(LOG_INFO, "ACPI", "interrupt override", "ACPI::Initialise - Source IRQ %d is mapped to GSI %d%z%z.\n",
						interruptOverrides[interruptOverrideCount].sourceIRQ,
						interruptOverrides[interruptOverrideCount].gsiNumber,
						interruptOverrides[interruptOverrideCount].activeLow ? ", active low" : ", active high",
						interruptOverrides[interruptOverrideCount].levelTriggered ? ", level triggered" : ", edge triggered");
				interruptOverrideCount++;
			} break;

			case 4: {
				// A non-maskable interrupt.
				lapicNMIs[lapicNMICount].processor = data[2];
				lapicNMIs[lapicNMICount].lintIndex = data[5];
				lapicNMIs[lapicNMICount].activeLow = (data[3] & 2) ? true : false;
				lapicNMIs[lapicNMICount].levelTriggered = (data[3] & 8) ? true : false;
				lapicNMICount++;
			} break;

			default: {
				KernelLog(LOG_ERROR, "ACPI", "unrecognised MADT entry", "ACPI::Initialise - Found unknown entry of type %d in MADT\n", entryType);
			} break;
		}

		nextEntry:
		length -= entryLength;
		data += entryLength;
	}

	if (processorCount > 256 || ioapicCount > 16 || interruptOverrideCount > 256 || lapicNMICount > 32) {
		KernelPanic("ACPI::KernelPanic - Invalid number of processors (%d/%d), \n"
			    "                    I/O APICs (%d/%d), interrupt overrides (%d/%d)\n"
			    "                    and LAPIC NMIs (%d/%d)\n", 
			    processorCount, 256, ioapicCount, 16, interruptOverrideCount, 256, lapicNMICount, 32);
	}

	uint8_t bootstrapLapicID = (lapic.ReadRegister(0x20 >> 2) >> 24); 

	for (uintptr_t i = 0; i < processorCount; i++) {
		if (processors[i].apicID == bootstrapLapicID) {
			// That's us!
			bootstrapProcessor = processors + i;
			bootstrapProcessor->bootstrapProcessor = true;
		}
	}

	if (!bootstrapProcessor) {
		KernelPanic("ACPI::Initialise - Could not find the bootstrap processor\n");
	}

	// Calibrate the LAPIC's timer and processor's timestamp counter.
	ProcessorDisableInterrupts();
	uint64_t start = ProcessorReadTimeStamp();
	acpi.lapic.WriteRegister(0x380 >> 2, (uint32_t) -1); 
	for (int i = 0; i < 8; i++) ArchDelay1Ms(); // Average over 8ms
	acpi.lapic.ticksPerMs = ((uint32_t) -1 - acpi.lapic.ReadRegister(0x390 >> 2)) >> 4;
	EsRandomAddEntropy(acpi.lapic.ReadRegister(0x390 >> 2));
	uint64_t end = ProcessorReadTimeStamp();
	timeStampTicksPerMs = (end - start) >> 3;
	ProcessorEnableInterrupts();
	// EsPrint("timeStampTicksPerMs = %d\n", timeStampTicksPerMs);

	// Finish processor initialisation.
	// This sets up interrupts, the timer, CPULocalStorage, the GDT and TSS,
	// and registers the processor with the scheduler.

	for (uintptr_t i = 0; i <= acpi.processorCount; i++) {
		if (i == acpi.processorCount) {
			KernelPanic("ACPI::Initialise - Could not find the bootstrap processor to perform second-stage initialisation.\n");
		}

		if (acpi.processors[i].bootstrapProcessor) {
			NewProcessorStorage storage = AllocateNewProcessorStorage(acpi.processors + i);
			SetupProcessor2(&storage);
			break;
		}
	}
}

void Wait1Ms() {
	if (scheduler.started) {
		KEvent event = {};
		KEventWait(&event, 1);
	} else {
		ArchDelay1Ms();
	}
}

void ACPI::StartupApplicationProcessors() {
#ifdef USE_SMP
	// TODO How do we know that this address is usable?
#define AP_TRAMPOLINE 0x10000

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

	for (uintptr_t i = 0; i < processorCount; i++) {
		ACPIProcessor *processor = processors + i;
		if (processor->bootstrapProcessor) continue;

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
		lapic.WriteRegister(0x310 >> 2, processor->apicID << 24);
		lapic.WriteRegister(0x300 >> 2, 0x4500);
		ProcessorEnableInterrupts();
		for (uintptr_t i = 0; i < 10; i++) Wait1Ms();

		// Send a startup IPI.
		ProcessorDisableInterrupts();
		lapic.WriteRegister(0x310 >> 2, processor->apicID << 24);
		lapic.WriteRegister(0x300 >> 2, 0x4600 | (AP_TRAMPOLINE >> K_PAGE_BITS));
		ProcessorEnableInterrupts();
		for (uintptr_t i = 0; i < 100 && *startupFlag == 0; i++) Wait1Ms();

		if (*startupFlag) {
			// The processor started correctly.
		} else {
			// Send a startup IPI, again.
			ProcessorDisableInterrupts();
			lapic.WriteRegister(0x310 >> 2, processor->apicID << 24);
			lapic.WriteRegister(0x300 >> 2, 0x4600 | (AP_TRAMPOLINE >> K_PAGE_BITS));
			ProcessorEnableInterrupts();
			for (uintptr_t i = 0; i < 1000 && *startupFlag == 0; i++) Wait1Ms(); // Wait longer this time.

			if (*startupFlag) {
				// The processor started correctly.
			} else {
				// The processor could not be started.
				KernelLog(LOG_ERROR, "ACPI", "processor startup failure", 
						"ACPI::Initialise - Could not start processor %d\n", processor->processorID);
				continue;
			}
		}

		// EsPrint("Startup flag 1 reached!\n");

		for (uintptr_t i = 0; i < 10000 && *startupFlag != 2; i++) Wait1Ms();

		if (*startupFlag == 2) {
			// The processor started!
		} else {
			// The processor did not report it completed initilisation, worringly.
			// Don't let it continue.

			KernelLog(LOG_ERROR, "ACPI", "processor startup failure", 
					"ACPI::Initialise - Could not initialise processor %d\n", processor->processorID);

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
