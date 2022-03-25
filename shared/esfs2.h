// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

// TODO Kernel driver:
// 		Extent allocation algorithm.
// TODO Design:
// 		Meta/flex block groups.
// 		Journal.
// 		Inline b-tree.
// 		Further data indirection. 
// 		Hash collisions. (Probably just remove index and enumerate directory contents instead?)

#ifndef KERNEL

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif

#ifndef OS_ESSENCE
typedef struct EsUniqueIdentifier {
	uint8_t d[16];
} EsUniqueIdentifier;
#endif

#endif

#define ESFS_BOOT_SUPER_BLOCK_SIZE 			(8192)			// The bootloader and superblock take up 16KB.
#define ESFS_DRIVE_MINIMUM_SIZE 			(1048576)		// The minimum drive size that can be formatted.
#define ESFS_DRIVER_VERSION 				(10)			// The current driver version.
#define ESFS_MAXIMUM_VOLUME_NAME_LENGTH 		(32)			// The volume name limit.

#define ESFS_CORE_NODE_KERNEL				(0)			// The kernel core node.
#define ESFS_CORE_NODE_ROOT				(1)			// The root directory core node.
#define ESFS_CORE_NODE_COUNT				(2)			// The number of core nodes.

#define ESFS_SIGNATURE_STRING        			("!EssenceFS2-----")	// The signature in the superblock.
#define ESFS_DIRECTORY_ENTRY_SIGNATURE 			("DirEntry")		// The signature in directory entries.
#define ESFS_GROUP_DESCRIPTOR_SIGNATURE			("GDTE")		// The signature in a group descriptor.
#define ESFS_INDEX_VERTEX_SIGNATURE			("INXE")		// The signature in a index vertex.

#define ESFS_NODE_TYPE_FILE 				(1)			// DirectoryEntry.nodeType: a file.
#define ESFS_NODE_TYPE_DIRECTORY 			(2)			// DirectoryEntry.nodeType: a directory.

#define ESFS_ATTRIBUTE_DATA				(1)			// Contains the data of the file, or a list of DirectoryEntries.
#define ESFS_ATTRIBUTE_FILENAME				(2)			// The UTF-8 filename.
#define ESFS_ATTRIBUTE_DIRECTORY			(3)			// Additional information about the directory.

#define ESFS_INDIRECTION_DIRECT				(1)			// The data is stored in the attribute.
#define ESFS_INDIRECTION_L1				(2)			// The attribute contains a extent list that points to the data.

#define ESFS_INDEX_MAX_DEPTH				(16)			// The maximum depth of the index tree. I'd be surprised if this gets past 8.
#define ESFS_VERTEX_KEY(vertex, key) 			((IndexKey *) ((uint8_t *) vertex + vertex->offset) + key)

typedef struct Attribute {
	/*  0 */ uint16_t type;						// Attribute type.
	/*  2 */ uint16_t size;						// The size in bytes. Must be 8 byte aligned.
} Attribute;

typedef struct AttributeFilename {
	/*  0 */ uint16_t type;						// ESFS_ATTRIBUTE_FILENAME.
	/*  2 */ uint16_t size;						// The size in bytes. Must be 8 byte aligned.
	/*  4 */ uint16_t length;					// The length of the filename in bytes.
	/*  6 */ uint16_t _unused;					// Unused.

#define ESFS_FILENAME_HEADER_SIZE (8)						// The size of the header of a AttributeFilename.
	/*  8 */ uint8_t filename[1];					// The UTF-8 filename.
} AttributeFilename;

typedef struct AttributeDirectory {
	/*  0 */ uint16_t type;						// ESFS_ATTRIBUTE_DIRECTORY.
	/*  2 */ uint16_t size;						// The size in bytes. Must be 8 byte aligned.
	/*  4 */ uint8_t _unused0[4];
	/*  8 */ uint64_t childNodes;					// The number of child nodes in the directory.
	/* 16 */ uint64_t indexRootBlock;				// The block containing the root IndexVertex for the directory.
	/* 24 */ uint64_t totalSize;					// The sum of sizes of all the directory's children in bytes.
} AttributeDirectory;

typedef struct AttributeData {
	/*  0 */ uint16_t type;						// ESFS_ATTRIBUTE_DATA.
	/*  2 */ uint16_t size;						// The size in bytes. Must be 8 byte aligned.
	/*  4 */ uint8_t indirection;					// The indirection used to access the data.
	/*  5 */ uint8_t dataOffset;					// The offset into the attribute where the data or extent list can be found.
	/*  6 */ uint16_t count;					// The number of data bytes in the attribute, or extents in the list.
	/*  8 */ uint64_t _unused[3];					// Unused.
#define ESFS_DATA_OFFSET (32)
	/* 32 */ uint8_t data[1];					// The data or extent list.

	// Format of each extent in the extent list:
	//       uint8_t offsetSize : 3, countSize : 3, unused : 2;	// The size of the offset and count fields in bytes - 1.
	//       uint8_t offset[offsetSize + 1];			// The first block in the extent, expressed as a signed offset from the start 
	//       							       of the previous extent in the list, or from 0 for the first extent. Big endian.
	//       uint8_t count[countSize + 1];				// The number of blocks encompassed by the extent. Big endian.
} AttributeData;

typedef struct DirectoryEntry {
	/*  0 */ char signature[8];					// Must be ESFS_DIRECTORY_ENTRY_SIGNATURE.
	/*  8 */ EsUniqueIdentifier identifier;				// Identifier of the node.
	/* 24 */ uint32_t checksum;					// CRC-32 checksum of DirectoryEntry.
	/* 28 */ uint16_t attributeOffset;				// Offset to the first attribute.
	/* 30 */ uint8_t nodeType;					// Node type.
	/* 31 */ uint8_t attributeCount;				// The number of attributes in the list.
	/* 32 */ uint64_t creationTime, accessTime, modificationTime;	// Timekeeping. In microseconds since 1st January 1970.
	/* 56 */ uint64_t fileSize;					// The amount of data referenced by the data attribute in bytes.
	/* 64 */ EsUniqueIdentifier parent;				// Identifier of the parent directory.
	/* 80 */ EsUniqueIdentifier contentType;                        // Identifier of the file content type.

#define ESFS_ATTRIBUTE_OFFSET (96)
	/* 96 */ uint8_t attributes[1024 - ESFS_ATTRIBUTE_OFFSET];	// Attribute list.
} DirectoryEntry;

typedef struct GroupDescriptor {
	/*  0 */ char signature[4];					// Must be ESFS_GROUP_DESCRIPTOR_SIGNATURE.
	/*  4 */ uint32_t blocksUsed;					// The number of used blocks in the group.
	/*  8 */ uint64_t blockBitmap;					// The bitmap indicating which blocks in the group are used.
	/* 16 */ uint32_t bitmapChecksum;				// CRC-32 checksum of the bitmap.
	/* 20 */ uint32_t checksum;					// CRC-32 checksum of this descriptor.
	/* 24 */ uint32_t largestExtent;				// The largest number of contiguous blocks.
	/* 28 */ uint32_t _unused[7];					// Unused.
} GroupDescriptor;

