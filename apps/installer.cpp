#define INSTALLER

#define ES_CRT_WITHOUT_PREFIX
#include <essence.h>

#include <shared/hash.cpp>
#include <shared/strings.cpp>
#include <shared/partitions.cpp>
#include <shared/array.cpp>
#include <shared/fat.cpp>
#include <ports/lzma/LzmaDec.c>

#define Log(...)
#define EsFSError() EsAssert(false)
#include <shared/esfs2.h>

// Assume an additional 64MB of storage capacity is needed on top of totalUncompressedBytes.
#define PARTITION_OVERHEAD (64 * 1024 * 1024)

#define MSG_SET_PROGRESS ((EsMessageType) (ES_MSG_USER_START + 1))

struct InstallerMetadata {
	uint64_t totalUncompressedBytes;
	uint64_t crc64;
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

const EsStyle styleTextboxMedium = {
	.inherit = ES_STYLE_TEXTBOX_BORDERED_SINGLE,

	.metrics = {
		.mask = ES_THEME_METRICS_PREFERRED_WIDTH,
		.preferredWidth = 80,
	},
};

InstallerMetadata *metadata;
Array<EsMessageDevice> connectedDrives;
EsListView *drivesList;
EsPanel *driveInformation;
EsObjectID selectedDriveID;
EsButton *installButton;
EsButton *useMBRCheckbox;
EsButton *finishButton;
EsPanel *switcher;
EsPanel *panelInstallOptions;
EsPanel *panelCustomizeOptions;
EsPanel *panelLicenses;
EsPanel *panelWait;
EsPanel *panelComplete;
EsPanel *panelError;
EsPanel *panelNotSupported;
EsTextbox *userNameTextbox;
EsTextbox *timeTextbox;
EsTextDisplay *progressDisplay;
const char *cSelectedFont;
int64_t clockOffsetMs;
uint8_t progress;
bool onWaitScreen;
bool startedInstallation;
bool useMBR;
EsBlockDeviceInformation blockDeviceInformation;
EsHandle driveHandle;
EsFileOffset partitionOffset;
EsFileOffset partitionBytes;
EsUniqueIdentifier installationIdentifier;
EsMountPoint newFileSystemMountPoint;
EsHandle mountNewFileSystemEvent;
EsError installError;
bool archiveCRCError;

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

	uint64_t crc64 = 0;
	uint64_t totalBytesExtracted = 0;
	uint8_t lastProgressByte = 0;

	EsMemoryCopy(e->pathBuffer, pathOut, pathOutBytes);

