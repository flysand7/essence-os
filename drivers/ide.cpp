// TODO Asynchronous timeout.
// TODO Inserting/removing ATAPI devices.

#include <module.h>
#include <kernel/x86_64.h>

#define ATA_BUSES 2
#define ATA_DRIVES (ATA_BUSES * 2)
#define ATA_SECTOR_SIZE (512)
#define ATA_TIMEOUT (10000)
#define ATAPI_SECTOR_SIZE (2048)

#define ATA_REGISTER(_bus, _reg) (_reg != -1 ? ((_bus ? IO_ATA_1 : IO_ATA_2) + _reg) : (_bus ? IO_ATA_3 : IO_ATA_4))
#define ATA_IRQ(_bus) (_bus ? 15 : 14)
#define ATA_DATA 0
#define ATA_FEATURES 1
#define ATA_SECTOR_COUNT 2
#define ATA_LBA1 3
#define ATA_LBA2 4
#define ATA_LBA3 5
#define ATA_DRIVE_SELECT 6
#define ATA_STATUS 7
#define ATA_COMMAND 7
#define ATA_DCR -1
#define ATA_IDENTIFY 0xEC
#define ATA_IDENTIFY_PACKET 0xA1
#define ATA_READ_PIO 0x20
#define ATA_READ_PIO_48 0x24
#define ATA_READ_DMA 0xC8
#define ATA_READ_DMA_48 0x25
#define ATA_WRITE_PIO 0x30
#define ATA_WRITE_PIO_48 0x34
#define ATA_PACKET 0xA0
#define ATA_WRITE_DMA 0xCA
#define ATA_WRITE_DMA_48 0x35

#define DMA_REGISTER(_bus, _reg) 4, ((_bus ? (_reg + 8) : _reg))
#define DMA_COMMAND 0
#define DMA_STATUS 2
#define DMA_PRDT 4

struct PRD {
	volatile uint32_t base;
	volatile uint16_t size;
	volatile uint16_t end;
};

struct ATAOperation {
	void *buffer;
	uintptr_t offsetIntoSector, readIndex;
	size_t countBytes, sectorsNeededToLoad;
	uint8_t operation, readingData, bus, slave;
	bool pio;
};

struct ATAController : KDevice {
	void Initialise();
	bool Access(uintptr_t drive, uint64_t sector, size_t count, int operation, uint8_t *buffer); // Returns true on success.
	bool AccessStart(int bus, int slave, uint64_t sector, uintptr_t offsetIntoSector, size_t sectorsNeededToLoad, size_t countBytes, int operation, uint8_t *buffer, bool atapi);
	bool AccessEnd(int bus, int slave);

	void SetDrive(int bus, int slave, int extra = 0);
	void Unblock();

	KPCIDevice *pci;

	uint64_t sectorCount[ATA_DRIVES];
	bool isATAPI[ATA_DRIVES];

	KSemaphore semaphore; 

	PRD *prdts[ATA_BUSES];
	void *buffers[ATA_BUSES];
	KEvent irqs[ATA_BUSES];

	uint16_t identifyData[ATA_SECTOR_SIZE / 2];

	volatile ATAOperation op;
	
	KMutex blockedPacketsMutex;
};

static ATAController *ataController;

struct ATADrive : KBlockDevice {
	uintptr_t index;
};

void ATAController::SetDrive(int bus, int slave, int extra) {
	ProcessorOut8(ATA_REGISTER(bus, ATA_DRIVE_SELECT), extra | 0xA0 | (slave << 4));
	for (int i = 0; i < 4; i++) ProcessorIn8(ATA_REGISTER(bus, ATA_STATUS));
}