typedef struct DirectoryEntryReference {
	/*  0 */ uint64_t block;					// The block containing the directory entry.
	/*  8 */ uint32_t offsetIntoBlock;				// Offset into the block to find the directory entry.
	/* 12 */ uint32_t _unused;					// Unused.
} DirectoryEntryReference;

typedef struct IndexKey {
	/*  0 */ uint64_t value;					// The CRC-64 hashed node path. Ignored for the +1 vertex (assumed to be maximum possible).
	/*  8 */ uint64_t child;					// The block containing the child IndexVertex. Set to 0 for a leaf.
									// All keys in the child should be less than this key.
									// This is the only valid field in the +1 key.
	/* 16 */ DirectoryEntryReference data;				// The directory entry this key refers to.
} IndexKey;

typedef struct IndexVertex {
	/*  0 */ char signature[4];					// Must be ESFS_INDEX_VERTEX_SIGNATURE.
	/*  4 */ uint32_t checksum;					// CRC-32 checksum of IndexVertex.
	/*  8 */ uint16_t offset;					// Offset to the first IndexKey.
	/* 10 */ uint16_t count;					// The number of IndexKeys, (superblock.blockSize - this.offset) / sizeof(IndexKey).
	/* 12 */ uint16_t maxCount;					// The maximum number of IndexKeys that can fit in the vertex.
	/* 14 */ uint16_t _unused0;					// Unused.
	/* 16 */ uint64_t _unused1[2];					// Unused.

#define ESFS_INDEX_KEY_OFFSET (32)
	/* 32 */ IndexKey keys[1];					// There are this.count keys.
} IndexVertex;

typedef struct Superblock {
	/*   0 */ char signature[16];					// The filesystem signature; should be ESFS_SIGNATURE_STRING.
	/*  16 */ char volumeName[ESFS_MAXIMUM_VOLUME_NAME_LENGTH];	// The name of the volume.
	
	/*  48 */ uint16_t requiredReadVersion;				// If this is greater than the driver's version, then the filesystem cannot be read.
	/*  50 */ uint16_t requiredWriteVersion;			// If this is greater than the driver's version, then the filesystem cannot be written.
	
	/*  52 */ uint32_t checksum;					// CRC-32 checksum of Superblock.
	/*  56 */ uint8_t mounted;					// Non-zero to indicate that the volume is mounted, or was not properly unmounted.
	/*  57 */ uint8_t _unused2[7];
	
	/*  64 */ uint64_t blockSize;					// The size of a block on the volume.
	/*  72 */ uint64_t blockCount;					// The number of blocks on the volume.
	/*  80 */ uint64_t blocksUsed;					// The number of blocks that are in use.
	
	/*  88 */ uint32_t blocksPerGroup;				// The number of blocks in a group.
	/*  92 */ uint8_t _unused3[4];
	/*  96 */ uint64_t groupCount;					// The number of groups on the volume.
	/* 104 */ uint64_t blocksPerGroupBlockBitmap;			// The number of blocks used to a store a group's block bitmap.
	/* 112 */ uint64_t gdtFirstBlock;				// The first block in the group descriptor table.
	/* 120 */ uint64_t directoryEntriesPerBlock;			// The number of directory entries in a block.
	/* 128 */ uint64_t _unused0;					// Unused.
	
	/* 136 */ EsUniqueIdentifier identifier;			// The unique identifier for the volume.
	/* 152 */ EsUniqueIdentifier osInstallation;			// The unique identifier of the Essence installation this volume was made for. All zero for a non-installation volume.
	/* 168 */ EsUniqueIdentifier nextIdentifier;			// The identifier to give to the next created file.

	/* 184 */ DirectoryEntryReference kernel;			// The kernel. For convenient access by the bootloader.
	/* 200 */ DirectoryEntryReference root;				// The root directory.

	/* 216 */ uint8_t _unused1[8192 - 216];				// Unused.
} Superblock;

uint64_t EncodeExtent(uint64_t extentStart, uint64_t previousExtentStart, uint64_t extentCount, uint8_t *encode) {
	int64_t relativeStart = (int64_t) (extentStart - previousExtentStart);
	uint64_t absoluteRelativeStart = (uint64_t) (relativeStart < 0 ? -relativeStart : relativeStart);

	uint8_t startBytes = 
		absoluteRelativeStart < 0x80 			? 1
		: absoluteRelativeStart < 0x8000 		? 2
		: absoluteRelativeStart < 0x800000		? 3
		: absoluteRelativeStart < 0x80000000		? 4
		: absoluteRelativeStart < 0x8000000000		? 5
		: absoluteRelativeStart < 0x800000000000	? 6
		: absoluteRelativeStart < 0x80000000000000	? 7
		: 8;
	uint8_t countBytes = 
		extentCount < 0x80 			? 1
		: extentCount < 0x8000 			? 2
		: extentCount < 0x800000		? 3
		: extentCount < 0x80000000		? 4
		: extentCount < 0x8000000000		? 5
		: extentCount < 0x800000000000		? 6
		: extentCount < 0x80000000000000	? 7
		: 8;

	uint64_t position = 0;
	encode[position++] = (startBytes - 1) + ((countBytes - 1) << 3);

	for (int i = 0; i < startBytes; i++, position++) encode[position] = (uint8_t) (relativeStart >> ((startBytes - 1 - i) * 8));
	for (int i = 0; i < countBytes; i++, position++) encode[position] = (uint8_t) (extentCount   >> ((countBytes - 1 - i) * 8));

#if 0
	Log("encode: %d/%d --> ", startBytes, countBytes);

	for (unsigned i = 0; i < position; i++) {
	     Log("%.2X ", (uint32_t) encode[i]);
	}

	Log("\n");
#endif

	return position;
}

bool DecodeExtent(uint64_t *previousExtentStart, uint64_t *extentCount, uint8_t *extents, uint64_t *position, uint64_t end) {
	uint64_t extentStart = 0;
	*extentCount = 0;
	
	if (*position == end) return false;
	uint8_t header = extents[*position];
	*position = *position + 1;

	uint8_t startBytes = ((header >> 0) & 7) + 1;
	uint8_t countBytes = ((header >> 3) & 7) + 1;

	bool negative = false;

	for (uint8_t i = 0; i < startBytes; i++) {
		if (*position == end) return false;
		uint8_t byte = extents[*position];
		if (!i) negative = byte & 0x80;
		extentStart <<= 8;
		extentStart += byte;
		*position = *position + 1;
	}

	for (uint8_t i = 0; i < countBytes; i++) {
		if (*position == end) return false;
		*extentCount = *extentCount << 8;
		*extentCount = *extentCount + extents[*position];
		*position = *position + 1;
	}

	if (negative) {
		for (uint64_t i = startBytes; i < sizeof(uint64_t) / sizeof(uint8_t); i++) {
			extentStart |= (uint64_t) 0xFF << (i * 8);
		}
	}

	*previousExtentStart = *previousExtentStart + extentStart;
	return true;
}

