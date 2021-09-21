// TODO Validation of all fields.
// TODO Update to the latest file subsystem changes.
// 	- Set spaceUsed and spaceTotal.

#include <module.h>

struct BootSector {
	uint8_t	 jump[3]; 
	char	 signature[8];
	uint16_t bytesPerSector;
	uint8_t	 sectorsPerCluster;
	uint16_t reservedSectors;
	uint8_t	 unused0[3];
	uint16_t unused1;
	uint8_t	 media;
	uint16_t unused2;
	uint16_t sectorsPerTrack;
	uint16_t headsPerCylinder;
	uint32_t hiddenSectors;
	uint32_t unused3;
	uint32_t unused4;
	uint64_t totalSectors;
	uint64_t mftStart;
	uint64_t mftMirrorStart;
	int8_t   clustersPerFileRecord;
	uint8_t  unused5[3];
	int8_t   clustersPerIndexBlock;
	uint8_t  unused6[3];
	uint64_t serialNumber;
	uint32_t checksum;
	uint8_t	 bootloader[426];
	uint16_t bootSignature;
} ES_STRUCT_PACKED;

struct FileRecordHeader {
	uint32_t magic;
	uint16_t updateSequenceOffset;
	uint16_t updateSequenceSize;
	uint64_t logSequence;
	uint16_t sequenceNumber;
	uint16_t hardLinkCount;
	uint16_t firstAttributeOffset;
	uint16_t flags;
	uint32_t usedSize;
	uint32_t allocatedSize;
	uint64_t fileReference;
	uint16_t nextAttributeID;
	uint16_t unused;
	uint32_t recordNumber;
} ES_STRUCT_PACKED;

struct AttributeHeader {
	uint32_t attributeType;
	uint32_t length;
	uint8_t	 nonResident;
	uint8_t	 nameLength;
	uint16_t nameOffset;
	uint16_t flags;
	uint16_t attributeID;
} ES_STRUCT_PACKED;

struct ResidentAttributeHeader : AttributeHeader {
	uint32_t attributeLength;
	uint16_t attributeOffset;
	uint8_t	 indexed;
	uint8_t	 unused;
} ES_STRUCT_PACKED;

struct FileNameAttributeHeader {
	uint64_t parentRecordNumber   : 48;
	uint64_t parentSequenceNumber : 16;
	uint64_t creationTime;
	uint64_t modificationTime;
	uint64_t metadataModificationTime;
	uint64_t readTime;
	uint64_t allocatedSize;
	uint64_t realSize;
	uint32_t flags;
	uint32_t reparse;
	uint8_t	 fileNameLength;
	uint8_t	 namespaceType;
} ES_STRUCT_PACKED;

struct NonResidentAttributeHeader : AttributeHeader {
	uint64_t firstCluster;
	uint64_t lastCluster;
	uint16_t dataRunsOffset;
	uint16_t compressionUnit;
	uint32_t unused;
	uint64_t attributeAllocated;
	uint64_t attributeSize;
	uint64_t streamDataSize;
} ES_STRUCT_PACKED;

struct RunHeader {
	uint8_t	 lengthFieldBytes : 4;
	uint8_t	 offsetFieldBytes : 4;
} ES_STRUCT_PACKED;

struct IndexBlockHeader {
	uint32_t magic;
	uint16_t updateSequenceOffset;
	uint16_t updateSequenceSize;
	uint64_t logSequence;
	uint64_t selfVirtualCluster;
	uint32_t firstEntryOffset;
	uint32_t totalEntrySize;
	uint32_t allocatedSize;
	uint32_t flags;
} ES_STRUCT_PACKED;

struct IndexRootHeader {
	uint32_t attributeType;
	uint32_t collationRule;
	uint32_t bytesPerIndexRecord;
	uint8_t clustersPerIndexRecord;
	uint8_t unused[3];
	uint32_t firstEntryOffset;
	uint32_t totalEntrySize;
	uint32_t allocatedSize;
	uint32_t flags;
} ES_STRUCT_PACKED;

struct IndexEntryHeader {
	uint64_t fileRecordNumber   : 48;
	uint64_t fileSequenceNumber : 16;
	uint16_t indexEntryLength;
	uint16_t streamLength;
	uint32_t flags;
} ES_STRUCT_PACKED;

// TODO Support other MFT file record sizes.
#define MFT_FILE_SIZE (1024)

struct FSNode {
	struct Volume *volume;

	union {
		uint8_t fileRecord[MFT_FILE_SIZE];
		FileRecordHeader header;
	};
};

struct Volume : KDevice {
	KFileSystem *fileSystem;
	KNode *root;
	BootSector bootSector;

	size_t clusterBytes,
	       mftRecordBytes, 
	       indexBlockBytes,
	       clusterCount,
	       sectorBytes;

	FSNode mftNode, volumeNode;

	uint16_t *upCaseLookup[256];
};

static uint16_t ToUpper(Volume *volume, uint16_t character) {
	uint16_t *table = volume->upCaseLookup[character >> 8];
	return table ? table[character & 0xFF] : character;
}

static uint64_t GetDataRunLength(RunHeader *dataRun) {
	uint64_t length = 0;

	for (int i = 0; i < dataRun->lengthFieldBytes; i++) {
		length |= (uint64_t) (((uint8_t *) dataRun)[1 + i]) << (i * 8);
	}

	return length;
}

static uint64_t GetDataRunOffset(RunHeader *dataRun) {
	uint64_t offset = 0;

	for (int i = 0; i < dataRun->offsetFieldBytes; i++) {
		offset |= (uint64_t) (((uint8_t *) dataRun)[1 + dataRun->lengthFieldBytes + i]) << (i * 8);
	}

	if (offset & ((uint64_t) 1 << (dataRun->offsetFieldBytes * 8 - 1))) {
		for (int i = dataRun->offsetFieldBytes; i < 8; i++) {
			offset |= (uint64_t) 0xFF << (i * 8);
		}
	}

	return offset;
}

