// TODO Replace ES_ERROR_UNKNOWN with proper errors.
// TODO Clean up the return values for system calls; with FATAL_ERRORs there should need to be less error codes returned.
// TODO If a file system call fails with an error indicating the file system is corrupted, or a drive is failing, report the problem to the user.

#ifndef IMPLEMENTATION

KMutex eventForwardMutex;

uintptr_t DoSyscall(EsSyscallType index,
		uintptr_t argument0, uintptr_t argument1,
		uintptr_t argument2, uintptr_t argument3,
		uint64_t flags, bool *fatal, uintptr_t *userStackPointer);

#define DO_SYSCALL_BATCHED (2)

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

#endif

#ifdef IMPLEMENTATION

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

	KEventSet(&notEmpty, false, true);

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

#define CHECK_OBJECT(x) if (!x.valid) SYSCALL_RETURN(ES_FATAL_ERROR_INVALID_HANDLE, !x.softFailure); else x.checked = true
#define SYSCALL_BUFFER_LIMIT (64 * 1024 * 1024) // To prevent overflow and DOS attacks.
#define SYSCALL_BUFFER(address, length, index, write) \
	MMRegion *_region ## index = MMFindAndPinRegion(currentVMM, (address), (length)); \
	if (!_region ## index) SYSCALL_RETURN(ES_FATAL_ERROR_INVALID_BUFFER, true); \
	EsDefer(if (_region ## index) MMUnpinRegion(currentVMM, _region ## index)); \
	if (write && (_region ## index->flags & MM_REGION_READ_ONLY) && (~_region ## index->flags & MM_REGION_COPY_ON_WRITE)) \
		SYSCALL_RETURN(ES_FATAL_ERROR_INVALID_BUFFER, true);
#define SYSCALL_HANDLE(handle, type, __object, variableType) \
	KObject ES_C_PREPROCESSOR_JOIN(_object, __LINE__)(currentProcess, handle, type); \
	CHECK_OBJECT(ES_C_PREPROCESSOR_JOIN(_object, __LINE__)); \
	variableType *const __object = (variableType *) (ES_C_PREPROCESSOR_JOIN(_object, __LINE__)).object
#define SYSCALL_HANDLE_2(handle, _type, out) \
	KObject ES_C_PREPROCESSOR_JOIN(_object, __LINE__)(currentProcess, handle, _type); \
	CHECK_OBJECT(ES_C_PREPROCESSOR_JOIN(_object, __LINE__)); \
	const Handle out = { \
		.object = (ES_C_PREPROCESSOR_JOIN(_object, __LINE__)).object, \
		.flags = (ES_C_PREPROCESSOR_JOIN(_object, __LINE__)).flags, \
		.type = (ES_C_PREPROCESSOR_JOIN(_object, __LINE__)).type, \
	}
#define SYSCALL_READ(destination, source, length) \
	if (!MMArchIsBufferInUserRange(source, length) || !MMArchSafeCopy((uintptr_t) (destination), source, length)) \
		SYSCALL_RETURN(ES_FATAL_ERROR_INVALID_BUFFER, true);
#define SYSCALL_READ_HEAP(destination, source, length) \
	*(void **) &(destination) = EsHeapAllocate((length), false, K_FIXED); \
	if ((length) != 0 && !(destination)) SYSCALL_RETURN(ES_ERROR_INSUFFICIENT_RESOURCES, false); \
	EsDefer(EsHeapFree((destination), (length), K_FIXED)); \
	SYSCALL_READ(destination, source, length);
#define SYSCALL_WRITE(destination, source, length) \
	if (!MMArchIsBufferInUserRange(destination, length) || !MMArchSafeCopy(destination, (uintptr_t) (source), length)) \
		SYSCALL_RETURN(ES_FATAL_ERROR_INVALID_BUFFER, true);
#define SYSCALL_ARGUMENTS uintptr_t argument0, uintptr_t argument1, uintptr_t argument2, uintptr_t argument3, \
		Thread *currentThread, Process *currentProcess, MMSpace *currentVMM, uintptr_t *userStackPointer, bool *fatalError
#define SYSCALL_IMPLEMENT(_type) uintptr_t Do ## _type (SYSCALL_ARGUMENTS) 
#define SYSCALL_RETURN(value, fatal) do { *fatalError = fatal; return (value); } while (0)
#define SYSCALL_PERMISSION(x) do { if ((x) != (currentProcess->permissions & (x))) { *fatalError = true; return ES_FATAL_ERROR_INSUFFICIENT_PERMISSIONS; } } while (0)
typedef uintptr_t (*SyscallFunction)(SYSCALL_ARGUMENTS);
#pragma GCC diagnostic ignored "-Wunused-parameter" push

SYSCALL_IMPLEMENT(ES_SYSCALL_COUNT) {
	SYSCALL_RETURN(ES_FATAL_ERROR_UNKNOWN_SYSCALL, true);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_PRINT) {
	char *buffer; 
	if (argument1 > SYSCALL_BUFFER_LIMIT) SYSCALL_RETURN(ES_FATAL_ERROR_INVALID_BUFFER, true);
	SYSCALL_READ_HEAP(buffer, argument0, argument1);
	EsPrint("%s", argument1, buffer);
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_MEMORY_ALLOCATE) {
	EsMemoryProtection protection = (EsMemoryProtection) argument2;
	uint32_t flags = MM_REGION_USER;
	if (protection == ES_MEMORY_PROTECTION_READ_ONLY) flags |= MM_REGION_READ_ONLY;
	if (protection == ES_MEMORY_PROTECTION_EXECUTABLE) flags |= MM_REGION_EXECUTABLE;
	uintptr_t address = (uintptr_t) MMStandardAllocate(currentVMM, argument0, flags, nullptr, argument1 & ES_MEMORY_RESERVE_COMMIT_ALL);
	SYSCALL_RETURN(address, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_MEMORY_FREE) {
	if (!MMFree(currentVMM, (void *) argument0, argument1, true /* only allow freeing regions marked with MM_REGION_USER */)) {
		SYSCALL_RETURN(ES_FATAL_ERROR_INVALID_MEMORY_REGION, true);
	}

	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_MEMORY_COMMIT) {
	SYSCALL_BUFFER(argument0 << K_PAGE_BITS, argument1 << K_PAGE_BITS, 0, false);

	argument0 -= _region0->baseAddress >> K_PAGE_BITS;

	if (argument0 >= _region0->pageCount 
			|| argument1 > _region0->pageCount - argument0 
			|| (~_region0->flags & MM_REGION_NORMAL) 
			|| (~_region0->flags & MM_REGION_USER) 
			|| !argument1) {
		SYSCALL_RETURN(ES_FATAL_ERROR_INVALID_MEMORY_REGION, true);
	}

	bool success = false;

	if (argument2 == 0) {
		KMutexAcquire(&currentVMM->reserveMutex);
		success = MMCommitRange(currentVMM, _region0, argument0, argument1); 
		KMutexRelease(&currentVMM->reserveMutex);
	} else if (argument2 == 1) {
		KMutexAcquire(&currentVMM->reserveMutex);
		success = MMDecommitRange(currentVMM, _region0, argument0, argument1); 
		KMutexRelease(&currentVMM->reserveMutex);
	} else {
		SYSCALL_RETURN(ES_FATAL_ERROR_UNKNOWN_SYSCALL, true);
	}

	SYSCALL_RETURN(success ? ES_SUCCESS : ES_ERROR_INSUFFICIENT_RESOURCES, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_MEMORY_FAULT_RANGE) {
	uintptr_t start = argument0 & ~(K_PAGE_SIZE - 1);
	uintptr_t end = (argument0 + argument1 - 1) & ~(K_PAGE_SIZE - 1);

	for (uintptr_t page = start; page <= end; page += K_PAGE_SIZE) {
		if (!MMArchHandlePageFault(page, ES_FLAGS_DEFAULT)) {
			SYSCALL_RETURN(ES_FATAL_ERROR_INVALID_BUFFER, true);
		}
	}

	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_PROCESS_CREATE) {
	EsProcessCreationArguments arguments;
	SYSCALL_READ(&arguments, argument0, sizeof(EsProcessCreationArguments));

	EsProcessInformation processInformation;
	EsMemoryZero(&processInformation, sizeof(EsProcessInformation));

	if (arguments.handleCount > 65536) {
		SYSCALL_RETURN(ES_ERROR_INSUFFICIENT_RESOURCES, false);
	}

	// Check the permissions.

	SYSCALL_PERMISSION(ES_PERMISSION_PROCESS_CREATE);

	if (arguments.permissions == ES_PERMISSION_INHERIT) {
		arguments.permissions = currentProcess->permissions;
	}

	if (arguments.permissions & ~currentProcess->permissions) {
		SYSCALL_RETURN(ES_FATAL_ERROR_INSUFFICIENT_PERMISSIONS, true);
	}

	// Check the executable file.

	SYSCALL_HANDLE(arguments.executable, KERNEL_OBJECT_NODE, executableObject, KNode);

	if (executableObject->directoryEntry->type != ES_NODE_FILE) {
		SYSCALL_RETURN(ES_FATAL_ERROR_INCORRECT_NODE_TYPE, true);
	}

	// TODO.

	SYSCALL_RETURN(ES_FATAL_ERROR_UNKNOWN_SYSCALL, true);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_SCREEN_FORCE_UPDATE) {
	if (argument0) {
		SYSCALL_PERMISSION(ES_PERMISSION_SCREEN_MODIFY);
	}

	KMutexAcquire(&windowManager.mutex);

	if (argument0) {
		windowManager.Redraw(ES_POINT(0, 0), graphics.frameBuffer.width, graphics.frameBuffer.height, nullptr);
	}

	GraphicsUpdateScreen();
	KMutexRelease(&windowManager.mutex);

	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_EYEDROP_START) {
	SYSCALL_HANDLE(argument1, KERNEL_OBJECT_WINDOW, avoid, Window);
	windowManager.StartEyedrop(argument0, avoid, argument2);
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_MESSAGE_GET) {
	_EsMessageWithObject message;
	EsMemoryZero(&message, sizeof(_EsMessageWithObject));

	if (currentProcess->messageQueue.GetMessage(&message)) {
		SYSCALL_WRITE(argument0, &message, sizeof(_EsMessageWithObject));
		SYSCALL_RETURN(ES_SUCCESS, false);
	} else {
		SYSCALL_RETURN(ES_ERROR_NO_MESSAGES_AVAILABLE, false);
	}
}

SYSCALL_IMPLEMENT(ES_SYSCALL_MESSAGE_WAIT) {
	currentThread->terminatableState = THREAD_USER_BLOCK_REQUEST;
	KEventWait(&currentProcess->messageQueue.notEmpty, argument0 /* timeout */);
	currentThread->terminatableState = THREAD_IN_SYSCALL;

	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_WINDOW_CREATE) {
	SYSCALL_PERMISSION(ES_PERMISSION_WINDOW_MANAGER);

	if (argument0 == ES_WINDOW_NORMAL) {
		void *_window = windowManager.CreateEmbeddedWindow(currentProcess, (void *) argument2);
		SYSCALL_RETURN(currentProcess->handleTable.OpenHandle(_window, 0, KERNEL_OBJECT_EMBEDDED_WINDOW), false);
	} else {
		void *_window = windowManager.CreateWindow(currentProcess, (void *) argument2, (EsWindowStyle) argument0);

		if (!_window) {
			SYSCALL_RETURN(ES_ERROR_INSUFFICIENT_RESOURCES, false);
		} else {
			SYSCALL_RETURN(currentProcess->handleTable.OpenHandle(_window, 0, KERNEL_OBJECT_WINDOW), false);
		}
	}
}

SYSCALL_IMPLEMENT(ES_SYSCALL_WINDOW_CLOSE) {
	SYSCALL_HANDLE_2(argument0, KERNEL_OBJECT_WINDOW | KERNEL_OBJECT_EMBEDDED_WINDOW, _window);
	KMutexAcquire(&windowManager.mutex);

	if (_window.type == KERNEL_OBJECT_EMBEDDED_WINDOW) {
		EmbeddedWindow *window = (EmbeddedWindow *) _window.object;
		window->Close();
	} else {
		Window *window = (Window *) _window.object;
		window->Close();
	}
	
	KMutexRelease(&windowManager.mutex);
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_WINDOW_SET_PROPERTY) {
	uint8_t property = argument3;

	SYSCALL_HANDLE_2(argument0, (property & 0x80) ? KERNEL_OBJECT_EMBEDDED_WINDOW : KERNEL_OBJECT_WINDOW, _window);
	Window *window = (Window *) _window.object;
	EmbeddedWindow *embed = (EmbeddedWindow *) _window.object;

	if (property == ES_WINDOW_PROPERTY_SOLID) {
		window->solid = (argument1 & ES_WINDOW_SOLID_TRUE) != 0;
		window->noClickActivate = (argument1 & ES_WINDOW_SOLID_NO_ACTIVATE) != 0;
		window->noBringToFront = (argument1 & ES_WINDOW_SOLID_NO_BRING_TO_FRONT) != 0;
		window->solidInsets = ES_RECT_1I(argument2);
		KMutexAcquire(&windowManager.mutex);
		windowManager.MoveCursor(0, 0);
		KMutexRelease(&windowManager.mutex);
	} else if (property == ES_WINDOW_PROPERTY_OPAQUE_BOUNDS) {
		SYSCALL_READ(&window->opaqueBounds, argument1, sizeof(EsRectangle));
	} else if (property == ES_WINDOW_PROPERTY_BLUR_BOUNDS) {
		SYSCALL_READ(&window->blurBounds, argument1, sizeof(EsRectangle));
	} else if (property == ES_WINDOW_PROPERTY_ALPHA) {
		window->alpha = argument1 & 0xFF;
	} else if (property == ES_WINDOW_PROPERTY_FOCUSED) {
		KMutexAcquire(&windowManager.mutex);
		windowManager.ActivateWindow(window);
		windowManager.MoveCursor(0, 0);
		KMutexRelease(&windowManager.mutex);
	} else if (property == ES_WINDOW_PROPERTY_MATERIAL) {
		window->material = argument1;
	} else if (property == ES_WINDOW_PROPERTY_EMBED) {
		SYSCALL_HANDLE(argument1, KERNEL_OBJECT_EMBEDDED_WINDOW | KERNEL_OBJECT_NONE, embed, EmbeddedWindow);
		KMutexAcquire(&windowManager.mutex);
		window->SetEmbed(embed);
		KMutexRelease(&windowManager.mutex);
	} else if (property == ES_WINDOW_PROPERTY_EMBED_INSETS) {
		KMutexAcquire(&windowManager.mutex);
		SYSCALL_READ(&window->embedInsets, argument1, sizeof(EsRectangle));
		window->ResizeEmbed();
		KMutexRelease(&windowManager.mutex);
	} else if (property == ES_WINDOW_PROPERTY_OBJECT) {
		if (embed->owner != currentProcess) {
			// TODO Permissions.
		}

		embed->apiWindow = (void *) argument2;
		__sync_synchronize();

		KMutexAcquire(&windowManager.mutex);
		if (embed->container) embed->container->ResizeEmbed();
		KMutexRelease(&windowManager.mutex);
	} else if (property == ES_WINDOW_PROPERTY_EMBED_OWNER) {
		SYSCALL_HANDLE(argument1, KERNEL_OBJECT_PROCESS, process, Process);
		OpenHandleToObject(embed, KERNEL_OBJECT_EMBEDDED_WINDOW);
		KMutexAcquire(&windowManager.mutex);
		embed->SetEmbedOwner(process);
		KMutexRelease(&windowManager.mutex);
		SYSCALL_RETURN(process->handleTable.OpenHandle(embed, 0, KERNEL_OBJECT_EMBEDDED_WINDOW), false);
	} else if (property == ES_WINDOW_PROPERTY_RESIZE_CLEAR_COLOR) {
		embed->resizeClearColor = argument1;
	} else {
		SYSCALL_RETURN(ES_FATAL_ERROR_OUT_OF_RANGE, true);
	}

	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_WINDOW_REDRAW) {
	SYSCALL_HANDLE(argument0, KERNEL_OBJECT_WINDOW, window, Window);
	KMutexAcquire(&windowManager.mutex);
	window->Update(nullptr, true);
	GraphicsUpdateScreen();
	KMutexRelease(&windowManager.mutex);
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_WINDOW_SET_BITS) {
	SYSCALL_HANDLE_2(argument0, KERNEL_OBJECT_WINDOW | KERNEL_OBJECT_EMBEDDED_WINDOW, _window);

	EsRectangle region;
	SYSCALL_READ(&region, argument1, sizeof(EsRectangle));

	if (!ES_RECT_VALID(region)) {
		SYSCALL_RETURN(ES_SUCCESS, false);
	}

	if (region.l < 0 || region.r > (int32_t) graphics.width * 2
			|| region.t < 0 || region.b > (int32_t) graphics.height * 2
			|| region.l >= region.r || region.t >= region.b) {
		SYSCALL_RETURN(ES_SUCCESS, false);
	}

	bool isEmbed = _window.type == KERNEL_OBJECT_EMBEDDED_WINDOW;
	Window *window = isEmbed ? ((EmbeddedWindow *) _window.object)->container : ((Window *) _window.object);

	if (!window || (isEmbed && currentProcess != ((EmbeddedWindow *) _window.object)->owner)) {
		SYSCALL_RETURN(ES_SUCCESS, false);
	}

	Surface *surface = &window->surface;
	EsRectangle insets = window->embedInsets;

	if (isEmbed) {
		region = Translate(region, insets.l, insets.t);
	}

	if (argument3 == WINDOW_SET_BITS_SCROLL_VERTICAL || argument3 == WINDOW_SET_BITS_SCROLL_HORIZONTAL) {
		ptrdiff_t scrollDelta = argument2;
		bool scrollVertical = argument3 == WINDOW_SET_BITS_SCROLL_VERTICAL;

		if (scrollVertical) {
			if (scrollDelta < 0) region.b += scrollDelta;
			else region.t += scrollDelta;
		} else {
			if (scrollDelta < 0) region.r += scrollDelta;
			else region.l += scrollDelta;
		}

		KMutexAcquire(&windowManager.mutex);

		if (window->closed 
				|| region.l < 0 || region.r > (int32_t) surface->width
				|| region.t < 0 || region.b > (int32_t) surface->height
				|| region.l >= region.r || region.t >= region.b) {
		} else {
			surface->Scroll(region, scrollDelta, scrollVertical);
			window->Update(&region, true);
			window->queuedScrollUpdate = true;
			// Don't update the screen until the rest of the window is painted.
		}

		KMutexRelease(&windowManager.mutex);
	} else {
		bool skipUpdate = false;
		SYSCALL_BUFFER(argument2, Width(region) * Height(region) * 4, 1, false);
		KMutexAcquire(&windowManager.mutex);

		bool resizeQueued = false;

		if (argument3 == WINDOW_SET_BITS_AFTER_RESIZE && windowManager.resizeWindow == window) {
			if (isEmbed) windowManager.resizeReceivedBitsFromEmbed = true;
			else windowManager.resizeReceivedBitsFromContainer = true;

			if (windowManager.resizeReceivedBitsFromContainer && windowManager.resizeReceivedBitsFromEmbed) {
				// Resize complete.
				resizeQueued = windowManager.resizeQueued;
				windowManager.resizeQueued = false;
				windowManager.resizeWindow = nullptr;
				windowManager.resizeSlow = KGetTimeInMs() - windowManager.resizeStartTimeStampMs >= RESIZE_SLOW_THRESHOLD
					|| windowManager.inspectorWindowCount /* HACK anti-flicker logic interfers with the inspector's logging */;

#if 0
				EsPrint("Resize complete in %dms%z.\n", KGetTimeInMs() - windowManager.resizeStartTimeStampMs, windowManager.resizeSlow ? " (slow)" : "");
#endif
			}
		}

		if (window->closed) {
			skipUpdate = true;
		} else {
			uintptr_t stride = Width(region) * 4;
			EsRectangle clippedRegion = EsRectangleIntersection(region, ES_RECT_2S(surface->width, surface->height));

			if (argument3 != WINDOW_SET_BITS_AFTER_RESIZE) {
				skipUpdate = window->UpdateDirect((K_USER_BUFFER uint32_t *) argument2, stride, clippedRegion);
			}

#define SET_BITS_REGION(...) { \
	EsRectangle subRegion = EsRectangleIntersection(clippedRegion, ES_RECT_4(__VA_ARGS__)); \
	if (ES_RECT_VALID(subRegion)) { surface->SetBits((K_USER_BUFFER const uint8_t *) argument2 \
	+ stride * (subRegion.t - clippedRegion.t) + 4 * (subRegion.l - clippedRegion.l), stride, subRegion); } }

			if (window->style == ES_WINDOW_CONTAINER && !isEmbed) {
				SET_BITS_REGION(0, window->width, 0, insets.t);
				SET_BITS_REGION(0, insets.l, insets.t, window->height - insets.b);
				SET_BITS_REGION(window->width - insets.r, window->width, insets.t, window->height - insets.b);
				SET_BITS_REGION(0, window->width, window->height - insets.b, window->height);
			} else if (window->style == ES_WINDOW_CONTAINER && isEmbed) {
				SET_BITS_REGION(insets.l, window->width - insets.r, insets.t, window->height - insets.b);
			} else {
				SET_BITS_REGION(0, window->width, 0, window->height);
			}
		}

		window->Update(&region, !skipUpdate);

		if (!skipUpdate || window->queuedScrollUpdate) {
			window->queuedScrollUpdate = false;
			GraphicsUpdateScreen();
		}

		if (resizeQueued) {
			window->Move(windowManager.resizeQueuedRectangle, ES_WINDOW_MOVE_DYNAMIC);
		}

		KMutexRelease(&windowManager.mutex);
	}

	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_EVENT_CREATE) {
	KEvent *event = (KEvent *) EsHeapAllocate(sizeof(KEvent), true, K_FIXED);
	if (!event) SYSCALL_RETURN(ES_ERROR_INSUFFICIENT_RESOURCES, false);
	event->handles = 1;
	event->autoReset = argument0;
	SYSCALL_RETURN(currentProcess->handleTable.OpenHandle(event, 0, KERNEL_OBJECT_EVENT), false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_HANDLE_CLOSE) {
	if (!currentProcess->handleTable.CloseHandle(argument0)) {
		SYSCALL_RETURN(ES_FATAL_ERROR_INVALID_HANDLE, true);
	}

	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_THREAD_TERMINATE) {
	bool self = false;

	{
		SYSCALL_HANDLE(argument0, KERNEL_OBJECT_THREAD, thread, Thread);
		if (thread == currentThread) self = true;
		else scheduler.TerminateThread(thread);
	}

	if (self) scheduler.TerminateThread(currentThread);

	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_PROCESS_TERMINATE) {
	// TODO Prevent the termination of the kernel/desktop.

	bool self = false;

	{
		SYSCALL_HANDLE(argument0, KERNEL_OBJECT_PROCESS, process, Process);
		if (process == currentProcess) self = true;
		else scheduler.TerminateProcess(process, argument1);
	}

	if (self) scheduler.TerminateProcess(currentProcess, argument1);

	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_THREAD_CREATE) {
	EsThreadInformation thread;
	EsMemoryZero(&thread, sizeof(EsThreadInformation));
	Thread *threadObject = scheduler.SpawnThread("Syscall", argument0, argument3, SPAWN_THREAD_USERLAND, currentProcess, argument1);

	if (!threadObject) {
		SYSCALL_RETURN(ES_ERROR_INSUFFICIENT_RESOURCES, false);
	}

	thread.tid = threadObject->id;

	thread.handle = currentProcess->handleTable.OpenHandle(threadObject, 0, KERNEL_OBJECT_THREAD); 

	SYSCALL_WRITE(argument2, &thread, sizeof(EsThreadInformation));
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_MEMORY_OPEN) {
	if (argument0 > ES_SHARED_MEMORY_MAXIMUM_SIZE) SYSCALL_RETURN(ES_FATAL_ERROR_OUT_OF_RANGE, true);
	if (argument1 && !argument2) SYSCALL_RETURN(ES_FATAL_ERROR_INVALID_BUFFER, true);
	if (argument2 > ES_SHARED_MEMORY_NAME_MAX_LENGTH) SYSCALL_RETURN(ES_FATAL_ERROR_OUT_OF_RANGE, true);

	char *name;
	if (argument2 > SYSCALL_BUFFER_LIMIT) SYSCALL_RETURN(ES_FATAL_ERROR_INVALID_BUFFER, true);
	SYSCALL_READ_HEAP(name, argument1, argument2);

	MMSharedRegion *region = MMSharedOpenRegion(name, argument2, argument0, argument3);
	if (!region) SYSCALL_RETURN(ES_INVALID_HANDLE, false);

	SYSCALL_RETURN(currentProcess->handleTable.OpenHandle(region, 0, KERNEL_OBJECT_SHMEM), false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_MEMORY_MAP_OBJECT) {
	SYSCALL_HANDLE_2(argument0, KERNEL_OBJECT_SHMEM | KERNEL_OBJECT_NODE, object);

	if (object.type == KERNEL_OBJECT_SHMEM) {
		// TODO Access permissions and modes.
		MMSharedRegion *region = (MMSharedRegion *) object.object;

		if (argument2 == ES_MAP_OBJECT_ALL) {
			argument2 = region->sizeBytes;
		}

		uintptr_t address = (uintptr_t) MMMapShared(currentVMM, region, argument1, argument2, MM_REGION_USER);
		SYSCALL_RETURN(address, false);
	} else if (object.type == KERNEL_OBJECT_NODE) {
		KNode *file = (KNode *) object.object;

		if (file->directoryEntry->type != ES_NODE_FILE) SYSCALL_RETURN(ES_FATAL_ERROR_INCORRECT_NODE_TYPE, true);

		if (argument3 == ES_MAP_OBJECT_READ_WRITE) {
			if (!(object.flags & (ES_FILE_WRITE_SHARED | ES_FILE_WRITE))) {
				SYSCALL_RETURN(ES_FATAL_ERROR_INCORRECT_FILE_ACCESS, true);
			}
		} else {
			if (!(object.flags & (ES_FILE_READ | ES_FILE_READ_SHARED))) {
				SYSCALL_RETURN(ES_FATAL_ERROR_INCORRECT_FILE_ACCESS, true);
			}
		}

		if (argument2 == ES_MAP_OBJECT_ALL) {
			argument2 = file->directoryEntry->totalSize;
		}

		uintptr_t address = (uintptr_t) MMMapFile(currentVMM, (FSFile *) file, argument1, argument2, argument3, nullptr, 0, MM_REGION_USER);
		SYSCALL_RETURN(address, false);
	}

	KernelPanic("ES_SYSCALL_MEMORY_MAP_OBJECT - Unhandled case.\n");
	SYSCALL_RETURN(0, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_CONSTANT_BUFFER_CREATE) {
	if (argument2 > SYSCALL_BUFFER_LIMIT) SYSCALL_RETURN(ES_FATAL_ERROR_INVALID_BUFFER, true);
	SYSCALL_BUFFER(argument0, argument2, 1, false);
	SYSCALL_HANDLE(argument1, KERNEL_OBJECT_PROCESS, process, Process);
	SYSCALL_RETURN(MakeConstantBuffer((void *) argument0, argument2, process), false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_HANDLE_SHARE) {
	SYSCALL_HANDLE_2(argument0, KERNEL_OBJECT_SHMEM | KERNEL_OBJECT_CONSTANT_BUFFER | KERNEL_OBJECT_PROCESS 
			| KERNEL_OBJECT_DEVICE | KERNEL_OBJECT_NODE | KERNEL_OBJECT_EVENT | KERNEL_OBJECT_PIPE, share);
	SYSCALL_HANDLE(argument1, KERNEL_OBJECT_PROCESS, process, Process);
	uint32_t sharedFlags = share.flags;

	if (share.type == KERNEL_OBJECT_SHMEM) {
		sharedFlags = argument2; // TODO Sort out flags.
	} else if (share.type == KERNEL_OBJECT_NODE) {
		sharedFlags = (argument2 & 1) && (share.flags & (ES_FILE_WRITE_SHARED | ES_FILE_WRITE)) ? ES_FILE_READ_SHARED : share.flags;
	} else if (share.type == KERNEL_OBJECT_PIPE) {
		// TODO Sort out flags.
	}

	if (!OpenHandleToObject(share.object, share.type, sharedFlags)) {
		SYSCALL_RETURN(ES_ERROR_PERMISSION_NOT_GRANTED, false);
	} else {
		SYSCALL_RETURN(process->handleTable.OpenHandle(share.object, sharedFlags, share.type), false);
	}
}

SYSCALL_IMPLEMENT(ES_SYSCALL_VOLUME_GET_INFORMATION) {
	if (~currentProcess->permissions & ES_PERMISSION_GET_VOLUME_INFORMATION) {
		SYSCALL_RETURN(0, false);
	}

	SYSCALL_HANDLE(argument0, KERNEL_OBJECT_NODE, node, KNode);
	KFileSystem *fileSystem = node->fileSystem;

	EsVolumeInformation information;
	EsMemoryZero(&information, sizeof(EsVolumeInformation));
	EsMemoryCopy(information.label, fileSystem->name, sizeof(fileSystem->name));
	information.labelBytes = fileSystem->nameBytes;
	information.driveType = fileSystem->block->driveType;
	information.spaceUsed = fileSystem->spaceUsed;
	information.spaceTotal = fileSystem->spaceTotal;
	information.id = fileSystem->objectID;
	information.flags = fileSystem->write ? ES_FLAGS_DEFAULT : ES_VOLUME_READ_ONLY;

	SYSCALL_WRITE(argument1, &information, sizeof(EsVolumeInformation));
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_NODE_OPEN) {
	size_t pathLength = (size_t) argument1;
	uint64_t flags = (uint64_t) argument2;

	flags &= ~_ES_NODE_FROM_WRITE_EXCLUSIVE | _ES_NODE_NO_WRITE_BASE;

	bool needWritePermission = flags & (ES_FILE_WRITE | ES_FILE_WRITE_SHARED | _ES_NODE_DIRECTORY_WRITE);

	char *path;
	if (argument1 > K_MAX_PATH) SYSCALL_RETURN(ES_FATAL_ERROR_OUT_OF_RANGE, true);
	SYSCALL_READ_HEAP(path, argument0, argument1);

	_EsNodeInformation information;
	SYSCALL_READ(&information, argument3, sizeof(_EsNodeInformation));

	SYSCALL_HANDLE_2(information.handle, KERNEL_OBJECT_NODE, _directory);
	KNode *directory = (KNode *) _directory.object; 

	if (directory->directoryEntry->type != ES_NODE_DIRECTORY) {
		SYSCALL_RETURN(ES_FATAL_ERROR_INCORRECT_NODE_TYPE, true);
	}

	if ((~_directory.flags & _ES_NODE_DIRECTORY_WRITE) && needWritePermission) {
		SYSCALL_RETURN(ES_ERROR_PERMISSION_NOT_GRANTED, false);
	}

	if (~_directory.flags & _ES_NODE_DIRECTORY_WRITE) {
		flags |= _ES_NODE_NO_WRITE_BASE;
	}

	KNodeInformation _information = FSNodeOpen(path, pathLength, flags, directory);

	if (!_information.node) {
		SYSCALL_RETURN(_information.error, false);
	}

	if (flags & ES_FILE_WRITE) {
		// Mark this handle as being the exclusive writer for this file.
		// This way, when the handle is used, OpenHandleToObject succeeds.
		// The exclusive writer flag will only be removed from the file where countWrite drops to zero.
		flags |= _ES_NODE_FROM_WRITE_EXCLUSIVE;
	}

	EsMemoryZero(&information, sizeof(_EsNodeInformation));
	information.type = _information.node->directoryEntry->type;
	information.fileSize = _information.node->directoryEntry->totalSize;
	information.directoryChildren = _information.node->directoryEntry->directoryChildren;
	information.handle = currentProcess->handleTable.OpenHandle(_information.node, flags, KERNEL_OBJECT_NODE);
	SYSCALL_WRITE(argument3, &information, sizeof(_EsNodeInformation));

	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_NODE_DELETE) {
	SYSCALL_HANDLE_2(argument0, KERNEL_OBJECT_NODE, handle);
	KNode *node = (KNode *) handle.object; 

	if (handle.flags & _ES_NODE_NO_WRITE_BASE) {
		SYSCALL_RETURN(ES_ERROR_PERMISSION_NOT_GRANTED, false);
	}
	
	if (node->directoryEntry->type == ES_NODE_FILE && (~handle.flags & ES_FILE_WRITE)) {
		SYSCALL_RETURN(ES_FATAL_ERROR_INCORRECT_FILE_ACCESS, true);
	}

	if (node->directoryEntry->type == ES_NODE_DIRECTORY || node->directoryEntry->type == ES_NODE_FILE) {
		SYSCALL_RETURN(FSNodeDelete(node), false);
	} else {
		SYSCALL_RETURN(ES_FATAL_ERROR_INCORRECT_NODE_TYPE, true);
	}
}

SYSCALL_IMPLEMENT(ES_SYSCALL_NODE_MOVE) {
	SYSCALL_HANDLE(argument0, KERNEL_OBJECT_NODE, file, KNode);
	SYSCALL_HANDLE(argument1, KERNEL_OBJECT_NODE | KERNEL_OBJECT_NONE, directory, KNode);
	char *newPath;
	if (argument3 > SYSCALL_BUFFER_LIMIT) SYSCALL_RETURN(ES_FATAL_ERROR_INVALID_BUFFER, true);
	SYSCALL_READ_HEAP(newPath, argument2, argument3);
	SYSCALL_RETURN(FSNodeMove(file, directory, newPath, (size_t) argument3), false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_FILE_READ_SYNC) {
	if (!argument2) SYSCALL_RETURN(0, false);

	SYSCALL_HANDLE(argument0, KERNEL_OBJECT_NODE, file, KNode);

	if (file->directoryEntry->type != ES_NODE_FILE) SYSCALL_RETURN(ES_FATAL_ERROR_INCORRECT_NODE_TYPE, true);

	SYSCALL_BUFFER(argument3, argument2, 1, false);

	size_t result = FSFileReadSync(file, (void *) argument3, argument1, argument2, 
			(_region1->flags & MM_REGION_FILE) ? FS_FILE_ACCESS_USER_BUFFER_MAPPED : 0);
	SYSCALL_RETURN(result, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_FILE_WRITE_SYNC) {
	if (!argument2) SYSCALL_RETURN(0, false);
		
	SYSCALL_HANDLE_2(argument0, KERNEL_OBJECT_NODE, handle);
	KNode *file = (KNode *) handle.object; 

	if (file->directoryEntry->type != ES_NODE_FILE) SYSCALL_RETURN(ES_FATAL_ERROR_INCORRECT_NODE_TYPE, true);

	SYSCALL_BUFFER(argument3, argument2, 1, true /* write */);

	if (handle.flags & (ES_FILE_WRITE_SHARED | ES_FILE_WRITE)) {
		size_t result = FSFileWriteSync(file, (void *) argument3, argument1, argument2, 
				(_region1->flags & MM_REGION_FILE) ? FS_FILE_ACCESS_USER_BUFFER_MAPPED : 0);
		SYSCALL_RETURN(result, false);
	} else {
		SYSCALL_RETURN(ES_FATAL_ERROR_INCORRECT_FILE_ACCESS, true);
	}
}

SYSCALL_IMPLEMENT(ES_SYSCALL_FILE_GET_SIZE) {
	SYSCALL_HANDLE(argument0, KERNEL_OBJECT_NODE, file, KNode);
	if (file->directoryEntry->type != ES_NODE_FILE) SYSCALL_RETURN(ES_FATAL_ERROR_INCORRECT_NODE_TYPE, true);
	SYSCALL_RETURN(file->directoryEntry->totalSize, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_FILE_RESIZE) {
	SYSCALL_HANDLE_2(argument0, KERNEL_OBJECT_NODE, handle);
	KNode *file = (KNode *) handle.object; 

	if (file->directoryEntry->type != ES_NODE_FILE) SYSCALL_RETURN(ES_FATAL_ERROR_INCORRECT_NODE_TYPE, true);

	if (handle.flags & (ES_FILE_WRITE_SHARED | ES_FILE_WRITE)) {
		SYSCALL_RETURN(FSFileResize(file, argument1), false);
	} else {
		SYSCALL_RETURN(ES_FATAL_ERROR_INCORRECT_FILE_ACCESS, true);
	}
}
					     
SYSCALL_IMPLEMENT(ES_SYSCALL_EVENT_SET) {
	SYSCALL_HANDLE(argument0, KERNEL_OBJECT_EVENT, event, KEvent);
	KEventSet(event, false, true);
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_EVENT_RESET) {
	SYSCALL_HANDLE(argument0, KERNEL_OBJECT_EVENT, event, KEvent);
	KEventReset(event);
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_SLEEP) {
	KTimer timer = {};
	KTimerSet(&timer, (argument0 << 32) | argument1);
	currentThread->terminatableState = THREAD_USER_BLOCK_REQUEST;
	KEventWait(&timer.event, ES_WAIT_NO_TIMEOUT);
	currentThread->terminatableState = THREAD_IN_SYSCALL;
	KTimerRemove(&timer);
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_WAIT) {
	if (argument1 >= ES_MAX_WAIT_COUNT - 1 /* leave room for timeout timer */) {
		SYSCALL_RETURN(ES_FATAL_ERROR_OUT_OF_RANGE, true);
	}

	EsHandle handles[ES_MAX_WAIT_COUNT];
	SYSCALL_READ(handles, argument0, argument1 * sizeof(EsHandle));

	KEvent *events[ES_MAX_WAIT_COUNT];
	KObject _objects[ES_MAX_WAIT_COUNT] = {};

	for (uintptr_t i = 0; i < argument1; i++) {
		_objects[i].Initialise(&currentProcess->handleTable, handles[i], 
				KERNEL_OBJECT_PROCESS | KERNEL_OBJECT_THREAD | KERNEL_OBJECT_EVENT | KERNEL_OBJECT_EVENT_SINK);
		CHECK_OBJECT(_objects[i]);

		void *object = _objects[i].object;

		switch (_objects[i].type) {
			case KERNEL_OBJECT_PROCESS: {
				events[i] = &((Process *) object)->killedEvent;
			} break;

			case KERNEL_OBJECT_THREAD: {
				events[i] = &((Thread *) object)->killedEvent;
			} break;

			case KERNEL_OBJECT_EVENT_SINK: {
				events[i] = &((EventSink *) object)->available;
			} break;

			case KERNEL_OBJECT_EVENT: {
				events[i] = (KEvent *) object;
			} break;

			default: {
				KernelPanic("DoSyscall - Unexpected wait object type %d.\n", _objects[i].type);
			} break;
		}
	}

	size_t waitObjectCount = argument1;
	KTimer timer = {};

	if (argument2 != (uintptr_t) ES_WAIT_NO_TIMEOUT) {
		KTimerSet(&timer, argument2);
		events[waitObjectCount++] = &timer.event;
	}

	uintptr_t waitReturnValue;
	currentThread->terminatableState = THREAD_USER_BLOCK_REQUEST;
	waitReturnValue = scheduler.WaitEvents(events, waitObjectCount);
	currentThread->terminatableState = THREAD_IN_SYSCALL;

	if (waitReturnValue == argument1) {
		waitReturnValue = ES_ERROR_TIMEOUT_REACHED;
	}

	if (argument2 != (uintptr_t) ES_WAIT_NO_TIMEOUT) {
		KTimerRemove(&timer);
	}

	SYSCALL_RETURN(waitReturnValue, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_WINDOW_SET_CURSOR) {
	SYSCALL_HANDLE_2(argument0, KERNEL_OBJECT_WINDOW | KERNEL_OBJECT_EMBEDDED_WINDOW, _window);

	uint32_t imageWidth = (argument2 >> 16) & 0xFF;
	uint32_t imageHeight = (argument2 >> 24) & 0xFF;

	SYSCALL_BUFFER(argument1, imageWidth * imageHeight * 4, 1, false);

	KMutexAcquire(&windowManager.mutex);
	Window *window;

	if (_window.type == KERNEL_OBJECT_EMBEDDED_WINDOW) {
		EmbeddedWindow *embeddedWindow = (EmbeddedWindow *) _window.object;
		window = embeddedWindow->container;

		if (!window || !window->hoveringOverEmbed || embeddedWindow->owner != currentProcess) {
			KMutexRelease(&windowManager.mutex);
			SYSCALL_RETURN(ES_SUCCESS, false);
		}
	} else {
		window = (Window *) _window.object;

		if (window->hoveringOverEmbed) {
			KMutexRelease(&windowManager.mutex);
			SYSCALL_RETURN(ES_SUCCESS, false);
		}
	}

	bool changedCursor = false;
	bool different = argument1 != windowManager.cursorID || windowManager.cursorShadow != !!(argument3 & (1 << 30));

	if (!window->closed && different && !windowManager.eyedropping && (windowManager.hoverWindow == window || !windowManager.hoverWindow)) {
		windowManager.cursorID = argument1;
		windowManager.cursorImageOffsetX = (int8_t) ((argument2 >> 0) & 0xFF);
		windowManager.cursorImageOffsetY = (int8_t) ((argument2 >> 8) & 0xFF);
		windowManager.cursorXOR = argument3 & (1 << 31);
		windowManager.cursorShadow = argument3 & (1 << 30);

		int width = imageWidth + CURSOR_SHADOW_OFFSET;
		int height = imageHeight + CURSOR_SHADOW_OFFSET;

		if (windowManager.cursorSurface.Resize(width, height)
				&& windowManager.cursorSwap.Resize(width, height)
				&& windowManager.cursorTemporary.Resize(width, height)) {
			windowManager.cursorSurface.SetBits((K_USER_BUFFER const void *) argument1, argument3 & 0xFFFFFF, ES_RECT_2S(imageWidth, imageHeight));

			if (windowManager.cursorShadow) {
				windowManager.cursorSurface.CreateCursorShadow(&windowManager.cursorTemporary);
			}
		}

		windowManager.changedCursorImage = true;
		changedCursor = true;
	}

	KMutexRelease(&windowManager.mutex);

	SYSCALL_RETURN(changedCursor, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_WINDOW_MOVE) {
	SYSCALL_HANDLE(argument0, KERNEL_OBJECT_WINDOW, window, Window);

	bool success = true;

	EsRectangle rectangle;

	if (argument1) {
		SYSCALL_READ(&rectangle, argument1, sizeof(EsRectangle));
	} else {
		EsMemoryZero(&rectangle, sizeof(EsRectangle));
	}

	KMutexAcquire(&windowManager.mutex);

	if (argument3 & ES_WINDOW_MOVE_HIDDEN) {
		windowManager.HideWindow(window);
	} else {
		window->Move(rectangle, argument3);
	}

	if (argument3 & ES_WINDOW_MOVE_UPDATE_SCREEN) {
		GraphicsUpdateScreen();
	}

	KMutexRelease(&windowManager.mutex);

	SYSCALL_RETURN(success ? ES_SUCCESS : ES_ERROR_INVALID_DIMENSIONS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_WINDOW_TRANSFER_PRESS) {
	SYSCALL_HANDLE(argument0, KERNEL_OBJECT_WINDOW, oldWindow, Window);
	SYSCALL_HANDLE(argument1, KERNEL_OBJECT_WINDOW, newWindow, Window);

	KMutexAcquire(&windowManager.mutex);
	
	if (windowManager.pressedWindow == oldWindow) {
		windowManager.pressedWindow = newWindow;
		newWindow->hoveringOverEmbed = false;
	}

	KMutexRelease(&windowManager.mutex);

	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_WINDOW_FIND_BY_POINT) {
	SYSCALL_PERMISSION(ES_PERMISSION_SCREEN_MODIFY);
	KMutexAcquire(&windowManager.mutex);
	Window *window = windowManager.FindWindowAtPosition(argument1 /* x */, argument2 /* y */, argument3 /* exclude */);
	EsObjectID id = window ? window->id : 0;
	KMutexRelease(&windowManager.mutex);
	SYSCALL_WRITE(argument0, &id, sizeof(id));
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_CURSOR_POSITION_GET) {
	EsPoint point = ES_POINT(windowManager.cursorX, windowManager.cursorY);
	SYSCALL_WRITE(argument0, &point, sizeof(EsPoint));
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_CURSOR_POSITION_SET) {
	KMutexAcquire(&windowManager.mutex);

	// Only allow the cursor position to be modified by the process
	// if it owns the window that is currently being pressed.

	bool allowed = false;

	if (windowManager.pressedWindow) {
		if (windowManager.pressedWindow->embed) {
			if (windowManager.pressedWindow->embed->owner == currentProcess) {
				allowed = true;
			}
		}

		if (windowManager.pressedWindow->owner == currentProcess) {
			allowed = true;
		}
	}

	if (allowed) {
		// Preseve the precise offset.
		int32_t offsetX = windowManager.cursorXPrecise - windowManager.cursorX * K_CURSOR_MOVEMENT_SCALE;
		int32_t offsetY = windowManager.cursorYPrecise - windowManager.cursorY * K_CURSOR_MOVEMENT_SCALE;
		windowManager.cursorX = argument0;
		windowManager.cursorY = argument1;
		windowManager.cursorXPrecise = argument0 * K_CURSOR_MOVEMENT_SCALE + offsetX;
		windowManager.cursorYPrecise = argument1 * K_CURSOR_MOVEMENT_SCALE + offsetY;
	}

	KMutexRelease(&windowManager.mutex);
	SYSCALL_RETURN(allowed ? ES_SUCCESS : ES_ERROR_PERMISSION_NOT_GRANTED, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_CURSOR_PROPERTIES_SET) {
	SYSCALL_PERMISSION(ES_PERMISSION_SCREEN_MODIFY);
	KMutexAcquire(&windowManager.mutex);
	windowManager.cursorProperties = argument0;
	KMutexRelease(&windowManager.mutex);
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_GAME_CONTROLLER_STATE_POLL) {
	EsGameControllerState gameControllers[ES_GAME_CONTROLLER_MAX_COUNT];
	size_t gameControllerCount;

	KMutexAcquire(&windowManager.gameControllersMutex);
	gameControllerCount = windowManager.gameControllerCount;
	EsMemoryCopy(gameControllers, windowManager.gameControllers, sizeof(EsGameControllerState) * gameControllerCount);
	KMutexRelease(&windowManager.gameControllersMutex);

	SYSCALL_WRITE(argument0, gameControllers, sizeof(EsGameControllerState) * gameControllerCount);
	SYSCALL_RETURN(gameControllerCount, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_WINDOW_GET_BOUNDS) {
	SYSCALL_HANDLE_2(argument0, KERNEL_OBJECT_WINDOW | KERNEL_OBJECT_EMBEDDED_WINDOW, _window);

	EsRectangle rectangle;
	EsMemoryZero(&rectangle, sizeof(EsRectangle));
	KMutexAcquire(&windowManager.mutex);

	if (_window.type == KERNEL_OBJECT_WINDOW) {
		Window *window = (Window *) _window.object;
		rectangle.l = window->position.x;
		rectangle.t = window->position.y;
		rectangle.r = window->position.x + window->width;
		rectangle.b = window->position.y + window->height;
	} else if (_window.type == KERNEL_OBJECT_EMBEDDED_WINDOW) {
		EmbeddedWindow *embed = (EmbeddedWindow *) _window.object;
		Window *window = embed->container;

		if (window) {
			rectangle.l = window->position.x + window->embedInsets.l;
			rectangle.t = window->position.y + window->embedInsets.t;
			rectangle.r = window->position.x + window->width - window->embedInsets.r;
			rectangle.b = window->position.y + window->height - window->embedInsets.b;
		}
	}

	KMutexRelease(&windowManager.mutex);
	SYSCALL_WRITE(argument1, &rectangle, sizeof(EsRectangle));
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_WINDOW_GET_EMBED_KEYBOARD) {
	SYSCALL_HANDLE(argument0, KERNEL_OBJECT_WINDOW, window, Window);
	_EsMessageWithObject m;
	EsMemoryZero(&m, sizeof(_EsMessageWithObject));
	KMutexAcquire(&windowManager.mutex);
	m.object = window->apiWindow;
	EsMemoryCopy(&m.message, &window->lastEmbedKeyboardMessage, sizeof(EsMessage));
	window->lastEmbedKeyboardMessage.type = ES_MSG_INVALID;
	KMutexRelease(&windowManager.mutex);
	SYSCALL_WRITE(argument1, &m, sizeof(_EsMessageWithObject));
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_PROCESS_PAUSE) {
	SYSCALL_HANDLE(argument0, KERNEL_OBJECT_PROCESS, process, Process);
	scheduler.PauseProcess(process, (bool) argument1);
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_PROCESS_CRASH) {
	KernelLog(LOG_ERROR, "Syscall", "process crash request", "Process crash request, reason %d\n", argument0);
	SYSCALL_RETURN(argument0, true);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_MESSAGE_POST) {
	SYSCALL_HANDLE(argument2, KERNEL_OBJECT_PROCESS, process, Process);

	_EsMessageWithObject message;
	SYSCALL_READ(&message.message, argument0, sizeof(EsMessage));
	message.object = (void *) argument1;

	if (process->messageQueue.SendMessage(&message)) {
		SYSCALL_RETURN(ES_SUCCESS, false);
	} else {
		SYSCALL_RETURN(ES_ERROR_MESSAGE_QUEUE_FULL, false);
	}
}

SYSCALL_IMPLEMENT(ES_SYSCALL_THREAD_GET_ID) {
	SYSCALL_HANDLE_2(argument0, KERNEL_OBJECT_THREAD | KERNEL_OBJECT_PROCESS, object);

	if (object.type == KERNEL_OBJECT_THREAD) {
		SYSCALL_RETURN(((Thread *) object.object)->id, false);
	} else if (object.type == KERNEL_OBJECT_PROCESS) {
		Process *process = (Process *) object.object;

#ifdef ENABLE_POSIX_SUBSYSTEM
		if (currentThread->posixData && currentThread->posixData->forkProcess) {
			SYSCALL_RETURN(currentThread->posixData->forkProcess->id, false);
		}
#endif

		SYSCALL_RETURN(process->id, false);
	}

	KernelPanic("ES_SYSCALL_THREAD_GET_ID - Unhandled case.\n");
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_THREAD_STACK_SIZE) {
	SYSCALL_HANDLE(argument0, KERNEL_OBJECT_THREAD, thread, Thread);

	SYSCALL_WRITE(argument1, &thread->userStackCommit, sizeof(thread->userStackCommit));
	SYSCALL_WRITE(argument2, &thread->userStackReserve, sizeof(thread->userStackReserve));

	bool success = true;

	if (argument3) {
		MMRegion *region = MMFindAndPinRegion(currentVMM, thread->userStackBase, thread->userStackReserve);
		KMutexAcquire(&currentVMM->reserveMutex);

		if (thread->userStackCommit <= argument3 && argument3 <= thread->userStackReserve && !(argument3 & (K_PAGE_BITS - 1)) && region) {
#ifdef K_STACK_GROWS_DOWN
			success = MMCommitRange(currentVMM, region, (thread->userStackReserve - argument3) / K_PAGE_SIZE, argument3 / K_PAGE_SIZE); 
#else
			success = MMCommitRange(currentVMM, region, 0, argument3 / K_PAGE_SIZE); 
#endif
		} else {
			success = false;
		}

		if (success) thread->userStackCommit = argument3;
		KMutexRelease(&currentVMM->reserveMutex);
		if (region) MMUnpinRegion(currentVMM, region);
	}

	SYSCALL_RETURN(success ? ES_SUCCESS : ES_ERROR_INSUFFICIENT_RESOURCES, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_DIRECTORY_ENUMERATE) {
	SYSCALL_HANDLE(argument0, KERNEL_OBJECT_NODE, node, KNode);
	
	if (node->directoryEntry->type != ES_NODE_DIRECTORY) SYSCALL_RETURN(ES_FATAL_ERROR_INCORRECT_NODE_TYPE, true);

	if (argument2 > SYSCALL_BUFFER_LIMIT / sizeof(EsDirectoryChild)) SYSCALL_RETURN(ES_FATAL_ERROR_INVALID_BUFFER, true);
	SYSCALL_BUFFER(argument1, argument2 * sizeof(EsDirectoryChild), 1, true /* write */);

	SYSCALL_RETURN(FSDirectoryEnumerateChildren(node, (K_USER_BUFFER EsDirectoryChild *) argument1, argument2), false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_FILE_CONTROL) {
	SYSCALL_HANDLE(argument0, KERNEL_OBJECT_NODE, file, KNode);

	if (file->directoryEntry->type != ES_NODE_FILE) {
		SYSCALL_RETURN(ES_FATAL_ERROR_INCORRECT_NODE_TYPE, true);
	}

	SYSCALL_RETURN(FSFileControl(file, argument1), false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_BATCH) {
	EsBatchCall *calls;
	if (argument1 > SYSCALL_BUFFER_LIMIT / sizeof(EsBatchCall)) SYSCALL_RETURN(ES_FATAL_ERROR_INVALID_BUFFER, true);
	SYSCALL_READ_HEAP(calls, argument0, sizeof(EsBatchCall) * argument1);

	size_t count = argument1;

	for (uintptr_t i = 0; i < count; i++) {
		EsBatchCall call = calls[i];
		bool fatal = false;
		uintptr_t _returnValue = calls[i].returnValue = DoSyscall(call.index, call.argument0, call.argument1, call.argument2, call.argument3, 
				DO_SYSCALL_BATCHED, &fatal, userStackPointer);
		if (fatal) SYSCALL_RETURN(_returnValue, true);
		if (calls->stopBatchIfError && ES_CHECK_ERROR(_returnValue)) break;
		if (currentThread->terminating) break;
	}

	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_CONSTANT_BUFFER_READ) {
	SYSCALL_HANDLE(argument0, KERNEL_OBJECT_CONSTANT_BUFFER, buffer, ConstantBuffer);
	if (!argument1) SYSCALL_RETURN(buffer->bytes, false);
	SYSCALL_WRITE(argument1, buffer + 1, buffer->bytes);
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_PROCESS_GET_STATE) {
	SYSCALL_HANDLE(argument0, KERNEL_OBJECT_PROCESS, process, Process);

	EsProcessState state;
	EsMemoryZero(&state, sizeof(EsProcessState));
	state.crashReason = process->crashReason;
	state.id = process->id;
	state.executableState = process->executableState;
	state.flags = (process->allThreadsTerminated ? ES_PROCESS_STATE_ALL_THREADS_TERMINATED : 0)
		| (process->terminating ? ES_PROCESS_STATE_TERMINATING : 0)
		| (process->crashed ? ES_PROCESS_STATE_CRASHED : 0)
		| (process->messageQueue.pinged ? ES_PROCESS_STATE_PINGED : 0);

	SYSCALL_WRITE(argument1, &state, sizeof(EsProcessState));
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_SHUTDOWN) {
	SYSCALL_PERMISSION(ES_PERMISSION_SHUTDOWN);
	KThreadCreate("Shutdown", [] (uintptr_t action) { KernelShutdown(action); }, argument0);
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_WINDOW_GET_ID) {
	SYSCALL_HANDLE_2(argument0, KERNEL_OBJECT_WINDOW | KERNEL_OBJECT_EMBEDDED_WINDOW, _window);

	if (_window.type == KERNEL_OBJECT_WINDOW) {
		SYSCALL_RETURN(((Window *) _window.object)->id, false);
	} else {
		SYSCALL_RETURN(((EmbeddedWindow *) _window.object)->id, false);
	}
}

SYSCALL_IMPLEMENT(ES_SYSCALL_YIELD_SCHEDULER) {
	ProcessorFakeTimerInterrupt();
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_SYSTEM_TAKE_SNAPSHOT) {
	SYSCALL_PERMISSION(ES_PERMISSION_TAKE_SYSTEM_SNAPSHOT);

	int type = argument0;
	void *buffer = nullptr;
	size_t bufferSize = 0;
	EsDefer(EsHeapFree(buffer, 0, K_FIXED));

	switch (type) {
		case ES_SYSTEM_SNAPSHOT_PROCESSES: {
			KSpinlockAcquire(&scheduler.lock);
			size_t count = scheduler.allProcesses.count + 8;
			bufferSize = sizeof(EsSnapshotProcesses) + sizeof(EsSnapshotProcessesItem) * count;
			KSpinlockRelease(&scheduler.lock);
			
			buffer = EsHeapAllocate(bufferSize, true, K_FIXED);

			if (!buffer) {
				SYSCALL_RETURN(ES_ERROR_INSUFFICIENT_RESOURCES, false);
			}

			EsMemoryZero(buffer, bufferSize);

			KSpinlockAcquire(&scheduler.lock);

			if (scheduler.allProcesses.count < count) {
				count = scheduler.allProcesses.count;
			}

			EsSnapshotProcesses *snapshot = (EsSnapshotProcesses *) buffer;

			LinkedItem<Process> *item = scheduler.allProcesses.firstItem;
			uintptr_t index = 0;

			while (item && index < count) {
				Process *process = item->thisItem;
				if (process->terminating) goto next;

				{
					snapshot->processes[index].pid = process->id;
					snapshot->processes[index].memoryUsage = process->vmm->commit * K_PAGE_SIZE; 
					snapshot->processes[index].cpuTimeSlices = process->cpuTimeSlices;
					snapshot->processes[index].idleTimeSlices = process->idleTimeSlices;
					snapshot->processes[index].handleCount = process->handleTable.handleCount;
					snapshot->processes[index].isKernel = process->type == PROCESS_KERNEL;

					snapshot->processes[index].nameBytes = EsCStringLength(process->cExecutableName);
					EsMemoryCopy(snapshot->processes[index].name, process->cExecutableName, snapshot->processes[index].nameBytes);

					index++;
				}

				next:;
				item = item->nextItem;
			}

			snapshot->count = index;
			bufferSize = sizeof(EsSnapshotProcesses) + sizeof(EsSnapshotProcessesItem) * index;
			KSpinlockRelease(&scheduler.lock);
		} break;

		default: {
			SYSCALL_RETURN(ES_FATAL_ERROR_OUT_OF_RANGE, true);
		} break;
	}

	SYSCALL_WRITE(argument1, &bufferSize, sizeof(size_t));
	SYSCALL_RETURN(MakeConstantBuffer(buffer, bufferSize, currentProcess), false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_PROCESS_OPEN) {
	SYSCALL_PERMISSION(ES_PERMISSION_PROCESS_OPEN);

	Process *process = scheduler.OpenProcess(argument0);

	if (process) {
		SYSCALL_RETURN(currentProcess->handleTable.OpenHandle(process, 0, KERNEL_OBJECT_PROCESS), false);
	} else {
		SYSCALL_RETURN(ES_INVALID_HANDLE, false);
	}
}

SYSCALL_IMPLEMENT(ES_SYSCALL_PROCESS_SET_TLS) {
	currentThread->tlsAddress = argument0; // Set this first, otherwise we could get pre-empted and restore without TLS set.
	ProcessorSetThreadStorage(argument0);
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_PROCESS_GET_TLS) {
	SYSCALL_RETURN(currentThread->tlsAddress, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_SCREEN_BOUNDS_GET) {
	EsRectangle rectangle;
	EsMemoryZero(&rectangle, sizeof(EsRectangle));

	rectangle.l = 0;
	rectangle.t = 0;
	rectangle.r = graphics.width;
	rectangle.b = graphics.height;

	SYSCALL_WRITE(argument1, &rectangle, sizeof(EsRectangle));
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_SCREEN_WORK_AREA_SET) {
	SYSCALL_PERMISSION(ES_PERMISSION_SCREEN_MODIFY);
	EsRectangle rectangle;
	SYSCALL_READ(&rectangle, argument1, sizeof(EsRectangle));
	windowManager.workArea = rectangle;
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_SCREEN_WORK_AREA_GET) {
	EsRectangle rectangle = windowManager.workArea;
	SYSCALL_WRITE(argument1, &rectangle, sizeof(EsRectangle));
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_MESSAGE_DESKTOP) {
	char *buffer;
	if (argument1 > DESKTOP_MESSAGE_SIZE_LIMIT) SYSCALL_RETURN(ES_ERROR_INSUFFICIENT_RESOURCES, false);
	SYSCALL_READ_HEAP(buffer, argument0, argument1);

	SYSCALL_HANDLE_2(argument2, KERNEL_OBJECT_EMBEDDED_WINDOW | KERNEL_OBJECT_NONE, _window);
	SYSCALL_HANDLE_2(argument3, KERNEL_OBJECT_PIPE | KERNEL_OBJECT_NONE, _pipe);

	EmbeddedWindow *window = (EmbeddedWindow *) _window.object;
	Pipe *pipe = (Pipe *) _pipe.object;

	if (pipe && (~_pipe.flags & PIPE_WRITER)) {
		SYSCALL_RETURN(ES_FATAL_ERROR_INCORRECT_FILE_ACCESS, true);
	}

	if (!scheduler.shutdown) {
		if (pipe) {
			OpenHandleToObject(pipe, KERNEL_OBJECT_PIPE, PIPE_WRITER);
		}

		_EsMessageWithObject m = {};
		m.message.type = ES_MSG_DESKTOP;
		m.message.desktop.buffer = MakeConstantBufferForDesktop(buffer, argument1);
		m.message.desktop.bytes = argument1;
		m.message.desktop.windowID = window ? window->id : 0;
		m.message.desktop.processID = currentProcess->id;
		m.message.desktop.pipe = pipe ? desktopProcess->handleTable.OpenHandle(pipe, PIPE_WRITER, KERNEL_OBJECT_PIPE) : ES_INVALID_HANDLE;

		if (!m.message.desktop.buffer || !desktopProcess->messageQueue.SendMessage(&m)) {
			desktopProcess->handleTable.CloseHandle(m.message.desktop.buffer); 
			desktopProcess->handleTable.CloseHandle(m.message.desktop.pipe); 
		}
	}

	SYSCALL_RETURN(ES_SUCCESS, false);
}

#ifdef ENABLE_POSIX_SUBSYSTEM
SYSCALL_IMPLEMENT(ES_SYSCALL_POSIX) {
	SYSCALL_PERMISSION(ES_PERMISSION_POSIX_SUBSYSTEM);

	_EsPOSIXSyscall syscall;
	SYSCALL_READ(&syscall, argument0, sizeof(_EsPOSIXSyscall));

	if (syscall.index == 2 /* open */ || syscall.index == 59 /* execve */) {
		SYSCALL_HANDLE_2(syscall.arguments[4], KERNEL_OBJECT_NODE, node);
		syscall.arguments[4] = (long) node.object;
		if (~node.flags & _ES_NODE_DIRECTORY_WRITE) SYSCALL_RETURN(ES_FATAL_ERROR_INVALID_HANDLE, true);
		long result = POSIX::DoSyscall(syscall, userStackPointer);
		SYSCALL_RETURN(result, false);
	} else if (syscall.index == 109 /* setpgid */) {
		SYSCALL_HANDLE_2(syscall.arguments[0], KERNEL_OBJECT_PROCESS, process);
		syscall.arguments[0] = (long) process.object;
		long result = POSIX::DoSyscall(syscall, userStackPointer);
		SYSCALL_RETURN(result, false);
	} else {
		long result = POSIX::DoSyscall(syscall, userStackPointer);
		SYSCALL_RETURN(result, false);
	}
}
#else
SYSCALL_IMPLEMENT(ES_SYSCALL_POSIX) {
	SYSCALL_RETURN(ES_FATAL_ERROR_UNKNOWN_SYSCALL, true);
}
#endif

SYSCALL_IMPLEMENT(ES_SYSCALL_PROCESS_GET_STATUS) {
	SYSCALL_HANDLE(argument0, KERNEL_OBJECT_PROCESS, process, Process);
	SYSCALL_RETURN(process->exitStatus, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_PIPE_CREATE) {
	Pipe *pipe = (Pipe *) EsHeapAllocate(sizeof(Pipe), true, K_PAGED);
	if (!pipe) SYSCALL_RETURN(ES_ERROR_INSUFFICIENT_RESOURCES, false);
	pipe->writers = pipe->readers = 1;
	KEventSet(&pipe->canWrite);
	EsHandle readEnd  = currentProcess->handleTable.OpenHandle(pipe, PIPE_READER, KERNEL_OBJECT_PIPE);
	EsHandle writeEnd = currentProcess->handleTable.OpenHandle(pipe, PIPE_WRITER, KERNEL_OBJECT_PIPE);
	SYSCALL_WRITE(argument0, &readEnd, sizeof(EsHandle));
	SYSCALL_WRITE(argument1, &writeEnd, sizeof(EsHandle));
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_PIPE_READ) {
	if (!argument2) SYSCALL_RETURN(ES_SUCCESS, false);
	SYSCALL_HANDLE(argument0, KERNEL_OBJECT_PIPE, pipe, Pipe);
	SYSCALL_BUFFER(argument1, argument2, 2, false);
	SYSCALL_RETURN(pipe->Access((void *) argument1, argument2, false, true), false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_PIPE_WRITE) {
	if (!argument2) SYSCALL_RETURN(ES_SUCCESS, false);
	SYSCALL_HANDLE(argument0, KERNEL_OBJECT_PIPE, pipe, Pipe);
	SYSCALL_BUFFER(argument1, argument2, 2, true /* write */);
	SYSCALL_RETURN(pipe->Access((void *) argument1, argument2, true, true), false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_EVENT_SINK_CREATE) {
	EventSink *sink = (EventSink *) EsHeapAllocate(sizeof(EventSink), true, K_FIXED);
	
	if (!sink) {
		SYSCALL_RETURN(ES_ERROR_INSUFFICIENT_RESOURCES, false);
	}

	sink->ignoreDuplicates = argument0;
	sink->handles = 1;

	SYSCALL_RETURN(currentProcess->handleTable.OpenHandle(sink, 0, KERNEL_OBJECT_EVENT_SINK), false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_EVENT_FORWARD) {
	SYSCALL_HANDLE(argument0, KERNEL_OBJECT_EVENT, event, KEvent);
	SYSCALL_HANDLE(argument1, KERNEL_OBJECT_EVENT_SINK, sink, EventSink);
	EsGeneric data = argument2;

	bool error = false, limitExceeded = false;

	KMutexAcquire(&eventForwardMutex);

	if (!event->sinkTable) {
		event->sinkTable = (EventSinkTable *) EsHeapAllocate(sizeof(EventSinkTable) * ES_MAX_EVENT_FORWARD_COUNT, true, K_FIXED);

		if (!event->sinkTable) {
			error = true;
		}
	}

	if (!error) {
		limitExceeded = true;

		for (uintptr_t i = 0; i < ES_MAX_EVENT_FORWARD_COUNT; i++) {
			if (!event->sinkTable[i].sink) {
				if (!OpenHandleToObject(sink, KERNEL_OBJECT_EVENT_SINK, 0, false)) {
					error = true;
					break;
				}

				KSpinlockAcquire(&scheduler.lock);
				event->sinkTable[i].sink = sink;
				event->sinkTable[i].data = data;
				KSpinlockRelease(&scheduler.lock);

				limitExceeded = false;
				break;
			}
		}
	}

	KMutexRelease(&eventForwardMutex);

	if (limitExceeded) {
		SYSCALL_RETURN(ES_FATAL_ERROR_OUT_OF_RANGE, true);
	} else if (error) {
		SYSCALL_RETURN(ES_ERROR_INSUFFICIENT_RESOURCES, false);
	} else {
	       	SYSCALL_RETURN(0, false);
	}
}

SYSCALL_IMPLEMENT(ES_SYSCALL_EVENT_SINK_POP) {
	SYSCALL_HANDLE(argument0, KERNEL_OBJECT_EVENT_SINK, sink, EventSink);

	bool empty = false, overflow = false;
	EsGeneric data = {};

	KSpinlockAcquire(&sink->spinlock);

	if (!sink->queueCount) {
		if (sink->overflow) {
			overflow = true;
			sink->overflow = false;
		} else {
			empty = true;
		}
	} else {
		data = sink->queue[sink->queuePosition];
		sink->queuePosition++;
		sink->queueCount--;

		if (sink->queuePosition == ES_MAX_EVENT_SINK_BUFFER_SIZE) {
			sink->queuePosition = 0;
		}
	}

	if (!sink->queueCount && !sink->overflow) {
		KEventReset(&sink->available); // KEvent::Reset doesn't take the scheduler lock, so this won't deadlock!
	}

	KSpinlockRelease(&sink->spinlock);

	SYSCALL_WRITE(argument1, &data, sizeof(EsGeneric));
	SYSCALL_RETURN(overflow ? ES_ERROR_EVENT_SINK_OVERFLOW : empty ? ES_ERROR_EVENT_NOT_SET : ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_EVENT_SINK_PUSH) {
	SYSCALL_HANDLE(argument0, KERNEL_OBJECT_EVENT_SINK, sink, EventSink);
	KSpinlockAcquire(&scheduler.lock);
	EsError result = sink->Push(argument1);
	KSpinlockRelease(&scheduler.lock);
	SYSCALL_RETURN(result, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_DOMAIN_NAME_RESOLVE) {
	SYSCALL_PERMISSION(ES_PERMISSION_NETWORKING);

	if (argument1 > ES_DOMAIN_NAME_MAX_LENGTH) {
		SYSCALL_RETURN(ES_ERROR_BAD_DOMAIN_NAME, false);
	}

	char domainName[ES_DOMAIN_NAME_MAX_LENGTH];
	SYSCALL_READ(domainName, argument0, argument1);

	EsAddress address;
	EsMemoryZero(&address, sizeof(EsAddress));

	KEvent completeEvent = {};

	NetDomainNameResolveTask task = {};
	task.event = &completeEvent;
	task.name = domainName;
	task.nameBytes = (size_t) argument1;
	task.address = &address;
	task.callback = NetDomainNameResolve;
	NetTaskBegin(&task);

	KEventWait(&completeEvent);

	if (task.error == ES_SUCCESS) {
		SYSCALL_WRITE(argument2, &address, sizeof(EsAddress));
	}

	SYSCALL_RETURN(task.error, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_ECHO_REQUEST) {
	SYSCALL_PERMISSION(ES_PERMISSION_NETWORKING);

	if (argument1 > ES_ECHO_REQUEST_MAX_LENGTH) {
		SYSCALL_RETURN(ES_FATAL_ERROR_INVALID_BUFFER, true);
	}

	uint8_t data[48];
	EsMemoryZero(data, sizeof(data));
	SYSCALL_READ(data, argument0, argument1);

	EsAddress address;
	SYSCALL_READ(&address, argument2, sizeof(EsAddress));

	KEvent completeEvent = {};

	NetEchoRequestTask task = {};
	task.event = &completeEvent;
	task.address = &address;
	task.data = data;
	task.callback = NetEchoRequest;
	NetTaskBegin(&task);

	KEventWait(&completeEvent);

	if (task.error == ES_SUCCESS) {
		SYSCALL_WRITE(argument0, data, argument1);
	}

	SYSCALL_RETURN(task.error, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_CONNECTION_OPEN) {
	SYSCALL_PERMISSION(ES_PERMISSION_NETWORKING);

	EsConnection connection;
	SYSCALL_READ(&connection, argument0, sizeof(EsConnection));

	if (connection.sendBufferBytes < 1024 || connection.receiveBufferBytes < 1024) {
		SYSCALL_RETURN(ES_ERROR_BUFFER_TOO_SMALL, false);
	}

	// TODO Upper limit on buffer sizes?

	NetConnection *netConnection = NetConnectionOpen(&connection.address, connection.sendBufferBytes, connection.receiveBufferBytes, argument1);

	if (!netConnection) {
		SYSCALL_RETURN(ES_ERROR_INSUFFICIENT_RESOURCES, false);
	}

	connection.sendBuffer = (uint8_t *) MMMapShared(currentVMM, netConnection->bufferRegion, 0, connection.sendBufferBytes + connection.receiveBufferBytes);
	connection.receiveBuffer = connection.sendBuffer + connection.sendBufferBytes;

	if (!connection.sendBuffer) {
		CloseHandleToObject(netConnection, KERNEL_OBJECT_CONNECTION);
		SYSCALL_RETURN(ES_ERROR_INSUFFICIENT_RESOURCES, false);
	}

	connection.error = ES_SUCCESS;

	connection.handle = currentProcess->handleTable.OpenHandle(netConnection, 0, KERNEL_OBJECT_CONNECTION); 

	SYSCALL_WRITE(argument0, &connection, sizeof(EsConnection));
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_CONNECTION_POLL) {
	SYSCALL_BUFFER(argument0, sizeof(EsConnection), 0, true /* write */);
	EsConnection *connection = (EsConnection *) argument0;
	SYSCALL_HANDLE(argument3, KERNEL_OBJECT_CONNECTION, netConnection, NetConnection);

	connection->receiveWritePointer = netConnection->receiveWritePointer;
	connection->sendReadPointer = netConnection->sendReadPointer;
	connection->open = netConnection->task.step == TCP_STEP_ESTABLISHED;
	connection->error = netConnection->task.completed ? netConnection->task.error : ES_SUCCESS;

	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_CONNECTION_NOTIFY) {
	SYSCALL_HANDLE(argument3, KERNEL_OBJECT_CONNECTION, netConnection, NetConnection);
	NetConnectionNotify(netConnection, argument1, argument2);
	SYSCALL_RETURN(ES_SUCCESS, false);
}

SYSCALL_IMPLEMENT(ES_SYSCALL_DEBUG_COMMAND) {
#ifdef DEBUG_BUILD
	if (argument0 == 1) {
		ArchResetCPU();
	} else if (argument0 == 2) {
		KernelPanic("Debug command 2.\n");
	} else if (argument0 == 3) {
		extern char kernelLog[];
		extern uintptr_t kernelLogPosition;
		size_t bytes = kernelLogPosition;
		if (argument2 < bytes) bytes = argument2;
		EsMemoryCopy((void *) argument1, kernelLog, bytes);
		SYSCALL_RETURN(bytes, false);
	} else if (argument0 == 4) {
		SYSCALL_BUFFER(argument1, 1, 0, false);

		if (_region0->data.normal.commitPageCount != (argument3 & 0x7FFFFFFFFFFFFFFF)) {
			KernelPanic("Commit page count mismatch.\n");
		}

		if (_region0->data.normal.commit.Contains(argument2) != (argument3 >> 63)) {
			KernelPanic("Commit contains mismatch at %x.\n", argument1);
		}
	} else if (argument0 == 6) {
		// SYSCALL_RETURN(DriversDebugGetEnumeratedPCIDevices((EsPCIDevice *) argument1, argument2), false);
	} else if (argument0 == 7) {
		EsAssert(!scheduler.threadEventLog);
		EsThreadEventLogEntry *buffer = (EsThreadEventLogEntry *) EsHeapAllocate(argument0 * sizeof(EsThreadEventLogEntry), false, K_FIXED);
		scheduler.threadEventLogAllocated = argument0;
		scheduler.threadEventLogPosition = 0;
		__sync_synchronize();
		scheduler.threadEventLog = buffer;
	} else if (argument0 == 8) {
		SYSCALL_RETURN(scheduler.threadEventLogPosition, false);
	} else if (argument0 == 9) {
		SYSCALL_WRITE(argument1, scheduler.threadEventLog, scheduler.threadEventLogPosition * sizeof(EsThreadEventLogEntry));
	} else if (argument0 == 10) {
		scheduler.threadEventLogAllocated = 0;
		// HACK Wait for threads to stop writing...
		KEvent event = {};
		KEventWait(&event, 1000);
		EsHeapFree(scheduler.threadEventLog, 0, K_FIXED);
		scheduler.threadEventLog = nullptr;
	} else if (argument0 == 12) {
		EsMemoryStatistics statistics;
		EsMemoryZero(&statistics, sizeof(statistics));
		statistics.fixedHeapAllocationCount = K_FIXED->allocationsCount;
		statistics.fixedHeapTotalSize = K_FIXED->size;
		statistics.coreHeapAllocationCount = K_CORE->allocationsCount;
		statistics.coreHeapTotalSize = K_CORE->size;
		statistics.cachedNodes = fs.bootFileSystem->cachedNodes.count;
		statistics.cachedDirectoryEntries = fs.bootFileSystem->cachedDirectoryEntries.count;
		statistics.totalSurfaceBytes = graphics.totalSurfaceBytes;
		statistics.commitPageable = pmm.commitPageable;
		statistics.commitFixed = pmm.commitFixed;
		statistics.commitLimit = pmm.commitLimit;
		statistics.commitFixedLimit = pmm.commitFixedLimit;
		statistics.commitRemaining = MM_REMAINING_COMMIT();
		statistics.maximumObjectCachePages = MM_OBJECT_CACHE_PAGES_MAXIMUM();
		statistics.approximateObjectCacheSize = pmm.approximateTotalObjectCacheBytes;
		SYSCALL_WRITE(argument1, &statistics, sizeof(statistics));
	}
#endif

	SYSCALL_RETURN(ES_SUCCESS, false);
}

SyscallFunction syscallFunctions[ES_SYSCALL_COUNT + 1] {
#include <bin/syscall_array.h>
};

#pragma GCC diagnostic pop

uintptr_t DoSyscall(EsSyscallType index,
		uintptr_t argument0, uintptr_t argument1,
		uintptr_t argument2, uintptr_t argument3,
		uint64_t flags, bool *fatal, uintptr_t *userStackPointer) {
	bool batched = flags & DO_SYSCALL_BATCHED;

	// Interrupts need to be enabled during system calls,
	// because many of them block on mutexes or events.
	ProcessorEnableInterrupts();

	Thread *currentThread = GetCurrentThread();
	Process *currentProcess = currentThread->process;
	MMSpace *currentVMM = currentProcess->vmm;

	if (!batched) {
		if (currentThread->terminating) {
			// The thread has been terminated.
			// Yield the scheduler so it can be removed.
			ProcessorFakeTimerInterrupt();
		}

		if (currentThread->terminatableState != THREAD_TERMINATABLE) {
			KernelPanic("DoSyscall - Current thread %x was not terminatable (was %d).\n", 
					currentThread, currentThread->terminatableState);
		}

		currentThread->terminatableState = THREAD_IN_SYSCALL;
	}

	EsError returnValue = ES_FATAL_ERROR_UNKNOWN_SYSCALL;
	bool fatalError = true;

	if (index < ES_SYSCALL_COUNT) {
		SyscallFunction function = syscallFunctions[index];

		if (batched && index == ES_SYSCALL_BATCH) {
			// This could cause a stack overflow, so it's a fatal error.
		} else if (function) {
			returnValue = (EsError) function(argument0, argument1, argument2, argument3, 
					currentThread, currentProcess, currentVMM, userStackPointer, &fatalError);
		}
	}

	if (fatal) *fatal = false;

	if (fatalError) {
		if (fatal) {
			*fatal = true;
		} else {
			EsCrashReason reason;
			EsMemoryZero(&reason, sizeof(EsCrashReason));
			reason.errorCode = (EsFatalError) returnValue;
			reason.duringSystemCall = index;
			KernelLog(LOG_ERROR, "Syscall", "syscall failure", 
					"Process crashed during system call [%x, %x, %x, %x, %x]\n", index, argument0, argument1, argument2, argument3);
			scheduler.CrashProcess(currentProcess, &reason);
		}
	}

	if (!batched) {
		currentThread->terminatableState = THREAD_TERMINATABLE;

		if (currentThread->terminating || currentThread->paused) {
			// The thread has been terminated or paused.
			// Yield the scheduler so it can be removed or sent to the paused thread queue.
			ProcessorFakeTimerInterrupt();
		}
	}
	
	return returnValue;
}

#endif
