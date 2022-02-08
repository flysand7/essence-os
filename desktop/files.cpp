EsError EsPathDelete(const char *path, ptrdiff_t pathBytes) {
	_EsNodeInformation node;
	if (pathBytes == -1) pathBytes = EsCStringLength(path);
	EsError error = NodeOpen(path, pathBytes, ES_NODE_FAIL_IF_NOT_FOUND | ES_FILE_WRITE, &node);
	if (ES_CHECK_ERROR(error)) return error;
	error = EsSyscall(ES_SYSCALL_NODE_DELETE, node.handle, 0, 0, 0);
	EsHandleClose(node.handle);
	return error;
}

EsError EsFileDelete(EsHandle handle) {
	return EsSyscall(ES_SYSCALL_NODE_DELETE, handle, 0, 0, 0);
}

void *EsFileMap(const char *path, ptrdiff_t pathBytes, size_t *fileSize, uint32_t flags) {
	EsFileInformation information = EsFileOpen(path, pathBytes, 
			ES_NODE_FAIL_IF_NOT_FOUND | ((flags & ES_MEMORY_MAP_OBJECT_READ_WRITE) ? ES_FILE_WRITE : ES_FILE_READ));

	if (ES_CHECK_ERROR(information.error)) {
		return nullptr;
	}

	void *base = EsMemoryMapObject(information.handle, 0, information.size, flags); 
	EsHandleClose(information.handle);
	if (fileSize) *fileSize = information.size;
	return base;
}

EsError EsPathMove(const char *oldPath, ptrdiff_t oldPathBytes, const char *newPath, ptrdiff_t newPathBytes, uint32_t flags) {
	if (oldPathBytes == -1) oldPathBytes = EsCStringLength(oldPath);
	if (newPathBytes == -1) newPathBytes = EsCStringLength(newPath);

	if (newPathBytes && newPath[newPathBytes - 1] == '/') {
		newPathBytes--;
	}

	_EsNodeInformation node = {};
	_EsNodeInformation directory = {};
	EsError error;

	error = NodeOpen(oldPath, oldPathBytes, ES_NODE_FAIL_IF_NOT_FOUND, &node);
	if (error != ES_SUCCESS) return error;

	uintptr_t s = 0;
	for (intptr_t i = 0; i < newPathBytes; i++) if (newPath[i] == '/') s = i + 1;
	error = NodeOpen(newPath, s, ES_NODE_DIRECTORY | ES_NODE_FAIL_IF_NOT_FOUND, &directory);
	if (error != ES_SUCCESS) { EsHandleClose(node.handle); return error; }

	error = EsSyscall(ES_SYSCALL_NODE_MOVE, node.handle, directory.handle, (uintptr_t) newPath + s, newPathBytes - s);

	if (error == ES_ERROR_VOLUME_MISMATCH && (flags & ES_PATH_MOVE_ALLOW_COPY_AND_DELETE) && (node.type == ES_NODE_FILE)) {
		// The paths are on different file systems, so we cannot directly move the file.
		// Instead we need to copy the file to the new path, and then delete the old file.
		// TODO Does it matter that this isn't atomic?
		error = EsFileCopy(oldPath, oldPathBytes, newPath, newPathBytes);
		if (error == ES_SUCCESS) error = EsPathDelete(oldPath, oldPathBytes);
	}

	EsHandleClose(node.handle);
	EsHandleClose(directory.handle);
	return error;
}

bool EsPathExists(const char *path, ptrdiff_t pathBytes, EsNodeType *type) {
	if (pathBytes == -1) pathBytes = EsCStringLength(path);
	_EsNodeInformation node = {};
	EsError error = NodeOpen(path, pathBytes, ES_NODE_FAIL_IF_NOT_FOUND, &node);
	if (error != ES_SUCCESS) return false;
	EsHandleClose(node.handle);
	if (type) *type = node.type;
	return true;
}

bool EsPathQueryInformation(const char *path, ptrdiff_t pathBytes, EsDirectoryChild *information) {
	if (pathBytes == -1) pathBytes = EsCStringLength(path);
	_EsNodeInformation node = {};
	EsError error = NodeOpen(path, pathBytes, ES_NODE_FAIL_IF_NOT_FOUND, &node);
	if (error != ES_SUCCESS) return false;
	EsHandleClose(node.handle);
	information->type = node.type;
	information->fileSize = node.fileSize;
	information->directoryChildren = node.directoryChildren;
	return true;
}