static bool ApplyFixup(Volume *volume, uint8_t *buffer, uint16_t fixupOffset, uint16_t fixupSize, size_t bufferBytes) {
#define FIXUP_FAILURE(message) do { KernelLog(LOG_ERROR, "NTFS", "fixup failure", "ApplyFixup - " message); return false; } while (0)

	// TODO Is the aliasing done by this function optimizations-safe?

	size_t sectorCount = bufferBytes / volume->sectorBytes;

	if ((fixupOffset & 1) || (fixupOffset + fixupSize > bufferBytes) || ((size_t) (fixupSize - 1) != sectorCount)) {
		FIXUP_FAILURE("Invalid offset/size.\n");
	}

	uint16_t *fixupArray = (uint16_t *) (buffer + fixupOffset);

	for (uintptr_t i = 0; i < sectorCount; i++) {
		uint16_t *position = (uint16_t *) (buffer + (i + 1) * volume->sectorBytes - 2);

		if (*position != fixupArray[0]) {
			FIXUP_FAILURE("Incorrect sequence number.\n");
		}

		*position = fixupArray[i + 1];
	}

	return true;
}

static bool ValidateFileRecord(Volume *volume, FSNode *node, bool isDirectory) {
#define VALIDATE_FILE_FAILURE(message) do { KernelLog(LOG_ERROR, "NTFS", "validate file record failure", "ValidateFileRecord - " message); return false; } while (0)

	FileRecordHeader *header = &node->header;

	if (header->magic != 0x454C4946) {
		VALIDATE_FILE_FAILURE("Invalid magic number.\n");
	}

	if (!ApplyFixup(volume, node->fileRecord, header->updateSequenceOffset, header->updateSequenceSize, MFT_FILE_SIZE)) {
		return false;
	}

	if (~header->flags & (1 << 0)) {
		VALIDATE_FILE_FAILURE("Record not in use.\n");
	}

	if (((header->flags & (1 << 1)) ? true : false) != isDirectory) {
		VALIDATE_FILE_FAILURE("Incorrect isDirectory flag.\n");
	}

	if (header->allocatedSize != MFT_FILE_SIZE || header->usedSize > MFT_FILE_SIZE) {
		VALIDATE_FILE_FAILURE("Invalid file header allocated/used size.\n");
	}

	if (header->firstAttributeOffset > MFT_FILE_SIZE - sizeof(AttributeHeader)) {
		VALIDATE_FILE_FAILURE("Invalid first attribute offset.\n");
	}

	return true;
}

static AttributeHeader *FindAttribute(Volume *, FSNode *node, uint8_t type, AttributeHeader *startingFrom = nullptr, bool optional = false) {
#define FIND_ATTRIBUTE_FAILURE(message) do { KernelLog(LOG_ERROR, "NTFS", "find attribute failure", "FindAttribute - " message); return nullptr; } while (0)

	AttributeHeader *attribute = startingFrom ?: (AttributeHeader *) (node->fileRecord + node->header.firstAttributeOffset);
	bool skip = startingFrom ? true : false;

	while (true) {
		if (attribute->attributeType == type && !skip) {
			return attribute;
		} else if (attribute->attributeType == 0xFFFFFFFF) {
			if (optional) return nullptr;
			FIND_ATTRIBUTE_FAILURE("Could not find attribute.\n");
		}

		skip = false;

		if ((size_t) ((uint8_t *) attribute - node->fileRecord + attribute->length) > MFT_FILE_SIZE - sizeof(AttributeHeader) 
				|| attribute->length < sizeof(AttributeHeader)) {
			FIND_ATTRIBUTE_FAILURE("Invalid attribute length.\n");
		}

		attribute = (AttributeHeader *) ((uint8_t *) attribute + attribute->length);
	}
}

static bool ReadFileCluster(Volume *volume, FSNode *node, uint64_t startCluster, void *_buffer) {
#define READ_FILE_CLUSTERS_FAILURE(message) do { KernelLog(LOG_ERROR, "NTFS", "read file clusters failure", "ReadFileClusters - " message); return false; } while (0)

	uint8_t *buffer = (uint8_t *) _buffer;

	// Find the data attribute.

	NonResidentAttributeHeader *dataHeader = (NonResidentAttributeHeader *) FindAttribute(volume, node, 0x80);

	if (!dataHeader) {
		// TODO Support $ATTRIBUTE_LIST.
		READ_FILE_CLUSTERS_FAILURE("Could not find data attribute.\n");
	}

	if (dataHeader->length < sizeof(NonResidentAttributeHeader)) {
		READ_FILE_CLUSTERS_FAILURE("Data attribute is too short.\n");
	}

	if (!dataHeader->nonResident) {
		// TODO Resident data attribute.
		READ_FILE_CLUSTERS_FAILURE("Data attribute is resident.\n");
	}

	if (dataHeader->flags) {
		READ_FILE_CLUSTERS_FAILURE("Data attribute has unsupported flags.\n");
	}

	// Find the data run containing the requested clusters.

	RunHeader *dataRun = (RunHeader *) ((uint8_t *) dataHeader + dataHeader->dataRunsOffset);
	uint64_t clusterNumber = 0;
	bool foundStart = false;
	uint64_t clusterCount = 1;

	KWorkGroup dispatchGroup = {};
	dispatchGroup.Initialise();

	while (((uint8_t *) dataRun - (uint8_t *) dataHeader) < dataHeader->length && dataRun->lengthFieldBytes) {
		uint64_t length = GetDataRunLength(dataRun), offset = GetDataRunOffset(dataRun);

		clusterNumber += offset;
		dataRun = (RunHeader *) ((uint8_t *) dataRun + 1 + dataRun->lengthFieldBytes + dataRun->offsetFieldBytes);

		if (!foundStart) {
			if (length <= startCluster) {
				startCluster -= length;
				continue;
			} else {
				foundStart = true;
			}
		}

		if (foundStart) {
			uint64_t firstClusterToRead = clusterNumber + startCluster;
			uint64_t clustersToRead = 1;
			startCluster = 0;

			// Read the clusters.

			volume->fileSystem->Access(firstClusterToRead * volume->clusterBytes, 
					clustersToRead * volume->clusterBytes, K_ACCESS_READ, 
					buffer, ES_FLAGS_DEFAULT, &dispatchGroup);

			clusterCount -= clustersToRead;
			buffer += clustersToRead * volume->clusterBytes;

			if (!clusterCount) {
				break;
			}
		}
	}

	if (clusterCount) {
		READ_FILE_CLUSTERS_FAILURE("Trying to read past the end of the data runs.\n");
	}

	// Wait for outstanding IO operations to complete.

	bool success = dispatchGroup.Wait();

	if (!success) {
		READ_FILE_CLUSTERS_FAILURE("Could not read clusters from block device.\n");
	}

	return true;
}

