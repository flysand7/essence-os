// TODO Reject opening handles if the handle table has been destroyed!!

#ifndef IMPLEMENTATION

inline KernelObjectType operator|(KernelObjectType a, KernelObjectType b) {
	return (KernelObjectType) ((int) a | (int) b);
}

struct Handle {
	void *object;	
	uint32_t flags;
	KernelObjectType type;
};

struct ConstantBuffer {
	volatile size_t handles;
	size_t bytes : 48,
	       isPaged : 1;
	// Data follows.
};

struct Pipe {
#define PIPE_READER (1)
#define PIPE_WRITER (2)
#define PIPE_BUFFER_SIZE (K_PAGE_SIZE)
#define PIPE_CLOSED (0)

	volatile char buffer[PIPE_BUFFER_SIZE];
	volatile size_t writers, readers;
	volatile uintptr_t writePosition, readPosition, unreadData;
	KEvent canWrite, canRead;
	KMutex mutex;

	size_t Access(void *buffer, size_t bytes, bool write, bool userBlockRequest);
};

struct EventSink {
	KEvent available;
	KSpinlock spinlock; // Take after the scheduler's spinlock.
	volatile size_t handles;
	uintptr_t queuePosition;
	size_t queueCount;
	bool overflow, ignoreDuplicates;
	EsGeneric queue[ES_MAX_EVENT_SINK_BUFFER_SIZE];

	EsError Push(EsGeneric data);
};

struct EventSinkTable {
	EventSink *sink;
	EsGeneric data;
};

size_t totalHandleCount;

struct HandleTableL2 {
#define HANDLE_TABLE_L2_ENTRIES (256)
	Handle t[HANDLE_TABLE_L2_ENTRIES];
};

struct HandleTableL1 {
#define HANDLE_TABLE_L1_ENTRIES (256)
	HandleTableL2 *t[HANDLE_TABLE_L1_ENTRIES];
	uint16_t u[HANDLE_TABLE_L1_ENTRIES];
};

struct HandleTable {
	HandleTableL1 l1r;
	KMutex lock;
	Process *process;
	bool destroyed;
	uint32_t handleCount;

	EsHandle OpenHandle(void *_object, uint32_t _flags, KernelObjectType _type, EsHandle at = ES_INVALID_HANDLE);
	bool CloseHandle(EsHandle handle);

	// Resolve the handle if it is valid and return the type in type.
	// The initial value of type is used as a mask of expected object types for the handle.
	void ResolveHandle(struct KObject *object, EsHandle handle); 

	void Destroy(); 
};

struct KObject {
	void Initialise(HandleTable *handleTable, EsHandle _handle, KernelObjectType _type);

	KObject() { EsMemoryZero(this, sizeof(*this)); }
	KObject(Process *process, EsHandle _handle, KernelObjectType _type);
	KObject(HandleTable *handleTable, EsHandle _handle, KernelObjectType _type);

	~KObject() {
		if (!checked && valid) {
			KernelPanic("KObject - Object not checked!\n");
		}

		if (parentObject && close) {
			CloseHandleToObject(parentObject, parentType, parentFlags);
		}
	}

	void *object, *parentObject;
	uint32_t flags, parentFlags;
	KernelObjectType type, parentType;
	bool valid, checked, close, softFailure;
};

void InitialiseObjectManager();

#endif

#ifdef IMPLEMENTATION

KObject::KObject(Process *process, EsHandle _handle, KernelObjectType _type) {
	EsMemoryZero(this, sizeof(*this));
	Initialise(&process->handleTable, _handle, _type);
}

KObject::KObject(HandleTable *handleTable, EsHandle _handle, KernelObjectType _type) {
	EsMemoryZero(this, sizeof(*this));
	Initialise(handleTable, _handle, _type);
}

void KObject::Initialise(HandleTable *handleTable, EsHandle _handle, KernelObjectType _type) {
	type = _type;

	handleTable->ResolveHandle(this, _handle);
	parentObject = object, parentType = type, parentFlags = flags;

	if (!valid) {
		KernelLog(LOG_ERROR, "Object Manager", "invalid handle", "KObject::Initialise - Invalid handle %d for type mask %x.\n", _handle, _type);
	}
}

// A lock used to change the handle count on several objects.
// TODO Make changing handle count lockless wherever possible?
KMutex objectHandleCountChange;