EsError EsPathCreate(const char *path, ptrdiff_t pathBytes, EsNodeType type, bool createLeadingDirectories) {
	if (pathBytes == -1) pathBytes = EsCStringLength(path);
	_EsNodeInformation node = {};
	EsError error = NodeOpen(path, pathBytes, 
			ES_NODE_FAIL_IF_FOUND | type | (createLeadingDirectories ? ES_NODE_CREATE_DIRECTORIES : 0), 
			&node);
	if (error != ES_SUCCESS) return error;
	EsHandleClose(node.handle);
	return ES_SUCCESS;
}

EsError EsFileControl(EsHandle file, uint32_t flags) {
	return EsSyscall(ES_SYSCALL_FILE_CONTROL, file, flags, 0, 0);
}

ptrdiff_t EsDirectoryEnumerateChildrenFromHandle(EsHandle directory, EsDirectoryChild *buffer, size_t size) {
	if (!size) return 0;
	return EsSyscall(ES_SYSCALL_DIRECTORY_ENUMERATE, directory, (uintptr_t) buffer, size, 0);
}

ptrdiff_t EsDirectoryEnumerateChildren(const char *path, ptrdiff_t pathBytes, EsDirectoryChild **buffer) {
	*buffer = nullptr;
	if (pathBytes == -1) pathBytes = EsCStringLength(path);

	_EsNodeInformation node;
	EsError error = NodeOpen(path, pathBytes, ES_NODE_FAIL_IF_NOT_FOUND | ES_NODE_DIRECTORY, &node);
	if (error != ES_SUCCESS) return error;

	if (node.directoryChildren == ES_DIRECTORY_CHILDREN_UNKNOWN) {
		node.directoryChildren = 4194304 / sizeof(EsDirectoryChild); // TODO Grow the buffer until all entries fit.
	}

	if (node.directoryChildren == 0) {
		// Empty directory.
		*buffer = nullptr;
		return 0;
	}

	*buffer = (EsDirectoryChild *) EsHeapAllocate(sizeof(EsDirectoryChild) * node.directoryChildren, true);
	ptrdiff_t result;

	if (*buffer) {
		result = EsDirectoryEnumerateChildrenFromHandle(node.handle, *buffer, node.directoryChildren);

		if (ES_CHECK_ERROR(result)) { 
			EsHeapFree(*buffer); 
			*buffer = nullptr; 
		}
	} else {
		result = ES_ERROR_INSUFFICIENT_RESOURCES;
	}

	EsHandleClose(node.handle);
	return result;
}

EsFileInformation EsFileOpen(const char *path, ptrdiff_t pathLength, uint32_t flags) {
	if (pathLength == -1) {
		pathLength = EsCStringLength(path);
	}

	_EsNodeInformation node;
	EsError result = NodeOpen(path, pathLength, flags, &node);

	if (result == ES_SUCCESS && node.type == ES_NODE_DIRECTORY && (~flags & ES_NODE_DIRECTORY /* for internal use only */)) {
		result = ES_ERROR_INCORRECT_NODE_TYPE;
		EsHandleClose(node.handle);
	}

	EsFileInformation information = {};

	if (result == ES_SUCCESS) {
		information.handle = node.handle;
		information.size = node.fileSize;
	}

	information.error = result;
	return information;
}

size_t EsFileReadSync(EsHandle handle, EsFileOffset offset, size_t size, void *buffer) {
	intptr_t result = EsSyscall(ES_SYSCALL_FILE_READ_SYNC, handle, offset, size, (uintptr_t) buffer);
	return result;
}

size_t EsFileWriteSync(EsHandle handle, EsFileOffset offset, size_t size, const void *buffer) {
	intptr_t result = EsSyscall(ES_SYSCALL_FILE_WRITE_SYNC, handle, offset, size, (uintptr_t) buffer);
	return result;
}

