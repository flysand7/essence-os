// TODO Report errors.
// TODO GPT support.
// TODO Handle crashing?
// TODO Write any modified settings during installation.

#define INSTALLER

#define ES_CRT_WITHOUT_PREFIX
#include <essence.h>

#include <shared/hash.cpp>
#include <shared/strings.cpp>
#include <shared/partitions.cpp>
#include <ports/lzma/LzmaDec.c>

#include <shared/array.cpp>
#define IMPLEMENTATION
#include <shared/array.cpp>
#undef IMPLEMENTATION

#define Log(...)
// TODO Error handling.
#define exit(x) EsAssert(false)
#include <shared/esfs2.h>

// Assume an additional 64MB of storage capacity is needed on top of totalUncompressedBytes.
#define PARTITION_OVERHEAD (64 * 1024 * 1024)

#define MSG_SET_PROGRESS ((EsMessageType) (ES_MSG_USER_START + 1))

struct InstallerMetadata {
	uint64_t totalUncompressedBytes;
};

const EsStyle styleRoot = {
	.metrics = {
		.mask = ES_THEME_METRICS_GAP_MAJOR | ES_THEME_METRICS_INSETS,
		.insets = ES_RECT_1(30),
		.gapMajor = 20,
	},
};

const EsStyle styleDrivesSelectHint = {
	.inherit = ES_STYLE_TEXT_PARAGRAPH_SECONDARY,

	.metrics = {
		.mask = ES_THEME_METRICS_TEXT_ALIGN | ES_THEME_METRICS_INSETS,
		.insets = ES_RECT_1(30),
		.textAlign = ES_TEXT_H_CENTER | ES_TEXT_V_CENTER | ES_TEXT_WRAP,
	},
};

const EsStyle styleButtonsRow = {
	.metrics = {
		.mask = ES_THEME_METRICS_GAP_MAJOR,
		.gapMajor = 10,
	},
};

const EsStyle styleCustomizeTable = {
	.metrics = {
		.mask = ES_THEME_METRICS_GAP_MAJOR | ES_THEME_METRICS_GAP_MINOR,
		.gapMajor = 7,
		.gapMinor = 7,
	},
};

InstallerMetadata *metadata;
Array<EsMessageDevice> connectedDrives;
EsListView *drivesList;
EsPanel *driveInformation;
EsObjectID selectedDriveID;
EsButton *installButton;
EsButton *finishButton;
EsPanel *switcher;
EsPanel *panelInstallOptions;
EsPanel *panelCustomizeOptions;
EsPanel *panelLicenses;
EsPanel *panelWait;
EsPanel *panelComplete;
EsTextbox *userNameTextbox;
EsTextDisplay *progressDisplay;
const char *cSelectedFont;
uint8_t progress;
bool onWaitScreen, startedInstallation;
EsBlockDeviceInformation blockDeviceInformation;
EsHandle driveHandle;
EsFileOffset partitionOffset;
EsUniqueIdentifier installationIdentifier;
EsMountPoint newFileSystemMountPoint;
EsHandle mountNewFileSystemEvent;

/////////////////////////////////////////////

#define BUFFER_SIZE (1048576)
#define NAME_MAX (4096)

struct Extractor {
	EsFileInformation fileIn;
	CLzmaDec state;
	uint8_t inBuffer[BUFFER_SIZE], outBuffer[BUFFER_SIZE], copyBuffer[BUFFER_SIZE], pathBuffer[NAME_MAX];
	size_t inFileOffset, inBytes, inPosition;
	uintptr_t positionInBlock, blockSize;
};

void *DecompressAllocate(ISzAllocPtr, size_t size) { return EsHeapAllocate(size, false); }
void DecompressFree(ISzAllocPtr, void *address) { EsHeapFree(address); }
const ISzAlloc decompressAllocator = { DecompressAllocate, DecompressFree };

ptrdiff_t DecompressBlock(Extractor *e) {
	if (e->inBytes == e->inPosition) {
		e->inBytes = EsFileReadSync(e->fileIn.handle, e->inFileOffset, BUFFER_SIZE, e->inBuffer);
		if (!e->inBytes) return -1;
		e->inPosition = 0;
		e->inFileOffset += e->inBytes;
	}

	size_t inProcessed = e->inBytes - e->inPosition;
	size_t outProcessed = BUFFER_SIZE;
	ELzmaStatus status;
	LzmaDec_DecodeToBuf(&e->state, e->outBuffer, &outProcessed, e->inBuffer + e->inPosition, &inProcessed, LZMA_FINISH_ANY, &status);
	e->inPosition += inProcessed;
	return outProcessed;
}

