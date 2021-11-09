// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

// TODO Validation of all fields.

#include <module.h>

#define SECTOR_SIZE (2048)

struct LBE16 {
#ifdef __BIG_ENDIAN__
	uint16_t _u, x;
#else
	uint16_t x, _u;
#endif
};

struct LBE32 {
#ifdef __BIG_ENDIAN__
	uint32_t _u, x;
#else
	uint32_t x, _u;
#endif
};

struct DateTime {
	char year[4];
	char month[2];
	char day[2];
	char hour[2];
	char minute[2];
	char second[2];
	char centiseconds[2];
	int8_t timeZoneOffset;
} ES_STRUCT_PACKED;

struct DateTime2 {
	uint8_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	int8_t timeZoneOffset;
} ES_STRUCT_PACKED;

struct DirectoryRecord {
	uint8_t length;
	uint8_t extendedAttributeLength;
	LBE32 extentStart;
	LBE32 extentSize;
	DateTime2 recordingTime;
	uint8_t flags;
	uint8_t interleavedUnitSize;
	uint8_t interleavedGapSize;
	LBE16 volumeSequenceNumber;
	uint8_t fileNameBytes;
} ES_STRUCT_PACKED;

struct PrimaryDescriptor {
	uint8_t typeCode;
	char signature[5];
	uint8_t version;
	uint8_t _unused0;
	char systemIdentifier[32];
	char volumeIdentifier[32];
	uint8_t _unused1[8];
	LBE32 volumeSize;
	uint8_t _unused2[32];
	LBE16 volumeSetSize;
	LBE16 volumeSequenceNumber;
	LBE16 logicalBlockSize;
	LBE32 pathTableSize;
	uint32_t pathTableLittle;
	uint32_t optionalPathTableLittle;
	uint32_t pathTableBig;
	uint32_t optionalPathTableBig;
	DirectoryRecord rootDirectory;
	char rootDirectoryName;
	char volumeSetIdentifier[128];
	char publisherIdentifier[128];
	char dataPreparerIdentifier[128];
	char applicationIdentifier[128];
	char copyrightFileIdentifier[38];
	char abstractFileIdentifier[36];
	char bibliographicFileIdentifier[37];
	DateTime volumeCreationTime;
	DateTime volumeModificationTime;
	DateTime volumeExpirationTime;
	DateTime volumeEffectiveTime;
	uint8_t fileStructureVersion;
	uint8_t _unused3;
	char applicationSpecific[512];
	uint8_t _unused4[653];
} ES_STRUCT_PACKED;

struct DirectoryRecordReference {
	uint32_t sector, offset;
};

struct FSNode {
	struct Volume *volume;
	DirectoryRecord record;
};

struct Volume : KFileSystem {
	PrimaryDescriptor primaryDescriptor;
};

static EsError ScanInternal(const char *name, size_t nameBytes, KNode *_directory, DirectoryRecord *_record = nullptr);

