// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

// TODO Features:
// 	- Handling errors creating files (prevent further file system operations).
// 	- Limiting the size of the directory/node cache.
//
// TODO Permissions:
// 	- Prevent modifications to directories without write permission.
// 	- Prevent launching executables without read permission.
//
// TODO Drivers:
// 	- Get NTFS driver working again.
//
// TODO Allocate nodes/directory entries from arenas?
// TODO Check that the MODIFIED tracking is correct.

#ifndef IMPLEMENTATION

#define NODE_MAX_ACCESSORS (16777216)

// KNode flags:
#define NODE_HAS_EXCLUSIVE_WRITER (1 << 0)
#define NODE_ENUMERATED_ALL_DIRECTORY_ENTRIES (1 << 1)
#define NODE_CREATED_ON_FILE_SYSTEM (1 << 2)
#define NODE_DELETED (1 << 3)
#define NODE_MODIFIED (1 << 4)
#define NODE_IN_CACHE_LIST (1 << 5) // Node has no handles and no directory entries, so it can be freed.

// Modes for opening a node handle.
#define FS_NODE_OPEN_HANDLE_STANDARD            (0)
#define FS_NODE_OPEN_HANDLE_FIRST               (1)
#define FS_NODE_OPEN_HANDLE_DIRECTORY_TEMPORARY (2)

struct FSDirectoryEntry : KNodeMetadata {
	MMObjectCacheItem cacheItem;
	AVLItem<FSDirectoryEntry> item; // item.key.longKey contains the entry's name.
	struct FSDirectory *parent; // The directory containing this entry.
	KNode *volatile node; // nullptr if the node hasn't been loaded.
	char inlineName[16]; // Store the name of the entry inline if it is small enough.
	// Followed by driver data.
};

struct FSDirectory : KNode {
	AVLTree<FSDirectoryEntry> entries;
	size_t entryCount;
};

struct FSFile : KNode {
	int32_t countWrite /* negative indicates non-shared readers */, blockResize;
	EsFileOffset fsFileSize; // Files are lazily resized; this is the size the file system thinks the file is.
	EsFileOffset fsZeroAfter; // This is the smallest size the file has reached without telling the file system.
	CCSpace cache;
	KWriterLock resizeLock; // Take exclusive for resizing or flushing.
};

struct KDMABuffer {
	uintptr_t virtualAddress;
	size_t totalByteCount;
	uintptr_t offsetBytes;
};

EsError FSNodeOpenHandle(KNode *node, uint32_t flags, uint8_t mode);
void FSNodeCloseHandle(KNode *node, uint32_t flags);
EsError FSNodeDelete(KNode *node);
EsError FSNodeMove(KNode *node, KNode *destination, const char *newName, size_t nameNameBytes);
EsError FSFileResize(KNode *node, EsFileOffset newSizeBytes);
ptrdiff_t FSDirectoryEnumerateChildren(KNode *node, K_USER_BUFFER EsDirectoryChild *buffer, size_t bufferSize);
EsError FSFileControl(KNode *node, uint32_t flags);
bool FSTrimCachedNode(MMObjectCache *);
bool FSTrimCachedDirectoryEntry(MMObjectCache *);
EsError FSBlockDeviceAccess(KBlockDeviceAccessRequest request);
void FSDetectFileSystem(KBlockDevice *device);

struct {
	KWriterLock fileSystemsLock;

	KFileSystem *bootFileSystem;
	KEvent foundBootFileSystemEvent;

	KSpinlock updateNodeHandles; // Also used for node/directory entry cache operations.

	bool shutdown;

	volatile uint64_t totalHandleCount;
	volatile uintptr_t fileSystemsUnmounting;
	KEvent fileSystemUnmounted;
} fs = {
	.fileSystemUnmounted = { .autoReset = true },
};

#else

EsFileOffset FSNodeGetTotalSize(KNode *node) {
	return node->directoryEntry->totalSize;
}

char *FSNodeGetName(KNode *node, size_t *bytes) {
	KWriterLockAssertLocked(&node->writerLock);
	*bytes = node->directoryEntry->item.key.longKeyBytes;
	return (char *) node->directoryEntry->item.key.longKey;
}

bool FSCheckPathForIllegalCharacters(const char *path, size_t pathBytes) {
	// Control ASCII characters are not allowed.

	for (uintptr_t i = 0; i < pathBytes; i++) {
		char c = path[i];

		if ((c >= 0x00 && c < 0x20) || c == 0x7F) {
			return false;
		}
	}

	// Invalid UTF-8 sequences are not allowed.
	// Surrogate characters are fine.
	// Overlong sequences are fine, except for ASCII characters.

	if (!EsUTF8IsValid(path, pathBytes)) {
		return false;
	}

	return true;
}

//////////////////////////////////////////
// Accessing files.
//////////////////////////////////////////

EsError FSReadIntoCache(CCSpace *fileCache, void *buffer, EsFileOffset offset, EsFileOffset count) {
	FSFile *node = EsContainerOf(FSFile, cache, fileCache);

	KWriterLockTake(&node->writerLock, K_LOCK_SHARED);
	EsDefer(KWriterLockReturn(&node->writerLock, K_LOCK_SHARED));

	if (node->flags & NODE_DELETED) {
		return ES_ERROR_NODE_DELETED;
	}

	if (offset > node->directoryEntry->totalSize) {
		KernelPanic("FSReadIntoCache - Read out of bounds in node %x.\n", node); 
	}

	if (node->fsZeroAfter < offset + count) {
		if (offset >= node->fsZeroAfter) {
			EsMemoryZero(buffer, count);
		} else {
			if (~node->flags & NODE_CREATED_ON_FILE_SYSTEM) {
				KernelPanic("FSReadIntoCache - Node %x has not been created on the file system.\n", node); 
			}

			size_t realBytes = node->fsZeroAfter - offset, fakeBytes = count - realBytes;
			EsMemoryZero((uint8_t *) buffer + realBytes, fakeBytes);
			count = node->fileSystem->read(node, buffer, offset, realBytes);
		}
	} else {
		if (~node->flags & NODE_CREATED_ON_FILE_SYSTEM) {
			KernelPanic("FSReadIntoCache - Node %x has not been created on the file system.\n", node); 
		}

		count = node->fileSystem->read(node, buffer, offset, count);

		if (ES_CHECK_ERROR(count)) {
			node->error = count;
		}
	}

	return ES_CHECK_ERROR(count) ? count : ES_SUCCESS;
}

EsError FSFileCreateAndResizeOnFileSystem(FSFile *node, EsFileOffset fileSize) {
	KWriterLockAssertExclusive(&node->writerLock);

	FSDirectoryEntry *entry = node->directoryEntry;

	if (node->flags & NODE_DELETED) {
		return ES_ERROR_NODE_DELETED;
	}

	if (~node->flags & NODE_CREATED_ON_FILE_SYSTEM) {
		KWriterLockTake(&entry->parent->writerLock, K_LOCK_EXCLUSIVE);

		EsError error = ES_SUCCESS;

		if (~entry->parent->flags & NODE_CREATED_ON_FILE_SYSTEM) {
			// TODO Get the node error mark?
			error = ES_ERROR_UNKNOWN;
		}

		if (error == ES_SUCCESS) {
			error = node->fileSystem->create((const char *) entry->item.key.longKey, entry->item.key.longKeyBytes, 
					ES_NODE_FILE, entry->parent, node, entry + 1);
		}

		if (error == ES_SUCCESS) {
			__sync_fetch_and_or(&node->flags, NODE_CREATED_ON_FILE_SYSTEM);
		} else {
			// TODO Mark the node with an error.
		}

		KWriterLockReturn(&entry->parent->writerLock, K_LOCK_EXCLUSIVE);

		if (error != ES_SUCCESS) {
			return error;
		}
	}

	if (node->fsFileSize != fileSize || node->fsZeroAfter != fileSize) {
		// Resize the file on the file system to match the cache size.
		EsError error = ES_ERROR_UNKNOWN;
		KWriterLockTake(&entry->parent->writerLock, K_LOCK_EXCLUSIVE);

		if (node->fsZeroAfter != node->fsFileSize) {
			// TODO Combined truncate-and-grow file operation.
			node->fsFileSize = node->fileSystem->resize(node, node->fsZeroAfter, &error);
		}

		// TODO Hint about where to zero upto - since we'll likely be about to write over the sectors!
		node->fsFileSize = node->fileSystem->resize(node, fileSize, &error);
		KWriterLockReturn(&entry->parent->writerLock, K_LOCK_EXCLUSIVE);

		if (node->fsFileSize != fileSize) {
			return ES_ERROR_COULD_NOT_RESIZE_FILE;
		}

		node->fsZeroAfter = fileSize;
	}

	return ES_SUCCESS;
}

EsError FSWriteFromCache(CCSpace *fileCache, const void *buffer, EsFileOffset offset, EsFileOffset count) {
	FSFile *node = EsContainerOf(FSFile, cache, fileCache);

	KWriterLockTake(&node->writerLock, K_LOCK_EXCLUSIVE);
	EsDefer(KWriterLockReturn(&node->writerLock, K_LOCK_EXCLUSIVE));

	FSDirectoryEntry *entry = node->directoryEntry;
	volatile EsFileOffset fileSize = entry->totalSize;

	EsError error = FSFileCreateAndResizeOnFileSystem(node, fileSize);

	if (error != ES_SUCCESS) {
		return error;
	}

	if (offset > fileSize) {
		KernelPanic("VFSWriteFromCache - Write out of bounds in node %x.\n", node); 
	}

	if (count > fileSize - offset) {
		count = fileSize - offset;
	}

	if (node->flags & NODE_DELETED) {
		return ES_ERROR_NODE_DELETED;
	}

	count = node->fileSystem->write(node, buffer, offset, count);

	if (ES_CHECK_ERROR(count)) {
		node->error = count;
	}

	return ES_CHECK_ERROR(count) ? count : ES_SUCCESS;
}

const CCSpaceCallbacks fsFileCacheCallbacks = {
	.readInto = FSReadIntoCache,
	.writeFrom = FSWriteFromCache,
};

