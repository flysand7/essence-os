#ifndef IMPLEMENTATION

#define PREEMPT_AFTER_MUTEX_RELEASE

#define THREAD_PRIORITY_NORMAL 	(0) // Lower value = higher priority.
#define THREAD_PRIORITY_LOW 	(1)
#define THREAD_PRIORITY_COUNT	(2)

void CloseHandleToThread(void *_thread);
void CloseHandleToProcess(void *_thread);
void KillThread(EsGeneric _thread);

void RegisterAsyncTask(KAsyncTaskCallback callback, EsGeneric argument, struct Process *targetProcess, 
		bool needed /*If false, the task may not be registered if there are many queued tasks.*/,
		bool unlocked = false /*Set to true if you haven't acquired the scheduler's lock.*/);

enum ThreadState : int8_t {
	THREAD_ACTIVE,			// An active thread. Not necessarily executing; `executing` determines if it executing.
	THREAD_WAITING_MUTEX,		// Waiting for a mutex to be released.
	THREAD_WAITING_EVENT,		// Waiting for a event to be notified.
	THREAD_WAITING_WRITER_LOCK,	// Waiting for a writer lock to be notified.
	THREAD_TERMINATED,		// The thread has been terminated. It will be deallocated when all handles are closed.
					// 	I believe this is called a "zombie thread" in UNIX terminology.
};

enum ThreadType : int8_t {
	THREAD_NORMAL,			// A normal thread.
	THREAD_IDLE,			// The CPU's idle thread.
	THREAD_ASYNC_TASK,		// A thread that processes the CPU's asynchronous tasks.
};

enum ThreadTerminatableState : int8_t {
	THREAD_INVALID_TS,
	THREAD_TERMINATABLE,		// The thread is currently executing user code.
	THREAD_IN_SYSCALL,		// The thread is currently executing kernel code from a system call.
					// It cannot be terminated/paused until it returns from the system call.
	THREAD_USER_BLOCK_REQUEST,	// The thread is sleeping because of a user system call to sleep.
					// It can be unblocked, and then terminated when it returns from the system call.
};

struct Thread {
	void SetAddressSpace(MMSpace *space); 		// Set a temporary address space for the thread.
							// Used by the asynchronous task threads,
							// and the memory manager's balancer.

	// ** Must be the first item in the structure; see MMArchSafeCopy. **
	bool inSafeCopy;

	LinkedItem<Thread> item;			// Entry in relevent thread queue or blockedThreads list for mutexes/writer locks.
	LinkedItem<Thread> allItem; 			// Entry in the allThreads list.
	LinkedItem<Thread> processItem; 		// Entry in the process's list of threads.
	LinkedItem<Thread> *blockedItems;		// Entries in the blockedThreads lists for events (not mutexes).

	struct Process *process;

	EsObjectID id;
	volatile uintptr_t cpuTimeSlices;
	volatile size_t handles;
	int executingProcessorID;

	uintptr_t userStackBase;
	uintptr_t kernelStackBase;
	uintptr_t kernelStack;
	uintptr_t tlsAddress;
	size_t userStackReserve;
	volatile size_t userStackCommit;

	ThreadType type;
	bool isKernelThread, isPageGenerator;
	int8_t priority;
	int32_t blockedThreadPriorities[THREAD_PRIORITY_COUNT]; // The number of threads blocking on this thread at each priority level.

	volatile ThreadState state;
	volatile ThreadTerminatableState terminatableState;
	volatile bool executing;
	volatile bool terminating; 	// Set when a request to terminate the thread has been registered.
	volatile bool paused;	   	// Set to pause a thread, usually when it has crashed or being debugged. The scheduler will skip threads marked as paused when deciding what to run.
	volatile bool receivedYieldIPI; // Used to terminate a thread executing on a different processor.

	union {
		KMutex *volatile mutex;

		struct {
			KWriterLock *volatile writerLock;
			bool writerLockType;
		};

		struct {
			KEvent *volatile events[ES_MAX_WAIT_COUNT];
			volatile size_t eventCount; 
		};
	} blocking;

	KEvent killedEvent;

	// If the type of the thread is THREAD_ASYNC_TASK,
	// then this is the virtual address space that should be loaded
	// when the task is being executed.
	MMSpace *volatile temporaryAddressSpace;

	InterruptContext *interruptContext;  // TODO Store the userland interrupt context instead?
	uintptr_t lastKnownExecutionAddress; // For debugging.

#ifdef ENABLE_POSIX_SUBSYSTEM
	struct POSIXThread *posixData;
#endif

	const char *cName;
};

enum ProcessType {
	PROCESS_NORMAL,
	PROCESS_KERNEL,
	PROCESS_DESKTOP,
};

struct Process {
	bool StartWithNode(KNode *node);
	bool Start(char *imagePath, size_t imagePathLength);

	MMSpace *vmm;
	HandleTable handleTable;
	MessageQueue messageQueue;
	LinkedList<Thread> threads;

	// Creation information:
	KNode *executableNode;
	char cExecutableName[ES_SNAPSHOT_MAX_PROCESS_NAME_LENGTH + 1];
	EsProcessCreateData data;
	uint64_t permissions;
	uint32_t creationFlags; 
	ProcessType type;

	// Object management:
	EsObjectID id;
	volatile size_t handles;
	LinkedItem<Process> allItem;

	// Crashing:
	KMutex crashMutex;
	EsCrashReason crashReason;
	bool crashed;

	// Termination:
	bool allThreadsTerminated;
	bool terminating;
	int exitStatus; // TODO Remove this.
	KEvent killedEvent;

	// Executable state:
	uint8_t executableState;
	bool executableStartRequest;
	KEvent executableLoadAttemptComplete;
	Thread *executableMainThread;

	// Statistics:
	uintptr_t cpuTimeSlices, idleTimeSlices;

	// POSIX:
#ifdef ENABLE_POSIX_SUBSYSTEM
	bool posixForking;
	int pgid;
#endif
};

Process _kernelProcess;
Process *kernelProcess = &_kernelProcess;

struct Scheduler {
	// External API:

	void Yield(InterruptContext *context);

#define SPAWN_THREAD_MANUALLY_ACTIVATED (1)
#define SPAWN_THREAD_USERLAND 		(2)
#define SPAWN_THREAD_LOW_PRIORITY	(4)
#define SPAWN_THREAD_PAUSED             (8)
	Thread *SpawnThread(const char *cName, uintptr_t startAddress, uintptr_t argument1 = 0, 
			unsigned flags = ES_FLAGS_DEFAULT, Process *process = kernelProcess, uintptr_t argument2 = 0);
	void TerminateThread(Thread *thread, bool lockAlreadyAcquired = false);
	void PauseThread(Thread *thread, bool resume /*true to resume, false to pause*/, bool lockAlreadyAcquired = false);

