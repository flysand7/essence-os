// TODO Warn on Read/WriteBAR if port IO/memory space access is disabled in the command register.

#include <module.h>

struct PCIController : KDevice {
#define PCI_BUS_DO_NOT_SCAN 0
#define PCI_BUS_SCAN_NEXT 1
#define PCI_BUS_SCANNED 2
	uint8_t busScanStates[256];
	
	bool foundUSBController;

	void Enumerate();
	void EnumerateFunction(int bus, int device, int function, int *busesToScan);
};

const char *classCodeStrings[] = {
	"Unknown",
	"Mass storage controller",
	"Network controller",
	"Display controller",
	"Multimedia controller",
	"Memory controller",
	"Bridge controller",
	"Simple communication controller",
	"Base system peripheral",
	"Input device controller",
	"Docking station",
	"Processor",
	"Serial bus controller",
	"Wireless controller",
	"Intelligent controller",
	"Satellite communication controller",
	"Encryption controller",
	"Signal processing controller",
};

const char *subclassCodeStrings1[] = {
	"SCSI bus controller",
	"IDE controller",
	"Floppy disk controller",
	"IPI bus controller",
	"RAID controller",
	"ATA controller",
	"Serial ATA",
	"Serial attached SCSI",
	"Non-volatile memory controller",
};

const char *subclassCodeStrings12[] = {
	"FireWire (IEEE 1394) controller",
	"ACCESS bus",
	"SSA",
	"USB controller",
	"Fibre channel",
	"SMBus",
	"InfiniBand",
	"IPMI interface",
	"SERCOS interface (IEC 61491)",
	"CANbus",
};

const char *progIFStrings12_3[] = {
	"UHCI",
	"OHCI",
	"EHCI",
	"XHCI",
};

uint8_t KPCIDevice::ReadBAR8(uintptr_t index, uintptr_t offset) {
	uint32_t baseAddress = baseAddresses[index];
	uint8_t result;

	if (baseAddress & 1) {
		result = ProcessorIn8((baseAddress & ~3) + offset);
	} else {
		result = *(volatile uint8_t *) (baseAddressesVirtual[index] + offset);
	}

	KernelLog(LOG_VERBOSE, "PCI", "read BAR", "ReadBAR8 - %x, %x, %x, %x\n", this, index, offset, result);
	return result;
}

void KPCIDevice::WriteBAR8(uintptr_t index, uintptr_t offset, uint8_t value) {
	uint32_t baseAddress = baseAddresses[index];
	KernelLog(LOG_VERBOSE, "PCI", "write BAR", "WriteBAR8 - %x, %x, %x, %x\n", this, index, offset, value);

	if (baseAddress & 1) {
		ProcessorOut8((baseAddress & ~3) + offset, value);
	} else {
		*(volatile uint8_t *) (baseAddressesVirtual[index] + offset) = value;
	}
}

uint16_t KPCIDevice::ReadBAR16(uintptr_t index, uintptr_t offset) {
	uint32_t baseAddress = baseAddresses[index];
	uint16_t result;

	if (baseAddress & 1) {
		result = ProcessorIn16((baseAddress & ~3) + offset);
	} else {
		result = *(volatile uint16_t *) (baseAddressesVirtual[index] + offset);
	}

	KernelLog(LOG_VERBOSE, "PCI", "read BAR", "ReadBAR16 - %x, %x, %x, %x\n", this, index, offset, result);
	return result;
}

void KPCIDevice::WriteBAR16(uintptr_t index, uintptr_t offset, uint16_t value) {
	uint32_t baseAddress = baseAddresses[index];
	KernelLog(LOG_VERBOSE, "PCI", "write BAR", "WriteBAR16 - %x, %x, %x, %x\n", this, index, offset, value);

	if (baseAddress & 1) {
		ProcessorOut16((baseAddress & ~3) + offset, value);
	} else {
		*(volatile uint16_t *) (baseAddressesVirtual[index] + offset) = value;
	}
}

uint32_t KPCIDevice::ReadBAR32(uintptr_t index, uintptr_t offset) {
	uint32_t baseAddress = baseAddresses[index];
	uint32_t result;

	if (baseAddress & 1) {
		result = ProcessorIn32((baseAddress & ~3) + offset);
	} else {
		result = *(volatile uint32_t *) (baseAddressesVirtual[index] + offset);
	}

	KernelLog(LOG_VERBOSE, "PCI", "read BAR", "ReadBAR32 - %x, %x, %x, %x\n", this, index, offset, result);
	return result;
}

