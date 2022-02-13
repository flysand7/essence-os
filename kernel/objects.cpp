// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

// TODO Reject opening handles if the handle table has been destroyed!!

#ifndef IMPLEMENTATION

struct Handle {
	void *object;	
	uint32_t flags;
	KernelObjectType type;
};

struct ConstantBuffer {
	volatile size_t handles;
	size_t bytes;
	bool isPaged;
	// Data follows.
};

struct Pipe {
#define PIPE_READER (1)
#define PIPE_WRITER (2)
#define PIPE_BUFFER_SIZE (256)
#define PIPE_CLOSED (0)

	volatile char buffer[PIPE_BUFFER_SIZE];
	volatile size_t writers, readers;
	volatile uintptr_t writePosition, readPosition, unreadData;
	KEvent canWrite, canRead;
	KMutex mutex;

	size_t Access(void *buffer, size_t bytes, bool write, bool userBlockRequest);
};

struct MessageQueue {
	bool SendMessage(void *target, EsMessage *message); // Returns false if the message queue is full.
	bool SendMessage(_EsMessageWithObject *message); // Returns false if the message queue is full.
	bool GetMessage(_EsMessageWithObject *message);

#define MESSAGE_QUEUE_MAX_LENGTH (4096)
	Array<_EsMessageWithObject, K_FIXED> messages;

	uintptr_t mouseMovedMessage, 
		  windowResizedMessage, 
		  eyedropResultMessage,
		  keyRepeatMessage;

	bool pinged;

	KMutex mutex;
	KEvent notEmpty;
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
	struct Process *process;
	bool destroyed;
	uint32_t handleCount;

	// Be careful putting handles in the handle table!
	// The process will be able to immediately close it.
	// If this fails, the handle is closed and ES_INVALID_HANDLE is returned.
	EsHandle OpenHandle(void *_object, uint32_t _flags, KernelObjectType _type, EsHandle at = ES_INVALID_HANDLE);

	bool CloseHandle(EsHandle handle);
	void ModifyFlags(EsHandle handle, uint32_t newFlags);

	// Resolve the handle if it is valid.
	// The initial value of type is used as a mask of expected object types for the handle.
#define RESOLVE_HANDLE_FAILED (0)
#define RESOLVE_HANDLE_NO_CLOSE (1)
#define RESOLVE_HANDLE_NORMAL (2)
	uint8_t ResolveHandle(Handle *outHandle, EsHandle inHandle, KernelObjectType typeMask); 

	void Destroy(); 
};

void InitialiseObjectManager();

#endif

#ifdef IMPLEMENTATION

// A lock used to change the handle count on several objects.
// TODO Make changing handle count lockless wherever possible?
KMutex objectHandleCountChange;

// TODO Use uint64_t for handle counts, or restrict OpenHandleToObject to some maximum (...but most callers don't check if OpenHandleToObject succeeds).

bool OpenHandleToObject(void *object, KernelObjectType type, uint32_t flags) {
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
			hadNoHandles = 0 == __sync_fetch_and_add(&((Process *) object)->handles, 1);
		} break;

		case KERNEL_OBJECT_THREAD: {
			hadNoHandles = 0 == __sync_fetch_and_add(&((Thread *) object)->handles, 1);
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

		case KERNEL_OBJECT_CONNECTION: {
			NetConnection *connection = (NetConnection *) object;
			hadNoHandles = 0 == __sync_fetch_and_add(&connection->handles, 1);
		} break;

		case KERNEL_OBJECT_DEVICE: {
			KDeviceOpenHandle((KDevice *) object, flags);
		} break;

		default: {
			KernelPanic("OpenHandleToObject - Cannot open object of type %x.\n", type);
		} break;
	}

	if (hadNoHandles) {
		KernelPanic("OpenHandleToObject - Object %x of type %x had no handles.\n", object, type);
	}

	return !failed;
}

