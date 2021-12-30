// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

ThreadLocalStorage *GetThreadLocalStorage() {
	return (ThreadLocalStorage *) ProcessorTLSRead(tlsStorageOffset);
}

double EsTimeStampMs() {
	if (!api.startupInformation->timeStampTicksPerMs) {
		return 0;
	} else {
		return (double) ProcessorReadTimeStamp() / api.startupInformation->timeStampTicksPerMs;
	}
}

void EsDateNowUTC(EsDateComponents *date) {
	uint64_t linear = api.global->schedulerTimeMs + api.global->schedulerTimeOffset;
	DateToComponents(linear, date);
}

void *EsMemoryReserve(size_t size, EsMemoryProtection protection, uint32_t flags) {
	intptr_t result = EsSyscall(ES_SYSCALL_MEMORY_ALLOCATE, size, flags, protection, 0);

	if (result >= 0) {
		return (void *) result;
	} else {
		return nullptr;
	}
}

EsHandle EsMemoryCreateShareableRegion(size_t bytes) {
	return EsSyscall(ES_SYSCALL_MEMORY_ALLOCATE, bytes, 0, 0, 1);
}

void EsMemoryUnreserve(void *address, size_t size) {
	EsSyscall(ES_SYSCALL_MEMORY_FREE, (uintptr_t) address, size, 0, 0);
}

bool EsMemoryCommit(void *pointer, size_t bytes) {
	EsAssert(((uintptr_t) pointer & (ES_PAGE_SIZE - 1)) == 0 && (bytes & (ES_PAGE_SIZE - 1)) == 0); // Misaligned pointer/bytes in EsMemoryCommit.
	return ES_SUCCESS == (intptr_t) EsSyscall(ES_SYSCALL_MEMORY_COMMIT, (uintptr_t) pointer >> ES_PAGE_BITS, bytes >> ES_PAGE_BITS, 0, 0);
}

void EsMemoryFaultRange(const void *pointer, size_t bytes, uint32_t flags) {
	EsSyscall(ES_SYSCALL_MEMORY_FAULT_RANGE, (uintptr_t) pointer, bytes, flags, 0);
}

bool EsMemoryDecommit(void *pointer, size_t bytes) {
	EsAssert(((uintptr_t) pointer & (ES_PAGE_SIZE - 1)) == 0 && (bytes & (ES_PAGE_SIZE - 1)) == 0); // Misaligned pointer/bytes in EsMemoryDecommit.
	return ES_SUCCESS == (intptr_t) EsSyscall(ES_SYSCALL_MEMORY_COMMIT, (uintptr_t) pointer >> ES_PAGE_BITS, bytes >> ES_PAGE_BITS, 1, 0);
}

EsError EsProcessCreate(const EsProcessCreationArguments *arguments, EsProcessInformation *information) {
	EsProcessInformation _information;
	if (!information) information = &_information;

	EsError error = EsSyscall(ES_SYSCALL_PROCESS_CREATE, (uintptr_t) arguments, 0, (uintptr_t) information, 0);

	if (error == ES_SUCCESS && information == &_information) {
		EsHandleClose(information->handle);
		EsHandleClose(information->mainThread.handle);
	}

	return error;
}

EsError EsMessagePost(EsElement *target, EsMessage *message) {
	EsMutexAcquire(&api.postBoxMutex);

	_EsMessageWithObject m = { target, *message };
	bool success = api.postBox.Add(m);

	if (api.postBox.Length() == 1 && success) {
		EsMessage m;
		m.type = ES_MSG_WAKEUP;
		success = ES_SUCCESS == (EsError) EsSyscall(ES_SYSCALL_MESSAGE_POST, (uintptr_t) &m, 0, ES_CURRENT_PROCESS, 0);
	}

	EsMutexRelease(&api.postBoxMutex);

	return success ? ES_SUCCESS : ES_ERROR_INSUFFICIENT_RESOURCES;
}

EsError EsMessagePostRemote(EsHandle process, EsMessage *message) {
	return EsSyscall(ES_SYSCALL_MESSAGE_POST, (uintptr_t) message, 0, process, 0);
}

EsHandle EsEventCreate(bool autoReset) {
	return EsSyscall(ES_SYSCALL_EVENT_CREATE, autoReset, 0, 0, 0);
}

void EsEventSet(EsHandle handle) {
	EsSyscall(ES_SYSCALL_EVENT_SET, handle, 0, 0, 0);
}

void EsEventReset(EsHandle handle) {
	EsSyscall(ES_SYSCALL_EVENT_RESET, handle, 0, 0, 0);
}