bool ATAController::AccessStart(int bus, int slave, uint64_t sector, uintptr_t offsetIntoSector, size_t sectorsNeededToLoad, size_t countBytes, 
		int operation, uint8_t *_buffer, bool atapi) {
	uint16_t *buffer = (uint16_t *) _buffer;

	bool s48 = false;

	// Start a timeout.
	KTimeout timeout(1000);

	while ((ProcessorIn8(ATA_REGISTER(bus, ATA_STATUS)) & 0x80) && !timeout.Hit());

	if (timeout.Hit()) return false;

	if (atapi) {
		SetDrive(bus, slave);
		ProcessorOut8(ATA_REGISTER(bus, ATA_FEATURES), 1); // Using DMA.
		uint32_t maxByteCount = sectorsNeededToLoad * ATAPI_SECTOR_SIZE;
		if (maxByteCount > 65535) KernelPanic("ATAController::AccessStart - Access too large for ATAPI drive (max 64KB).\n");
		ProcessorOut8(ATA_REGISTER(bus, ATA_LBA2), maxByteCount & 0xFF);
		ProcessorOut8(ATA_REGISTER(bus, ATA_LBA3), (maxByteCount >> 8) & 0xFF);
	} else if (sector >= 0x10000000) {
		s48 = true;

		SetDrive(bus, slave, 0x40);

		ProcessorOut8(ATA_REGISTER(bus, ATA_SECTOR_COUNT), 0);
		ProcessorOut8(ATA_REGISTER(bus, ATA_SECTOR_COUNT), sectorsNeededToLoad);

		// Set the sector to access.
		// The drive will keep track of the previous and current values of these registers,
		// allowing it to construct a 48-bit sector number.
		ProcessorOut8(ATA_REGISTER(bus, ATA_LBA3), sector >> 40);
		ProcessorOut8(ATA_REGISTER(bus, ATA_LBA2), sector >> 32);
		ProcessorOut8(ATA_REGISTER(bus, ATA_LBA1), sector >> 24);
		ProcessorOut8(ATA_REGISTER(bus, ATA_LBA3), sector >> 16);
		ProcessorOut8(ATA_REGISTER(bus, ATA_LBA2), sector >>  8);
		ProcessorOut8(ATA_REGISTER(bus, ATA_LBA1), sector >>  0);
	} else {
		SetDrive(bus, slave, 0x40 | (sector >> 24));
		ProcessorOut8(ATA_REGISTER(bus, ATA_SECTOR_COUNT), sectorsNeededToLoad);
		ProcessorOut8(ATA_REGISTER(bus, ATA_LBA3), sector >> 16);
		ProcessorOut8(ATA_REGISTER(bus, ATA_LBA2), sector >>  8);
		ProcessorOut8(ATA_REGISTER(bus, ATA_LBA1), sector >>  0);
	}

	KEvent *event = irqs + bus;
	event->autoReset = false;
	KEventReset(event);

	// Save the operation information.
	op.buffer = buffer;
	op.offsetIntoSector = offsetIntoSector;
	op.countBytes = countBytes;
	op.operation = operation;
	op.readingData = false;
	op.readIndex = 0;
	op.sectorsNeededToLoad = sectorsNeededToLoad;
	op.pio = false;
	op.bus = bus;
	op.slave = slave;

	{
		// Make sure the previous request has completed.
		ProcessorIn8(ATA_REGISTER(bus, ATA_STATUS));
		pci->ReadBAR8(DMA_REGISTER(bus, DMA_STATUS));

		// Prepare the PRDT and buffer
		prdts[bus]->size = sectorsNeededToLoad * (atapi ? ATAPI_SECTOR_SIZE : ATA_SECTOR_SIZE);
		if (operation == K_ACCESS_WRITE) EsMemoryCopy((uint8_t *) buffers[bus] + offsetIntoSector, buffer, countBytes);

		// Set the mode.
		pci->WriteBAR8(DMA_REGISTER(bus, DMA_COMMAND), operation == K_ACCESS_WRITE ? 0 : 8);
		pci->WriteBAR8(DMA_REGISTER(bus, DMA_STATUS), 6);

		// Wait for the RDY bit to set.
		while (!(ProcessorIn8(ATA_REGISTER(bus, ATA_STATUS)) & (1 << 6)) && !timeout.Hit());
		if (timeout.Hit()) return false;

		// Issue the command.
		if (atapi)	ProcessorOut8(ATA_REGISTER(bus, ATA_COMMAND), ATA_PACKET);
		else if (s48) 	ProcessorOut8(ATA_REGISTER(bus, ATA_COMMAND), operation == K_ACCESS_READ ? ATA_READ_DMA_48 : ATA_WRITE_DMA_48);
		else		ProcessorOut8(ATA_REGISTER(bus, ATA_COMMAND), operation == K_ACCESS_READ ? ATA_READ_DMA :    ATA_WRITE_DMA   );

		// Wait for the DRQ bit to set.
		while (!(ProcessorIn8(ATA_REGISTER(bus, ATA_DCR)) & (1 << 3)) && !timeout.Hit());

		if (timeout.Hit()) return false;

		if (atapi) {
			uint8_t packet[12] = {};
			packet[0] = 0xA8; // Read sectors.
			packet[2] = (sector >> 0x18) & 0xFF;
			packet[3] = (sector >> 0x10) & 0xFF;
			packet[4] = (sector >> 0x08) & 0xFF;
			packet[5] = (sector >> 0x00) & 0xFF;
			packet[9] = sectorsNeededToLoad;

			// Wait for the BSY bit to clear.
			while ((ProcessorIn8(ATA_REGISTER(bus, ATA_STATUS)) & (1 << 7)) && !timeout.Hit());
			if (timeout.Hit()) return false;

			// Send the ATAPI command.
			ProcessorOut16(ATA_REGISTER(bus, ATA_DATA), ((uint16_t *) packet)[0]);
			ProcessorOut16(ATA_REGISTER(bus, ATA_DATA), ((uint16_t *) packet)[1]);
			ProcessorOut16(ATA_REGISTER(bus, ATA_DATA), ((uint16_t *) packet)[2]);
			ProcessorOut16(ATA_REGISTER(bus, ATA_DATA), ((uint16_t *) packet)[3]);
			ProcessorOut16(ATA_REGISTER(bus, ATA_DATA), ((uint16_t *) packet)[4]);
			ProcessorOut16(ATA_REGISTER(bus, ATA_DATA), ((uint16_t *) packet)[5]);
		}

		pci->WriteBAR8(DMA_REGISTER(bus, DMA_COMMAND), operation == K_ACCESS_WRITE ? 1 : 9);
		if (pci->ReadBAR8(DMA_REGISTER(bus, DMA_STATUS)) & 2) return false;
		if (ProcessorIn8(ATA_REGISTER(bus, ATA_DCR)) & 33) return false;
	}

	return true;
}