void CloseHandleToObject(void *object, KernelObjectType type, uint32_t flags) {
	switch (type) {
		case KERNEL_OBJECT_PROCESS: {
			Process *process = (Process *) object;
			uintptr_t previous = __sync_fetch_and_sub(&process->handles, 1);
			KernelLog(LOG_VERBOSE, "Scheduler", "close process handle", "Closed handle to process %d; %d handles remain.\n", process->id, process->handles);

			if (previous == 0) {
				KernelPanic("CloseHandleToProcess - All handles to process %x have been closed.\n", process);
			} else if (previous == 1) {
				ProcessRemove(process);
			}
		} break;

		case KERNEL_OBJECT_THREAD: {
			Thread *thread = (Thread *) object;
			uintptr_t previous = __sync_fetch_and_sub(&thread->handles, 1);

			if (previous == 0) {
				KernelPanic("CloseHandleToObject - All handles to thread %x have been closed.\n", thread);
			} else if (previous == 1) {
				ThreadRemove(thread);
			}
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
				KEventSet(&windowManager.windowsToCloseEvent, true /* maybe already set */);
			} else if (previous == 1) {
				window->Destroy();
			}
		} break;

		case KERNEL_OBJECT_EMBEDDED_WINDOW: {
			EmbeddedWindow *window = (EmbeddedWindow *) object;
			unsigned previous = __sync_fetch_and_sub(&window->handles, 1);
			if (!previous) KernelPanic("CloseHandleToObject - EmbeddedWindow %x has no handles.\n", window);

			if (previous == 2) {
				KEventSet(&windowManager.windowsToCloseEvent, true /* maybe already set */);
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
					KEventSet(&pipe->canWrite, true);
				}
			} 
			
			if (flags & PIPE_WRITER) {
				pipe->writers--;

				if (!pipe->writers) {
					// If there are no more writers, wake up any blocking readers.
					KEventSet(&pipe->canRead, true);
				}
			} 

			bool destroy = pipe->readers == 0 && pipe->writers == 0;

			KMutexRelease(&pipe->mutex);

			if (destroy) {
				EsHeapFree(pipe, sizeof(Pipe), K_PAGED);
			}
		} break;

		case KERNEL_OBJECT_CONNECTION: {
			NetConnection *connection = (NetConnection *) object;
			unsigned previous = __sync_fetch_and_sub(&connection->handles, 1);
			if (!previous) KernelPanic("CloseHandleToObject - NetConnection %x has no handles.\n", connection);
			if (previous == 1) NetConnectionClose(connection);
		} break;

		case KERNEL_OBJECT_DEVICE: {
			KDeviceCloseHandle((KDevice *) object, flags);
		} break;

		default: {
			KernelPanic("CloseHandleToObject - Cannot close object of type %x.\n", type);
		} break;
	}
}