bool Decompress(Extractor *e, void *_buffer, size_t bytes) {
	uint8_t *buffer = (uint8_t *) _buffer;

	while (bytes) {
		if (e->positionInBlock == e->blockSize) {
			ptrdiff_t processed = DecompressBlock(e);
			if (processed == -1) return false;
			e->blockSize = processed;
			e->positionInBlock = 0;
		}

		size_t copyBytes = bytes > e->blockSize - e->positionInBlock ? e->blockSize - e->positionInBlock : bytes;
		EsMemoryCopy(buffer, e->outBuffer + e->positionInBlock, copyBytes);
		e->positionInBlock += copyBytes, buffer += copyBytes, bytes -= copyBytes;
	}

	return true;
}

EsError Extract(const char *pathIn, size_t pathInBytes, const char *pathOut, size_t pathOutBytes) {
	Extractor *e = (Extractor *) EsHeapAllocate(sizeof(Extractor), true);
	if (!e) return ES_ERROR_INSUFFICIENT_RESOURCES;
	EsDefer(EsHeapFree(e));

	e->fileIn = EsFileOpen(pathIn, pathInBytes, ES_FILE_READ);
	if (e->fileIn.error != ES_SUCCESS) return e->fileIn.error;

	uint8_t header[LZMA_PROPS_SIZE + 8];
	EsFileReadSync(e->fileIn.handle, 0, sizeof(header), header);

	LzmaDec_Construct(&e->state);
	LzmaDec_Allocate(&e->state, header, LZMA_PROPS_SIZE, &decompressAllocator);
	LzmaDec_Init(&e->state);

	e->inFileOffset = sizeof(header);

	uint64_t crc64 = 0, actualCRC64 = 0;
	uint64_t totalBytesExtracted = 0;
	uint8_t lastProgressByte = 0;

	EsMemoryCopy(e->pathBuffer, pathOut, pathOutBytes);

	while (true) {
		uint64_t fileSize;
		if (!Decompress(e, &fileSize, sizeof(fileSize))) break;
		actualCRC64 = fileSize;
		uint16_t nameBytes;
		if (!Decompress(e, &nameBytes, sizeof(nameBytes))) break;
		if (nameBytes > NAME_MAX - pathOutBytes) break;
		if (!Decompress(e, e->pathBuffer + pathOutBytes, nameBytes)) break;

		EsFileInformation fileOut = EsFileOpen((const char *) e->pathBuffer, pathOutBytes + nameBytes, 
				ES_FILE_WRITE | ES_NODE_CREATE_DIRECTORIES | ES_NODE_FAIL_IF_FOUND);
		EsFileOffset fileOutPosition = 0;

		if (fileOut.error != ES_SUCCESS) {
			LzmaDec_Free(&e->state, &decompressAllocator);
			EsHandleClose(e->fileIn.handle);
			return fileOut.error;
		}

		while (fileOutPosition < fileSize) {
			size_t copyBytes = (fileSize - fileOutPosition) > BUFFER_SIZE ? BUFFER_SIZE : (fileSize - fileOutPosition);
			Decompress(e, e->copyBuffer, copyBytes);
			EsFileWriteSync(fileOut.handle, fileOutPosition, copyBytes, e->copyBuffer);
			fileOutPosition += copyBytes;
			totalBytesExtracted += copyBytes;
			crc64 = CalculateCRC64(e->copyBuffer, copyBytes, crc64);

			EsMessage m = { MSG_SET_PROGRESS };
			double progress = (double) totalBytesExtracted / metadata->totalUncompressedBytes;
			m.user.context1.u = 10 + 80 * progress;

			if (lastProgressByte != m.user.context1.u) {
				lastProgressByte = m.user.context1.u;
				EsMessagePost(nullptr, &m);
			}
		}

		EsHandleClose(fileOut.handle);
	}

	LzmaDec_Free(&e->state, &decompressAllocator);
	EsHandleClose(e->fileIn.handle);

	return crc64 == actualCRC64 ? ES_SUCCESS : ES_ERROR_CORRUPT_DATA;
}

/////////////////////////////////////////////

// TODO Error handling.

uint64_t writeOffset;
uint64_t writeBytes;
uint8_t writeBuffer[BUFFER_SIZE];

void FlushWriteBuffer() {
	if (!writeBytes) return;
	EsFileOffset parameters[2] = { partitionOffset * blockDeviceInformation.sectorSize + writeOffset, writeBytes };
	EsError error = EsDeviceControl(driveHandle, ES_DEVICE_CONTROL_BLOCK_WRITE, writeBuffer, parameters);
	EsAssert(error == ES_SUCCESS);
	writeBytes = 0;
}