static bool Mount(Volume *volume) {
#define MOUNT_FAILURE(message) do { KernelLog(LOG_ERROR, "ISO9660", "mount failure", "Mount - " message); return false; } while (0)

	uintptr_t descriptorIndex = 0;

	while (true) {
		if (ES_SUCCESS != volume->Access(32768 + SECTOR_SIZE * descriptorIndex, SECTOR_SIZE, K_ACCESS_READ, &volume->primaryDescriptor, ES_FLAGS_DEFAULT)) {
			MOUNT_FAILURE("Could not access descriptor list.\n");
		}	

		if (0 != EsMemoryCompare(volume->primaryDescriptor.signature, "CD001", 5)) {
			MOUNT_FAILURE("Invalid descriptor signature.\n");
		}

		if (volume->primaryDescriptor.typeCode == 1) {
			break;
		}

		if (volume->primaryDescriptor.typeCode == 0xFF) {
			MOUNT_FAILURE("Could not find primary descriptor in descriptor list.\n");
		}

		if (++descriptorIndex > 16) {
			MOUNT_FAILURE("Could not find end of descriptor list.\n");
		}
	}

	if (volume->primaryDescriptor.version != 1 || volume->primaryDescriptor.fileStructureVersion != 1) {
		MOUNT_FAILURE("Unsupported file system version.\n");
	}

	if (volume->primaryDescriptor.logicalBlockSize.x != SECTOR_SIZE) {
		MOUNT_FAILURE("Unsupported block size.\n");
	}

	{
		FSNode *root = (FSNode *) EsHeapAllocate(sizeof(FSNode), true, K_FIXED);

		if (!root) {
			MOUNT_FAILURE("Could not allocate root node.\n");
		}

		volume->rootDirectory->driverNode = root;
		volume->rootDirectoryInitialChildren = volume->primaryDescriptor.rootDirectory.extentSize.x / sizeof(DirectoryRecord);

		root->volume = volume;
		root->record = volume->primaryDescriptor.rootDirectory;
	}

	{
		// Is this the boot disc?

		EsUniqueIdentifier identifier = KGetBootIdentifier();

		if (0 != EsMemoryCompare("Essence::", volume->primaryDescriptor.applicationSpecific, 9)) {
			goto notBoot;
		}

		if (EsMemoryCompare(&identifier, volume->primaryDescriptor.applicationSpecific + 9, 16)) {
			goto notBoot;
		}

		DirectoryRecord record = {};
		ScanInternal(EsLiteral("ESSENCE.DAT;1"), volume->rootDirectory, &record);
		record.extentSize.x = (record.extentSize.x + SECTOR_SIZE - 1) / SECTOR_SIZE;

		if (!record.length || record.extentStart.x >= volume->block->information.sectorCount 
				|| record.extentSize.x >= volume->block->information.sectorCount - record.extentStart.x) {
			goto notBoot;
		}

		// Load the first sector to look at the MBR.

		uint8_t *firstSector = (uint8_t *) EsHeapAllocate(SECTOR_SIZE, false, K_FIXED);

		if (!firstSector) {
			KernelLog(LOG_ERROR, "ISO9660", "allocation failure", "Could not allocate sector buffer to check MBR.\n");
			goto notBoot;
		}

		EsDefer(EsHeapFree(firstSector, SECTOR_SIZE, K_FIXED));

		if (ES_SUCCESS != volume->Access(record.extentStart.x * SECTOR_SIZE, SECTOR_SIZE, K_ACCESS_READ, firstSector, ES_FLAGS_DEFAULT)) {
			goto notBoot;
		}	

		uint32_t sectorOffset = ((uint32_t) firstSector[0x1BE + 8] << 0) + ((uint32_t) firstSector[0x1BE + 9] << 8)
			+ ((uint32_t) firstSector[0x1BE + 10] << 16) + ((uint32_t) firstSector[0x1BE + 11] << 24);
		sectorOffset /= (SECTOR_SIZE / 512); // Convert to disc sectors.

		if (sectorOffset >= record.extentSize.x) {
			goto notBoot;
		}

		record.extentStart.x += sectorOffset;
		record.extentSize.x -= sectorOffset;

		KernelLog(LOG_INFO, "ISO9660", "found boot disc", "Found boot disc. Image at %d/%d.\n",
				record.extentStart.x, record.extentSize.x);
		FSPartitionDeviceCreate(volume->block, record.extentStart.x, record.extentSize.x, ES_FLAGS_DEFAULT, EsLiteral("CD-ROM boot partition"));
	}

	notBoot:;

	return true;
}