bool ATAController::AccessEnd(int bus, int slave) {
	(void) slave;
	KEvent *event = irqs + bus;

	{
		// Wait for the command to complete.
		KEventWait(event, ATA_TIMEOUT);

		// Copy the data that we read.
		ATAOperation *op = (ATAOperation *) &ataController->op;
		if (op->buffer && op->operation == K_ACCESS_READ) {
			// EsPrint("copying %d to %x\n", op->countBytes, op->buffer);
			EsMemoryCopy((void *) op->buffer, (uint8_t *) ataController->buffers[op->bus] + op->offsetIntoSector, op->countBytes);
			// EsPrint("done\n");
		}

		// Check for error.
		if (ProcessorIn8(ATA_REGISTER(bus, ATA_STATUS)) & 33) return false;
		if (pci->ReadBAR8(DMA_REGISTER(bus, DMA_STATUS)) & 3) return false;

		// Check if the command has completed.
		if (!KEventPoll(event)) {
			return false;
		}

		return true;
	}
}

void ATAController::Unblock() {
	KMutexAcquire(&blockedPacketsMutex);
	// EsPrint("unblock!\n");
	KSemaphoreReturn(&semaphore, 1);
	KMutexRelease(&blockedPacketsMutex);
}

bool ATAController::Access(uintptr_t drive, uint64_t offset, size_t countBytes, int operation, uint8_t *_buffer) {
	bool atapi = isATAPI[drive];
	uint64_t sectorSize = atapi ? ATAPI_SECTOR_SIZE : ATA_SECTOR_SIZE;
	uint64_t sector = offset / sectorSize;
	uint64_t offsetIntoSector = offset % sectorSize;
	uint64_t sectorsNeededToLoad = (countBytes + offsetIntoSector + sectorSize - 1) / sectorSize;
	uintptr_t bus = drive >> 1;
	uintptr_t slave = drive & 1;

	if (drive >= ATA_DRIVES) KernelPanic("ATAController::Access - Drive %d exceedes the maximum number of ATA driver (%d).\n", drive, ATA_DRIVES);
	if (atapi && operation == K_ACCESS_WRITE) KernelPanic("ATAController::Access - Drive %d is an ATAPI drive. ATAPI write operations are currently not supported.\n", drive);
	if (!sectorCount[drive] && !atapi) KernelPanic("ATAController::Access - Drive %d is invalid.\n", drive);
	if ((sector > sectorCount[drive] || (sector + sectorsNeededToLoad) > sectorCount[drive]) && !atapi) KernelPanic("ATAController::Access - Attempt to access sector %d when drive only has %d sectors.\n", sector, sectorCount[drive]);
	if (sectorsNeededToLoad > 64) KernelPanic("ATAController::Access - Attempt to read more than 64 consecutive sectors in 1 function call.\n");

	// Lock the driver.
	{
		while (true) {
			KEventWait(&semaphore.available, ES_WAIT_NO_TIMEOUT);
			KMutexAcquire(&blockedPacketsMutex);

			if (semaphore.units) {
				KSemaphoreTake(&semaphore, 1);
				break;
			}

			KMutexRelease(&blockedPacketsMutex);
		}

		KMutexRelease(&blockedPacketsMutex);
	}

	// EsPrint("locked (%d/%x)!\n", countBytes, _buffer);

	op.bus = bus;
	op.slave = slave;

	if (!AccessStart(bus, slave, sector, offsetIntoSector, sectorsNeededToLoad, countBytes, operation, _buffer, atapi)) {
		KSemaphoreReturn(&semaphore, 1);
		return false;
	}

	bool result = AccessEnd(bus, slave);
	Unblock();
	return result;
}