// TODO Use uint64_t for handle counts, or restrict OpenHandleToObject to some maximum (...but most callers don't check if OpenHandleToObject succeeds).

bool OpenHandleToObject(void *object, KernelObjectType type, uint32_t flags, bool maybeHasNoHandles) {
	bool hadNoHandles = false, failed = false;

	switch (type) {
		case KERNEL_OBJECT_EVENT: {
			KMutexAcquire(&objectHandleCountChange);
			KEvent *event = (KEvent *) object;
			if (!event->handles) hadNoHandles = true;
			else event->handles++;
			KMutexRelease(&objectHandleCountChange);
		} break;

		case KERNEL_OBJECT_PROCESS: {
			KSpinlockAcquire(&scheduler.lock);
			Process *process = (Process *) object;
			if (!process->handles) hadNoHandles = true;
			else process->handles++; // NOTE Scheduler::OpenProcess and MMBalanceThread also adjust process handles.
			KernelLog(LOG_VERBOSE, "Scheduler", "open process handle", "Opened handle to process %d; %d handles.\n", process->id, process->handles);
			KSpinlockRelease(&scheduler.lock);
		} break;

		case KERNEL_OBJECT_THREAD: {
			KSpinlockAcquire(&scheduler.lock);
			Thread *thread = (Thread *) object;
			if (!thread->handles) hadNoHandles = true;
			else thread->handles++;
			KSpinlockRelease(&scheduler.lock);
		} break;

		case KERNEL_OBJECT_SHMEM: {
			MMSharedRegion *region = (MMSharedRegion *) object;
			KMutexAcquire(&region->mutex);
			if (!region->handles) hadNoHandles = true;
			else region->handles++;
			KMutexRelease(&region->mutex);
		} break;

		case KERNEL_OBJECT_WINDOW: {
			// NOTE The handle count of Window object is modified elsewhere.
			Window *window = (Window *) object;
			hadNoHandles = 0 == __sync_fetch_and_add(&window->handles, 1);
		} break;

		case KERNEL_OBJECT_EMBEDDED_WINDOW: {
			EmbeddedWindow *window = (EmbeddedWindow *) object;
			hadNoHandles = 0 == __sync_fetch_and_add(&window->handles, 1);
		} break;

		case KERNEL_OBJECT_CONSTANT_BUFFER: {
			ConstantBuffer *buffer = (ConstantBuffer *) object;
			KMutexAcquire(&objectHandleCountChange);
			if (!buffer->handles) hadNoHandles = true;
			else buffer->handles++;
			KMutexRelease(&objectHandleCountChange);
		} break;

#ifdef ENABLE_POSIX_SUBSYSTEM
		case KERNEL_OBJECT_POSIX_FD: {
			POSIXFile *file = (POSIXFile *) object;
			KMutexAcquire(&file->mutex);
			if (!file->handles) hadNoHandles = true;
			else file->handles++;
			KMutexRelease(&file->mutex);
		} break;
#endif

		case KERNEL_OBJECT_NODE: {
			failed = ES_SUCCESS != FSNodeOpenHandle((KNode *) object, flags, FS_NODE_OPEN_HANDLE_STANDARD);
		} break;

		case KERNEL_OBJECT_PIPE: {
			Pipe *pipe = (Pipe *) object;
			KMutexAcquire(&pipe->mutex);

			if (((flags & PIPE_READER) && !pipe->readers)
					|| ((flags & PIPE_WRITER) && !pipe->writers)) {
				hadNoHandles = true;
			} else {
				if (flags & PIPE_READER) {
					pipe->readers++;
				} 

				if (flags & PIPE_WRITER) {
					pipe->writers++;
				} 
			}

			KMutexRelease(&pipe->mutex);
		} break;

		case KERNEL_OBJECT_EVENT_SINK: {
			EventSink *sink = (EventSink *) object;
			KSpinlockAcquire(&sink->spinlock);
			if (!sink->handles) hadNoHandles = true;
			else sink->handles++;
			KSpinlockRelease(&sink->spinlock);
		} break;

		case KERNEL_OBJECT_CONNECTION: {
			NetConnection *connection = (NetConnection *) object;
			hadNoHandles = 0 == __sync_fetch_and_add(&connection->handles, 1);
		} break;

		default: {
			KernelPanic("OpenHandleToObject - Cannot open object of type %x.\n", type);
		} break;
	}

	if (hadNoHandles) {
		if (maybeHasNoHandles) {
			return false;
		} else {
			KernelPanic("OpenHandleToObject - Object %x of type %x had no handles.\n", object, type);
		}
	}

	return !failed;
}