	while (true) {
		uint64_t fileSize;
		if (!Decompress(e, &fileSize, sizeof(fileSize))) break;
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

	return crc64 == metadata->crc64 ? ES_SUCCESS : ES_ERROR_CORRUPT_DATA;
}

/////////////////////////////////////////////

uint64_t writeOffset;
uint64_t writeBytes;
uint8_t writeBuffer[BUFFER_SIZE];
EsError formatError = ES_SUCCESS;

bool FlushWriteBuffer() {
	if (!writeBytes) return true;
	EsFileOffset parameters[2] = { partitionOffset * blockDeviceInformation.sectorSize + writeOffset, writeBytes };
	EsError error = EsDeviceControl(driveHandle, ES_DEVICE_CONTROL_BLOCK_WRITE, writeBuffer, parameters);
	writeBytes = 0;
	if (error != ES_SUCCESS) formatError = error;
	return error == ES_SUCCESS;
}

bool ReadBlock(uint64_t block, uint64_t count, void *buffer) {
	if (!FlushWriteBuffer()) {
		return false;
	}

	EsFileOffset parameters[2] = { partitionOffset * blockDeviceInformation.sectorSize + block * blockSize, count * blockSize };
	EsError error = EsDeviceControl(driveHandle, ES_DEVICE_CONTROL_BLOCK_READ, buffer, parameters);
	if (error != ES_SUCCESS) formatError = error;
	return error == ES_SUCCESS;
}

bool WriteBlock(uint64_t block, uint64_t count, void *buffer) {
	uint64_t offset = block * blockSize, bytes = count * blockSize;

	if (writeBytes && writeOffset + writeBytes == offset && writeBytes + bytes < sizeof(writeBuffer)) {
		EsMemoryCopy(writeBuffer + writeBytes, buffer, bytes);
		writeBytes += bytes;
		return true;
	} else {
		if (!FlushWriteBuffer()) {
			return false;
		}

		writeOffset = offset;
		writeBytes = bytes;
		EsMemoryCopy(writeBuffer, buffer, bytes);
		return true;
	}
}

bool WriteBytes(uint64_t byteOffset, uint64_t byteCount, void *buffer) {
	uint64_t firstSector = byteOffset / blockDeviceInformation.sectorSize;
	uint64_t lastSector = (byteOffset + byteCount - 1) / blockDeviceInformation.sectorSize;
	uint64_t sectorCount = lastSector - firstSector + 1;

	void *buffer2 = EsHeapAllocate(sectorCount * blockDeviceInformation.sectorSize, false);
	
	for (uintptr_t i = 0; i < sectorCount; i++) {
		if (i > 0 && i < sectorCount - 1) continue;
		EsFileOffset parameters[2] = { (partitionOffset + firstSector + i) * blockDeviceInformation.sectorSize, blockDeviceInformation.sectorSize } ;
		EsError error = EsDeviceControl(driveHandle, ES_DEVICE_CONTROL_BLOCK_READ, (uint8_t *) buffer2 + i * blockDeviceInformation.sectorSize, parameters);

		if (error != ES_SUCCESS) {
			formatError = error;
			return false;
		}
	}

	EsMemoryCopy((uint8_t *) buffer2 + byteOffset % blockDeviceInformation.sectorSize, buffer, byteCount);

	EsFileOffset parameters[2] = { (partitionOffset + firstSector) * blockDeviceInformation.sectorSize, sectorCount * blockDeviceInformation.sectorSize };
	EsError error = EsDeviceControl(driveHandle, ES_DEVICE_CONTROL_BLOCK_WRITE, buffer, parameters);

	if (error != ES_SUCCESS) {
		formatError = error;
		return false;
	}

	EsHeapFree(buffer2);
	return true;
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
		EsElementSetDisabled(useMBRCheckbox, true);
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

	uint8_t *signatureBlock = (uint8_t *) EsHeapAllocate(K_SIGNATURE_BLOCK_SIZE, false);
	EsFileOffset parameters[2] = { 0, K_SIGNATURE_BLOCK_SIZE };
	EsError readError = EsDeviceControl(device.handle, ES_DEVICE_CONTROL_BLOCK_READ, signatureBlock, parameters);
	bool alreadyHasPartitions = false;

	if (readError == ES_SUCCESS && information.sectorSize >= 0x200) {
		{
			MBRPartition partitions[4];

			if (MBRGetPartitions(signatureBlock, information.sectorCount, partitions)) {
				for (uintptr_t i = 0; i < 4; i++) {
					if (partitions[i].present) {
						alreadyHasPartitions = true;
					}
				}
			}
		}

		{
			GPTPartition partitions[GPT_PARTITION_COUNT];

			if (GPTGetPartitions(signatureBlock, information.sectorCount, information.sectorSize, partitions)) {
				for (uintptr_t i = 0; i < GPT_PARTITION_COUNT; i++) {
					if (partitions[i].present) {
						alreadyHasPartitions = true;
					}
				}
			}
		}
	}

	EsHeapFree(signatureBlock);

	bool showCapacity = false;

	// Sector sizes up to 4KB should be possible on GPT, but this hasn't been tested yet.
#if 0
	if (information.sectorSize < 0x200 || information.sectorSize > 0x1000) {
#else
	if (information.sectorSize != 0x200) {
#endif
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

	EsElementSetEnabled(useMBRCheckbox, information.sectorSize == 0x200);

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

EsError InstallMBR(EsBlockDeviceInformation driveInformation, uint8_t *sectorBuffer, EsMessageDevice drive, 
		void *mbr, EsMessage m, uint8_t *bootloader, size_t bootloaderSectors) {
	// Create the partition table.
	// TODO Adding new entries to existing tables.

	EsAssert(driveInformation.sectorSize == 0x200);
	EsError error;

	partitionOffset = 0x800;
	partitionBytes = driveInformation.sectorSize * (driveInformation.sectorCount - partitionOffset);

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

	return ES_SUCCESS;
}

void GenerateGUID(uint8_t *destination) {
	for (uintptr_t i = 0; i < 16; i++) {
		destination[i] = EsRandomU8();
	}

	destination[7] = 0x40 | (destination[6] & 0x0F);
	destination[8] = 0x80 | (destination[6] & 0x3F);
}

EsError FATAddFile(FATDirectoryEntry *directory, size_t directoryEntries, uint16_t *fat, 
		EsFileOffset totalSectors, EsFileOffset sectorOffset, bool isDirectory,
		const char *name, void *file, size_t fileBytes, EsHandle driveHandle, EsFileOffset *_sectorFirst) {
	// Allocate the sector extent.

	EsFileOffset sectorFirst = 0;
	size_t sectorCount = (fileBytes + blockDeviceInformation.sectorSize - 1) / blockDeviceInformation.sectorSize;

	for (uintptr_t i = 2; i < totalSectors - sectorOffset + 2; i++) {
		if (!fat[i]) {
			sectorFirst = i;
			break;
		}
	}

	EsAssert(sectorFirst);

	for (uintptr_t i = 0; i < sectorCount; i++) {
		fat[sectorFirst + i] = i == sectorCount - 1 ? 0xFFF8 : sectorFirst + i + 1;
	}

	// Write out the file contents.

	uint8_t *buffer = (uint8_t *) EsHeapAllocate(sectorCount * blockDeviceInformation.sectorSize, true);
	EsMemoryCopy(buffer, file, fileBytes);
	EsFileOffset parameters[2] = {};
	parameters[0] = (sectorFirst + sectorOffset) * blockDeviceInformation.sectorSize;
	parameters[1] = sectorCount * blockDeviceInformation.sectorSize;
	EsError error = EsDeviceControl(driveHandle, ES_DEVICE_CONTROL_BLOCK_WRITE, buffer, parameters);
	EsHeapFree(buffer);
	if (error != ES_SUCCESS) return error;

	// Create the directory entry.

	FATDirectoryEntry *entry = nullptr;

	for (uintptr_t i = 0; i < directoryEntries; i++) {
		if (!directory[i].name[0]) {
			entry = directory + i;
			break;
		}
	}

	EsAssert(entry);
	EsMemoryCopy(entry->name, name, 11);
	entry->firstClusterHigh = sectorFirst >> 16;
	entry->firstClusterLow = sectorFirst & 0xFFFF;
	entry->fileSizeBytes = isDirectory ? 0 : fileBytes;
	entry->attributes = isDirectory ? 0x10 : 0x00;
	entry->creationDate = 33;
	entry->accessedDate = 33;
	entry->modificationDate = 33;

	if (_sectorFirst) {
		*_sectorFirst = sectorFirst;
	}

	return ES_SUCCESS;
}

void FATAddDotFiles(FATDirectoryEntry *directory, EsFileOffset thisSector, EsFileOffset parentSector) {
	EsMemoryCopy(directory[0].name, ".          ", 11);
	directory[0].firstClusterHigh = thisSector >> 16;
	directory[0].firstClusterLow = thisSector & 0xFFFF;
	directory[0].attributes = 0x10;
	directory[0].creationDate = 33;
	directory[0].accessedDate = 33;
	directory[0].modificationDate = 33;
	EsMemoryCopy(directory[1].name, "..         ", 11);
	directory[1].firstClusterHigh = parentSector >> 16;
	directory[1].firstClusterLow = parentSector & 0xFFFF;
	directory[1].attributes = 0x10;
	directory[1].creationDate = 33;
	directory[1].accessedDate = 33;
	directory[1].modificationDate = 33;
}

EsError InstallGPT(EsBlockDeviceInformation driveInformation, EsMessageDevice drive, EsMessage m,
		void *uefi1, size_t uefi1Bytes, void *uefi2, size_t uefi2Bytes) {
	// Create the partition table.
	// TODO Adding new entries to existing tables.

	const uint8_t espGUID[] = { 0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B };
	const uint8_t dataGUID[] = { 0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47, 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4 };

	EsError error;
	EsFileOffset parameters[2] = {};

	size_t partitionEntryCount = 0x80;
	size_t tableSectors = (partitionEntryCount * sizeof(GPTEntry) + driveInformation.sectorSize - 1) / driveInformation.sectorSize;

	ProtectiveMBR *mbr = (ProtectiveMBR *) EsHeapAllocate(driveInformation.sectorSize, true);
	uint32_t mbrEntry[4];
	mbrEntry[0] = 0x00020000;
	mbrEntry[1] = 0xFFFFFFEE;
	mbrEntry[2] = 0x00000001;
	mbrEntry[3] = driveInformation.sectorCount > 0xFFFFFFFF ? 0xFFFFFFFF : driveInformation.sectorCount - 1;
	mbr->signature = 0xAA55;
	EsMemoryCopy(mbr->entry, mbrEntry, 16);

	GPTHeader *header = (GPTHeader *) EsHeapAllocate(driveInformation.sectorSize, true);
	EsMemoryCopy(header->signature, "EFI PART", 8);
	header->revision = 0x00010000;
	header->headerBytes = 0x5C;
	header->headerSelfLBA = 1;
	header->headerBackupLBA = driveInformation.sectorCount - 1;
	header->firstUsableLBA = 2 + tableSectors;
	header->lastUsableLBA = driveInformation.sectorCount - 2;
	GenerateGUID(header->driveGUID);
	header->tableLBA = 2;
	header->partitionEntryCount = partitionEntryCount;
	header->partitionEntryBytes = sizeof(GPTEntry);

	GPTEntry *partitionTable = (GPTEntry *) EsHeapAllocate(partitionEntryCount * sizeof(GPTEntry), true);
	EsMemoryCopy(partitionTable[0].typeGUID, espGUID, 16);
	GenerateGUID(partitionTable[0].partitionGUID);
	partitionTable[0].firstLBA = header->firstUsableLBA;
	partitionTable[0].lastLBA = partitionTable[0].firstLBA + 16777216 / driveInformation.sectorSize - 1;
	EsMemoryCopy(partitionTable[1].typeGUID, dataGUID, 16);
	GenerateGUID(partitionTable[1].partitionGUID);
	partitionTable[1].firstLBA = partitionTable[0].lastLBA + 1;
	partitionTable[1].lastLBA = header->lastUsableLBA;

	header->tableCRC32 = CalculateCRC32(partitionTable, partitionEntryCount * sizeof(GPTEntry));
	header->headerCRC32 = CalculateCRC32(header, header->headerBytes);

	GPTHeader *backupHeader = (GPTHeader *) EsHeapAllocate(driveInformation.sectorSize, true);
	EsMemoryCopy(backupHeader, header, driveInformation.sectorSize);
	backupHeader->headerSelfLBA = header->headerBackupLBA;
	backupHeader->headerBackupLBA = header->headerSelfLBA;
	backupHeader->headerCRC32 = 0;
	backupHeader->headerCRC32 = CalculateCRC32(backupHeader, header->headerBytes);

	parameters[0] = 0; 
	parameters[1] = driveInformation.sectorSize;
	error = EsDeviceControl(drive.handle, ES_DEVICE_CONTROL_BLOCK_WRITE, mbr, parameters);
	if (error != ES_SUCCESS) return error;
	parameters[0] = header->headerSelfLBA * driveInformation.sectorSize; 
	parameters[1] = driveInformation.sectorSize;
	error = EsDeviceControl(drive.handle, ES_DEVICE_CONTROL_BLOCK_WRITE, header, parameters);
	if (error != ES_SUCCESS) return error;
	parameters[0] = header->tableLBA * driveInformation.sectorSize; 
	parameters[1] = driveInformation.sectorSize * tableSectors;
	error = EsDeviceControl(drive.handle, ES_DEVICE_CONTROL_BLOCK_WRITE, partitionTable, parameters);
	if (error != ES_SUCCESS) return error;
	parameters[0] = header->headerBackupLBA * driveInformation.sectorSize; 
	parameters[1] = driveInformation.sectorSize;
	error = EsDeviceControl(drive.handle, ES_DEVICE_CONTROL_BLOCK_WRITE, backupHeader, parameters);
	if (error != ES_SUCCESS) return error;

	partitionOffset = partitionTable[1].firstLBA;
	partitionBytes = (partitionTable[1].lastLBA - partitionTable[1].firstLBA + 1) * driveInformation.sectorSize;

	m.user.context1.u = 2;
	EsMessagePost(nullptr, &m);

	// Load the kernel.

	size_t kernelBytes;
	void *kernel = EsFileReadAll(EsLiteral(K_SYSTEM_FOLDER "/Kernel.esx"), &kernelBytes);
	if (!kernel) return ES_ERROR_FILE_DOES_NOT_EXIST;

	m.user.context1.u = 3;
	EsMessagePost(nullptr, &m);

	// Format the ESP.

	EsAssert(driveInformation.sectorSize >= 0x200 && driveInformation.sectorSize <= 0x1000); // We only support FAT16.

	EsFileOffset espOffset = partitionTable[0].firstLBA;
	EsFileOffset espSectors = partitionTable[0].lastLBA - partitionTable[0].firstLBA + 1;
	EsAssert(espSectors * driveInformation.sectorSize == 16777216 && espSectors < 0x10000);

	SuperBlock16 *superBlock = (SuperBlock16 *) EsHeapAllocate(driveInformation.sectorSize, true);
	superBlock->jmp[0] = 0xEB;
	superBlock->jmp[1] = 0x3C;
	superBlock->jmp[2] = 0x90;
	EsMemoryCopy(superBlock->oemName, "MSWIN4.1", 8);
	superBlock->bytesPerSector = driveInformation.sectorSize;
	superBlock->sectorsPerCluster = 1;
	superBlock->reservedSectors = 1;
	superBlock->fatCount = 2;
	superBlock->rootDirectoryEntries = 512;
	superBlock->totalSectors = espSectors;
	superBlock->mediaDescriptor = 0xF8;
	superBlock->sectorsPerFAT16 = (superBlock->totalSectors * 2 + driveInformation.sectorSize - 1) / driveInformation.sectorSize;
	superBlock->sectorsPerTrack = 63;
	superBlock->heads = 256;
	superBlock->deviceID = 0x80;
	superBlock->signature = 0x29;
	superBlock->serial = (uint32_t) EsRandomU64();
	EsMemoryCopy(superBlock->label, "EFI SYSTEM ", 11);
	EsMemoryCopy(&superBlock->systemIdentifier, "FAT16   ", 8);
	superBlock->_unused1[448] = 0x55;
	superBlock->_unused1[449] = 0xAA;

	uint32_t rootDirectoryOffset = superBlock->reservedSectors + superBlock->fatCount * superBlock->sectorsPerFAT16 + espOffset;
	uint32_t rootDirectorySectors = (superBlock->rootDirectoryEntries * sizeof(FATDirectoryEntry) + (superBlock->bytesPerSector - 1)) / superBlock->bytesPerSector;
	uint32_t sectorOffset = rootDirectoryOffset + rootDirectorySectors - 2 * superBlock->sectorsPerCluster;
	size_t directoryEntriesPerSector = driveInformation.sectorSize / sizeof(FATDirectoryEntry);

	uint16_t *fat = (uint16_t *) EsHeapAllocate(superBlock->sectorsPerFAT16 * driveInformation.sectorSize, true);
	fat[0] = 0xFFF8;
	fat[1] = 0xFFFF;

	EsFileOffset efiDirectorySector, bootDirectorySector;
	FATDirectoryEntry *rootDirectory = (FATDirectoryEntry *) EsHeapAllocate(rootDirectorySectors * driveInformation.sectorSize, true);
	FATDirectoryEntry *efiDirectory = (FATDirectoryEntry *) EsHeapAllocate(driveInformation.sectorSize, true);
	FATDirectoryEntry *bootDirectory = (FATDirectoryEntry *) EsHeapAllocate(driveInformation.sectorSize, true);
	error = FATAddFile(rootDirectory, superBlock->rootDirectoryEntries, fat, superBlock->totalSectors, sectorOffset, false,
			"ESLOADERBIN", uefi2, uefi2Bytes, drive.handle, nullptr);
	if (error != ES_SUCCESS) return error;
	error = FATAddFile(rootDirectory, superBlock->rootDirectoryEntries, fat, superBlock->totalSectors, sectorOffset, false,
			"ESKERNELESX", kernel, kernelBytes, drive.handle, nullptr);
	if (error != ES_SUCCESS) return error;
	error = FATAddFile(rootDirectory, superBlock->rootDirectoryEntries, fat, superBlock->totalSectors, sectorOffset, false,
			"ESIID   DAT", &installationIdentifier, 16, drive.handle, nullptr);
	if (error != ES_SUCCESS) return error;
	error = FATAddFile(rootDirectory, superBlock->rootDirectoryEntries, fat, superBlock->totalSectors, sectorOffset, true,
			"EFI        ", efiDirectory, driveInformation.sectorSize, drive.handle, &efiDirectorySector);
	if (error != ES_SUCCESS) return error;
	FATAddDotFiles(efiDirectory, efiDirectorySector, 0);
	error = FATAddFile(efiDirectory, directoryEntriesPerSector, fat, superBlock->totalSectors, sectorOffset, true,
			"BOOT       ", bootDirectory, driveInformation.sectorSize, drive.handle, &bootDirectorySector);
	if (error != ES_SUCCESS) return error;
	FATAddDotFiles(bootDirectory, bootDirectorySector, efiDirectorySector);
	error = FATAddFile(bootDirectory, directoryEntriesPerSector, fat, superBlock->totalSectors, sectorOffset, false,
			"BOOTX64 EFI", uefi1, uefi1Bytes, drive.handle, nullptr);
	if (error != ES_SUCCESS) return error;

	m.user.context1.u = 4;
	EsMessagePost(nullptr, &m);

	parameters[0] = espOffset * driveInformation.sectorSize; 
	parameters[1] = driveInformation.sectorSize;
	error = EsDeviceControl(drive.handle, ES_DEVICE_CONTROL_BLOCK_WRITE, superBlock, parameters);
	if (error != ES_SUCCESS) return error;
	parameters[0] = (espOffset + 1) * driveInformation.sectorSize; 
	parameters[1] = superBlock->sectorsPerFAT16 * driveInformation.sectorSize;
	error = EsDeviceControl(drive.handle, ES_DEVICE_CONTROL_BLOCK_WRITE, fat, parameters);
	if (error != ES_SUCCESS) return error;
	parameters[0] = (espOffset + 1 + superBlock->sectorsPerFAT16) * driveInformation.sectorSize; 
	parameters[1] = superBlock->sectorsPerFAT16 * driveInformation.sectorSize;
	error = EsDeviceControl(drive.handle, ES_DEVICE_CONTROL_BLOCK_WRITE, fat, parameters);
	if (error != ES_SUCCESS) return error;
	parameters[0] = rootDirectoryOffset * driveInformation.sectorSize; 
	parameters[1] = rootDirectorySectors * driveInformation.sectorSize;
	error = EsDeviceControl(drive.handle, ES_DEVICE_CONTROL_BLOCK_WRITE, rootDirectory, parameters);
	if (error != ES_SUCCESS) return error;
	parameters[0] = (efiDirectorySector + sectorOffset) * blockDeviceInformation.sectorSize;
	parameters[1] = blockDeviceInformation.sectorSize;
	error = EsDeviceControl(driveHandle, ES_DEVICE_CONTROL_BLOCK_WRITE, efiDirectory, parameters);
	if (error != ES_SUCCESS) return error;
	parameters[0] = (bootDirectorySector + sectorOffset) * blockDeviceInformation.sectorSize;
	parameters[1] = blockDeviceInformation.sectorSize;
	error = EsDeviceControl(driveHandle, ES_DEVICE_CONTROL_BLOCK_WRITE, bootDirectory, parameters);
	if (error != ES_SUCCESS) return error;

	// Cleanup.

	EsHeapFree(mbr);
	EsHeapFree(header);
	EsHeapFree(partitionTable);
	EsHeapFree(backupHeader);
	EsHeapFree(superBlock);
	EsHeapFree(fat);
	EsHeapFree(rootDirectory);
	EsHeapFree(efiDirectory);
	EsHeapFree(bootDirectory);

	m.user.context1.u = 6;
	EsMessagePost(nullptr, &m);

	return ES_SUCCESS;
}

EsError Install() {
	EsMessage m = { MSG_SET_PROGRESS };
	EsError error;

	for (int i = 0; i < 16; i++) {
		installationIdentifier.d[i] = EsRandomU8();
	}

	size_t mbrBytes, stage1Bytes, stage2Bytes, uefi1Bytes, uefi2Bytes, kernelBytes;
	void *mbr = EsFileReadAll(EsLiteral("0:/mbr.dat"), &mbrBytes);
	void *stage1 = EsFileReadAll(EsLiteral("0:/stage1.dat"), &stage1Bytes);
	void *stage2 = EsFileReadAll(EsLiteral("0:/stage2.dat"), &stage2Bytes);
	void *uefi1 = EsFileReadAll(EsLiteral("0:/uefi1.dat"), &uefi1Bytes);
	void *uefi2 = EsFileReadAll(EsLiteral("0:/uefi2.dat"), &uefi2Bytes);

	if (!mbr || !stage1 || !stage2 || !uefi1 || !uefi2) {
		return ES_ERROR_FILE_DOES_NOT_EXIST;
	}

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

	// Create the partitions and possibly install the bootloader.

	if (useMBR) {
		error = InstallMBR(driveInformation, sectorBuffer, drive, mbr, m, bootloader, bootloaderSectors);
	} else {
		error = InstallGPT(driveInformation, drive, m, uefi1, uefi1Bytes, uefi2, uefi2Bytes);
	}

	if (error != ES_SUCCESS) {
		return error;
	}

	// Format the partition.

	void *kernel;

	if (useMBR) {
		kernel = EsFileReadAll(EsLiteral(K_SYSTEM_FOLDER "/Kernel.esx"), &kernelBytes);
		if (!kernel) return ES_ERROR_FILE_DOES_NOT_EXIST;
		m.user.context1.u = 6;
		EsMessagePost(nullptr, &m);
	} else {
		// The kernel is located on the ESP.
		kernel = nullptr;
		kernelBytes = 0;
	}

	Format(partitionBytes, interfaceString_InstallerVolumeLabel, installationIdentifier, kernel, kernelBytes);
	FlushWriteBuffer();

	if (formatError != ES_SUCCESS) {
		return formatError;
	}

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

	if (error == ES_ERROR_CORRUPT_DATA) {
		archiveCRCError = true;
	}

	if (error != ES_SUCCESS) {
		return error;
	}

	return ES_SUCCESS;
}

void InstallThread(EsGeneric) {
	EsPerformanceTimerPush();
	installError = Install();
	EsPrint("Installation finished in %Fs. Extracted %D from the archive. Error code = %d.\n", EsPerformanceTimerPop(), metadata->totalUncompressedBytes, installError);

	EsMessage m = { MSG_SET_PROGRESS };
	m.user.context1.u = 100;
	EsMessagePost(nullptr, &m);
}

void WriteNewConfiguration() {
	size_t newSystemConfigurationPathBytes, newSystemConfigurationBytes;
	char *newSystemConfigurationPath = EsStringAllocateAndFormat(&newSystemConfigurationPathBytes, "%s/" K_SYSTEM_FOLDER_NAME "/Default.ini", 
			newFileSystemMountPoint.prefixBytes, newFileSystemMountPoint.prefix);
	char *newSystemConfiguration = (char *) EsFileReadAll(newSystemConfigurationPath, newSystemConfigurationPathBytes, &newSystemConfigurationBytes); 

	size_t lineBufferBytes = 4096;
	char *lineBuffer = (char *) EsHeapAllocate(lineBufferBytes, false);
	EsINIState s = { .buffer = newSystemConfiguration, .bytes = newSystemConfigurationBytes };
	EsBuffer buffer = { .canGrow = true };

	while (EsINIParse(&s)) {
		if (!s.sectionClassBytes && 0 == EsStringCompareRaw(s.section, s.sectionBytes, EsLiteral("ui_fonts"))
				&& 0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("sans"))) {
			EsBufferFormat(&buffer, "sans=%z\n", cSelectedFont);
		} else if (!s.sectionClassBytes && 0 == EsStringCompareRaw(s.section, s.sectionBytes, EsLiteral("general"))
				&& 0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("installation_state"))) {
			EsBufferFormat(&buffer, "installation_state=0\n");
		} else if (!s.sectionClassBytes && 0 == EsStringCompareRaw(s.section, s.sectionBytes, EsLiteral("general"))
				&& 0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("clock_offset_ms"))) {
			EsAssert(false);
		} else if (!s.sectionClassBytes && 0 == EsStringCompareRaw(s.section, s.sectionBytes, EsLiteral("general"))
				&& 0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("user_name"))) {
			EsAssert(false);
		} else if (!s.sectionClassBytes && 0 == EsStringCompareRaw(s.section, s.sectionBytes, EsLiteral("general")) && !s.keyBytes) {
			size_t userNameBytes;
			char *userName = EsTextboxGetContents(userNameTextbox, &userNameBytes);
			EsBufferFormat(&buffer, "[general]\nclock_offset_ms=%d\nuser_name=%s\n", clockOffsetMs, userNameBytes, userName);
			EsHeapFree(userName);
		} else {
			size_t lineBytes = EsINIFormat(&s, lineBuffer, lineBufferBytes);
			EsBufferWrite(&buffer, lineBuffer, lineBytes);
			EsAssert(lineBytes < lineBufferBytes);
		}
	}