static size_t Read(KNode *node, void *_buffer, EsFileOffset offset, EsFileOffset count) {
#define READ_FAILURE(message, error) do { KernelLog(LOG_ERROR, "ISO9660", "read failure", "Read - " message); return error; } while (0)

	FSNode *file = (FSNode *) node->driverNode;
	Volume *volume = file->volume;

	uint8_t *sectorBuffer = (uint8_t *) EsHeapAllocate(SECTOR_SIZE, false, K_FIXED);

	if (!sectorBuffer) {
		READ_FAILURE("Could not allocate sector buffer.\n", ES_ERROR_INSUFFICIENT_RESOURCES);
	}

	EsDefer(EsHeapFree(sectorBuffer, SECTOR_SIZE, K_FIXED));

	uint8_t *outputBuffer = (uint8_t *) _buffer;

	uint64_t firstSector = offset / SECTOR_SIZE;
	uint32_t lba = file->record.extentStart.x + firstSector;
	offset %= SECTOR_SIZE;

	while (count) {
		if (offset || count < SECTOR_SIZE) {
			EsError error = volume->Access(lba * SECTOR_SIZE, SECTOR_SIZE, K_ACCESS_READ, sectorBuffer, ES_FLAGS_DEFAULT);
			if (error != ES_SUCCESS) READ_FAILURE("Could not read file sector.\n", error);

			uint64_t bytesToRead = (count > SECTOR_SIZE - offset) ? (SECTOR_SIZE - offset) : count;
			EsMemoryCopy(outputBuffer, sectorBuffer + offset, bytesToRead);

			lba++, count -= bytesToRead, offset = 0, outputBuffer += bytesToRead;
		} else {
			uint64_t sectorsToRead = count / SECTOR_SIZE;
			EsError error = volume->Access(lba * SECTOR_SIZE, sectorsToRead * SECTOR_SIZE, K_ACCESS_READ, outputBuffer, ES_FLAGS_DEFAULT);
			if (error != ES_SUCCESS) READ_FAILURE("Could not read file sectors.\n", error);
			lba += sectorsToRead, count -= SECTOR_SIZE * sectorsToRead, outputBuffer += SECTOR_SIZE * sectorsToRead;
		}
	}

	return true;
}

static EsError Enumerate(KNode *node) {
#define ENUMERATE_FAILURE(message, error) do { KernelLog(LOG_ERROR, "ISO9660", "enumerate failure", "Enumerate - " message); return error; } while (0)

	FSNode *directory = (FSNode *) node->driverNode;
	Volume *volume = directory->volume;

	// TODO Load multiple sectors at once?

	uint8_t *sectorBuffer = (uint8_t *) EsHeapAllocate(SECTOR_SIZE, false, K_FIXED);

	if (!sectorBuffer) {
		ENUMERATE_FAILURE("Could not allocate sector buffer.\n", ES_ERROR_INSUFFICIENT_RESOURCES);
	}

	EsDefer(EsHeapFree(sectorBuffer, SECTOR_SIZE, K_FIXED));

	uint32_t currentSector = directory->record.extentStart.x;
	uint32_t remainingBytes = directory->record.extentSize.x;

	while (remainingBytes) {
		EsError accessResult = volume->Access(currentSector * SECTOR_SIZE, SECTOR_SIZE, K_ACCESS_READ, (uint8_t *) sectorBuffer, ES_FLAGS_DEFAULT);

		if (accessResult != ES_SUCCESS) {
			ENUMERATE_FAILURE("Could not read sector.\n", accessResult);
		}

		uintptr_t positionInSector = 0;

		while (positionInSector < SECTOR_SIZE && positionInSector < remainingBytes) {
			DirectoryRecord *record = (DirectoryRecord *) (sectorBuffer + positionInSector);

			if (!record->length) {
				break;
			}

			if (positionInSector + record->length > SECTOR_SIZE || record->length < sizeof(DirectoryRecord)) {
				ENUMERATE_FAILURE("Invalid directory record.\n", ES_ERROR_CORRUPT_DATA);
			}

			if (record->fileNameBytes <= 2) {
				goto nextEntry;
			}

			{
				KNodeMetadata metadata = {};

				size_t nameBytes = record->fileNameBytes;

				for (uintptr_t i = 0; i < record->fileNameBytes; i++) {
					if (((char *) (record + 1))[i] == ';') {
						nameBytes = i;
						break;
					}
				}

				metadata.type = (record->flags & (1 << 1)) ? ES_NODE_DIRECTORY : ES_NODE_FILE;

				if (metadata.type == ES_NODE_DIRECTORY) {
					metadata.directoryChildren = ES_DIRECTORY_CHILDREN_UNKNOWN;
					metadata.totalSize = 0;
				} else if (metadata.type == ES_NODE_FILE) {
					metadata.totalSize = record->extentSize.x;
				}

				DirectoryRecordReference reference = {};
				reference.sector = currentSector;
				reference.offset = positionInSector;

				EsError error = FSDirectoryEntryFound(node, &metadata, &reference, 
						(const char *) (record + 1), nameBytes, false);

				if (error != ES_SUCCESS) {
					return error;
				}
			}

			nextEntry:;
			positionInSector += record->length;
		}

		if (remainingBytes < SECTOR_SIZE) {
			remainingBytes = 0;
		} else {
			remainingBytes -= SECTOR_SIZE;
		}
	}

	return ES_SUCCESS;
}

