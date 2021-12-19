// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

#include <module.h>

// Filesystem structures and constant definitions.
#include <shared/esfs2.h>

// TODO Calling FSDirectoryEntryFound on all directory entries seen during scanning, even if they're not the target.
// TODO Informing the block cache when a directory is truncated and its extents are freed.
// TODO Renaming directories does not work?
// TODO ESFS_CHECK_XXX are used to report out of memory errors, which shouldn't report KERNEL_PROBLEM_DAMAGED_FILESYSTEM.

#define ESFS_CHECK(x, y)                 if (!(x)) { KernelLog(LOG_ERROR, "EsFS", "damaged file system", y "\n"); return false; }
#define ESFS_CHECK_ERROR(x, y)           if ((x) != ES_SUCCESS) { KernelLog(LOG_ERROR, "EsFS", "damaged file system", y "\n"); return x; }
#define ESFS_CHECK_TO_ERROR(x, y, z)     if (!(x)) { KernelLog(LOG_ERROR, "EsFS", "damaged file system", y "\n"); return z; }
#define ESFS_CHECK_VA(x, y, ...)         if (!(x)) { KernelLog(LOG_ERROR, "EsFS", "damaged file system", y "\n", __VA_ARGS__); return false; }
#define ESFS_CHECK_RETURN(x, y)          if (!(x)) { KernelLog(LOG_ERROR, "EsFS", "damaged file system", y "\n"); return; }
#define ESFS_CHECK_CORRUPT(x, y)         if (!(x)) { KernelLog(LOG_ERROR, "EsFS", "damaged file system", y "\n"); return ES_ERROR_CORRUPT_DATA; }
#define ESFS_CHECK_FATAL(x, y)           if (!(x)) { KernelLog(LOG_ERROR, "EsFS", "damaged file system", "Mount - " y "\n"); return false; }
#define ESFS_CHECK_READ_ONLY(x, y)       if (!(x)) { KernelLog(LOG_ERROR, "EsFS", "mount read only", "Mount - " y " Mounting as read only.\n"); volume->readOnly = true; }
#define ESFS_CHECK_ERROR_READ_ONLY(x, y) if ((x) != ES_SUCCESS) { KernelLog(LOG_ERROR, "EsFS", "mount read only", "Mount - " y " Mounting as read only.\n"); volume->readOnly = true; }

struct Volume : KFileSystem {
	Superblock superblock;
	struct FSNode *root;
	bool readOnly;
	KWriterLock blockBitmapLock; 
	GroupDescriptor *groupDescriptorTable;
	KMutex nextIdentifierMutex;
};

struct FSNode {
	Volume *volume;
	DirectoryEntry entry;
	DirectoryEntryReference reference;
	EsUniqueIdentifier identifier;
	EsNodeType type;
	bool corrupt;
};

static bool AccessBlock(Volume *volume, uint64_t index, uint64_t count, void *buffer, uint64_t flags, int driveAccess) {
	// TODO Return EsError.
	Superblock *superblock = &volume->superblock;
	EsError error = volume->Access(index * superblock->blockSize, count * superblock->blockSize, driveAccess, buffer, flags, nullptr);
	ESFS_CHECK_ERROR(error, "AccessBlock - Could not access blocks.");
	return error == ES_SUCCESS;
}

static bool ValidateIndexVertex(Superblock *superblock, IndexVertex *vertex) {
	uint32_t checksum = vertex->checksum;
	vertex->checksum = 0;
	uint32_t calculated = CalculateCRC32(vertex, superblock->blockSize, 0);

	ESFS_CHECK(checksum == calculated, "ValidateIndexVertex - Invalid vertex checksum.");
	ESFS_CHECK(0 == EsMemoryCompare(vertex->signature, ESFS_INDEX_VERTEX_SIGNATURE, 4), "ValidateIndexVertex - Invalid vertex signature.");
	ESFS_CHECK(vertex->offset + vertex->maxCount * sizeof(IndexKey) < superblock->blockSize, "ValidateIndexVertex - Keys do not fit in vertex.");
	ESFS_CHECK(vertex->count <= vertex->maxCount, "ValidateIndexVertex - Too many keys in vertex.");

	return true;
}

static EsError FindDirectoryEntryReferenceFromIndex(Volume *volume, uint8_t *buffer /* superblock->blockSize */, 
		DirectoryEntryReference *entry, const char *name, size_t nameLength, uint64_t rootBlock) {
	// EsPrint("FindDirectoryEntryReferenceFromIndex: %s (%d)\n", nameLength, name, rootBlock);
	if (!rootBlock) return ES_ERROR_FILE_DOES_NOT_EXIST; // No index - the directory is empty?
	
	Superblock *superblock = &volume->superblock;

	// Get the root vertex.
	uint64_t nameHash = CalculateCRC64(name, nameLength, 0);
	IndexVertex *vertex = (IndexVertex *) buffer;

	if (!AccessBlock(volume, rootBlock, 1, vertex, FS_BLOCK_ACCESS_CACHED, K_ACCESS_READ)) {
		return ES_ERROR_DRIVE_CONTROLLER_REPORTED;
	}

	int depth = 0;

	while (true) {
		ESFS_CHECK(depth++ < ESFS_INDEX_MAX_DEPTH - 1, "FindDirectoryEntryReferenceFromIndex - Reached tree max depth.");

		if (!ValidateIndexVertex(superblock, vertex)) return ES_ERROR_CORRUPT_DATA;
		IndexKey *keys = (IndexKey *) ((uint8_t *) vertex + vertex->offset);

		// For every key...
		for (int i = 0; i <= vertex->count; i++) {
			if (i == vertex->count || keys[i].value > nameHash) {
				if (keys[i].child) {
					// The directory is in the child.
					if (!AccessBlock(volume, keys[i].child, 1, vertex, FS_BLOCK_ACCESS_CACHED, K_ACCESS_READ)) return ES_ERROR_DRIVE_CONTROLLER_REPORTED;
					else goto nextVertex;
				} else {
					// We couldn't find the entry.
					return ES_ERROR_FILE_DOES_NOT_EXIST;
				}
			} else if (keys[i].value == nameHash) {
				// We've found the directory.
				ESFS_CHECK_CORRUPT(keys[i].data.block < superblock->blockCount 
						&& keys[i].data.offsetIntoBlock + sizeof(DirectoryEntry) <= superblock->blockSize, 
						"FindDirectoryEntryReferenceFromIndex - Invalid key entry.");
				if (entry) *entry = keys[i].data;
				return ES_SUCCESS;
			}
		}

		nextVertex:;
	}
}

static bool ValidateDirectoryEntry(Volume *volume, DirectoryEntry *entry) {
	uint32_t checksum = entry->checksum;
	entry->checksum = 0;
	uint32_t calculated = CalculateCRC32(entry, sizeof(DirectoryEntry), 0);
	entry->checksum = calculated;

	ESFS_CHECK_VA(checksum == calculated, "ValidateDirectoryEntry - Invalid checksum (%x, calculated %x).", checksum, calculated);
	ESFS_CHECK(0 == EsMemoryCompare(entry->signature, ESFS_DIRECTORY_ENTRY_SIGNATURE, 8), "ValidateDirectoryEntry - Invalid signature.");
	ESFS_CHECK(entry->attributeOffset < sizeof(DirectoryEntry) - sizeof(Attribute), "ValidateDirectoryEntry - Invalid attribute offset.");

	Attribute *attribute = (Attribute *) ((uint8_t *) entry + entry->attributeOffset);

	for (uintptr_t i = 0; i < entry->attributeCount; i++) {
		ESFS_CHECK(attribute->size && attribute->type, "ValidateDirectoryEntry - Invalid attribute.");
		ESFS_CHECK((uintptr_t) attribute <= (uintptr_t) entry + sizeof(DirectoryEntry), "ValidateDirectoryEntry - Too many attributes.");

		if (attribute->type == ESFS_ATTRIBUTE_DATA) {
			AttributeData *data = (AttributeData *) attribute;

			if (data->indirection == ESFS_INDIRECTION_DIRECT) {
				ESFS_CHECK(data->dataOffset + data->count <= data->size, "ValidateDirectoryEntry - Data too long.");
				ESFS_CHECK(data->count == entry->fileSize, "ValidateDirectoryEntry - Expected direct attribute to cover entire file.");
			} 
		} else if (attribute->type == ESFS_ATTRIBUTE_DIRECTORY) {
			AttributeDirectory *directory = (AttributeDirectory *) attribute;
			ESFS_CHECK(directory->indexRootBlock < volume->superblock.blockCount, "ValidateDirectoryEntry - Directory index root block outside volume.");
		} else if (attribute->type == ESFS_ATTRIBUTE_FILENAME) {
			AttributeFilename *filename = (AttributeFilename *) attribute;
			ESFS_CHECK(filename->length + 8 <= filename->size, "ValidateDirectoryEntry - Filename too long.");
		} else {
			// Unrecognised attribute.
		}

		attribute = (Attribute *) ((uint8_t *) attribute + attribute->size);
	}

	return true;
}

static Attribute *FindAttribute(DirectoryEntry *entry, uint16_t type) {
	Attribute *attribute = (Attribute *) ((uint8_t *) entry + entry->attributeOffset);
	int count = 0;

	while (attribute->type != type) {
		if (!attribute->size) {
			KernelLog(LOG_ERROR, "EsFS", "damaged file system", "FindAttribute - Attribute size was 0.\n"); 
			return nullptr;
		}

		attribute = (Attribute *) ((uint8_t *) attribute + attribute->size);

		if (count++ == entry->attributeCount) {
			return nullptr;
		}
	}

	return attribute;
}

static bool ReadWrite(FSNode *file, uint64_t offset, uint64_t count, uint8_t *buffer, bool needBlockBuffer, bool write, 
		DirectoryEntryReference *reference = nullptr /* Returns the position of a directory just accessed */) {
	// TODO Return EsError.
	// TODO Support KWorkGroup.

	Volume *volume = file->volume;
	Superblock *superblock = &volume->superblock;
	DirectoryEntry *entry = &file->entry;

	uint64_t accessBlockFlags = 0;

	if (file->type == ES_NODE_DIRECTORY) {
		accessBlockFlags |= FS_BLOCK_ACCESS_CACHED;
	}

	uint8_t *blockBuffer = !needBlockBuffer ? nullptr : (uint8_t *) EsHeapAllocate(superblock->blockSize, false, K_FIXED);
	EsDefer(EsHeapFree(blockBuffer, 0, K_FIXED));
	ESFS_CHECK(!needBlockBuffer || blockBuffer, "Read - Could not allocate block buffer.");

	// EsPrint("ReadWrite - %d, %d, %x, %d\n", offset, count, buffer, write);

	if (!count) {
		return true;
	}

	AttributeData *data = (AttributeData *) FindAttribute(entry, ESFS_ATTRIBUTE_DATA);
	ESFS_CHECK(data, "Read - Expected data attribute.");

	if (data->indirection == ESFS_INDIRECTION_DIRECT) {
		EsAssert(data->dataOffset + offset <= data->size && data->dataOffset + offset + count <= data->size);

		if (write) {
			EsMemoryCopy((uint8_t *) data + data->dataOffset + offset, buffer, count);
		} else {
			EsMemoryCopy(buffer, (uint8_t *) data + data->dataOffset + offset, count);
		}
	} else if (data->indirection == ESFS_INDIRECTION_L1) {
		uint64_t offsetBlock = offset / superblock->blockSize;
		uint64_t offsetIntoCurrentBlock = offset % superblock->blockSize;
		uint8_t *extentList = (uint8_t *) data + data->dataOffset;
		uint64_t previousExtentStart = 0, positionInExtentList = 0, blockInFile = 0, extentIndex = 0;

		while (count) {
			// Find the extent containing offsetBlock.

			uint64_t extentStart = 0, extentCount = 0;

			while (!extentStart) {
				if (extentIndex == data->count) {
					ESFS_CHECK(false, "Read - Invalid extent.");
				}

				uint64_t count = 0;

				if (!DecodeExtent(&previousExtentStart, &count, extentList, &positionInExtentList, data->size - data->dataOffset) 
						|| !count || !previousExtentStart) {
					ESFS_CHECK(false, "Read - Invalid extent.");
				}

				// EsPrint("\tExtent %d -> %d covers blocks %d -> %d\n", previousExtentStart, previousExtentStart + count, blockInFile, blockInFile + count);

				if (blockInFile + count > offsetBlock) {
					uint64_t offsetIntoExtent = offsetBlock - blockInFile;
					extentStart = previousExtentStart + offsetIntoExtent;
					extentCount = count - offsetIntoExtent; 
					// EsPrint("\t\tUsing section %d -> %d for reading from block %d\n", extentStart, extentStart + extentCount, offsetBlock);
				}

				blockInFile += count;
				extentIndex++;
			}

			// Read the data.  

			repeatExtent:;

			if (offsetIntoCurrentBlock || count < superblock->blockSize) {
				if (!needBlockBuffer) {
					KernelPanic("EsFS::Read - Need a block buffer, but needBlockBuffer was false.\n");
				}

				uint64_t copyCount = superblock->blockSize - offsetIntoCurrentBlock;
				if (copyCount > count) copyCount = count;

				// EsPrint("\tCopying %d bytes through block buffer.\n", copyCount);

				if (!AccessBlock(volume, extentStart, 1, blockBuffer, accessBlockFlags, K_ACCESS_READ)) {
				     return false;
				}

				if (reference) {
					reference->block = extentStart;
					reference->offsetIntoBlock = offsetIntoCurrentBlock;
				}

				if (write) {
					EsMemoryCopy(blockBuffer + offsetIntoCurrentBlock, buffer, copyCount);

					if (!AccessBlock(volume, extentStart, 1, blockBuffer, accessBlockFlags, K_ACCESS_WRITE)) {
						return false;
					}
				} else {
					EsMemoryCopy(buffer, blockBuffer + offsetIntoCurrentBlock, copyCount);
				}

				buffer += copyCount, count -= copyCount;
				offsetIntoCurrentBlock = 0, offsetBlock++;
				extentStart++, extentCount--;

				// EsPrint("\t\tUsing section %d -> %d for reading from block %d\n", extentStart, extentCount, offsetBlock);
			}

			{
				uint64_t bytesToRead = extentCount * superblock->blockSize;
				if (bytesToRead > count) bytesToRead = count;
				bytesToRead -= bytesToRead % superblock->blockSize;
				uint64_t blocksRead = bytesToRead / superblock->blockSize;

				// EsPrint("\tReading %d blocks from %d.\n", blocksRead, extentStart);

				if (reference && bytesToRead) {
					reference->block = extentStart;
					reference->offsetIntoBlock = 0;
				}

				if (!AccessBlock(volume, extentStart, blocksRead, buffer, accessBlockFlags, 
							write ? K_ACCESS_WRITE : K_ACCESS_READ)) {
					return false;
				}

				buffer += bytesToRead, count -= bytesToRead;

				offsetBlock += blocksRead;
				extentStart += blocksRead, extentCount -= blocksRead;

				if (extentCount && count) {
				     goto repeatExtent;
				}
			}
		}
	} else {
		ESFS_CHECK(data, "Read - Unrecognised indirection mode.");
		return false;
	}

	return true;
}