bool ATAIRQHandler(uintptr_t interruptIndex, void *) {
	int bus = interruptIndex - ATA_IRQ(0);

	// Acknowledge the interrupt.
	ProcessorIn8(ATA_REGISTER(bus, ATA_STATUS));
	ataController->pci->ReadBAR8(DMA_REGISTER(bus, DMA_STATUS));

	// *Don't* queue an asynchronous task.
	// First of all, we don't need to (it's slower),
	// and secondly, we need to Sync() nodes we're closing during process handle table termination,
	// which takes place in the asynchronous task thread. (Meaning we'd get deadlock).
	// TODO Is there a better way to do this, preventing similar bugs in the future?

	ATAOperation *op = (ATAOperation *) &ataController->op;
	KEvent *event = ataController->irqs + op->bus;

	if (op->pio) {
		KEventSet(event);
	} else if (!(ataController->pci->ReadBAR8(DMA_REGISTER(op->bus, DMA_STATUS)) & 4)) {
		// The interrupt bit was not set, so the IRQ must have been generated by a different device.
	} else {
		if (!event->state) {
			// Stop the transfer.
			ataController->pci->WriteBAR8(DMA_REGISTER(op->bus, DMA_COMMAND), 0);
			KEventSet(event);
		} else {
			KernelLog(LOG_ERROR, "IDE", "too many IRQs", "ATAIRQHandler - Received more interrupts than expected.\n");
		}
	}

	KSwitchThreadAfterIRQ();

	return true;
}

inline uint32_t ByteSwap32(uint32_t x) {
	return    ((x & 0xFF000000) >> 24) 
		| ((x & 0x000000FF) << 24) 
		| ((x & 0x00FF0000) >> 8) 
		| ((x & 0x0000FF00) << 8);
}