void ReadBlock(uint64_t block, uint64_t count, void *buffer) {
	EsFileOffset parameters[2] = { partitionOffset * blockDeviceInformation.sectorSize + block * blockSize, count * blockSize };
	EsError error = EsDeviceControl(driveHandle, ES_DEVICE_CONTROL_BLOCK_READ, buffer, parameters);
	EsAssert(error == ES_SUCCESS);
}

void WriteBlock(uint64_t block, uint64_t count, void *buffer) {
	uint64_t offset = block * blockSize, bytes = count * blockSize;

	if (writeBytes && writeOffset + writeBytes == offset && writeBytes + bytes < sizeof(writeBuffer)) {
		EsMemoryCopy(writeBuffer + writeBytes, buffer, bytes);
		writeBytes += bytes;
	} else {
		FlushWriteBuffer();
		writeOffset = offset;
		writeBytes = bytes;
		EsMemoryCopy(writeBuffer, buffer, bytes);
	}
}

void WriteBytes(uint64_t byteOffset, uint64_t byteCount, void *buffer) {
	uint64_t firstSector = byteOffset / blockDeviceInformation.sectorSize;
	uint64_t lastSector = (byteOffset + byteCount - 1) / blockDeviceInformation.sectorSize;
	uint64_t sectorCount = lastSector - firstSector + 1;

	void *buffer2 = EsHeapAllocate(sectorCount * blockDeviceInformation.sectorSize, false);
	
	for (uintptr_t i = 0; i < sectorCount; i++) {
		if (i > 0 && i < sectorCount - 1) continue;
		EsFileOffset parameters[2] = { (partitionOffset + firstSector + i) * blockDeviceInformation.sectorSize, blockDeviceInformation.sectorSize } ;
		EsError error = EsDeviceControl(driveHandle, ES_DEVICE_CONTROL_BLOCK_READ, (uint8_t *) buffer2 + i * blockDeviceInformation.sectorSize, parameters);
		EsAssert(error == ES_SUCCESS);
	}

	EsMemoryCopy((uint8_t *) buffer2 + byteOffset % blockDeviceInformation.sectorSize, buffer, byteCount);

	EsFileOffset parameters[2] = { (partitionOffset + firstSector) * blockDeviceInformation.sectorSize, sectorCount * blockDeviceInformation.sectorSize };
	EsError error = EsDeviceControl(driveHandle, ES_DEVICE_CONTROL_BLOCK_WRITE, buffer, parameters);
	EsAssert(error == ES_SUCCESS);

	EsHeapFree(buffer2);
}

/////////////////////////////////////////////

EsBlockDeviceInformation ConnectedDriveGetInformation(EsHandle handle) {
	EsBlockDeviceInformation information;
	EsDeviceControl(handle, ES_DEVICE_CONTROL_BLOCK_GET_INFORMATION, &information, nullptr);

	for (uintptr_t i = 0; i < information.modelBytes; i++) {
		if (information.model[i] == 0) {
			information.modelBytes = i;
			break;
		}
	}

	for (uintptr_t i = information.modelBytes; i > 0; i--) {
		if (information.model[i - 1] == ' ') {
			information.modelBytes--;
		} else {
			break;
		}
	}

	return information;
}

void ConnectedDriveAdd(EsMessageDevice device) {
	if (device.type != ES_DEVICE_BLOCK) {
		return;
	}

	EsBlockDeviceInformation information = ConnectedDriveGetInformation(device.handle); 

	if (information.nestLevel || information.driveType == ES_DRIVE_TYPE_CDROM) {
		return;
	}

	// TODO EsObjectID might not necessarily fit in EsGeneric...
	EsListViewFixedItemInsert(drivesList, information.model, information.modelBytes, device.id);
	connectedDrives.Add(device);
}

void ConnectedDriveRemove(EsMessageDevice device) {
	if (device.id == selectedDriveID) {
		EsElementDestroyContents(driveInformation);
		EsTextDisplayCreate(driveInformation, ES_CELL_H_FILL | ES_CELL_V_FILL, &styleDrivesSelectHint, INTERFACE_STRING(InstallerDriveRemoved));
		selectedDriveID = 0;
		EsElementSetDisabled(installButton, true);
	}

	for (uintptr_t i = 0; i < connectedDrives.Length(); i++) {
		if (connectedDrives[i].id == device.id) {
			EsListViewFixedItemRemove(drivesList, device.id);
			connectedDrives.Delete(i);
			return;
		}
	}
}