	Process *SpawnProcess(ProcessType processType = PROCESS_NORMAL);
	void TerminateProcess(Process *process, int status);
	void PauseProcess(Process *process, bool resume);
	void CrashProcess(Process *process, EsCrashReason *reason);

	Process *OpenProcess(uint64_t id);

	void WaitMutex(KMutex *mutex);
	uintptr_t WaitEvents(KEvent **events, size_t count); // Returns index of notified object.

	void Shutdown();

	// Internal functions:

	void CreateProcessorThreads(CPULocalStorage *local);

	void RemoveProcess(Process *process); // Do not call. Use TerminateProcess/CloseHandleToObject.
	void RemoveThread(Thread *thread); // Do not call. Use TerminateThread/CloseHandleToObject.
	void AddActiveThread(Thread *thread, bool start /*Put it at the start*/);	// Add an active thread into the queue.
	void InsertNewThread(Thread *thread, bool addToActiveList, Process *owner); 	// Used during thread creation.
	void MaybeUpdateActiveList(Thread *thread); // After changing the priority of a thread, call this to move it to the correct active thread queue if needed.

	void NotifyObject(LinkedList<Thread> *blockedThreads, bool unblockAll, Thread *previousMutexOwner = nullptr);
	void UnblockThread(Thread *unblockedThread, Thread *previousMutexOwner = nullptr);

	Thread *PickThread(CPULocalStorage *local); // Pick the next thread to execute.
	int8_t GetThreadEffectivePriority(Thread *thread);

	// Variables:

	KSpinlock lock;

	KEvent killedEvent; // Set during shutdown when all processes have been terminated.
	uintptr_t blockShutdownProcessCount;

	LinkedList<Thread> activeThreads[THREAD_PRIORITY_COUNT], pausedThreads;
	LinkedList<KTimer> activeTimers;

	Pool threadPool, processPool, mmSpacePool;
	LinkedList<Thread>  allThreads;
	LinkedList<Process> allProcesses;
	EsObjectID nextThreadID;
	EsObjectID nextProcessID;
	size_t activeProcessCount;

	volatile bool started, panic, shutdown;

	uint64_t timeMs, lastTimeStamp;

	unsigned currentProcessorID;

#ifdef DEBUG_BUILD
	EsThreadEventLogEntry *volatile threadEventLog;
	volatile uintptr_t threadEventLogPosition;
	volatile size_t threadEventLogAllocated;
#endif
};

extern Scheduler scheduler;

#endif

#ifdef IMPLEMENTATION

Scheduler scheduler;

int8_t Scheduler::GetThreadEffectivePriority(Thread *thread) {
	KSpinlockAssertLocked(&lock);

	for (int8_t i = 0; i < thread->priority; i++) {
		if (thread->blockedThreadPriorities[i]) {
			// A thread is blocking on a resource owned by this thread,
			// and the blocking thread has a higher priority than this thread.
			// Therefore, this thread should assume that higher priority,
			// until it releases the resource.
			return i;
		}
	}

	return thread->priority;
}

void Scheduler::AddActiveThread(Thread *thread, bool start) {
	if (thread->type == THREAD_ASYNC_TASK) {
		// An asynchronous task thread was unblocked.
		// It will be run immediately, so there's no need to add it to the active thread list.
		return;
	}
	
	KSpinlockAssertLocked(&lock);

	if (thread->state != THREAD_ACTIVE) {
		KernelPanic("Scheduler::AddActiveThread - Thread %d not active\n", thread->id);
	} else if (thread->executing) {
		KernelPanic("Scheduler::AddActiveThread - Thread %d executing\n", thread->id);
	} else if (thread->type != THREAD_NORMAL) {
		KernelPanic("Scheduler::AddActiveThread - Thread %d has type %d\n", thread->id, thread->type);
	} else if (thread->item.list) {
		KernelPanic("Scheduler::AddActiveThread - Thread %d is already in queue %x.\n", thread->id, thread->item.list);
	}

	if (thread->paused && thread->terminatableState == THREAD_TERMINATABLE) {
		// The thread is paused, so we can put it into the paused queue until it is resumed.
		pausedThreads.InsertStart(&thread->item);
	} else {
		int8_t effectivePriority = GetThreadEffectivePriority(thread);

		if (start) {
			activeThreads[effectivePriority].InsertStart(&thread->item);
		} else {
			activeThreads[effectivePriority].InsertEnd(&thread->item);
		}
	}
}

void Scheduler::MaybeUpdateActiveList(Thread *thread) {
	// TODO Is this correct with regards to paused threads?

	if (thread->type == THREAD_ASYNC_TASK) {
		// Asynchronous task threads do not go in the activeThreads lists.
		return;
	}

	if (thread->type != THREAD_NORMAL) {
		KernelPanic("Scheduler::MaybeUpdateActiveList - Trying to update the active list of a non-normal thread %x.\n", thread);
	}

	KSpinlockAssertLocked(&lock);

	if (thread->state != THREAD_ACTIVE || thread->executing) {
		// The thread is not currently in an active list, 
		// so it'll end up in the correct activeThreads list when it becomes active.
		return;
	}

	if (!thread->item.list) {
		KernelPanic("Scheduler::MaybeUpdateActiveList - Despite thread %x being active and not executing, it is not in an activeThreads lists.\n", thread);
	}

	int8_t effectivePriority = GetThreadEffectivePriority(thread);

	if (&activeThreads[effectivePriority] == thread->item.list) {
		// The thread's effective priority has not changed.
		// We don't need to do anything.
		return;
	}

	// Remove the thread from its previous active list.
	thread->item.RemoveFromList();

	// Add it to the start of its new active list.
	// TODO I'm not 100% sure we want to always put it at the start.
	activeThreads[effectivePriority].InsertStart(&thread->item);
}

void Scheduler::InsertNewThread(Thread *thread, bool addToActiveList, Process *owner) {
	KSpinlockAcquire(&lock);
	EsDefer(KSpinlockRelease(&lock));

	// New threads are initialised here.
	thread->id = nextThreadID++;
	thread->process = owner;

	owner->handles++; 	// Each thread owns a handles to the owner process.
				// This makes sure the process isn't destroyed before all its threads have been destroyed.
	// EsPrint("Open handle to process %d/%x (new thread). New handle count: %d.\n", owner->id, owner, owner->handles);

	thread->item.thisItem = thread;
	thread->allItem.thisItem = thread;
	thread->processItem.thisItem = thread;
	owner->threads.InsertEnd(&thread->processItem);

	KernelLog(LOG_INFO, "Scheduler", "create thread", "Create thread ID %d, type %d, owner process %d\n", thread->id, thread->type, owner->id);

	if (addToActiveList) {
		// Add the thread to the start of the active thread list to make sure that it runs immediately.
		AddActiveThread(thread, true);
	} else {
		// Some threads (such as idle threads) do this themselves.
	}

	allThreads.InsertStart(&thread->allItem);
}