void CloseHandleToObject(void *object, KernelObjectType type, uint32_t flags) {
	switch (type) {
		case KERNEL_OBJECT_PROCESS: {
			CloseHandleToProcess(object);
		} break;

		case KERNEL_OBJECT_THREAD: {
			CloseHandleToThread(object);
		} break;

		case KERNEL_OBJECT_NODE: {
			FSNodeCloseHandle((KNode *) object, flags);
		} break;

		case KERNEL_OBJECT_EVENT: {
			KEvent *event = (KEvent *) object;
			KMutexAcquire(&objectHandleCountChange);
			bool destroy = event->handles == 1;
			event->handles--;
			KMutexRelease(&objectHandleCountChange);

			if (destroy) {
				if (event->sinkTable) {
					for (uintptr_t i = 0; i < ES_MAX_EVENT_FORWARD_COUNT; i++) {
						if (event->sinkTable[i].sink) {
							CloseHandleToObject(event->sinkTable[i].sink, KERNEL_OBJECT_EVENT_SINK, 0);
						}
					}

					EsHeapFree(event->sinkTable, sizeof(EventSinkTable) * ES_MAX_EVENT_FORWARD_COUNT, K_FIXED);
				}

				EsHeapFree(event, sizeof(KEvent), K_FIXED);
			}
		} break;

		case KERNEL_OBJECT_CONSTANT_BUFFER: {
			ConstantBuffer *buffer = (ConstantBuffer *) object;
			KMutexAcquire(&objectHandleCountChange);
			bool destroy = buffer->handles == 1;
			buffer->handles--;
			KMutexRelease(&objectHandleCountChange);

			if (destroy) {
				EsHeapFree(object, sizeof(ConstantBuffer) + buffer->bytes, buffer->isPaged ? K_PAGED : K_FIXED);
			}
		} break;

		case KERNEL_OBJECT_SHMEM: {
			MMSharedRegion *region = (MMSharedRegion *) object;
			KMutexAcquire(&region->mutex);
			bool destroy = region->handles == 1;
			region->handles--;
			KMutexRelease(&region->mutex);

			if (destroy) {
				MMSharedDestroyRegion(region);
			}
		} break;

		case KERNEL_OBJECT_WINDOW: {
			Window *window = (Window *) object;
			unsigned previous = __sync_fetch_and_sub(&window->handles, 1);
			if (!previous) KernelPanic("CloseHandleToObject - Window %x has no handles.\n", window);

			if (previous == 2) {
				KEventSet(&windowManager.windowsToCloseEvent, false, true /* maybe already set */);
			} else if (previous == 1) {
				window->Destroy();
			}
		} break;

		case KERNEL_OBJECT_EMBEDDED_WINDOW: {
			EmbeddedWindow *window = (EmbeddedWindow *) object;
			unsigned previous = __sync_fetch_and_sub(&window->handles, 1);
			if (!previous) KernelPanic("CloseHandleToObject - EmbeddedWindow %x has no handles.\n", window);

			if (previous == 2) {
				KEventSet(&windowManager.windowsToCloseEvent, false, true /* maybe already set */);
			} else if (previous == 1) {
				window->Destroy();
			}
		} break;

#ifdef ENABLE_POSIX_SUBSYSTEM
		case KERNEL_OBJECT_POSIX_FD: {
			POSIXFile *file = (POSIXFile *) object;
			KMutexAcquire(&file->mutex);
			file->handles--;
			bool destroy = !file->handles;
			KMutexRelease(&file->mutex);

			if (destroy) {
				if (file->type == POSIX_FILE_NORMAL || file->type == POSIX_FILE_DIRECTORY) CloseHandleToObject(file->node, KERNEL_OBJECT_NODE, file->openFlags);
				if (file->type == POSIX_FILE_PIPE) CloseHandleToObject(file->pipe, KERNEL_OBJECT_PIPE, file->openFlags);
				EsHeapFree(file->path, 0, K_FIXED);
				EsHeapFree(file->directoryBuffer, file->directoryBufferLength, K_PAGED);
				EsHeapFree(file, sizeof(POSIXFile), K_FIXED);
			}
		} break;
#endif

		case KERNEL_OBJECT_PIPE: {
			Pipe *pipe = (Pipe *) object;
			KMutexAcquire(&pipe->mutex);

			if (flags & PIPE_READER) {
				pipe->readers--;

				if (!pipe->readers) {
					// If there are no more readers, wake up any blocking writers.
					KEventSet(&pipe->canWrite, false, true);
				}
			} 
			
			if (flags & PIPE_WRITER) {
				pipe->writers--;

				if (!pipe->writers) {
					// If there are no more writers, wake up any blocking readers.
					KEventSet(&pipe->canRead, false, true);
				}
			} 

			bool destroy = pipe->readers == 0 && pipe->writers == 0;

			KMutexRelease(&pipe->mutex);

			if (destroy) {
				EsHeapFree(pipe, sizeof(Pipe), K_PAGED);
			}
		} break;

		case KERNEL_OBJECT_EVENT_SINK: {
			EventSink *sink = (EventSink *) object;
			KSpinlockAcquire(&sink->spinlock);
			bool destroy = sink->handles == 1;
			sink->handles--;
			KSpinlockRelease(&sink->spinlock);

			if (destroy) {
				EsHeapFree(sink, sizeof(EventSink), K_FIXED);
			}
		} break;

		case KERNEL_OBJECT_CONNECTION: {
			NetConnection *connection = (NetConnection *) object;
			unsigned previous = __sync_fetch_and_sub(&connection->handles, 1);
			if (!previous) KernelPanic("CloseHandleToObject - NetConnection %x has no handles.\n", connection);
			if (previous == 1) NetConnectionClose(connection);
		} break;

		default: {
			KernelPanic("CloseHandleToObject - Cannot close object of type %x.\n", type);
		} break;
	}
}