#ifndef KERNEL

uint64_t blockSize;
Superblock superblock;
GroupDescriptor *groupDescriptorTable;
uint64_t copiedCount;

bool ReadBlock(uint64_t block, uint64_t count, void *buffer);
bool WriteBlock(uint64_t block, uint64_t count, void *buffer);
bool WriteBytes(uint64_t offset, uint64_t count, void *buffer);

bool ReadDirectoryEntryReference(DirectoryEntryReference reference, DirectoryEntry *entry) {
	uint8_t buffer[superblock.blockSize];

	if (!ReadBlock(reference.block, 1, buffer)) {
		return false;
	}

	memcpy(entry, buffer + reference.offsetIntoBlock, sizeof(DirectoryEntry));
	return true;
}

bool WriteDirectoryEntryReference(DirectoryEntryReference reference, DirectoryEntry *entry) {
	entry->checksum = 0;
	entry->checksum = CalculateCRC32(entry, sizeof(DirectoryEntry), 0);
	uint8_t buffer[superblock.blockSize];

	if (ReadBlock(reference.block, 1, buffer)) {
		memcpy(buffer + reference.offsetIntoBlock, entry, sizeof(DirectoryEntry));
		return WriteBlock(reference.block, 1, buffer);
	} else {
		return false;
	}
}

Attribute *FindAttribute(DirectoryEntry *entry, uint16_t type) {
	Attribute *attribute = (Attribute *) ((uint8_t *) entry + entry->attributeOffset);
	int count = 0;

	while (attribute->type != type) {
		attribute = (Attribute *) ((uint8_t *) attribute + attribute->size);

		if (count++ == entry->attributeCount) {
			Log("Could not find attribute %d.\n", type);
			EsFSError();
		}
	}

	return attribute;
}

void GenerateUniqueIdentifier(EsUniqueIdentifier *u, bool random) {
	if (random) {
		for (int i = 0; i < 16; i++) {
			u->d[i] = rand();
		}
	} else {
		*u = superblock.nextIdentifier;

		for (int i = 0; i < 16; i++) {
			superblock.nextIdentifier.d[i]++;
			if (superblock.nextIdentifier.d[i]) break;
		}
	}
}

IndexKey *InsertKeyIntoVertex(uint64_t newKey, IndexVertex *vertex) {
	// Find where in this vertex we should insert the key.

	int position;

	for (position = 0; position < vertex->count; position++) {
		if (newKey < ESFS_VERTEX_KEY(vertex, position)->value) {
			break;
		}
	}

	// Insert the key.

	// Log("%d//%d\n", vertex->count, vertex->maxCount);
	assert(vertex->count != vertex->maxCount);
	IndexKey *insertionPosition = ESFS_VERTEX_KEY(vertex, position);
	memmove(insertionPosition + 1, insertionPosition, 
			(vertex->count + 1 - position) * sizeof(IndexKey));
	vertex->count++;
	insertionPosition->value = newKey;

	// Update the checksum.

	vertex->checksum = 0;
	vertex->checksum = CalculateCRC32(vertex, superblock.blockSize, 0);

	return insertionPosition;
}

bool AllocateExtent(uint64_t increaseBlocks, uint64_t *extentStart, uint64_t *extentCount) {
	// Log("used %ld/%ld, need %ld more\n", superblock.blocksUsed, superblock.blockCount, increaseBlocks);

	// Find a group to allocate the next extent from.

	GroupDescriptor *target = NULL;

	{
		for (uint64_t i = 0; !target && i < superblock.groupCount; i++) {
			GroupDescriptor *group = groupDescriptorTable + i;
			if (!group->blocksUsed) group->largestExtent = superblock.blocksPerGroup - superblock.blocksPerGroupBlockBitmap;
			if (group->largestExtent >= increaseBlocks) target = group;
		}

		for (uint64_t i = 0; !target && i < superblock.groupCount; i++) {
			GroupDescriptor *group = groupDescriptorTable + i;
			if (superblock.blocksPerGroup - group->blocksUsed >= increaseBlocks) target = group;
		}

		for (uint64_t i = 0; !target && i < superblock.groupCount; i++) {
			GroupDescriptor *group = groupDescriptorTable + i;
			if (superblock.blocksPerGroup != group->blocksUsed) target = group;
		}
	}

	if (!target) {
		Log("Out of space.\n");
		EsFSError();
	}

	// Load the bitmap, find the largest extent, and mark it as in use.

	uint8_t bitmap[superblock.blocksPerGroupBlockBitmap * superblock.blockSize];

	{
		if (target->blockBitmap) {
			if (!ReadBlock(target->blockBitmap, superblock.blocksPerGroupBlockBitmap, bitmap)) {
				return false;
			}
		} else {
			memset(bitmap, 0, superblock.blocksPerGroupBlockBitmap * superblock.blockSize);
			for (uint64_t i = 0; i < superblock.blocksPerGroupBlockBitmap; i++) bitmap[i / 8] |= 1 << (i % 8);
			target->blockBitmap = superblock.blocksPerGroup * (target - groupDescriptorTable);
			target->blocksUsed = superblock.blocksPerGroupBlockBitmap;
		}

		uint64_t largestExtentStart = 0, largestExtentCount = 0, i = 0;

		while (i < superblock.blocksPerGroup) {
			if (bitmap[i / 8] & (1 << (i % 8))) {
				i++;
			} else {
				uint64_t start = i, count = 0;

				while (i < superblock.blocksPerGroup) {
					if (bitmap[i / 8] & (1 << (i % 8))) break;
					else count++, i++;
				}

				if (largestExtentCount < count) {
					largestExtentStart = start;
					largestExtentCount = count;
				}
			}
		}

		assert(largestExtentCount == target->largestExtent);

		*extentStart = largestExtentStart;
		*extentCount = largestExtentCount;

		if (*extentCount > increaseBlocks) {
			*extentCount = increaseBlocks;
		}

		for (uint64_t i = *extentStart; i < *extentStart + *extentCount; i++) {
			bitmap[i / 8] |= 1 << (i % 8);
		}

		{
			uint64_t largestExtentCount = 0, i = 0;

			while (i < superblock.blocksPerGroup) {
				if (bitmap[i / 8] & (1 << (i % 8))) {
					i++;
				} else {
					uint64_t count = 0;

					while (i < superblock.blocksPerGroup) {
						if (bitmap[i / 8] & (1 << (i % 8))) break;
						else count++, i++;
					}

					if (largestExtentCount < count) {
						largestExtentCount = count;
					}
				}
			}

			target->blocksUsed += *extentCount;
			target->largestExtent = largestExtentCount;

			assert(superblock.blocksPerGroup == target->blocksUsed || target->largestExtent);
		}

		target->bitmapChecksum = CalculateCRC32(bitmap, sizeof(bitmap), 0);
		target->checksum = 0;
		target->checksum = CalculateCRC32(target, sizeof(GroupDescriptor), 0);

		if (!WriteBlock(target->blockBitmap, superblock.blocksPerGroupBlockBitmap, bitmap)) {
			return false;
		}
	}

	*extentStart = *extentStart + (target - groupDescriptorTable) * superblock.blocksPerGroup;
	superblock.blocksUsed += *extentCount;
	// Log("allocate extent: %ld -> %ld (for %ld)\n", extentStart, extentStart + extentCount, increaseBlocks);
	return true;
}