Thread *Scheduler::SpawnThread(const char *cName, uintptr_t startAddress, uintptr_t argument1, unsigned flags, Process *process, uintptr_t argument2) {
	bool userland = flags & SPAWN_THREAD_USERLAND;

	if (userland && process == kernelProcess) {
		KernelPanic("Scheduler::SpawnThread - Cannot add userland thread to kernel process.\n");
	}

	KSpinlockAcquire(&scheduler.lock);
	bool terminating = process->terminating;
	KSpinlockRelease(&scheduler.lock);

	if (shutdown && userland) return nullptr;
	if (terminating) return nullptr;

	Thread *thread = (Thread *) threadPool.Add(sizeof(Thread));
	if (!thread) return nullptr;
	KernelLog(LOG_INFO, "Scheduler", "spawn thread", "Created thread, %x to start at %x\n", thread, startAddress);
	thread->isKernelThread = !userland;
	thread->priority = (flags & SPAWN_THREAD_LOW_PRIORITY) ? THREAD_PRIORITY_LOW : THREAD_PRIORITY_NORMAL;
	thread->cName = cName;

	// 2 handles to the thread:
	// 	One for spawning the thread, 
	// 	and the other for remaining during the thread's life.
	thread->handles = 2;

	// Allocate the thread's stacks.
	uintptr_t kernelStackSize = userland ? 0x4000 /* 16KB */ : 0x10000 /* 64KB */;
	uintptr_t userStackReserve = userland ? 0x400000 /* 4MB */ : kernelStackSize;
	uintptr_t userStackCommit = userland ? 0x20000 /* 128KB */ : 0;
	uintptr_t stack = 0, kernelStack = (uintptr_t) MMStandardAllocate(kernelMMSpace, kernelStackSize, MM_REGION_FIXED);

	if (!kernelStack) goto fail;

	if (userland) {
		stack = (uintptr_t) MMStandardAllocate(process->vmm, userStackReserve, ES_FLAGS_DEFAULT, nullptr, false);

		MMRegion *region = MMFindAndPinRegion(process->vmm, stack, userStackReserve);
		KMutexAcquire(&process->vmm->reserveMutex);
#ifdef K_STACK_GROWS_DOWN
		bool success = MMCommitRange(process->vmm, region, (userStackReserve - userStackCommit) / K_PAGE_SIZE, userStackCommit / K_PAGE_SIZE); 
#else
		bool success = MMCommitRange(process->vmm, region, 0, userStackCommit / K_PAGE_SIZE); 
#endif
		KMutexRelease(&process->vmm->reserveMutex);
		MMUnpinRegion(process->vmm, region);
		if (!success) goto fail;
	} else {
		stack = kernelStack;
	}

	if (!stack) goto fail;

	KernelLog(LOG_INFO, "Scheduler", "thread stacks", 
			"Spawning thread with stacks (k,u): %x->%x, %x->%x\n", kernelStack, kernelStack + kernelStackSize, stack, stack + userStackReserve);

	thread->kernelStackBase = kernelStack;
	thread->userStackBase = userland ? stack : 0;
	thread->userStackReserve = userStackReserve;
	thread->userStackCommit = userStackCommit;
	thread->paused = flags & SPAWN_THREAD_PAUSED;
	thread->terminatableState = userland ? THREAD_TERMINATABLE : THREAD_IN_SYSCALL;
	thread->interruptContext = ArchInitialiseThread(kernelStack, kernelStackSize, thread, 
			startAddress, argument1, argument2, 
			userland, stack, userStackReserve);

	InsertNewThread(thread, ~flags & SPAWN_THREAD_MANUALLY_ACTIVATED, process);
	return thread;

	fail:;
	if (stack) MMFree(process->vmm, (void *) stack);
	if (kernelStack) MMFree(kernelMMSpace, (void *) kernelStack);
	threadPool.Remove(thread);
	return nullptr;
}

void Scheduler::TerminateProcess(Process *process, int status) {
	KSpinlockAcquire(&scheduler.lock);

	KernelLog(LOG_INFO, "Scheduler", "terminate process", "Terminating process %d '%z' with status %i...\n", 
			process->id, process->cExecutableName, status);
	process->exitStatus = status;
	process->terminating = true;

	Thread *currentThread = GetCurrentThread();
	bool isCurrentProcess = process == currentThread->process;
	bool foundCurrentThread = false;

	LinkedItem<Thread> *thread = process->threads.firstItem;

	while (thread) {
		Thread *threadObject = thread->thisItem;
		thread = thread->nextItem;

		if (threadObject != currentThread) {
			TerminateThread(threadObject, true);
		} else if (isCurrentProcess) {
			foundCurrentThread = true;
		} else {
			KernelPanic("Scheduler::TerminateProcess - Found current thread in the wrong process?!\n");
		}
	}

	if (!foundCurrentThread && isCurrentProcess) {
		KernelPanic("Scheduler::TerminateProcess - Could not find current thread in the current process?!\n");
	} else if (isCurrentProcess) {
		// This doesn't return.
		TerminateThread(currentThread, true);
	}

	KSpinlockRelease(&scheduler.lock);
	ProcessorFakeTimerInterrupt(); // Process the asynchronous tasks.
}

