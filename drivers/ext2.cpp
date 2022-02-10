// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

// TODO Validation of all fields.
// TODO Contiguous block reading in Scan and Enumerate.
// TODO Make GetDataBlock use (not yet implemented) system block cache.

#include <module.h>

struct SuperBlock {
	uint32_t inodeCount;
	uint32_t blockCount;
	uint32_t reservedBlockCount;
	uint32_t unallocatedBlockCount;
	uint32_t unallocatedInodeCount;

	uint32_t superBlockContainer;
	uint32_t blockSizeExponent;
	uint32_t fragmentSizeExponent;

	uint32_t blocksPerBlockGroup;
	uint32_t fragmentsPerBlockGroup;
	uint32_t inodesPerBlockGroup;

	uint32_t lastMountTime;
	uint32_t lastWriteTime;

	uint16_t mountsSinceLastCheck;
	uint16_t mountsAllowedBetweenChecks;

	uint16_t signature;

	uint16_t state;
	uint16_t errorHandling;

	uint16_t minorVersion;

	uint32_t lastCheckTime;
	uint32_t intervalCheckTime;

	uint32_t creatorID;
	uint32_t majorVersion;

	uint16_t superUserID;
	uint16_t superGroupID;

	uint32_t firstNonReservedInode;
	uint16_t inodeStructureBytes;
	uint16_t blockGroupOfSuperBlock;

	uint32_t optionalFeatures;
	uint32_t requiredFeatures;
	uint32_t writeFeatures;

	uint8_t fileSystemID[16];
	uint8_t volumeName[16];
	uint8_t lastMountPath[64];

	uint32_t compressionAlgorithms;

	uint8_t preallocateFileBlocks;
	uint8_t preallocateDirectoryBlocks;

	uint16_t _unused0;

	uint8_t journalID[16];
	uint32_t journalInode;
	uint32_t journalDevice;

	uint32_t orphanInodeList;
};

struct BlockGroupDescriptor {
	uint32_t blockUsageBitmap;
	uint32_t inodeUsageBitmap;
	uint32_t inodeTable;

	uint16_t unallocatedBlockCount;
	uint16_t unallocatedInodeCount;
	uint16_t directoryCount;

	uint8_t _unused1[14];
};

struct Inode {
#define INODE_TYPE_DIRECTORY (0x4000)
#define INODE_TYPE_REGULAR   (0x8000)
	uint16_t type;

	uint16_t userID;
	uint32_t fileSizeLow;

	uint32_t accessTime;
	uint32_t creationTime;
	uint32_t modificationTime;
	uint32_t deletionTime;

	uint16_t groupID;
	uint16_t hardLinkCount;
	uint32_t usedSectorCount;
	uint32_t flags;
	uint32_t _unused0;

	uint32_t directBlockPointers[12];
	uint32_t indirectBlockPointers[3];

	uint32_t generation;
	uint32_t extendedAttributeBlock;
	uint32_t fileSizeHigh;
	uint32_t fragmentBlock;
	uint8_t osSpecific[12];
};

struct DirectoryEntry {
	uint32_t inode;
	uint16_t entrySize;
	uint8_t nameLengthLow;

	union { 
		uint8_t nameLengthHigh; 

#define DIRENT_TYPE_REGULAR   (1)
#define DIRENT_TYPE_DIRECTORY (2)
		uint8_t type; 
	};

	// Followed by name.
};

struct FSNode {
	struct Volume *volume;
	Inode inode;
};

struct Volume : KFileSystem {
	SuperBlock superBlock;
	BlockGroupDescriptor *blockGroupDescriptorTable;
	size_t blockBytes;
};