static bool ReadFileRecord(KNode *kNode, FSNode *node, uint64_t index, EsNodeType nodeType, void *clusterBuffer) {
#define READ_FILE_RECORD_FAILURE(message) do { KernelLog(LOG_ERROR, "NTFS", "read file record failure", "ReadFileRecord - " message); return false; } while (0)

	Volume *volume = node->volume;

	// Work out the cluster containing the file record.

	uint64_t cluster = index * MFT_FILE_SIZE / volume->clusterBytes;
	uint64_t offsetInCluster = index * MFT_FILE_SIZE % volume->clusterBytes;

	// Read this cluster.

	if (!ReadFileCluster(volume, &volume->mftNode, cluster, clusterBuffer)) {
		READ_FILE_RECORD_FAILURE("Could not read the file record from the MFT.\n");
	}

	// Copy into the node.

	EsMemoryCopy(node->fileRecord, (uint8_t *) clusterBuffer + offsetInCluster, MFT_FILE_SIZE);

	if (!ValidateFileRecord(volume, node, nodeType == ES_NODE_DIRECTORY)) {
		return false;
	}

	// Store information about the node.

	if (!kNode) {
		return true;
	}

	if (nodeType == ES_NODE_FILE) {
		AttributeHeader *dataHeader = (AttributeHeader *) FindAttribute(volume, node, 0x80);

		if (!dataHeader) {
			READ_FILE_RECORD_FAILURE("File did not have a data attribute.\n");
		}

		if (dataHeader->nonResident) {
			kNode->data.fileSize = ((NonResidentAttributeHeader *) dataHeader)->attributeSize;
		} else {
			kNode->data.fileSize = ((ResidentAttributeHeader *) dataHeader)->attributeLength;
		}

		kNode->data.type = ES_NODE_FILE;
	} else if (nodeType == ES_NODE_DIRECTORY) {
		NonResidentAttributeHeader *indexAllocationHeader = (NonResidentAttributeHeader *) FindAttribute(volume, node, 0xA0, nullptr, true);

		if (!indexAllocationHeader || indexAllocationHeader->flags || !indexAllocationHeader->nonResident) {
			kNode->data.directoryChildren = MFT_FILE_SIZE / 16;
		} else {
			// TODO This is a terrible upper estimate! Find a better one?
			kNode->data.directoryChildren = indexAllocationHeader->attributeSize / 16;
		}

		kNode->data.type = ES_NODE_DIRECTORY;
	}

	return true;
}

static bool Mount(Volume *volume) {
#define MOUNT_FAILURE(message) do { KernelLog(LOG_ERROR, "NTFS", "mount failure", "Mount - " message); return false; } while (0)

	// Load the boot sector.

	{
		void *buffer = EsHeapAllocate(volume->fileSystem->block->sectorSize, false, K_FIXED);

		if (!buffer) {
			MOUNT_FAILURE("Could not allocate buffer.\n");
		}

		EsDefer(EsHeapFree(buffer, volume->fileSystem->block->sectorSize, K_FIXED));

		if (!volume->fileSystem->Access(0, volume->fileSystem->block->sectorSize, K_ACCESS_READ, buffer, ES_FLAGS_DEFAULT)) {
			MOUNT_FAILURE("Could not read boot sector.\n");
		}

		EsMemoryCopy(&volume->bootSector, buffer, sizeof(BootSector));

		volume->sectorBytes = volume->bootSector.bytesPerSector;
		volume->clusterBytes = volume->bootSector.sectorsPerCluster * volume->bootSector.bytesPerSector;
		volume->mftRecordBytes = volume->bootSector.clustersPerFileRecord >= 0 
			? volume->bootSector.clustersPerFileRecord * volume->clusterBytes : (1 << -volume->bootSector.clustersPerFileRecord);
		volume->indexBlockBytes = volume->bootSector.clustersPerIndexBlock >= 0 
			? volume->bootSector.clustersPerIndexBlock * volume->clusterBytes : (1 << -volume->bootSector.clustersPerIndexBlock);
		volume->clusterCount = volume->fileSystem->block->sectorCount / volume->bootSector.sectorsPerCluster;

		if (volume->bootSector.bytesPerSector != volume->fileSystem->block->sectorSize) MOUNT_FAILURE("Invalid bytes per sector.\n");
		if ((volume->bootSector.sectorsPerCluster - 1) & volume->bootSector.sectorsPerCluster) MOUNT_FAILURE("Invalid sectors per cluster.\n");
		if (volume->mftRecordBytes != MFT_FILE_SIZE) MOUNT_FAILURE("Unsupported MFT record size.\n");
		if (volume->mftRecordBytes > volume->clusterBytes) MOUNT_FAILURE("MFT record size bigger than cluster size.\n");
		if (volume->indexBlockBytes % volume->clusterBytes) MOUNT_FAILURE("Index block size is not a multiple of the cluster size.\n");
		if (volume->bootSector.mftStart >= volume->clusterCount) MOUNT_FAILURE("MFT starts past end of volume.\n");
	}

	void *buffer = EsHeapAllocate(volume->clusterBytes, false, K_FIXED);

	if (!buffer) {
		MOUNT_FAILURE("Could not allocate buffer.\n");
	}

	EsDefer(EsHeapFree(buffer, volume->clusterBytes, K_FIXED));

	// Load the MFT.

	{
		if (!volume->fileSystem->Access(volume->bootSector.mftStart * volume->clusterBytes, 
					volume->clusterBytes, K_ACCESS_READ, buffer, ES_FLAGS_DEFAULT)) {
			MOUNT_FAILURE("Could not read MFT file.\n");
		}

		EsMemoryCopy(volume->mftNode.fileRecord, buffer, MFT_FILE_SIZE);

		if (!ValidateFileRecord(volume, &volume->mftNode, false)) {
			return false;
		}
	}

	// Load the volume node.

	{
		volume->volumeNode.volume = volume;

		if (!ReadFileRecord(nullptr, &volume->volumeNode, 3, ES_NODE_FILE, buffer)) {
			return false;
		}
	}

	// Load the root directory.

	{
		volume->root = FSNodeAllocate();

		if (!volume->root) {
			MOUNT_FAILURE("Could not allocate root node.\n");
		}

		volume->root->driverNode = EsHeapAllocate(sizeof(FSNode), true, K_FIXED);

		if (!volume->root->driverNode) {
			MOUNT_FAILURE("Could not allocate root node.\n");
		}

		FSNode *root = (FSNode *) volume->root->driverNode;
		root->volume = volume;

		if (!ReadFileRecord(volume->root, root, 5, ES_NODE_DIRECTORY, buffer)) {
			return false;
		}

		if (!FSRegisterNewNode(volume->root, nullptr, ES_NODE_DIRECTORY, EsLiteral("$Root"))) {
			MOUNT_FAILURE("Could not register root directory node.\n");
		}
	}

	// Load $UpCase.

	{
		FSNode *upCase = (FSNode *) EsHeapAllocate(sizeof(FSNode), true, K_FIXED);

		if (!upCase) {
			MOUNT_FAILURE("Could not allocate upCase node.\n");
		}

		EsDefer(EsHeapFree(upCase, sizeof(FSNode), K_FIXED));

		upCase->volume = volume;

		if (!ReadFileRecord(nullptr, upCase, 10, ES_NODE_FILE, buffer)) {
			return false;
		}

		for (uintptr_t i = 0; i < 131072 / volume->clusterBytes; i++) {
			if (!ReadFileCluster(volume, upCase, i, buffer)) {
				return false;
			}

			for (uint16_t j = 0, codepoint = i * volume->clusterBytes / 2; j < volume->clusterBytes / 2; j++, codepoint++) {
				uint16_t upperCase = ((uint16_t *) buffer)[j];
				if (codepoint == upperCase) continue;

				uint8_t msb = codepoint >> 8, lsb = codepoint & 0xFF;

				if (!volume->upCaseLookup[msb]) {
					volume->upCaseLookup[msb] = (uint16_t *) EsHeapAllocate(512, false, K_FIXED);

					if (!volume->upCaseLookup[msb]) {
						MOUNT_FAILURE("Could not allocate upCase table.\n");
					}

					for (uintptr_t i = 0; i < 256; i++) {
						volume->upCaseLookup[msb][i] = (msb << 8) | i;
					}
				}

				volume->upCaseLookup[msb][lsb] = upperCase;
			}
		}
	}

	return true;
}