void Scheduler::TerminateThread(Thread *thread, bool terminatingProcess) {
	// Overview:
	//	Set terminating true, and paused false.
	// 	Is this the current thread?
	// 		Mark as terminatable, then yield.
	// 	Else, is thread->terminating not set?
	// 		Set thread->terminating.
	//
	// 		Is this the current thread?
	// 			Mark as terminatable, then yield.
	// 		Else, are we executing user code?
	// 			If we aren't currently executing the thread, remove the thread from its scheduling queue and kill it.
	//		Else, is the user waiting on a mutex/event?
	//			If we aren't currently executing the thread, unblock the thread.
		
	if (!terminatingProcess) {
		KSpinlockAcquire(&scheduler.lock);
	} else {
		KSpinlockAssertLocked(&scheduler.lock);
	}

	bool yield = false;

	if (thread->terminating) {
		KernelLog(LOG_INFO, "Scheduler", "thread already terminating", "Already terminating thread %d.\n", thread->id);
		if (thread == GetCurrentThread()) goto terminateThisThread;
		else goto done;
	}

	KernelLog(LOG_INFO, "Scheduler", "terminate thread", "Terminating thread %d...\n", thread->id);
	thread->terminating = true;
	thread->paused = false;

	if (thread == GetCurrentThread()) {
		terminateThisThread:;

		// Mark the thread as terminatable.
		thread->terminatableState = THREAD_TERMINATABLE;
		KSpinlockRelease(&scheduler.lock);

		// We cannot return to the previous function as it expects to be killed.
		ProcessorFakeTimerInterrupt();
		KernelPanic("Scheduler::TerminateThread - ProcessorFakeTimerInterrupt returned.\n");
	} else {
		if (thread->terminatableState == THREAD_TERMINATABLE) {
			// We're in user code..

			if (thread->executing) {
				// The thread is executing, so the next time it tries to make a system call or
				// is pre-empted, it will be terminated.
			} else {
				if (thread->state != THREAD_ACTIVE) {
					KernelPanic("Scheduler::TerminateThread - Terminatable thread non-active.\n");
				}

				// The thread is terminatable and it isn't executing.
				// Remove it from its queue, and then remove the thread.
				thread->item.RemoveFromList();
				RegisterAsyncTask(KillThread, thread, thread->process, true);
				yield = true;
			}
		} else if (thread->terminatableState == THREAD_USER_BLOCK_REQUEST) {
			// We're in the kernel, but because the user wanted to block on a mutex/event.

			if (thread->executing) {
				// The mutex and event waiting code is designed to recognise when a thread is in this state,
				// and exit to the system call handler immediately.
				// If the thread however is pre-empted while in a blocked state before this code can execute,
				// Scheduler::Yield will automatically force the thread to be active again.
			} else {
				// Unblock the thread.
				// See comment above.
				if (thread->state == THREAD_WAITING_MUTEX || thread->state == THREAD_WAITING_EVENT) {
					UnblockThread(thread);
				}
			}
		} else {
			// The thread is executing kernel code.
			// Therefore, we can't simply terminate the thread.
			// The thread will set its state to THREAD_TERMINATABLE whenever it can be terminated.
		}
	}

	done:;

	if (!terminatingProcess) {
		KSpinlockRelease(&scheduler.lock);
		if (yield) ProcessorFakeTimerInterrupt(); // Process the asynchronous task.
	}
}

void NewProcess() {
	Process *thisProcess = GetCurrentThread()->process;

	KernelLog(LOG_INFO, "Scheduler", "new process", 
			"New process %d %x, '%z'.\n", thisProcess->id, thisProcess, thisProcess->cExecutableName);

	KLoadedExecutable api = {};
	api.isDesktop = true;
	EsError loadError = ES_SUCCESS;

	{
		uint64_t fileFlags = ES_FILE_READ | ES_NODE_FAIL_IF_NOT_FOUND;
		KNodeInformation node = FSNodeOpen(EsLiteral(K_DESKTOP_EXECUTABLE), fileFlags);
		loadError = node.error;

		if (node.error == ES_SUCCESS) {
			if (node.node->directoryEntry->type != ES_NODE_FILE) {
				loadError = ES_ERROR_INCORRECT_NODE_TYPE;
			} else {
				loadError = KLoadELF(node.node, &api);
			}

			CloseHandleToObject(node.node, KERNEL_OBJECT_NODE, fileFlags);
		}
	}

	KLoadedExecutable application = {};

	if (thisProcess != desktopProcess && loadError == ES_SUCCESS) {
		loadError = KLoadELF(thisProcess->executableNode, &application);
	}

	bool success = loadError == ES_SUCCESS;

	if (success) {
		// We "link" the API by putting its table of function pointers at a known address.

		MMSharedRegion *tableRegion = MMSharedOpenRegion(EsLiteral("Desktop.APITable"), 0xF000, ES_FLAGS_DEFAULT); 
		// TODO Write protection.

		if (!MMMapShared(thisProcess->vmm, tableRegion, 0, 0xF000, ES_FLAGS_DEFAULT, ES_API_BASE)) { 
			success = false;
		}
	}

	EsProcessStartupInformation *startupInformation;

	if (success) {
		startupInformation = (EsProcessStartupInformation *) MMStandardAllocate(
				thisProcess->vmm, sizeof(EsProcessStartupInformation), ES_FLAGS_DEFAULT);

		if (!startupInformation) {
			success = false;
		} else {
			startupInformation->isDesktop = thisProcess == desktopProcess;
			startupInformation->isBundle = application.isBundle;
			startupInformation->applicationStartAddress = application.startAddress;
			startupInformation->tlsImageStart = application.tlsImageStart;
			startupInformation->tlsImageBytes = application.tlsImageBytes;
			startupInformation->tlsBytes = application.tlsBytes;
			startupInformation->timeStampTicksPerMs = timeStampTicksPerMs;
			startupInformation->optimalWorkQueueThreadCount = scheduler.currentProcessorID; // TODO Update this as processors are added/removed.
			EsMemoryCopy(&startupInformation->data, &thisProcess->data, sizeof(EsProcessCreateData));
		}
	}

	if (success) {
		uint64_t threadFlags = SPAWN_THREAD_USERLAND;
		if (thisProcess->creationFlags & ES_PROCESS_CREATE_PAUSED) threadFlags |= SPAWN_THREAD_PAUSED;

		thisProcess->executableState = ES_PROCESS_EXECUTABLE_LOADED;
		thisProcess->executableMainThread = scheduler.SpawnThread("MainThread", api.startAddress, 
				(uintptr_t) startupInformation, threadFlags, thisProcess);

		if (!thisProcess->executableMainThread) {
			success = false;
		}
	}

	if (!success) {
		if (thisProcess->type != PROCESS_NORMAL) {
			KernelPanic("NewProcess - Failed to start the critical process %z.\n", thisProcess->cExecutableName);
		}

		thisProcess->executableState = ES_PROCESS_EXECUTABLE_FAILED_TO_LOAD;
	}

	KEventSet(&thisProcess->executableLoadAttemptComplete);
}

bool Process::Start(char *imagePath, size_t imagePathLength) {
	uint64_t flags = ES_FILE_READ | ES_NODE_FAIL_IF_NOT_FOUND;
	KNodeInformation node = FSNodeOpen(imagePath, imagePathLength, flags);
	bool result = false;

	if (!ES_CHECK_ERROR(node.error)) {
		if (node.node->directoryEntry->type == ES_NODE_FILE) {
			result = StartWithNode(node.node);
		}

		CloseHandleToObject(node.node, KERNEL_OBJECT_NODE, flags);
	}

	if (!result && type == PROCESS_DESKTOP) {
		KernelPanic("Process::Start - Could not load the desktop executable.\n");
	}

	return result;
}