static size_t Read(KNode *node, void *_buffer, EsFileOffset offset, EsFileOffset count) {
	FSNode *file = (FSNode *) node->driverNode;
	if (file->corrupt) return ES_ERROR_CORRUPT_DATA;
	return ReadWrite(file, offset, count, (uint8_t *) _buffer, true, false) ? count : ES_ERROR_UNKNOWN;
}

static size_t Write(KNode *node, const void *_buffer, EsFileOffset offset, EsFileOffset count) {
	FSNode *file = (FSNode *) node->driverNode;
	if (file->corrupt) return ES_ERROR_CORRUPT_DATA;
	return ReadWrite(file, offset, count, (uint8_t *) _buffer, true, true) ? count : ES_ERROR_UNKNOWN;
}

static void Sync(KNode *_directory, KNode *node) {
	(void) _directory;
	FSNode *file = (FSNode *) node->driverNode;
	if (!file) KernelPanic("EsFS::Sync - Node %x has null driver node.\n", node);
	if (file->corrupt) return;
	Volume *volume = file->volume;
	Superblock *superblock = &volume->superblock;

	// EsPrint("SYNC! %d,%d\n", file->reference.block, file->reference.offsetIntoBlock);

	if (file->type == ES_NODE_DIRECTORY) {
		// Get the most recent totalSize for the directory.
		AttributeDirectory *directoryAttribute = (AttributeDirectory *) FindAttribute(&file->entry, ESFS_ATTRIBUTE_DIRECTORY);
		directoryAttribute->totalSize = FSNodeGetTotalSize(node);
	}

	{
		file->entry.checksum = 0;
		file->entry.checksum = CalculateCRC32(&file->entry, sizeof(DirectoryEntry), 0);
	}

	uint8_t *blockBuffer = (uint8_t *) EsHeapAllocate(superblock->blockSize, false, K_FIXED);
	EsDefer(EsHeapFree(blockBuffer, 0, K_FIXED));
	ESFS_CHECK_RETURN(blockBuffer, "Sync - Could not allocate block buffer.");

	if (!AccessBlock(volume, file->reference.block, 1, blockBuffer, FS_BLOCK_ACCESS_CACHED, K_ACCESS_READ)) {
		KernelLog(LOG_ERROR, "EsFS", "drive access failure", "Sync - Could not read reference block.\n");
		return;
	}

	if (!ValidateDirectoryEntry(volume, (DirectoryEntry *) (blockBuffer + file->reference.offsetIntoBlock))) {
		return;
	}

	EsMemoryCopy(blockBuffer + file->reference.offsetIntoBlock, &file->entry, sizeof(DirectoryEntry));

	if (!AccessBlock(volume, file->reference.block, 1, blockBuffer, FS_BLOCK_ACCESS_CACHED, K_ACCESS_WRITE)) {
		KernelLog(LOG_ERROR, "EsFS", "drive access failure", "Sync - Could not write reference block.\n");
		return;
	}
}

static EsError Enumerate(KNode *node) {
	// TODO Support KWorkGroup.

	FSNode *file = (FSNode *) node->driverNode;
	if (file->corrupt) return ES_ERROR_CORRUPT_DATA;
	Volume *volume = file->volume;
	Superblock *superblock = &volume->superblock;
	DirectoryEntry *entry = &file->entry;

	uint8_t *blockBuffer = (uint8_t *) EsHeapAllocate(superblock->blockSize, false, K_FIXED);
	if (!blockBuffer) return ES_ERROR_INSUFFICIENT_RESOURCES;
	EsDefer(EsHeapFree(blockBuffer, 0, K_FIXED));

	DirectoryEntryReference reference = {};
	AttributeDirectory *directory = (AttributeDirectory *) FindAttribute(entry, ESFS_ATTRIBUTE_DIRECTORY);
	uint64_t blocksInDirectory = (directory->childNodes + superblock->directoryEntriesPerBlock - 1) / superblock->directoryEntriesPerBlock;

	for (uint64_t i = 0; i < blocksInDirectory; i++) {
		if (!ReadWrite(file, i * superblock->blockSize, superblock->blockSize, blockBuffer, false, false, &reference)) {
			return ES_ERROR_UNKNOWN;
		}

		uint64_t entriesInThisBlock = superblock->directoryEntriesPerBlock;

		if (i == blocksInDirectory - 1 && directory->childNodes % superblock->directoryEntriesPerBlock) {
			entriesInThisBlock = directory->childNodes % superblock->directoryEntriesPerBlock;
		}

		for (uint64_t j = 0; j < entriesInThisBlock; j++, reference.offsetIntoBlock += sizeof(DirectoryEntry)) {
			DirectoryEntry *entry = (DirectoryEntry *) blockBuffer + j;

			if (!ValidateDirectoryEntry(volume, entry)) {
				// Try the entries in the next block.
				break;
			}

			AttributeFilename *filename = (AttributeFilename *) FindAttribute(entry, ESFS_ATTRIBUTE_FILENAME);
			if (!filename) continue;

			KNodeMetadata metadata = {};

			metadata.type = entry->nodeType == ESFS_NODE_TYPE_DIRECTORY ? ES_NODE_DIRECTORY : ES_NODE_FILE;

			if (metadata.type == ES_NODE_DIRECTORY) {
				AttributeDirectory *directory = (AttributeDirectory *) FindAttribute(entry, ESFS_ATTRIBUTE_DIRECTORY);

				if (directory) {
					metadata.directoryChildren = directory->childNodes;
					metadata.totalSize = directory->totalSize;
				}
			} else if (metadata.type == ES_NODE_FILE) {
				metadata.totalSize = entry->fileSize;
			}

			EsError error = FSDirectoryEntryFound(node, &metadata, &reference, 
					(const char *) filename->filename, filename->length, false);

			if (error != ES_SUCCESS) {
				return error;
			}
		}
	}

	return ES_SUCCESS;
}

static uint64_t FindLargestExtent(uint8_t *bitmap, Superblock *superblock) {
	uint64_t largestExtentCount = 0, i = 0;

	while (i < superblock->blocksPerGroup) {
		if (bitmap[i / 8] & (1 << (i % 8))) {
			i++;
		} else {
			uint64_t count = 0;

			while (i < superblock->blocksPerGroup) {
				if (bitmap[i / 8] & (1 << (i % 8))) break;
				else count++, i++;
			}

			if (largestExtentCount < count) {
				largestExtentCount = count;
			}
		}
	}

	return largestExtentCount;
}

static bool ValidateGroupDescriptor(GroupDescriptor *descriptor) {
	uint32_t checksum = descriptor->checksum;
	descriptor->checksum = 0;
	uint32_t calculated = CalculateCRC32(descriptor, sizeof(GroupDescriptor), 0);
	ESFS_CHECK(checksum == calculated, "ValidateGroupDescriptor - Invalid checksum.");
	ESFS_CHECK(0 == EsMemoryCompare(descriptor->signature, ESFS_GROUP_DESCRIPTOR_SIGNATURE, 4), "ValidateGroupDescriptor - Invalid signature.");
	return true;
}

static bool ValidateBlockBitmap(GroupDescriptor *descriptor, uint8_t *bitmap, Superblock *superblock) {
	uint32_t calculated = CalculateCRC32(bitmap, superblock->blocksPerGroupBlockBitmap * superblock->blockSize, 0);
	ESFS_CHECK(calculated == descriptor->bitmapChecksum, "ValidateBlockBitmap - Invalid checksum.");

	uint32_t blocksUsed = 0;

	for (uint64_t i = 0; i < superblock->blocksPerGroup; i++) {
		if (bitmap[i / 8] & (1 << (i % 8))) {
			blocksUsed++;
		}
	}

	ESFS_CHECK(blocksUsed == descriptor->blocksUsed, "ValidateBlockBitmap - Used block count mismatch.");

	return true;
}

