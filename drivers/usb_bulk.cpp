#include <module.h>

// TODO STALL handling.
// TODO Resetting the device on error.
// TODO Command timeout.

struct Device : KDevice {
	KUSBDevice *parent;
	KUSBEndpointDescriptor *inputEndpoint, *outputEndpoint;
	uint8_t maximumLUN;
	KMutex mutex;

	void Initialise();
	bool DoTransfer(struct CommandBlock *block, void *buffer);
};

struct Drive : KBlockDevice {
	Device *device;
	uint8_t lun;
};

struct CommandBlock {
#define COMMAND_BLOCK_SIGNATURE (0x43425355)
	uint32_t signature;
	uint32_t tag; // Returned in the corresponding command status.
	uint32_t transferBytes;
#define COMMAND_FLAGS_INPUT (0x80)
#define COMMAND_FLAGS_OUTPUT (0x00)
	uint8_t flags; 
	uint8_t lun;
	uint8_t commandBytes;
	uint8_t command[16];
} __attribute__((packed));

struct CommandStatus {
#define COMMAND_STATUS_SIGNATURE (0x53425355)
	uint32_t signature;
	uint32_t tag; 
	uint32_t residue;
#define STATUS_FAILED (1)
#define STATUS_PHASE_ERROR (2)
	uint8_t status;
} __attribute__((packed));

bool Device::DoTransfer(CommandBlock *block, void *buffer) {
	KMutexAcquire(&mutex);
	EsDefer(KMutexRelease(&mutex));

	block->signature = COMMAND_BLOCK_SIGNATURE;
	block->tag = EsRandomU64() & 0xFFFFFFFF;

	KernelLog(LOG_VERBOSE, "USBBulk", "transfer", "Transferring %D to %x, %z, LUN %d, command %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X (%D).\n",
			block->transferBytes, buffer, block->flags == COMMAND_FLAGS_INPUT ? "input" : "output", block->lun, 
			block->command[0], block->command[1], block->command[2], block->command[3],
			block->command[4], block->command[5], block->command[6], block->command[7],
			block->command[8], block->command[9], block->command[10], block->command[11],
			block->command[12], block->command[13], block->command[14], block->command[15], block->commandBytes);

	// Send the command block to the output endpoint.

	if (!parent->RunTransfer(outputEndpoint, block, sizeof(CommandBlock), nullptr)) {
		KernelLog(LOG_ERROR, "USBBulk", "send command block error", "Could not send the command block to the device.\n");
		return false;
	}

	// Perform the transfer.

	if (!parent->RunTransfer(block->flags == COMMAND_FLAGS_INPUT ? inputEndpoint : outputEndpoint, buffer, block->transferBytes, nullptr)) {
		KernelLog(LOG_ERROR, "USBBulk", "transfer error", "Could not transfer with the device.\n");
		return false;
	}

	// Read the command status from the input endpoint.

	CommandStatus status = {};

	if (!parent->RunTransfer(inputEndpoint, &status, sizeof(CommandStatus), nullptr)) {
		KernelLog(LOG_ERROR, "USBBulk", "read command status error", "Could not read the command status from the device.\n");
		return false;
	}

	if (status.signature != COMMAND_STATUS_SIGNATURE
			|| status.tag != block->tag
			|| status.residue
			|| status.status) {
		KernelLog(LOG_ERROR, "USBBulk", "command unsuccessful", "Command status indicates it was unsuccessful: "
				"signature: %x, tag: %x (%x), residue: %D, status: %d.\n",
				status.signature, status.tag, block->tag, status.residue, status.status);
		return false;
	}

	return true;
}

void DriveAccess(KBlockDeviceAccessRequest request) {
	Drive *drive = (Drive *) request.device;

	Device *device = drive->device;
	request.dispatchGroup->Start();

	uint32_t offsetSectors = request.offset / drive->sectorSize;
	uint32_t countSectors = request.count / drive->sectorSize;

	CommandBlock command = {
		.transferBytes = (uint32_t) request.count,
		.flags = (uint8_t) (request.operation == K_ACCESS_WRITE ? COMMAND_FLAGS_OUTPUT : COMMAND_FLAGS_INPUT),
		.lun = drive->lun,
		.commandBytes = 10,

		.command = { 
			[0] = (uint8_t) (request.operation == K_ACCESS_WRITE ? 0x2A /* WRITE (12) */ : 0x28 /* READ (12) */),
			[1] = 0,
			[2] = (uint8_t) (offsetSectors >> 0x18),
			[3] = (uint8_t) (offsetSectors >> 0x10),
			[4] = (uint8_t) (offsetSectors >> 0x08),
			[5] = (uint8_t) (offsetSectors >> 0x00),
			[6] = 0,
			[7] = (uint8_t) (countSectors  >> 0x08),
			[8] = (uint8_t) (countSectors  >> 0x00),
		},
	};

	request.dispatchGroup->End(device->DoTransfer(&command, (void *) KDMABufferGetVirtualAddress(request.buffer)));
}