EsFileOffset EsFileGetSize(EsHandle handle) {
	return EsSyscall(ES_SYSCALL_FILE_GET_SIZE, handle, 0, 0, 0);
}

EsError EsFileResize(EsHandle handle, EsFileOffset newSize) {
	return EsSyscall(ES_SYSCALL_FILE_RESIZE, handle, newSize, 0, 0);
}

void *EsFileStoreReadAll(EsFileStore *file, size_t *fileSize) {
	if (file->error != ES_SUCCESS) return nullptr;

	if (file->type == FILE_STORE_HANDLE) {
		return EsFileReadAllFromHandle(file->handle, fileSize, &file->error);
	} else if (file->type == FILE_STORE_PATH) {
		return EsFileReadAll(file->path, file->pathBytes, fileSize, &file->error);
	} else if (file->type == FILE_STORE_EMBEDDED_FILE) {
		size_t _fileSize;
		const void *data = EsBundleFind(file->bundle, file->path, file->pathBytes, &_fileSize);
		void *copy = EsHeapAllocate(_fileSize, false);
		if (!copy) return nullptr;
		if (fileSize) *fileSize = _fileSize;
		EsMemoryCopy(copy, data, _fileSize);
		return copy;
	} else {
		EsAssert(false);
		return nullptr;
	}
}

bool EsFileStoreWriteAll(EsFileStore *file, const void *data, size_t dataBytes) {
	if (file->error == ES_SUCCESS) {
		if (file->type == FILE_STORE_HANDLE) {
			file->error = EsFileWriteAllFromHandle(file->handle, data, dataBytes);
		} else if (file->type == FILE_STORE_PATH) {
			file->error = EsFileWriteAll(file->path, file->pathBytes, data, dataBytes);
		} else {
			EsAssert(false);
		}
	}

	return file->error == ES_SUCCESS;
}

bool EsFileStoreAppend(EsFileStore *file, const void *data, size_t dataBytes) {
	if (file->error == ES_SUCCESS) {
		if (file->type == FILE_STORE_HANDLE) {
			EsError error = EsFileWriteSync(file->handle, EsFileGetSize(file->handle), dataBytes, data);
			if (ES_CHECK_ERROR(error)) file->error = error;
		} else {
			EsAssert(false);
		}
	}

	return file->error == ES_SUCCESS;
}

EsFileOffsetDifference EsFileStoreGetSize(EsFileStore *file) {
	if (file->type == FILE_STORE_HANDLE) {
		return EsFileGetSize(file->handle);
	} else if (file->type == FILE_STORE_PATH) {
		EsDirectoryChild information;

		if (EsPathQueryInformation(file->path, file->pathBytes, &information)) {
			return file->pathBytes;
		} else {
			return -1;
		}
	} else if (file->type == FILE_STORE_EMBEDDED_FILE) {
		size_t size;
		EsBundleFind(file->bundle, file->path, file->pathBytes, &size);
		return size;
	} else {
		EsAssert(false);
		return 0;
	}
}

void *EsFileStoreMap(EsFileStore *file, size_t *fileSize, uint32_t flags) {
	if (file->type == FILE_STORE_HANDLE) {
		EsFileOffsetDifference size = EsFileStoreGetSize(file);
		if (size == -1) return nullptr;
		*fileSize = size;
		return EsMemoryMapObject(file->handle, 0, size, flags); 
	} else if (file->type == FILE_STORE_PATH) {
		return EsFileMap(file->path, file->pathBytes, fileSize, flags);
	} else if (file->type == FILE_STORE_EMBEDDED_FILE) {
		return (void *) EsBundleFind(file->bundle, file->path, file->pathBytes, fileSize);
	} else {
		EsAssert(false);
		return nullptr;
	}
}