void KPCIDevice::WriteBAR32(uintptr_t index, uintptr_t offset, uint32_t value) {
	uint32_t baseAddress = baseAddresses[index];
	KernelLog(LOG_VERBOSE, "PCI", "write BAR", "WriteBAR32 - %x, %x, %x, %x\n", this, index, offset, value);

	if (baseAddress & 1) {
		ProcessorOut32((baseAddress & ~3) + offset, value);
	} else {
		*(volatile uint32_t *) (baseAddressesVirtual[index] + offset) = value;
	}
}

uint64_t KPCIDevice::ReadBAR64(uintptr_t index, uintptr_t offset) {
	uint32_t baseAddress = baseAddresses[index];
	uint64_t result;

	if (baseAddress & 1) {
		result = (uint64_t) ReadBAR32(index, offset) | ((uint64_t) ReadBAR32(index, offset + 4) << 32);
	} else {
		result = *(volatile uint64_t *) (baseAddressesVirtual[index] + offset);
	}

	KernelLog(LOG_VERBOSE, "PCI", "read BAR", "ReadBAR64 - %x, %x, %x, %x\n", this, index, offset, result);
	return result;
}

void KPCIDevice::WriteBAR64(uintptr_t index, uintptr_t offset, uint64_t value) {
	uint32_t baseAddress = baseAddresses[index];
	KernelLog(LOG_VERBOSE, "PCI", "write BAR", "WriteBAR64 - %x, %x, %x, %x\n", this, index, offset, value);

	if (baseAddress & 1) {
		WriteBAR32(index, offset, value & 0xFFFFFFFF);
		WriteBAR32(index, offset + 4, (value >> 32) & 0xFFFFFFFF);
	} else {
		*(volatile uint64_t *) (baseAddressesVirtual[index] + offset) = value;
	}
}

static PCIController *pci;

void KPCIDevice::WriteConfig8(uintptr_t offset, uint8_t value) {
	KPCIWriteConfig(bus, slot, function, offset, value, 8);
}

uint8_t KPCIDevice::ReadConfig8(uintptr_t offset) {
	return KPCIReadConfig(bus, slot, function, offset, 8);
}

void KPCIDevice::WriteConfig16(uintptr_t offset, uint16_t value) {
	KPCIWriteConfig(bus, slot, function, offset, value, 16);
}

uint16_t KPCIDevice::ReadConfig16(uintptr_t offset) {
	return KPCIReadConfig(bus, slot, function, offset, 16);
}

void KPCIDevice::WriteConfig32(uintptr_t offset, uint32_t value) {
	KPCIWriteConfig(bus, slot, function, offset, value, 32);
}

uint32_t KPCIDevice::ReadConfig32(uintptr_t offset) {
	return KPCIReadConfig(bus, slot, function, offset, 32);
}

bool KPCIDevice::EnableSingleInterrupt(KIRQHandler irqHandler, void *context, const char *cOwnerName) {
	if (EnableMSI(irqHandler, context, cOwnerName)) {
		return true;
	}

	if (interruptPin == 0) {
		// The device does not support interrupts.
		return false;
	}

	if (interruptPin > 4) {
		KernelLog(LOG_ERROR, "PCI", "bad interrupt pin", "Interrupt pin should be between 0 and 4; got %d.\n", interruptPin);
		return false;
	}

	EnableFeatures(K_PCI_FEATURE_INTERRUPTS);

	// If we booted from EFI, we need to get the interrupt line from ACPI.
	// See the comment in InterruptHandler for what happens when passing -1.

	intptr_t line = interruptLine;
#if defined(ES_ARCH_X86_32) || defined(ES_ARCH_X86_64)
	extern uint32_t bootloaderID;
	if (bootloaderID == 2) line = -1;
#endif

	if (KRegisterIRQ(line, irqHandler, context, cOwnerName, this)) {
		return true;
	}

	return false;
}