static bool Mount(Volume *volume) {
#define MOUNT_FAILURE(message) do { KernelLog(LOG_ERROR, "Ext2", "mount failure", "Mount - " message); return false; } while (0)

	// Load the superblock.

	uint8_t *sectorBuffer = (uint8_t *) EsHeapAllocate(volume->block->information.sectorSize, false, K_FIXED);

	if (!sectorBuffer) {
		MOUNT_FAILURE("Could not allocate buffer.\n");
	}

	EsDefer(EsHeapFree(sectorBuffer, volume->block->information.sectorSize, K_FIXED));

	{
		if (ES_SUCCESS != volume->Access(1024, volume->block->information.sectorSize, K_ACCESS_READ, sectorBuffer, ES_FLAGS_DEFAULT)) {
			MOUNT_FAILURE("Could not read boot sector.\n");
		}

		EsMemoryCopy(&volume->superBlock, sectorBuffer, sizeof(SuperBlock));

		if (volume->superBlock.majorVersion < 1) {
			MOUNT_FAILURE("Volumes below major version 1 not supprted.\n");
		}

		if (volume->superBlock.requiredFeatures != 2) {
			MOUNT_FAILURE("Volume uses unsupported features that are required to read it.\n");
		}

		if (volume->superBlock.inodeStructureBytes < sizeof(Inode)) {
			MOUNT_FAILURE("Inode structure size too small.\n");
		}

		volume->blockBytes = 1024 << volume->superBlock.blockSizeExponent;

		if (volume->blockBytes < volume->block->information.sectorSize) {
			MOUNT_FAILURE("Block size smaller than drive sector size.\n");
		}
	}

	// Load the block group descriptor table.

	{
		uint32_t blockGroupCount = (volume->superBlock.blockCount + volume->superBlock.blocksPerBlockGroup - 1) / volume->superBlock.blocksPerBlockGroup;
		uint32_t firstBlockContainingBlockGroupDescriptorTable = volume->blockBytes == 1024 ? 2 : 1;
		uint32_t blockGroupDescriptorTableLengthInBlocks = (blockGroupCount * sizeof(BlockGroupDescriptor) + volume->blockBytes - 1) / volume->blockBytes;

		volume->blockGroupDescriptorTable = (BlockGroupDescriptor *) EsHeapAllocate(blockGroupDescriptorTableLengthInBlocks * volume->blockBytes, false, K_FIXED);

		if (!volume->blockGroupDescriptorTable) {
			MOUNT_FAILURE("Could not allocate the block group descriptor table.\n");
		}

		if (ES_SUCCESS != volume->Access(firstBlockContainingBlockGroupDescriptorTable * volume->blockBytes, 
				blockGroupDescriptorTableLengthInBlocks * volume->blockBytes, 
				K_ACCESS_READ, volume->blockGroupDescriptorTable, ES_FLAGS_DEFAULT)) {
			MOUNT_FAILURE("Could not read the block group descriptor table from the drive.\n");
		}
	}

	// Load the root directory.

	{
		uint32_t inode = 2;

		uint32_t blockGroup = (inode - 1) / volume->superBlock.inodesPerBlockGroup;
		uint32_t indexInInodeTable = (inode - 1) % volume->superBlock.inodesPerBlockGroup;
		uint32_t sectorInInodeTable = (indexInInodeTable * volume->superBlock.inodeStructureBytes) / volume->block->information.sectorSize;
		uint32_t offsetInSector = (indexInInodeTable * volume->superBlock.inodeStructureBytes) % volume->block->information.sectorSize;

		BlockGroupDescriptor *blockGroupDescriptor = volume->blockGroupDescriptorTable + blockGroup;

		if (ES_SUCCESS != volume->Access(blockGroupDescriptor->inodeTable * volume->blockBytes 
					+ sectorInInodeTable * volume->block->information.sectorSize, 
				volume->block->information.sectorSize, 
				K_ACCESS_READ, sectorBuffer, ES_FLAGS_DEFAULT)) {
			MOUNT_FAILURE("Could not read the inode table.\n");
		}

		volume->rootDirectory->driverNode = EsHeapAllocate(sizeof(FSNode), true, K_FIXED);

		if (!volume->rootDirectory->driverNode) {
			MOUNT_FAILURE("Could not allocate root node.\n");
		}

		FSNode *root = (FSNode *) volume->rootDirectory->driverNode;
		root->volume = volume;
		EsMemoryCopy(&root->inode, sectorBuffer + offsetInSector, sizeof(Inode));

		volume->rootDirectoryInitialChildren = root->inode.fileSizeLow / sizeof(DirectoryEntry); // TODO This is a terrible upper-bound!

		if ((root->inode.type & 0xF000) != INODE_TYPE_DIRECTORY) {
			MOUNT_FAILURE("Root directory is not a directory.\n");
		}
	}

	return true;
}