void ConnectedDriveSelect(uintptr_t index) {
	EsMessageDevice device = connectedDrives[index];
	if (selectedDriveID == device.id) return;
	selectedDriveID = device.id;
	EsElementSetDisabled(installButton, true);

	EsBlockDeviceInformation information = ConnectedDriveGetInformation(device.handle);
	EsElementStartTransition(driveInformation, ES_TRANSITION_FADE_VIA_TRANSPARENT, ES_FLAGS_DEFAULT, 4.0f);
	EsElementDestroyContents(driveInformation);

	EsPanel *nameRow = EsPanelCreate(driveInformation, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL);
	EsIconDisplayCreate(nameRow, ES_FLAGS_DEFAULT, 0, EsIconIDFromDriveType(information.driveType));
	EsSpacerCreate(nameRow, ES_CELL_V_FILL, 0, 8, 0);
	EsTextDisplayCreate(nameRow, ES_CELL_H_FILL, ES_STYLE_TEXT_HEADING2, information.model, information.modelBytes);
	EsSpacerCreate(driveInformation, ES_CELL_H_FILL, 0, 0, 16);

	EsPanel *messageRow = EsPanelCreate(driveInformation, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL);
	EsIconDisplay *statusIcon = EsIconDisplayCreate(messageRow, ES_CELL_V_TOP, ES_STYLE_ICON_DISPLAY_SMALL, ES_ICON_DIALOG_INFORMATION);
	EsSpacerCreate(messageRow, ES_CELL_V_FILL, 0, 8, 0);

	uint8_t *sectorBuffer = (uint8_t *) EsHeapAllocate(information.sectorSize, false);
	EsFileOffset parameters[2] = { 0, information.sectorSize };
	EsError readError = EsDeviceControl(device.handle, ES_DEVICE_CONTROL_BLOCK_READ, sectorBuffer, parameters);
	bool alreadyHasPartitions = false;

	if (readError == ES_SUCCESS && information.sectorSize >= 0x200) {
		// TODO Support GPT.
		MBRPartition partitions[4];

		if (MBRGetPartitions(sectorBuffer, information.sectorCount, partitions)) {
			for (uintptr_t i = 0; i < 4; i++) {
				if (partitions[i].present) {
					alreadyHasPartitions = true;
				}
			}
		}
	}

	EsHeapFree(sectorBuffer);

	bool showCapacity = false;

	if (information.sectorSize != 0x200) {
		// TODO Allow other sector sizes if a GPT is being used.
		EsIconDisplaySetIcon(statusIcon, ES_ICON_DIALOG_ERROR);
		EsTextDisplayCreate(messageRow, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(InstallerDriveUnsupported));
	} else if (readError != ES_SUCCESS) {
		EsIconDisplaySetIcon(statusIcon, ES_ICON_DIALOG_ERROR);
		EsTextDisplayCreate(messageRow, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(InstallerDriveCouldNotRead));
	} else if (information.readOnly) {
		EsIconDisplaySetIcon(statusIcon, ES_ICON_DIALOG_ERROR);
		EsTextDisplayCreate(messageRow, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(InstallerDriveReadOnly));
	} else if (alreadyHasPartitions) {
		EsIconDisplaySetIcon(statusIcon, ES_ICON_DIALOG_ERROR);
		EsTextDisplayCreate(messageRow, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(InstallerDriveAlreadyHasPartitions));
	} else if (information.sectorSize * information.sectorCount < metadata->totalUncompressedBytes + PARTITION_OVERHEAD) {
		EsIconDisplaySetIcon(statusIcon, ES_ICON_DIALOG_ERROR);
		EsTextDisplayCreate(messageRow, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(InstallerDriveNotEnoughSpace));
		showCapacity = true;
	} else {
		EsTextDisplayCreate(messageRow, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(InstallerDriveOkay));
		EsElementSetDisabled(installButton, false);
		showCapacity = true;
	}

	if (showCapacity) {
		// TODO Localization.
		char buffer[128];
		size_t bytes = EsStringFormat(buffer, sizeof(buffer), "Minimum space required: %D", metadata->totalUncompressedBytes + PARTITION_OVERHEAD);
		EsSpacerCreate(driveInformation, ES_CELL_H_FILL, 0, 0, 10);
		EsTextDisplayCreate(driveInformation, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, buffer, bytes);
		bytes = EsStringFormat(buffer, sizeof(buffer), "Drive capacity: %D", information.sectorSize * information.sectorCount);
		EsTextDisplayCreate(driveInformation, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, buffer, bytes);
	}
}

/////////////////////////////////////////////