EsError MountPointAdd(const char *prefix, size_t prefixBytes, EsHandle base, bool addedByApplication) {
	EsMutexAcquire(&api.mountPointsMutex);
	bool duplicate = NodeFindMountPoint(prefix, prefixBytes, nullptr, true);
	EsError error = ES_SUCCESS;

	if (duplicate) {
		error = ES_ERROR_MOUNT_POINT_ALREADY_EXISTS;
	} else {
		EsMountPoint mountPoint = {};
		EsAssert(prefixBytes < sizeof(mountPoint.prefix));
		EsMemoryCopy(mountPoint.prefix, prefix, prefixBytes);
		mountPoint.base = EsSyscall(ES_SYSCALL_HANDLE_SHARE, base, ES_CURRENT_PROCESS, 0, 0);
		mountPoint.prefixBytes = prefixBytes;
		mountPoint.addedByApplication = addedByApplication;

		if (ES_CHECK_ERROR(mountPoint.base)) {
			error = ES_ERROR_INSUFFICIENT_RESOURCES;
		} else {
			if (!api.mountPoints.Add(mountPoint)) {
				EsHandleClose(mountPoint.base);
				error = ES_ERROR_INSUFFICIENT_RESOURCES;
			}
		}
	}

	EsMutexRelease(&api.mountPointsMutex);
	return error;
}

EsError EsMountPointAdd(const char *prefix, ptrdiff_t prefixBytes, EsHandle base) {
	return MountPointAdd(prefix, prefixBytes == -1 ? EsCStringLength(prefix) : prefixBytes, base, true);
}

bool NodeFindMountPoint(const char *prefix, size_t prefixBytes, EsMountPoint *result, bool mutexTaken) {
	if (!mutexTaken) EsMutexAcquire(&api.mountPointsMutex);
	bool found = false;

	for (uintptr_t i = 0; i < api.mountPoints.Length(); i++) {
		EsMountPoint *mountPoint = &api.mountPoints[i];

		if (prefixBytes >= mountPoint->prefixBytes && 0 == EsMemoryCompare(prefix, mountPoint->prefix, mountPoint->prefixBytes)) {
			// Only permanent mount points can be used retrieved with NodeFindMountPoint when mutexTaken = false,
			// because mount points added by the application can be removed as soon as we release the mutex,
			// and the base handle would be closed.
			EsAssert(mutexTaken || !mountPoint->addedByApplication);
			if (result) EsMemoryCopy(result, mountPoint, sizeof(EsMountPoint));
			found = true;
			break;
		}
	}

	if (!mutexTaken) EsMutexRelease(&api.mountPointsMutex);
	return found;
}

bool EsMountPointRemove(const char *prefix, ptrdiff_t prefixBytes) {
	if (prefixBytes == -1) {
		prefixBytes = EsCStringLength(prefix);
	}

	EsMutexAcquire(&api.mountPointsMutex);
	bool found = false;

	for (uintptr_t i = 0; i < api.mountPoints.Length(); i++) {
		EsMountPoint *mountPoint = &api.mountPoints[i];

		if ((uintptr_t) prefixBytes >= mountPoint->prefixBytes 
				&& 0 == EsMemoryCompare(prefix, mountPoint->prefix, mountPoint->prefixBytes)) {
			EsAssert(mountPoint->addedByApplication);
			EsHandleClose(mountPoint->base);
			api.mountPoints.Delete(i);
			found = true;
			break;
		}
	}

	EsMutexRelease(&api.mountPointsMutex);
	return found;
}

bool EsMountPointGetVolumeInformation(const char *prefix, ptrdiff_t prefixBytes, EsVolumeInformation *information) {
	if (prefixBytes == -1) {
		prefixBytes = EsCStringLength(prefix);
	}

	EsMutexAcquire(&api.mountPointsMutex);
	EsMountPoint mountPoint;
	bool found = NodeFindMountPoint(prefix, prefixBytes, &mountPoint, true);

	if (found) {
		_EsNodeInformation node;
		node.handle = mountPoint.base;
		EsError error = EsSyscall(ES_SYSCALL_NODE_OPEN, (uintptr_t) "/", 1, ES_NODE_DIRECTORY, (uintptr_t) &node);

		if (error == ES_SUCCESS) {
			EsSyscall(ES_SYSCALL_VOLUME_GET_INFORMATION, node.handle, (uintptr_t) information, 0, 0);
			EsHandleClose(node.handle);
		} else {
			EsMemoryZero(information, sizeof(EsVolumeInformation));
		}
	}

	EsMutexRelease(&api.mountPointsMutex);
	return found;
}