static uint32_t GetDataBlock(Volume *volume, Inode *node, uint64_t blockIndex, uint8_t *blockBuffer) {
#define CHECK_BLOCK_INDEX() if (offset == 0 || offset / volume->block->information.sectorSize > volume->block->information.sectorCount) { \
		KernelLog(LOG_ERROR, "Ext2", "invalid block index", "GetDataBlock - Block out of bounds.\n"); return 0; }
#define GET_DATA_BLOCK_ACCESS_FAILURE() do { KernelLog(LOG_ERROR, "Ext2", "block access failure", "GetDataBlock - Could not read block.\n"); return 0; } while (0)

	size_t blockPointersPerBlock = volume->blockBytes / 4;
	uint32_t *blockPointers = (uint32_t *) blockBuffer;
	uint64_t offset;

	if (blockIndex < 12) {
		offset = node->directBlockPointers[blockIndex];
		CHECK_BLOCK_INDEX();
		return offset;
	}

	blockIndex -= 12;

	if (blockIndex < blockPointersPerBlock) {
		offset = node->indirectBlockPointers[0] * volume->blockBytes;
		CHECK_BLOCK_INDEX();
		if (ES_SUCCESS != volume->Access(offset, volume->blockBytes, K_ACCESS_READ, blockBuffer, ES_FLAGS_DEFAULT)) GET_DATA_BLOCK_ACCESS_FAILURE();
		offset = blockPointers[blockIndex];
		CHECK_BLOCK_INDEX();
		return offset;
	}

	blockIndex -= blockPointersPerBlock;

	if (blockIndex < blockPointersPerBlock * blockPointersPerBlock) {
		offset = node->indirectBlockPointers[1] * volume->blockBytes;
		CHECK_BLOCK_INDEX();
		if (ES_SUCCESS != volume->Access(offset, volume->blockBytes, K_ACCESS_READ, blockBuffer, ES_FLAGS_DEFAULT)) GET_DATA_BLOCK_ACCESS_FAILURE();
		offset = blockPointers[blockIndex / blockPointersPerBlock];
		CHECK_BLOCK_INDEX();
		if (ES_SUCCESS != volume->Access(offset, volume->blockBytes, K_ACCESS_READ, blockBuffer, ES_FLAGS_DEFAULT)) GET_DATA_BLOCK_ACCESS_FAILURE();
		offset = blockPointers[blockIndex % blockPointersPerBlock];
		CHECK_BLOCK_INDEX();
		return offset;
	}

	blockIndex -= blockPointersPerBlock * blockPointersPerBlock;

	if (blockIndex < blockPointersPerBlock * blockPointersPerBlock * blockPointersPerBlock) {
		offset = node->indirectBlockPointers[2] * volume->blockBytes;
		CHECK_BLOCK_INDEX();
		if (ES_SUCCESS != volume->Access(offset, volume->blockBytes, K_ACCESS_READ, blockBuffer, ES_FLAGS_DEFAULT)) GET_DATA_BLOCK_ACCESS_FAILURE();
		offset = blockPointers[blockIndex / blockPointersPerBlock / blockPointersPerBlock];
		CHECK_BLOCK_INDEX();
		if (ES_SUCCESS != volume->Access(offset, volume->blockBytes, K_ACCESS_READ, blockBuffer, ES_FLAGS_DEFAULT)) GET_DATA_BLOCK_ACCESS_FAILURE();
		offset = blockPointers[(blockIndex / blockPointersPerBlock) % blockPointersPerBlock];
		CHECK_BLOCK_INDEX();
		if (ES_SUCCESS != volume->Access(offset, volume->blockBytes, K_ACCESS_READ, blockBuffer, ES_FLAGS_DEFAULT)) GET_DATA_BLOCK_ACCESS_FAILURE();
		offset = blockPointers[blockIndex % blockPointersPerBlock];
		CHECK_BLOCK_INDEX();
		return offset;
	}

	KernelLog(LOG_ERROR, "Ext2", "invalid index in inode", "GetDataBlock - Index %d out of bounds.\n", blockIndex);
	return 0;
}