EsError Install() {
	EsMessage m = { MSG_SET_PROGRESS };
	EsError error;

	size_t mbrBytes, stage1Bytes, stage2Bytes, kernelBytes;
	void *mbr = EsFileReadAll(EsLiteral("0:/mbr.dat"), &mbrBytes);
	void *stage1 = EsFileReadAll(EsLiteral("0:/stage1.dat"), &stage1Bytes);
	void *stage2 = EsFileReadAll(EsLiteral("0:/stage2.dat"), &stage2Bytes);

	EsMessageDevice drive = {};

	for (uintptr_t i = 0; i < connectedDrives.Length(); i++) {
		if (connectedDrives[i].id == selectedDriveID) {
			drive = connectedDrives[i];
		}
	}

	EsAssert(drive.handle);
	EsBlockDeviceInformation driveInformation = ConnectedDriveGetInformation(drive.handle);
	blockDeviceInformation = driveInformation;
	driveHandle = drive.handle;

	uint8_t *sectorBuffer = (uint8_t *) EsHeapAllocate(driveInformation.sectorSize, false);
	if (!sectorBuffer) return ES_ERROR_INSUFFICIENT_RESOURCES;

	size_t bootloaderSectors = (stage1Bytes + stage2Bytes + driveInformation.sectorSize - 1) / driveInformation.sectorSize;
	uint8_t *bootloader = (uint8_t *) EsHeapAllocate(bootloaderSectors * driveInformation.sectorSize, true);
	EsMemoryCopy(bootloader, stage1, stage1Bytes);
	EsMemoryCopy(bootloader + stage1Bytes, stage2, stage2Bytes);

	m.user.context1.u = 1;
	EsMessagePost(nullptr, &m);

	// Create the partition table.
	// TODO GPT.
	// TODO Adding new entries to existing tables.

	EsAssert(driveInformation.sectorSize == 0x200);

	partitionOffset = 0x800;
	EsFileOffset partitionBytes = driveInformation.sectorSize * (driveInformation.sectorCount - partitionOffset);

	uint32_t partitions[16] = { 0x80 /* bootable */, 0x83 /* type */ };
	uint16_t bootSignature = 0xAA55;
	partitions[2] = partitionOffset; // Offset.
	partitions[3] = driveInformation.sectorCount - 0x800; // Sector count.
	MBRFixPartition(partitions);

	EsMemoryCopy(sectorBuffer + 0, mbr, 446);
	EsMemoryCopy(sectorBuffer + 446, partitions, 64);
	EsMemoryCopy(sectorBuffer + 510, &bootSignature, 2);

	{
		EsFileOffset parameters[2] = { 0, driveInformation.sectorSize };
		error = EsDeviceControl(drive.handle, ES_DEVICE_CONTROL_BLOCK_WRITE, sectorBuffer, parameters);
		if (error != ES_SUCCESS) return error;
	}

	m.user.context1.u = 2;
	EsMessagePost(nullptr, &m);

	// Install the bootloader.

	{
		EsFileOffset parameters[2] = { partitionOffset * driveInformation.sectorSize, bootloaderSectors * driveInformation.sectorSize };
		error = EsDeviceControl(drive.handle, ES_DEVICE_CONTROL_BLOCK_WRITE, bootloader, parameters);
		if (error != ES_SUCCESS) return error;
	}

	m.user.context1.u = 4;
	EsMessagePost(nullptr, &m);

	// Format the partition.

	void *kernel = EsFileReadAll(EsLiteral(K_OS_FOLDER "/Kernel.esx"), &kernelBytes);

	m.user.context1.u = 6;
	EsMessagePost(nullptr, &m);

	for (int i = 0; i < 16; i++) {
		installationIdentifier.d[i] = EsRandomU8();
	}

	Format(partitionBytes, interfaceString_InstallerVolumeLabel, installationIdentifier, kernel, kernelBytes);
	FlushWriteBuffer();

	m.user.context1.u = 8;
	EsMessagePost(nullptr, &m);

	// Mount the new partition and extract the archive to it.

	EsDeviceControl(drive.handle, ES_DEVICE_CONTROL_BLOCK_DETECT_FS, nullptr, nullptr);

	m.user.context1.u = 9;
	EsMessagePost(nullptr, &m);

	if (ES_ERROR_TIMEOUT_REACHED == (EsError) EsWait(&mountNewFileSystemEvent, 1, 10000)) {
		return ES_ERROR_TIMEOUT_REACHED;
	}

	error = Extract(EsLiteral("0:/installer_archive.dat"), newFileSystemMountPoint.prefix, newFileSystemMountPoint.prefixBytes);
	if (error != ES_SUCCESS) return error;

	return ES_SUCCESS;
}

void InstallThread(EsGeneric) {
	EsPerformanceTimerPush();
	EsError error = Install();
	EsAssert(error == ES_SUCCESS); // TODO Reporting errors.
	EsPrint("Installation finished in %Fs. Extracted %D from the archive.\n", EsPerformanceTimerPop(), metadata->totalUncompressedBytes);

	EsMessage m = { MSG_SET_PROGRESS };
	m.user.context1.u = 100;
	EsMessagePost(nullptr, &m);
}

/////////////////////////////////////////////