EsError NodeOpen(const char *path, size_t pathBytes, uint32_t flags, _EsNodeInformation *node) {
	// TODO I really don't like having to acquire a mutex to open a node.
	// 	This could be replaced with a writer lock!
	// 	(...but we don't have writer locks in userland yet.)
	EsMutexAcquire(&api.mountPointsMutex);

	EsMountPoint mountPoint;
	bool found = NodeFindMountPoint(path, pathBytes, &mountPoint, true);
	EsError error = ES_ERROR_PATH_NOT_WITHIN_MOUNTED_VOLUME;

	if (found) {
		node->handle = mountPoint.base;
		path += mountPoint.prefixBytes;
		pathBytes -= mountPoint.prefixBytes;
		error = EsSyscall(ES_SYSCALL_NODE_OPEN, (uintptr_t) path, pathBytes, flags, (uintptr_t) node);
	}

	EsMutexRelease(&api.mountPointsMutex);
	return error;
}

void _EsPathAnnouncePathMoved(const char *oldPath, ptrdiff_t oldPathBytes, const char *newPath, ptrdiff_t newPathBytes) {
	if (oldPathBytes == -1) oldPathBytes = EsCStringLength(oldPath);
	if (newPathBytes == -1) newPathBytes = EsCStringLength(newPath);
	size_t bufferBytes = 1 + sizeof(uintptr_t) * 2 + oldPathBytes + newPathBytes;
	char *buffer = (char *) EsHeapAllocate(bufferBytes, false);

	if (buffer) {
		buffer[0] = DESKTOP_MSG_ANNOUNCE_PATH_MOVED;
		EsMemoryCopy(buffer + 1, &oldPathBytes, sizeof(uintptr_t));
		EsMemoryCopy(buffer + 1 + sizeof(uintptr_t), &newPathBytes, sizeof(uintptr_t));
		EsMemoryCopy(buffer + 1 + sizeof(uintptr_t) * 2, oldPath, oldPathBytes);
		EsMemoryCopy(buffer + 1 + sizeof(uintptr_t) * 2 + oldPathBytes, newPath, newPathBytes);
		MessageDesktop(buffer, bufferBytes);
		EsHeapFree(buffer);
	}
}

void EsOpenDocumentQueryInformation(const char *path, ptrdiff_t pathBytes, EsOpenDocumentInformation *information) {
	if (pathBytes == -1) pathBytes = EsCStringLength(path);
	char *buffer = (char *) EsHeapAllocate(pathBytes + 1, false);

	if (buffer) {
		buffer[0] = DESKTOP_MSG_QUERY_OPEN_DOCUMENT;
		EsMemoryCopy(buffer + 1, path, pathBytes);
		EsBuffer response = { .out = (uint8_t *) information, .bytes = sizeof(EsOpenDocumentInformation) };
		MessageDesktop(buffer, pathBytes + 1, ES_INVALID_HANDLE, &response);
		EsHeapFree(buffer);
	}
}

void _EsOpenDocumentEnumerate(EsBuffer *outputBuffer) {
	uint8_t m = DESKTOP_MSG_LIST_OPEN_DOCUMENTS;
	MessageDesktop(&m, 1, ES_INVALID_HANDLE, outputBuffer);
}

void FileStoreCloseHandle(EsFileStore *fileStore) {
	EsMessageMutexCheck(); // TODO Remove this limitation?
	EsAssert(fileStore->handles < 0x80000000);

	if (--fileStore->handles) {
		return;
	}

	if (fileStore->type == FILE_STORE_HANDLE) {
		if (fileStore->handle) {
			EsHandleClose(fileStore->handle);
		}
	} else if (fileStore->type == FILE_STORE_PATH || fileStore->type == FILE_STORE_EMBEDDED_FILE) {
		// The path is stored after the file store allocation.
	}

	EsHeapFree(fileStore);
}

EsFileStore *FileStoreCreateFromPath(const char *path, size_t pathBytes) {
	EsFileStore *fileStore = (EsFileStore *) EsHeapAllocate(sizeof(EsFileStore) + pathBytes, false);
	if (!fileStore) return nullptr;
	EsMemoryZero(fileStore, sizeof(EsFileStore));
	fileStore->type = FILE_STORE_PATH;
	fileStore->handles = 1;
	fileStore->error = ES_SUCCESS;
	fileStore->path = (char *) (fileStore + 1);
	fileStore->pathBytes = pathBytes;
	EsMemoryCopy(fileStore->path, path, pathBytes);
	return fileStore;
}