#define BAD_ATTRIBUTE_FAILURE(message) do { KernelLog(LOG_ERROR, "NTFS", "bad attribute failure", "GetIndexAllocationHeader - " message); return nullptr; } while (0)

static NonResidentAttributeHeader *GetIndexAllocationHeader(Volume *volume, FSNode *directory) {
	NonResidentAttributeHeader *indexAllocationHeader = (NonResidentAttributeHeader *) FindAttribute(volume, directory, 0xA0);

	if (!indexAllocationHeader) {
		// TODO Support $ATTRIBUTE_LIST.
		BAD_ATTRIBUTE_FAILURE("Could not find index allocation attribute.\n");
	}

	if (indexAllocationHeader->length < sizeof(NonResidentAttributeHeader)) {
		BAD_ATTRIBUTE_FAILURE("Index allocation attribute is too short.\n");
	}

	if (!indexAllocationHeader->nonResident) {
		BAD_ATTRIBUTE_FAILURE("Index allocation attribute is resident.\n");
	}

	if (indexAllocationHeader->flags) {
		BAD_ATTRIBUTE_FAILURE("Index allocation attribute has unsupported flags.\n");
	}

	return indexAllocationHeader;
}

#define ENUMERATE_FAILURE(message) do { KernelLog(LOG_ERROR, "NTFS", "enumerate failure", "Enumerate - " message); return ES_ERROR_UNKNOWN; } while (0)

static EsError EnumerateIndexEntry(IndexEntryHeader *entry, uint8_t *bufferLimit, KNode *directory) {
	if (entry->indexEntryLength < sizeof(IndexEntryHeader)) {
		ENUMERATE_FAILURE("Invalid index entry length.\n");
	}

	if (entry->flags & (1 << 1)) {
		return ES_SUCCESS;
	}

	FileNameAttributeHeader *fileNameAttribute = (FileNameAttributeHeader *) (entry + 1);

	if ((uint8_t *) fileNameAttribute + fileNameAttribute->fileNameLength * 2 > bufferLimit) {
		ENUMERATE_FAILURE("File name attribute has length too large.\n");
	}

	if ((fileNameAttribute->namespaceType == 1 || fileNameAttribute->namespaceType == 3) && entry->fileRecordNumber >= 24) {
		KNodeMetadata metadata = {};

		metadata.type = (fileNameAttribute->flags & 0x10000000) ? ES_NODE_DIRECTORY : ES_NODE_FILE;

		if (metadata.type == ES_NODE_FILE) {
			metadata.totalSize = fileNameAttribute->realSize;
		} else {
			metadata.directoryChildren = ES_DIRECTORY_CHILDREN_UNKNOWN;
		}

		uintptr_t namePosition = 0;
		uint16_t *fileName = (uint16_t *) (fileNameAttribute + 1);
		size_t inputCharactersRemaining = fileNameAttribute->fileNameLength;
		char childName[ES_MAX_DIRECTORY_CHILD_NAME_LENGTH];
		size_t childNameBytes = 0;

		while (inputCharactersRemaining) {
			uint32_t c = *fileName;
			inputCharactersRemaining--;
			fileName++;

			if (c >= 0xD800 && c < 0xDC00 && inputCharactersRemaining) {
				uint32_t c2 = *fileName;

				if (c2 >= 0xDC00 && c2 < 0xE000) {
					inputCharactersRemaining--;
					fileName++;

					c = ((c - 0xD800) << 10) + (c2 - 0xDC00) + 0x10000;
				}
			}

			size_t encodedBytes = utf8_encode(c, nullptr);

			if (namePosition + encodedBytes <= ES_MAX_DIRECTORY_CHILD_NAME_LENGTH) {
				utf8_encode(c, childName + namePosition);
				namePosition += encodedBytes;
				childNameBytes += encodedBytes;
			} else {
				// Name too long.
				// TODO Now what?
			}
		}

		EsError error = FSDirectoryEntryFound(directory, &metadata, 
				&reference, sizeof(DirectoryEntryReference),
				childName, childNameBytes, false);

		if (error != ES_SUCCESS) {
			return error;
		}
	}

	return ES_SUCCESS;
}