static EsError Enumerate(KNode *node) {
#define ENUMERATE_FAILURE(message, error) do { KernelLog(LOG_ERROR, "Ext2", "enumerate failure", "Enumerate - " message); return error; } while (0)

	FSNode *directory = (FSNode *) node->driverNode;
	Volume *volume = directory->volume;

	uint8_t *blockBuffer = (uint8_t *) EsHeapAllocate(volume->blockBytes, false, K_FIXED);

	if (!blockBuffer) {
		ENUMERATE_FAILURE("Could not allocate buffer.\n", ES_ERROR_INSUFFICIENT_RESOURCES);
	}

	EsDefer(EsHeapFree(blockBuffer, volume->blockBytes, K_FIXED));

	uint32_t blocksInDirectory = directory->inode.fileSizeLow / volume->blockBytes;

	for (uintptr_t i = 0; i < blocksInDirectory; i++) {
		uint32_t block = GetDataBlock(volume, &directory->inode, i, blockBuffer);

		if (!block) {
			return ES_ERROR_HARDWARE_FAILURE;
		}

		EsError error = volume->Access((uint64_t) block * volume->blockBytes, volume->blockBytes, K_ACCESS_READ, blockBuffer, ES_FLAGS_DEFAULT);
		if (error != ES_SUCCESS) ENUMERATE_FAILURE("Could not read block.\n", error);

		uintptr_t positionInBlock = 0;

		while (positionInBlock + sizeof(DirectoryEntry) < volume->blockBytes) {
			DirectoryEntry *entry = (DirectoryEntry *) (blockBuffer + positionInBlock);

			if (entry->entrySize > volume->blockBytes - positionInBlock
					|| entry->nameLengthLow > volume->blockBytes - positionInBlock - sizeof(DirectoryEntry)) {
				ENUMERATE_FAILURE("Invalid directory entry size.\n", ES_ERROR_CORRUPT_DATA);
			}

			KNodeMetadata metadata = {};

			const char *name = (const char *) (blockBuffer + positionInBlock + sizeof(DirectoryEntry));
			size_t nameBytes = entry->nameLengthLow;

			metadata.type = entry->type == DIRENT_TYPE_DIRECTORY ? ES_NODE_DIRECTORY : entry->type == DIRENT_TYPE_REGULAR ? ES_NODE_FILE : ES_NODE_INVALID;

			if (metadata.type == ES_NODE_DIRECTORY) {
				metadata.directoryChildren = ES_DIRECTORY_CHILDREN_UNKNOWN;
			}

			if (metadata.type != ES_NODE_INVALID
					&& !(nameBytes == 1 && name[0] == '.')
					&& !(nameBytes == 2 && name[0] == '.' && name[1] == '.')) {
				EsError error = FSDirectoryEntryFound(node, &metadata, &entry->inode, name, nameBytes, false);

				if (error != ES_SUCCESS) {
					return error;
				}
			}

			positionInBlock += entry->entrySize;
		}
	}

	return ES_SUCCESS;
}