ptrdiff_t FSFileReadSync(KNode *node, K_USER_BUFFER void *buffer, EsFileOffset offset, EsFileOffset bytes, uint32_t accessFlags) {
	if (fs.shutdown) KernelPanic("FSFileReadSync - Attempting to read from a file after FSShutdown called.\n");

	FSFile *file = (FSFile *) node;
	KWriterLockTake(&file->resizeLock, K_LOCK_SHARED);
	EsDefer(KWriterLockReturn(&file->resizeLock, K_LOCK_SHARED));

	if (offset > file->directoryEntry->totalSize) return ES_ERROR_ACCESS_NOT_WITHIN_FILE_BOUNDS;
	if (bytes > file->directoryEntry->totalSize - offset) bytes = file->directoryEntry->totalSize - offset;
	if (!bytes) return 0;

	EsError error = CCSpaceAccess(&file->cache, buffer, offset, bytes, 
			CC_ACCESS_READ | ((accessFlags & FS_FILE_ACCESS_USER_BUFFER_MAPPED) ? CC_ACCESS_USER_BUFFER_MAPPED : 0));
	return error == ES_SUCCESS ? bytes : error;
}

void _FSFileResize(FSFile *file, EsFileOffset newSize) {
	KWriterLockAssertExclusive(&file->resizeLock);

	EsFileOffsetDifference delta = newSize - file->directoryEntry->totalSize;

	if (!delta) {
		return;
	}

	if (delta < 0) {
		// Truncate the space first, so that any pending write-backs past this point can complete.
		CCSpaceTruncate(&file->cache, newSize);
	}

	// No more writes-back can be issued past newSize until the resize lock is returned. Since:
	// - CCSpaceTruncate waits for any queued writes past newSize to finish.
	// - FSFileWriteSync waits for shared access on the resize lock before it can queue any more writes.
	// This means we are safe to possible decrease sizing information.

	if (newSize < file->fsZeroAfter) {
		file->fsZeroAfter = newSize;
	}

	// Take the move lock before we write to the directoryEntry,
	// because FSNodeMove needs to also update ancestors with the file's size when it is moved.
	KMutexAcquire(&file->fileSystem->moveMutex); 

	// We'll get the filesystem to resize the file during write-back.
	file->directoryEntry->totalSize = newSize;

	KNode *ancestor = file->directoryEntry->parent;

	while (ancestor) {
		__sync_fetch_and_or(&ancestor->flags, NODE_MODIFIED);
		ancestor->directoryEntry->totalSize += delta;
		ancestor = ancestor->directoryEntry->parent;
	}

	__sync_fetch_and_or(&file->flags, NODE_MODIFIED);

	KMutexRelease(&file->fileSystem->moveMutex);
}

EsError FSFileResize(KNode *node, EsFileOffset newSize) {
	if (fs.shutdown) KernelPanic("FSFileResize - Attempting to resize a file after FSShutdown called.\n");

	if (newSize > (EsFileOffset) 1 << 60) {
		return ES_ERROR_INSUFFICIENT_RESOURCES;
	}

	if (node->directoryEntry->type != ES_NODE_FILE) {
		KernelPanic("FSFileResize - Node %x is not a file.\n", node);
	}

	FSFile *file = (FSFile *) node;
	EsError error = ES_SUCCESS;
	KWriterLockTake(&file->resizeLock, K_LOCK_EXCLUSIVE);

	if (file->blockResize) {
		error = ES_ERROR_FILE_IN_EXCLUSIVE_USE;
	} else if (!file->fileSystem->resize) {
		error = ES_ERROR_FILE_ON_READ_ONLY_VOLUME;
	} else {
		_FSFileResize(file, newSize);
	}

	KWriterLockReturn(&file->resizeLock, K_LOCK_EXCLUSIVE);
	return error;
}

ptrdiff_t FSFileWriteSync(KNode *node, K_USER_BUFFER const void *buffer, EsFileOffset offset, EsFileOffset bytes, uint32_t flags) {
	if (fs.shutdown) KernelPanic("FSFileWriteSync - Attempting to write to a file after FSShutdown called.\n");

	if (offset + bytes > node->directoryEntry->totalSize) {
		if (ES_SUCCESS != FSFileResize(node, offset + bytes)) {
			return ES_ERROR_COULD_NOT_RESIZE_FILE;
		}
	}

	FSFile *file = (FSFile *) node;
	KWriterLockTake(&file->resizeLock, K_LOCK_SHARED);
	EsDefer(KWriterLockReturn(&file->resizeLock, K_LOCK_SHARED));

	if (!file->fileSystem->write) return ES_ERROR_FILE_ON_READ_ONLY_VOLUME;
	if (offset > file->directoryEntry->totalSize) return ES_ERROR_ACCESS_NOT_WITHIN_FILE_BOUNDS;
	if (bytes > file->directoryEntry->totalSize - offset) bytes = file->directoryEntry->totalSize - offset;
	if (!bytes) return 0;

	EsError error = CCSpaceAccess(&file->cache, (void *) buffer, offset, bytes, 
			CC_ACCESS_WRITE | ((flags & FS_FILE_ACCESS_USER_BUFFER_MAPPED) ? CC_ACCESS_USER_BUFFER_MAPPED : 0));
	__sync_fetch_and_or(&file->flags, NODE_MODIFIED);
	return error == ES_SUCCESS ? bytes : error;
}

EsError FSFileControl(KNode *node, uint32_t flags) {
	FSFile *file = (FSFile *) node;

	if (flags & ES_FILE_CONTROL_FLUSH) {
		KWriterLockTake(&file->resizeLock, K_LOCK_EXCLUSIVE);
		EsDefer(KWriterLockReturn(&file->resizeLock, K_LOCK_EXCLUSIVE));

		CCSpaceFlush(&file->cache);

		KWriterLockTake(&file->writerLock, K_LOCK_EXCLUSIVE);
		EsDefer(KWriterLockReturn(&file->writerLock, K_LOCK_EXCLUSIVE));

		__sync_fetch_and_and(&file->flags, ~NODE_MODIFIED);

		EsError error = FSFileCreateAndResizeOnFileSystem(file, file->directoryEntry->totalSize);
		if (error != ES_SUCCESS) return error;

		if (file->fileSystem->sync) {
			// TODO Should we also sync the parent?
			FSDirectory *parent = file->directoryEntry->parent;

			if (parent) KWriterLockTake(&parent->writerLock, K_LOCK_EXCLUSIVE);
			file->fileSystem->sync(parent, file);
			if (parent) KWriterLockReturn(&parent->writerLock, K_LOCK_EXCLUSIVE);
		}

		if (file->error != ES_SUCCESS) {
			EsError error = file->error;
			file->error = ES_SUCCESS;
			return error;
		}
	}

	return ES_SUCCESS;
}

//////////////////////////////////////////
// Directories.
//////////////////////////////////////////

EsError FSNodeDelete(KNode *node) {
	if (fs.shutdown) KernelPanic("FSNodeDelete - Attempting to delete a file after FSShutdown called.\n");

	EsError error = ES_SUCCESS;

	FSDirectoryEntry *entry = node->directoryEntry;
	FSDirectory *parent = entry->parent;
	FSFile *file = entry->type == ES_NODE_FILE ? (FSFile *) node : nullptr;

	if (!parent) return ES_ERROR_PERMISSION_NOT_GRANTED;

	// Open a handle to the parent, so that if its directory entry count drops to zero after the operation,
	// it is put on the node cache list when the handle is closed.

	if (ES_SUCCESS != FSNodeOpenHandle(parent, ES_FLAGS_DEFAULT, FS_NODE_OPEN_HANDLE_DIRECTORY_TEMPORARY)) {
		return ES_ERROR_INSUFFICIENT_RESOURCES;
	}

	EsDefer(FSNodeCloseHandle(parent, ES_FLAGS_DEFAULT));

	if (file) {
		KWriterLockTake(&file->resizeLock, K_LOCK_EXCLUSIVE);

		if (file->blockResize) {
			KWriterLockReturn(&file->resizeLock, K_LOCK_EXCLUSIVE);
			return ES_ERROR_FILE_IN_EXCLUSIVE_USE;
		}

		_FSFileResize(file, 0);
	}

	KWriterLockTake(&node->writerLock, K_LOCK_EXCLUSIVE);
	KWriterLockTake(&parent->writerLock, K_LOCK_EXCLUSIVE);

	if (node->flags & NODE_DELETED) {
		error = ES_ERROR_NODE_DELETED;
	} else if (entry->type == ES_NODE_DIRECTORY && ((FSDirectory *) node)->entryCount) {
		error = ES_ERROR_DIRECTORY_NOT_EMPTY;
	} else if (!node->fileSystem->remove) {
		error = ES_ERROR_FILE_ON_READ_ONLY_VOLUME;
	} else if (node->flags & NODE_CREATED_ON_FILE_SYSTEM) {
		error = node->fileSystem->remove(parent, node);
	}

	if (error == ES_SUCCESS) {
		__sync_fetch_and_or(&node->flags, NODE_DELETED);
		TreeRemove(&parent->entries, &entry->item);
		parent->entryCount--;
	}

	__sync_fetch_and_or(&parent->flags, NODE_MODIFIED);
	KWriterLockReturn(&parent->writerLock, K_LOCK_EXCLUSIVE);
	KWriterLockReturn(&node->writerLock, K_LOCK_EXCLUSIVE);
	if (file) KWriterLockReturn(&file->resizeLock, K_LOCK_EXCLUSIVE);

	return error;
}