EsError EsHandleClose(EsHandle handle) {
	return EsSyscall(ES_SYSCALL_HANDLE_CLOSE, handle, 0, 0, 0);
}

void EsThreadTerminate(EsHandle thread) {
	EsSyscall(ES_SYSCALL_THREAD_TERMINATE, thread, 0, 0, 0);
}

void EsProcessTerminate(EsHandle process, int status) {
	EsSyscall(ES_SYSCALL_PROCESS_TERMINATE, process, status, 0, 0);
}

void EsProcessTerminateCurrent() {
	EsSyscall(ES_SYSCALL_PROCESS_TERMINATE, ES_CURRENT_PROCESS, 0, 0, 0);
}

int EsProcessGetExitStatus(EsHandle process) {
	return EsSyscall(ES_SYSCALL_PROCESS_GET_STATUS, process, 0, 0, 0);
}

void EsProcessGetCreateData(EsProcessCreateData *data) {
	EsMemoryCopy(data, &api.startupInformation->data, sizeof(EsProcessCreateData));
}

void ThreadInitialise(ThreadLocalStorage *local);

__attribute__((no_instrument_function))
void ThreadEntry(EsGeneric argument, EsThreadEntryCallback entryFunction) {
	ThreadLocalStorage local;
	ThreadInitialise(&local);
	entryFunction(argument);
	EsThreadTerminate(ES_CURRENT_THREAD);
}

EsError EsThreadCreate(EsThreadEntryCallback entryFunction, EsThreadInformation *information, EsGeneric argument) {
	EsThreadInformation discard = {};

	if (!information) {
		information = &discard;
	}

	EsError error = EsSyscall(ES_SYSCALL_THREAD_CREATE, (uintptr_t) ThreadEntry, (uintptr_t) entryFunction, (uintptr_t) information, argument.u);

	if (error == ES_SUCCESS && information == &discard) {
		EsHandleClose(information->handle);
	}

	return error;
}

EsError EsFileWriteAll(const char *filePath, ptrdiff_t filePathLength, const void *data, size_t sizes) {
	return EsFileWriteAllGather(filePath, filePathLength, &data, &sizes, 1);
}