bool Process::StartWithNode(KNode *node) {
	KSpinlockAcquire(&scheduler.lock);

	if (executableStartRequest) {
		KSpinlockRelease(&scheduler.lock);
		return false;
	}

	executableStartRequest = true;

	KSpinlockRelease(&scheduler.lock);

	KWriterLockTake(&node->writerLock, K_LOCK_SHARED);
	size_t byteCount = node->directoryEntry->item.key.longKeyBytes;
	if (byteCount > ES_SNAPSHOT_MAX_PROCESS_NAME_LENGTH) byteCount = ES_SNAPSHOT_MAX_PROCESS_NAME_LENGTH;
	EsMemoryCopy(cExecutableName, node->directoryEntry->item.key.longKey, byteCount);
	cExecutableName[byteCount] = 0;
	KWriterLockReturn(&node->writerLock, K_LOCK_SHARED);

	bool success = MMSpaceInitialise(vmm);
	if (!success) return false;

	// NOTE If you change these flags, make sure to update the flags when the handle is closed!

	if (!OpenHandleToObject(node, KERNEL_OBJECT_NODE, ES_FILE_READ)) {
		KernelPanic("Process::StartWithNode - Could not open read handle to node %x.\n", node);
	}

	executableNode = node;

	KSpinlockAcquire(&scheduler.lock);
	scheduler.allProcesses.InsertEnd(&allItem);
	scheduler.activeProcessCount++;
	scheduler.blockShutdownProcessCount++;
	KSpinlockRelease(&scheduler.lock);

	Thread *newProcessThread = scheduler.SpawnThread("NewProcess", (uintptr_t) NewProcess, 0, ES_FLAGS_DEFAULT, this);

	if (!newProcessThread) {
		CloseHandleToObject(this, KERNEL_OBJECT_PROCESS);
		return false;
	}

	CloseHandleToObject(newProcessThread, KERNEL_OBJECT_THREAD);
	KEventWait(&executableLoadAttemptComplete, ES_WAIT_NO_TIMEOUT);

	if (executableState == ES_PROCESS_EXECUTABLE_FAILED_TO_LOAD) {
		KernelLog(LOG_ERROR, "Scheduler", "executable load failure", "Executable failed to load.\n");
		return false;
	}

	return true;
}

Process *Scheduler::SpawnProcess(ProcessType processType) {
	if (shutdown) return nullptr;

	Process *process = processType == PROCESS_KERNEL ? kernelProcess : (Process *) processPool.Add(sizeof(Process));

	if (!process) {
		return nullptr;
	}

	process->vmm = processType == PROCESS_KERNEL ? kernelMMSpace : (MMSpace *) mmSpacePool.Add(sizeof(MMSpace));

	if (!process->vmm) {
		processPool.Remove(process);
		return nullptr;
	}

	KSpinlockAcquire(&scheduler.lock);
	process->id = nextProcessID++;
	KSpinlockRelease(&scheduler.lock);

	process->allItem.thisItem = process;
	process->handles = 1;
	process->handleTable.process = process;
	process->permissions = ES_PERMISSION_ALL;
	process->type = processType;

	if (processType == PROCESS_KERNEL) {
		EsCRTstrcpy(process->cExecutableName, "Kernel");
		scheduler.allProcesses.InsertEnd(&process->allItem);
		MMInitialise();								
		scheduler.SpawnThread("InitKernel", (uintptr_t) KernelInitialise);
	}

	return process; 
}

void Thread::SetAddressSpace(MMSpace *space) {
	temporaryAddressSpace = space;

	if (this == GetCurrentThread()) {
		ProcessorSetAddressSpace(VIRTUAL_ADDRESS_SPACE_IDENTIFIER(space ? space : kernelMMSpace));
	}
}

void AsyncTaskThread() {
	CPULocalStorage *local = GetLocalStorage();

	while (true) {
		if (local->asyncTasksRead == local->asyncTasksWrite) {
			ProcessorFakeTimerInterrupt();
		} else {
			volatile AsyncTask *task = local->asyncTasks + local->asyncTasksRead;
			if (task->addressSpace) local->currentThread->SetAddressSpace(task->addressSpace);
			task->callback(task->argument);
			local->currentThread->SetAddressSpace(nullptr);
			local->asyncTasksRead++;
		}
	}
}

void Scheduler::CreateProcessorThreads(CPULocalStorage *local) {
	Thread *idleThread = (Thread *) threadPool.Add(sizeof(Thread));
	idleThread->isKernelThread = true;
	idleThread->state = THREAD_ACTIVE;
	idleThread->executing = true;
	idleThread->type = THREAD_IDLE;
	idleThread->terminatableState = THREAD_IN_SYSCALL;
	idleThread->cName = "Idle";
	local->currentThread = local->idleThread = idleThread;

	KSpinlockAcquire(&lock);

	if (currentProcessorID >= K_MAX_PROCESSORS) { 
		KernelPanic("Scheduler::CreateProcessorThreads - Maximum processor count (%d) exceeded.\n", currentProcessorID);
	}
	
	local->processorID = currentProcessorID++;

	// Force release the lock because we've changed our currentThread value.
	KSpinlockRelease(&lock);

	InsertNewThread(idleThread, false, kernelProcess);

	local->asyncTaskThread = SpawnThread("AsyncTasks", (uintptr_t) AsyncTaskThread, 0, SPAWN_THREAD_MANUALLY_ACTIVATED);
	local->asyncTaskThread->type = THREAD_ASYNC_TASK;
}

void RegisterAsyncTask(KAsyncTaskCallback callback, EsGeneric argument, Process *targetProcess, bool needed, bool unlocked) {
	if (!unlocked) KSpinlockAssertLocked(&scheduler.lock);
	else KSpinlockAcquire(&scheduler.lock);
	EsDefer(if (unlocked) KSpinlockRelease(&scheduler.lock));

	if (targetProcess == nullptr) {
		targetProcess = kernelProcess;
	}

	CPULocalStorage *local = GetLocalStorage();

	int difference = local->asyncTasksWrite - local->asyncTasksRead;
	if (difference < 0) difference += MAX_ASYNC_TASKS;

	if (difference >= MAX_ASYNC_TASKS / 2 && !needed) {
		return;
	}

	if (difference == MAX_ASYNC_TASKS - 1) {
		KernelPanic("RegisterAsyncTask - Maximum number of queued asynchronous tasks reached.\n");
	}

	// We need to register tasks for terminating processes.
#if 0
	if (!targetProcess->handles) {
		KernelPanic("RegisterAsyncTask - Process has no handles.\n");
	}
#endif

	volatile AsyncTask *task = local->asyncTasks + local->asyncTasksWrite;
	task->callback = callback;
	task->argument = argument.p;
	task->addressSpace = targetProcess->vmm;
	local->asyncTasksWrite++;
}

void KRegisterAsyncTask(KAsyncTaskCallback callback, EsGeneric argument, bool needed) {
	RegisterAsyncTask(callback, argument, kernelProcess, needed, true);
}