EsError FSNodeMove(KNode *node, KNode *_newParent, const char *newName, size_t newNameBytes) {
	if (fs.shutdown) KernelPanic("FSNodeMove - Attempting to move a file after FSShutdown called.\n");

	if (!FSCheckPathForIllegalCharacters(newName, newNameBytes)) {
		return ES_ERROR_ILLEGAL_PATH;
	}

	FSDirectoryEntry *entry = node->directoryEntry;
	FSDirectory *newParent = (FSDirectory *) _newParent;
	FSDirectory *oldParent = entry->parent;

	// Check the move is valid.

	if (newParent->directoryEntry->type != ES_NODE_DIRECTORY) {
		return ES_ERROR_TARGET_INVALID_TYPE;
	}

	if (!oldParent || oldParent->fileSystem != newParent->fileSystem || oldParent->fileSystem != node->fileSystem) {
		return ES_ERROR_VOLUME_MISMATCH;
	}

	if (!newNameBytes || newNameBytes > ES_MAX_DIRECTORY_CHILD_NAME_LENGTH) {
		return ES_ERROR_INVALID_NAME;
	}

	for (uintptr_t i = 0; i < newNameBytes; i++) {
		if (newName[i] == '/') {
			return ES_ERROR_INVALID_NAME;
		}
	}

	if (!node->fileSystem->move) {
		return ES_ERROR_FILE_ON_READ_ONLY_VOLUME;
	}

	// Open a handle to the parent, so that if its directory entry count drops to zero after the operation,
	// it is put on the node cache list when the handle is closed.

	if (ES_SUCCESS != FSNodeOpenHandle(oldParent, ES_FLAGS_DEFAULT, FS_NODE_OPEN_HANDLE_DIRECTORY_TEMPORARY)) {
		return ES_ERROR_INSUFFICIENT_RESOURCES;
	}

	EsDefer(FSNodeCloseHandle(oldParent, ES_FLAGS_DEFAULT));

	EsError error = ES_SUCCESS;
	bool alreadyExists = false;
	void *newKeyBuffer = nullptr;

	KWriterLock *locks[] = { &node->writerLock, &oldParent->writerLock, &newParent->writerLock };
	KWriterLockTakeMultiple(locks, oldParent == newParent ? 2 : 3, K_LOCK_EXCLUSIVE);

	KMutexAcquire(&node->fileSystem->moveMutex);
	EsDefer(KMutexRelease(&node->fileSystem->moveMutex));

	KNode *newAncestor = newParent, *oldAncestor;

	while (newAncestor) {
		if (newAncestor == node) {
			// We are trying to move this node into a folder within itself.
			error = ES_ERROR_TARGET_WITHIN_SOURCE;
			goto fail;
		}

		newAncestor = newAncestor->directoryEntry->parent;
	}

	if ((node->flags | newParent->flags) & NODE_DELETED) {
		error = ES_ERROR_NODE_DELETED;
		goto fail;
	}

	// Check a node with the same name doesn't already exist in the new directory.

	alreadyExists = TreeFind(&newParent->entries, MakeLongKey(newName, newNameBytes), TREE_SEARCH_EXACT);

	if (!alreadyExists && (~newParent->flags & NODE_ENUMERATED_ALL_DIRECTORY_ENTRIES)) {
		// The entry is not cached; load it from the file system.
		node->fileSystem->scan(newName, newNameBytes, newParent);
		alreadyExists = TreeFind(&newParent->entries, MakeLongKey(newName, newNameBytes), TREE_SEARCH_EXACT);
	}

	if (alreadyExists) {
		error = ES_ERROR_FILE_ALREADY_EXISTS;
		goto fail;
	}

	// Allocate a buffer for the new key before we try to do anything permanent...

	newKeyBuffer = nullptr;

	if (newNameBytes > sizeof(entry->inlineName)) {
		newKeyBuffer = EsHeapAllocate(newNameBytes, false, K_FIXED);

		if (!newKeyBuffer) {
			error = ES_ERROR_INSUFFICIENT_RESOURCES;
			goto fail;
		}
	}

	// Move the node on the file system, if it has been created.

	if (entry->node && (entry->node->flags & NODE_CREATED_ON_FILE_SYSTEM)) {
		error = node->fileSystem->move(oldParent, node, newParent, newName, newNameBytes);

		if (error != ES_SUCCESS) {
			goto fail;
		}
	}

	// Update the node's parent in our cache.

	entry->parent = newParent;
	
	TreeRemove(&oldParent->entries, &entry->item);

	entry->item.key.longKey = newKeyBuffer ?: entry->inlineName;
	EsMemoryCopy(entry->item.key.longKey, newName, newNameBytes);
	entry->item.key.longKeyBytes = newNameBytes;

	TreeInsert(&newParent->entries, &entry->item, entry, entry->item.key);

	oldParent->entryCount--;

	if (oldParent->directoryEntry->directoryChildren != ES_DIRECTORY_CHILDREN_UNKNOWN) {
		oldParent->directoryEntry->directoryChildren--;
	}

	newParent->entryCount++;

	if (newParent->directoryEntry->directoryChildren != ES_DIRECTORY_CHILDREN_UNKNOWN) {
		newParent->directoryEntry->directoryChildren++;
	}

	// Move the size of the node from the old to the new ancestors.

	oldAncestor = oldParent;

	while (oldAncestor) {
		oldAncestor->directoryEntry->totalSize -= entry->totalSize;
		oldAncestor = oldAncestor->directoryEntry->parent;
	}

	newAncestor = newParent;

	while (newAncestor) {
		newAncestor->directoryEntry->totalSize += entry->totalSize;
		newAncestor = newAncestor->directoryEntry->parent;
	}

	fail:;

	if (error != ES_SUCCESS) {
		if (newKeyBuffer) {
			EsHeapFree(newKeyBuffer, newNameBytes, K_FIXED);
		}
	}

	if (oldParent != newParent) {
		KWriterLockReturn(&oldParent->writerLock, K_LOCK_EXCLUSIVE);
	}

	__sync_fetch_and_or(&node->flags, NODE_MODIFIED);
	__sync_fetch_and_or(&newParent->flags, NODE_MODIFIED);
	__sync_fetch_and_or(&oldParent->flags, NODE_MODIFIED);

	KWriterLockReturn(&newParent->writerLock, K_LOCK_EXCLUSIVE);
	KWriterLockReturn(&node->writerLock, K_LOCK_EXCLUSIVE);

	return error;
}

void _FSDirectoryEnumerateChildrenVisit(AVLItem<FSDirectoryEntry> *item, K_USER_BUFFER EsDirectoryChild *buffer, size_t bufferSize, uintptr_t *position) {
	if (!item || *position == bufferSize) {
		return;
	}

	FSDirectoryEntry *entry = item->thisItem;
	EsDirectoryChild *output = buffer + *position;
	*position = *position + 1;

	if (entry->node && (entry->node->flags & NODE_DELETED)) {
		KernelPanic("_FSDirectoryEnumerateChildrenVisit - Deleted node %x found in directory tree.\n");
	}

	size_t nameBytes = entry->item.key.longKeyBytes > ES_MAX_DIRECTORY_CHILD_NAME_LENGTH ? ES_MAX_DIRECTORY_CHILD_NAME_LENGTH : entry->item.key.longKeyBytes;
	EsMemoryCopy(output->name, entry->item.key.longKey, nameBytes);
	output->type = entry->type;
	output->fileSize = entry->totalSize;
	output->directoryChildren = entry->directoryChildren;
	output->nameBytes = nameBytes;

	_FSDirectoryEnumerateChildrenVisit(item->children[0], buffer, bufferSize, position);
	_FSDirectoryEnumerateChildrenVisit(item->children[1], buffer, bufferSize, position);
}

ptrdiff_t FSDirectoryEnumerateChildren(KNode *node, K_USER_BUFFER EsDirectoryChild *buffer, size_t bufferSize) {
	// uint64_t start = ProcessorReadTimeStamp();

	if (node->directoryEntry->type != ES_NODE_DIRECTORY) {
		KernelPanic("FSDirectoryEnumerateChildren - Node %x is not a directory.\n", node);
	}

	FSDirectory *directory = (FSDirectory *) node;

	// I think it's safe to modify the user's buffer with this lock.
	KWriterLockTake(&directory->writerLock, K_LOCK_EXCLUSIVE);

	if (~directory->flags & NODE_ENUMERATED_ALL_DIRECTORY_ENTRIES) {
		EsError error = directory->fileSystem->enumerate(directory);

		if (error != ES_SUCCESS) {
			KWriterLockReturn(&directory->writerLock, K_LOCK_EXCLUSIVE);
			return error;
		}

		__sync_fetch_and_or(&directory->flags, NODE_ENUMERATED_ALL_DIRECTORY_ENTRIES);
		directory->directoryEntry->directoryChildren = directory->entryCount;
	}

	uintptr_t position = 0;
	_FSDirectoryEnumerateChildrenVisit(directory->entries.root, buffer, bufferSize, &position);

	KWriterLockReturn(&directory->writerLock, K_LOCK_EXCLUSIVE);

	// uint64_t end = ProcessorReadTimeStamp();
	// EsPrint("FSDirectoryEnumerateChildren took %dmcs for %d items.\n", (end - start) / KGetTimeStampTicksPerUs(), position);

	return position;
}

void FSNodeFree(KNode *node) {
	FSDirectoryEntry *entry = node->directoryEntry;

	if (entry->node != node) {
		KernelPanic("FSNodeFree - FSDirectoryEntry node mismatch for node %x.\n", node);
	} else if (node->flags & NODE_IN_CACHE_LIST) {
		KernelPanic("FSNodeFree - Node %x is in the cache list.\n", node);
	}

	if (entry->type == ES_NODE_FILE) {
		CCSpaceDestroy(&((FSFile *) node)->cache);
	} else if (entry->type == ES_NODE_DIRECTORY) {
		if (((FSDirectory *) node)->entries.root) {
			KernelPanic("FSNodeFree - Directory %x still had items in its tree.\n", node);
		}
	}

	if (node->driverNode) {
		node->fileSystem->close(node);
	}

	// EsPrint("Freeing node with name '%s'...\n", entry->item.key.longKeyBytes, entry->item.key.longKey);

	bool deleted = node->flags & NODE_DELETED;

	KFileSystem *fileSystem = node->fileSystem;
	EsHeapFree(node, entry->type == ES_NODE_DIRECTORY ? sizeof(FSDirectory) : sizeof(FSFile), K_FIXED);

	if (!deleted) {
		KSpinlockAcquire(&fs.updateNodeHandles);
		MMObjectCacheInsert(&fileSystem->cachedDirectoryEntries, &entry->cacheItem);
		entry->node = nullptr;
		entry->removingNodeFromCache = false;
		KSpinlockRelease(&fs.updateNodeHandles);
	} else {
		// The node has been deleted, and we're about to deallocate the directory entry anyway.
		// See FSNodeCloseHandle.
	}
}