bool HandleTable::CloseHandle(EsHandle handle) {
	if (handle > HANDLE_TABLE_L1_ENTRIES * HANDLE_TABLE_L2_ENTRIES) {
		return false;
	}

	KMutexAcquire(&lock);
	HandleTableL1 *l1 = &l1r;
	HandleTableL2 *l2 = l1->t[handle / HANDLE_TABLE_L2_ENTRIES];
	if (!l2) { KMutexRelease(&lock); return false; }
	Handle *_handle = l2->t + (handle % HANDLE_TABLE_L2_ENTRIES);
	KernelObjectType type = _handle->type;
	uint64_t flags = _handle->flags;
	void *object = _handle->object;
	if (!object) { KMutexRelease(&lock); return false; }
	EsMemoryZero(_handle, sizeof(Handle));
	l1->u[handle / HANDLE_TABLE_L2_ENTRIES]--;
	handleCount--;
	KMutexRelease(&lock);

	__sync_fetch_and_sub(&totalHandleCount, 1);
	CloseHandleToObject(object, type, flags);
	return true;
}

void HandleTable::ResolveHandle(KObject *object, EsHandle handle) {
	KernelObjectType requestedType = object->type;
	object->type = COULD_NOT_RESOLVE_HANDLE;

	// Special handles.
	if (handle == ES_CURRENT_THREAD && (requestedType & KERNEL_OBJECT_THREAD)) {
		object->type = KERNEL_OBJECT_THREAD, object->valid = true, object->object = GetCurrentThread();
		return;
	} else if (handle == ES_CURRENT_PROCESS && (requestedType & KERNEL_OBJECT_PROCESS)) {
		object->type = KERNEL_OBJECT_PROCESS, object->valid = true, object->object = GetCurrentThread()->process;
		return;
	} else if (handle == ES_INVALID_HANDLE && (requestedType & KERNEL_OBJECT_NONE)) {
		object->type = KERNEL_OBJECT_NONE, object->valid = true;
		return;
	}

	// Check that the handle is within the correct bounds.
	if ((!handle) || handle >= HANDLE_TABLE_L1_ENTRIES * HANDLE_TABLE_L2_ENTRIES) {
		return;
	}

	KMutexAcquire(&lock);
	EsDefer(KMutexRelease(&lock));

	HandleTableL2 *l2 = l1r.t[handle / HANDLE_TABLE_L2_ENTRIES];

	if (l2) {
		Handle *_handle = l2->t + (handle % HANDLE_TABLE_L2_ENTRIES);

		if ((_handle->type & requestedType) && (_handle->object)) {
			// Open a handle to the object so that it can't be destroyed while the system call is still using it.
			// The handle is closed in the KObject's destructor.
			if (OpenHandleToObject(_handle->object, _handle->type, _handle->flags)) {
				object->type = _handle->type, object->object = _handle->object, object->flags = _handle->flags;
				object->valid = object->close = true;
			}
		}
	}
}