bool AccessNode(DirectoryEntry *node, void *buffer, uint64_t offsetIntoFile, uint64_t totalCount, DirectoryEntryReference *reference, bool read) {
	if (!totalCount) return true;
	AttributeData *dataAttribute = (AttributeData *) FindAttribute(node, ESFS_ATTRIBUTE_DATA);

	if (dataAttribute->indirection == ESFS_INDIRECTION_DIRECT && read) {
		memcpy(buffer, dataAttribute->data + offsetIntoFile, totalCount);
		return true;
	} else if (dataAttribute->indirection == ESFS_INDIRECTION_DIRECT && !read) {
		assert(!reference);
		memcpy(dataAttribute->data + offsetIntoFile, buffer, totalCount);
		return true;
	}

	assert(dataAttribute->indirection == ESFS_INDIRECTION_L1);

	int decoded = -1;

	// Log("\twrite %ld bytes at %ld\n", totalCount, offsetIntoFile);

	next:;
	uint64_t block = offsetIntoFile / superblock.blockSize;
	uint64_t offset = offsetIntoFile % superblock.blockSize;
	uint64_t count = superblock.blockSize - offset;
	if (totalCount < count) count = totalCount;

	// Log("block: %ld, offset: %ld, count: %ld\n", block, offset, count);

	// Find the extent.

	uint8_t *extents = ((uint8_t *) dataAttribute + dataAttribute->dataOffset);

	{
		uint64_t position = 0, blockInFile = 0, extentStart = 0;
		assert(dataAttribute->count || !node->fileSize);

		bool found = false;

		for (uint64_t i = 0; i < dataAttribute->count; i++) {
			uint64_t extentCount = 0;
			DecodeExtent(&extentStart, &extentCount, extents, &position, dataAttribute->size - dataAttribute->dataOffset);
			if (decoded < (int) i) {
				decoded = i;
			}

			if (blockInFile + extentCount > block) {
				uint64_t offsetIntoExtent = block - blockInFile;
				block = extentStart + offsetIntoExtent;
				found = true;
				break;
			}

			blockInFile += extentCount;
		}

		assert(found);
	}

	uint8_t blockBuffer[superblock.blockSize];

	if (read || count != superblock.blockSize) {
		if (!ReadBlock(block, 1, blockBuffer)) {
			return false;
		}
	}

	if (read) {
		memcpy(buffer, blockBuffer + offset, count);
	} else {
		memcpy(blockBuffer + offset, buffer, count);

		if (!WriteBlock(block, 1, blockBuffer)) {
			return false;
		}
	}

	if (reference) {
		reference->block = block;
		reference->offsetIntoBlock = offset;
	}

	totalCount -= count;

	if (totalCount) {
		offsetIntoFile += count;
		buffer = (uint8_t *) buffer + count;
		goto next;
	}

	return true;
}

bool ResizeNode(DirectoryEntry *entry, uint64_t newSize) {
	assert(newSize >= entry->fileSize);

	AttributeData *dataAttribute = (AttributeData *) FindAttribute(entry, ESFS_ATTRIBUTE_DATA);

	if (newSize < (uint64_t) (dataAttribute->size - dataAttribute->dataOffset) && entry->nodeType == ESFS_NODE_TYPE_FILE) {
		dataAttribute->indirection = ESFS_INDIRECTION_DIRECT;
		dataAttribute->count = entry->fileSize = newSize;
		return true;
	}

	// Log("\tresize to %lu\n", newSize);

	dataAttribute->indirection = ESFS_INDIRECTION_L1;

	uint64_t oldSize = entry->fileSize;
	uint64_t oldBlocks = (oldSize + superblock.blockSize - 1) / superblock.blockSize;
	uint64_t newBlocks = (newSize + superblock.blockSize - 1) / superblock.blockSize;

	entry->fileSize = newSize;

	if (oldBlocks == newBlocks) {
		// Do nothing.
	} else if (oldBlocks < newBlocks) {
		uint64_t increaseBlocks = newBlocks - oldBlocks;
		uint64_t previousExtentStart = 0, previousExtentCount = 0, previousExtentStart2 = 0;

		uint8_t *extents = ((uint8_t *) dataAttribute + dataAttribute->dataOffset);
		uint64_t offsetIntoExtentList = 0;
		uint64_t previousOffsetIntoExtentList = 0;

		for (uint64_t i = 0; i < dataAttribute->count; i++) {
			previousOffsetIntoExtentList = offsetIntoExtentList;
			previousExtentStart2 = previousExtentStart;
			DecodeExtent(&previousExtentStart, &previousExtentCount, extents, &offsetIntoExtentList, dataAttribute->size - dataAttribute->dataOffset);
			// Log("%ld/%ld/%ld\n", previousExtentStart, extentCount, offsetIntoExtentList);
			assert(previousExtentStart < superblock.blockCount);
		}

		while (increaseBlocks) {
			uint64_t extentStart, extentCount, encodedLength;
			uint8_t encode[32];

			if (!AllocateExtent(increaseBlocks, &extentStart, &extentCount)) {
				return false;
			}

			if (extentStart == previousExtentStart + previousExtentCount) {
				dataAttribute->count--;
				offsetIntoExtentList = previousOffsetIntoExtentList;
				encodedLength = EncodeExtent(previousExtentStart, previousExtentStart2, extentCount + previousExtentCount, encode);
			} else {
				encodedLength = EncodeExtent(extentStart, previousExtentStart, extentCount, encode);
			}

			// Log("\t@%ld alloc %ld, %ld\n", offsetIntoExtentList, extentStart, extentCount);

			// Log("%ld vs %ld\n", offsetIntoExtentList + encodedLength, (uint64_t) (dataAttribute->size - dataAttribute->dataOffset));

			if (offsetIntoExtentList + encodedLength > (uint64_t) (dataAttribute->size - dataAttribute->dataOffset)) {
				Log("Unimplemented - indirection past L1.\n");
				EsFSError();
			}

			memcpy(extents + offsetIntoExtentList, encode, encodedLength);
			offsetIntoExtentList += encodedLength;
			increaseBlocks -= extentCount;
			dataAttribute->count++;
			previousExtentStart = extentStart;
		}
	} else {
		Log("Unimplemented - node truncation.\n");
		EsFSError();
	}

	return true;
}