void FSNodeScanAndLoadComplete(KNode *node, bool success) {
	if (success) {
		if (node->flags & NODE_IN_CACHE_LIST) {
			KernelPanic("FSNodeScanAndLoadComplete - Node %x is already in the cache list.\n", node);
		} else if (node->directoryEntry->type == ES_NODE_DIRECTORY && ((FSDirectory *) node)->entryCount) {
			KernelPanic("FSNodeScanAndLoadComplete - Node %x has entries.\n", node);
		}

		// The driver just scanned and loaded the node.
		// Put it in the cache list; this is similar to what's done in FSNodeCreate.
		// This is because we haven't opened any handles to the node yet,
		// so for correctness it must be on the cache list.
		// However, we do not expect it to be freed (although it would not be a problem if it were), 
		// since the parent's writer lock should be taken.
		MMObjectCacheInsert(&node->fileSystem->cachedNodes, &node->cacheItem);
		__sync_fetch_and_or(&node->flags, NODE_IN_CACHE_LIST);
	} else {
		FSNodeFree(node);
	}
}

void FSDirectoryEntryFree(FSDirectoryEntry *entry) {
	if (entry->cacheItem.previous || entry->cacheItem.next) {
		KernelPanic("FSDirectoryEntryFree - Entry %x is in cache.\n", entry);
#ifdef TREE_VALIDATE
	} else if (entry->item.tree) {
		KernelPanic("FSDirectoryEntryFree - Entry %x is in parent's tree.\n", entry);
#endif
	}

	// EsPrint("Freeing directory entry with name '%s'...\n", entry->item.key.longKeyBytes, entry->item.key.longKey);

	if (entry->item.key.longKey != entry->inlineName) {
		EsHeapFree((void *) entry->item.key.longKey, entry->item.key.longKeyBytes, K_FIXED);
	}

	EsHeapFree(entry, 0, K_FIXED);
}

EsError FSNodeCreate(FSDirectory *parent, const char *name, size_t nameBytes, EsNodeType type) {
	KWriterLockAssertExclusive(&parent->writerLock);
	KFileSystem *fileSystem = parent->fileSystem;

	if (!fileSystem->create) {
		// Read-only file system.
		return ES_ERROR_FILE_ON_READ_ONLY_VOLUME;
	}

	KNodeMetadata metadata = {};
	metadata.type = type;

	KNode *node;
	EsError error = FSDirectoryEntryFound(parent, &metadata, nullptr, name, nameBytes, false, &node);
	if (error != ES_SUCCESS) return error;

	if (parent->directoryEntry->directoryChildren != ES_DIRECTORY_CHILDREN_UNKNOWN) {
		parent->directoryEntry->directoryChildren++;
	}

	__sync_fetch_and_or(&parent->flags, NODE_MODIFIED);
	__sync_fetch_and_or(&node->flags, NODE_MODIFIED);
	
	// Only create directories immediately; files are created in FSWriteFromCache.

	if (type != ES_NODE_FILE) {
		error = fileSystem->create(name, nameBytes, type, parent, node, node->directoryEntry + 1);

		if (error == ES_SUCCESS) {
			__sync_fetch_and_or(&node->flags, NODE_CREATED_ON_FILE_SYSTEM);
		} else {
			// TODO Mark the node with an error.
		}
	}

	// Put the node onto the object cache list.
	// Since the parent directory is locked, it should stay around for long enough to be immediately found FSNodeTraverseLayer.
	if (node->flags & NODE_IN_CACHE_LIST) KernelPanic("FSNodeCreate - Node %x is already in the cache list.\n", node);
	MMObjectCacheInsert(&node->fileSystem->cachedNodes, &node->cacheItem);
	__sync_fetch_and_or(&node->flags, NODE_IN_CACHE_LIST);

	return ES_SUCCESS;
}

EsError FSDirectoryEntryAllocateNode(FSDirectoryEntry *entry, KFileSystem *fileSystem, bool createdOnFileSystem, bool inDirectoryEntryCache) {
	{
		KSpinlockAcquire(&fs.updateNodeHandles);
		EsDefer(KSpinlockRelease(&fs.updateNodeHandles));

		if (entry->removingThisFromCache) {
			if (!inDirectoryEntryCache) {
				KernelPanic("FSDirectoryEntryAllocateNode - Entry %x is being removed from the cache, "
						"but the caller did not expect it to have ever been in the cache.\n", entry);
			}

			return ES_ERROR_DIRECTORY_ENTRY_BEING_REMOVED;
		}

		if (inDirectoryEntryCache) {
			MMObjectCacheRemove(&fileSystem->cachedDirectoryEntries, &entry->cacheItem);
		}
	}

	KNode *node = (KNode *) EsHeapAllocate(entry->type == ES_NODE_DIRECTORY ? sizeof(FSDirectory) : sizeof(FSFile), true, K_FIXED);

	if (!node) {
		MMObjectCacheInsert(&fileSystem->cachedDirectoryEntries, &entry->cacheItem);
		return ES_ERROR_INSUFFICIENT_RESOURCES;
	}

	if (entry->type == ES_NODE_DIRECTORY) {
		FSDirectory *directory = (FSDirectory *) node;
		directory->entries.longKeys = true;

		if (!createdOnFileSystem) {
			// We just created the directory, so we've definitely got all the entries.
			directory->flags |= NODE_ENUMERATED_ALL_DIRECTORY_ENTRIES;
		}
	} else if (entry->type == ES_NODE_FILE) {
		FSFile *file = (FSFile *) node;
		file->fsFileSize = entry->totalSize;
		file->fsZeroAfter = entry->totalSize;
		file->cache.callbacks = &fsFileCacheCallbacks;

		if (!CCSpaceInitialise(&file->cache)) {
			MMObjectCacheInsert(&fileSystem->cachedDirectoryEntries, &entry->cacheItem);
			EsHeapFree(node, 0, K_FIXED);
			return ES_ERROR_INSUFFICIENT_RESOURCES;
		}
	}

	static uint64_t nextNodeID = 1;
	node->id = __sync_fetch_and_add(&nextNodeID, 1);

	// TODO On file systems that support it, use their stable unique ID for the node.
	// 	- What happens with delay-created files?
	// 	- This ID should be unique among all volumes.
	// 	- Maybe this should be a separate ID?

	if (createdOnFileSystem) {
		node->flags |= NODE_CREATED_ON_FILE_SYSTEM;
	}

	node->directoryEntry = entry;
	node->fileSystem = fileSystem;
	node->error = ES_SUCCESS;
	entry->node = node;
	return ES_SUCCESS;
}

EsError FSDirectoryEntryFound(KNode *_parent, KNodeMetadata *metadata, 
		const void *driverData, const void *name, size_t nameBytes,
		bool update, KNode **node) {
	FSDirectory *parent = (FSDirectory *) _parent;
	size_t driverDataBytes = parent->fileSystem->directoryEntryDataBytes;
	KWriterLockAssertExclusive(&parent->writerLock);

	AVLItem<FSDirectoryEntry> *existingEntry = TreeFind(&parent->entries, MakeLongKey(name, nameBytes), TREE_SEARCH_EXACT);

	if (existingEntry) {
		if (!driverData) {
			KernelPanic("FSDirectoryEntryFound - Directory entry '%s' in %x already exists, but no driverData is provided.\n",
					nameBytes, name, parent);
		}

		if (update) {
			EsMemoryCopy(existingEntry->thisItem + 1, driverData, driverDataBytes);
		}

		if (node) {
			if (!update) {
				// Only try to create a node for this directory entry if update is false.

				if (existingEntry->thisItem->node) {
					KernelPanic("FSDirectoryEntryFound - Entry exists and is created on file system.\n");
				}

				EsError error = FSDirectoryEntryAllocateNode(existingEntry->thisItem, parent->fileSystem, true, true);

				if (error != ES_SUCCESS) {
					return error;
				}
			}

			*node = existingEntry->thisItem->node;
		} 
		
		if (!node && !update && EsMemoryCompare(existingEntry->thisItem + 1, driverData, driverDataBytes)) {
			// NOTE This can be caused by a directory containing an entry with the same name multiple times.
			KernelLog(LOG_ERROR, "FS", "directory entry driverData changed", "FSDirectoryEntryFound - 'update' is false but driverData has changed.\n");
		}

		return ES_SUCCESS;
	} else if (update) {
		return ES_ERROR_FILE_DOES_NOT_EXIST;
	}

	FSDirectoryEntry *entry = (FSDirectoryEntry *) EsHeapAllocate(sizeof(FSDirectoryEntry) + driverDataBytes, true, K_FIXED);

	if (!entry) {
		return ES_ERROR_INSUFFICIENT_RESOURCES;
	}

	if (nameBytes > sizeof(entry->inlineName)) {
		entry->item.key.longKey = EsHeapAllocate(nameBytes, false, K_FIXED);

		if (!entry->item.key.longKey) {
			EsHeapFree(entry, sizeof(FSDirectoryEntry) + driverDataBytes, K_FIXED);
			return ES_ERROR_INSUFFICIENT_RESOURCES;
		}
	} else {
		entry->item.key.longKey = entry->inlineName;
	}

	EsMemoryCopy(entry->item.key.longKey, name, nameBytes);
	entry->item.key.longKeyBytes = nameBytes;

	EsMemoryCopy(entry, metadata, sizeof(KNodeMetadata));
	if (driverData) EsMemoryCopy(entry + 1, driverData, driverDataBytes);
	entry->parent = parent;

	TreeInsert(&parent->entries, &entry->item, entry, entry->item.key, AVL_DUPLICATE_KEYS_PANIC);
	parent->entryCount++;

	if (node) {
		if (!update) {
			EsError error = FSDirectoryEntryAllocateNode(entry, parent->fileSystem, driverData, false);

			if (error != ES_SUCCESS) {
				return error;
			}
		}

		*node = entry->node;
	} else {
		MMObjectCacheInsert(&parent->fileSystem->cachedDirectoryEntries, &entry->cacheItem);
	}

	return ES_SUCCESS;
}

void FSNodeUpdateDriverData(KNode *node, const void *newDriverData) {
	KWriterLockAssertExclusive(&node->writerLock);
	EsMemoryCopy(node->directoryEntry + 1, newDriverData, node->fileSystem->directoryEntryDataBytes);
}