bool KPCIDevice::EnableMSI(KIRQHandler irqHandler, void *context, const char *cOwnerName) {
	// Find the MSI capability.

	uint16_t status = ReadConfig32(0x04) >> 16;

	if (~status & (1 << 4)) {
		KernelLog(LOG_ERROR, "PCI", "no MSI support", "Device does not support MSI.\n");
		return false;
	}

	uint8_t pointer = ReadConfig8(0x34);
	uintptr_t index = 0;

	while (pointer && index++ < 0xFF) {
		uint32_t dw = ReadConfig32(pointer);
		uint8_t nextPointer = (dw >> 8) & 0xFF;
		uint8_t id = dw & 0xFF;

		if (id != 5) {
			pointer = nextPointer;
			continue;
		}

		KMSIInformation msi = KRegisterMSI(irqHandler, context, cOwnerName);

		if (!msi.address) {
			KernelLog(LOG_ERROR, "PCI", "register MSI failure", "Could not register MSI.\n");
			return false;
		}

		uint16_t control = (dw >> 16) & 0xFFFF;

		if (msi.data & ~0xFFFF) {
			KUnregisterMSI(msi.tag);
			KernelLog(LOG_ERROR, "PCI", "unsupported MSI data", "PCI only supports 16 bits of MSI data. Requested: %x.\n", msi.data);
			return false;
		}

		if (msi.address & 3) {
			KUnregisterMSI(msi.tag);
			KernelLog(LOG_ERROR, "PCI", "unsupported MSI address", "PCI requires DWORD alignment of MSI address. Requested: %x.\n", msi.address);
			return false;
		}

#ifdef ES_BITS_64
		if ((msi.address & 0xFFFFFFFF00000000) && (~control & (1 << 7))) {
			KUnregisterMSI(msi.tag);
			KernelLog(LOG_ERROR, "PCI", "unsupported MSI address", "MSI does not support 64-bit addresses. Requested: %x.\n", msi.address);
			return false;
		}
#endif

		control = (control & ~(7 << 4) /* don't allow modifying data */) 
			| (1 << 0 /* enable MSI */);
		dw = (dw & 0x0000FFFF) | (control << 16);

		WriteConfig32(pointer + 0, dw);
		WriteConfig32(pointer + 4, msi.address & 0xFFFFFFFF);

		if (control & (1 << 7)) {
			WriteConfig32(pointer + 8, ES_PTR64_MS32(msi.address));
			WriteConfig16(pointer + 12, (ReadConfig16(pointer + 12) & 0x3800) | msi.data);
			if (control & (1 << 8)) WriteConfig32(pointer + 16, 0);
		} else {
			WriteConfig16(pointer + 8, msi.data);
			if (control & (1 << 8)) WriteConfig32(pointer + 12, 0);
		}

		return true;
	}

	KernelLog(LOG_ERROR, "PCI", "no MSI support", "Device does not support MSI (2).\n");
	return false;
}

bool KPCIDevice::EnableFeatures(uint64_t features) {
	uint32_t config = ReadConfig32(4);
	if (features & K_PCI_FEATURE_INTERRUPTS) 		config &= ~(1 << 10);
	if (features & K_PCI_FEATURE_BUSMASTERING_DMA) 		config |= 1 << 2;
	if (features & K_PCI_FEATURE_MEMORY_SPACE_ACCESS) 	config |= 1 << 1;
	if (features & K_PCI_FEATURE_IO_PORT_ACCESS)		config |= 1 << 0;
	WriteConfig32(4, config);

	if (ReadConfig32(4) != config) {
		KernelLog(LOG_ERROR, "PCI", "configuration update", "Could not update the configuration for device %x.\n", this);
		return false; 
	}

	for (int i = 0; i < 6; i++) {
		if (~features & (1 << i)) {
			continue;
		}

		if (baseAddresses[i] & 1) {
			continue; // The BAR is an IO port.
		}

		bool size64 = baseAddresses[i] & 4;

		if (!(baseAddresses[i] & 8)) {
			// TODO Not prefetchable.
		}

		uint64_t address, size;

		if (size64) {
			WriteConfig32(0x10 + 4 * i, 0xFFFFFFFF);
			WriteConfig32(0x10 + 4 * (i + 1), 0xFFFFFFFF);
			size = ReadConfig32(0x10 + 4 * i);
			size |= (uint64_t) ReadConfig32(0x10 + 4 * (i + 1)) << 32;
			WriteConfig32(0x10 + 4 * i, baseAddresses[i]);
			WriteConfig32(0x10 + 4 * (i + 1), baseAddresses[i + 1]);
			address = baseAddresses[i];
			address |= (uint64_t) baseAddresses[i + 1] << 32;
		} else {
			WriteConfig32(0x10 + 4 * i, 0xFFFFFFFF);
			size = ReadConfig32(0x10 + 4 * i);
			size |= (uint64_t) 0xFFFFFFFF << 32;
			WriteConfig32(0x10 + 4 * i, baseAddresses[i]);
			address = baseAddresses[i];
		}

		if (!size || !address) {
			KernelLog(LOG_ERROR, "PCI", "enable device BAR", "Could not enable BAR %d for device %x.\n", i, this);
			return false;
		}

		size &= ~15;
		size = ~size + 1;
		address &= ~15;

		// TODO Do we sometimes have to allocate the physical address ourselves..?

		// If the driver wants to allow WC caching later, it can, with MMAllowWriteCombiningCaching.
		baseAddressesVirtual[i] = (uint8_t *) MMMapPhysical(MMGetKernelSpace(), address, size, MM_REGION_NOT_CACHEABLE);
		baseAddressesPhysical[i] = address;
		baseAddressesSizes[i] = size;

		MMCheckUnusable(address, size);

		KernelLog(LOG_INFO, "PCI", "enable device BAR", "BAR %d has address %x and size %x, mapped to %x.\n",
				i, address, size, baseAddressesVirtual[i]);
	}

	return true;
}