static bool AllocateExtent(Volume *volume, uint64_t nearby, uint64_t increaseBlocks, uint64_t *extentStart, uint64_t *extentCount, bool zero) {
	Superblock *superblock = &volume->superblock;
	KWriterLockAssertExclusive(&volume->blockBitmapLock);

	(void) nearby;
	// TODO Smarter extent allocation.

	uint8_t *zeroBuffer = nullptr;
	EsDefer(EsHeapFree(zeroBuffer, 0, K_FIXED));
	size_t zeroBufferSize = superblock->blockSize * (increaseBlocks > 16 ? 16 : increaseBlocks);

	if (zero) {
		zeroBuffer = (uint8_t *) EsHeapAllocate(zeroBufferSize, true, K_FIXED);
		ESFS_CHECK(zeroBuffer, "AllocateExtent - Could not allocate buffer for zeroing extent.");
	}

	// Find a group to allocate the next extent from.

	GroupDescriptor *target = nullptr;

	{
		for (uint64_t i = 0; !target && i < superblock->groupCount; i++) {
			GroupDescriptor *group = volume->groupDescriptorTable + i;
			if (!group->blocksUsed) group->largestExtent = superblock->blocksPerGroup - superblock->blocksPerGroupBlockBitmap;
			if (group->largestExtent >= increaseBlocks) target = group;
		}

		for (uint64_t i = 0; !target && i < superblock->groupCount; i++) {
			GroupDescriptor *group = volume->groupDescriptorTable + i;
			if (superblock->blocksPerGroup - group->blocksUsed >= increaseBlocks) target = group;
		}

		for (uint64_t i = 0; !target && i < superblock->groupCount; i++) {
			GroupDescriptor *group = volume->groupDescriptorTable + i;
			if (superblock->blocksPerGroup != group->blocksUsed) target = group;
		}
	}

	if (!target) {
		return false;
	}

	ESFS_CHECK(ValidateGroupDescriptor(target), "AllocateExtent - Invalid group descriptor.");

	// Load the bitmap, find the largest extent, and mark it as in use.

	uint8_t *bitmap = (uint8_t *) EsHeapAllocate(superblock->blocksPerGroupBlockBitmap * superblock->blockSize, false, K_FIXED);
	EsDefer(EsHeapFree(bitmap, 0, K_FIXED));
	ESFS_CHECK(bitmap, "AllocateExtent - Could not allocate buffer for block bitmap.");

	{
		if (target->blockBitmap) {
			if (!AccessBlock(volume, target->blockBitmap, superblock->blocksPerGroupBlockBitmap, bitmap, FS_BLOCK_ACCESS_CACHED, K_ACCESS_READ)) {
				return false;
			}

			ESFS_CHECK(ValidateBlockBitmap(target, bitmap, superblock), "AllocateExtent - Invalid block bitmap.");
		} else {
			EsMemoryZero(bitmap, superblock->blocksPerGroupBlockBitmap * superblock->blockSize);
			for (uint64_t i = 0; i < superblock->blocksPerGroupBlockBitmap; i++) bitmap[i / 8] |= 1 << (i % 8);
			target->blockBitmap = superblock->blocksPerGroup * (target - volume->groupDescriptorTable);
			target->blocksUsed = superblock->blocksPerGroupBlockBitmap;
		}

		uint64_t largestExtentStart = 0, largestExtentCount = 0, i = 0;

		while (i < superblock->blocksPerGroup) {
			if (bitmap[i / 8] & (1 << (i % 8))) {
				i++;
			} else {
				uint64_t start = i, count = 0;

				while (i < superblock->blocksPerGroup) {
					if (bitmap[i / 8] & (1 << (i % 8))) break;
					else count++, i++;
				}

				if (largestExtentCount < count) {
					largestExtentStart = start;
					largestExtentCount = count;
				}
			}
		}

		*extentStart = largestExtentStart;
		*extentCount = largestExtentCount;

		if (*extentCount > increaseBlocks) {
			*extentCount = increaseBlocks;
		}

		for (uint64_t i = *extentStart; i < *extentStart + *extentCount; i++) {
			bitmap[i / 8] |= 1 << (i % 8);
		}

		if (!AccessBlock(volume, target->blockBitmap, superblock->blocksPerGroupBlockBitmap, bitmap, FS_BLOCK_ACCESS_CACHED, K_ACCESS_WRITE)) {
			return false;
		}

		target->largestExtent = FindLargestExtent(bitmap, superblock);
		target->blocksUsed += *extentCount;
		target->bitmapChecksum = CalculateCRC32(bitmap, superblock->blocksPerGroupBlockBitmap * superblock->blockSize, 0);
		target->checksum = 0;
		target->checksum = CalculateCRC32(target, sizeof(GroupDescriptor), 0);
	}

	*extentStart += (target - volume->groupDescriptorTable) * superblock->blocksPerGroup;
	superblock->blocksUsed += *extentCount;
	volume->spaceUsed += *extentCount * superblock->blockSize;

	if (zero) {
		// TODO This is really slow - introduce K_ACCESS_ZERO?
		// TODO Support KWorkGroup.

		for (uint64_t i = 0; i < *extentCount * superblock->blockSize; i += zeroBufferSize) {
#if 0
			if ((i / zeroBufferSize) % 100 == 0) {
				EsPrint("AllocateExtent zeroing %d/%d\n", i / zeroBufferSize, extentCount * superblock->blockSize / zeroBufferSize);
			}
#endif

			uint64_t count = zeroBufferSize;

			if (i + count >= *extentCount * superblock->blockSize) {
				count = *extentCount * superblock->blockSize - i;
			}

			if (!AccessBlock(volume, *extentStart + i / superblock->blockSize, count / superblock->blockSize, zeroBuffer, ES_FLAGS_DEFAULT, K_ACCESS_WRITE)) {
				return false;
			}
		}
	}

	return true;
}

static bool FreeExtent(Volume *volume, uint64_t extentStart, uint64_t extentCount) {
	// TODO Return EsError.
	Superblock *superblock = &volume->superblock;
	KWriterLockAssertExclusive(&volume->blockBitmapLock);

	uint64_t blockGroup = extentStart / superblock->blocksPerGroup;

	// Validate the extent.

	ESFS_CHECK((extentStart + extentCount - 1) / superblock->blocksPerGroup == blockGroup, "FreeExtent - Extent spans multiple block groups.");
	ESFS_CHECK(extentCount < superblock->blocksUsed, "FreeExtent - Extent is larged than the number of used blocks.");
	ESFS_CHECK(extentStart + extentCount < superblock->blockCount, "FreeExtent - Extent goes past end of the volume.");

	// Load the block bitmap.

	GroupDescriptor *target = volume->groupDescriptorTable + blockGroup;
	ESFS_CHECK(ValidateGroupDescriptor(target), "FreeExtent - Invalid group descriptor.");
	uint8_t *bitmap = (uint8_t *) EsHeapAllocate(superblock->blocksPerGroupBlockBitmap * superblock->blockSize, false, K_FIXED);
	EsDefer(EsHeapFree(bitmap, 0, K_FIXED));
	ESFS_CHECK(bitmap, "FreeExtent - Could not allocate buffer for block bitmap.");
	ESFS_CHECK(target->blockBitmap, "FreeExtent - Group descriptor does not have block bitmap.");
	ESFS_CHECK(target->blocksUsed >= extentCount, "FreeExtent - Group descriptor indicates fewer blocks are used than are given in this extent.");
	ESFS_CHECK(AccessBlock(volume, target->blockBitmap, superblock->blocksPerGroupBlockBitmap, bitmap, FS_BLOCK_ACCESS_CACHED, K_ACCESS_READ), "FreeExtent - Could not read block bitmap.");
	ESFS_CHECK(ValidateBlockBitmap(target, bitmap, superblock), "FreeExtent - Invalid block bitmap.");

	// Clear the bits representing the freed blocks.

	for (uint64_t i = 0; i < extentCount; i++) {
		uint64_t blockInGroup = (extentStart % superblock->blocksPerGroup) + i;
		uint8_t bit = bitmap[blockInGroup / 8] & (1 << (blockInGroup % 8));
		ESFS_CHECK(bit, "FreeExtent - Attempting to free a block that has not been allocated.");
		bitmap[blockInGroup / 8] &= ~(1 << (blockInGroup % 8));
	}

	// Write out the modified bitmap and update the group descriptor.

	if (!AccessBlock(volume, target->blockBitmap, superblock->blocksPerGroupBlockBitmap, bitmap, FS_BLOCK_ACCESS_CACHED, K_ACCESS_WRITE)) {
		return false;
	}

	target->largestExtent = FindLargestExtent(bitmap, superblock);
	target->bitmapChecksum = CalculateCRC32(bitmap, superblock->blocksPerGroupBlockBitmap * superblock->blockSize, 0);
	target->blocksUsed -= extentCount;
	target->checksum = 0;
	target->checksum = CalculateCRC32(target, sizeof(GroupDescriptor), 0);
	superblock->blocksUsed -= extentCount;
	volume->spaceUsed -= extentCount * superblock->blockSize;

	return true;
}

static uint64_t ResizeInternal(FSNode *file, uint64_t newSize, EsError *error, uint64_t newDataAttributeSize = 0) {
	if (file->corrupt) return *error = ES_ERROR_CORRUPT_DATA, 0;

	Volume *volume = file->volume;
	Superblock *superblock = &volume->superblock;
	DirectoryEntry *entry = &file->entry;

	uint64_t oldSize = entry->fileSize;

	if (newDataAttributeSize && newSize != oldSize) {
		KernelPanic("EsFS::ResizeInternal - Attempting to change data attribute and node size at the same time.\n");
	}

	if (!newDataAttributeSize && newSize == oldSize) {
		// The file size hasn't changed.
		return newSize;
	}

	uint8_t *blockBuffer = (uint8_t *) EsHeapAllocate(superblock->blockSize, true, K_FIXED);
	EsDefer(EsHeapFree(blockBuffer, 0, K_FIXED));

	*error = ES_ERROR_INSUFFICIENT_RESOURCES;
	ESFS_CHECK(blockBuffer, "Resize - Could not allocate block buffer.");

	AttributeData *data = (AttributeData *) FindAttribute(entry, ESFS_ATTRIBUTE_DATA);
	uint8_t *dataBuffer = (uint8_t *) data + data->dataOffset;
	size_t dataBufferSize = data->size - data->dataOffset;
	size_t newDataBufferSize = (newDataAttributeSize ? newDataAttributeSize : data->size) - data->dataOffset;

	if (newDataBufferSize >= newSize && entry->nodeType != ESFS_NODE_TYPE_DIRECTORY) {
		if (data->indirection == ESFS_INDIRECTION_DIRECT) {
		} else if (data->indirection == ESFS_INDIRECTION_L1) {
			// Load the data from the indirect storage.

			if (entry->fileSize) {
				if (!ReadWrite(file, 0, superblock->blockSize, blockBuffer, false, false)) {
					return *error = ES_ERROR_DRIVE_ERROR_FILE_DAMAGED, entry->fileSize;
				}
			}

			// Free the extents.

			uint64_t previousExtentStart = 0, extentCount = 0, position = 0;

			for (uintptr_t i = 0; i < data->count; i++) {
				if (!DecodeExtent(&previousExtentStart, &extentCount, dataBuffer, &position, dataBufferSize)) {
					file->corrupt = true;
					*error = ES_ERROR_CORRUPT_DATA;
					return (entry->fileSize = 0);
				}

				KWriterLockTake(&volume->blockBitmapLock, K_LOCK_EXCLUSIVE);
				EsDefer(KWriterLockReturn(&volume->blockBitmapLock, K_LOCK_EXCLUSIVE));

				if (!FreeExtent(volume, previousExtentStart, extentCount)) {
					file->corrupt = true;
					entry->fileSize = 0;
					*error = ES_ERROR_DRIVE_ERROR_FILE_DAMAGED;
					return (entry->fileSize = 0);
				}
			}

			// Store the existing data in the entry.

			EsMemoryCopy(dataBuffer, blockBuffer, newSize);
		} else {
			*error = ES_ERROR_UNSUPPORTED_FEATURE;
			return entry->fileSize; // Unrecognised indirection.
		}

		data->indirection = ESFS_INDIRECTION_DIRECT;
		data->count = newSize;

		// Zero out everything past the new size.
		EsMemoryZero(dataBuffer + newSize, newDataBufferSize - newSize);
	} else {
		uint64_t oldBlocks = (entry->fileSize + superblock->blockSize - 1) / superblock->blockSize;
		uint64_t newBlocks = (newSize + superblock->blockSize - 1) / superblock->blockSize;
		bool copyData = false;

		if (data->indirection == ESFS_INDIRECTION_DIRECT) {
			if (!ReadWrite(file, 0, entry->fileSize, blockBuffer, false, false)) {
				*error = ES_ERROR_DRIVE_ERROR_FILE_DAMAGED;
				return entry->fileSize;
			}

			copyData = entry->fileSize > 0;
			data->count = 0;
			oldBlocks = 0;
			entry->fileSize = 0;
		} else if (data->indirection != ESFS_INDIRECTION_L1) {
			*error = ES_ERROR_UNSUPPORTED_FEATURE;
			return entry->fileSize; // Unrecognised indirection.
		}

		if (oldBlocks < newBlocks) {
			uint64_t previousExtentStart = 0, oldPreviousExtentStart = 0, extentCount = 0, position = 0, previousPosition = 0;

			for (uintptr_t i = 0; i < data->count; i++) {
				previousPosition = position;
				oldPreviousExtentStart = previousExtentStart;

				if (!DecodeExtent(&previousExtentStart, &extentCount, dataBuffer, &position, dataBufferSize)) {
					*error = ES_ERROR_CORRUPT_DATA;
					return entry->fileSize;
				}
			}

			KWriterLockTake(&volume->blockBitmapLock, K_LOCK_EXCLUSIVE);
			EsDefer(KWriterLockReturn(&volume->blockBitmapLock, K_LOCK_EXCLUSIVE));

			uint64_t remaining = newBlocks - oldBlocks;

			if (superblock->blocksUsed + remaining >= superblock->blockCount) {
				// There isn't enough space to grow the file.
				*error = ES_ERROR_DRIVE_FULL;
				return entry->fileSize;
			}

			while (remaining) {
				uint64_t allocatedStart, allocatedCount;

				bool success = AllocateExtent(volume, 
						previousExtentStart + extentCount /* Attempt to allocate near the end of the last extent */, 
						remaining /* Attempt to get an extent covering all the remaining blocks */,
						&allocatedStart, &allocatedCount, true /* Zero the blocks */);

				if (!success) {
					*error = ES_ERROR_DRIVE_ERROR_FILE_DAMAGED;
					return entry->fileSize;
				}

				if (previousExtentStart + extentCount == allocatedStart) {
					// We need to grow the previous extent.

					allocatedStart = previousExtentStart;
					previousExtentStart = oldPreviousExtentStart;
					remaining += extentCount;
					allocatedCount += extentCount;
					position = previousPosition;
					data->count--;
				}

				oldPreviousExtentStart = previousExtentStart;
				uint8_t encode[32];
				uint64_t length = EncodeExtent(allocatedStart, previousExtentStart, allocatedCount, encode);

				if (length + position > newDataBufferSize) {
					// The data buffer is full.
					*error = ES_ERROR_FILE_TOO_FRAGMENTED;
					return entry->fileSize;
				} else {
					EsMemoryCopy(dataBuffer + position, encode, length);
					previousPosition = position;
					position += length;
					data->count++;
					remaining -= allocatedCount;
					entry->fileSize += allocatedCount * superblock->blockSize;
					previousExtentStart = allocatedStart;
				}
			}
		} else if (oldBlocks > newBlocks) {
			uint64_t previousExtentStart = 0, extentCount = 0, 
				 position = 0, blockInFile = 0;

			file->corrupt = true;
			entry->fileSize = 0;
			*error = ES_ERROR_DRIVE_ERROR_FILE_DAMAGED;

			// Free the removed extents.

			for (uintptr_t i = 0; i < data->count; i++) {
				if (!DecodeExtent(&previousExtentStart, &extentCount, dataBuffer, &position, dataBufferSize)) {
					return 0;
				}

				if (blockInFile + extentCount > newBlocks) {
					uint64_t extentStart = previousExtentStart, extentCount2 = extentCount;

					if (blockInFile < newBlocks) {
						extentStart += newBlocks - blockInFile;
						extentCount2 -= newBlocks - blockInFile;
					}

					KWriterLockTake(&volume->blockBitmapLock, K_LOCK_EXCLUSIVE);
					EsDefer(KWriterLockReturn(&volume->blockBitmapLock, K_LOCK_EXCLUSIVE));

					if (!FreeExtent(volume, extentStart, extentCount2)) {
						return 0;
					}
				}

				blockInFile += extentCount;
			}

			// Modify the last extent.

			previousExtentStart = 0, position = 0, blockInFile = 0;

			for (uintptr_t i = 0; i < data->count; i++) {
				uint64_t previousPosition = position, lastExtentStart = previousExtentStart;

				if (!DecodeExtent(&previousExtentStart, &extentCount, dataBuffer, &position, dataBufferSize)) {
					return 0;
				}

				if (blockInFile + extentCount > newBlocks) {
					uint64_t extentStart = previousExtentStart;

					if (blockInFile < newBlocks) {
						uint8_t encode[32];
						uint64_t length = EncodeExtent(extentStart, lastExtentStart, newBlocks - blockInFile, encode);
						EsMemoryCopy(dataBuffer + previousPosition, encode, length);
						data->count = i + 1;
						break;
					} else {
						data->count = i;
						break;
					}
				}

				blockInFile += extentCount;
			}
		} else {
			// Do nothing.
		}

		data->indirection = ESFS_INDIRECTION_L1;

		if (copyData) {
			if (!ReadWrite(file, 0, superblock->blockSize, blockBuffer, false, true)) {
				// Rollback changes.
				data->indirection = ESFS_INDIRECTION_DIRECT;
				data->count = entry->fileSize = oldSize;
				EsMemoryCopy(dataBuffer, blockBuffer, data->count);
				*error = ES_ERROR_DRIVE_ERROR_FILE_DAMAGED;
				return (entry->fileSize = oldSize);
			}
		}
	}

	file->corrupt = false;
	*error = ES_SUCCESS;
	if (newDataAttributeSize) data->size = newDataAttributeSize;
	return (entry->fileSize = newSize);
}