void ButtonViewLicenses(EsInstance *, EsElement *, EsCommand *) {
	EsPanelSwitchTo(switcher, panelLicenses, ES_TRANSITION_FADE_IN);
}

void ButtonInstallOptions(EsInstance *, EsElement *, EsCommand *) {
	EsPanelSwitchTo(switcher, panelInstallOptions, ES_TRANSITION_FADE_IN);
}

void ButtonShutdown(EsInstance *, EsElement *, EsCommand *) {
	EsSyscall(ES_SYSCALL_SHUTDOWN, SHUTDOWN_ACTION_POWER_OFF, 0, 0, 0);
}

void ButtonRestart(EsInstance *, EsElement *, EsCommand *) {
	EsSyscall(ES_SYSCALL_SHUTDOWN, SHUTDOWN_ACTION_RESTART, 0, 0, 0);
}

void ButtonInstall(EsInstance *, EsElement *, EsCommand *) {
	EsPanelSwitchTo(switcher, panelCustomizeOptions, ES_TRANSITION_FADE_IN);
	EsElementFocus(userNameTextbox);
	startedInstallation = true;
	EsThreadCreate(InstallThread, nullptr, 0);
}

void ButtonFont(EsInstance *, EsElement *element, EsCommand *) {
	EsFontInformation information;
	cSelectedFont = (const char *) element->userData.p;

	if (EsFontDatabaseLookupByName(cSelectedFont, -1, &information)) {
		_EsUISetFont(information.id);
	}
}

void ButtonFinish(EsInstance *, EsElement *, EsCommand *) {
	if (progress == 100) {
		EsPanelSwitchTo(switcher, panelComplete, ES_TRANSITION_FADE_IN);
	} else {
		onWaitScreen = true;
		EsPanelSwitchTo(switcher, panelWait, ES_TRANSITION_FADE_IN);
	}
}

int DrivesListMessage(EsElement *element, EsMessage *message) {
	if (message->type == ES_MSG_LIST_VIEW_SELECT) {
		EsGeneric deviceID;

		if (EsListViewFixedItemGetSelected(((EsListView *) element), &deviceID)) {
			for (uintptr_t i = 0; i < connectedDrives.Length(); i++) {
				if (connectedDrives[i].id == deviceID.u) {
					ConnectedDriveSelect(i);
					break;
				}
			}
		}
	}

	return 0;
}

int SwitcherMessage(EsElement *, EsMessage *message) {
	if (message->type == ES_MSG_GET_WIDTH || message->type == ES_MSG_GET_HEIGHT) {
		return EsMessageSend(panelInstallOptions, message);
	}

	return 0;
}

int UserNameTextboxMessage(EsElement *, EsMessage *message) {
	if (message->type == ES_MSG_TEXTBOX_UPDATED) {
		EsElementSetEnabled(finishButton, EsTextboxGetLineLength(userNameTextbox));
	}

	return 0;
}