static EsError Scan(const char *name, size_t nameBytes, KNode *_directory) {
#define SCAN_FAILURE(message, error) do { KernelLog(LOG_ERROR, "Ext2", "scan failure", "Scan - " message); return error; } while (0)

	if (nameBytes == 2 && name[0] == '.' && name[1] == '.') return ES_ERROR_FILE_DOES_NOT_EXIST;
	if (nameBytes == 1 && name[0] == '.') return ES_ERROR_FILE_DOES_NOT_EXIST;

	FSNode *directory = (FSNode *) _directory->driverNode;
	Volume *volume = directory->volume;
	DirectoryEntry *entry = nullptr;

	uint8_t *blockBuffer = (uint8_t *) EsHeapAllocate(volume->blockBytes, false, K_FIXED);

	if (!blockBuffer) {
		SCAN_FAILURE("Could not allocate buffer.\n", ES_ERROR_INSUFFICIENT_RESOURCES);
	}

	EsDefer(EsHeapFree(blockBuffer, volume->blockBytes, K_FIXED));

	uint32_t blocksInDirectory = directory->inode.fileSizeLow / volume->blockBytes;
	uint32_t inode = 0;

	for (uintptr_t i = 0; i < blocksInDirectory; i++) {
		uint32_t block = GetDataBlock(volume, &directory->inode, i, blockBuffer);

		if (!block) {
			return ES_ERROR_HARDWARE_FAILURE;
		}

		EsError error = volume->Access((uint64_t) block * volume->blockBytes, volume->blockBytes, K_ACCESS_READ, blockBuffer, ES_FLAGS_DEFAULT);
		if (error != ES_SUCCESS) SCAN_FAILURE("Could not read block.\n", error);

		uintptr_t positionInBlock = 0;

		while (positionInBlock + sizeof(DirectoryEntry) < volume->blockBytes) {
			entry = (DirectoryEntry *) (blockBuffer + positionInBlock);

			if (entry->entrySize > volume->blockBytes - positionInBlock
					|| entry->nameLengthLow > volume->blockBytes - positionInBlock - sizeof(DirectoryEntry)) {
				SCAN_FAILURE("Invalid directory entry size.\n", ES_ERROR_CORRUPT_DATA);
			}

			if (entry->nameLengthLow == nameBytes && 0 == EsMemoryCompare(name, blockBuffer + positionInBlock + sizeof(DirectoryEntry), nameBytes)) {
				inode = entry->inode;
				goto foundInode;
			}

			positionInBlock += entry->entrySize;
		}
	}

	return ES_ERROR_FILE_DOES_NOT_EXIST;

	foundInode:;

	if (inode >= volume->superBlock.inodeCount || inode == 0) {
		SCAN_FAILURE("Invalid inode index.\n", ES_ERROR_CORRUPT_DATA);
	}

	KNodeMetadata metadata = {};

	if (entry->type == DIRENT_TYPE_DIRECTORY) {
		metadata.type = ES_NODE_DIRECTORY;
		metadata.directoryChildren = ES_DIRECTORY_CHILDREN_UNKNOWN;
	} else if (entry->type == DIRENT_TYPE_REGULAR) {
		metadata.type = ES_NODE_FILE;
	} else {
		SCAN_FAILURE("Unsupported file type.\n", ES_ERROR_UNSUPPORTED);
	}

	return FSDirectoryEntryFound(_directory, &metadata, &inode, name, nameBytes, false);
}