#if 0
void PrintTree(uint64_t block, int indent = 2, uint64_t lowerThan = -1) {
	if (!block) return;

	uint8_t buffer[superblock.blockSize];
	ReadBlock(block, 1, buffer);
	IndexVertex *node = (IndexVertex *) buffer;
	const char *spaces = "|   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   ";

#ifndef VALIDATE_TREE_ONLY
	Log("%.*snode with %d+1 keys [%p]\n", indent, spaces, node->count, node);
#endif

	for (uint64_t i = 0; i <= node->count; i++) {
		if (i && i != node->count && node->keys[i].value <= node->keys[i - 1].value) {
			Log("%.*s %lu VIOLATION [1]\n", indent, spaces, i);
			assert(false);
		}

		uint64_t next = lowerThan;

		if (i == node->count) {
#ifndef VALIDATE_TREE_ONLY
			Log("%.*s %lu last key\n", indent, spaces, i);
#endif
		} else {
			if (node->keys[i].value >= lowerThan) {
				Log("%.*s %lu VIOLATION [2]\n", indent, spaces, i);
				assert(false);
			}

#ifndef VALIDATE_TREE_ONLY
			Log("%.*s %lu key = %lu\n", indent, spaces, i, node->keys[i].value);
#endif
			next = node->keys[i].value;
		}

		PrintTree(node->keys[i].child, indent + 4, next);
	}
}
#endif

void NewDirectoryEntry(DirectoryEntry *entry, uint8_t nodeType, EsUniqueIdentifier parentUID, const char *name, EsUniqueIdentifier contentType) {
	memcpy(entry->signature, ESFS_DIRECTORY_ENTRY_SIGNATURE, 8);
	GenerateUniqueIdentifier(&entry->identifier, false);
	entry->attributeOffset = ESFS_ATTRIBUTE_OFFSET;
	entry->nodeType = nodeType; 
	entry->parent = parentUID;
	entry->contentType = contentType;

	uint8_t *position = (uint8_t *) entry + entry->attributeOffset;
	size_t newFilenameSize = ((strlen(name) + ESFS_FILENAME_HEADER_SIZE - 1) & ~7) + 8; // Size of name + size of header, rounded up to the nearest 8 bytes.

	if (nodeType == ESFS_NODE_TYPE_DIRECTORY) {
		AttributeDirectory *directory = (AttributeDirectory *) position;
		directory->type = ESFS_ATTRIBUTE_DIRECTORY;
		directory->size = sizeof(AttributeDirectory);
		directory->indexRootBlock = 0;
		directory->totalSize = 0;
		entry->attributeCount++;
		position += directory->size;
	}

	AttributeData *data = (AttributeData *) position;
	data->type = ESFS_ATTRIBUTE_DATA;
	data->size = sizeof(DirectoryEntry) - newFilenameSize - (position - (uint8_t *) entry);
	data->indirection = nodeType == ESFS_NODE_TYPE_DIRECTORY ? ESFS_INDIRECTION_L1 : ESFS_INDIRECTION_DIRECT;
	data->dataOffset = ESFS_DATA_OFFSET;
	entry->attributeCount++;
	position += data->size;

	AttributeFilename *filename = (AttributeFilename *) position;
	filename->type = ESFS_ATTRIBUTE_FILENAME;
	filename->size = newFilenameSize;
	filename->length = strlen(name);
	memcpy(filename->filename, name, filename->length);
	entry->attributeCount++;
	position += filename->size;

	assert(position == (uint8_t *) (entry + 1));
	entry->checksum = 0;
	entry->checksum = CalculateCRC32(entry, sizeof(DirectoryEntry), 0);
}