uintptr_t HandleShare(Handle share, Process *process, uint32_t mode, EsHandle at = ES_INVALID_HANDLE) {
#define HANDLE_SHARE_TYPE_MASK ((KernelObjectType) (KERNEL_OBJECT_SHMEM | KERNEL_OBJECT_CONSTANT_BUFFER | KERNEL_OBJECT_PROCESS \
		| KERNEL_OBJECT_DEVICE | KERNEL_OBJECT_NODE | KERNEL_OBJECT_EVENT | KERNEL_OBJECT_PIPE))

	if ((share.type & HANDLE_SHARE_TYPE_MASK) == 0) {
		KernelPanic("HandleShare - Invalid object type %x; allowed types are %x.\n", share.type, HANDLE_SHARE_TYPE_MASK);
	}

	uint32_t sharedFlags = share.flags;

	// TODO Sort out flag modes.

	if (share.type == KERNEL_OBJECT_SHMEM) {
		sharedFlags &= mode;
	} else if (share.type == KERNEL_OBJECT_NODE) {
		sharedFlags = (mode & 1) && (share.flags & (ES_FILE_WRITE_SHARED | ES_FILE_WRITE)) ? ES_FILE_READ_SHARED : share.flags;
		if (mode & 2) sharedFlags &= ~ES__NODE_DIRECTORY_WRITE;
	} else if (share.type == KERNEL_OBJECT_PIPE) {
	}

	if (!OpenHandleToObject(share.object, share.type, sharedFlags)) {
		return ES_ERROR_PERMISSION_NOT_GRANTED;
	} else {
		return process->handleTable.OpenHandle(share.object, sharedFlags, share.type, at);
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

void HandleTable::ModifyFlags(EsHandle handle, uint32_t newFlags) {
	KMutexAcquire(&lock);
	EsDefer(KMutexRelease(&lock));
	if ((!handle) || handle >= HANDLE_TABLE_L1_ENTRIES * HANDLE_TABLE_L2_ENTRIES) return;
	HandleTableL2 *l2 = l1r.t[handle / HANDLE_TABLE_L2_ENTRIES];
	if (!l2) return;
	Handle *_handle = l2->t + (handle % HANDLE_TABLE_L2_ENTRIES);
	if (!_handle->object) return;
	_handle->flags = newFlags;
}

uint8_t HandleTable::ResolveHandle(Handle *outHandle, EsHandle inHandle, KernelObjectType typeMask) {
	// Special handles.
	if (inHandle == ES_CURRENT_THREAD && (typeMask & KERNEL_OBJECT_THREAD)) {
		outHandle->type = KERNEL_OBJECT_THREAD;
		outHandle->object = GetCurrentThread();
		outHandle->flags = 0;
		return RESOLVE_HANDLE_NO_CLOSE;
	} else if (inHandle == ES_CURRENT_PROCESS && (typeMask & KERNEL_OBJECT_PROCESS)) {
		outHandle->type = KERNEL_OBJECT_PROCESS;
		outHandle->object = GetCurrentThread()->process;
		outHandle->flags = 0;
		return RESOLVE_HANDLE_NO_CLOSE;
	} else if (inHandle == ES_INVALID_HANDLE && (typeMask & KERNEL_OBJECT_NONE)) {
		outHandle->type = KERNEL_OBJECT_NONE;
		outHandle->object = nullptr;
		outHandle->flags = 0;
		return RESOLVE_HANDLE_NO_CLOSE;
	}

	// Check that the handle is within the correct bounds.
	if ((!inHandle) || inHandle >= HANDLE_TABLE_L1_ENTRIES * HANDLE_TABLE_L2_ENTRIES) {
		return RESOLVE_HANDLE_FAILED;
	}

	KMutexAcquire(&lock);
	EsDefer(KMutexRelease(&lock));

	HandleTableL2 *l2 = l1r.t[inHandle / HANDLE_TABLE_L2_ENTRIES];
	if (!l2) return RESOLVE_HANDLE_FAILED;

	Handle *_handle = l2->t + (inHandle % HANDLE_TABLE_L2_ENTRIES);

	if ((_handle->type & typeMask) && (_handle->object)) {
		// Open a handle to the object so that it can't be destroyed while the system call is still using it.
		// The handle is closed in the KObject's destructor.
		if (OpenHandleToObject(_handle->object, _handle->type, _handle->flags)) {
			*outHandle = *_handle;
			return RESOLVE_HANDLE_NORMAL;
		}
	}

	return RESOLVE_HANDLE_FAILED;
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
		if (!l2) goto error;
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
	CloseHandleToObject(object, type, flags);
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

ConstantBuffer *ConstantBufferCreate(K_USER_BUFFER const void *data, size_t bytes) {
	ConstantBuffer *buffer = (ConstantBuffer *) EsHeapAllocate(sizeof(ConstantBuffer) + bytes, false, K_FIXED);
	if (!buffer) return nullptr;
	EsMemoryZero(buffer, sizeof(ConstantBuffer));
	buffer->handles = 1;
	buffer->bytes = bytes;
	EsMemoryCopy(buffer + 1, data, buffer->bytes);
	return buffer;
}

EsHandle ConstantBufferCreate(K_USER_BUFFER const void *data, size_t bytes, Process *process) {
	void *object = ConstantBufferCreate(data, bytes);
	return object ? process->handleTable.OpenHandle(object, 0, KERNEL_OBJECT_CONSTANT_BUFFER) : ES_INVALID_HANDLE; 
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

			if (_buffer) EsMemoryCopy((uint8_t *) buffer + writePosition, _buffer, toWriteRight);
			if (_buffer) EsMemoryCopy((uint8_t *) buffer, (uint8_t *) _buffer + toWriteRight, toWriteLeft);

			writePosition += toWrite;
			writePosition %= PIPE_BUFFER_SIZE;
			unreadData += toWrite;
			bytes -= toWrite;
			if (_buffer) _buffer = (uint8_t *) _buffer + toWrite;
			amount += toWrite;

			KEventSet(&canRead, true);

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

			if (_buffer) EsMemoryCopy(_buffer, (uint8_t *) buffer + readPosition, toReadRight);
			if (_buffer) EsMemoryCopy((uint8_t *) _buffer + toReadRight, (uint8_t *) buffer, toReadLeft);

			readPosition += toRead;
			readPosition %= PIPE_BUFFER_SIZE;
			unreadData -= toRead;
			bytes -= toRead;
			if (_buffer) _buffer = (uint8_t *) _buffer + toRead;
			amount += toRead;

			KEventSet(&canWrite, true);

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

bool MessageQueue::SendMessage(void *object, EsMessage *_message) {
	// TODO Remove unnecessary copy.
	_EsMessageWithObject message = { object, *_message };
	return SendMessage(&message);
}

bool MessageQueue::SendMessage(_EsMessageWithObject *_message) {
	// TODO Don't send messages if the process has been terminated.

	KMutexAcquire(&mutex);
	EsDefer(KMutexRelease(&mutex));

	if (messages.Length() == MESSAGE_QUEUE_MAX_LENGTH) {
		KernelLog(LOG_ERROR, "Messages", "message dropped", "Message of type %d and target %x has been dropped because queue %x was full.\n",
				_message->message.type, _message->object, this);
		return false;
	}

#define MERGE_MESSAGES(variable, change) \
	do { \
		if (variable && messages[variable - 1].object == _message->object) { \
			if (change) EsMemoryCopy(&messages[variable - 1], _message, sizeof(_EsMessageWithObject)); \
		} else if (messages.AddPointer(_message)) { \
			variable = messages.Length(); \
		} else { \
			return false; \
		} \
	} while (0)

	// NOTE Don't forget to update GetMessage with the merged messages!

	if (_message->message.type == ES_MSG_MOUSE_MOVED) {
		MERGE_MESSAGES(mouseMovedMessage, true);
	} else if (_message->message.type == ES_MSG_WINDOW_RESIZED) {
		MERGE_MESSAGES(windowResizedMessage, true);
	} else if (_message->message.type == ES_MSG_EYEDROP_REPORT) {
		MERGE_MESSAGES(eyedropResultMessage, true);
	} else if (_message->message.type == ES_MSG_KEY_DOWN && _message->message.keyboard.repeat) {
		MERGE_MESSAGES(keyRepeatMessage, false);
	} else {
		if (!messages.AddPointer(_message)) {
			return false;
		}

		if (_message->message.type == ES_MSG_PING) {
			pinged = true;
		}
	}

	KEventSet(&notEmpty, true);

	return true;
}

bool MessageQueue::GetMessage(_EsMessageWithObject *_message) {
	KMutexAcquire(&mutex);
	EsDefer(KMutexRelease(&mutex));

	if (!messages.Length()) {
		return false;
	}

	*_message = messages[0];
	messages.Delete(0);

	if (mouseMovedMessage)    mouseMovedMessage--;
	if (windowResizedMessage) windowResizedMessage--;
	if (eyedropResultMessage) eyedropResultMessage--;
	if (keyRepeatMessage)     keyRepeatMessage--;

	pinged = false;

	if (!messages.Length()) {
		KEventReset(&notEmpty);
	}

	return true;
}

#endif