void FSNodeSynchronize(KNode *node) {
	if (node->directoryEntry->type == ES_NODE_FILE) {
		FSFile *file = (FSFile *) node;
		CCSpaceFlush(&file->cache);
		KWriterLockTake(&node->writerLock, K_LOCK_EXCLUSIVE);
		FSFileCreateAndResizeOnFileSystem(file, file->directoryEntry->totalSize);
		KWriterLockReturn(&node->writerLock, K_LOCK_EXCLUSIVE);
	}

	if (node->flags & NODE_MODIFIED) {
		if (node->fileSystem->sync && (node->flags & NODE_CREATED_ON_FILE_SYSTEM /* might be false if create() failed */)) {
			FSDirectory *parent = node->directoryEntry->parent;

			if (parent) KWriterLockTake(&parent->writerLock, K_LOCK_EXCLUSIVE);
			node->fileSystem->sync(parent, node);
			if (parent) KWriterLockReturn(&parent->writerLock, K_LOCK_EXCLUSIVE);
		}
	}
}

void FSUnmountFileSystem(uintptr_t argument) {
	KFileSystem *fileSystem = (KFileSystem *) argument;
	KernelLog(LOG_INFO, "FS", "unmount start", "Unmounting file system %x...\n", fileSystem);

	MMObjectCacheUnregister(&fileSystem->cachedNodes);
	MMObjectCacheUnregister(&fileSystem->cachedDirectoryEntries);

	while (fileSystem->cachedNodes.count || fileSystem->cachedDirectoryEntries.count) {
		MMObjectCacheFlush(&fileSystem->cachedNodes);
		MMObjectCacheFlush(&fileSystem->cachedDirectoryEntries);
	}

	if (fileSystem->unmount) {
		fileSystem->unmount(fileSystem);
	}

	KernelLog(LOG_INFO, "FS", "unmount complete", "Unmounted file system %x.\n", fileSystem);
	KDeviceCloseHandle(fileSystem);
	__sync_fetch_and_sub(&fs.fileSystemsUnmounting, 1);
	KEventSet(&fs.fileSystemUnmounted, true);
}

//////////////////////////////////////////
// Opening nodes.
//////////////////////////////////////////

#define NODE_INCREMENT_HANDLE_COUNT(node) \
	node->handles++; \
	node->fileSystem->totalHandleCount++; \
	fs.totalHandleCount++;

EsError FSDirectoryEntryOpenHandleToNode(FSDirectoryEntry *directoryEntry) {
	KSpinlockAcquire(&fs.updateNodeHandles);
	EsDefer(KSpinlockRelease(&fs.updateNodeHandles));

	if (!directoryEntry->node || directoryEntry->removingNodeFromCache) {
		return ES_ERROR_NODE_NOT_LOADED;
	}
			
	if (directoryEntry->node->handles == NODE_MAX_ACCESSORS) {
		return ES_ERROR_INSUFFICIENT_RESOURCES;
	}

	KNode *node = directoryEntry->node;

	if (!node->handles) {
		if (node->directoryEntry->type == ES_NODE_DIRECTORY && ((FSDirectory *) node)->entryCount) {
			if (node->flags & NODE_IN_CACHE_LIST) KernelPanic("FSNodeOpenHandle - Directory %x with entries is in the cache list.\n", node);
		} else {
			if (~node->flags & NODE_IN_CACHE_LIST) KernelPanic("FSNodeOpenHandle - Node %x is not in the cache list.\n", node);
			MMObjectCacheRemove(&node->fileSystem->cachedNodes, &node->cacheItem);
			__sync_fetch_and_and(&node->flags, ~NODE_IN_CACHE_LIST);
		}
	}

	NODE_INCREMENT_HANDLE_COUNT(node);
	return ES_SUCCESS;
}

EsError FSNodeOpenHandle(KNode *node, uint32_t flags, uint8_t mode) {
	{
		// See comment in FSNodeCloseHandle for why we use the spinlock.
		KSpinlockAcquire(&fs.updateNodeHandles);
		EsDefer(KSpinlockRelease(&fs.updateNodeHandles));

		if (node->handles && mode == FS_NODE_OPEN_HANDLE_FIRST) {
			KernelPanic("FSNodeOpenHandle - Trying to open first handle to %x, but it already has handles.\n", node);
		} else if (!node->handles && mode == FS_NODE_OPEN_HANDLE_STANDARD) {
			KernelPanic("FSNodeOpenHandle - Trying to open handle to %x, but it has no handles.\n", node);
		}

		if (node->handles == NODE_MAX_ACCESSORS) { 
			return ES_ERROR_INSUFFICIENT_RESOURCES; 
		}

		if (node->directoryEntry->type == ES_NODE_FILE) {
			FSFile *file = (FSFile *) node;

			if (flags & ES_FILE_READ) {
				if (file->countWrite > 0) return ES_ERROR_FILE_HAS_WRITERS; 
			} else if (flags & ES_FILE_WRITE) {
				if (flags & _ES_NODE_FROM_WRITE_EXCLUSIVE) {
					if (!file->countWrite || (~file->flags & NODE_HAS_EXCLUSIVE_WRITER)) {
						KernelPanic("FSNodeOpenHandle - File %x is invalid state for a handle to have the _ES_NODE_FROM_WRITE_EXCLUSIVE flag.\n", file);
					}
				} else {
					if (file->countWrite) {
						return ES_ERROR_FILE_CANNOT_GET_EXCLUSIVE_USE; 
					}
				}
			} else if (flags & ES_FILE_WRITE_SHARED) {
				if ((file->flags & NODE_HAS_EXCLUSIVE_WRITER) || file->countWrite < 0) return ES_ERROR_FILE_IN_EXCLUSIVE_USE;
			}

			if (flags & (ES_FILE_WRITE_SHARED | ES_FILE_WRITE)) {
				if (!file->fileSystem->write) {
					return ES_ERROR_FILE_ON_READ_ONLY_VOLUME;
				}
			}

			if (flags & (ES_FILE_WRITE_SHARED | ES_FILE_WRITE)) file->countWrite++;
			if (flags & ES_FILE_READ) file->countWrite--;
			if (flags & ES_FILE_WRITE) __sync_fetch_and_or(&node->flags, NODE_HAS_EXCLUSIVE_WRITER);
		}

		NODE_INCREMENT_HANDLE_COUNT(node);

		// EsPrint("Open handle to %s (%d; %d).\n", node->directoryEntry->item.key.longKeyBytes, 
		// 		node->directoryEntry->item.key.longKey, node->handles, fs.totalHandleCount);
	}

	if (node->directoryEntry->type == ES_NODE_FILE && (flags & ES_NODE_PREVENT_RESIZE)) {
		// Modify blockResize with the resizeLock, to prevent a resize being in progress when blockResize becomes positive.
		FSFile *file = (FSFile *) node;
		KWriterLockTake(&file->resizeLock, K_LOCK_EXCLUSIVE);
		file->blockResize++;
		KWriterLockReturn(&file->resizeLock, K_LOCK_EXCLUSIVE);
	}

	return ES_SUCCESS;
}

void FSNodeCloseHandle(KNode *node, uint32_t flags) {
	if (node->directoryEntry->type == ES_NODE_FILE && (flags & ES_NODE_PREVENT_RESIZE)) {
		FSFile *file = (FSFile *) node;
		KWriterLockTake(&file->resizeLock, K_LOCK_EXCLUSIVE);
		file->blockResize--;
		KWriterLockReturn(&file->resizeLock, K_LOCK_EXCLUSIVE);
	}

	// Don't use the node's writer lock for this.
	// It'd be unnecessarily require getting exclusive access.
	// There's not much to do, so just use a global spinlock.
	KSpinlockAcquire(&fs.updateNodeHandles);

	if (node->handles) {
		node->handles--;
		node->fileSystem->totalHandleCount--;
		fs.totalHandleCount--;

		// EsPrint("Close handle to %s (%d; %d).\n", node->directoryEntry->item.key.longKeyBytes, 
		// 		node->directoryEntry->item.key.longKey, node->handles, fs.totalHandleCount);
	} else {
		KernelPanic("FSNodeCloseHandle - Node %x had no handles.\n", node);
	}

	if (node->directoryEntry->type == ES_NODE_FILE) {
		FSFile *file = (FSFile *) node;

		if ((flags & (ES_FILE_WRITE_SHARED | ES_FILE_WRITE))) {
			if (file->countWrite <= 0) KernelPanic("FSNodeCloseHandle - Invalid countWrite on node %x.\n", node);
			file->countWrite--;
		}

		if ((flags & ES_FILE_READ)) {
			if (file->countWrite >= 0) KernelPanic("FSNodeCloseHandle - Invalid countWrite on node %x.\n", node);
			file->countWrite++;
		}

		if ((flags & ES_FILE_WRITE) && file->countWrite == 0) {
			if (~file->flags & NODE_HAS_EXCLUSIVE_WRITER) KernelPanic("FSNodeCloseHandle - Missing exclusive flag on node %x.\n", node);
			__sync_fetch_and_and(&node->flags, ~NODE_HAS_EXCLUSIVE_WRITER);
		}
	}

	bool deleted = (node->flags & NODE_DELETED) && !node->handles;
	bool unmounted = !node->fileSystem->totalHandleCount;
	bool hasEntries = node->directoryEntry->type == ES_NODE_DIRECTORY && ((FSDirectory *) node)->entryCount;
	if (unmounted && node->handles) KernelPanic("FSNodeCloseHandle - File system has no handles but this node %x has handles.\n", node);
	KFileSystem *fileSystem = node->fileSystem;

	if (!node->handles && !deleted && !hasEntries) {
		if (node->flags & NODE_IN_CACHE_LIST) KernelPanic("FSNodeCloseHandle - Node %x is already in the cache list.\n", node);
		MMObjectCacheInsert(&node->fileSystem->cachedNodes, &node->cacheItem);
		__sync_fetch_and_or(&node->flags, NODE_IN_CACHE_LIST);
		node = nullptr; // The node could be freed at any time after MMObjectCacheInsert.
	}

	KSpinlockRelease(&fs.updateNodeHandles);

	if (unmounted && !fileSystem->unmounting) {
		// All handles to all nodes in the file system have been closed.
		// Spawn a thread to unmount it.
		fileSystem->unmounting = true;
		__sync_fetch_and_add(&fs.fileSystemsUnmounting, 1);
		KThreadCreate("FSUnmount", FSUnmountFileSystem, (uintptr_t) fileSystem); // TODO What should happen if creating the thread fails?
	}

	if (deleted) {
		if (!node->directoryEntry->parent) KernelPanic("FSNodeCloseHandle - A root directory %x was deleted.\n", node);

		// The node has been deleted, and no handles remain.
		// When it was deleted, it should have been removed from its parent directory,
		// both on the file system and in the directory lookup structures.
		// So, we are free to deallocate the node.

		FSDirectoryEntry *entry = node->directoryEntry;
		FSNodeFree(node);
		FSDirectoryEntryFree(entry);
	} 
}