static EsError ScanInternal(const char *name, size_t nameBytes, KNode *_directory, DirectoryRecord *_record) {
#define SCAN_FAILURE(message, error) do { KernelLog(LOG_ERROR, "ISO9660", "scan failure", "Scan - " message); return error; } while (0)

	// Check for invalid characters.

	for (uintptr_t i = 0; i < nameBytes; i++) {
		bool validCharacter = name[i] == '.' || name[i] == ';' || name[i] == '_' 
			|| (name[i] >= 'A' && name[i] <= 'Z')
			|| (name[i] >= '0' && name[i] <= '9');

		if (!validCharacter) {
			return ES_ERROR_FILE_DOES_NOT_EXIST;
		}
	}

	FSNode *directory = (FSNode *) _directory->driverNode;
	Volume *volume = directory->volume;

	// TODO Load multiple sectors at once?

	uint8_t *sectorBuffer = (uint8_t *) EsHeapAllocate(SECTOR_SIZE, false, K_FIXED);

	if (!sectorBuffer) {
		SCAN_FAILURE("Could not allocate sector buffer.\n", ES_ERROR_INSUFFICIENT_RESOURCES);
	}

	EsDefer(EsHeapFree(sectorBuffer, SECTOR_SIZE, K_FIXED));

	uint32_t currentSector = directory->record.extentStart.x;
	uint32_t remainingBytes = directory->record.extentSize.x;

	while (remainingBytes) {
		EsError accessResult = volume->Access(currentSector * SECTOR_SIZE, SECTOR_SIZE, K_ACCESS_READ, (uint8_t *) sectorBuffer, ES_FLAGS_DEFAULT);

		if (accessResult != ES_SUCCESS) {
			SCAN_FAILURE("Could not read sector.\n", accessResult);
		}

		uintptr_t positionInSector = 0;

		while (positionInSector < SECTOR_SIZE && positionInSector < remainingBytes) {
			DirectoryRecord *record = (DirectoryRecord *) (sectorBuffer + positionInSector);

			if (!record->length) {
				break;
			}

			if (positionInSector + record->length > SECTOR_SIZE || record->length < sizeof(DirectoryRecord)) {
				SCAN_FAILURE("Invalid directory record.\n", ES_ERROR_CORRUPT_DATA);
			}

			if (record->fileNameBytes <= 2) {
				goto nextEntry;
			}

			if (!((nameBytes == record->fileNameBytes && 0 == EsMemoryCompare(record + 1, name, nameBytes))
					|| (nameBytes + 2 == record->fileNameBytes && 0 == EsMemoryCompare(record + 1, name, nameBytes) 
						&& 0 == EsMemoryCompare((char *) (record + 1) + record->fileNameBytes - 2, ";1", 2)))) {
				goto nextEntry;
			}

			if (_record) {
				EsMemoryCopy(_record, record, sizeof(DirectoryRecord));
				return ES_SUCCESS;
			}

			{
				KNodeMetadata metadata = {};

				metadata.type = (record->flags & (1 << 1)) ? ES_NODE_DIRECTORY : ES_NODE_FILE;

				if (metadata.type == ES_NODE_DIRECTORY) {
					metadata.directoryChildren = ES_DIRECTORY_CHILDREN_UNKNOWN;
					metadata.totalSize = 0;
				} else if (metadata.type == ES_NODE_FILE) {
					metadata.totalSize = record->extentSize.x;
				}

				DirectoryRecordReference reference = {};
				reference.sector = currentSector;
				reference.offset = positionInSector;

				return FSDirectoryEntryFound(_directory, &metadata, &reference, 
						name, nameBytes, false);
			}

			nextEntry:;
			positionInSector += record->length;
		}

		if (remainingBytes < SECTOR_SIZE) {
			remainingBytes = 0;
		} else {
			remainingBytes -= SECTOR_SIZE;
		}
	}

	return ES_ERROR_FILE_DOES_NOT_EXIST;
}