void _start() {
	_init();

	metadata = (InstallerMetadata *) EsFileReadAll(EsLiteral("0:/installer_metadata.dat"), nullptr);
	EsAssert(metadata);

	mountNewFileSystemEvent = EsEventCreate(true);

	EsWindow *window = EsWindowCreate(_EsInstanceCreate(sizeof(EsInstance), nullptr), ES_WINDOW_PLAIN);
	EsHandle handle = _EsWindowGetHandle(window);
	window->instance->window = window;

	EsRectangle screen;
	EsSyscall(ES_SYSCALL_SCREEN_BOUNDS_GET, 0, (uintptr_t) &screen, 0, 0);
	EsSyscall(ES_SYSCALL_WINDOW_MOVE, handle, (uintptr_t) &screen, 0, 0);
	EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, handle, ES_WINDOW_SOLID_TRUE, 0, ES_WINDOW_PROPERTY_SOLID);
	EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, handle, 0, 0, ES_WINDOW_PROPERTY_FOCUSED);

	EsPanel *sheet = EsPanelCreate(window, ES_PANEL_VERTICAL | ES_CELL_PUSH | ES_CELL_CENTER, ES_STYLE_INSTALLER_ROOT);
	switcher = EsPanelCreate(sheet, ES_CELL_H_FILL | ES_PANEL_SWITCHER);
	switcher->messageUser = SwitcherMessage;

	{
		panelInstallOptions = EsPanelCreate(switcher, ES_CELL_H_FILL, &styleRoot);
		EsPanelSwitchTo(switcher, panelInstallOptions, ES_TRANSITION_NONE);
		EsTextDisplayCreate(panelInstallOptions, ES_CELL_H_FILL, ES_STYLE_TEXT_HEADING0, INTERFACE_STRING(InstallerTitle));

		EsPanel *drivesPanel = EsPanelCreate(panelInstallOptions, ES_CELL_H_FILL, ES_STYLE_PANEL_INSET);
		EsPanel *drivesSplit = EsPanelCreate(drivesPanel, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL);
		EsPanel *drivesLeft = EsPanelCreate(drivesSplit, ES_CELL_H_FILL);
		EsTextDisplayCreate(drivesLeft, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(InstallerDrivesList));
		EsSpacerCreate(drivesLeft, ES_CELL_H_FILL, 0, 0, 8);
		drivesList = EsListViewCreate(drivesLeft, ES_CELL_H_FILL | ES_LIST_VIEW_CHOICE_SELECT | ES_LIST_VIEW_FIXED_ITEMS, ES_STYLE_LIST_CHOICE_BORDERED);
		drivesList->messageUser = DrivesListMessage;
		EsElementFocus(drivesList);
		EsSpacerCreate(drivesSplit, ES_CELL_V_FILL, 0, 25, 0);
		driveInformation = EsPanelCreate(drivesSplit, ES_CELL_H_FILL | ES_CELL_V_FILL);
		EsTextDisplayCreate(driveInformation, ES_CELL_H_FILL | ES_CELL_V_FILL, &styleDrivesSelectHint, INTERFACE_STRING(InstallerDrivesSelectHint));

		EsPanel *buttonsRow = EsPanelCreate(panelInstallOptions, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL, &styleButtonsRow);
		EsButtonOnCommand(EsButtonCreate(buttonsRow, ES_FLAGS_DEFAULT, 0, INTERFACE_STRING(InstallerViewLicenses)), ButtonViewLicenses);
		EsButtonOnCommand(EsButtonCreate(buttonsRow, ES_FLAGS_DEFAULT, 0, INTERFACE_STRING(DesktopShutdownAction)), ButtonShutdown);
		EsSpacerCreate(buttonsRow, ES_CELL_H_FILL);
		installButton = EsButtonCreate(buttonsRow, ES_ELEMENT_DISABLED, 0, INTERFACE_STRING(InstallerInstall));
		EsButtonOnCommand(installButton, ButtonInstall);
	}

	{
		panelLicenses = EsPanelCreate(switcher, ES_CELL_FILL, &styleRoot);
		EsTextbox *textbox = EsTextboxCreate(panelLicenses, ES_CELL_FILL | ES_TEXTBOX_MULTILINE);
		EsElementSetDisabled(textbox);
		size_t bytes;
		char *data = (char *) EsFileReadAll(EsLiteral("0:/installer_licenses.txt"), &bytes);
		EsTextboxInsert(textbox, data, bytes);
		EsHeapFree(data);
		EsButtonOnCommand(EsButtonCreate(panelLicenses, ES_CELL_H_LEFT, 0, INTERFACE_STRING(InstallerGoBack)), ButtonInstallOptions);
	}

	{
		panelCustomizeOptions = EsPanelCreate(switcher, ES_CELL_FILL, &styleRoot);
		EsTextDisplayCreate(panelCustomizeOptions, ES_CELL_H_FILL, ES_STYLE_TEXT_HEADING0, INTERFACE_STRING(InstallerTitle));
		EsTextDisplayCreate(panelCustomizeOptions, ES_CELL_H_FILL, ES_STYLE_TEXT_HEADING1, INTERFACE_STRING(InstallerCustomizeOptions));

		EsPanel *table = EsPanelCreate(panelCustomizeOptions, ES_CELL_H_CENTER | ES_PANEL_HORIZONTAL | ES_PANEL_TABLE, &styleCustomizeTable);
		EsPanelSetBands(table, 2 /* columns */);

		EsTextDisplayCreate(table, ES_CELL_H_RIGHT, ES_STYLE_TEXT_LABEL, INTERFACE_STRING(InstallerUserName));
		userNameTextbox = EsTextboxCreate(table, ES_CELL_H_LEFT);
		userNameTextbox->messageUser = UserNameTextboxMessage;

		EsTextDisplayCreate(table, ES_CELL_H_RIGHT | ES_CELL_V_TOP, ES_STYLE_TEXT_RADIO_GROUP_LABEL, INTERFACE_STRING(InstallerSystemFont));
		EsPanel *fonts = EsPanelCreate(table, ES_CELL_H_LEFT);
		EsButton *button = EsButtonCreate(fonts, ES_BUTTON_RADIOBOX | ES_CELL_H_EXPAND, 0, INTERFACE_STRING(InstallerFontDefault));
		button->userData = (void *) "Inter";
		EsButtonOnCommand(button, ButtonFont);
		EsButtonSetCheck(button, ES_CHECK_CHECKED);
		button = EsButtonCreate(fonts, ES_BUTTON_RADIOBOX | ES_CELL_H_EXPAND, 0, EsLiteral("Atkinson Hyperlegible"));
		button->userData = (void *) "Atkinson Hyperlegible";
		EsButtonOnCommand(button, ButtonFont);
		button = EsButtonCreate(fonts, ES_BUTTON_RADIOBOX | ES_CELL_H_EXPAND, 0, EsLiteral("OpenDyslexic"));
		button->userData = (void *) "OpenDyslexic";
		EsButtonOnCommand(button, ButtonFont);

		EsTextDisplayCreate(panelCustomizeOptions, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH_SECONDARY, INTERFACE_STRING(InstallerCustomizeOptionsHint));

		EsSpacerCreate(panelCustomizeOptions, ES_CELL_FILL);

		EsPanel *buttonsRow = EsPanelCreate(panelCustomizeOptions, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL, &styleButtonsRow);
		EsSpacerCreate(buttonsRow, ES_CELL_H_FILL);
		finishButton = EsButtonCreate(buttonsRow, ES_ELEMENT_DISABLED, 0, INTERFACE_STRING(InstallerFinish));
		EsButtonOnCommand(finishButton, ButtonFinish);
	}

	{
		panelWait = EsPanelCreate(switcher, ES_CELL_FILL, &styleRoot);
		EsTextDisplayCreate(panelWait, ES_CELL_H_FILL, ES_STYLE_TEXT_HEADING0, INTERFACE_STRING(InstallerTitle));
		progressDisplay = EsTextDisplayCreate(panelWait, ES_TEXT_DISPLAY_RICH_TEXT | ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH);
	}

	{
		panelComplete = EsPanelCreate(switcher, ES_CELL_FILL, &styleRoot);
		EsTextDisplayCreate(panelComplete, ES_CELL_H_FILL, ES_STYLE_TEXT_HEADING0, INTERFACE_STRING(InstallerTitle));

		EsVolumeInformation information;

		if (EsMountPointGetVolumeInformation(EsLiteral("0:/"), &information) && information.driveType == ES_DRIVE_TYPE_USB_MASS_STORAGE) {
			EsTextDisplayCreate(panelComplete, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(InstallerCompleteFromUSB));
		} else {
			EsTextDisplayCreate(panelComplete, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(InstallerCompleteFromOther));
		}

		// TODO Failure messages.

		EsSpacerCreate(panelComplete, ES_CELL_FILL);
		EsPanel *buttonsRow = EsPanelCreate(panelComplete, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL, &styleButtonsRow);
		EsSpacerCreate(buttonsRow, ES_CELL_H_FILL);
		EsButtonOnCommand(EsButtonCreate(buttonsRow, ES_FLAGS_DEFAULT, 0, INTERFACE_STRING(DesktopRestartAction)), ButtonRestart);
	}

	EsDeviceEnumerate([] (EsMessageDevice device, EsGeneric) {
		ConnectedDriveAdd(device);
	}, 0);

	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_DEVICE_CONNECTED) {
			if (!startedInstallation) {
				ConnectedDriveAdd(message->device);
			}
		} else if (message->type == ES_MSG_DEVICE_DISCONNECTED) {
			if (!startedInstallation) {
				ConnectedDriveRemove(message->device);
			}
		} else if (message->type == ES_MSG_REGISTER_FILE_SYSTEM) {
			EsVolumeInformation information;

			if (EsMountPointGetVolumeInformation(message->registerFileSystem.mountPoint->prefix, message->registerFileSystem.mountPoint->prefixBytes, &information)) {
				bool isBootable = false;

				for (uintptr_t i = 0; i < sizeof(EsUniqueIdentifier); i++) {
					if (information.installationIdentifier.d[i]) {
						isBootable = true;
						break;
					}
				}

				if (isBootable && 0 == EsMemoryCompare(&information.installationIdentifier, &installationIdentifier, sizeof(EsUniqueIdentifier))) {
					newFileSystemMountPoint = *message->registerFileSystem.mountPoint;
					EsEventSet(mountNewFileSystemEvent);
				}
			}
		} else if (message->type == MSG_SET_PROGRESS) {
			if (progress != message->user.context1.u) {
				char buffer[128];
				progress = message->user.context1.u;
				EsAssert(progress <= 100);
				size_t bytes = EsStringFormat(buffer, sizeof(buffer), "%z%d%z", 
						interfaceString_InstallerProgressMessage, progress, interfaceString_CommonUnitPercent);
				EsTextDisplaySetContents(progressDisplay, buffer, bytes);

				if (onWaitScreen && progress == 100) {
					onWaitScreen = false;
					EsPanelSwitchTo(switcher, panelComplete, ES_TRANSITION_FADE_IN);
				}
			}
		}
	}
}