EsError FSNodeTraverseLayer(uintptr_t *sectionEnd, 
		const char *path, size_t pathBytes, bool isFinalPath,
		KFileSystem *fileSystem, FSDirectory *directory, 
		uint32_t flags, KNode **node, bool *createdNode) {
	EsError error = ES_SUCCESS;

	*sectionEnd = *sectionEnd + 1;
	uintptr_t sectionStart = *sectionEnd;

	while (*sectionEnd != pathBytes && path[*sectionEnd] != '/') {
		*sectionEnd = *sectionEnd + 1;
	}

	const char *name = path + sectionStart;
	size_t nameBytes = *sectionEnd - sectionStart;

	if (!nameBytes) {
		FSNodeCloseHandle(directory, 0);
		return ES_ERROR_PATH_NOT_TRAVERSABLE;
	}

	AVLKey key = MakeLongKey(name, nameBytes);
	FSDirectoryEntry *entry = nullptr;
	AVLItem<FSDirectoryEntry> *treeItem = nullptr;

	// First, try to get the cached directory entry with shared access.

	{
		KWriterLockTake(&directory->writerLock, K_LOCK_SHARED);

		treeItem = TreeFind(&directory->entries, key, TREE_SEARCH_EXACT);
		bool needExclusiveAccess = true;

		if (treeItem) {
			error = FSDirectoryEntryOpenHandleToNode(treeItem->thisItem);

			if (error == ES_ERROR_NODE_NOT_LOADED) {
				error = ES_SUCCESS; // Proceed to use exclusive access.
			} else {
				entry = treeItem->thisItem;
				needExclusiveAccess = false;
			}
		}

		KWriterLockReturn(&directory->writerLock, K_LOCK_SHARED);

		if (!needExclusiveAccess) {
			goto usedSharedAccess;
		}
	}

	tryAgain:;

	KWriterLockTake(&directory->writerLock, K_LOCK_EXCLUSIVE);

	treeItem = TreeFind(&directory->entries, key, TREE_SEARCH_EXACT);

	if (!treeItem && (~directory->flags & NODE_ENUMERATED_ALL_DIRECTORY_ENTRIES)) {
		// The entry is not cached; load it from the file system.
		fileSystem->scan(name, nameBytes, directory);
		treeItem = TreeFind(&directory->entries, key, TREE_SEARCH_EXACT);
	}

	if (!treeItem) {
		// The node does not exist.

		if (flags & _ES_NODE_NO_WRITE_BASE) {
			error = ES_ERROR_PERMISSION_NOT_GRANTED;
			goto failed;
		}

		if (*sectionEnd == pathBytes && isFinalPath) {
			if (~flags & ES_NODE_FAIL_IF_NOT_FOUND) {
				error = FSNodeCreate(directory, name, nameBytes, flags & ES_NODE_DIRECTORY);
				if (error != ES_SUCCESS) goto failed;
				treeItem = TreeFind(&directory->entries, key, TREE_SEARCH_EXACT);
				flags &= ~ES_NODE_FAIL_IF_FOUND;
				*createdNode = true;
			}

			if (!treeItem) {
				error = ES_ERROR_FILE_DOES_NOT_EXIST;
				goto failed;
			}
		} else {
			if (flags & ES_NODE_CREATE_DIRECTORIES) {
				error = FSNodeCreate(directory, name, nameBytes, ES_NODE_DIRECTORY);
				if (error != ES_SUCCESS) goto failed;
				treeItem = TreeFind(&directory->entries, key, TREE_SEARCH_EXACT);
			}

			if (!treeItem) {
				error = ES_ERROR_PATH_NOT_TRAVERSABLE;
				goto failed;
			}
		}
	}

	entry = treeItem->thisItem;

	if (!entry->node) {
		// The node has not be loaded; load it from the file system.

		error = FSDirectoryEntryAllocateNode(entry, fileSystem, true, true);

		if (error == ES_ERROR_DIRECTORY_ENTRY_BEING_REMOVED) {
			// The directory entry is being removed.
			// Since this will take the directory's writer lock, we shouldn't need an additional synchronisation object.
			// Instead we can just yield to wait for a bit, then try again.
			// Similar to the strategy for the ES_ERROR_NODE_NOT_LOADED race below.
			KWriterLockReturn(&directory->writerLock, K_LOCK_EXCLUSIVE);
			KYield(); 
			goto tryAgain;
		}

		if (error != ES_SUCCESS) {
			goto failed;
		}

		error = fileSystem->load(directory, entry->node, entry, entry + 1);

		if (error != ES_SUCCESS) {
			FSNodeFree(entry->node);
			goto failed;
		}

		if (ES_SUCCESS != FSNodeOpenHandle(entry->node, 0, FS_NODE_OPEN_HANDLE_FIRST)) {
			KernelPanic("FSNodeTraverseLayer - FSNodeOpenHandle failed while opening first handle for %x.\n", entry->node);
		}
	} else {
		error = FSDirectoryEntryOpenHandleToNode(entry);

		if (error == ES_ERROR_NODE_NOT_LOADED) {
			// The node is being removed, or was just removed.
			// The main bottleneck in removing a node from the cache is CCSpaceFlush.
			// Since this will take the directory's writer lock, we shouldn't need an additional synchronisation object.
			// Instead we can just yield to wait for a bit, then try again.
			KWriterLockReturn(&directory->writerLock, K_LOCK_EXCLUSIVE);
			KYield(); 
			goto tryAgain;
		}
	}

	failed:;
	KWriterLockReturn(&directory->writerLock, K_LOCK_EXCLUSIVE);
	usedSharedAccess:;
	FSNodeCloseHandle(directory, 0);

	if (error != ES_SUCCESS) {
		return error;
	} 

	if (*sectionEnd != pathBytes || !isFinalPath) {
		if (entry->node->directoryEntry->type != ES_NODE_DIRECTORY) {
			FSNodeCloseHandle(directory, 0);
			return ES_ERROR_PATH_NOT_TRAVERSABLE;
		}
	}

	*node = entry->node;
	return ES_SUCCESS;
}

KNodeInformation FSNodeOpen(const char *path, size_t pathBytes, uint32_t flags, KNode *baseDirectory) {
	if ((1 << (flags & 0xF)) & ~(0x117)) {
		// You should only pass one access flag! (or none)
		return { ES_ERROR_PERMISSION_NOT_GRANTED }; 
	}

	if (fs.shutdown) return { ES_ERROR_PATH_NOT_TRAVERSABLE };
	if (pathBytes && path[pathBytes - 1] == '/') pathBytes--;

	if (!FSCheckPathForIllegalCharacters(path, pathBytes)) {
		return { ES_ERROR_ILLEGAL_PATH };
	}

	KFileSystem *fileSystem = nullptr;
	FSDirectory *directory = nullptr;
	EsError error = ES_ERROR_PATH_NOT_WITHIN_MOUNTED_VOLUME;

	KWriterLockTake(&fs.fileSystemsLock, K_LOCK_SHARED);

	if (!baseDirectory) {
		fileSystem = fs.bootFileSystem;
		directory = fileSystem ? (FSDirectory *) fileSystem->rootDirectory : nullptr;
	} else {
		fileSystem = baseDirectory->fileSystem;
		directory = (FSDirectory *) baseDirectory;

		if (directory->directoryEntry->type != ES_NODE_DIRECTORY) {
			KernelPanic("FSNodeOpen - Base directory %x was not a directory.\n", directory);
		}
	}

	KWriterLockReturn(&fs.fileSystemsLock, K_LOCK_SHARED);

	KNode *node = pathBytes ? nullptr : directory;

	error = FSNodeOpenHandle(directory, 0, FS_NODE_OPEN_HANDLE_STANDARD);
	if (error != ES_SUCCESS) return { error };

	bool createdNode = false;

	for (uintptr_t sectionEnd = 0; sectionEnd < pathBytes; ) {
		error = FSNodeTraverseLayer(&sectionEnd, path, pathBytes, true, fileSystem, directory, flags, &node, &createdNode);
		if (error != ES_SUCCESS) return { error };
		if (sectionEnd != pathBytes) directory = (FSDirectory *) node;
	}

	if (node->directoryEntry->type != ES_NODE_DIRECTORY && (flags & ES_NODE_DIRECTORY)) {
		return { ES_ERROR_INCORRECT_NODE_TYPE }; 
	}

	if ((flags & ES_NODE_FAIL_IF_FOUND) && !createdNode) {
		error = ES_ERROR_FILE_ALREADY_EXISTS;
	} else {
		error = FSNodeOpenHandle(node, flags, FS_NODE_OPEN_HANDLE_STANDARD);
	}

	FSNodeCloseHandle(node, 0);
	if (error != ES_SUCCESS) node = nullptr;
	return { error, node };
}

bool FSTrimCachedDirectoryEntry(MMObjectCache *cache) {
	FSDirectoryEntry *entry = nullptr;

	KSpinlockAcquire(&fs.updateNodeHandles);

	MMObjectCacheItem *item = MMObjectCacheRemoveLRU(cache);

	if (item) {
		entry = EsContainerOf(FSDirectoryEntry, cacheItem, item);
		entry->removingThisFromCache = true;
	}

	KSpinlockRelease(&fs.updateNodeHandles);

	if (entry) {
		if (!entry->parent) {
			// This is the root of the file system.
			FSDirectoryEntryFree(entry);
		} else if (ES_SUCCESS == FSNodeOpenHandle(entry->parent, ES_FLAGS_DEFAULT, FS_NODE_OPEN_HANDLE_DIRECTORY_TEMPORARY)) {
			KWriterLockTake(&entry->parent->writerLock, K_LOCK_EXCLUSIVE);
			TreeRemove(&entry->parent->entries, &entry->item);
			entry->parent->entryCount--;
			__sync_fetch_and_and(&entry->parent->flags, ~NODE_ENUMERATED_ALL_DIRECTORY_ENTRIES);
			KWriterLockReturn(&entry->parent->writerLock, K_LOCK_EXCLUSIVE);
			FSNodeCloseHandle(entry->parent, ES_FLAGS_DEFAULT); // This will put the parent in the node cache if needed.
			FSDirectoryEntryFree(entry);
		} else {
			// A very rare case where the parent directory had so many handles open that a temporary handle couldn't be opened.
			// Put it back in the cache, and hopefully next time we try to get rid of it there won't be 16 million handles open on the parent.
			// TODO Test this branch!
			KSpinlockAcquire(&fs.updateNodeHandles);
			entry->removingThisFromCache = false;
			MMObjectCacheInsert(cache, &entry->cacheItem);
			KSpinlockRelease(&fs.updateNodeHandles);
		}
	}

	return entry != nullptr;
}