void Scheduler::RemoveProcess(Process *process) {
	KernelLog(LOG_INFO, "Scheduler", "remove process", "Removing process %d.\n", process->id);

	bool started = process->executableStartRequest;

	if (started) {
		// Make sure that the process cannot be opened.

		KSpinlockAcquire(&lock);

		allProcesses.Remove(&process->allItem);

		if (pmm.nextProcessToBalance == process) {
			// If the balance thread got interrupted while balancing this process,
			// start at the beginning of the next process.

			pmm.nextProcessToBalance = process->allItem.nextItem ? process->allItem.nextItem->thisItem : nullptr;
			pmm.nextRegionToBalance = nullptr;
			pmm.balanceResumePosition = 0;
		}

		KSpinlockRelease(&lock);

		// At this point, no pointers to the process (should) remain (I think).

		if (!process->allThreadsTerminated) {
			KernelPanic("Scheduler::RemoveProcess - The process is being removed before all its threads have terminated?!\n");
		}

		// Close the handle to the executable node.

		CloseHandleToObject(process->executableNode, KERNEL_OBJECT_NODE, ES_FILE_READ);
	}

	// Destroy the process's handle table, if it has already been destroyed.
	// For most processes, the handle table is destroyed when the last thread terminates.

	process->handleTable.Destroy();

	// Free all the remaining messages in the message queue.
	// This is done after closing all handles, since closing handles can generate messages.

	process->messageQueue.messages.Free();

	// Free the process.

	KRegisterAsyncTask([] (EsGeneric _process) { 
		Process *process = (Process *) _process.p;
		MMSpace *space = process->vmm;
		if (process->executableStartRequest) MMFinalizeVAS(space);
		scheduler.mmSpacePool.Remove(space);
		scheduler.processPool.Remove(process); 
	}, process);

	if (started) {
		// If all processes (except the kernel process) have terminated, set the scheduler's killedEvent.

		KSpinlockAcquire(&scheduler.lock);
		scheduler.blockShutdownProcessCount--;

		if (!scheduler.blockShutdownProcessCount) {
			KEventSet(&scheduler.killedEvent, true, false);
		}

		KSpinlockRelease(&scheduler.lock);
	}
}

void Scheduler::RemoveThread(Thread *thread) {
	KernelLog(LOG_INFO, "Scheduler", "remove thread", "Removing thread %d.\n", thread->id);

	// The last handle to the thread has been closed,
	// so we can finally deallocate the thread.

#ifdef ENABLE_POSIX_SUBSYSTEM
	if (thread->posixData) {
		if (thread->posixData->forkStack) {
			EsHeapFree(thread->posixData->forkStack, thread->posixData->forkStackSize, K_PAGED);
			CloseHandleToObject(thread->posixData->forkProcess, KERNEL_OBJECT_PROCESS);
		}

		EsHeapFree(thread->posixData, sizeof(POSIXThread), K_PAGED);
	}
#endif

	// TODO Deallocate user and kernel stacks.

	scheduler.threadPool.Remove(thread);
}

void Scheduler::CrashProcess(Process *process, EsCrashReason *crashReason) {
	if (process == kernelProcess) {
		KernelPanic("Scheduler::CrashProcess - Kernel process has crashed (%d).\n", crashReason->errorCode);
	}

	if (process->type != PROCESS_NORMAL) {
		KernelPanic("Scheduler::CrashProcess - A critical process has crashed (%d).\n", crashReason->errorCode);
	}

	if (GetCurrentThread()->process != process) {
		KernelPanic("Scheduler::CrashProcess - Attempt to crash process from different process.\n");
	}

	KMutexAcquire(&process->crashMutex);

	if (process->crashed) {
		KMutexRelease(&process->crashMutex);
		return;
	}

	process->crashed = true;

	KernelLog(LOG_ERROR, "Scheduler", "process crashed", "Process %x has crashed! (%d)\n", process, crashReason->errorCode);

	EsMemoryCopy(&process->crashReason, crashReason, sizeof(EsCrashReason));

	if (!shutdown) {
		_EsMessageWithObject m;
		EsMemoryZero(&m, sizeof(m));
		m.message.type = ES_MSG_APPLICATION_CRASH;
		m.message.crash.pid = process->id;
		EsMemoryCopy(&m.message.crash.reason, crashReason, sizeof(EsCrashReason));
		desktopProcess->messageQueue.SendMessage(&m);
	}

	KMutexRelease(&process->crashMutex);

	// TODO Shouldn't this be done before sending the desktop message?
	scheduler.PauseProcess(GetCurrentThread()->process, false);
}

void Scheduler::PauseThread(Thread *thread, bool resume, bool lockAlreadyAcquired) {
	if (!lockAlreadyAcquired) KSpinlockAcquire(&lock);

	if (thread->paused == !resume) {
		return;
	}

	thread->paused = !resume;

	if (!resume && thread->terminatableState == THREAD_TERMINATABLE) {
		if (thread->state == THREAD_ACTIVE) {
			if (thread->executing) {
				if (thread == GetCurrentThread()) {
					KSpinlockRelease(&lock);

					// Yield.
					ProcessorFakeTimerInterrupt();

					if (thread->paused) {
						KernelPanic("Scheduler::PauseThread - Current thread incorrectly resumed.\n");
					}
				} else {
					// The thread is executing, but on a different processor.
					// Send them an IPI to stop.
					thread->receivedYieldIPI = false;
					KSpinlockAcquire(&ipiLock);
					ProcessorSendIPI(YIELD_IPI, false);
					KSpinlockRelease(&ipiLock);
					while (!thread->receivedYieldIPI); // Spin until the thread gets the IPI.
					// TODO The interrupt context might not be set at this point.
				}
			} else {
				// Remove the thread from the active queue, and put it into the paused queue.
				thread->item.RemoveFromList();
				AddActiveThread(thread, false);
			}
		} else {
			// The thread doesn't need to be in the paused queue as it won't run anyway.
			// If it is unblocked, then AddActiveThread will put it into the correct queue.
		}
	} else if (resume && thread->item.list == &pausedThreads) {
		// Remove the thread from the paused queue, and put it into the active queue.
		pausedThreads.Remove(&thread->item);
		AddActiveThread(thread, false);
	}

	if (!lockAlreadyAcquired) KSpinlockRelease(&lock);
}

void Scheduler::PauseProcess(Process *process, bool resume) {
	Thread *currentThread = GetCurrentThread();
	bool isCurrentProcess = process == currentThread->process;
	bool foundCurrentThread = false;

	{
		KSpinlockAcquire(&scheduler.lock);
		EsDefer(KSpinlockRelease(&scheduler.lock));

		LinkedItem<Thread> *thread = process->threads.firstItem;

		while (thread) {
			Thread *threadObject = thread->thisItem;
			thread = thread->nextItem;

			if (threadObject != currentThread) {
				PauseThread(threadObject, resume, true);
			} else if (isCurrentProcess) {
				foundCurrentThread = true;
			} else {
				KernelPanic("Scheduler::PauseProcess - Found current thread in the wrong process?!\n");
			}
		}
	}

	if (!foundCurrentThread && isCurrentProcess) {
		KernelPanic("Scheduler::PauseProcess - Could not find current thread in the current process?!\n");
	} else if (isCurrentProcess) {
		PauseThread(currentThread, resume, false);
	}
}