static uint64_t Resize(KNode *node, uint64_t newSize, EsError *error) {
	// EsPrint("Resize %s to %d\n", node->name.bytes, node->name.buffer, newSize);
	return ResizeInternal((FSNode *) node->driverNode, newSize, error);
}

static IndexKey *InsertKeyIntoVertex(uint64_t newKey, IndexVertex *vertex) {
	// Find where in this vertex we should insert the key.

	int position;

	for (position = 0; position < vertex->count; position++) {
		if (newKey < ESFS_VERTEX_KEY(vertex, position)->value) {
			break;
		}
	}

	// Insert the key.

	IndexKey *insertionPosition = ESFS_VERTEX_KEY(vertex, position);
	EsMemoryMove(insertionPosition, ESFS_VERTEX_KEY(vertex, vertex->count + 1), sizeof(IndexKey), true);
	vertex->count++;
	insertionPosition->value = newKey;

	return insertionPosition;
}

static bool IndexModifyKey(Volume *volume, uint64_t newKey, DirectoryEntryReference reference, uint64_t rootBlock, uint8_t *buffer /* superblock->blockSize */) {
	// TODO Return EsError.
	if (!rootBlock) return false; // No index - the directory is empty?
	
	Superblock *superblock = &volume->superblock;

	// Get the root vertex.
	IndexVertex *vertex = (IndexVertex *) buffer;
	uint64_t block;

	if (!AccessBlock(volume, (block = rootBlock), 1, vertex, FS_BLOCK_ACCESS_CACHED, K_ACCESS_READ)) {
		return false;
	}

	int depth = 0;

	while (true) {
		ESFS_CHECK(depth++ < ESFS_INDEX_MAX_DEPTH - 1, "IndexModifyKey - Reached tree max depth.");

		if (!ValidateIndexVertex(superblock, vertex)) return false;
		IndexKey *keys = (IndexKey *) ((uint8_t *) vertex + vertex->offset);

		// For every key...
		for (int i = 0; i <= vertex->count; i++) {
			if (i == vertex->count || keys[i].value > newKey) {
				if (keys[i].child) {
					// The directory is in the child.
					if (!AccessBlock(volume, (block = keys[i].child), 1, vertex, FS_BLOCK_ACCESS_CACHED, K_ACCESS_READ)) return false;
					else goto nextVertex;
				} else {
					// We couldn't find the entry.
					return false;
				}
			} else if (keys[i].value == newKey) {
				// We've found the key.
				ESFS_CHECK_CORRUPT(keys[i].data.block < superblock->blockCount 
						&& keys[i].data.offsetIntoBlock + sizeof(DirectoryEntry) <= superblock->blockSize, 
						"IndexModifyKey - Invalid key entry.");
				keys[i].data = reference;
				vertex->checksum = 0; vertex->checksum = CalculateCRC32(vertex, superblock->blockSize, 0);
				return AccessBlock(volume, block, 1, vertex, FS_BLOCK_ACCESS_CACHED, K_ACCESS_WRITE);
			}
		}

		nextVertex:;
	}
}