static EsError Load(KNode *_directory, KNode *node, KNodeMetadata *metadata, const void *entryData) {
	uint32_t inode = *(uint32_t *) entryData;

	FSNode *directory = (FSNode *) _directory->driverNode;
	Volume *volume = directory->volume;

	uint8_t *blockBuffer = (uint8_t *) EsHeapAllocate(volume->blockBytes, false, K_FIXED);

	if (!blockBuffer) {
		return ES_ERROR_INSUFFICIENT_RESOURCES;
	}

	EsDefer(EsHeapFree(blockBuffer, volume->blockBytes, K_FIXED));

	uint32_t blockGroup = (inode - 1) / volume->superBlock.inodesPerBlockGroup;
	uint32_t indexInInodeTable = (inode - 1) % volume->superBlock.inodesPerBlockGroup;
	uint32_t sectorInInodeTable = (indexInInodeTable * volume->superBlock.inodeStructureBytes) / volume->block->information.sectorSize;
	uint32_t offsetInSector = (indexInInodeTable * volume->superBlock.inodeStructureBytes) % volume->block->information.sectorSize;

	BlockGroupDescriptor *blockGroupDescriptor = volume->blockGroupDescriptorTable + blockGroup;

	EsError error = volume->Access(blockGroupDescriptor->inodeTable * volume->blockBytes 
			+ sectorInInodeTable * volume->block->information.sectorSize, 
			volume->block->information.sectorSize, 
			K_ACCESS_READ, blockBuffer, ES_FLAGS_DEFAULT);
	if (error != ES_SUCCESS) return error;

	FSNode *data = (FSNode *) EsHeapAllocate(sizeof(FSNode), true, K_FIXED);

	if (!data) { 
		return ES_ERROR_INSUFFICIENT_RESOURCES;
	}

	data->volume = volume;
	node->driverNode = data;

	EsMemoryCopy(&data->inode, blockBuffer + offsetInSector, sizeof(Inode));

	if ((data->inode.type & 0xF000) == INODE_TYPE_DIRECTORY) {
		if (metadata->type != ES_NODE_DIRECTORY) {
			EsHeapFree(data, sizeof(FSNode), K_FIXED);
			return ES_ERROR_CORRUPT_DATA;
		}

		metadata->directoryChildren = data->inode.fileSizeLow / sizeof(DirectoryEntry); // TODO This is a terrible upper-bound!
	} else if ((data->inode.type & 0xF000) == INODE_TYPE_REGULAR) {
		if (metadata->type != ES_NODE_FILE) {
			EsHeapFree(data, sizeof(FSNode), K_FIXED);
			return ES_ERROR_CORRUPT_DATA;
		}

		metadata->totalSize = (uint64_t) data->inode.fileSizeLow | ((uint64_t) data->inode.fileSizeHigh << 32);
	} else {
		if (metadata->type != ES_NODE_INVALID) {
			EsHeapFree(data, sizeof(FSNode), K_FIXED);
			return ES_ERROR_CORRUPT_DATA;
		}
	}

	return ES_SUCCESS;
}

struct ReadDispatchGroup : KWorkGroup {
	uint64_t extentIndex;
	uint64_t extentCount;
	uint8_t *extentBuffer;
	Volume *volume;

	void QueueExtent() {
		if (!extentCount) return;

		volume->Access(extentIndex * volume->blockBytes, 
				volume->blockBytes * extentCount, K_ACCESS_READ, extentBuffer, ES_FLAGS_DEFAULT, this);
	}

	void QueueBlock(Volume *_volume, uint64_t index, uint8_t *buffer) {
		if (extentIndex + extentCount == index && extentCount
				&& extentBuffer + extentCount * volume->blockBytes == buffer) {
			extentCount++;
		} else {
			QueueExtent();
			extentIndex = index;
			extentCount = 1;
			extentBuffer = buffer;
			volume = _volume;
		}
	}

	bool Read() {
		QueueExtent();
		return Wait();
	}
};