	EsFileWriteAll(newSystemConfigurationPath, newSystemConfigurationPathBytes, buffer.out, buffer.position);

	EsHeapFree(newSystemConfigurationPath);
	EsHeapFree(buffer.out);
	EsHeapFree(lineBuffer);
}

/////////////////////////////////////////////

void ButtonViewLicenses(EsInstance *, EsElement *, EsCommand *) {
	EsPanelSwitchTo(switcher, panelLicenses, ES_TRANSITION_FADE);
}

void ButtonInstallOptions(EsInstance *, EsElement *, EsCommand *) {
	EsPanelSwitchTo(switcher, panelInstallOptions, ES_TRANSITION_FADE);
}

void ButtonShutdown(EsInstance *, EsElement *, EsCommand *) {
	EsSyscall(ES_SYSCALL_SHUTDOWN, SHUTDOWN_ACTION_POWER_OFF, 0, 0, 0);
}

void ButtonRestart(EsInstance *, EsElement *, EsCommand *) {
	EsSyscall(ES_SYSCALL_SHUTDOWN, SHUTDOWN_ACTION_RESTART, 0, 0, 0);
}

void ButtonInstall(EsInstance *, EsElement *, EsCommand *) {
	useMBR = EsButtonGetCheck(useMBRCheckbox) == ES_CHECK_CHECKED;
	EsPanelSwitchTo(switcher, panelCustomizeOptions, ES_TRANSITION_FADE);
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

void Complete() {
	if (installError == ES_SUCCESS) {
		WriteNewConfiguration();
		EsPanelSwitchTo(switcher, panelComplete, ES_TRANSITION_FADE);
	} else {
		EsPanel *row = EsPanelCreate(panelError, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL);
		EsIconDisplayCreate(row, ES_FLAGS_DEFAULT, 0, ES_ICON_DIALOG_ERROR);
		EsSpacerCreate(row, ES_FLAGS_DEFAULT, 0, 15, 0);

		if (installError == ES_ERROR_INSUFFICIENT_RESOURCES) {
			EsTextDisplayCreate(row, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(InstallerFailedResources));
		} else if (archiveCRCError) {
			EsTextDisplayCreate(row, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(InstallerFailedArchiveCRCError));
		} else {
			EsTextDisplayCreate(row, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(InstallerFailedGeneric));
		}

		EsSpacerCreate(panelError, ES_CELL_FILL);
		EsPanel *buttonsRow = EsPanelCreate(panelError, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL, &styleButtonsRow);
		EsSpacerCreate(buttonsRow, ES_CELL_H_FILL);
		EsButtonOnCommand(EsButtonCreate(buttonsRow, ES_FLAGS_DEFAULT, 0, INTERFACE_STRING(DesktopRestartAction)), ButtonRestart);

		EsPanelSwitchTo(switcher, panelError, ES_TRANSITION_FADE);
	}
}

void ButtonFinish(EsInstance *, EsElement *, EsCommand *) {
	EsDateComponents base, modified;
	EsDateNowUTC(&base);
	modified = base;
	// TODO Proper date/time parsing.
	int64_t input = EsTextboxGetContentsAsDouble(timeTextbox); 
	modified.hour = input / 100;
	modified.minute = (input % 100) % 60;
	clockOffsetMs = DateToLinear(&modified) - DateToLinear(&base);

	if (progress == 100) {
		Complete();
	} else {
		onWaitScreen = true;
		EsPanelSwitchTo(switcher, panelWait, ES_TRANSITION_FADE);
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

	EsPanel *clearBackground = EsPanelCreate(window, ES_CELL_FILL, ES_STYLE_CLEAR_BACKGROUND);
	EsPanel *sheet = EsPanelCreate(clearBackground, ES_PANEL_VERTICAL | ES_CELL_PUSH | ES_CELL_CENTER, ES_STYLE_INSTALLER_ROOT);
	switcher = EsPanelCreate(sheet, ES_CELL_H_FILL | ES_PANEL_SWITCHER);
	switcher->messageUser = SwitcherMessage;

	{
		panelInstallOptions = EsPanelCreate(switcher, ES_CELL_H_FILL, &styleRoot);
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

		useMBRCheckbox = EsButtonCreate(panelInstallOptions, ES_CELL_H_FILL | ES_BUTTON_CHECKBOX | ES_ELEMENT_DISABLED, 0, INTERFACE_STRING(InstallerUseMBR));

		EsPanel *buttonsRow = EsPanelCreate(panelInstallOptions, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL, &styleButtonsRow);
		EsButtonOnCommand(EsButtonCreate(buttonsRow, ES_FLAGS_DEFAULT, 0, INTERFACE_STRING(InstallerViewLicenses)), ButtonViewLicenses);
		EsButtonOnCommand(EsButtonCreate(buttonsRow, ES_FLAGS_DEFAULT, ES_STYLE_PUSH_BUTTON_DANGEROUS, INTERFACE_STRING(DesktopShutdownAction)), ButtonShutdown);
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

		EsPanel *table = EsPanelCreate(panelCustomizeOptions, ES_CELL_H_CENTER | ES_PANEL_HORIZONTAL | ES_PANEL_TABLE, ES_STYLE_PANEL_FORM_TABLE);
		EsPanelSetBands(table, 2 /* columns */);

		EsTextDisplayCreate(table, ES_CELL_H_RIGHT, ES_STYLE_TEXT_LABEL, INTERFACE_STRING(InstallerUserName));
		userNameTextbox = EsTextboxCreate(table, ES_CELL_H_LEFT);
		userNameTextbox->messageUser = UserNameTextboxMessage;

		// TODO Proper date formatting.
		EsDateComponents date;
		EsDateNowUTC(&date);
		char timeBuffer[64];
		ptrdiff_t timeBytes = EsStringFormat(timeBuffer, sizeof(timeBuffer), "%d%d:%d%d", 
				date.hour / 10, date.hour % 10, date.minute / 10, date.minute % 10); 

		// TODO Make a date/time entry element or textbox overlay.
		EsTextDisplayCreate(table, ES_CELL_H_RIGHT, ES_STYLE_TEXT_LABEL, INTERFACE_STRING(InstallerTime));
		timeTextbox = EsTextboxCreate(table, ES_CELL_H_LEFT, &styleTextboxMedium);
		EsTextboxInsert(timeTextbox, timeBuffer, timeBytes);
		// TODO A date field.

		EsTextDisplayCreate(table, ES_CELL_H_RIGHT | ES_CELL_V_TOP, ES_STYLE_TEXT_RADIO_GROUP_LABEL, INTERFACE_STRING(InstallerSystemFont));
		EsPanel *fonts = EsPanelCreate(table, ES_CELL_H_LEFT | ES_PANEL_RADIO_GROUP);
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

		EsSpacerCreate(panelComplete, ES_CELL_FILL);
		EsPanel *buttonsRow = EsPanelCreate(panelComplete, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL, &styleButtonsRow);
		EsSpacerCreate(buttonsRow, ES_CELL_H_FILL);
		EsButtonOnCommand(EsButtonCreate(buttonsRow, ES_FLAGS_DEFAULT, 0, INTERFACE_STRING(DesktopRestartAction)), ButtonRestart);
	}

	{
		panelError = EsPanelCreate(switcher, ES_CELL_FILL, &styleRoot);
		EsTextDisplayCreate(panelError, ES_CELL_H_FILL, ES_STYLE_TEXT_HEADING0, INTERFACE_STRING(InstallerTitle));
		// Contents is created in Complete().
	}

	{
		panelNotSupported = EsPanelCreate(switcher, ES_CELL_FILL, &styleRoot);
		EsTextDisplayCreate(panelNotSupported, ES_CELL_H_FILL, ES_STYLE_TEXT_HEADING0, INTERFACE_STRING(InstallerTitle));

		EsPanel *row = EsPanelCreate(panelNotSupported, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL);
		EsIconDisplayCreate(row, ES_FLAGS_DEFAULT, 0, ES_ICON_DIALOG_ERROR);
		EsSpacerCreate(row, ES_FLAGS_DEFAULT, 0, 15, 0);

		EsTextDisplayCreate(row, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(InstallerNotSupported));

		EsSpacerCreate(panelNotSupported, ES_CELL_FILL);
		EsPanel *buttonsRow = EsPanelCreate(panelNotSupported, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL, &styleButtonsRow);
		EsSpacerCreate(buttonsRow, ES_CELL_H_FILL);
		EsButtonOnCommand(EsButtonCreate(buttonsRow, ES_FLAGS_DEFAULT, 0, INTERFACE_STRING(DesktopRestartAction)), ButtonRestart);
	}

	{
		MemoryAvailable available;
		EsSyscall(ES_SYSCALL_MEMORY_GET_AVAILABLE, (uintptr_t) &available, 0, 0, 0);

		if (available.total < 64 * 1024 * 1024) {
			EsPanelSwitchTo(switcher, panelNotSupported, ES_TRANSITION_NONE);
		} else {
			EsPanelSwitchTo(switcher, panelInstallOptions, ES_TRANSITION_NONE);
		}
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
					Complete();
				}
			}
		}
	}
}