void ATAController::Initialise() {
	KSemaphoreReturn(&semaphore, 1);

	KernelLog(LOG_INFO, "IDE", "found controller", "ATAController::Initialise - Found an ATA controller.\n");

	for (uintptr_t bus = 0; bus < ATA_BUSES; bus++) {
		// If the status is 0xFF, then the bus does not exist.
		if (ProcessorIn8(ATA_REGISTER(bus, ATA_STATUS)) == 0xFF) {
			continue;
		}

		// Check that the LBA registers are RW.
		ProcessorOut8(ATA_REGISTER(bus, ATA_LBA1), 0xAB);
		ProcessorOut8(ATA_REGISTER(bus, ATA_LBA2), 0xCD);
		ProcessorOut8(ATA_REGISTER(bus, ATA_LBA3), 0xEF);

		// Otherwise, the bus doesn't exist.
		if (ProcessorIn8(ATA_REGISTER(bus, ATA_LBA1) != 0xAB)) continue;
		if (ProcessorIn8(ATA_REGISTER(bus, ATA_LBA2) != 0xCD)) continue;
		if (ProcessorIn8(ATA_REGISTER(bus, ATA_LBA3) != 0xEF)) continue;

		// Clear the device command register.
		ProcessorOut8(ATA_REGISTER(bus, ATA_DCR), 0);

		int dmaDrivesOnBus = 0;
		int drivesOnBus = 0;
		uint8_t status;

		size_t drivesPerBus = 2;

		for (uintptr_t slave = 0; slave < drivesPerBus; slave++) {
			// Issue the IDENTIFY command to the drive.
			SetDrive(bus, slave);
			ProcessorOut8(ATA_REGISTER(bus, ATA_LBA2), 0);
			ProcessorOut8(ATA_REGISTER(bus, ATA_LBA3), 0);
			ProcessorOut8(ATA_REGISTER(bus, ATA_COMMAND), ATA_IDENTIFY);

			// Start a timeout.
			KTimeout timeout(100);

			// Check for error.
			bool atapi = false;
			if (ProcessorIn8(ATA_REGISTER(bus, ATA_STATUS)) & 32) continue;
			if (ProcessorIn8(ATA_REGISTER(bus, ATA_STATUS)) & 1) {
				uint8_t a = ProcessorIn8(ATA_REGISTER(bus, ATA_LBA2));
				uint8_t b = ProcessorIn8(ATA_REGISTER(bus, ATA_LBA3));
				if (a == 0x14 && b == 0xEB) {
					atapi = true;
					ProcessorOut8(ATA_REGISTER(bus, ATA_COMMAND), ATA_IDENTIFY_PACKET);
				} else {
					continue;
				}
			}

			// Wait for the drive to be ready for the data transfer.
			while ((ProcessorIn8(ATA_REGISTER(bus, ATA_STATUS)) & 0x80) && !timeout.Hit());
			if (timeout.Hit()) continue;
			while ((!(status = ProcessorIn8(ATA_REGISTER(bus, ATA_STATUS)) & 9)) && !timeout.Hit());
			if (timeout.Hit()) continue;
			if (status & 33) continue;

			// Transfer the data.
			for (uintptr_t i = 0; i < 256; i++) {
				identifyData[i] = ProcessorIn16(ATA_REGISTER(bus, ATA_DATA));
			}

			// Check if the device supports LBA/DMA.
			if (!(identifyData[49] & 0x200)) continue;
			if (!(identifyData[49] & 0x100)) continue;
			dmaDrivesOnBus |= 1;
			drivesOnBus |= 1;

			// Work out the number of sectors in the drive.
			uint32_t lba28Sectors = ((uint32_t) identifyData[60] << 0) + ((uint32_t) identifyData[61] << 16);
			uint64_t lba48Sectors = ((uint64_t) identifyData[100] << 0) + ((uint64_t) identifyData[101] << 16) +
				((uint64_t) identifyData[102] << 32) + ((uint64_t) identifyData[103] << 48);
			bool supportsLBA48 = lba48Sectors && (identifyData[83] & (1 << 10));
			uint64_t sectors = supportsLBA48 ? lba48Sectors : lba28Sectors;
			sectorCount[slave + bus * 2] = sectors;

			if (atapi) {
				isATAPI[slave + bus * 2] = true;
				KernelLog(LOG_INFO, "IDE", "found drive", "ATAController::Initialise - Found ATAPI drive: %d/%d%z.\n", 
						bus, slave, supportsLBA48 ? "; supports LBA48" : "");
			} else {
				KernelLog(LOG_INFO, "IDE", "found drive", "ATAController::Initialise - Found ATA drive: %d/%d with %x sectors%z.\n", 
						bus, slave, sectors, supportsLBA48 ? "; supports LBA48" : "");
			}
		}

		if (dmaDrivesOnBus) {
			uint8_t *dataVirtual;
			uintptr_t dataPhysical;

			if (!MMPhysicalAllocateAndMap(131072, 131072, 32, true, ES_FLAGS_DEFAULT, &dataVirtual, &dataPhysical)) {
				KernelLog(LOG_ERROR, "IDE", "allocation failure", "ATAController::Initialise - Could not allocate memory for DMA on bus %d.\n", bus);
				sectorCount[bus * 2 + 0] = sectorCount[bus * 2 + 1] = 0;
				drivesOnBus = 0;
				continue;
			}

			PRD *prdt = (PRD *) dataVirtual;
			prdt->end = 0x8000;
			prdt->base = dataPhysical + 65536;
			prdts[bus] = prdt;

			void *buffer = (void *) (dataVirtual + 65536);
			buffers[bus] = buffer;

			pci->WriteBAR32(DMA_REGISTER(bus, DMA_PRDT), dataPhysical);
		}

		if (drivesOnBus) {
			if (!KRegisterIRQ(ATA_IRQ(bus), ATAIRQHandler, nullptr, "IDE")) {
				KernelLog(LOG_ERROR, "IDE", "IRQ registration failure", "ATAController::Initialise - Could not register IRQ for bus %d.\n", bus);

				// Disable the drives on this bus.
				sectorCount[bus * 2 + 0] = 0;
				sectorCount[bus * 2 + 1] = 0;
				isATAPI[bus * 2 + 0] = false;
				isATAPI[bus * 2 + 1] = false;
			}
		}

		for (uintptr_t slave = 0; slave < 2; slave++) {
			if (sectorCount[slave + bus * 2]) {
				bool success = Access(bus * 2 + slave, 0, ATA_SECTOR_SIZE, K_ACCESS_READ, (uint8_t *) identifyData);

				if (!success) {
					KernelLog(LOG_ERROR, "IDE", "test read failure", "ATAController::Initialise - Could not perform test read on drive.\n");
					continue;
				}

				success = Access(bus * 2 + slave, 0, ATA_SECTOR_SIZE, K_ACCESS_WRITE, (uint8_t *) identifyData);

				if (!success) {
					KernelLog(LOG_ERROR, "IDE", "test write failure", "ATAController::Initialise - Could not perform test write to drive.\n");
				}
			} else if (isATAPI[slave + bus * 2]) {
				KEvent *event = irqs + bus;
				event->autoReset = false;
				KEventReset(event);

				op.bus = bus;
				op.slave = slave;
				op.pio = true;

				uint16_t capacity[4];

				SetDrive(bus, slave);
				ProcessorOut8(ATA_REGISTER(bus, ATA_LBA2), 0);
				ProcessorOut8(ATA_REGISTER(bus, ATA_LBA3), 8);
				ProcessorOut8(ATA_REGISTER(bus, ATA_FEATURES), 0);
				ProcessorOut8(ATA_REGISTER(bus, ATA_COMMAND), ATA_PACKET);

				KTimeout timeout(100);

				if (ProcessorIn8(ATA_REGISTER(bus, ATA_STATUS)) & 33) {
					goto readCapacityFailure;
				}

				while ((ProcessorIn8(ATA_REGISTER(bus, ATA_STATUS)) & 0x80) && !timeout.Hit());
				if (timeout.Hit()) goto readCapacityFailure;
				while ((!(status = ProcessorIn8(ATA_REGISTER(bus, ATA_STATUS)) & 9)) && !timeout.Hit());
				if (timeout.Hit()) goto readCapacityFailure;
				if (status & 33) goto readCapacityFailure;

				ProcessorOut16(ATA_REGISTER(bus, ATA_DATA), 0x0025);
				ProcessorOut16(ATA_REGISTER(bus, ATA_DATA), 0x0000);
				ProcessorOut16(ATA_REGISTER(bus, ATA_DATA), 0x0000);
				ProcessorOut16(ATA_REGISTER(bus, ATA_DATA), 0x0000);
				ProcessorOut16(ATA_REGISTER(bus, ATA_DATA), 0x0000);
				ProcessorOut16(ATA_REGISTER(bus, ATA_DATA), 0x0000);

				KEventWait(event, ATA_TIMEOUT);
				KEventReset(event);

				while ((ProcessorIn8(ATA_REGISTER(bus, ATA_STATUS)) & 0x80) && !timeout.Hit());
				if (timeout.Hit()) goto readCapacityFailure;
				while ((!(status = ProcessorIn8(ATA_REGISTER(bus, ATA_STATUS)) & 9)) && !timeout.Hit());
				if (timeout.Hit()) goto readCapacityFailure;
				if (status & 33) goto readCapacityFailure;

				capacity[0] = ProcessorIn16(ATA_REGISTER(bus, ATA_DATA));
				capacity[1] = ProcessorIn16(ATA_REGISTER(bus, ATA_DATA));
				capacity[2] = ProcessorIn16(ATA_REGISTER(bus, ATA_DATA));
				capacity[3] = ProcessorIn16(ATA_REGISTER(bus, ATA_DATA));

				KEventWait(event, ATA_TIMEOUT);
				KEventReset(event);

				{
					uint32_t blockCount = ByteSwap32(capacity[0] | ((uint32_t) capacity[1] << 16)) + 1;
					uint32_t blockLength = ByteSwap32(capacity[2] | ((uint32_t) capacity[3] << 16));

					if (blockLength != ATAPI_SECTOR_SIZE) {
						KernelLog(LOG_ERROR, "IDE", "unsupported ATAPI block length", 
								"ATAController::Initialise - ATAPI drive reported block length of %d bytes, which is unsupported.\n", blockLength);
						continue;
					}

					KernelLog(LOG_INFO, "IDE", "ATAPI capacity", "ATAController::Initialise - ATAPI drive has %d blocks (%D).\n", 
							blockCount, blockCount * ATAPI_SECTOR_SIZE);

					sectorCount[slave + bus * 2] = blockCount;
					continue;
				}

				readCapacityFailure:;
				KernelLog(LOG_ERROR, "IDE", "read capacity failure", "ATAController::Initialise - Could not read capacity of ATAPI drive %d/%d.\n", bus, slave);
				continue;
			}
		}
	}

	for (uintptr_t i = 0; i < ATA_DRIVES; i++) {
		if (sectorCount[i]) {
			// Register the drive.
			ATADrive *device = (ATADrive *) KDeviceCreate("IDE drive", this, sizeof(ATADrive));

			if (!device) {
				KernelLog(LOG_ERROR, "IDE", "allocation failure", "Could not create device for drive %d.\n", i);
				break;
			}

			device->index = i;
			device->information.sectorSize = isATAPI[i] ? ATAPI_SECTOR_SIZE : ATA_SECTOR_SIZE;
			device->information.sectorCount = sectorCount[i];
			device->maxAccessSectorCount = isATAPI[i] ? 31 : 64;
			device->information.readOnly = isATAPI[i];
			device->information.driveType = isATAPI[i] ? ES_DRIVE_TYPE_CDROM : ES_DRIVE_TYPE_HDD;

			device->access = [] (KBlockDeviceAccessRequest request) {
				request.dispatchGroup->Start();
				bool success = ataController->Access(((ATADrive *) request.device)->index, 
						request.offset, request.count, request.operation, (uint8_t *) KDMABufferGetVirtualAddress(request.buffer));
				request.dispatchGroup->End(success);
			};

			FSRegisterBlockDevice(device);
		}
	}
}

static void DeviceAttach(KDevice *_parent) {
	KPCIDevice *parent = (KPCIDevice *) _parent;

	if (ataController) {
		KernelLog(LOG_ERROR, "IDE", "multiple controllers", "EntryIDE - Attempt to register multiple IDE controllers; ignored.\n");
		return;
	}

	ATAController *device = (ATAController *) KDeviceCreate("IDE controller", parent, sizeof(ATAController));
	if (!device) return;
	ataController = device;
	device->pci = parent;

	// Enable busmastering DMA and interrupts.
	parent->EnableFeatures(K_PCI_FEATURE_INTERRUPTS | K_PCI_FEATURE_BUSMASTERING_DMA | K_PCI_FEATURE_BAR_4);

	// Initialise the controller.
	device->Initialise();
}

KDriver driverIDE = {
	.attach = DeviceAttach,
};