void Device::Initialise() {
	uint16_t transferred;

	// Find the input and output endpoints.

	for (uintptr_t i = 0; true; i++) {
		KUSBEndpointDescriptor *endpoint = (KUSBEndpointDescriptor *) parent->GetCommonDescriptor(0x05 /* endpoint */, i);

		if (!endpoint) {
			break;
		} else if (endpoint->IsBulk() && endpoint->IsInput() && !inputEndpoint) {
			inputEndpoint = endpoint;
		} else if (endpoint->IsBulk() && endpoint->IsOutput() && !outputEndpoint) {
			outputEndpoint = endpoint;
		}
	}

	if (!inputEndpoint || !outputEndpoint) {
		KernelLog(LOG_ERROR, "USBBulk", "endpoint missing", "Could not find both bulk endpoints.\n");
		return;
	}

	// Reset the mass storage device.

	if (!parent->controlTransfer(parent, 0b00100001, 0b11111111, 0, parent->interfaceDescriptor.interfaceIndex, nullptr, 0, K_ACCESS_WRITE, &transferred)) {
		KernelLog(LOG_ERROR, "USBBulk", "reset failure", "Could not reset the mass storage device.\n");
		return;
	}

	// Get the maximum LUN.

	parent->controlTransfer(parent, 0b10100001, 0b11111110, 0, parent->interfaceDescriptor.interfaceIndex, &maximumLUN, 1, K_ACCESS_READ, &transferred);
	KernelLog(LOG_INFO, "USBBulk", "maximum LUN", "Device reports maximum LUN of %d.\n", maximumLUN);

	for (uintptr_t i = 0; i <= maximumLUN; i++) {
		// Get the capacity of the LUN.

		CommandBlock command = {
			.transferBytes = 8,
			.flags = COMMAND_FLAGS_INPUT,
			.lun = (uint8_t) i,
			.commandBytes = 10,
			.command = { [0] = 0x25 /* READ CAPACITY (10) */ },
		};

		uint8_t capacity[8];

		if (!DoTransfer(&command, capacity)) {
			KernelLog(LOG_ERROR, "USBBulk", "read capacity error", "Could not read the capacity of LUN %d.\n", i);
			continue;
		}

		uint32_t sectorCount = (((uint32_t) capacity[3] << 0) + ((uint32_t) capacity[2] << 8) 
				+ ((uint32_t) capacity[1] << 16) + ((uint32_t) capacity[0] << 24)) + 1;
		uint32_t sectorBytes = ((uint32_t) capacity[7] << 0) + ((uint32_t) capacity[6] << 8) 
			+ ((uint32_t) capacity[5] << 16) + ((uint32_t) capacity[4] << 24);

		KernelLog(LOG_INFO, "USBBulk", "capacity", "LUN %d has capacity of %D (one sector is %D).\n",
				i, (uint64_t) sectorCount * sectorBytes, sectorBytes);

		// Register the drive.

		Drive *drive = (Drive *) KDeviceCreate("USB bulk drive", this, sizeof(Drive));

		if (!drive) {
			KernelLog(LOG_ERROR, "USBBulk", "allocation failure", "Could not create drive for LUN %d.\n", i);
			break;
		}

		drive->device = this;
		drive->lun = i;
		drive->sectorSize = sectorBytes;
		drive->sectorCount = sectorCount;
		drive->maxAccessSectorCount = 262144 / sectorBytes; // TODO How to determine this? What does the USB layer support?
		drive->readOnly = false; // TODO How to detect this?
		drive->access = DriveAccess;
		drive->driveType = ES_DRIVE_TYPE_USB_MASS_STORAGE;

		FSRegisterBlockDevice(drive);
	}
}

static void DeviceAttach(KDevice *parent) {
	Device *device = (Device *) KDeviceCreate("USB bulk", parent, sizeof(Device));

	if (!device) {
		KernelLog(LOG_ERROR, "USBBulk", "allocation failure", "Could not allocate device structure.\n");
		return;
	}

	device->parent = (KUSBDevice *) parent;
	device->Initialise();
	KDeviceCloseHandle(device);
}

KDriver driverUSBBulk = {
	.attach = DeviceAttach,
};