static bool IndexAddKey(Volume *volume, uint64_t newKey, DirectoryEntryReference reference, uint64_t *rootBlock) {
	// TODO Return EsError.
	// TODO Support KWorkGroup.
	
	// EsPrint("adding key %x\n", newKey);

	Superblock *superblock = &volume->superblock;
	uint8_t *blockBuffers = (uint8_t *) EsHeapAllocate(superblock->blockSize * 3, true, K_FIXED);
	EsDefer(EsHeapFree(blockBuffers, 0, K_FIXED));
	ESFS_CHECK(blockBuffers, "IndexAddKey - Could not allocate block buffers.");

	uint64_t _unused;

	// Find the leaf to insert the key into.

	uint8_t *buffer = blockBuffers + 0 * superblock->blockSize;
	IndexVertex *vertex = (IndexVertex *) buffer;
	uint64_t depth = 0, blocks[ESFS_INDEX_MAX_DEPTH] = { *rootBlock };
	bool skipFirst = false;

	if (blocks[0] == 0) {
		// Directory is empty - create the root vertex.

		KWriterLockTake(&volume->blockBitmapLock, K_LOCK_EXCLUSIVE);
		ESFS_CHECK(AllocateExtent(volume, 0 /* TODO */, 1, &blocks[0], &_unused, false), "IndexAddKey - Could not allocate space.");
		KWriterLockReturn(&volume->blockBitmapLock, K_LOCK_EXCLUSIVE);
		*rootBlock = blocks[0];
		vertex->maxCount = (superblock->blockSize - ESFS_INDEX_KEY_OFFSET) / sizeof(IndexKey) - 1 /* +1 key */;
		vertex->offset = ESFS_INDEX_KEY_OFFSET;
		EsMemoryCopy(vertex->signature, ESFS_INDEX_VERTEX_SIGNATURE, 4);
		skipFirst = true;
	}

	{
		next:;

		if (!skipFirst) {
			ESFS_CHECK(AccessBlock(volume, blocks[depth], 1, vertex, FS_BLOCK_ACCESS_CACHED, K_ACCESS_READ), "IndexAddKey - Could not read index.");

			if (!ValidateIndexVertex(superblock, vertex)) {
				return false;
			}
		}

		for (int i = 0; i < vertex->count; i++) {
			if (ESFS_VERTEX_KEY(vertex, i)->value == newKey) {
				// The key is already in the tree.
				// TODO Hash collisions.
				ESFS_CHECK(false, "IndexAddKey - Possible hash collision?");
			}
		}

		for (int i = 0; i <= vertex->count; i++) {
			IndexKey *key = ESFS_VERTEX_KEY(vertex, i);

			if ((i == vertex->count || newKey < key->value) && key->child) {
				ESFS_CHECK(depth < ESFS_INDEX_MAX_DEPTH - 1, "IndexAddKey - Reached tree max depth.");

				blocks[++depth] = key->child;
				goto next;
			}
		}
	}

	ESFS_CHECK(vertex->count < vertex->maxCount, "IndexAddKey - Vertex is full; unbalanced tree.");
	
	// Insert the key into the vertex.

	InsertKeyIntoVertex(newKey, vertex)->data = reference;

	// While the vertex is full...

	if (vertex->count > vertex->maxCount) {
		KernelPanic("IndexAddKey - Corrupt vertex.");
	}

	while (vertex->count == vertex->maxCount) {
		// printf("\tsplit!\n");

		uint8_t *_buffer0 = blockBuffers + 1 * superblock->blockSize;
		uint8_t *_buffer1 = blockBuffers + 2 * superblock->blockSize;

		// Create a new sibling.

		uint64_t siblingBlock = 0;

		{
			KWriterLockTake(&volume->blockBitmapLock, K_LOCK_EXCLUSIVE);
			EsDefer(KWriterLockReturn(&volume->blockBitmapLock, K_LOCK_EXCLUSIVE));
			ESFS_CHECK(AllocateExtent(volume, blocks[depth], 1, &siblingBlock, &_unused, false), "IndexAddKey - Could not allocate space.");
		}

		IndexVertex *sibling = (IndexVertex *) _buffer0;
		sibling->maxCount = (superblock->blockSize - ESFS_INDEX_KEY_OFFSET) / sizeof(IndexKey) - 1 /* +1 key */;
		sibling->offset = ESFS_INDEX_KEY_OFFSET;
		EsMemoryCopy(sibling->signature, ESFS_INDEX_VERTEX_SIGNATURE, 4);

		// Load the parent vertex.

		bool newRoot = !depth;
		IndexVertex *parent = (IndexVertex *) _buffer1;

		if (newRoot) {
			// Create a new root block.

			blocks[1] = blocks[0];
			depth++;

			{
				KWriterLockTake(&volume->blockBitmapLock, K_LOCK_EXCLUSIVE);
				EsDefer(KWriterLockReturn(&volume->blockBitmapLock, K_LOCK_EXCLUSIVE));
				ESFS_CHECK(AllocateExtent(volume, blocks[1], 1, &blocks[0], &_unused, false), "IndexAddKey - Could not allocate space.");
			}

			parent->maxCount = (superblock->blockSize - ESFS_INDEX_KEY_OFFSET) / sizeof(IndexKey) - 1 /* +1 key */;
			parent->offset = ESFS_INDEX_KEY_OFFSET;
			EsMemoryCopy(parent->signature, ESFS_INDEX_VERTEX_SIGNATURE, 4);

			// The superblock points to the new root, and the +1 key of the new root points to the old root.
			// It has no other keys yet.

			parent->keys[0].child = blocks[1];
			*rootBlock = blocks[0];
		} else {
			ESFS_CHECK(AccessBlock(volume, blocks[depth - 1], 1, parent, FS_BLOCK_ACCESS_CACHED, K_ACCESS_READ), "IndexAddKey - Could not read index.");
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

		ESFS_CHECK(found == 1, "IndexAddKey - Could not find current vertex in its parent.");

		// Move the median key to the parent.
		// If this makes the parent full we'll fix it next iteration.

		uint64_t median = (vertex->maxCount - 1) / 2; 
		uint64_t newKey = vertexKeys[median].value;

		for (uint64_t i = 0; i <= parent->count; i++) {
			if (i == parent->count || newKey < parentKeys[i].value) {
				parent->count++;
				EsMemoryMove(parentKeys + i, parentKeys + parent->count, sizeof(IndexKey), true);
				parentKeys[i].value = newKey;
				parentKeys[i].data = vertexKeys[median].data;
				parentKeys[i].child = blocks[depth];
				break;
			}
		}

		// Move all keys above the median key to the new sibling.

		sibling->count = vertex->count - median /*Kept in the node*/ - 1 /*Added to the parent*/;
		vertex->count = median; // The data on the median key becomes the +1 key's data.
		EsMemoryCopy(siblingKeys, vertexKeys + median + 1, (sibling->count + 1) * sizeof(IndexKey));

		// Write the blocks.

		sibling->checksum = 0; sibling->checksum = CalculateCRC32(sibling, superblock->blockSize, 0);
		vertex->checksum = 0; vertex->checksum = CalculateCRC32(vertex, superblock->blockSize, 0);
		ESFS_CHECK(AccessBlock(volume, siblingBlock, 1, sibling, FS_BLOCK_ACCESS_CACHED, K_ACCESS_WRITE), "IndexAddKey - Could not update index.");
		ESFS_CHECK(AccessBlock(volume, blocks[depth], 1, vertex, FS_BLOCK_ACCESS_CACHED, K_ACCESS_WRITE), "IndexAddKey - Could not update index.");

		// Check if the parent vertex is full.

		EsMemoryCopy(vertex, parent, superblock->blockSize);
		depth--;
	}

	// Write the block.

	vertex->checksum = 0; vertex->checksum = CalculateCRC32(vertex, superblock->blockSize, 0);
	ESFS_CHECK(AccessBlock(volume, blocks[depth], 1, vertex, FS_BLOCK_ACCESS_CACHED, K_ACCESS_WRITE), "IndexAddKey - Could not update index.");

	return true;
}

static bool IndexRemoveKey(Volume *volume, uint64_t removeKey, uint64_t *rootBlock) {
	// TODO Return EsError.
	// TODO Support KWorkGroup.

	Superblock *superblock = &volume->superblock;
	uint8_t *blockBuffers = (uint8_t *) EsHeapAllocate(superblock->blockSize * 3, true, K_FIXED);
	EsDefer(EsHeapFree(blockBuffers, 0, K_FIXED));
	ESFS_CHECK(blockBuffers, "IndexRemoveKey - Could not allocate block buffers.");

	// Find the vertex that contains this key.

	uint8_t *buffer = blockBuffers + 0 * superblock->blockSize;
	IndexVertex *vertex = (IndexVertex *) buffer;
	uint64_t depth = 0, blocks[ESFS_INDEX_MAX_DEPTH] = { *rootBlock };
	ESFS_CHECK(blocks[0], "IndexRemoveKey - Index has no root.");
	int position = 0;

	{
		next:;
		int i;

		ESFS_CHECK(AccessBlock(volume, blocks[depth], 1, vertex, FS_BLOCK_ACCESS_CACHED, K_ACCESS_READ), "IndexRemoveKey - Could not read index.");
		if (!ValidateIndexVertex(superblock, vertex)) return false;

		for (i = 0; i <= vertex->count; i++) {
			IndexKey *key = ESFS_VERTEX_KEY(vertex, i);

			if (i != vertex->count && key->value == removeKey) {
				// EsPrint("found key %x at depth %d block %d position %d\n", removeKey, depth, blocks[depth], i);
				goto done;
			} else if ((i == vertex->count || removeKey < key->value) && key->child) {
				ESFS_CHECK(depth < ESFS_INDEX_MAX_DEPTH - 1, "IndexRemoveKey - Reached tree max depth.");
				blocks[++depth] = key->child;
				// EsPrint("recurse into block %d at depth %d position %d\n", blocks[depth], depth, i);
				goto next;
			}
		}

		ESFS_CHECK(false, "IndexRemoveKey - The key was not in the tree.");
		done:;
		position = i;
	}

	if (ESFS_VERTEX_KEY(vertex, position)->child) {
		// If the removed key has children, replace it with the smallest above, then consider that leaf.

		uint64_t startDepth = depth;
		uint8_t *buffer2 = blockBuffers + 1 * superblock->blockSize;
		IndexVertex *search = (IndexVertex *) buffer2;
		blocks[++depth] = ESFS_VERTEX_KEY(vertex, position + 1)->child;

		while (true) {
			ESFS_CHECK(AccessBlock(volume, blocks[depth], 1, search, FS_BLOCK_ACCESS_CACHED, K_ACCESS_READ), "IndexRemoveKey - Could not read index.");
			if (!ValidateIndexVertex(superblock, search)) return false;

			if (ESFS_VERTEX_KEY(search, 0)->child) {
				ESFS_CHECK(depth < ESFS_INDEX_MAX_DEPTH - 1, "IndexRemoveKey - Reached tree max depth.");
				blocks[++depth] = ESFS_VERTEX_KEY(vertex, 1)->child;
			} else break;
		}

		ESFS_VERTEX_KEY(vertex, position)->value = ESFS_VERTEX_KEY(search, 0)->value;
		ESFS_VERTEX_KEY(vertex, position)->data  = ESFS_VERTEX_KEY(search, 0)->data;

		vertex->checksum = 0; vertex->checksum = CalculateCRC32(vertex, superblock->blockSize, 0);
		ESFS_CHECK(AccessBlock(volume, blocks[startDepth], 1, vertex, FS_BLOCK_ACCESS_CACHED, K_ACCESS_WRITE), "IndexRemoveKey - Could not write index.");

		EsMemoryCopy(vertex, search, superblock->blockSize);
		position = 0;
	}

	// Remove the key.

	// EsPrint("Removing key from vertex...\n");
	EsMemoryMove(ESFS_VERTEX_KEY(vertex, position + 1), ESFS_VERTEX_KEY(vertex, vertex->count + 1), ES_MEMORY_MOVE_BACKWARDS sizeof(IndexKey), true);
	vertex->count--;

	repeat:;

	// If the vertex still has enough keys, return.

	if (vertex->count >= (vertex->maxCount - 1) / 2) {
		// EsPrint("Vertex has enough keys, exiting...\n");
		vertex->checksum = 0; vertex->checksum = CalculateCRC32(vertex, superblock->blockSize, 0);
		ESFS_CHECK(AccessBlock(volume, blocks[depth], 1, vertex, FS_BLOCK_ACCESS_CACHED, K_ACCESS_WRITE), "IndexRemoveKey - Could not write index.");
		return true;
	}

	// If the we reach the root of the tree, we're done.

	if (depth == 0) {
		if (!vertex->count && vertex->keys[0].child) {
			// Reduce the height of the tree.

			KWriterLockTake(&volume->blockBitmapLock, K_LOCK_EXCLUSIVE);
			EsDefer(KWriterLockReturn(&volume->blockBitmapLock, K_LOCK_EXCLUSIVE));
			ESFS_CHECK(FreeExtent(volume, blocks[0], 1), "IndexRemoveKey - Could not free the old root index block.");
			*rootBlock = vertex->keys[0].child;
		} else {
			// EsPrint("Vertex is at root, exiting...\n");
			vertex->checksum = 0; vertex->checksum = CalculateCRC32(vertex, superblock->blockSize, 0);
			ESFS_CHECK(AccessBlock(volume, blocks[depth], 1, vertex, FS_BLOCK_ACCESS_CACHED, K_ACCESS_WRITE), "IndexRemoveKey - Could not write index.");
		}

		return true; 
	}

	// Find the position of vertex in its parent.

	uint8_t *buffer2 = blockBuffers + 1 * superblock->blockSize;
	IndexVertex *parent = (IndexVertex *) buffer2;
	ESFS_CHECK(AccessBlock(volume, blocks[depth - 1], 1, parent, FS_BLOCK_ACCESS_CACHED, K_ACCESS_READ), "IndexRemoveKey - Could not read index.");
	if (!ValidateIndexVertex(superblock, parent)) return false;

	int positionInParent = -1;

	for (int i = 0; i <= parent->count; i++) {
		if (ESFS_VERTEX_KEY(parent, i)->child == blocks[depth]) {
			positionInParent = i;
			break;
		}
	}

	ESFS_CHECK(positionInParent != -1, "IndexRemoveKey - Could not find position in parent.");

	uint8_t *buffer3 = blockBuffers + 2 * superblock->blockSize;
	IndexVertex *sibling = (IndexVertex *) buffer3;

	if (positionInParent) {
		ESFS_CHECK(AccessBlock(volume, ESFS_VERTEX_KEY(parent, positionInParent - 1)->child, 1, sibling, FS_BLOCK_ACCESS_CACHED, K_ACCESS_READ), "IndexRemoveKey - Could not read index.");

		if (sibling->count > (sibling->maxCount - 1) / 2) {
			// Steal left.

			EsMemoryMove(ESFS_VERTEX_KEY(vertex, 0), ESFS_VERTEX_KEY(vertex, vertex->count + 1), sizeof(IndexKey), true);

			ESFS_VERTEX_KEY(vertex, 0)->			value = ESFS_VERTEX_KEY(parent, 	positionInParent - 1)->	value;
			ESFS_VERTEX_KEY(vertex, 0)->			data = 	ESFS_VERTEX_KEY(parent, 	positionInParent - 1)->	data;
			ESFS_VERTEX_KEY(vertex, 0)->			child = ESFS_VERTEX_KEY(sibling, 	sibling->count)->	child;
			ESFS_VERTEX_KEY(parent, positionInParent - 1)->	value = ESFS_VERTEX_KEY(sibling, 	sibling->count - 1)->	value;
			ESFS_VERTEX_KEY(parent, positionInParent - 1)->	data = 	ESFS_VERTEX_KEY(sibling, 	sibling->count - 1)->	data;

			sibling->count--, vertex->count++;

			vertex->checksum = 0; 	vertex->checksum = 	CalculateCRC32(vertex, 	superblock->blockSize, 0);
			sibling->checksum = 0; 	sibling->checksum = 	CalculateCRC32(sibling, superblock->blockSize, 0);
			parent->checksum = 0; 	parent->checksum = 	CalculateCRC32(parent, 	superblock->blockSize, 0);

			ESFS_CHECK(AccessBlock(volume, ESFS_VERTEX_KEY(parent, positionInParent - 1)->child, 1, sibling, FS_BLOCK_ACCESS_CACHED, K_ACCESS_WRITE), "IndexRemoveKey - Could not write index.");
			ESFS_CHECK(AccessBlock(volume, blocks[depth], 1, vertex, FS_BLOCK_ACCESS_CACHED, K_ACCESS_WRITE), "IndexRemoveKey - Could not write index.");
			ESFS_CHECK(AccessBlock(volume, blocks[depth - 1], 1, parent, FS_BLOCK_ACCESS_CACHED, K_ACCESS_WRITE), "IndexRemoveKey - Could not write index.");

			return true;
		}
	}

	if (positionInParent != parent->count) {
		ESFS_CHECK(AccessBlock(volume, ESFS_VERTEX_KEY(parent, positionInParent + 1)->child, 1, sibling, FS_BLOCK_ACCESS_CACHED, K_ACCESS_READ), "IndexRemoveKey - Could not read index.");

		if (sibling->count > (sibling->maxCount - 1) / 2) {
			// Steal right.

			ESFS_VERTEX_KEY(vertex, vertex->count)->	value = ESFS_VERTEX_KEY(parent, 	positionInParent)->	value;
			ESFS_VERTEX_KEY(vertex, vertex->count)->	data = 	ESFS_VERTEX_KEY(parent, 	positionInParent)->	data;
			ESFS_VERTEX_KEY(vertex, vertex->count + 1)->	child = ESFS_VERTEX_KEY(sibling, 	0)->			child;
			ESFS_VERTEX_KEY(parent, positionInParent)->	value = ESFS_VERTEX_KEY(sibling, 	0)->			value;
			ESFS_VERTEX_KEY(parent, positionInParent)->	data = 	ESFS_VERTEX_KEY(sibling, 	0)->			data;

			EsMemoryMove(ESFS_VERTEX_KEY(sibling, 1), ESFS_VERTEX_KEY(sibling, sibling->count + 1), ES_MEMORY_MOVE_BACKWARDS sizeof(IndexKey), true);

			sibling->count--, vertex->count++;

			vertex->checksum = 0; 	vertex->checksum = 	CalculateCRC32(vertex, 	superblock->blockSize, 0);
			sibling->checksum = 0; 	sibling->checksum = 	CalculateCRC32(sibling, superblock->blockSize, 0);
			parent->checksum = 0; 	parent->checksum = 	CalculateCRC32(parent, 	superblock->blockSize, 0);

			ESFS_CHECK(AccessBlock(volume, ESFS_VERTEX_KEY(parent, positionInParent + 1)->child, 1, sibling, FS_BLOCK_ACCESS_CACHED, K_ACCESS_WRITE), "IndexRemoveKey - Could not write index.");
			ESFS_CHECK(AccessBlock(volume, blocks[depth], 1, vertex, FS_BLOCK_ACCESS_CACHED, K_ACCESS_WRITE), "IndexRemoveKey - Could not write index.");
			ESFS_CHECK(AccessBlock(volume, blocks[depth - 1], 1, parent, FS_BLOCK_ACCESS_CACHED, K_ACCESS_WRITE), "IndexRemoveKey - Could not write index.");

			return true;
		}
	}

	{
		// Merge nodes.

		if (!positionInParent) {
			EsMemoryCopy(sibling, vertex, superblock->blockSize);
			positionInParent = 1;
			blocks[depth] = ESFS_VERTEX_KEY(parent, positionInParent)->child;
			ESFS_CHECK(AccessBlock(volume, blocks[depth], 1, vertex, FS_BLOCK_ACCESS_CACHED, K_ACCESS_READ), "IndexRemoveKey - Could not read index.");
			if (!ValidateIndexVertex(superblock, vertex)) return false;
		} else {
			ESFS_CHECK(AccessBlock(volume, ESFS_VERTEX_KEY(parent, positionInParent - 1)->child, 1, sibling, FS_BLOCK_ACCESS_CACHED, K_ACCESS_READ), "IndexRemoveKey - Could not read index.");
			if (!ValidateIndexVertex(superblock, sibling)) return false;
		}

		// TODO I'm fairly certain this won't happen (as we should have stolen a key), but it needs to be double-checked.
		ESFS_CHECK(sibling->count + vertex->count + 1 < sibling->maxCount, "IndexRemoveKey - Merged node would be too large.");

		ESFS_VERTEX_KEY(sibling, sibling->count)->	value = ESFS_VERTEX_KEY(parent, positionInParent - 1)->value;
		ESFS_VERTEX_KEY(sibling, sibling->count)->	data = 	ESFS_VERTEX_KEY(parent, positionInParent - 1)->data;
		ESFS_VERTEX_KEY(parent,  positionInParent)->	child = ESFS_VERTEX_KEY(parent, positionInParent - 1)->child;

		EsMemoryCopy(ESFS_VERTEX_KEY(sibling, sibling->count + 1), ESFS_VERTEX_KEY(vertex, 0), (vertex->count + 1) * sizeof(IndexKey));
		EsMemoryMove(ESFS_VERTEX_KEY(parent, positionInParent), ESFS_VERTEX_KEY(parent, parent->count + 1), ES_MEMORY_MOVE_BACKWARDS sizeof(IndexKey), true);

		sibling->count += vertex->count + 1, parent->count--;

		{
			KWriterLockTake(&volume->blockBitmapLock, K_LOCK_EXCLUSIVE);
			EsDefer(KWriterLockReturn(&volume->blockBitmapLock, K_LOCK_EXCLUSIVE));
			ESFS_CHECK(FreeExtent(volume, blocks[depth], 1), "IndexRemoveKey - Could not free merged vertex.");
		}

		sibling->checksum = 0; 	sibling->checksum = 	CalculateCRC32(sibling, superblock->blockSize, 0);
		ESFS_CHECK(AccessBlock(volume, ESFS_VERTEX_KEY(parent, positionInParent - 1)->child, 1, sibling, FS_BLOCK_ACCESS_CACHED, K_ACCESS_WRITE), "IndexRemoveKey - Could not write index.");

		EsMemoryCopy(vertex, parent, superblock->blockSize);
		depth--;

		goto repeat;
	}
}

static EsError RemoveDirectoryEntry(FSNode *file, uint8_t *blockBuffers /* superblock->blockSize * 2 */, FSNode *directory, KNode *_directory) {
	// EsPrint("RemoveDirectoryEntry for %s from %s\n", node->name.bytes, node->name.buffer, 
	// 		node->parent->name.bytes, node->parent->name.buffer);

	Volume *volume = file->volume;
	Superblock *superblock = &volume->superblock;
	DirectoryEntry *entry = &file->entry;
	AttributeDirectory *directoryAttribute = (AttributeDirectory *) FindAttribute(&directory->entry, ESFS_ATTRIBUTE_DIRECTORY);
	EsError error;

	// EsPrint("\tChildren = %d\n", directoryAttribute->childNodes);

	// Step 1: Replace the directory entry with the last entry in the directory.

	uint64_t positionOfLastEntry = (directoryAttribute->childNodes - 1) * sizeof(DirectoryEntry);

	// EsPrint("\tpositionOfLastEntry = %d\n\tThis node Reference = %d/%d\n", positionOfLastEntry, file->reference.block, file->reference.offsetIntoBlock);

	ESFS_CHECK_TO_ERROR(AccessBlock(volume, file->reference.block, 1, blockBuffers, FS_BLOCK_ACCESS_CACHED, K_ACCESS_READ), 
			"Remove - Could not load the container block.", ES_ERROR_DRIVE_CONTROLLER_REPORTED);
	ESFS_CHECK_TO_ERROR(ReadWrite(directory, positionOfLastEntry & ~(superblock->blockSize - 1), superblock->blockSize, 
				blockBuffers + superblock->blockSize, false, false), "Remove - Could not load the last block.", ES_ERROR_DRIVE_CONTROLLER_REPORTED);
	ESFS_CHECK_TO_ERROR(0 == EsMemoryCompare(blockBuffers + file->reference.offsetIntoBlock, &file->entry, sizeof(DirectoryEntry)), 
			"Remove - Inconsistent file entry.", ES_ERROR_DRIVE_CONTROLLER_REPORTED);

	DirectoryEntry *movedEntry = (DirectoryEntry *) (blockBuffers + superblock->blockSize + (positionOfLastEntry & (superblock->blockSize - 1)));
	DirectoryEntry *deletedEntry = (DirectoryEntry *) (blockBuffers + file->reference.offsetIntoBlock);
	EsMemoryCopy(deletedEntry, movedEntry, sizeof(DirectoryEntry));
	ESFS_CHECK_TO_ERROR(AccessBlock(volume, file->reference.block, 1, blockBuffers, FS_BLOCK_ACCESS_CACHED, K_ACCESS_WRITE), 
			"Remove - Could not save the container block.", ES_ERROR_DRIVE_CONTROLLER_REPORTED);

	// Step 2: Update the node for the moved entry.

	if (EsMemoryCompare(movedEntry->identifier.d, entry->identifier.d, sizeof(EsUniqueIdentifier))) {
		AttributeFilename *filename = (AttributeFilename *) FindAttribute(movedEntry, ESFS_ATTRIBUTE_FILENAME);

		if (filename) {
			KNode *node = nullptr;

			FSDirectoryEntryFound(_directory, nullptr, &file->reference, 
					(const char *) filename->filename, filename->length, true, &node);

			if (node) {
				((FSNode *) node->driverNode)->reference = file->reference;
			}

			uint64_t key = CalculateCRC64(filename->filename, filename->length, 0);
			// EsPrint("\tModify index key for %s\n", filename->length, filename->filename);
			ESFS_CHECK_TO_ERROR(IndexModifyKey(volume, key, file->reference, directoryAttribute->indexRootBlock, blockBuffers + superblock->blockSize), 
					"Remove - Could not update index (2).", ES_ERROR_DRIVE_CONTROLLER_REPORTED);
		}
	}

	// Step 3: Decrease the size of the directory.

	directoryAttribute->childNodes--;

	if (!(directoryAttribute->childNodes % superblock->directoryEntriesPerBlock)) {
		uint64_t newSize = directory->entry.fileSize - superblock->blockSize;
		ESFS_CHECK_TO_ERROR(newSize == ResizeInternal(directory, newSize, &error), "Remove - Could not resize directory.", ES_ERROR_DRIVE_CONTROLLER_REPORTED);
	}

	// Step 4: Remove the entry from the index.

	AttributeFilename *filename = (AttributeFilename *) FindAttribute(entry, ESFS_ATTRIBUTE_FILENAME);
	// EsPrint("\tRemoving %s from index\n", filename->length, filename->filename);

	if (filename) {
		uint64_t removeKey = CalculateCRC64(filename->filename, filename->length, 0);
		ESFS_CHECK_TO_ERROR(IndexRemoveKey(volume, removeKey, &directoryAttribute->indexRootBlock), "Remove - Could not update index.", ES_ERROR_DRIVE_CONTROLLER_REPORTED);
	}

	return ES_SUCCESS;
}

static EsError Remove(KNode *_directory, KNode *node) {
	FSNode *directory = (FSNode *) _directory->driverNode;
	FSNode *file = (FSNode *) node->driverNode;
	Volume *volume = file->volume;
	Superblock *superblock = &volume->superblock;
	DirectoryEntry *entry = &file->entry;
	AttributeDirectory *directoryAttribute = (AttributeDirectory *) FindAttribute(&directory->entry, ESFS_ATTRIBUTE_DIRECTORY);
	ESFS_CHECK_TO_ERROR(directoryAttribute->childNodes, "Remove - Directory is empty.", ES_ERROR_CORRUPT_DATA);

	uint8_t *blockBuffers = (uint8_t *) EsHeapAllocate(superblock->blockSize * 2, false, K_FIXED);
	if (!blockBuffers) return ES_ERROR_INSUFFICIENT_RESOURCES;
	EsDefer(EsHeapFree(blockBuffers, 0, K_FIXED));

	// Step 1: If we're deleting a directory, deallocate its empty index.

	if (entry->nodeType == ESFS_NODE_TYPE_DIRECTORY) {
		AttributeDirectory *attribute = (AttributeDirectory *) FindAttribute(entry, ESFS_ATTRIBUTE_DIRECTORY);
		ESFS_CHECK_TO_ERROR(!attribute->childNodes, "Remove - Directory was not empty.", ES_ERROR_CORRUPT_DATA);
		
		if (attribute->indexRootBlock) {
			IndexVertex *vertex = (IndexVertex *) blockBuffers;
			ESFS_CHECK_TO_ERROR(AccessBlock(volume, attribute->indexRootBlock, 1, vertex, FS_BLOCK_ACCESS_CACHED, K_ACCESS_READ), 
					"Remove - Could not access index root.", ES_ERROR_DRIVE_CONTROLLER_REPORTED);
			ESFS_CHECK_TO_ERROR(!vertex->count, "Remove - Index was not empty (although it should be as the directory is empty).", ES_ERROR_CORRUPT_DATA);
			KWriterLockTake(&volume->blockBitmapLock, K_LOCK_EXCLUSIVE);
			EsDefer(KWriterLockReturn(&volume->blockBitmapLock, K_LOCK_EXCLUSIVE));
			ESFS_CHECK_TO_ERROR(FreeExtent(volume, attribute->indexRootBlock, 1), 
					"Remove - Could not free the root index block.", ES_ERROR_UNKNOWN);
		}
	}

	// Step 2: Truncate the node to 0 bytes.

	EsError error;

	if (ResizeInternal(file, 0, &error)) {
		ESFS_CHECK_TO_ERROR(false, "Remove - Could not resize node.", error);
	}

	// Step 3: Sync the file, and remove its directory entry.

	Sync(_directory, node);
	return RemoveDirectoryEntry(file, blockBuffers, directory, _directory);
}

static bool RenameInternal(FSNode *existingNode, DirectoryEntry *entry, const void *name, size_t nameLength) {
	// Size of name + size of header, rounded up to the nearest 8 bytes.
	size_t newFilenameSize = ((nameLength + ESFS_FILENAME_HEADER_SIZE - 1) & ~7) + 8; 

	AttributeFilename *filename = (AttributeFilename *) FindAttribute(entry, ESFS_ATTRIBUTE_FILENAME);
	AttributeData *data = (AttributeData *) FindAttribute(entry, ESFS_ATTRIBUTE_DATA);

	if (filename && data && (uint8_t *) filename - (uint8_t *) data == data->size) {
		// TODO Improve this.
		// TODO Renaming directories does not work!

		intptr_t filenameSizeChange = newFilenameSize - filename->size;
		size_t newDataSize = data->size - filenameSizeChange;

		EsError error;
		ESFS_CHECK(existingNode->entry.fileSize == ResizeInternal(existingNode, existingNode->entry.fileSize, &error, newDataSize), 
				"CreateInternal - Could not resize data attribute.");
		ESFS_CHECK(error == ES_SUCCESS, "CreateInternal - Could not resize data attribute.");

		AttributeFilename *filename = (AttributeFilename *) ((uint8_t *) data + data->size);
		filename->type = ESFS_ATTRIBUTE_FILENAME;
		filename->size = newFilenameSize;
		filename->length = nameLength;
		EsMemoryCopy(filename->filename, name, filename->length);
	} else {
		ESFS_CHECK(false, "CreateInternal - Could not rename node.");
	}

	return true;
}

static bool CreateInternal(const char *name, size_t nameLength, EsNodeType type, FSNode *parent, uint8_t *buffer /* superblock->blockSize */, 
		FSNode *existingNode = nullptr, DirectoryEntryReference *outReference = nullptr) {
	// TODO Return EsError.
	Volume *volume = parent->volume;
	Superblock *superblock = &volume->superblock;
	EsError error; 

	if (type != ES_NODE_DIRECTORY && type != ES_NODE_FILE) {
		return false;
	}

	AttributeDirectory *directoryAttribute = (AttributeDirectory *) FindAttribute(&parent->entry, ESFS_ATTRIBUTE_DIRECTORY);

	// Resize the directory so that it can fit another directory entry.

	if (!(directoryAttribute->childNodes % superblock->directoryEntriesPerBlock)) {
		uint64_t newSize = parent->entry.fileSize + superblock->blockSize;
		ESFS_CHECK(newSize == ResizeInternal(parent, newSize, &error), "Create - Could not resize directory.");
	}

	DirectoryEntry *entry = nullptr;
	size_t newFilenameSize = ((nameLength + ESFS_FILENAME_HEADER_SIZE - 1) & ~7) + 8; // Size of name + size of header, rounded up to the nearest 8 bytes.

	if (!existingNode) {
		// Create the directory entry.

		entry = (DirectoryEntry *) buffer; // NOTE This must use the provided buffer; see Create.
		EsMemoryZero(entry, sizeof(DirectoryEntry));

		EsMemoryCopy(entry->signature, ESFS_DIRECTORY_ENTRY_SIGNATURE, 8);

		{
			KMutexAcquire(&volume->nextIdentifierMutex);

			entry->identifier = superblock->nextIdentifier;

			for (int i = 0; i < 16; i++) {
				superblock->nextIdentifier.d[i]++;
				if (superblock->nextIdentifier.d[i]) break;
			}

			KMutexRelease(&volume->nextIdentifierMutex);
		}

		entry->attributeOffset = ESFS_ATTRIBUTE_OFFSET;
		entry->nodeType = type == ES_NODE_DIRECTORY ? ESFS_NODE_TYPE_DIRECTORY : ESFS_NODE_TYPE_FILE; 
		entry->parent = parent->identifier;

		uint8_t *position = entry->attributes;

		if (entry->nodeType == ESFS_NODE_TYPE_DIRECTORY) {
			AttributeDirectory *directory = (AttributeDirectory *) position;
			directory->type = ESFS_ATTRIBUTE_DIRECTORY;
			directory->size = sizeof(AttributeDirectory);
			directory->indexRootBlock = 0;
			entry->attributeCount++;
			position += directory->size;
		}

		AttributeData *data = (AttributeData *) position;
		data->type = ESFS_ATTRIBUTE_DATA;
		data->size = sizeof(DirectoryEntry) - newFilenameSize - (position - (uint8_t *) entry);
		data->indirection = ESFS_INDIRECTION_DIRECT;
		data->dataOffset = ESFS_DATA_OFFSET;
		entry->attributeCount++;
		position += data->size;

		AttributeFilename *filename = (AttributeFilename *) position;
		filename->type = ESFS_ATTRIBUTE_FILENAME;
		filename->size = newFilenameSize;
		filename->length = nameLength;
		EsMemoryCopy(filename->filename, name, filename->length);
		entry->attributeCount++;
		position += filename->size;

		if (position - (uint8_t *) entry != sizeof(DirectoryEntry)) KernelPanic("EsFS::CreateInternal - Directory entry has incorrect size.\n");
	} else {
		// Update the existing directory entry.

		entry = &existingNode->entry;
		entry->parent = parent->identifier;

		if (!RenameInternal(existingNode, entry, name, nameLength)) {
			return false;
		}
	}

	entry->checksum = 0;
	entry->checksum = CalculateCRC32(entry, sizeof(DirectoryEntry), 0);
	if (!ValidateDirectoryEntry(volume, entry)) KernelPanic("EsFS::CreateInternal - Created directory entry is invalid.\n");

	// Write the directory entry.

	DirectoryEntryReference reference = {};
	ESFS_CHECK(ReadWrite(parent, directoryAttribute->childNodes * sizeof(DirectoryEntry), 
				sizeof(DirectoryEntry), (uint8_t *) entry, true, true, &reference), "Create - Could not update directory.");
	if (existingNode) existingNode->reference = reference;

	// Add the node into the index. 

	uint64_t newKey = CalculateCRC64(name, nameLength, 0);
	ESFS_CHECK(IndexAddKey(volume, newKey, reference, &directoryAttribute->indexRootBlock), "Create - Could not add file to index.");

	directoryAttribute->childNodes++;

	if (outReference) {
		*outReference = reference;
	}

	return true;
}

static EsError Move(KNode *_oldDirectory, KNode *_file, KNode *_newDirectory, const char *newName, size_t newNameLength) {
	FSNode *file = (FSNode *) _file->driverNode;
	FSNode *newDirectory = (FSNode *) _newDirectory->driverNode;
	FSNode *oldDirectory = (FSNode *) _oldDirectory->driverNode;

	Volume *volume = file->volume;
	Superblock *superblock = &volume->superblock;

	if (oldDirectory->type != ES_NODE_DIRECTORY || newDirectory->type != ES_NODE_DIRECTORY) KernelPanic("EsFS::Move - Incorrect node types.\n");

	file->entry.checksum = 0;
	file->entry.checksum = CalculateCRC32(&file->entry, sizeof(DirectoryEntry), 0);
	if (!ValidateDirectoryEntry(volume, &file->entry)) KernelPanic("EsFS::Move - Existing entry is invalid.\n");

	uint8_t *buffers = (uint8_t *) EsHeapAllocate(superblock->blockSize * 2, true, K_FIXED);
	if (!buffers) return ES_ERROR_INSUFFICIENT_RESOURCES;
	EsDefer(EsHeapFree(buffers, 0, K_FIXED));

	// Remove the node from the old directory.

	Sync(_oldDirectory, _file);
	ESFS_CHECK_ERROR(RemoveDirectoryEntry(file, buffers, oldDirectory, _oldDirectory), "Move - Could not remove old directory entry.");

	// Add the node to the new directory.

	DirectoryEntryReference reference = {};
	ESFS_CHECK_TO_ERROR(CreateInternal(newName, newNameLength, file->type, newDirectory, nullptr,
			file, &reference), "Move - Could not create new directory entry.", ES_ERROR_UNKNOWN);
	FSNodeUpdateDriverData(_file, &reference);

	return ES_SUCCESS;
}

static void Close(KNode *node) {
	EsHeapFree(node->driverNode, sizeof(FSNode), K_FIXED);
}

static EsError LoadInternal(Volume *volume, KNode *_node, DirectoryEntry *entry, DirectoryEntryReference reference) {
	FSNode *node = (FSNode *) EsHeapAllocate(sizeof(FSNode), true, K_FIXED);

	if (!node) {
		return ES_ERROR_INSUFFICIENT_RESOURCES;
	}

	_node->driverNode = node;
	node->reference = reference;
	node->volume = volume;
	node->identifier = entry->identifier;
	node->type = entry->nodeType == ESFS_NODE_TYPE_DIRECTORY ? ES_NODE_DIRECTORY : ES_NODE_FILE;
	EsMemoryCopy(&node->entry, entry, sizeof(DirectoryEntry));
	return ES_SUCCESS;
}

static EsError Load(KNode *_directory, KNode *_node, KNodeMetadata *, const void *entryData) {
	DirectoryEntryReference reference = *(DirectoryEntryReference *) entryData;
	FSNode *directory = (FSNode *) _directory->driverNode;
	Superblock *superblock = &directory->volume->superblock;

	uint8_t *blockBuffer = (uint8_t *) EsHeapAllocate(superblock->blockSize, false, K_FIXED);
	if (!blockBuffer) return ES_ERROR_INSUFFICIENT_RESOURCES;
	EsDefer(EsHeapFree(blockBuffer, 0, K_FIXED));

	if (!AccessBlock(directory->volume, reference.block, 1, blockBuffer, FS_BLOCK_ACCESS_CACHED, K_ACCESS_READ)) {
		KernelLog(LOG_ERROR, "EsFS", "drive access failure", "Load - Could not load directory entry.\n");
		return ES_ERROR_UNKNOWN;
	}

	DirectoryEntry *entry = (DirectoryEntry *) (blockBuffer + reference.offsetIntoBlock);

	if (!ValidateDirectoryEntry(directory->volume, entry)) {
		return ES_ERROR_CORRUPT_DATA;
	}

	if ((entry->nodeType == ESFS_NODE_TYPE_DIRECTORY && !FindAttribute(entry, ESFS_ATTRIBUTE_DIRECTORY))
			|| (entry->nodeType == ESFS_NODE_TYPE_FILE && !FindAttribute(entry, ESFS_ATTRIBUTE_DATA))) {
		KernelLog(LOG_ERROR, "EsFS", "damaged file system", "Load - Node is missing attribute.\n");
		return ES_ERROR_CORRUPT_DATA;
	}

	return LoadInternal(directory->volume, _node, entry, reference);
}

static EsError Create(const char *name, size_t nameLength, EsNodeType type, KNode *_parent, KNode *node, void *driverData) {
	FSNode *parent = (FSNode *) _parent->driverNode;
	if (!parent) return ES_ERROR_UNKNOWN;
	Volume *volume = parent->volume;
	Superblock *superblock = &volume->superblock;

	uint8_t *buffer = (uint8_t *) EsHeapAllocate(superblock->blockSize, true, K_FIXED);
	if (!buffer) return ES_ERROR_INSUFFICIENT_RESOURCES;
	EsDefer(EsHeapFree(buffer, 0, K_FIXED));

	DirectoryEntryReference reference = {};

	if (!CreateInternal(name, nameLength, type, parent, buffer, nullptr, &reference)) {
		return ES_ERROR_UNKNOWN;
	}

	EsMemoryCopy(driverData, &reference, sizeof(DirectoryEntryReference));
	return LoadInternal(volume, node, (DirectoryEntry *) buffer, reference);
}

static EsError Scan(const char *name, size_t nameLength, KNode *_directory) {
	// EsPrint("Scan: %s, %x\n", offsetToName + nameLength, name, _directory);

	DirectoryEntryReference reference = {};
	FSNode *directory = (FSNode *) _directory->driverNode;
	if (directory->corrupt) return ES_ERROR_CORRUPT_DATA;
	Volume *volume = directory->volume;
	Superblock *superblock = &volume->superblock;
	uint8_t *blockBuffer = (uint8_t *) EsHeapAllocate(superblock->blockSize, false, K_FIXED);
	if (!blockBuffer) return ES_ERROR_INSUFFICIENT_RESOURCES;
	EsDefer(EsHeapFree(blockBuffer, 0, K_FIXED));

	AttributeDirectory *attributeDirectory = (AttributeDirectory *) FindAttribute(&directory->entry, ESFS_ATTRIBUTE_DIRECTORY);
	EsError error = FindDirectoryEntryReferenceFromIndex(volume, blockBuffer, &reference, name, nameLength, attributeDirectory->indexRootBlock);

	if (error != ES_SUCCESS) {
		// EsPrint("\tCould not find in directory. (%d - %s)\n", error, nameLength, name);
		return error;
	}

	// EsPrint("\t%d/%d\n", reference.block, reference.offsetIntoBlock);

	if (!AccessBlock(volume, reference.block, 1, blockBuffer, FS_BLOCK_ACCESS_CACHED, K_ACCESS_READ)) {
		KernelLog(LOG_ERROR, "EsFS", "drive access failure", "Scan - Could not load directory entry.\n");
		return ES_ERROR_UNKNOWN;
	}

	DirectoryEntry *entry = (DirectoryEntry *) (blockBuffer + reference.offsetIntoBlock);
	if (!ValidateDirectoryEntry(volume, entry)) return ES_ERROR_CORRUPT_DATA;

	if ((entry->nodeType == ESFS_NODE_TYPE_DIRECTORY && !FindAttribute(entry, ESFS_ATTRIBUTE_DIRECTORY))
			|| (entry->nodeType == ESFS_NODE_TYPE_FILE && !FindAttribute(entry, ESFS_ATTRIBUTE_DATA))) {
		KernelLog(LOG_ERROR, "EsFS", "damaged file system", "Scan - Node is missing attribute.\n");
		return ES_ERROR_CORRUPT_DATA;
	}

	KNodeMetadata metadata = {};

	if (entry->nodeType == ESFS_NODE_TYPE_DIRECTORY) {
		AttributeDirectory *directoryAttribute = (AttributeDirectory *) FindAttribute(entry, ESFS_ATTRIBUTE_DIRECTORY);

		if (directoryAttribute) {
			metadata.directoryChildren = directoryAttribute->childNodes;
			metadata.totalSize = directoryAttribute->totalSize;
		}
	} else {
		metadata.totalSize = entry->fileSize;
	}

	metadata.type = entry->nodeType == ESFS_NODE_TYPE_DIRECTORY ? ES_NODE_DIRECTORY : ES_NODE_FILE;

	KNode *_node;
	error = FSDirectoryEntryFound(_directory, &metadata, &reference, name, nameLength, false, &_node);

	if (error != ES_SUCCESS) {
		return error;
	}

	error = LoadInternal(volume, _node, entry, reference);
	FSNodeScanAndLoadComplete(_node, error == ES_SUCCESS);
	return error;
}

static bool Mount(Volume *volume, EsFileOffsetDifference *rootDirectoryChildren) {
	// TODO Return EsError.
	// Load the superblock.

	Superblock *superblock = &volume->superblock;

	if (ES_SUCCESS != volume->Access(ESFS_BOOT_SUPER_BLOCK_SIZE, ESFS_BOOT_SUPER_BLOCK_SIZE, K_ACCESS_READ, (uint8_t *) superblock, ES_FLAGS_DEFAULT)) {
		KernelLog(LOG_ERROR, "EsFS", "drive access failure", "Mount - Could not read superblock.\n");
		return false;
	}

	if (volume->block->information.readOnly) {
		volume->readOnly = true;
	}

	// Check the superblock is valid.

	uint32_t checksum = volume->superblock.checksum;
	volume->superblock.checksum = 0;
	uint32_t calculated = CalculateCRC32(&volume->superblock, sizeof(Superblock), 0);
	ESFS_CHECK_FATAL(checksum == calculated, "Invalid superblock checksum.");

	ESFS_CHECK_FATAL(0 == EsMemoryCompare(volume->superblock.signature, ESFS_SIGNATURE_STRING, 16), "Invalid superblock signature.");
	ESFS_CHECK_FATAL(volume->superblock.requiredReadVersion <= ESFS_DRIVER_VERSION, "Incompatible file system version.");
	ESFS_CHECK_FATAL(superblock->blockSize >= 1024 && superblock->blockSize <= 16384 && (superblock->blockSize % volume->block->information.sectorSize) == 0, "Invalid block size.");
	ESFS_CHECK_FATAL(superblock->blockCount * superblock->blockSize / volume->block->information.sectorSize <= volume->block->information.sectorCount, "More blocks than drive.");
	ESFS_CHECK_FATAL(superblock->blocksUsed <= superblock->blockCount, "More blocks used than exist.");
	ESFS_CHECK_FATAL(superblock->blocksPerGroup <= 65536 && superblock->blocksPerGroup < superblock->blockCount && superblock->blocksPerGroup >= 1024, "Invalid block group size.");
	ESFS_CHECK_FATAL((superblock->groupCount - 1) * superblock->blocksPerGroup <= superblock->blockCount, "Invalid number of block groups.");
	ESFS_CHECK_FATAL(superblock->blocksPerGroupBlockBitmap < superblock->blocksPerGroup, "Invalid number of blocks per group block bitmap.");
	ESFS_CHECK_FATAL(superblock->gdtFirstBlock < superblock->blockCount, "First GDT block not within volume.\n");
	ESFS_CHECK_FATAL(superblock->directoryEntriesPerBlock <= superblock->blockSize / sizeof(DirectoryEntry), "Invalid number of directory entries per block.");
	ESFS_CHECK_FATAL(superblock->root.block < superblock->blockCount && superblock->root.offsetIntoBlock < superblock->blockSize, "Invalid root directory position in volume.\n");
	ESFS_CHECK_READ_ONLY(!superblock->nextIdentifier.d[15], "Too many nodes created and deleted.");
	ESFS_CHECK_READ_ONLY(superblock->requiredWriteVersion <= ESFS_DRIVER_VERSION, "Outdated file system version.");

	// ESFS_CHECK_READ_ONLY(!superblock->mounted, "Volume already mounted.");

	if (superblock->mounted) {
		KernelLog(LOG_ERROR, "EsFS", "volume already mounted", 
				"Superblock indicates that the volume was either unmounted incorrectly, or is mounted by another driver instance.\n");
	}

	if (!volume->readOnly) {
		superblock->mounted = true;
		superblock->checksum = 0;
		superblock->checksum = CalculateCRC32(superblock, sizeof(Superblock), 0);
		ESFS_CHECK_ERROR_READ_ONLY(volume->Access(ESFS_BOOT_SUPER_BLOCK_SIZE, ESFS_BOOT_SUPER_BLOCK_SIZE, 
					K_ACCESS_WRITE, (uint8_t *) superblock, ES_FLAGS_DEFAULT), "Could not mark volume as mounted.");
	}

	// Load the group descriptor table.

	{
		volume->groupDescriptorTable = (GroupDescriptor *) EsHeapAllocate((superblock->groupCount * sizeof(GroupDescriptor) + superblock->blockSize - 1), false, K_FIXED);

		if (!AccessBlock(volume, superblock->gdtFirstBlock, (superblock->groupCount * sizeof(GroupDescriptor) + superblock->blockSize - 1) / superblock->blockSize, 
					volume->groupDescriptorTable, FS_BLOCK_ACCESS_CACHED, K_ACCESS_READ)) {
			EsHeapFree(volume->groupDescriptorTable, 0, K_FIXED);
			ESFS_CHECK_FATAL(false, "Could not read group descriptor table.");
		}
	}

	// Load the root directory.

	{
		uint8_t *blockBuffer = (uint8_t *) EsHeapAllocate(superblock->blockSize, false, K_FIXED);
		EsDefer(if (blockBuffer) EsHeapFree(blockBuffer, superblock->blockSize, K_FIXED));
		FSNode *node = nullptr;

		if (!blockBuffer) {
			KernelLog(LOG_ERROR, "EsFS", "allocation failure", "Mount - Could not allocate block buffer.\n");
			goto failure;
		}

		{
			DirectoryEntryReference rootReference = superblock->root;

			if (!AccessBlock(volume, rootReference.block, 1, blockBuffer, FS_BLOCK_ACCESS_CACHED, K_ACCESS_READ)) {
				KernelLog(LOG_ERROR, "EsFS", "drive access failure", "Mount - Could not load root directory.\n");
				goto failure;
			}

			DirectoryEntry *entry = (DirectoryEntry *) (blockBuffer + rootReference.offsetIntoBlock);
			if (!ValidateDirectoryEntry(volume, entry)) goto failure;
			AttributeDirectory *directory = (AttributeDirectory *) FindAttribute(entry, ESFS_ATTRIBUTE_DIRECTORY);

			if (!directory || !FindAttribute(entry, ESFS_ATTRIBUTE_DATA)) {
				KernelLog(LOG_ERROR, "EsFS", "damaged file system", "Mount - Root directory is missing attributes.\n");
				goto failure;
			}

			volume->root = node = (FSNode *) EsHeapAllocate(sizeof(FSNode), true, K_FIXED);
			node->volume = volume;
			node->reference = rootReference;
			node->identifier = entry->identifier;
			node->type = ES_NODE_DIRECTORY;
			*rootDirectoryChildren = directory->childNodes;
			EsMemoryCopy(&node->entry, entry, sizeof(DirectoryEntry));

			goto success;
		}

		failure:;
		if (node) EsHeapFree(node, sizeof(FSNode), K_FIXED);
		return false;
	}

	success:;
	return true;
}

static void Unmount(KFileSystem *fileSystem) {
	Volume *volume = (Volume *) fileSystem;
	Superblock *superblock = &volume->superblock;

	if (!volume->readOnly) {
		AccessBlock(volume, superblock->gdtFirstBlock, (superblock->groupCount * sizeof(GroupDescriptor) + superblock->blockSize - 1) / superblock->blockSize, 
				volume->groupDescriptorTable, FS_BLOCK_ACCESS_CACHED, K_ACCESS_WRITE);

		superblock->mounted = false;
		superblock->checksum = 0;
		superblock->checksum = CalculateCRC32(superblock, sizeof(Superblock), 0);
		volume->Access(ESFS_BOOT_SUPER_BLOCK_SIZE, ESFS_BOOT_SUPER_BLOCK_SIZE, K_ACCESS_WRITE, 
				(uint8_t *) superblock, ES_FLAGS_DEFAULT);
	}

	EsHeapFree(volume->groupDescriptorTable, 0, K_FIXED);
}

static void Register(KDevice *_parent) {
	Volume *volume = (Volume *) KDeviceCreate("EssenceFS", _parent, sizeof(Volume));

	if (!volume || !FSFileSystemInitialise(volume)) {
		KernelLog(LOG_ERROR, "EsFS", "allocation failure", "Register - Could not allocate file system.\n");
		return;
	}

	EsFileOffsetDifference rootDirectoryChildren;

	if (!Mount(volume, &rootDirectoryChildren)) {
		KernelLog(LOG_ERROR, "EsFS", "mount error", "Register - Could not mount EsFS volume.\n");
		KDeviceDestroy(volume);
		return;
	}

	volume->spaceUsed = volume->superblock.blocksUsed * volume->superblock.blockSize;
	volume->spaceTotal = volume->superblock.blockCount * volume->superblock.blockSize;
	volume->identifier = volume->superblock.identifier;

	volume->read = Read;
	volume->scan = Scan;
	volume->load = Load;
	volume->enumerate = Enumerate;
	volume->unmount = Unmount;
	volume->close = Close;

	if (!volume->readOnly) {
		volume->write = Write;
		volume->sync = Sync;
		volume->resize = Resize;
		volume->create = Create;
		volume->remove = Remove;
		volume->move = Move;
	}

	volume->superblock.volumeName[ESFS_MAXIMUM_VOLUME_NAME_LENGTH - 1] = 0;
	volume->nameBytes = EsCStringLength(volume->superblock.volumeName);
	EsMemoryCopy(volume->name, volume->superblock.volumeName, volume->nameBytes);

	volume->rootDirectory->driverNode = volume->root;
	volume->rootDirectoryInitialChildren = rootDirectoryChildren;
	volume->directoryEntryDataBytes = sizeof(DirectoryEntryReference);
	volume->nodeDataBytes = sizeof(FSNode);

	FSRegisterBootFileSystem(volume, volume->superblock.osInstallation);
}

KDriver driverEssenceFS = {
	.attach = Register,
};