bool AddNode(const char *name, uint8_t nodeType, DirectoryEntry *outputEntry, DirectoryEntryReference *outputReference, 
		DirectoryEntryReference directoryReference, EsUniqueIdentifier contentType) {
	// Log("add %s to %s\n", name, path);

	// Step 1: Resize the directory so that it can fit another directory entry.

	DirectoryEntry directory; 

	if (!ReadDirectoryEntryReference(directoryReference, &directory)) return false;
	AttributeData *dataAttribute = (AttributeData *) FindAttribute(&directory, ESFS_ATTRIBUTE_DATA);
	AttributeDirectory *directoryAttribute = (AttributeDirectory *) FindAttribute(&directory, ESFS_ATTRIBUTE_DIRECTORY);

	{

		assert(dataAttribute->indirection == ESFS_INDIRECTION_L1);

		if (!(directoryAttribute->childNodes % superblock.directoryEntriesPerBlock)) {
			// Log("increasing directory to fit %ld entries\n========={\n", (directory.fileSize + superblock.blockSize) / sizeof(DirectoryEntry));
			if (!ResizeNode(&directory, directory.fileSize + superblock.blockSize)) return false;
			// Log("========}\n");
		}

		directoryAttribute->childNodes++;

		if (!WriteDirectoryEntryReference(directoryReference, &directory)) {
			return false;
		}
	}

	// Step 2: Create the directory entry, and write it to the directory.

	DirectoryEntryReference reference = {};
	DirectoryEntry entry = {};

	{
		NewDirectoryEntry(&entry, nodeType, directory.identifier, name, contentType);
		// Log("\tchild nodes: %ld\n", directoryAttribute->childNodes);

		if (!AccessNode(&directory, &entry, (directoryAttribute->childNodes - 1) * sizeof(DirectoryEntry), sizeof(DirectoryEntry), &reference, false)) {
			return false;
		}
	}

	// Step 3: Add the node into the index. 

	uint64_t newKey = CalculateCRC64(name, strlen(name), 0);
	// Log("adding file '%s' to index...\n", name);

	{
		// Find the leaf to insert the key into.

		uint8_t buffer[superblock.blockSize];
		memset(buffer, 0, superblock.blockSize);
		IndexVertex *vertex = (IndexVertex *) buffer;
		uint64_t depth = 0, blocks[ESFS_INDEX_MAX_DEPTH] = { directoryAttribute->indexRootBlock };

		if (blocks[0] == 0) {
			// Directory is empty - create the root vertex.

			uint64_t _unused;

			if (!AllocateExtent(1, &directoryAttribute->indexRootBlock, &_unused)) {
				return false;
			}

			blocks[0] = directoryAttribute->indexRootBlock;
			vertex->maxCount = (superblock.blockSize - ESFS_INDEX_KEY_OFFSET) / sizeof(IndexKey) - 1 /* +1 key */;
			vertex->offset = ESFS_INDEX_KEY_OFFSET;
			memcpy(vertex->signature, ESFS_INDEX_VERTEX_SIGNATURE, 4);
			// Log("rootBlock = %ld for %s\n", directoryAttribute->indexRootBlock, path);
		} else {
			if (!ReadBlock(blocks[0], 1, vertex)) {
				return false;
			}
			// Log("start = %ld for %s\n", blocks[0], path);
		}

		while (true) {
			for (int i = 0; i < vertex->count; i++) {
				if (ESFS_VERTEX_KEY(vertex, i)->value == newKey) {
					// The key is already in the tree.
					Log("The file already exists.");
					EsFSError();
				}
			}

			for (int i = 0; i <= vertex->count; i++) {
				IndexKey *key = ESFS_VERTEX_KEY(vertex, i);

				if ((i == vertex->count || newKey < key->value) && key->child) {
					blocks[++depth] = key->child;

					if (!ReadBlock(key->child, 1, vertex)) {
						return false;
					}

					goto next;
				}
			}

			break;
			next:;	
		}

		// Insert the key into the vertex.

		InsertKeyIntoVertex(newKey, vertex)->data = reference;

		// While the vertex is full...

		assert(vertex->count <= vertex->maxCount);

		while (vertex->count == vertex->maxCount) {
			// Log("\tsplit!\n");

			char _buffer0[superblock.blockSize];
			char _buffer1[superblock.blockSize];
			memset(_buffer0, 0, superblock.blockSize);
			memset(_buffer1, 0, superblock.blockSize);

			// Create a new sibling.

			uint64_t siblingBlock = 0, _unused;
			if (!AllocateExtent(1, &siblingBlock, &_unused)) return false;
			IndexVertex *sibling = (IndexVertex *) _buffer0;
			sibling->maxCount = (superblock.blockSize - ESFS_INDEX_KEY_OFFSET) / sizeof(IndexKey) - 1 /* +1 key */;
			sibling->offset = ESFS_INDEX_KEY_OFFSET;
			memcpy(sibling->signature, ESFS_INDEX_VERTEX_SIGNATURE, 4);

			// Load the parent vertex.

			bool newRoot = !depth;
			IndexVertex *parent = (IndexVertex *) _buffer1;

			if (newRoot) {
				// Create a new root block.

				// Log("\t(new root)\n");

				blocks[1] = blocks[0];
				depth++;

				uint64_t _unused;
				if (!AllocateExtent(1, &blocks[0], &_unused)) return false;

				parent->maxCount = (superblock.blockSize - ESFS_INDEX_KEY_OFFSET) / sizeof(IndexKey) - 1 /* +1 key */;
				parent->offset = ESFS_INDEX_KEY_OFFSET;
				memcpy(parent->signature, ESFS_INDEX_VERTEX_SIGNATURE, 4);

				// The superblock points to the new root, and the +1 key of the new root points to the old root.
				// It has no other keys yet.

				parent->keys[0].child = blocks[1];
				directoryAttribute->indexRootBlock = blocks[0];
			} else {
				if (!ReadBlock(blocks[depth - 1], 1, parent)) {
					return false;
				}
			}

			IndexKey *parentKeys = (IndexKey *) ((uint8_t *) parent + parent->offset);
			IndexKey *vertexKeys = (IndexKey *) ((uint8_t *) vertex + vertex->offset);
			IndexKey *siblingKeys = (IndexKey *) ((uint8_t *) sibling + sibling->offset);

			// Change the link to this vertex to point to the sibling.

			int found = 0;

			for (uint64_t i = 0; i <= parent->count; i++) {
				if (parentKeys[i].child == blocks[depth]) {
					parentKeys[i].child = siblingBlock;
					found++;
				}
			}

			assert(found == 1);

			// Move the median key to the parent.
			// If this makes the parent full we'll fix it next iteration.

			uint64_t median = (vertex->maxCount - 1) / 2; 
			uint64_t newKey = vertexKeys[median].value;

			for (uint64_t i = 0; i <= parent->count; i++) {
				if (i == parent->count || newKey < parentKeys[i].value) {
					memmove(parentKeys + i + 1, parentKeys + i, (++parent->count - i) * sizeof(IndexKey));
					parentKeys[i].value = newKey;
					parentKeys[i].data = vertexKeys[median].data;
					parentKeys[i].child = blocks[depth];
					break;
				}
			}

			// Move all keys above the median key to the new sibling.
			sibling->count = vertex->count - median /*Kept in the node*/ - 1 /*Added to the parent*/;
			vertex->count = median; // The data on the median key becomes the +1 key's data.
			memcpy(siblingKeys, vertexKeys + median + 1, (sibling->count + 1) * sizeof(IndexKey));

			// Write the blocks.

			sibling->checksum = 0; sibling->checksum = CalculateCRC32(sibling, superblock.blockSize, 0);
			vertex->checksum = 0; vertex->checksum = CalculateCRC32(vertex, superblock.blockSize, 0);
			if (!WriteBlock(siblingBlock, 1, sibling)) return false;
			if (!WriteBlock(blocks[depth], 1, vertex)) return false;

			// Check if the parent vertex is full.

			memcpy(vertex, parent, superblock.blockSize);
			depth--;
		}

		// Write the block.

		vertex->checksum = 0; vertex->checksum = CalculateCRC32(vertex, superblock.blockSize, 0);
		if (!WriteBlock(blocks[depth], 1, vertex)) return false;
	}

	if (outputEntry) *outputEntry = entry;
	if (outputReference) *outputReference = reference;

	// PrintTree(directoryAttribute->indexRootBlock);
	if (!WriteDirectoryEntryReference(directoryReference, &directory)) {
		return false;
	}

	return true;
}

bool MountVolume() {
	// Read the superblock.
	blockSize = ESFS_BOOT_SUPER_BLOCK_SIZE;

	if (!ReadBlock(1, 1, &superblock)) {
		return false;
	}

	if (superblock.mounted) {
		Log("EsFS: Volume not unmounted, exiting...\n");
		EsFSError();
	}

	superblock.mounted = 1;

	if (!WriteBlock(1, 1, &superblock)) {
		return false;
	}

	blockSize = superblock.blockSize;

	// Read the group descriptor table.
	groupDescriptorTable = (GroupDescriptor *) malloc(superblock.groupCount * sizeof(GroupDescriptor) + superblock.blockSize - 1);

	if (!ReadBlock(superblock.gdtFirstBlock, (superblock.groupCount * sizeof(GroupDescriptor) + superblock.blockSize - 1) / superblock.blockSize, groupDescriptorTable)) {
		return false;
	}

	return true;
}