bool EnumeratePCIDrivers(KInstalledDriver *driver, KDevice *_device) {
	KPCIDevice *device = (KPCIDevice *) _device;

	int classCode = -1, subclassCode = -1, progIF = -1;
	bool foundAnyDeviceIDs = false, foundMatchingDeviceID = false;

	EsINIState s = {};
	s.buffer = driver->config, s.bytes = driver->configBytes;

	while (EsINIParse(&s)) {
		if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("classCode"))) classCode = EsIntegerParse(s.value, s.valueBytes);
		if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("subclassCode"))) subclassCode = EsIntegerParse(s.value, s.valueBytes);
		if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("progIF"))) progIF = EsIntegerParse(s.value, s.valueBytes);

		if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("deviceID"))) {
			foundAnyDeviceIDs = true;
			
			if (device->deviceID == EsIntegerParse(s.value, s.valueBytes)) {
				foundMatchingDeviceID = true;
			}
		}
	}

	if (classCode    != -1 && device->classCode      != classCode)    return false;
	if (subclassCode != -1 && device->subclassCode   != subclassCode) return false;
	if (progIF       != -1 && device->progIF         != progIF)       return false;

	return !foundAnyDeviceIDs || foundMatchingDeviceID;
}

void PCIController::EnumerateFunction(int bus, int device, int function, int *busesToScan) {
	uint32_t deviceID = KPCIReadConfig(bus, device, function, 0x00);
	if ((deviceID & 0xFFFF) == 0xFFFF) return;

	uint32_t deviceClass = KPCIReadConfig(bus, device, function, 0x08);
	uint32_t interruptInformation = KPCIReadConfig(bus, device, function, 0x3C);

	KPCIDevice *pciDevice = (KPCIDevice *) KDeviceCreate("PCI function", this, sizeof(KPCIDevice));
	if (!pciDevice) return;

	pciDevice->classCode = (deviceClass >> 24) & 0xFF;
	pciDevice->subclassCode = (deviceClass >> 16) & 0xFF;
	pciDevice->progIF = (deviceClass >> 8) & 0xFF;

	pciDevice->bus = bus;
	pciDevice->slot = device;
	pciDevice->function = function;
	
	pciDevice->interruptPin = (interruptInformation >> 8) & 0xFF;
	pciDevice->interruptLine = (interruptInformation >> 0) & 0xFF;

	pciDevice->deviceID = KPCIReadConfig(bus, device, function, 0);
	pciDevice->subsystemID = KPCIReadConfig(bus, device, function, 0x2C);

	for (int i = 0; i < 6; i++) {
		pciDevice->baseAddresses[i] = pciDevice->ReadConfig32(0x10 + 4 * i);
	}

	const char *classCodeString = pciDevice->classCode < sizeof(classCodeStrings) / sizeof(classCodeStrings[0]) 
		? classCodeStrings[pciDevice->classCode] : "Unknown";
	const char *subclassCodeString = 
		  pciDevice->classCode == 1  && pciDevice->subclassCode < sizeof(subclassCodeStrings1)  / sizeof(const char *) 
			? subclassCodeStrings1 [pciDevice->subclassCode] 
		: pciDevice->classCode == 12 && pciDevice->subclassCode < sizeof(subclassCodeStrings12) / sizeof(const char *)
			? subclassCodeStrings12[pciDevice->subclassCode] : "";
	const char *progIFString = 
		  pciDevice->classCode == 12 && pciDevice->subclassCode == 3 && pciDevice->progIF / 0x10 < sizeof(progIFStrings12_3) / sizeof(const char *)
			? progIFStrings12_3[pciDevice->progIF / 0x10] : "";

	KernelLog(LOG_INFO, "PCI", "enumerate device", 
			"Found PCI device at %d/%d/%d with ID %x. Class code: %X '%z'. Subclass code: %X '%z'. Prog IF: %X '%z'. Interrupt pin: %d. Interrupt line: %d.\n", 
			bus, device, function, deviceID, 
			pciDevice->classCode, classCodeString, pciDevice->subclassCode, subclassCodeString, pciDevice->progIF, progIFString,
			pciDevice->interruptPin, pciDevice->interruptLine);

	if (pciDevice->classCode == 0x06 && pciDevice->subclassCode == 0x04 /* PCI bridge */) {
		uint8_t secondaryBus = (KPCIReadConfig(bus, device, function, 0x18) >> 8) & 0xFF; 

		if (busScanStates[secondaryBus] == PCI_BUS_DO_NOT_SCAN) {
			KernelLog(LOG_INFO, "PCI", "PCI bridge", "PCI bridge to bus %d.\n", secondaryBus);
			*busesToScan = *busesToScan + 1;
			busScanStates[secondaryBus] = PCI_BUS_SCAN_NEXT;
		}
	}

	bool foundDriver = KDeviceAttach(pciDevice, "PCI", EnumeratePCIDrivers);

	if (foundDriver && pciDevice->classCode == 12 && pciDevice->subclassCode == 3) {
		// The USB controller is responsible for disabling PS/2 emulation, and calling KPS2SafeToInitialise.
		KernelLog(LOG_INFO, "PCI", "found USB", "Found USB controller.\n");
		foundUSBController = true;
	}
}