void _RemoveProcess(EsGeneric process) {
	scheduler.RemoveProcess((Process *) process.p);
}

void CloseHandleToProcess(void *_process) {
	KSpinlockAcquire(&scheduler.lock);
	Process *process = (Process *) _process;
	if (!process->handles) KernelPanic("CloseHandleToProcess - All handles to the process have been closed.\n");
	process->handles--;
	bool removeProcess = !process->handles;

	KernelLog(LOG_VERBOSE, "Scheduler", "close process handle", "Closed handle to process %d; %d handles remain.\n", process->id, process->handles);

	if (removeProcess && process->executableStartRequest) {
		// This must be done in the correct virtual address space!
		RegisterAsyncTask(_RemoveProcess, process, process, true);
	}

	KSpinlockRelease(&scheduler.lock);

	if (removeProcess && !process->executableStartRequest) {
		// The process was never started, so we can't make a RemoveProcess task, because it doesn't have an MMSpace yet.
		scheduler.RemoveProcess(process);
	}

	ProcessorFakeTimerInterrupt(); // Process the asynchronous task.
}

void CloseHandleToThread(void *_thread) {
	KSpinlockAcquire(&scheduler.lock);
	Thread *thread = (Thread *) _thread;
	if (!thread->handles) KernelPanic("CloseHandleToThread - All handles to the thread have been closed.\n");
	thread->handles--;
	bool removeThread = thread->handles == 0;
	// EsPrint("Thread %d has %d handles\n", thread->id, thread->handles);
	KSpinlockRelease(&scheduler.lock);

	if (removeThread) {
		scheduler.RemoveThread(thread);
	}
}

void KillThread(EsGeneric _thread) {
	Thread *thread = (Thread *) _thread.p;

	KSpinlockAcquire(&scheduler.lock);
	scheduler.allThreads.Remove(&thread->allItem);
	thread->process->threads.Remove(&thread->processItem);

	KernelLog(LOG_INFO, "Scheduler", "killing thread", 
			"Killing thread (ID %d, %d remain in process %d) %x...\n", thread->id, thread->process->threads.count, thread->process->id, _thread);

	if (thread->process->threads.count == 0) {
		Process *process = thread->process;
		KernelLog(LOG_INFO, "Scheduler", "killing process", 
				"Killing process (%d) %x...\n", process->id, process);

		process->allThreadsTerminated = true;
		scheduler.activeProcessCount--;

		bool setProcessKilledEvent = true;

#ifdef ENABLE_POSIX_SUBSYSTEM
		if (process->posixForking) {
			// If the process is from an incomplete vfork(),
			// then the parent process gets to set the killed event
			// and the exit status.
			setProcessKilledEvent = false;
		}
#endif

		if (setProcessKilledEvent) {
			// We can now also set the killed event on the process.
			KEventSet(&process->killedEvent, true);
		}

		KSpinlockRelease(&scheduler.lock);

		// There are no threads left in this process.
		// We should destroy the handle table at this point.
		// Otherwise, the process might never be freed
		// because of a cyclic-dependency.
		process->handleTable.Destroy();

		// Destroy the virtual memory space.
		// Don't actually deallocate it yet though; that is done on an async task queued by RemoveProcess.
		// This must be destroyed after the handle table!
		MMSpaceDestroy(process->vmm); 

		// Tell Desktop the process has terminated.
		if (!scheduler.shutdown) {
			_EsMessageWithObject m;
			EsMemoryZero(&m, sizeof(m));
			m.message.type = ES_MSG_PROCESS_TERMINATED;
			m.message.crash.pid = process->id;
			desktopProcess->messageQueue.SendMessage(&m);
		}
	} else {
		KSpinlockRelease(&scheduler.lock);
	}

	MMFree(kernelMMSpace, (void *) thread->kernelStackBase);
	if (thread->userStackBase) MMFree(thread->process->vmm, (void *) thread->userStackBase);

	KEventSet(&thread->killedEvent);

	// Close the handle that this thread owns of its owner process.
	CloseHandleToObject(thread->process, KERNEL_OBJECT_PROCESS);

	CloseHandleToThread(thread);
}

Thread *Scheduler::PickThread(CPULocalStorage *local) {
	KSpinlockAssertLocked(&lock);

	if (local->asyncTasksRead != local->asyncTasksWrite 
			&& local->asyncTaskThread->state == THREAD_ACTIVE) {
		// If the asynchronous task thread for this processor isn't blocked, and has tasks to process, execute it.
		return local->asyncTaskThread;
	}

	for (int i = 0; i < THREAD_PRIORITY_COUNT; i++) {
		// For every priority, check if there is a thread available. If so, execute it.
		LinkedItem<Thread> *item = activeThreads[i].firstItem;
		if (!item) continue;
		item->RemoveFromList();
		return item->thisItem;
	}

	// If we couldn't find a thread to execute, idle.
	return local->idleThread;
}