void UnmountVolume() {
	WriteBlock(superblock.gdtFirstBlock, (superblock.groupCount * sizeof(GroupDescriptor) + superblock.blockSize - 1) / superblock.blockSize, groupDescriptorTable);
	blockSize = ESFS_BOOT_SUPER_BLOCK_SIZE;
	superblock.mounted = 0;
	superblock.checksum = 0;
	superblock.checksum = CalculateCRC32(&superblock, sizeof(Superblock), 0);
	WriteBlock(1, 1, &superblock); 
	free(groupDescriptorTable);
}

bool FindNode(const char *cName, DirectoryEntry *node, DirectoryEntryReference directoryReference) {
	DirectoryEntry directory; 
	if (!ReadDirectoryEntryReference(directoryReference, &directory)) return false;
	AttributeDirectory *directoryAttribute = (AttributeDirectory *) FindAttribute(&directory, ESFS_ATTRIBUTE_DIRECTORY);

	for (uintptr_t i = 0; i < directoryAttribute->childNodes; i++) {
		if (!AccessNode(&directory, node, sizeof(DirectoryEntry) * i, sizeof(DirectoryEntry), NULL, true)) {
			return false;
		}

		AttributeFilename *filename = (AttributeFilename *) FindAttribute(node, ESFS_ATTRIBUTE_FILENAME);

		if (filename->length == strlen(cName) && 0 == memcmp(filename->filename, cName, filename->length)) {
			return true;
		}
	}

	Log("Could not find '%s'.\n", cName);
	return false;
}

#if 0
void Read(char *target, DirectoryEntryReference parentDirectory) {
	DirectoryEntryReference outputReference;
	DirectoryEntry entry;
	if (!FindNode(target, &entry, parentDirectory)) return;
	char *data = (char *) malloc(entry.fileSize);
	AccessNode(&entry, data, 0, entry.fileSize, &outputReference, true);
	fwrite(data, 1, entry.fileSize, stdout);
}
#endif

#ifndef INSTALLER
typedef struct ImportNode {
	const char *name, *path;
	struct ImportNode *children;
	bool isFile;
	EsUniqueIdentifier contentType;
} ImportNode;

int64_t Import(ImportNode node, DirectoryEntryReference parentDirectory) {
	uint64_t totalSize = 0;

	for (uintptr_t i = 0; i < arrlenu(node.children); i++) {
		if (node.children[i].isFile) {
			size_t fileLength;
			void *data = LoadFile(node.children[i].path, &fileLength);

			if (!data) {
				Log("Warning: Could not read file '%s'!\n", node.children[i].path);
			} else {
				copiedCount += fileLength;

				DirectoryEntryReference reference;
				DirectoryEntry entry;

				if (!AddNode(node.children[i].name, ESFS_NODE_TYPE_FILE, &entry, &reference, parentDirectory, node.children[i].contentType)) {
					return -1;
				}

				if (!ResizeNode(&entry, fileLength)) {
					return -1;
				}

				totalSize += fileLength;

				if (!AccessNode(&entry, data, 0, fileLength, NULL, false)) {
					return -1;
				}

				if (!WriteDirectoryEntryReference(reference, &entry)) {
					return -1;
				}

				free(data);
			}
		} else {
			DirectoryEntryReference reference;
			if (!AddNode(node.children[i].name, ESFS_NODE_TYPE_DIRECTORY, NULL, &reference, parentDirectory, node.children[i].contentType)) return -1;
			int64_t size = Import(node.children[i], reference);
			if (size == -1) return -1;
			DirectoryEntry directory; 
			if (!ReadDirectoryEntryReference(reference, &directory)) return -1;
			AttributeDirectory *directoryAttribute = (AttributeDirectory *) FindAttribute(&directory, ESFS_ATTRIBUTE_DIRECTORY);
			directoryAttribute->totalSize = size;
			if (!WriteDirectoryEntryReference(reference, &directory)) return -1;
			totalSize += size;
		}
	}

	return totalSize;
}
#endif