bool FSTrimCachedNode(MMObjectCache *cache) {
	KNode *node = nullptr;

	KSpinlockAcquire(&fs.updateNodeHandles);

	MMObjectCacheItem *item = MMObjectCacheRemoveLRU(cache);

	if (item) {
		node = EsContainerOf(KNode, cacheItem, item);
		__sync_fetch_and_and(&node->flags, ~NODE_IN_CACHE_LIST);
		node->directoryEntry->removingNodeFromCache = true;
	}

	KSpinlockRelease(&fs.updateNodeHandles);

	if (node) {
		FSNodeSynchronize(node);
		FSNodeFree(node);
	}

	return node != nullptr;
}

//////////////////////////////////////////
// DMA transfer buffers.
//////////////////////////////////////////

uintptr_t KDMABufferGetVirtualAddress(KDMABuffer *buffer) {
	return buffer->virtualAddress;
}

size_t KDMABufferGetTotalByteCount(KDMABuffer *buffer) {
	return buffer->totalByteCount;
}

bool KDMABufferIsComplete(KDMABuffer *buffer) {
	return buffer->offsetBytes == buffer->totalByteCount;
}

KDMASegment KDMABufferNextSegment(KDMABuffer *buffer, bool peek) {
	if (buffer->offsetBytes >= buffer->totalByteCount || !buffer->virtualAddress) {
		KernelPanic("KDMABufferNextSegment - Invalid state in buffer %x.\n", buffer);
	}

	size_t transferByteCount = K_PAGE_SIZE;
	uintptr_t virtualAddress = buffer->virtualAddress + buffer->offsetBytes;
	uintptr_t physicalAddress = MMArchTranslateAddress(MMGetKernelSpace(), virtualAddress);
	uintptr_t offsetIntoPage = virtualAddress & (K_PAGE_SIZE - 1);

	if (!physicalAddress) {
		KernelPanic("KDMABufferNextSegment - Page in buffer %x unmapped.\n", buffer);
	}

	if (offsetIntoPage) {
		transferByteCount = K_PAGE_SIZE - offsetIntoPage;
		physicalAddress += offsetIntoPage;
	}

	if (transferByteCount > buffer->totalByteCount - buffer->offsetBytes) {
		transferByteCount = buffer->totalByteCount - buffer->offsetBytes;
	}

	bool isLast = buffer->offsetBytes + transferByteCount == buffer->totalByteCount;
	if (!peek) buffer->offsetBytes += transferByteCount;
	return { physicalAddress, transferByteCount, isLast };
}

//////////////////////////////////////////
// Block devices.
//////////////////////////////////////////

EsError FSBlockDeviceAccess(KBlockDeviceAccessRequest request) {
	KBlockDevice *device = request.device;

	if (!request.count) {
		return ES_SUCCESS;
	}

	if (device->information.readOnly && request.operation == K_ACCESS_WRITE) {
		if (request.flags & FS_BLOCK_ACCESS_SOFT_ERRORS) return ES_ERROR_BLOCK_ACCESS_INVALID;
		KernelPanic("FSBlockDeviceAccess - Drive %x is read-only.\n", device);
	}

	if (request.offset / device->information.sectorSize > device->information.sectorCount 
			|| (request.offset + request.count) / device->information.sectorSize > device->information.sectorCount) {
		if (request.flags & FS_BLOCK_ACCESS_SOFT_ERRORS) return ES_ERROR_BLOCK_ACCESS_INVALID;
		KernelPanic("FSBlockDeviceAccess - Access out of bounds on drive %x.\n", device);
	}

	if ((request.offset % device->information.sectorSize) || (request.count % device->information.sectorSize)) {
		if (request.flags & FS_BLOCK_ACCESS_SOFT_ERRORS) return ES_ERROR_BLOCK_ACCESS_INVALID;
		KernelPanic("FSBlockDeviceAccess - Misaligned access.\n");
	}

	KDMABuffer buffer = *request.buffer;

	if (buffer.virtualAddress & 3) {
		if (request.flags & FS_BLOCK_ACCESS_SOFT_ERRORS) return ES_ERROR_BLOCK_ACCESS_INVALID;
		KernelPanic("FSBlockDeviceAccess - Buffer must be DWORD aligned.\n");
	}

	KWorkGroup fakeDispatchGroup = {};

	if (!request.dispatchGroup) {
		fakeDispatchGroup.Initialise();
		request.dispatchGroup = &fakeDispatchGroup;
	}

	KBlockDeviceAccessRequest r = {};
	r.device = request.device;
	r.buffer = &buffer;
	r.flags = request.flags;
	r.dispatchGroup = request.dispatchGroup;
	r.operation = request.operation;
	r.offset = request.offset;

	while (request.count) {
		r.count = device->maxAccessSectorCount * device->information.sectorSize;
		if (r.count > request.count) r.count = request.count;
		buffer.offsetBytes = 0;
		buffer.totalByteCount = r.count;
		r.count = r.count;
		device->access(r);
		r.offset += r.count;
		buffer.virtualAddress += r.count;
		request.count -= r.count;
	}

	if (request.dispatchGroup == &fakeDispatchGroup) {
		return fakeDispatchGroup.Wait() ? ES_SUCCESS : ES_ERROR_DRIVE_CONTROLLER_REPORTED;
	} else {
		return ES_SUCCESS;
	}
}

EsError FSReadIntoBlockCache(CCSpace *cache, void *buffer, EsFileOffset offset, EsFileOffset count) {
	KFileSystem *fileSystem = EsContainerOf(KFileSystem, cacheSpace, cache);
	return fileSystem->Access(offset, count, K_ACCESS_READ, buffer, ES_FLAGS_DEFAULT, nullptr);
}

EsError FSWriteFromBlockCache(CCSpace *cache, const void *buffer, EsFileOffset offset, EsFileOffset count) {
	KFileSystem *fileSystem = EsContainerOf(KFileSystem, cacheSpace, cache);
	return fileSystem->Access(offset, count, K_ACCESS_WRITE, (void *) buffer, ES_FLAGS_DEFAULT, nullptr);
}

const CCSpaceCallbacks fsBlockCacheCallbacks = {
	.readInto = FSReadIntoBlockCache,
	.writeFrom = FSWriteFromBlockCache,
};

EsError KFileSystem::Access(EsFileOffset offset, size_t count, int operation, void *buffer, uint32_t flags, KWorkGroup *dispatchGroup) {
	if (this->flags & K_DEVICE_REMOVED) {
		if (dispatchGroup) {
			dispatchGroup->Start();
			dispatchGroup->End(false);
		}

		return ES_ERROR_DEVICE_REMOVED;
	}
	
	bool blockDeviceCachedEnabled = true;

	if (blockDeviceCachedEnabled && (flags & FS_BLOCK_ACCESS_CACHED)) {
		if (dispatchGroup) {
			dispatchGroup->Start();
		}

		// TODO Use the dispatch group.

		// We use the CC_ACCESS_PRECISE flag for file systems that have a block size less than the page size.
		// Otherwise, we might end up trashing file blocks (which aren't kept in the block device cache).

		EsError result = CCSpaceAccess(&cacheSpace, buffer, offset, count, 
				operation == K_ACCESS_READ ? CC_ACCESS_READ : (CC_ACCESS_WRITE | CC_ACCESS_WRITE_BACK | CC_ACCESS_PRECISE));

		if (dispatchGroup) {
			dispatchGroup->End(result == ES_SUCCESS);
		}

		return result;
	} else {
		KDMABuffer dmaBuffer = { (uintptr_t) buffer, count };
		KBlockDeviceAccessRequest request = {};
		request.device = block;
		request.offset = offset;
		request.count = count;
		request.operation = operation;
		request.buffer = &dmaBuffer;
		request.flags = flags;
		request.dispatchGroup = dispatchGroup;
		return FSBlockDeviceAccess(request);
	}
}

//////////////////////////////////////////
// Partition devices.
//////////////////////////////////////////

struct PartitionDevice : KBlockDevice {
	EsFileOffset sectorOffset;
};

void FSPartitionDeviceAccess(KBlockDeviceAccessRequest request) {
	PartitionDevice *_device = (PartitionDevice *) request.device;
	request.device = (KBlockDevice *) _device->parent;
	request.offset += _device->sectorOffset * _device->information.sectorSize;
	FSBlockDeviceAccess(request);
}

void FSPartitionDeviceCreate(KBlockDevice *parent, EsFileOffset offset, EsFileOffset sectorCount, uint32_t flags, const char *model, size_t modelBytes) {
	(void) flags;
	PartitionDevice *child = (PartitionDevice *) KDeviceCreate("Partition", parent, sizeof(PartitionDevice));
	if (!child) return;

	if (modelBytes > sizeof(child->information.model)) modelBytes = sizeof(child->information.model);
	EsMemoryCopy(child->information.model, model, modelBytes);

	child->parent = parent;
	child->information.sectorSize = parent->information.sectorSize;
	child->maxAccessSectorCount = parent->maxAccessSectorCount;
	child->sectorOffset = offset;
	child->information.sectorCount = sectorCount;
	child->information.readOnly = parent->information.readOnly;
	child->access = FSPartitionDeviceAccess;
	child->information.modelBytes = modelBytes;
	child->information.nestLevel = parent->information.nestLevel + 1;
	child->information.driveType = parent->information.driveType;

	FSRegisterBlockDevice(child);
}

//////////////////////////////////////////
// File system and partition table detection.
//////////////////////////////////////////