static EsError Scan(const char *name, size_t nameBytes, KNode *_directory) {
	return ScanInternal(name, nameBytes, _directory);
}

static EsError Load(KNode *_directory, KNode *_node, KNodeMetadata *, const void *entryData) {
	DirectoryRecordReference reference = *(DirectoryRecordReference *) entryData;
	FSNode *directory = (FSNode *) _directory->driverNode;
	Volume *volume = directory->volume;

	uint8_t *sectorBuffer = (uint8_t *) EsHeapAllocate(SECTOR_SIZE, false, K_FIXED);

	if (!sectorBuffer) {
		return ES_ERROR_INSUFFICIENT_RESOURCES;
	}

	EsDefer(EsHeapFree(sectorBuffer, SECTOR_SIZE, K_FIXED));

	EsError error = volume->Access(reference.sector * SECTOR_SIZE, SECTOR_SIZE, K_ACCESS_READ, (uint8_t *) sectorBuffer, ES_FLAGS_DEFAULT);
	if (error != ES_SUCCESS) return error;

	FSNode *data = (FSNode *) EsHeapAllocate(sizeof(FSNode), true, K_FIXED);

	if (!data) { 
		return ES_ERROR_INSUFFICIENT_RESOURCES;
	}

	data->volume = volume;
	_node->driverNode = data;

	DirectoryRecord *record = (DirectoryRecord *) (sectorBuffer + reference.offset);
	EsMemoryCopy(&data->record, record, sizeof(DirectoryRecord));

	return ES_SUCCESS;
}

static void Close(KNode *node) {
	EsHeapFree(node->driverNode, sizeof(FSNode), K_FIXED);
}

static void DeviceAttach(KDevice *parent) {
	Volume *volume = (Volume *) KDeviceCreate("ISO9660", parent, sizeof(Volume));

	if (!volume || !FSFileSystemInitialise(volume)) {
		KernelLog(LOG_ERROR, "ISO9660", "allocate error", "DeviceAttach - Could not initialise volume.\n");
		return;
	}

	if (volume->block->information.sectorSize != SECTOR_SIZE) {
		KernelLog(LOG_ERROR, "ISO9660", "incorrect sector size", "DeviceAttach - Expected 2KB sectors, but drive's sectors are %D.\n", 
				volume->block->information.sectorSize);
		KDeviceDestroy(volume);
		return;
	}

	if (!Mount(volume)) {
		KernelLog(LOG_ERROR, "ISO9660", "mount failure", "DeviceAttach - Could not mount ISO9660 volume.\n");
		KDeviceDestroy(volume);
		return;
	}

	volume->read = Read;
	volume->scan = Scan;
	volume->load = Load;
	volume->enumerate = Enumerate;
	volume->close = Close;

	volume->spaceUsed = volume->primaryDescriptor.volumeSize.x * volume->primaryDescriptor.logicalBlockSize.x;
	volume->spaceTotal = volume->spaceUsed;

	uint64_t crc64 = CalculateCRC64(&volume->primaryDescriptor, sizeof(PrimaryDescriptor));
	EsMemoryCopy(&volume->identifier, &crc64, sizeof(crc64));

	volume->nameBytes = sizeof(volume->primaryDescriptor.volumeIdentifier);
	if (volume->nameBytes > sizeof(volume->name)) volume->nameBytes = sizeof(volume->name);
	EsMemoryCopy(volume->name, volume->primaryDescriptor.volumeIdentifier, volume->nameBytes);

	for (intptr_t i = volume->nameBytes - 1; i >= 0; i--) {
		if (volume->name[i] == ' ') {
			volume->nameBytes--;
		} else {
			break;
		}
	}

	volume->directoryEntryDataBytes = sizeof(DirectoryRecordReference);
	volume->nodeDataBytes = sizeof(FSNode);

	KernelLog(LOG_INFO, "ISO9660", "register file system", "DeviceAttach - Registering file system with name '%s'.\n", 
			volume->nameBytes, volume->name);
	FSRegisterFileSystem(volume); 
}

KDriver driverISO9660 = {
	.attach = DeviceAttach,
};