static EsError EnumerateIndexNode(Volume *volume, uint8_t *buffer, uint64_t clustersRead, KNode *directory) {
	for (uintptr_t indexBlock = 0; indexBlock < volume->clusterBytes * clustersRead / volume->indexBlockBytes; indexBlock++) {
		uint8_t *blockStart = buffer + indexBlock * volume->indexBlockBytes;

		IndexBlockHeader *header = (IndexBlockHeader *) blockStart;

		if (header->magic != 0x58444E49) {
			ENUMERATE_FAILURE("Index block has incorrect magic number.\n");
		}

		if (!ApplyFixup(volume, blockStart, header->updateSequenceOffset, header->updateSequenceSize, volume->indexBlockBytes)) {
			return ES_ERROR_CORRUPT_DATA;
		}

		if (header->firstEntryOffset > volume->indexBlockBytes || header->firstEntryOffset + header->totalEntrySize > volume->indexBlockBytes) {
			ENUMERATE_FAILURE("Index block has incorrect entries offset/size.\n");
		}

		uint8_t *entriesPosition = blockStart + header->firstEntryOffset + 0x18;
		uint8_t *entriesEnd = entriesPosition + header->totalEntrySize;

		while (entriesPosition + sizeof(IndexEntryHeader) <= entriesEnd) {
			IndexEntryHeader *entry = (IndexEntryHeader *) entriesPosition;
			EsError error = EnumerateIndexEntry(entry, blockStart + header->firstEntryOffset + header->totalEntrySize, directory);
			if (error != ES_SUCCESS) return error; 
			if (entry->flags & (1 << 1)) break;
			entriesPosition += entry->indexEntryLength;
		}
	}

	return ES_SUCCESS; 
}

static EsError Enumerate(KNode *node) {
	FSNode *directory = (FSNode *) node->driverNode;
	Volume *volume = directory->volume;

	size_t bufferSize = volume->indexBlockBytes > volume->clusterBytes ? volume->indexBlockBytes : volume->clusterBytes;
	size_t clustersPerBuffer = bufferSize / volume->clusterBytes;

	uint8_t *buffer = (uint8_t *) EsHeapAllocate(bufferSize, false, K_FIXED);

	if (!buffer) {
		ENUMERATE_FAILURE("Could not allocate buffer.\n");
	}

	EsDefer(EsHeapFree(buffer, bufferSize, K_FIXED));

	// Parse entries in the index root.

	ResidentAttributeHeader *indexRootHeader = (ResidentAttributeHeader *) FindAttribute(volume, directory, 0x90);

	if (!indexRootHeader || indexRootHeader->nonResident || indexRootHeader->attributeLength < sizeof(IndexRootHeader)
			|| indexRootHeader->attributeOffset > MFT_FILE_SIZE - ((uint8_t *) indexRootHeader - directory->fileRecord)
			|| indexRootHeader->attributeOffset + indexRootHeader->attributeLength > MFT_FILE_SIZE - ((uint8_t *) indexRootHeader - directory->fileRecord)) {
		ENUMERATE_FAILURE("Invalid index root attribute.\n");
	}

	{
		IndexRootHeader *header = (IndexRootHeader *) ((uint8_t *) indexRootHeader + indexRootHeader->attributeOffset);

		if (header->firstEntryOffset > indexRootHeader->attributeLength 
				|| header->totalEntrySize > indexRootHeader->attributeLength - header->firstEntryOffset) {
			ENUMERATE_FAILURE("Index root has incorrect entries offset/size.\n");
		}

		uint8_t *entriesPosition = (uint8_t *) header + header->firstEntryOffset + 0x10;
		uint8_t *entriesEnd = entriesPosition + header->totalEntrySize;

		while (entriesPosition + sizeof(IndexEntryHeader) <= entriesEnd) {
			IndexEntryHeader *entry = (IndexEntryHeader *) entriesPosition;
			EnumerateIndexEntry(entry, entriesEnd, node);
			if (entry->flags & (1 << 1)) break;
			entriesPosition += entry->indexEntryLength;
		}
	}

	if (!FindAttribute(volume, directory, 0xA0, nullptr, true)) {
		return true;
	}

	// Get the index allocation attribute.

	NonResidentAttributeHeader *indexAllocationHeader = GetIndexAllocationHeader(volume, directory);

	if (!indexAllocationHeader) {
		return false;
	}

	// For every data run...

	RunHeader *dataRun = (RunHeader *) ((uint8_t *) indexAllocationHeader + indexAllocationHeader->dataRunsOffset);
	uint64_t clusterNumber = 0;
	uintptr_t clustersInBuffer = 0;

	while (((uint8_t *) dataRun - (uint8_t *) indexAllocationHeader) < indexAllocationHeader->length && dataRun->lengthFieldBytes) {
		uint64_t length = GetDataRunLength(dataRun), offset = GetDataRunOffset(dataRun);

		clusterNumber += offset;
		dataRun = (RunHeader *) ((uint8_t *) dataRun + 1 + dataRun->lengthFieldBytes + dataRun->offsetFieldBytes);

		uint64_t positionInRun = 0;

		while (positionInRun != length) {
			size_t clustersToRead = (clustersPerBuffer - clustersInBuffer > length - positionInRun)
				? (length - positionInRun) : (clustersPerBuffer - clustersInBuffer);

			volume->fileSystem->Access((clusterNumber + positionInRun) * volume->clusterBytes, 
					clustersToRead * volume->clusterBytes, K_ACCESS_READ, 
					buffer + clustersInBuffer * volume->clusterBytes, ES_FLAGS_DEFAULT);

			clustersInBuffer += clustersToRead;
			positionInRun += clustersToRead;

			if (clustersInBuffer == clustersPerBuffer) {
				EsError error = EnumerateIndexNode(volume, buffer, clustersPerBuffer, node);
				if (error != ES_SUCCESS) return error;
				clustersInBuffer = 0;
			}
		}
	}

	return ES_SUCCESS;
}