EsFileStore *FileStoreCreateFromHandle(EsHandle handle) {
	EsFileStore *fileStore = (EsFileStore *) EsHeapAllocate(sizeof(EsFileStore), true);
	if (!fileStore) return nullptr;
	fileStore->type = FILE_STORE_HANDLE;
	fileStore->handles = 1;
	fileStore->error = ES_SUCCESS;
	fileStore->handle = handle;
	return fileStore;
}

EsFileStore *FileStoreCreateFromEmbeddedFile(const EsBundle *bundle, const char *name, size_t nameBytes) {
	EsFileStore *fileStore = (EsFileStore *) EsHeapAllocate(sizeof(EsFileStore) + nameBytes, false);
	if (!fileStore) return nullptr;
	EsMemoryZero(fileStore, sizeof(EsFileStore));
	fileStore->type = FILE_STORE_EMBEDDED_FILE;
	fileStore->handles = 1;
	fileStore->error = ES_SUCCESS;
	fileStore->path = (char *) (fileStore + 1);
	fileStore->pathBytes = nameBytes;
	fileStore->bundle = bundle;
	EsMemoryCopy(fileStore->path, name, nameBytes);
	return fileStore;
}

const void *EsBundleFind(const EsBundle *bundle, const char *_name, ptrdiff_t nameBytes, size_t *byteCount) {
	if (!bundle) {
		bundle = &bundleDefault;
	}

	if (nameBytes == -1) {
		nameBytes = EsCStringLength(_name);
	}

	if (bundle->bytes != -1) {
		if ((size_t) bundle->bytes < sizeof(BundleHeader) 
				|| (size_t) (bundle->bytes - sizeof(BundleHeader)) / sizeof(BundleFile) < bundle->base->fileCount
				|| bundle->base->signature != BUNDLE_SIGNATURE || bundle->base->version != 1) {
			return nullptr;
		}
	}

	const BundleHeader *header = bundle->base;
	const BundleFile *files = (const BundleFile *) (header + 1);
	uint64_t name = CalculateCRC64(_name, nameBytes, 0);

	for (uintptr_t i = 0; i < header->fileCount; i++) {
		if (files[i].nameCRC64 == name) {
			if (byteCount) {
				*byteCount = files[i].bytes;
			}

			if (bundle->bytes != -1) {
				if (files[i].offset >= (size_t) bundle->bytes || files[i].bytes > (size_t) (bundle->bytes - files[i].offset)) {
					return nullptr;
				}
			}

			return (const uint8_t *) header + files[i].offset;
		}
	}

	return nullptr;
}

EsError EsFileWriteAll(const char *filePath, ptrdiff_t filePathLength, const void *data, size_t sizes) {
	return EsFileWriteAllGather(filePath, filePathLength, &data, &sizes, 1);
}

EsError EsFileWriteAllGather(const char *filePath, ptrdiff_t filePathLength, const void **data, const size_t *sizes, size_t gatherCount) {
	if (filePathLength == -1) {
		filePathLength = EsCStringLength(filePath);
	}

	EsFileInformation information = EsFileOpen((char *) filePath, filePathLength, ES_FILE_WRITE | ES_NODE_CREATE_DIRECTORIES);

	if (ES_SUCCESS != information.error) {
		return information.error;
	}

	EsError error = EsFileWriteAllGatherFromHandle(information.handle, data, sizes, gatherCount);
	EsHandleClose(information.handle);
	return error;
}

EsError EsFileWriteAllFromHandle(EsHandle handle, const void *data, size_t sizes) {
	return EsFileWriteAllGatherFromHandle(handle, &data, &sizes, 1);
}

