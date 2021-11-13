// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

// TODO Warning: Not all of the OSL has been tested.

extern "C"  {
#pragma GCC diagnostic ignored "-Wunused-parameter" push
#include <ports/acpica/include/acpi.h>
#pragma GCC diagnostic pop
}

struct ACPICAEvent {
	ACPI_OSD_EXEC_CALLBACK function;
	void *context;
};

// TODO Can these arrays be made smaller?
Thread *acpiEvents[256];
size_t acpiEventCount;
ACPI_OSD_HANDLER acpiInterruptHandlers[256];
void *acpiInterruptContexts[256];
uint8_t acpicaPageBuffer[K_PAGE_SIZE];
KMutex acpicaPageBufferMutex;
char acpiPrintf[4096];
bool acpiOSLayerActive = false;
KAsyncTask powerButtonAsyncTask;

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
	return ArchFindRootSystemDescriptorPointer();
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

void RunACPICAEvent(void *e) {
	ACPICAEvent *event = (ACPICAEvent *) e;
	event->function(event->context);
	EsHeapFree(event, 0, K_FIXED);
}

ES_EXTERN_C ACPI_STATUS AcpiOsExecute(ACPI_EXECUTE_TYPE type, ACPI_OSD_EXEC_CALLBACK function, void *context) {
	(void) type;

	if (!function) return AE_BAD_PARAMETER;

	ACPICAEvent *event = (ACPICAEvent *) EsHeapAllocate(sizeof(ACPICAEvent), true, K_FIXED);
	event->function = function;
	event->context = context;

	Thread *thread = ThreadSpawn("ACPICAEvent", (uintptr_t) RunACPICAEvent, (uintptr_t) event);

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

ES_EXTERN_C void AcpiOsPrintf(const char *format, ...) {
	va_list arguments;
	va_start(arguments, format);
	int x = stbsp_vsnprintf(acpiPrintf, sizeof(acpiPrintf), format, arguments);
	EsPrint("%s", x, acpiPrintf);
	va_end(arguments);
}

ES_EXTERN_C void AcpiOsVprintf(const char *format, va_list arguments) {
	int x = stbsp_vsnprintf(acpiPrintf, sizeof(acpiPrintf), format, arguments);
	EsPrint("%s", x, acpiPrintf);
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
	KRegisterAsyncTask(&powerButtonAsyncTask, [] (KAsyncTask *) {
		_EsMessageWithObject m = { nullptr, ES_MSG_POWER_BUTTON_PRESSED };
		if (scheduler.shutdown) return;
		if (desktopProcess) desktopProcess->messageQueue.SendMessage(&m);
	});

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
					ArchSetPCIIRQLine(table->Address >> 16, table->Pin, irq);
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

void ArchShutdown() {
	if (shutdownAction == SHUTDOWN_ACTION_RESTART) ProcessorReset();
	AcpiEnterSleepStatePrep(5);
	ProcessorDisableInterrupts();
	AcpiEnterSleepState(5);
}

void ACPICAInitialise() {
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
}