bool Format(uint64_t driveSize, const char *volumeName, EsUniqueIdentifier osInstallation,
		void *kernel, size_t kernelBytes) {
	assert(sizeof(Superblock) == 8192);

	if (driveSize < ESFS_DRIVE_MINIMUM_SIZE) {
		Log("Error: Cannot create a drive of %d bytes (too small).\n", (int) driveSize);
		EsFSError();
	}

	if (strlen(volumeName) > ESFS_MAXIMUM_VOLUME_NAME_LENGTH) {
		Log("Error: Volume name '%s' is too long; must be <= %d bytes.\n", volumeName, (int) ESFS_MAXIMUM_VOLUME_NAME_LENGTH);
		EsFSError();
	}

	// Format the volume.

	{
		memcpy(superblock.signature, ESFS_SIGNATURE_STRING, 16);
		memcpy(superblock.volumeName, volumeName, strlen(volumeName));

		superblock.requiredReadVersion = ESFS_DRIVER_VERSION;
		superblock.requiredWriteVersion = ESFS_DRIVER_VERSION;

		if (driveSize < 2048ll * 1024 * 1024) { // < 2GB
			superblock.blockSize = 2048; // Must be >= sizeof(DirectoryEntry).
		} else if (driveSize < 2ll * 1024 * 1024 * 1024 * 1024) { // < 2TB
			superblock.blockSize = 4096;
		} else if (driveSize < 256ll * 1024 * 1024 * 1024 * 1024) { // < 256TB
			superblock.blockSize = 8192;
		} else {
			superblock.blockSize = 16384;
		}

		superblock.blockCount = driveSize / superblock.blockSize;
		superblock.blocksPerGroup = 32768;
		if (superblock.blockCount < superblock.blocksPerGroup) superblock.blocksPerGroup = superblock.blockCount / 2;
		superblock.groupCount = (superblock.blockCount + superblock.blocksPerGroup - 1) / superblock.blocksPerGroup;
		superblock.blocksPerGroupBlockBitmap = ((superblock.blocksPerGroup + 7) / 8 + superblock.blockSize - 1) / superblock.blockSize;
		superblock.directoryEntriesPerBlock = superblock.blockSize / sizeof(DirectoryEntry);

		uint64_t blockGDT = ESFS_BOOT_SUPER_BLOCK_SIZE * 2 / superblock.blockSize + 1;
		uint64_t blockGroup0Bitmap = blockGDT + (superblock.groupCount * sizeof(GroupDescriptor) + superblock.blockSize - 1) / superblock.blockSize;
		uint64_t blockGroupLastBitmap = blockGroup0Bitmap + (superblock.groupCount * sizeof(GroupDescriptor) + superblock.blockSize - 1) / superblock.blockSize;
		uint64_t blockCoreNodes = blockGroupLastBitmap + superblock.blocksPerGroupBlockBitmap;
		uint64_t end = blockCoreNodes + ((ESFS_CORE_NODE_COUNT + superblock.directoryEntriesPerBlock - 1) / superblock.directoryEntriesPerBlock);

		superblock.blocksUsed = end;
		superblock.gdtFirstBlock = blockGDT;

		GenerateUniqueIdentifier(&superblock.identifier, true);
		superblock.osInstallation = osInstallation;

		DirectoryEntry coreNodes[ESFS_CORE_NODE_COUNT] = {};

		superblock.root.block =              (blockCoreNodes * superblock.blockSize + sizeof(DirectoryEntry) * ESFS_CORE_NODE_ROOT)   / superblock.blockSize;
		superblock.root.offsetIntoBlock =    (blockCoreNodes * superblock.blockSize + sizeof(DirectoryEntry) * ESFS_CORE_NODE_ROOT)   % superblock.blockSize;
		superblock.kernel.block =            (blockCoreNodes * superblock.blockSize + sizeof(DirectoryEntry) * ESFS_CORE_NODE_KERNEL) / superblock.blockSize;
		superblock.kernel.offsetIntoBlock =  (blockCoreNodes * superblock.blockSize + sizeof(DirectoryEntry) * ESFS_CORE_NODE_KERNEL) % superblock.blockSize;

		{
			// Root directory.
			DirectoryEntry *entry = coreNodes + ESFS_CORE_NODE_ROOT;
			memcpy(entry->signature, ESFS_DIRECTORY_ENTRY_SIGNATURE, 8);
			GenerateUniqueIdentifier(&entry->identifier, false);
			entry->attributeOffset = ESFS_ATTRIBUTE_OFFSET;
			entry->nodeType = ESFS_NODE_TYPE_DIRECTORY;
			entry->attributeCount = 2;
			AttributeDirectory *directory = (AttributeDirectory *) entry->attributes;
			directory->type = ESFS_ATTRIBUTE_DIRECTORY;
			directory->size = sizeof(AttributeDirectory);
			AttributeData *data = (AttributeData *) ((uint8_t *) directory + directory->size);
			data->type = ESFS_ATTRIBUTE_DATA;
			data->size = sizeof(DirectoryEntry) - ESFS_ATTRIBUTE_OFFSET - directory->size;
			data->indirection = ESFS_INDIRECTION_L1;
			data->dataOffset = ESFS_ATTRIBUTE_OFFSET;
			entry->checksum = CalculateCRC32(entry, sizeof(DirectoryEntry), 0);
		}

		if (!WriteBytes(blockCoreNodes * superblock.blockSize, sizeof(coreNodes), &coreNodes)) {
			return false;
		}

		superblock.checksum = CalculateCRC32(&superblock, sizeof(Superblock), 0);

		if (!WriteBytes(ESFS_BOOT_SUPER_BLOCK_SIZE, ESFS_BOOT_SUPER_BLOCK_SIZE, &superblock)) {
			return false;
		}

		{
			GroupDescriptor *buffer = (GroupDescriptor *) malloc(superblock.groupCount * sizeof(GroupDescriptor));
			memset(buffer, 0, superblock.groupCount * sizeof(GroupDescriptor));

			for (uintptr_t i = 0; i < superblock.groupCount; i++) {
				memcpy(buffer[i].signature, ESFS_GROUP_DESCRIPTOR_SIGNATURE, 4);
				buffer[i].largestExtent = superblock.blocksPerGroup - superblock.blocksPerGroupBlockBitmap;
				uint8_t bitmap[superblock.blocksPerGroupBlockBitmap * superblock.blockSize];

				if (i == 0) {
					memset(bitmap, 0, superblock.blocksPerGroupBlockBitmap * superblock.blockSize);
					for (uint64_t i = 0; i < superblock.blocksUsed; i++) bitmap[i / 8] |= 1 << (i % 8);
					buffer[i].blocksUsed = superblock.blocksUsed;
					buffer[i].blockBitmap = blockGroup0Bitmap;
					buffer[i].bitmapChecksum = CalculateCRC32(bitmap, sizeof(bitmap), 0);
					buffer[i].largestExtent = superblock.blocksPerGroup - superblock.blocksUsed;

					if (!WriteBytes(blockGroup0Bitmap * superblock.blockSize, sizeof(bitmap), &bitmap)) {
						return false;
					}
				} else if (i == superblock.groupCount - 1) {
					// Mark the blocks that go past the end of the file system in the last block group as allocated.
					// TODO Make sure these blocks aren't deallocated by any future file system checker tools!
					uint64_t overflow = superblock.blocksPerGroup * superblock.groupCount - superblock.blockCount;
					assert(overflow < superblock.blocksPerGroup);

					memset(bitmap, 0, superblock.blocksPerGroupBlockBitmap * superblock.blockSize);
					for (uint64_t i = superblock.blocksPerGroup - overflow; i < superblock.blocksPerGroup; i++) bitmap[i / 8] |= 1 << (i % 8);
					buffer[i].blocksUsed = overflow;
					buffer[i].blockBitmap = blockGroupLastBitmap;
					buffer[i].bitmapChecksum = CalculateCRC32(bitmap, sizeof(bitmap), 0);
					buffer[i].largestExtent = superblock.blocksPerGroup - overflow;

					if (!WriteBytes(blockGroupLastBitmap * superblock.blockSize, sizeof(bitmap), &bitmap)) {
						return false;
					}
				}

				buffer[i].checksum = CalculateCRC32(buffer + i, sizeof(GroupDescriptor), 0);
			}

			if (!WriteBytes(superblock.gdtFirstBlock * superblock.blockSize, superblock.groupCount * sizeof(GroupDescriptor), buffer)) {
				return false;
			}

			free(buffer);
		}
	}

	// Add the kernel.

	if (MountVolume()) {
		DirectoryEntryReference reference = superblock.kernel;
		DirectoryEntry entry = {};
		EsUniqueIdentifier unused = {};

		EsUniqueIdentifier elf = (EsUniqueIdentifier) {{ 0xAB, 0xDE, 0x98, 0xB5, 0x56, 0x2C, 0x04, 0xDF, 0x1E, 0x43, 0xC8, 0x10, 0x24, 0x63, 0xDB, 0xB8 }};
		NewDirectoryEntry(&entry, ESFS_NODE_TYPE_FILE, unused, "Kernel", elf);

		if (WriteDirectoryEntryReference(reference, &entry)) {
			if (ResizeNode(&entry, kernelBytes)) {
				if (AccessNode(&entry, kernel, 0, kernelBytes, NULL, false)) {
					WriteDirectoryEntryReference(reference, &entry);
				} else {
					return false;
				}
			} else {
				return false;
			}
		} else {
			return false;
		}

		UnmountVolume();
	} else {
		return false;
	}

	return true;
}

#endif