EsError EsFileWriteAllGather(const char *filePath, ptrdiff_t filePathLength, const void **data, size_t *sizes, size_t gatherCount) {
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

EsError EsFileWriteAllGatherFromHandle(EsHandle handle, const void **data, size_t *sizes, size_t gatherCount) {
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

#ifdef KERNEL
	void *buffer = EsHeapAllocate(size + 1, false, K_PAGED);
#else
	void *buffer = EsHeapAllocate(size + 1, false);
#endif

	if (!buffer) {
		if (error) *error = ES_ERROR_INSUFFICIENT_RESOURCES;
		return nullptr;
	}

	((char *) buffer)[size] = 0;

	uintptr_t result = EsFileReadSync(handle, 0, size, buffer);

	if (size != result) {
#ifdef KERNEL
		EsHeapFree(buffer, size + 1, K_PAGED);
#else
		EsHeapFree(buffer);
#endif
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

EsHandle EsMemoryShare(EsHandle sharedMemoryRegion, EsHandle targetProcess, bool readOnly) {
	return EsSyscall(ES_SYSCALL_HANDLE_SHARE, sharedMemoryRegion, targetProcess, readOnly, 0);
}

void *EsMemoryMapObject(EsHandle sharedMemoryRegion, uintptr_t offset, size_t size, unsigned flags) {
	intptr_t result = EsSyscall(ES_SYSCALL_MEMORY_MAP_OBJECT, sharedMemoryRegion, offset, size, flags);

	if (result >= 0) {
		return (void *) result;
	} else {
		return nullptr;
	}
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

uintptr_t EsWait(EsHandle *handles, size_t count, uintptr_t timeoutMs) {
	return EsSyscall(ES_SYSCALL_WAIT, (uintptr_t) handles, count, timeoutMs, 0);
}

void EsProcessPause(EsHandle process, bool resume) {
	EsSyscall(ES_SYSCALL_PROCESS_PAUSE, process, resume, 0, 0);
}

EsObjectID EsThreadGetID(EsHandle thread) {
	if (thread == ES_CURRENT_THREAD) {
		return GetThreadLocalStorage()->id;
	} else {
		EsObjectID id;
		EsSyscall(ES_SYSCALL_THREAD_GET_ID, thread, (uintptr_t) &id, 0, 0);
		return id;
	}
}

EsObjectID EsProcessGetID(EsHandle process) {
	EsObjectID id;
	EsSyscall(ES_SYSCALL_THREAD_GET_ID, process, (uintptr_t) &id, 0, 0);
	return id;
}

ptrdiff_t EsDirectoryEnumerateChildrenFromHandle(EsHandle directory, EsDirectoryChild *buffer, size_t size) {
	if (!size) return 0;
	return EsSyscall(ES_SYSCALL_DIRECTORY_ENUMERATE, directory, (uintptr_t) buffer, size, 0);
}

#ifndef KERNEL
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
#endif

void EsBatch(EsBatchCall *calls, size_t count) {
#if 0
	for (uintptr_t i = 0; i < count; i++) {
		EsBatchCall *call = calls + i;
		// ... modify system call for version changes ... 
	}
#endif

	EsSyscall(ES_SYSCALL_BATCH, (uintptr_t) calls, count, 0, 0);
}

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

void EsConstantBufferRead(EsHandle buffer, void *output) {
	EsSyscall(ES_SYSCALL_CONSTANT_BUFFER_READ, buffer, (uintptr_t) output, 0, 0);
}

void EsProcessGetState(EsHandle process, EsProcessState *state) {
	EsSyscall(ES_SYSCALL_PROCESS_GET_STATE, process, (uintptr_t) state, 0, 0);
}

void EsSchedulerYield() {
	EsSyscall(ES_SYSCALL_YIELD_SCHEDULER, 0, 0, 0, 0);
}

void EsSleep(uint64_t milliseconds) {
	EsSyscall(ES_SYSCALL_SLEEP, milliseconds >> 32, milliseconds & 0xFFFFFFFF, 0, 0);
}

EsHandle EsTakeSystemSnapshot(int type, size_t *bufferSize) {
	return EsSyscall(ES_SYSCALL_SYSTEM_TAKE_SNAPSHOT, type, (uintptr_t) bufferSize, 0, 0);
}

EsHandle EsProcessOpen(uint64_t pid) {
	// TODO This won't work correctly if arguments to system call are 32-bit.
	return EsSyscall(ES_SYSCALL_PROCESS_OPEN, pid, 0, 0, 0);
}

#ifndef KERNEL
EsHandle EsConstantBufferShare(EsHandle constantBuffer, EsHandle targetProcess) {
	return EsSyscall(ES_SYSCALL_HANDLE_SHARE, constantBuffer, targetProcess, 0, 0);
}

EsHandle EsConstantBufferCreate(const void *data, size_t dataBytes, EsHandle targetProcess) {
	return EsSyscall(ES_SYSCALL_CONSTANT_BUFFER_CREATE, (uintptr_t) data, targetProcess, dataBytes, 0);
}

size_t EsConstantBufferGetSize(EsHandle buffer) {
	return EsSyscall(ES_SYSCALL_CONSTANT_BUFFER_READ, buffer, 0, 0, 0);
}
#endif

EsError EsAddressResolve(const char *domain, ptrdiff_t domainBytes, uint32_t flags, EsAddress *address) {
	return EsSyscall(ES_SYSCALL_DOMAIN_NAME_RESOLVE, (uintptr_t) domain, domainBytes, (uintptr_t) address, flags);
}

EsError EsConnectionOpen(EsConnection *connection, uint32_t flags) {
	connection->error = ES_SUCCESS;
	connection->open = false;

	EsError error = EsSyscall(ES_SYSCALL_CONNECTION_OPEN, (uintptr_t) connection, flags, 0, 0);

	if (error == ES_SUCCESS && (flags & ES_CONNECTION_OPEN_WAIT)) {
		while (!connection->open && connection->error == ES_SUCCESS) {
			EsConnectionPoll(connection);
		}

		return connection->error;
	} else {
		return error;
	}
}

void EsConnectionPoll(EsConnection *connection) {
	EsSyscall(ES_SYSCALL_CONNECTION_POLL, (uintptr_t) connection, 0, 0, connection->handle);
}

void EsConnectionNotify(EsConnection *connection) {
	EsSyscall(ES_SYSCALL_CONNECTION_NOTIFY, 0, connection->sendWritePointer, connection->receiveReadPointer, connection->handle);
}

void EsConnectionClose(EsConnection *connection) {
	EsObjectUnmap(connection->sendBuffer);
	EsHandleClose(connection->handle);
}

EsError EsConnectionWriteSync(EsConnection *connection, const void *_data, size_t dataBytes) {
	const uint8_t *data = (const uint8_t *) _data;

	while (dataBytes) {
		EsConnectionPoll(connection);

		if (connection->error != ES_SUCCESS) {
			return connection->error;
		}

		size_t space = connection->sendWritePointer >= connection->sendReadPointer 
			? connection->sendBufferBytes - connection->sendWritePointer
			: connection->sendReadPointer - connection->sendWritePointer - 1;

		if (!space) {
			continue;
		}

		size_t bytesToWrite = space > dataBytes ? dataBytes : space;
		EsMemoryCopy(connection->sendBuffer + connection->sendWritePointer, data, bytesToWrite);
		data += bytesToWrite, dataBytes -= bytesToWrite;
		connection->sendWritePointer = (connection->sendWritePointer + bytesToWrite) % connection->sendBufferBytes;
		EsConnectionNotify(connection);
	}

	return ES_SUCCESS;
}

EsError EsConnectionRead(EsConnection *connection, void *_buffer, size_t bufferBytes, size_t *bytesRead) {
	uint8_t *buffer = (uint8_t *) _buffer;
	*bytesRead = 0;

	EsConnectionPoll(connection);

	if (connection->error != ES_SUCCESS) {
		return connection->error;
	}

	while (bufferBytes && connection->receiveReadPointer != connection->receiveWritePointer) {
		size_t bytesAvailable = connection->receiveReadPointer > connection->receiveWritePointer
			? connection->receiveBufferBytes - connection->receiveReadPointer
			: connection->receiveWritePointer - connection->receiveReadPointer;
		size_t bytesToRead = bufferBytes > bytesAvailable ? bytesAvailable : bufferBytes;
		EsMemoryCopy(buffer, connection->receiveBuffer + connection->receiveReadPointer, bytesToRead);
		connection->receiveReadPointer = (connection->receiveReadPointer + bytesToRead) % connection->receiveBufferBytes;
		buffer += bytesToRead, bufferBytes -= bytesToRead;
		*bytesRead += bytesToRead;
		EsConnectionNotify(connection);
	}

	return ES_SUCCESS;
}

size_t EsGameControllerStatePoll(EsGameControllerState *buffer) {
	return EsSyscall(ES_SYSCALL_GAME_CONTROLLER_STATE_POLL, (uintptr_t) buffer, 0, 0, 0);
}

void DesktopSyscall(EsMessage *message, uint8_t *buffer, EsBuffer *pipe);

void MessageDesktop(void *message, size_t messageBytes, EsHandle embeddedWindow = ES_INVALID_HANDLE, EsBuffer *responseBuffer = nullptr) {
	if (api.startupInformation->isDesktop) {
		EsMessage m = {};
		m.type = ES_MSG_DESKTOP;
		m.desktop.windowID = embeddedWindow ? EsSyscall(ES_SYSCALL_WINDOW_GET_ID, embeddedWindow, 0, 0, 0) : 0;
		m.desktop.processID = EsProcessGetID(ES_CURRENT_PROCESS);
		m.desktop.bytes = messageBytes;
		DesktopSyscall(&m, (uint8_t *) message, responseBuffer);
	} else {
		EsHandle pipeRead = ES_INVALID_HANDLE, pipeWrite = ES_INVALID_HANDLE;

		if (responseBuffer) {
			EsPipeCreate(&pipeRead, &pipeWrite);
		}

		EsSyscall(ES_SYSCALL_MESSAGE_DESKTOP, (uintptr_t) message, messageBytes, embeddedWindow, pipeWrite);

		if (responseBuffer) {
			char buffer[4096];
			EsHandleClose(pipeWrite);

			while (true) {
				size_t bytesRead = EsPipeRead(pipeRead, buffer, sizeof(buffer));
				if (!bytesRead) break;
				EsBufferWrite(responseBuffer, buffer, bytesRead);
			}

			EsHandleClose(pipeRead);
		}
	}

	if (responseBuffer) {
		responseBuffer->bytes = responseBuffer->position;
		responseBuffer->position = 0;
	}
}

struct ClipboardInformation {
	uint8_t desktopMessageTag;
	intptr_t error;
	EsClipboardFormat format;
	uint32_t flags;
};

EsFileStore *EsClipboardOpen(EsClipboard clipboard) {
	(void) clipboard;
	uint8_t m = DESKTOP_MSG_CREATE_CLIPBOARD_FILE;
	EsBuffer buffer = { .canGrow = true };
	EsHandle file;
	EsError error;
	MessageDesktop(&m, 1, ES_INVALID_HANDLE, &buffer);
	EsBufferReadInto(&buffer, &file, sizeof(file));
	EsBufferReadInto(&buffer, &error, sizeof(error));
	EsHeapFree(buffer.out);
	EsFileStore *fileStore = FileStoreCreateFromHandle(file);
	if (!fileStore) return nullptr;
	fileStore->error = error;
	return fileStore;
}

EsError EsClipboardCloseAndAdd(EsClipboard clipboard, EsClipboardFormat format, EsFileStore *fileStore, uint32_t flags) {
	(void) clipboard;
	EsError error = fileStore->error;

	if (error == ES_SUCCESS) {
		ClipboardInformation information = {};
		information.desktopMessageTag = DESKTOP_MSG_CLIPBOARD_PUT;
		information.error = error;
		information.format = format;
		information.flags = flags;
		MessageDesktop(&information, sizeof(information));
	}

	FileStoreCloseHandle(fileStore);
	return error;
}

EsError EsClipboardAddText(EsClipboard clipboard, const char *text, ptrdiff_t textBytes) {
	EsFileStore *fileStore = EsClipboardOpen(clipboard);

	if (fileStore) {
		EsFileStoreWriteAll(fileStore, text, textBytes); 
		return EsClipboardCloseAndAdd(clipboard, ES_CLIPBOARD_FORMAT_TEXT, fileStore);
	} else {
		return ES_ERROR_INSUFFICIENT_RESOURCES;
	}
}

void ClipboardGetInformation(EsHandle *file, ClipboardInformation *information) {
	uint8_t m = DESKTOP_MSG_CLIPBOARD_GET;
	EsBuffer buffer = { .canGrow = true };
	MessageDesktop(&m, 1, ES_INVALID_HANDLE, &buffer);
	EsBufferReadInto(&buffer, information, sizeof(*information));
	EsBufferReadInto(&buffer, file, sizeof(file));
	EsHeapFree(buffer.out);
}

bool EsClipboardHasFormat(EsClipboard clipboard, EsClipboardFormat format) {
	(void) clipboard;
	EsHandle file;
	ClipboardInformation information;
	ClipboardGetInformation(&file, &information);
	if (file) EsHandleClose(file);
	if (information.error != ES_SUCCESS) return false;

	if (format == ES_CLIPBOARD_FORMAT_TEXT) {
		return information.format == ES_CLIPBOARD_FORMAT_TEXT || information.format == ES_CLIPBOARD_FORMAT_PATH_LIST;
	} else {
		return information.format == format;
	}
}

bool EsClipboardHasData(EsClipboard clipboard) {
	(void) clipboard;
	EsHandle file;
	ClipboardInformation information;
	ClipboardGetInformation(&file, &information);
	if (file) EsHandleClose(file);
	return information.error == ES_SUCCESS && information.format != ES_CLIPBOARD_FORMAT_INVALID;
}

char *EsClipboardReadText(EsClipboard clipboard, size_t *bytes, uint32_t *flags) {
	(void) clipboard;

	char *result = nullptr;
	*bytes = 0;

	EsHandle file;
	ClipboardInformation information;
	ClipboardGetInformation(&file, &information);

	if (file) {
		if (information.format == ES_CLIPBOARD_FORMAT_TEXT || information.format == ES_CLIPBOARD_FORMAT_PATH_LIST) {
			result = (char *) EsFileReadAllFromHandle(file, bytes);

			if (flags) {
				*flags = information.flags;
			}
		}

		EsHandleClose(file);
	}

	return result;
}

void EsPipeCreate(EsHandle *readEnd, EsHandle *writeEnd) {
	*readEnd = *writeEnd = ES_INVALID_HANDLE;
	EsSyscall(ES_SYSCALL_PIPE_CREATE, (uintptr_t) readEnd, (uintptr_t) writeEnd, 0, 0);
}

size_t EsPipeRead(EsHandle pipe, void *buffer, size_t bytes) {
	return EsSyscall(ES_SYSCALL_PIPE_READ, pipe, (uintptr_t) buffer, bytes, 0);
}

size_t EsPipeWrite(EsHandle pipe, const void *buffer, size_t bytes) {
	return EsSyscall(ES_SYSCALL_PIPE_WRITE, pipe, (uintptr_t) buffer, bytes, 0);
}

EsError EsDeviceControl(EsHandle handle, EsDeviceControlType type, void *dp, void *dq) {
	return EsSyscall(ES_SYSCALL_DEVICE_CONTROL, handle, type, (uintptr_t) dp, (uintptr_t) dq);
}

uintptr_t _EsDebugCommand(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
	return EsSyscall(ES_SYSCALL_DEBUG_COMMAND, a, b, c, d);
}