void PCIController::Enumerate() {
	uint32_t baseHeaderType = KPCIReadConfig(0, 0, 0, 0x0C);
	int baseBuses = (baseHeaderType & 0x80) ? 8 : 1;

	int busesToScan = 0;

	for (int baseBus = 0; baseBus < baseBuses; baseBus++) {
		uint32_t deviceID = KPCIReadConfig(0, 0, baseBus, 0x00);
		if ((deviceID & 0xFFFF) == 0xFFFF) continue;
		busScanStates[baseBus] = PCI_BUS_SCAN_NEXT;
		busesToScan++;
	}

	if (!busesToScan) {
		KernelPanic("PCIController::Enumerate - No buses found\n");
	}

	while (busesToScan) {
		for (int bus = 0; bus < 256; bus++) {
			if (busScanStates[bus] != PCI_BUS_SCAN_NEXT) continue;

			KernelLog(LOG_INFO, "PCI", "scan bus", "Scanning bus %d...\n", bus);

			busScanStates[bus] = PCI_BUS_SCANNED;
			busesToScan--;

			for (int device = 0; device < 32; device++) {
				uint32_t deviceID = KPCIReadConfig(bus, device, 0, 0x00);
				if ((deviceID & 0xFFFF) == 0xFFFF) continue;

				uint32_t headerType = (KPCIReadConfig(bus, device, 0, 0x0C) >> 16) & 0xFF;
				int functions = (headerType & 0x80) ? 8 : 1;

				for (int function = 0; function < functions; function++) {
					EnumerateFunction(bus, device, function, &busesToScan);
				}
			}
		}
	}
}

static void DeviceAttach(KDevice *parent) {
	if (pci) {
		KernelLog(LOG_ERROR, "PCI", "multiple PCI controllers", "EntryPCI - Attempt to register multiple PCI controllers; ignored.\n");
		return;
	}

	pci = (PCIController *) KDeviceCreate("PCI controller", parent, sizeof(PCIController));

	if (pci) {
		pci->Enumerate();

		if (!pci->foundUSBController) {
			KernelLog(LOG_INFO, "PCI", "no USB", "No USB controller found; initialising PS/2...\n");
			KPS2SafeToInitialise();
		}
	}
}

KDriver driverPCI = {
	.attach = DeviceAttach,
};