EsError EsFileWriteAllGatherFromHandle(EsHandle handle, const void **data, const size_t *sizes, size_t gatherCount) {
	size_t fileSize = 0;

	for (uintptr_t i = 0; i < gatherCount; i++) {
		fileSize += sizes[i];
	}

	EsError error = EsFileResize(handle, fileSize);
	if (ES_CHECK_ERROR(error)) return error;

	size_t offset = 0;

	for (uintptr_t i = 0; i < gatherCount; i++) {
		error = EsFileWriteSync(handle, offset, sizes[i], data[i]);
		if (ES_CHECK_ERROR(error)) return error;
		offset += sizes[i];
	}

	error = EsFileControl(handle, ES_FILE_CONTROL_FLUSH);
	return error;
}

void *EsFileReadAllFromHandle(EsHandle handle, size_t *fileSize, EsError *error) {
	if (error) *error = ES_SUCCESS;
	EsFileOffset size = EsFileGetSize(handle);
	if (fileSize) *fileSize = size;

	void *buffer = EsHeapAllocate(size + 1, false);

	if (!buffer) {
		if (error) *error = ES_ERROR_INSUFFICIENT_RESOURCES;
		return nullptr;
	}

	((char *) buffer)[size] = 0;

	uintptr_t result = EsFileReadSync(handle, 0, size, buffer);

	if (size != result) {
		EsHeapFree(buffer);
		buffer = nullptr;
		if (error) *error = (EsError) result;
	}
	
	return buffer;
}

void *EsFileReadAll(const char *filePath, ptrdiff_t filePathLength, size_t *fileSize, EsError *error) {
	if (error) *error = ES_SUCCESS;
	EsFileInformation information = EsFileOpen((char *) filePath, filePathLength, ES_FILE_READ | ES_NODE_FAIL_IF_NOT_FOUND);

	if (ES_SUCCESS != information.error) {
		if (error) *error = information.error;
		return nullptr;
	}

	void *buffer = EsFileReadAllFromHandle(information.handle, fileSize);
	EsHandleClose(information.handle);
	return buffer;
}

EsError EsFileCopy(const char *source, ptrdiff_t sourceBytes, const char *destination, ptrdiff_t destinationBytes, void **_copyBuffer,
		EsFileCopyCallback callback, EsGeneric callbackData) {
	const size_t copyBufferBytes = 262144;
	void *copyBuffer = _copyBuffer && *_copyBuffer ? *_copyBuffer : EsHeapAllocate(copyBufferBytes, false);
	if (_copyBuffer) *_copyBuffer = copyBuffer;

	if (!copyBuffer) {
		return ES_ERROR_INSUFFICIENT_RESOURCES;
	}

	EsError error = ES_SUCCESS;

	EsFileInformation sourceFile = EsFileOpen(source, sourceBytes, ES_FILE_READ | ES_NODE_FILE | ES_NODE_FAIL_IF_NOT_FOUND);

	if (sourceFile.error == ES_SUCCESS) {
		EsFileInformation destinationFile = EsFileOpen(destination, destinationBytes, ES_FILE_WRITE | ES_NODE_FILE | ES_NODE_FAIL_IF_FOUND);

		if (destinationFile.error == ES_SUCCESS) {
			error = EsFileResize(destinationFile.handle, sourceFile.size);

			if (error == ES_SUCCESS) {
				for (uintptr_t i = 0; i < sourceFile.size; i += copyBufferBytes) {
					size_t bytesRead = EsFileReadSync(sourceFile.handle, i, copyBufferBytes, copyBuffer);

					if (ES_CHECK_ERROR(bytesRead)) { 
						error = bytesRead; 
						break; 
					}

					size_t bytesWritten = EsFileWriteSync(destinationFile.handle, i, bytesRead, copyBuffer);

					if (ES_CHECK_ERROR(bytesWritten)) { 
						error = bytesWritten; 
						break; 
					}

					EsAssert(bytesRead == bytesWritten);

					if (callback && !callback(i + bytesWritten, sourceFile.size, callbackData)) {
						error = ES_ERROR_CANCELLED;
						break;
					}
				}
			}

			EsHandleClose(destinationFile.handle);
		} else {
			error = destinationFile.error;
		}

		EsHandleClose(sourceFile.handle);
	} else {
		error = sourceFile.error;
	}

	if (!_copyBuffer) EsHeapFree(copyBuffer);
	return error;
}