static KNode *Scan(const char *_name, size_t _nameBytes, KNode *_directory) {
#define SCAN_FAILURE(message) do { KernelLog(LOG_ERROR, "NTFS", "scan failure", "Scan - " message); return nullptr; } while (0)

	uint16_t *searchName = (uint16_t *) EsHeapAllocate(256 * sizeof(uint16_t), false, K_FIXED);
	size_t searchNameCharacters = 0;

	if (!searchName) {
		SCAN_FAILURE("Could not allocate buffer for search name.\n");
	}

	EsDefer(EsHeapFree(searchName, 256 * sizeof(uint16_t), K_FIXED));

	const char *_name2 = _name;
	size_t _nameBytes2 = _nameBytes;

	while (_nameBytes) {
		size_t lengthBytes = utf8_length_char(_name);
		if (_nameBytes < lengthBytes) {
			SCAN_FAILURE("Invalid search name.\n");
		}

		uint32_t value = utf8_value(_name);

		if (value <= 0xFFFF) {
			if (searchNameCharacters >= 255) return nullptr;
			searchName[searchNameCharacters++] = value;
		} else {
			if (searchNameCharacters >= 254) return nullptr;
			value -= 0x10000;
			searchName[searchNameCharacters++] = 0xD800 | ((value >> 10) & 0x3FF);
			searchName[searchNameCharacters++] = 0xDC00 | ((value >>  0) & 0x3FF);
		}

		_name += lengthBytes;
		_nameBytes -= lengthBytes;
	}

	FSNode *directory = (FSNode *) _directory->driverNode;
	Volume *volume = directory->volume;

	uint8_t *buffer = (uint8_t *) EsHeapAllocate(volume->indexBlockBytes, false, K_FIXED);

	if (!buffer) {
		SCAN_FAILURE("Could not allocate buffer for index block.\n");
	}

	EsDefer(EsHeapFree(buffer, volume->indexBlockBytes, K_FIXED));

	bool hasIndexAllocation = FindAttribute(volume, directory, 0xA0, nullptr, true) ? true : false;
	NonResidentAttributeHeader *indexAllocationHeader = hasIndexAllocation ? GetIndexAllocationHeader(volume, directory) : nullptr;
	ResidentAttributeHeader *indexRootHeader = (ResidentAttributeHeader *) FindAttribute(volume, directory, 0x90);

	if ((!indexAllocationHeader && hasIndexAllocation) || !indexRootHeader) {
		SCAN_FAILURE("Could not find necessary attributes.\n");
	}

	if (indexRootHeader->nonResident || indexRootHeader->attributeLength < sizeof(IndexRootHeader)
			|| indexRootHeader->attributeOffset > MFT_FILE_SIZE - ((uint8_t *) indexRootHeader - directory->fileRecord)
			|| indexRootHeader->attributeOffset + indexRootHeader->attributeLength > MFT_FILE_SIZE - ((uint8_t *) indexRootHeader - directory->fileRecord)) {
		SCAN_FAILURE("Invalid index root attribute.\n");
	}

	IndexRootHeader *indexRoot = (IndexRootHeader *) ((uint8_t *) indexRootHeader + indexRootHeader->attributeOffset);

	if (indexRoot->firstEntryOffset > indexRootHeader->attributeLength
			|| indexRoot->firstEntryOffset + indexRoot->totalEntrySize > indexRootHeader->attributeLength
			|| indexRoot->totalEntrySize < sizeof(IndexEntryHeader)
			|| indexRoot->attributeType != 0x30 /* filenames */
			|| indexRoot->collationRule != 1 /* Unicode strings */) {
		SCAN_FAILURE("Invalid index root attribute (2).\n");
	}

	uint8_t *entriesPosition = (uint8_t *) indexRoot + indexRoot->firstEntryOffset + 0x10;
	uint8_t *entriesEnd = entriesPosition + indexRoot->totalEntrySize;
	bool hasSubNodes = indexRoot->flags & (1 << 0);
	int depth = 0;

	while (entriesPosition + sizeof(IndexEntryHeader) <= entriesEnd) {
		IndexEntryHeader *entry = (IndexEntryHeader *) entriesPosition;

		if (entry->streamLength > entry->indexEntryLength
				|| entriesPosition + entry->indexEntryLength > entriesEnd) {
			SCAN_FAILURE("Invalid index entry.\n");
		}

		bool descend = false;

		if (entry->flags & (1 << 1)) {
			// Last entry in the node.
			descend = true;
		} else {
			if (entry->streamLength < sizeof(FileNameAttributeHeader)) {
				SCAN_FAILURE("Index entry stream too short.\n");
			}

			FileNameAttributeHeader *fileNameHeader = (FileNameAttributeHeader *) (entry + 1);

			if (fileNameHeader->fileNameLength * 2 > entry->streamLength - sizeof(FileNameAttributeHeader)) {
				SCAN_FAILURE("Index entry stream too short (2).\n");
			}

			if (fileNameHeader->namespaceType == 1 || fileNameHeader->namespaceType == 3) {
				uint16_t *fileName = (uint16_t *) (fileNameHeader + 1);
				size_t fileNameCharacters = fileNameHeader->fileNameLength;

				uintptr_t index = 0;
				bool match = true;

				while (index != fileNameCharacters || index != searchNameCharacters) {
					if (index == fileNameCharacters) {
						match = false;
						break;
					} else if (index == searchNameCharacters) {
						descend = true;
						match = false;
						break;
					}

					uint16_t c1 = ToUpper(volume, fileName[index]), c2 = ToUpper(volume, searchName[index]);

					if (c1 != c2) {
						if (c1 > c2) {
							descend = true;
						}

						match = false;
						break;
					}

					index++;
				}

				if (match) {
					// Found a match; open the node.

					KNode *child = FSNodeAllocate();

					if (!child) {
						SCAN_FAILURE("Could not allocate node.\n");
					}

					child->driverNode = EsHeapAllocate(sizeof(FSNode), true, K_FIXED);

					if (!child->driverNode) {
						EsHeapFree(child, 0, K_FIXED);
						SCAN_FAILURE("Could not allocate node.\n");
					}

					FSNode *node = (FSNode *) child->driverNode;
					node->volume = volume;

					if (!ReadFileRecord(child, node, entry->fileRecordNumber, 
								(fileNameHeader->flags & 0x10000000) ? ES_NODE_DIRECTORY : ES_NODE_FILE, 
								buffer)) {
						EsHeapFree(node, sizeof(FSNode), K_FIXED);
						EsHeapFree(child, 0, K_FIXED);
						return nullptr;
					}

					if (!FSRegisterNewNode(child, _directory, child->data.type, _name2, _nameBytes2)) { // TODO Case-insensitivity support in the VFS.
						EsHeapFree(node, sizeof(FSNode), K_FIXED);
						EsHeapFree(child, 0, K_FIXED);
						SCAN_FAILURE("Could not register root directory node.\n");
					}

					return child;
				}
			}
		}

		if (descend) {
			if (!hasIndexAllocation) {
				SCAN_FAILURE("Cannot descend in index without allocation attribute.\n");
			}

			if (!hasSubNodes || (~entry->flags & (1 << 0))) {
				// Could not find the entry.
				return nullptr; 
			}

			uint64_t nextVCN = *(uint64_t *) (entriesPosition + entry->indexEntryLength - sizeof(uint64_t));

			// Descend to the next index level.

			if (++depth > 16) {
				SCAN_FAILURE("Directory filename index too deep.\n");
			}

			RunHeader *dataRun = (RunHeader *) ((uint8_t *) indexAllocationHeader + indexAllocationHeader->dataRunsOffset);

			// Load the next index block.

			uint64_t clusterNumber = 0;
			uint64_t positionInAllocation = 0;
			bool foundStart = false;

			uintptr_t positionInBuffer = 0;
			size_t clustersToRead = volume->indexBlockBytes / volume->clusterBytes; // TODO Assumes a cluster fits in an index block.

			while (((uint8_t *) dataRun - (uint8_t *) indexAllocationHeader) < indexAllocationHeader->length && dataRun->lengthFieldBytes) {
				uint64_t length = GetDataRunLength(dataRun), offset = GetDataRunOffset(dataRun);

				clusterNumber += offset;

				uintptr_t clusterIndex = clusterNumber;

				if (nextVCN >= positionInAllocation && nextVCN < positionInAllocation + length) {
					foundStart = true;
					clusterIndex += nextVCN - positionInAllocation;
					length -= nextVCN - positionInAllocation;
				}

				if (foundStart) {
					size_t read = length > (clustersToRead - positionInBuffer) ? (clustersToRead - positionInBuffer) : length;

					if (!volume->fileSystem->Access(clusterIndex * volume->clusterBytes, 
								read * volume->clusterBytes, K_ACCESS_READ, 
								buffer + positionInBuffer * volume->clusterBytes, ES_FLAGS_DEFAULT)) {
						SCAN_FAILURE("Could not read the clusters containing VCN to descend to.\n");
					}

					positionInBuffer += read;

					if (positionInBuffer == clustersToRead) {
						break;
					}
				}

				positionInAllocation += length;
				dataRun = (RunHeader *) ((uint8_t *) dataRun + 1 + dataRun->lengthFieldBytes + dataRun->offsetFieldBytes);
			}

			if (positionInBuffer != clustersToRead) {
				SCAN_FAILURE("Could not find all the cluster containing VCN to descend to.\n");
			}

			IndexBlockHeader *header = (IndexBlockHeader *) buffer;

			if (header->magic != 0x58444E49) {
				SCAN_FAILURE("Index block has incorrect magic number.\n");
			}

			if (!ApplyFixup(volume, buffer, header->updateSequenceOffset, header->updateSequenceSize, volume->indexBlockBytes)) {
				return nullptr;
			}

			if (header->firstEntryOffset > volume->indexBlockBytes 
					|| header->totalEntrySize > volume->indexBlockBytes - header->firstEntryOffset) {
				SCAN_FAILURE("Index block has incorrect entries offset/size.\n");
			}

			entriesPosition = (uint8_t *) header + header->firstEntryOffset + 0x18;
			entriesEnd = entriesPosition + header->totalEntrySize;
			hasSubNodes = header->flags & (1 << 0);
		} else {
			entriesPosition += entry->indexEntryLength;
		}
	}

	SCAN_FAILURE("The last entry in an index node did not have the last entry flag set.\n");
}