// TODO Switch the order of flags and type, so that the default value of flags can be 0.
EsHandle HandleTable::OpenHandle(void *object, uint32_t flags, KernelObjectType type, EsHandle at) {
	KMutexAcquire(&lock);
	EsDefer(KMutexRelease(&lock));

	Handle handle = {};
	handle.object = object;
	handle.flags = flags;
	handle.type = type;

	{
		if (destroyed) goto error;

		if (!handle.object) {
			KernelPanic("HandleTable::OpenHandle - Invalid object.\n");
		}

		HandleTableL1 *l1 = &l1r;
		uintptr_t l1Index = HANDLE_TABLE_L1_ENTRIES;

		for (uintptr_t i = 1 /* The first set of handles are reserved. */; i < HANDLE_TABLE_L1_ENTRIES; i++) {
			if (at) i = at / HANDLE_TABLE_L2_ENTRIES;

			if (l1->u[i] != HANDLE_TABLE_L2_ENTRIES) {
				l1->u[i]++;
				handleCount++;
				l1Index = i;
				break;
			}

			if (at) goto error;
		}

		if (l1Index == HANDLE_TABLE_L1_ENTRIES) goto error;

		if (!l1->t[l1Index]) l1->t[l1Index] = (HandleTableL2 *) EsHeapAllocate(sizeof(HandleTableL2), true, K_FIXED);
		HandleTableL2 *l2 = l1->t[l1Index];
		uintptr_t l2Index = HANDLE_TABLE_L2_ENTRIES;

		for (uintptr_t i = 0; i < HANDLE_TABLE_L2_ENTRIES; i++) {
			if (at) i = at % HANDLE_TABLE_L2_ENTRIES;

			if (!l2->t[i].object) {
				l2Index = i;
				break;
			}

			if (at) goto error;
		}

		if (l2Index == HANDLE_TABLE_L2_ENTRIES)	KernelPanic("HandleTable::OpenHandle - Unexpected lack of free handles.\n");
		Handle *_handle = l2->t + l2Index;
		*_handle = handle;

		__sync_fetch_and_add(&totalHandleCount, 1);

		EsHandle index = l2Index + (l1Index * HANDLE_TABLE_L2_ENTRIES);
		return index;
	}

	error:;
	// TODO Close the handle to the object with CloseHandleToObject?
	return ES_INVALID_HANDLE;
}

void HandleTable::Destroy() {
	KMutexAcquire(&lock);
	EsDefer(KMutexRelease(&lock));

	if (destroyed) {
		return;
	}

	destroyed = true;
	HandleTableL1 *l1 = &l1r;

	for (uintptr_t i = 0; i < HANDLE_TABLE_L1_ENTRIES; i++) {
		if (!l1->u[i]) continue;

		for (uintptr_t k = 0; k < HANDLE_TABLE_L2_ENTRIES; k++) {
			Handle *handle = &l1->t[i]->t[k];
			if (handle->object) CloseHandleToObject(handle->object, handle->type, handle->flags);
		}

		EsHeapFree(l1->t[i], 0, K_FIXED);
	}
}

ConstantBuffer *MakeConstantBuffer(K_USER_BUFFER const void *data, size_t bytes) {
	ConstantBuffer *buffer = (ConstantBuffer *) EsHeapAllocate(sizeof(ConstantBuffer) + bytes, false, K_FIXED);
	if (!buffer) return nullptr;
	EsMemoryZero(buffer, sizeof(ConstantBuffer));
	buffer->handles = 1;
	buffer->bytes = bytes;
	EsMemoryCopy(buffer + 1, data, buffer->bytes);
	return buffer;
}

EsHandle MakeConstantBuffer(K_USER_BUFFER const void *data, size_t bytes, Process *process) {
	void *object = MakeConstantBuffer(data, bytes);

	if (!object) {
		return ES_INVALID_HANDLE;
	}

	EsHandle h = process->handleTable.OpenHandle(object, 0, KERNEL_OBJECT_CONSTANT_BUFFER); 

	if (h == ES_INVALID_HANDLE) {
		CloseHandleToObject(object, KERNEL_OBJECT_CONSTANT_BUFFER, 0);
		return ES_INVALID_HANDLE;
	}

	return h;
}