static size_t Read(KNode *node, void *_buffer, EsFileOffset offset, EsFileOffset count) {
#define READ_FAILURE(message, error) do { KernelLog(LOG_ERROR, "Ext2", "read failure", "Read - " message); return error; } while (0)

	FSNode *file = (FSNode *) node->driverNode;
	Volume *volume = file->volume;

	uint8_t *blockBuffer = (uint8_t *) EsHeapAllocate(volume->blockBytes, false, K_FIXED);

	if (!blockBuffer) {
		READ_FAILURE("Could not allocate sector buffer.\n", ES_ERROR_INSUFFICIENT_RESOURCES);
	}

	EsDefer(EsHeapFree(blockBuffer, volume->blockBytes, K_FIXED));

	uint8_t *outputBuffer = (uint8_t *) _buffer;
	uintptr_t outputPosition = 0;

	uint32_t firstBlock = offset / volume->blockBytes,
		 lastBlock = (offset + count) / volume->blockBytes,
		 currentBlock = firstBlock;

	ReadDispatchGroup dispatchGroup = {};
	dispatchGroup.Initialise();

	while (currentBlock <= lastBlock) {
		uint32_t block = GetDataBlock(volume, &file->inode, currentBlock, blockBuffer);

		if (!block) {
			return false;
		}

		uintptr_t readStart = currentBlock == firstBlock ? (offset % volume->blockBytes)           : 0;
		uintptr_t readEnd   = currentBlock == lastBlock  ? ((offset + count) % volume->blockBytes) : volume->blockBytes;

		bool readEntireBlock = readStart == 0 && readEnd == volume->blockBytes;

		if (readEntireBlock) {
			dispatchGroup.QueueBlock(volume, block, outputBuffer + outputPosition);
			outputPosition += volume->blockBytes;
		} else {
			EsError error = volume->Access((uint64_t) block * volume->blockBytes, volume->blockBytes, K_ACCESS_READ, blockBuffer, ES_FLAGS_DEFAULT);
			if (error != ES_SUCCESS) READ_FAILURE("Could not read blocks from drive.\n", error);

			EsMemoryCopy(outputBuffer + outputPosition, blockBuffer + readStart, readEnd - readStart);
			outputPosition += readEnd - readStart;
		}

		currentBlock++;
	}

	bool success = dispatchGroup.Read();

	if (!success) {
		READ_FAILURE("Could not read blocks from drive.\n", ES_ERROR_HARDWARE_FAILURE);
	}

	return count;
}

static void Close(KNode *node) {
	EsHeapFree(node->driverNode, sizeof(FSNode), K_FIXED);
}

static void DeviceAttach(KDevice *parent) {
	Volume *volume = (Volume *) KDeviceCreate("ext2", parent, sizeof(Volume));

	if (!volume || !FSFileSystemInitialise(volume)) {
		KernelLog(LOG_ERROR, "Ext2", "allocate error", "Could not initialise volume.\n");
		return;
	}

	if (volume->block->information.sectorSize & 0x1FF) {
		KernelLog(LOG_ERROR, "Ext2", "incorrect sector size", "Expected sector size to be a multiple of 512, but drive's sectors are %D.\n", 
				volume->block->information.sectorSize);
		KDeviceDestroy(volume);
		return;
	}

	if (!Mount(volume)) {
		KernelLog(LOG_ERROR, "Ext2", "mount failure", "Could not mount Ext2 volume.\n");
		EsHeapFree(volume->rootDirectory->driverNode, 0, K_FIXED);
		EsHeapFree(volume->blockGroupDescriptorTable, 0, K_FIXED);
		KDeviceDestroy(volume);
		return;
	}

	volume->read = Read;
	volume->scan = Scan;
	volume->load = Load;
	volume->enumerate = Enumerate;
	volume->close = Close;

	volume->spaceTotal = volume->superBlock.blockCount * volume->blockBytes;
	volume->spaceUsed = (volume->superBlock.blockCount - volume->superBlock.unallocatedBlockCount) * volume->blockBytes;

	volume->nameBytes = sizeof(volume->superBlock.volumeName);
	if (volume->nameBytes > sizeof(volume->name)) volume->nameBytes = sizeof(volume->name);
	EsMemoryCopy(volume->name, volume->superBlock.volumeName, volume->nameBytes);

	for (uintptr_t i = 0; i < volume->nameBytes; i++) {
		if (!volume->name[i]) {
			volume->nameBytes = i;
		}
	}

	volume->directoryEntryDataBytes = sizeof(uint32_t);
	volume->nodeDataBytes = sizeof(FSNode);

	KernelLog(LOG_INFO, "Ext2", "register file system", "Registering file system with name '%s'.\n", 
			volume->nameBytes, volume->name);
	FSRegisterFileSystem(volume); 
}

KDriver driverExt2 = {
	.attach = DeviceAttach,
};