static size_t Read(KNode *node, void *_buffer, EsFileOffset offset, EsFileOffset count) {
#define READ_FAILURE(message) do { KernelLog(LOG_ERROR, "NTFS", "read failure", "Read - " message); return ES_ERROR_UNKNOWN; } while (0)

	FSNode *file = (FSNode *) node->driverNode;
	Volume *volume = file->volume;

	uint8_t *clusterBuffer = (uint8_t *) EsHeapAllocate(volume->clusterBytes * 2, false, K_FIXED);

	if (!clusterBuffer) {
		READ_FAILURE("Could not allocate cluster buffer.\n");
	}

	EsDefer(EsHeapFree(clusterBuffer, volume->clusterBytes * 2, K_FIXED));

	uint8_t *clusterBuffer2 = clusterBuffer + volume->clusterBytes;
	uint8_t *outputBuffer = (uint8_t *) _buffer;

	// Find the data attribute.

	AttributeHeader *_dataHeader = (AttributeHeader *) FindAttribute(volume, file, 0x80);

	if (!_dataHeader) {
		// TODO Support $ATTRIBUTE_LIST.
		READ_FAILURE("Could not find data attribute.\n");
	}

	if (!_dataHeader->nonResident) {
		// Resident data attribute.

		if (_dataHeader->length < sizeof(ResidentAttributeHeader)) {
			READ_FAILURE("Data attribute is too short.\n");
		}

		ResidentAttributeHeader *residentDataHeader = (ResidentAttributeHeader *) _dataHeader;

		if (residentDataHeader->attributeOffset > MFT_FILE_SIZE - ((uint8_t *) residentDataHeader - file->fileRecord)
				|| residentDataHeader->attributeLength > MFT_FILE_SIZE - ((uint8_t *) residentDataHeader - file->fileRecord) 
					- residentDataHeader->attributeOffset) {
			READ_FAILURE("Data attribute offset/length invalid.\n");
		}

		// Copy data into the buffer.

		uint8_t *residentData = (uint8_t *) residentDataHeader + residentDataHeader->attributeOffset;

		if (offset > residentDataHeader->attributeLength || count > residentDataHeader->attributeLength - offset) {
			READ_FAILURE("Data attribute offset/length invalid (2).\n");
		}

		EsMemoryCopy(outputBuffer, residentData + offset, count);
		return true;
	}

	NonResidentAttributeHeader *dataHeader = (NonResidentAttributeHeader *) _dataHeader;

	if (dataHeader->length < sizeof(NonResidentAttributeHeader)) {
		READ_FAILURE("Data attribute is too short.\n");
	}

	if (dataHeader->flags) {
		READ_FAILURE("Data attribute has unsupported flags.\n");
	}

	// Find the data run containing the requested clusters.

	RunHeader *dataRun = (RunHeader *) ((uint8_t *) dataHeader + dataHeader->dataRunsOffset);
	uint64_t clusterNumber = 0;
	bool foundStart = false;

	uint64_t startCluster = offset / volume->clusterBytes;
	uint64_t endCluster = (offset + count) / volume->clusterBytes;
	uint64_t startOffset = offset - startCluster * volume->clusterBytes;
	uint64_t endOffset = (offset + count) - endCluster * volume->clusterBytes;

	uint64_t clusterPosition = 0;
	uint8_t *buffer = outputBuffer;

	KWorkGroup dispatchGroup = {};
	dispatchGroup.Initialise();

	while (((uint8_t *) dataRun - (uint8_t *) dataHeader) < dataHeader->length && dataRun->lengthFieldBytes) {
		uint64_t length = GetDataRunLength(dataRun), offset = GetDataRunOffset(dataRun);

		clusterNumber += offset;
		dataRun = (RunHeader *) ((uint8_t *) dataRun + 1 + dataRun->lengthFieldBytes + dataRun->offsetFieldBytes);

		uint64_t runOffset = 0;

		if (!foundStart) {
			if (length <= startCluster - clusterPosition) {
				clusterPosition += length;
				continue;
			} else {
				runOffset = startCluster - clusterPosition;
				clusterPosition = startCluster;
				foundStart = true;
			}
		}

		if (!foundStart) {
			continue;
		}

		while (runOffset < length && clusterPosition <= endCluster) {
			uint64_t firstClusterToRead = clusterNumber + runOffset;
			uint64_t clustersToRead = (endCluster - clusterPosition) > (length - runOffset) 
				? (length - runOffset) : (endCluster - clusterPosition);

			if (clusterPosition == startCluster || clusterPosition == endCluster) {
				clustersToRead = 1;
			}

			// Read the clusters.

			if (clusterPosition == startCluster) {
				volume->fileSystem->Access(firstClusterToRead * volume->clusterBytes, 
						clustersToRead * volume->clusterBytes, K_ACCESS_READ, 
						clusterBuffer, ES_FLAGS_DEFAULT, &dispatchGroup);
				buffer += volume->clusterBytes - startOffset;
			} else if (clusterPosition == endCluster) {
				volume->fileSystem->Access(firstClusterToRead * volume->clusterBytes, 
						clustersToRead * volume->clusterBytes, K_ACCESS_READ, 
						clusterBuffer2, ES_FLAGS_DEFAULT, &dispatchGroup);
			} else {
				volume->fileSystem->Access(firstClusterToRead * volume->clusterBytes, 
						clustersToRead * volume->clusterBytes, K_ACCESS_READ, 
						buffer, ES_FLAGS_DEFAULT, &dispatchGroup);
				buffer += clustersToRead * volume->clusterBytes;
			}

			clusterPosition += clustersToRead;
			runOffset += clustersToRead;
		}
	}

	if (clusterPosition <= endCluster) {
		READ_FAILURE("Trying to read past the end of the data runs.\n");
	}

	// Wait for outstanding IO operations to complete.

	bool success = dispatchGroup.Wait();

	if (!success) {
		READ_FAILURE("Could not read clusters from block device.\n");
	}

	// Copy first and last clusters into the buffer.

	if (startCluster == endCluster) {
		EsMemoryCopy(outputBuffer, clusterBuffer + startOffset, count);
	} else {
		EsMemoryCopy(outputBuffer, clusterBuffer + startOffset, volume->clusterBytes - startOffset);
		EsMemoryCopy(outputBuffer + count - endOffset, clusterBuffer2, endOffset);
	}

	return true;
}