bool FSSignatureCheck(KInstalledDriver *driver, KDevice *device) {
	uint8_t *block = ((KBlockDevice *) device)->signatureBlock;

	EsINIState s = {};
	s.buffer = driver->config;
	s.bytes = driver->configBytes;

	int64_t offset = -1;

	while (EsINIParse(&s)) {
		if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("signature_offset"))) {
			offset = EsIntegerParse(s.value, s.valueBytes);
		} else if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("signature"))) {
			if (offset >= K_SIGNATURE_BLOCK_SIZE || offset >= K_SIGNATURE_BLOCK_SIZE - (int64_t) s.valueBytes || offset < 0) {
				KernelPanic("FSSignatureCheck - Filesystem '%s' has invalid signature detection information.\n", driver->nameBytes, driver->name);
			}

			if (0 == EsMemoryCompare(block + offset, s.value, s.valueBytes)) {
				return true;
			}
		}
	}

	return false;
}

bool FSCheckMBR(KBlockDevice *device) {
	MBRPartition partitions[4];

	if (MBRGetPartitions(device->signatureBlock, device->information.sectorCount, partitions)) {
		bool foundAny = false;

		for (uintptr_t i = 0; i < 4; i++) {
			if (partitions[i].present) {
				KernelLog(LOG_INFO, "FS", "MBR partition", "Found MBR partition %d with offset %d and count %d.\n", 
						i, partitions[i].offset, partitions[i].count);
				FSPartitionDeviceCreate(device, partitions[i].offset, partitions[i].count, ES_FLAGS_DEFAULT, EsLiteral("MBR partition"));
				foundAny = true;
			}
		}

		return foundAny;
	} else {
		return false;
	}
}

bool FSCheckGPT(KBlockDevice *device) {
	GPTPartition *partitions = (GPTPartition *) EsHeapAllocate(sizeof(GPTPartition) * GPT_PARTITION_COUNT, false, K_FIXED);
	EsDefer(EsHeapFree(partitions, sizeof(GPTPartition) * GPT_PARTITION_COUNT, K_FIXED));

	if (GPTGetPartitions(device->signatureBlock, device->information.sectorCount, device->information.sectorSize, partitions)) {
		for (uintptr_t i = 0; i < GPT_PARTITION_COUNT; i++) {
			if (partitions[i].present) {
				KernelLog(LOG_INFO, "FS", "GPT partition", "Found GPT partition %d with offset %d and count %d%z.\n", 
						i, partitions[i].offset, partitions[i].count, partitions[i].isESP ? "; this is the ESP" : "");
				FSPartitionDeviceCreate(device, partitions[i].offset, partitions[i].count, ES_FLAGS_DEFAULT, EsLiteral("GPT partition"));
			}
		}

		return true;
	} else {
		return false;
	}
}

bool FSFileSystemInitialise(KFileSystem *fileSystem) {
	FSDirectoryEntry *rootEntry = (FSDirectoryEntry *) EsHeapAllocate(sizeof(FSDirectoryEntry), true, K_FIXED);
	if (!rootEntry) goto error;

	rootEntry->type = ES_NODE_DIRECTORY;
	if (ES_SUCCESS != FSDirectoryEntryAllocateNode(rootEntry, nullptr, true, false)) goto error;

	fileSystem->rootDirectory = rootEntry->node;
	fileSystem->rootDirectory->fileSystem = fileSystem;
	fileSystem->block = (KBlockDevice *) fileSystem->parent;
	if (!CCSpaceInitialise(&fileSystem->cacheSpace)) goto error;

	fileSystem->cacheSpace.callbacks = &fsBlockCacheCallbacks;
	return true;

	error:;
	if (rootEntry && rootEntry->node) FSNodeFree(rootEntry->node);
	if (rootEntry) EsHeapFree(rootEntry, sizeof(FSDirectoryEntry), K_FIXED);
	KDeviceDestroy(fileSystem);
	return false;
}

void FSDetectFileSystem(KBlockDevice *device) {
	KMutexAcquire(&device->detectFileSystemMutex);
	EsDefer(KMutexRelease(&device->detectFileSystemMutex));

	if (device->children.Length()) {
		// The file system or partitions on the device have already been detected and mounted.
		return;
	}

	KernelLog(LOG_INFO, "FS", "detect file system", "Detecting file system on block device '%s'.\n", device->information.modelBytes, device->information.model);

	if (device->information.nestLevel > 4) {
		KernelLog(LOG_ERROR, "FS", "file system nest limit", "Reached file system nest limit (4), ignoring device.\n");
	}

	uint64_t sectorsToRead = (K_SIGNATURE_BLOCK_SIZE + device->information.sectorSize - 1) / device->information.sectorSize;

	if (sectorsToRead > device->information.sectorCount) {
		KernelLog(LOG_ERROR, "FS", "drive too small", "The drive must be at least %D (K_SIGNATURE_BLOCK_SIZE).\n", K_SIGNATURE_BLOCK_SIZE);
		return;
	}

	uint8_t *signatureBlock = (uint8_t *) EsHeapAllocate(sectorsToRead * device->information.sectorSize, false, K_FIXED);

	if (signatureBlock) {
		device->signatureBlock = signatureBlock;

		KDMABuffer dmaBuffer = { (uintptr_t) signatureBlock };
		KBlockDeviceAccessRequest request = {};
		request.device = device;
		request.count = sectorsToRead * device->information.sectorSize;
		request.operation = K_ACCESS_READ;
		request.buffer = &dmaBuffer;

		if (ES_SUCCESS != FSBlockDeviceAccess(request)) {
			// We could not access the block device.
			KernelLog(LOG_ERROR, "FS", "detect fileSystem read failure", "The signature block could not be read on block device %x.\n", device);
		} else {
			if (!device->information.nestLevel && FSCheckGPT(device)) {
				// Found a GPT.
			} else if (!device->information.nestLevel && FSCheckMBR(device)) {
				// Found an MBR.
			} else {
				KDeviceAttach(device, "Files", FSSignatureCheck);
			}
		}

		EsHeapFree(signatureBlock, sectorsToRead * device->information.sectorSize, K_FIXED);
	}

	KDeviceCloseHandle(device);
}

//////////////////////////////////////////
// Device management.
//////////////////////////////////////////

void FSRegisterBootFileSystem(KFileSystem *fileSystem, EsUniqueIdentifier identifier) {
	fileSystem->installationIdentifier = identifier;

	if (!EsMemoryCompare(&identifier, &installationID, sizeof(EsUniqueIdentifier))) {
		KWriterLockTake(&fs.fileSystemsLock, K_LOCK_EXCLUSIVE);

		if (!fs.bootFileSystem) {
			fs.bootFileSystem = fileSystem;
			KEventSet(&fs.foundBootFileSystemEvent);
			fileSystem->isBootFileSystem = true;
			FSNodeOpenHandle(fileSystem->rootDirectory, ES_FLAGS_DEFAULT, FS_NODE_OPEN_HANDLE_FIRST);
		} else {
			KernelLog(LOG_ERROR, "FS", "duplicate boot file system", "Found multiple boot file systems; the first registered will be used.\n");
		}

		KWriterLockReturn(&fs.fileSystemsLock, K_LOCK_EXCLUSIVE);
	}

	FSRegisterFileSystem(fileSystem); 
}

void FSFileSystemDeviceRemoved(KDevice *device) {
	KFileSystem *fileSystem = (KFileSystem *) device;
	_EsMessageWithObject m;
	EsMemoryZero(&m, sizeof(m));
	m.message.type = ES_MSG_UNREGISTER_FILE_SYSTEM;
	m.message.unregisterFileSystem.id = fileSystem->objectID;
	DesktopSendMessage(&m);
}

void FSRegisterFileSystem(KFileSystem *fileSystem) {
	fileSystem->removed = FSFileSystemDeviceRemoved;
		
	MMObjectCacheRegister(&fileSystem->cachedDirectoryEntries, FSTrimCachedDirectoryEntry, 
			sizeof(FSDirectoryEntry) + 16 /* approximate average name bytes */ + fileSystem->directoryEntryDataBytes);
	MMObjectCacheRegister(&fileSystem->cachedNodes, FSTrimCachedNode,
			sizeof(FSFile) + fileSystem->nodeDataBytes);
	fileSystem->rootDirectory->directoryEntry->directoryChildren = fileSystem->rootDirectoryInitialChildren;
	FSNodeOpenHandle(fileSystem->rootDirectory, ES_FLAGS_DEFAULT, fileSystem->isBootFileSystem ? FS_NODE_OPEN_HANDLE_STANDARD : FS_NODE_OPEN_HANDLE_FIRST);

	_EsMessageWithObject m;
	EsMemoryZero(&m, sizeof(m));
	m.message.type = ES_MSG_REGISTER_FILE_SYSTEM;
	m.message.registerFileSystem.isBootFileSystem = fileSystem->isBootFileSystem;
	m.message.registerFileSystem.rootDirectory = DesktopOpenHandle(fileSystem->rootDirectory, _ES_NODE_DIRECTORY_WRITE, KERNEL_OBJECT_NODE);

	if (m.message.registerFileSystem.rootDirectory) {
		if (!DesktopSendMessage(&m)) {
			DesktopCloseHandle(m.message.registerFileSystem.rootDirectory); // This will check that the handle is still valid.
		}
	}

	KDeviceSendConnectedMessage(fileSystem, ES_DEVICE_FILE_SYSTEM);
}

void FSRegisterBlockDevice(KBlockDevice *device) {
	KThreadCreate("FSDetect", [] (uintptr_t context) {
		KBlockDevice *device = (KBlockDevice *) context;
		FSDetectFileSystem(device);
		KDeviceSendConnectedMessage(device, ES_DEVICE_BLOCK);
	}, (uintptr_t) device);
}

void FSShutdown() {
	// A file system is unmounted when the last open handle to its nodes is closed.
	// When a file system is registered, a handle is opened to its root directory and given to Desktop.
	// Therefore, when the Desktop process is terminated, that handle is closed.
	// However, we additionally have a handle open to the boot file system, which we need to close.
	// Then, we wait for all file system unmounting threads to complete.

	// By this point there should be one open handle to the root directory,
	// and any handles temporarily opened by object cache trimming threads.
	// (FSTrimCachedDirectoryEntry opens a handle on the parent directory.)

	fs.shutdown = true;
	CloseHandleToObject(fs.bootFileSystem->rootDirectory, KERNEL_OBJECT_NODE);
	while (fs.fileSystemsUnmounting) KEventWait(&fs.fileSystemUnmounted);
	if (fs.totalHandleCount) KernelPanic("FSShutdown - Expected no open handles, got %d.\n", fs.totalHandleCount);
}

#endif