EsHandle MakeConstantBufferForDesktop(K_USER_BUFFER const void *data, size_t bytes) {
	return MakeConstantBuffer(data, bytes, desktopProcess);
}

size_t Pipe::Access(void *_buffer, size_t bytes, bool write, bool user) {
	size_t amount = 0;
	Thread *currentThread = GetCurrentThread();
	// EsPrint("--> %z %d\n", write ? "Write" : "Read", bytes);

	while (bytes) {
		if (user) currentThread->terminatableState = THREAD_USER_BLOCK_REQUEST;

		if (write) {
			// Wait until we can write to  the pipe.
			KEventWait(&canWrite, ES_WAIT_NO_TIMEOUT);
		} else {
			// Wait until we can read from the pipe.
			KEventWait(&canRead, ES_WAIT_NO_TIMEOUT);
		}

		if (user) {
			currentThread->terminatableState = THREAD_IN_SYSCALL;
			if (currentThread->terminating) goto done;
		}

		KMutexAcquire(&mutex);
		EsDefer(KMutexRelease(&mutex));

		if (write) {
			// EsPrint("Write:\n");

			size_t spaceAvailable = PIPE_BUFFER_SIZE - unreadData;
			size_t toWrite = bytes > spaceAvailable ? spaceAvailable : bytes;

			size_t spaceAvailableRight = PIPE_BUFFER_SIZE - writePosition;
			size_t toWriteRight = toWrite > spaceAvailableRight ? spaceAvailableRight : toWrite;
			size_t toWriteLeft = toWrite - toWriteRight;

			// EsPrint("\tunread: %d; wp: %d\n", unreadData, writePosition);
			// EsPrint("\t%d, %d, %d, %d, %d\n", spaceAvailable, spaceAvailableRight, toWrite, toWriteRight, toWriteLeft);

			EsMemoryCopy((uint8_t *) buffer + writePosition, _buffer, toWriteRight);
			EsMemoryCopy((uint8_t *) buffer, (uint8_t *) _buffer + toWriteRight, toWriteLeft);

			writePosition += toWrite;
			writePosition %= PIPE_BUFFER_SIZE;
			unreadData += toWrite;
			bytes -= toWrite;
			_buffer = (uint8_t *) _buffer + toWrite;
			amount += toWrite;

			KEventSet(&canRead, false, true);

			if (!readers) {
				// EsPrint("\tPipe closed\n");
				// Nobody is reading from the pipe, so there's no point writing to it.
				goto done;
			} else	if (PIPE_BUFFER_SIZE == unreadData) {
				KEventReset(&canWrite);
				// EsPrint("\treset canWrite\n");
			}
		} else {
			// EsPrint("Read:\n");

			size_t dataAvailable = unreadData;
			size_t toRead = bytes > dataAvailable ? dataAvailable : bytes;

			size_t spaceAvailableRight = PIPE_BUFFER_SIZE - readPosition;
			size_t toReadRight = toRead > spaceAvailableRight ? spaceAvailableRight : toRead;
			size_t toReadLeft = toRead - toReadRight;

			// EsPrint("\tunread: %d; rp: %d\n", unreadData, readPosition);
			// EsPrint("\t%d, %d, %d, %d, %d\n", dataAvailable, spaceAvailableRight, toRead, toReadRight, toReadLeft);

			EsMemoryCopy(_buffer, (uint8_t *) buffer + readPosition, toReadRight);
			EsMemoryCopy((uint8_t *) _buffer + toReadRight, (uint8_t *) buffer, toReadLeft);

			readPosition += toRead;
			readPosition %= PIPE_BUFFER_SIZE;
			unreadData -= toRead;
			bytes -= toRead;
			_buffer = (uint8_t *) _buffer + toRead;
			amount += toRead;

			KEventSet(&canWrite, false, true);

			if (!writers) {
				// Nobody is writing to the pipe, so there's no point reading from it.
				// EsPrint("\tPipe closed\n");
				goto done;
			} else if (!unreadData) {
				KEventReset(&canRead);
			}

			// Don't block when reading from pipes after the first chunk of data.
			// TODO Change this behaviour?
			goto done;
		}
	}

	done:;
	// EsPrint("<-- %d (%d remaining, %z)\n", amount, bytes, write ? "Write" : "Read");
	return amount;
}

#endif