static void Close(KNode *node) {
	EsHeapFree(node->driverNode, sizeof(FSNode), K_FIXED);
}

static void DeviceAttach(KDevice *parent) {
	Volume *volume = (Volume *) KDeviceCreate("NTFS", parent, sizeof(Volume), ES_DEVICE_FILE_SYSTEM);

	if (!volume) {
		KernelLog(LOG_ERROR, "NTFS", "allocate error", "EntryNTFS - Could not allocate Volume structure.\n");
		return;
	}

	volume->fileSystem = (KFileSystem *) parent;

	if (!Mount(volume)) {
		KernelLog(LOG_ERROR, "NTFS", "mount failure", "EntryNTFS - Could not mount NTFS volume.\n");

		if (volume->root) EsHeapFree(volume->root->driverNode, 0, K_FIXED);
		EsHeapFree(volume->root, 0, K_FIXED);

		for (uintptr_t i = 0; i < 256; i++) {
			if (volume->upCaseLookup[i]) {
				EsHeapFree(volume->upCaseLookup[i], 512, K_FIXED);
			}
		}

		KDeviceDestroy(volume);
		return;
	}

	{
		ResidentAttributeHeader *volumeNameHeader = (ResidentAttributeHeader *) FindAttribute(volume, &volume->volumeNode, 0x60);

		if (volumeNameHeader && !volumeNameHeader->nonResident 
				&& (volumeNameHeader->attributeOffset + volumeNameHeader->attributeLength 
					+ ((uint8_t *) volumeNameHeader - volume->volumeNode.fileRecord) <= MFT_FILE_SIZE)) {
			volume->fileSystem->nameBytes = volumeNameHeader->attributeLength;
			if (volume->fileSystem->nameBytes > sizeof(volume->fileSystem->name)) volume->fileSystem->nameBytes = sizeof(volume->fileSystem->name);
			EsMemoryCopy(volume->fileSystem->name, (uint8_t *) volumeNameHeader + volumeNameHeader->attributeOffset, volume->fileSystem->nameBytes);
		}
	}

	volume->fileSystem->read = Read;
	volume->fileSystem->scan = Scan;
	volume->fileSystem->load = Load;
	volume->fileSystem->enumerate = Enumerate;
	volume->fileSystem->close = Close;

	EsMemoryCopy(&volume->fileSystem->identifier, &volume->bootSector.serialNumber, sizeof(volume->bootSector.serialNumber));

	KernelLog(LOG_INFO, "NTFS", "register file system", "EntryNTFS - Registering file system with name '%s'.\n", 
			volume->fileSystem->nameBytes, volume->fileSystem->name);
	FSRegisterFileSystem(volume->fileSystem); 
}

KDriver driverNTFS = {
	.attach = DeviceAttach;
};