void Scheduler::Yield(InterruptContext *context) {
	CPULocalStorage *local = GetLocalStorage();

	if (!started || !local || !local->schedulerReady) {
		return;
	}

	if (local->spinlockCount) {
		KernelPanic("Scheduler::Yield - Spinlocks acquired while attempting to yield (2).\n");
	}

	if (local->spinlockCount) {
		KernelPanic("Scheduler::Yield - Spinlocks acquired while attempting to yield.\n");
	}

	ProcessorDisableInterrupts(); // We don't want interrupts to get reenabled after the context switch.
	KSpinlockAcquire(&lock);

	local->currentThread->interruptContext = context;

	if (lock.interruptsEnabled) {
		KernelPanic("Scheduler::Yield - Interrupts were enabled when scheduler lock was acquired.\n");
	}

	local->currentThread->executing = false;

	bool killThread = local->currentThread->terminatableState == THREAD_TERMINATABLE 
		&& local->currentThread->terminating;
	bool keepThreadAlive = local->currentThread->terminatableState == THREAD_USER_BLOCK_REQUEST
		&& local->currentThread->terminating; // The user can't make the thread block if it is terminating.

	if (killThread) {
		local->currentThread->state = THREAD_TERMINATED;
		KernelLog(LOG_INFO, "Scheduler", "terminate yielded thread", "Terminated yielded thread %x\n", local->currentThread);
		RegisterAsyncTask(KillThread, local->currentThread, local->currentThread->process, true);
	}

	// If the thread is waiting for an object to be notified, put it in the relevant blockedThreads list.
	// But if the object has been notified yet hasn't made itself active yet, do that for it.

	else if (local->currentThread->state == THREAD_WAITING_MUTEX) {
		KMutex *mutex = local->currentThread->blocking.mutex;

		if (!keepThreadAlive && mutex->owner) {
			mutex->owner->blockedThreadPriorities[local->currentThread->priority]++;
			MaybeUpdateActiveList(mutex->owner);
			mutex->blockedThreads.InsertEnd(&local->currentThread->item);
		} else {
			local->currentThread->state = THREAD_ACTIVE;
		}
	}

	else if (local->currentThread->state == THREAD_WAITING_EVENT) {
		if (keepThreadAlive) {
			local->currentThread->state = THREAD_ACTIVE;
		} else {
			bool unblocked = false;

			for (uintptr_t i = 0; i < local->currentThread->blocking.eventCount; i++) {
				if (local->currentThread->blocking.events[i]->state) {
					local->currentThread->state = THREAD_ACTIVE;
					unblocked = true;
					break;
				}
			}

			if (!unblocked) {
				for (uintptr_t i = 0; i < local->currentThread->blocking.eventCount; i++) {
					local->currentThread->blocking.events[i]->blockedThreads.InsertEnd(&local->currentThread->blockedItems[i]);
				}
			}
		}
	}

	else if (local->currentThread->state == THREAD_WAITING_WRITER_LOCK) {
		KWriterLock *lock = local->currentThread->blocking.writerLock;

		if ((local->currentThread->blocking.writerLockType == K_LOCK_SHARED && lock->state >= 0)
				|| (local->currentThread->blocking.writerLockType == K_LOCK_EXCLUSIVE && lock->state == 0)) {
			local->currentThread->state = THREAD_ACTIVE;
		} else {
			local->currentThread->blocking.writerLock->blockedThreads.InsertEnd(&local->currentThread->item);
		}
	}

	// Put the current thread at the end of the activeThreads list.
	if (!killThread && local->currentThread->state == THREAD_ACTIVE) {
		if (local->currentThread->type == THREAD_NORMAL) {
			AddActiveThread(local->currentThread, false);
		} else if (local->currentThread->type == THREAD_IDLE || local->currentThread->type == THREAD_ASYNC_TASK) {
			// Do nothing.
		} else {
			KernelPanic("Scheduler::Yield - Unrecognised thread type\n");
		}
	}

	// Notify any triggered timers.
	
	LinkedItem<KTimer> *_timer = activeTimers.firstItem;

	while (_timer) {
		KTimer *timer = _timer->thisItem;
		LinkedItem<KTimer> *next = _timer->nextItem;

		if (timer->triggerTimeMs <= timeMs) {
			activeTimers.Remove(_timer);
			KEventSet(&timer->event, true /* scheduler already locked */);

			if (timer->callback) {
				RegisterAsyncTask(timer->callback, timer->argument, nullptr, true);
			}
		} else {
			break; // Timers are kept sorted, so there's no point continuing.
		}

		_timer = next;
	}

	// Get the next thread to execute.
	Thread *newThread = local->currentThread = PickThread(local);

	if (!newThread) {
		KernelPanic("Scheduler::Yield - Could not find a thread to execute.\n");
	}

	if (newThread->executing) {
		KernelPanic("Scheduler::Yield - Thread (ID %d) in active queue already executing with state %d, type %d.\n", 
				local->currentThread->id, local->currentThread->state, local->currentThread->type);
	}

	// Store information about the thread.
	newThread->executing = true;
	newThread->executingProcessorID = local->processorID;
	newThread->cpuTimeSlices++;
	if (newThread->type == THREAD_IDLE) newThread->process->idleTimeSlices++;
	else newThread->process->cpuTimeSlices++;

	// Prepare the next timer interrupt.
	uint64_t time = 1;
	ArchNextTimer(time);

	InterruptContext *newContext = newThread->interruptContext;

	if (!local->processorID) {
		// Update the scheduler's time.
		timeMs = ProcessorReadTimeStamp() / KGetTimeStampTicksPerMs(); 

		// Update the time stamp counter synchronization value.
		extern volatile uint64_t timeStampCounterSynchronizationValue;
		timeStampCounterSynchronizationValue = ((timeStampCounterSynchronizationValue & 0x8000000000000000) 
				^ 0x8000000000000000) | ProcessorReadTimeStamp();
	}

	MMSpace *addressSpace = newThread->process->vmm;
	if (newThread->temporaryAddressSpace) addressSpace = newThread->temporaryAddressSpace;
	DoContextSwitch(newContext, VIRTUAL_ADDRESS_SPACE_IDENTIFIER(addressSpace), newThread->kernelStack, newThread);
}

void Scheduler::Shutdown() {
	// Prevent the creation of new proceses or threads,
	// or terminate this thread if we're already shutting down.
	if (__sync_val_compare_and_swap(&scheduler.shutdown, false, true)) KThreadTerminate();

	// Close our handle to the desktop process.
	CloseHandleToObject(desktopProcess->executableMainThread, KERNEL_OBJECT_THREAD);
	CloseHandleToObject(desktopProcess, KERNEL_OBJECT_PROCESS);

	KernelLog(LOG_INFO, "Scheduler", "killing all processes", "Scheduler::Destroy - Killing all processes....\n");

	while (true) {
		KSpinlockAcquire(&lock);
		Process *process = allProcesses.firstItem->thisItem;

		while (process && (process->terminating || process == kernelProcess)) {
			LinkedItem<Process> *item = process->allItem.nextItem;
			process = item ? item->thisItem : nullptr;
		}

		KSpinlockRelease(&lock);
		if (!process) break;

		TerminateProcess(process, -1);
	}

	KEventWait(&killedEvent);
}

Process *Scheduler::OpenProcess(uint64_t id) {
	KSpinlockAcquire(&scheduler.lock);

	LinkedItem<Process> *item = scheduler.allProcesses.firstItem;

	while (item) {
		Process *process = item->thisItem;

		if (process->id == id 
				&& process->handles /* if the process has no handles, it's about to be removed */
				&& process->type != PROCESS_KERNEL /* the kernel process cannot be opened */) {
			process->handles++;
			break;
		}

		item = item->nextItem;
	}

	KSpinlockRelease(&scheduler.lock);

	return item ? item->thisItem : nullptr;
}

bool KThreadCreate(const char *cName, void (*startAddress)(uintptr_t), uintptr_t argument) {
	return scheduler.SpawnThread(cName, (uintptr_t) startAddress, argument) ? true : false;
}

void KThreadTerminate() {
	scheduler.TerminateThread(GetCurrentThread());
}

void KYield() {
	ProcessorFakeTimerInterrupt();
}

uint64_t KCPUCurrentID() 	{ return GetLocalStorage() ->processorID; }
uint64_t KProcessCurrentID()	{ return GetCurrentThread()->process->id; }
uint64_t KThreadCurrentID()	{ return GetCurrentThread()         ->id; }

#endif
