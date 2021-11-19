// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

// TODO Review vforking interaction from the POSIX subsystem with the process termination algorithm.
// TODO Simplify or remove asynchronous task thread semantics.
// TODO Break up or remove dispatchSpinlock.

// How thread termination works:
// 1. ThreadTerminate
// 	- terminating is set to true.
// 	- If the thread is executing, then on the next context switch, ThreadKill is called on a async task. 
// 	- If it is not executing, ThreadKill is called on a async task.
// 	- Note, terminatableState must be set to THREAD_TERMINATABLE.
// 	- When a thread terminates itself, its terminatableState is automatically set to THREAD_TERMINATABLE.
// 2. ThreadKill
// 	- Removes the thread from the lists, frees the stacks, and sets killedEvent.
// 	- The thread's handles to itself and its process are closed.
// 	- If this is the last thread in the process, ProcessKill is called.
// 3. CloseHandleToObject KERNEL_OBJECT_THREAD
// 	- If the last handle to the thread has been closed, then ThreadRemove is called.
// 4. ThreadRemove
// 	- The thread structure is deallocated.

// How process termination works:
// 1. ProcessTerminate (optional)
// 	- preventNewThreads is set to true.
// 	- ThreadTerminate is called on each thread, leading to an eventual call to ProcessKill.
// 	- This is optional because ProcessKill is called if all threads get terminated naturally; in this case, preventNewThreads is never set.
// 2. ProcessKill
// 	- Removes the process from the lists, destroys the handle table and memory space, and sets killedEvent.
// 	- Sends a message to Desktop informing it the process was killed.
// 	- Since ProcessKill is only called from ThreadKill, there is an associated closing of a process handle from the killed thread.
// 3. CloseHandleToObject KERNEL_OBJECT_PROCESS
// 	- If the last handle to the process has been closed, then ProcessRemove is called.
// 4. ProcessRemove
// 	- Destroys the message queue, and closes the handle to the executable node.
// 	- The process and memory space structures are deallocated.

#ifndef IMPLEMENTATION

#define THREAD_PRIORITY_NORMAL 	(0) // Lower value = higher priority.
#define THREAD_PRIORITY_LOW 	(1)
#define THREAD_PRIORITY_COUNT	(2)

enum ThreadState : int8_t {
	THREAD_ACTIVE,			// An active thread. Not necessarily executing; `executing` determines if it executing.
	THREAD_WAITING_MUTEX,		// Waiting for a mutex to be released.
	THREAD_WAITING_EVENT,		// Waiting for a event to be notified.
	THREAD_WAITING_WRITER_LOCK,	// Waiting for a writer lock to be notified.
	THREAD_TERMINATED,		// The thread has been terminated. It will be deallocated when all handles are closed.
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
	// ** Must be the first item in the structure; see MMArchSafeCopy. **
	bool inSafeCopy;

	LinkedItem<Thread> item;        // Entry in relevent thread queue or blockedThreads list for mutexes/writer locks.
	LinkedItem<Thread> allItem;     // Entry in the allThreads list.
	LinkedItem<Thread> processItem; // Entry in the process's list of threads.

	struct Process *process;

	EsObjectID id;
	volatile uintptr_t cpuTimeSlices;
	volatile size_t handles;
	uint32_t executingProcessorID;

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
	volatile bool paused;	   	// Set to pause a thread. Paused threads are not executed (unless the terminatableState prevents that).
	volatile bool receivedYieldIPI; // Used to terminate a thread executing on a different processor.

	union {
		KMutex *volatile mutex;

		struct {
			KWriterLock *volatile writerLock;
			bool writerLockType;
		};

		struct {
			LinkedItem<Thread> *eventItems; // Entries in the blockedThreads lists (one per event).
			KEvent *volatile events[ES_MAX_WAIT_COUNT];
			volatile size_t eventCount; 
		};
	} blocking;

	KEvent killedEvent;
	KAsyncTask killAsyncTask;

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
	MMSpace *vmm;
	HandleTable handleTable;
	MessageQueue messageQueue;

	LinkedList<Thread> threads;
	KMutex threadsMutex;

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
	bool blockShutdown;
	bool preventNewThreads; // Set by ProcessTerminate.
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

struct Scheduler {
	void Yield(InterruptContext *context);
	void CreateProcessorThreads(CPULocalStorage *local);
	void AddActiveThread(Thread *thread, bool start /* put it at the start of the active list */); // Add an active thread into the queue.
	void MaybeUpdateActiveList(Thread *thread); // After changing the priority of a thread, call this to move it to the correct active thread queue if needed.
	void NotifyObject(LinkedList<Thread> *blockedThreads, bool unblockAll, Thread *previousMutexOwner = nullptr);
	void UnblockThread(Thread *unblockedThread, Thread *previousMutexOwner = nullptr);
	Thread *PickThread(CPULocalStorage *local); // Pick the next thread to execute.
	int8_t GetThreadEffectivePriority(Thread *thread);

	KSpinlock dispatchSpinlock; // For accessing synchronisation objects, thread states, scheduling lists, etc. TODO Break this up!
	KSpinlock activeTimersSpinlock; // For accessing the activeTimers lists.
	LinkedList<Thread> activeThreads[THREAD_PRIORITY_COUNT];
	LinkedList<Thread> pausedThreads;
	LinkedList<KTimer> activeTimers;

	KMutex allThreadsMutex; // For accessing the allThreads list.
	KMutex allProcessesMutex; // For accessing the allProcesses list.
	KSpinlock asyncTaskSpinlock; // For accessing the per-CPU asyncTaskList.
	LinkedList<Thread> allThreads;
	LinkedList<Process> allProcesses;

	Pool threadPool, processPool, mmSpacePool;
	EsObjectID nextThreadID, nextProcessID, nextProcessorID;

	KEvent allProcessesTerminatedEvent; // Set during shutdown when all processes have been terminated.
	volatile uintptr_t blockShutdownProcessCount;
	volatile size_t activeProcessCount;
	volatile bool started, panic, shutdown;
	uint64_t timeMs;

#ifdef DEBUG_BUILD
	EsThreadEventLogEntry *volatile threadEventLog;
	volatile uintptr_t threadEventLogPosition;
	volatile size_t threadEventLogAllocated;
#endif
};

Process *ProcessSpawn(ProcessType processType);
void ProcessRemove(Process *process);
void ProcessTerminate(Process *process, int status);
void ThreadRemove(Thread *thread);
void ThreadTerminate(Thread *thread);
void ThreadSetTemporaryAddressSpace(MMSpace *space);

#define SPAWN_THREAD_USERLAND     (1 << 0)
#define SPAWN_THREAD_LOW_PRIORITY (1 << 1)
#define SPAWN_THREAD_PAUSED       (1 << 2)
#define SPAWN_THREAD_ASYNC_TASK   (1 << 3)
#define SPAWN_THREAD_IDLE         (1 << 4)
Thread *ThreadSpawn(const char *cName, uintptr_t startAddress, uintptr_t argument1 = 0, 
		uint32_t flags = ES_FLAGS_DEFAULT, Process *process = nullptr, uintptr_t argument2 = 0);

bool DesktopSendMessage(_EsMessageWithObject *message);
EsHandle DesktopOpenHandle(void *object, uint32_t flags, KernelObjectType type);
void DesktopCloseHandle(EsHandle handle);

Process _kernelProcess;
Process *kernelProcess = &_kernelProcess;
Process *desktopProcess;
KMutex desktopMutex;
Scheduler scheduler;

#endif

#ifdef IMPLEMENTATION

void KRegisterAsyncTask(KAsyncTask *task, KAsyncTaskCallback callback) {
	KSpinlockAcquire(&scheduler.asyncTaskSpinlock);

	if (!task->callback) {
		task->callback = callback;
		GetLocalStorage()->asyncTaskList.Insert(&task->item, false);
	}

	KSpinlockRelease(&scheduler.asyncTaskSpinlock);
}

int8_t Scheduler::GetThreadEffectivePriority(Thread *thread) {
	KSpinlockAssertLocked(&dispatchSpinlock);

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
	
	KSpinlockAssertLocked(&dispatchSpinlock);

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

	KSpinlockAssertLocked(&dispatchSpinlock);

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

Thread *ThreadSpawn(const char *cName, uintptr_t startAddress, uintptr_t argument1, uint32_t flags, Process *process, uintptr_t argument2) {
	bool userland = flags & SPAWN_THREAD_USERLAND;
	Thread *parentThread = GetCurrentThread();

	if (!process) {
		process = kernelProcess;
	}

	if (userland && process == kernelProcess) {
		KernelPanic("ThreadSpawn - Cannot add userland thread to kernel process.\n");
	}

	// Adding the thread to the owner's list of threads and adding the thread to a scheduling list
	// need to be done in the same atomic block.
	KMutexAcquire(&process->threadsMutex);
	EsDefer(KMutexRelease(&process->threadsMutex));

	if (process->preventNewThreads) {
		return nullptr;
	}

	Thread *thread = (Thread *) scheduler.threadPool.Add(sizeof(Thread));
	if (!thread) return nullptr;
	KernelLog(LOG_INFO, "Scheduler", "spawn thread", "Created thread, %x to start at %x\n", thread, startAddress);

	// Allocate the thread's stacks.
#if defined(ES_BITS_64)
	uintptr_t kernelStackSize = 0x5000 /* 20KB */;
#elif defined(ES_BITS_32)
	uintptr_t kernelStackSize = 0x4000 /* 16KB */;
#endif
	uintptr_t userStackReserve = userland ? 0x400000 /* 4MB */ : kernelStackSize;
	uintptr_t userStackCommit = userland ? 0x10000 /* 64KB */ : 0;
	uintptr_t stack = 0, kernelStack = 0;

	if (flags & SPAWN_THREAD_IDLE) goto skipStackAllocation;

	kernelStack = (uintptr_t) MMStandardAllocate(kernelMMSpace, kernelStackSize, MM_REGION_FIXED);
	if (!kernelStack) goto fail;

	if (userland) {
		stack = (uintptr_t) MMStandardAllocate(process->vmm, userStackReserve, ES_FLAGS_DEFAULT, nullptr, false);

		MMRegion *region = MMFindAndPinRegion(process->vmm, stack, userStackReserve);
		KMutexAcquire(&process->vmm->reserveMutex);
#ifdef K_ARCH_STACK_GROWS_DOWN
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
	skipStackAllocation:;

	// If ProcessPause is called while a thread in that process is spawning a new thread, mark the thread as paused. 
	// This is synchronized under the threadsMutex.
	thread->paused = (parentThread && process == parentThread->process && parentThread->paused) || (flags & SPAWN_THREAD_PAUSED);

	// 2 handles to the thread:
	// 	One for spawning the thread, 
	// 	and the other for remaining during the thread's life.
	thread->handles = 2;

	thread->isKernelThread = !userland;
	thread->priority = (flags & SPAWN_THREAD_LOW_PRIORITY) ? THREAD_PRIORITY_LOW : THREAD_PRIORITY_NORMAL;
	thread->cName = cName;
	thread->kernelStackBase = kernelStack;
	thread->userStackBase = userland ? stack : 0;
	thread->userStackReserve = userStackReserve;
	thread->userStackCommit = userStackCommit;
	thread->terminatableState = userland ? THREAD_TERMINATABLE : THREAD_IN_SYSCALL;
	thread->type = (flags & SPAWN_THREAD_ASYNC_TASK) ? THREAD_ASYNC_TASK : (flags & SPAWN_THREAD_IDLE) ? THREAD_IDLE : THREAD_NORMAL;
	thread->id = __sync_fetch_and_add(&scheduler.nextThreadID, 1);
	thread->process = process;
	thread->item.thisItem = thread;
	thread->allItem.thisItem = thread;
	thread->processItem.thisItem = thread;

	if (thread->type != THREAD_IDLE) {
		thread->interruptContext = ArchInitialiseThread(kernelStack, kernelStackSize, thread, 
				startAddress, argument1, argument2, userland, stack, userStackReserve);
	} else {
		thread->state = THREAD_ACTIVE;
		thread->executing = true;
	}

	process->threads.InsertEnd(&thread->processItem);

	KMutexAcquire(&scheduler.allThreadsMutex);
	scheduler.allThreads.InsertStart(&thread->allItem);
	KMutexRelease(&scheduler.allThreadsMutex);

	// Each thread owns a handles to the owner process.
	// This makes sure the process isn't destroyed before all its threads have been destroyed.
	OpenHandleToObject(process, KERNEL_OBJECT_PROCESS, ES_FLAGS_DEFAULT);

	KernelLog(LOG_INFO, "Scheduler", "thread stacks", "Spawning thread with stacks (k,u): %x->%x, %x->%x\n", 
			kernelStack, kernelStack + kernelStackSize, stack, stack + userStackReserve);
	KernelLog(LOG_INFO, "Scheduler", "create thread", "Create thread ID %d, type %d, owner process %d\n", 
			thread->id, thread->type, process->id);

	if (thread->type == THREAD_NORMAL) {
		// Add the thread to the start of the active thread list to make sure that it runs immediately.
		KSpinlockAcquire(&scheduler.dispatchSpinlock);
		scheduler.AddActiveThread(thread, true);
		KSpinlockRelease(&scheduler.dispatchSpinlock);
	} else {
		// Idle and asynchronous task threads don't need to be added to a scheduling list.
	}

	// The thread may now be terminated at any moment.

	return thread;

	fail:;
	if (stack) MMFree(process->vmm, (void *) stack);
	if (kernelStack) MMFree(kernelMMSpace, (void *) kernelStack);
	scheduler.threadPool.Remove(thread);
	return nullptr;
}

void ProcessKill(Process *process) {
	// This function should only be called by ThreadKill, when the last thread in the process exits.
	// There should be at least one remaining handle to the process here, owned by that thread.
	// It will be closed at the end of the ThreadKill function.

	if (!process->handles) {
		KernelPanic("ProcessKill - Process %x is on the allProcesses list but there are no handles to it.\n", process);
	}

	KernelLog(LOG_INFO, "Scheduler", "killing process", "Killing process (%d) %x...\n", process->id, process);

	__sync_fetch_and_sub(&scheduler.activeProcessCount, 1);

	// Remove the process from the list of processes.
	KMutexAcquire(&scheduler.allProcessesMutex);
	scheduler.allProcesses.Remove(&process->allItem);

	if (pmm.nextProcessToBalance == process) {
		// If the balance thread got interrupted while balancing this process,
		// start at the beginning of the next process.
		pmm.nextProcessToBalance = process->allItem.nextItem ? process->allItem.nextItem->thisItem : nullptr;
		pmm.nextRegionToBalance = nullptr;
		pmm.balanceResumePosition = 0;
	}

	KMutexRelease(&scheduler.allProcessesMutex);

	KSpinlockAcquire(&scheduler.dispatchSpinlock);
	process->allThreadsTerminated = true;
	bool setProcessKilledEvent = true;

#ifdef ENABLE_POSIX_SUBSYSTEM
	if (process->posixForking) {
		// If the process is from an incomplete vfork(),
		// then the parent process gets to set the killed event
		// and the exit status.
		setProcessKilledEvent = false;
	}
#endif

	KSpinlockRelease(&scheduler.dispatchSpinlock);

	if (setProcessKilledEvent) {
		// We can now also set the killed event on the process.
		KEventSet(&process->killedEvent, true);
	}

	// There are no threads left in this process.
	// We should destroy the handle table at this point.
	// Otherwise, the process might never be freed
	// because of a cyclic-dependency.
	process->handleTable.Destroy();

	// Destroy the virtual memory space.
	// Don't actually deallocate it yet though; that is done on an async task queued by ProcessRemove.
	// This must be destroyed after the handle table!
	MMSpaceDestroy(process->vmm); 

	// Tell Desktop the process has terminated.
	if (!scheduler.shutdown) {
		_EsMessageWithObject m;
		EsMemoryZero(&m, sizeof(m));
		m.message.type = ES_MSG_PROCESS_TERMINATED;
		m.message.crash.pid = process->id;
		DesktopSendMessage(&m);
	}
}

void ThreadKill(KAsyncTask *task) {
	Thread *thread = EsContainerOf(Thread, killAsyncTask, task);
	ThreadSetTemporaryAddressSpace(thread->process->vmm);

	KMutexAcquire(&scheduler.allThreadsMutex);
	scheduler.allThreads.Remove(&thread->allItem);
	KMutexRelease(&scheduler.allThreadsMutex);

	KMutexAcquire(&thread->process->threadsMutex);
	thread->process->threads.Remove(&thread->processItem);
	bool lastThread = thread->process->threads.count == 0;
	KMutexRelease(&thread->process->threadsMutex);

	KernelLog(LOG_INFO, "Scheduler", "killing thread", 
			"Killing thread (ID %d, %d remain in process %d) %x...\n", thread->id, thread->process->threads.count, thread->process->id, thread);

	if (lastThread) {
		ProcessKill(thread->process);
	}

	MMFree(kernelMMSpace, (void *) thread->kernelStackBase);
	if (thread->userStackBase) MMFree(thread->process->vmm, (void *) thread->userStackBase);

	KEventSet(&thread->killedEvent);

	// Close the handle that this thread owns of its owner process, and the handle it owns of itself.
	CloseHandleToObject(thread->process, KERNEL_OBJECT_PROCESS);
	CloseHandleToObject(thread, KERNEL_OBJECT_THREAD);
}

void ProcessTerminate(Process *process, int status) {
	KMutexAcquire(&process->threadsMutex);

	KernelLog(LOG_INFO, "Scheduler", "terminate process", "Terminating process %d '%z' with status %i...\n", 
			process->id, process->cExecutableName, status);
	process->exitStatus = status;
	process->preventNewThreads = true;

	Thread *currentThread = GetCurrentThread();
	bool isCurrentProcess = process == currentThread->process;
	bool foundCurrentThread = false;

	LinkedItem<Thread> *thread = process->threads.firstItem;

	while (thread) {
		Thread *threadObject = thread->thisItem;
		thread = thread->nextItem;

		if (threadObject != currentThread) {
			ThreadTerminate(threadObject);
		} else if (isCurrentProcess) {
			foundCurrentThread = true;
		} else {
			KernelPanic("Scheduler::ProcessTerminate - Found current thread in the wrong process?!\n");
		}
	}

	KMutexRelease(&process->threadsMutex);

	if (!foundCurrentThread && isCurrentProcess) {
		KernelPanic("Scheduler::ProcessTerminate - Could not find current thread in the current process?!\n");
	} else if (isCurrentProcess) {
		// This doesn't return.
		ThreadTerminate(currentThread);
	}
}

void ThreadTerminate(Thread *thread) {
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
		
	KSpinlockAcquire(&scheduler.dispatchSpinlock);

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
		KSpinlockRelease(&scheduler.dispatchSpinlock);

		// We cannot return to the previous function as it expects to be killed.
		ProcessorFakeTimerInterrupt();
		KernelPanic("Scheduler::ThreadTerminate - ProcessorFakeTimerInterrupt returned.\n");
	} else {
		if (thread->terminatableState == THREAD_TERMINATABLE) {
			// We're in user code..

			if (thread->executing) {
				// The thread is executing, so the next time it tries to make a system call or
				// is pre-empted, it will be terminated.
			} else {
				if (thread->state != THREAD_ACTIVE) {
					KernelPanic("Scheduler::ThreadTerminate - Terminatable thread non-active.\n");
				}

				// The thread is terminatable and it isn't executing.
				// Remove it from its queue, and then remove the thread.
				thread->item.RemoveFromList();
				KRegisterAsyncTask(&thread->killAsyncTask, ThreadKill);
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
					scheduler.UnblockThread(thread);
				}
			}
		} else {
			// The thread is executing kernel code.
			// Therefore, we can't simply terminate the thread.
			// The thread will set its state to THREAD_TERMINATABLE whenever it can be terminated.
		}
	}

	done:;

	KSpinlockRelease(&scheduler.dispatchSpinlock);
	if (yield) ProcessorFakeTimerInterrupt(); // Process the asynchronous task.
}

void ProcessLoadExecutable() {
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

	if (thisProcess->type != PROCESS_DESKTOP && loadError == ES_SUCCESS) {
		loadError = KLoadELF(thisProcess->executableNode, &application);
	}

	bool success = loadError == ES_SUCCESS;

	if (success) {
		// We "link" the API by putting its table of function pointers at a known address.
		// TODO Write protection.

		if (!MMMapShared(thisProcess->vmm, mmAPITableRegion, 0, mmAPITableRegion->sizeBytes, ES_FLAGS_DEFAULT, ES_API_BASE)) { 
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
			startupInformation->isDesktop = thisProcess->type == PROCESS_DESKTOP;
			startupInformation->isBundle = application.isBundle;
			startupInformation->applicationStartAddress = application.startAddress;
			startupInformation->tlsImageStart = application.tlsImageStart;
			startupInformation->tlsImageBytes = application.tlsImageBytes;
			startupInformation->tlsBytes = application.tlsBytes;
			startupInformation->timeStampTicksPerMs = timeStampTicksPerMs;

			uint32_t globalDataRegionFlags = thisProcess->type == PROCESS_DESKTOP ? ES_SHARED_MEMORY_READ_WRITE : ES_FLAGS_DEFAULT;

			if (OpenHandleToObject(mmGlobalDataRegion, KERNEL_OBJECT_SHMEM, globalDataRegionFlags)) {
				startupInformation->globalDataRegion = thisProcess->handleTable.OpenHandle(mmGlobalDataRegion, globalDataRegionFlags, KERNEL_OBJECT_SHMEM);
			}

			EsMemoryCopy(&startupInformation->data, &thisProcess->data, sizeof(EsProcessCreateData));
		}
	}

	if (success) {
		uint64_t threadFlags = SPAWN_THREAD_USERLAND;
		if (thisProcess->creationFlags & ES_PROCESS_CREATE_PAUSED) threadFlags |= SPAWN_THREAD_PAUSED;

		thisProcess->executableState = ES_PROCESS_EXECUTABLE_LOADED;
		thisProcess->executableMainThread = ThreadSpawn("MainThread", api.startAddress, 
				(uintptr_t) startupInformation, threadFlags, thisProcess);

		if (!thisProcess->executableMainThread) {
			success = false;
		}
	}

	if (!success) {
		if (thisProcess->type != PROCESS_NORMAL) {
			KernelPanic("ProcessLoadExecutable - Failed to start the critical process %z.\n", thisProcess->cExecutableName);
		}

		thisProcess->executableState = ES_PROCESS_EXECUTABLE_FAILED_TO_LOAD;
	}

	KEventSet(&thisProcess->executableLoadAttemptComplete);
}

bool ProcessStartWithNode(Process *process, KNode *node) {
	// Make sure nobody has tried to start the process.

	KSpinlockAcquire(&scheduler.dispatchSpinlock);

	if (process->executableStartRequest) {
		KSpinlockRelease(&scheduler.dispatchSpinlock);
		return false;
	}

	process->executableStartRequest = true;

	KSpinlockRelease(&scheduler.dispatchSpinlock);

	// Get the name of the process from the node.

	KWriterLockTake(&node->writerLock, K_LOCK_SHARED);
	size_t byteCount = node->directoryEntry->item.key.longKeyBytes;
	if (byteCount > ES_SNAPSHOT_MAX_PROCESS_NAME_LENGTH) byteCount = ES_SNAPSHOT_MAX_PROCESS_NAME_LENGTH;
	EsMemoryCopy(process->cExecutableName, node->directoryEntry->item.key.longKey, byteCount);
	process->cExecutableName[byteCount] = 0;
	KWriterLockReturn(&node->writerLock, K_LOCK_SHARED);

	// Initialise the memory space.

	bool success = MMSpaceInitialise(process->vmm);
	if (!success) return false;

	// NOTE If you change these flags, make sure to update the flags when the handle is closed!

	if (!OpenHandleToObject(node, KERNEL_OBJECT_NODE, ES_FILE_READ)) {
		KernelPanic("ProcessStartWithNode - Could not open read handle to node %x.\n", node);
	}

	if (KEventPoll(&scheduler.allProcessesTerminatedEvent)) {
		KernelPanic("ProcessStartWithNode - allProcessesTerminatedEvent was set.\n");
	}

	process->executableNode = node;
	process->blockShutdown = true;
	__sync_fetch_and_add(&scheduler.activeProcessCount, 1);
	__sync_fetch_and_add(&scheduler.blockShutdownProcessCount, 1);

	// Add the process to the list of all processes,
	// and spawn the kernel thread to load the executable.
	// This is synchronized under allProcessesMutex so that the process can't be terminated or paused
	// until loadExecutableThread has been spawned.
	KMutexAcquire(&scheduler.allProcessesMutex);
	scheduler.allProcesses.InsertEnd(&process->allItem);
	Thread *loadExecutableThread = ThreadSpawn("ExecLoad", (uintptr_t) ProcessLoadExecutable, 0, ES_FLAGS_DEFAULT, process);
	KMutexRelease(&scheduler.allProcessesMutex);

	if (!loadExecutableThread) {
		CloseHandleToObject(process, KERNEL_OBJECT_PROCESS);
		return false;
	}

	// Wait for the executable to be loaded.

	CloseHandleToObject(loadExecutableThread, KERNEL_OBJECT_THREAD);
	KEventWait(&process->executableLoadAttemptComplete, ES_WAIT_NO_TIMEOUT);

	if (process->executableState == ES_PROCESS_EXECUTABLE_FAILED_TO_LOAD) {
		KernelLog(LOG_ERROR, "Scheduler", "executable load failure", "Executable failed to load.\n");
		return false;
	}

	return true;
}

bool ProcessStart(Process *process, char *imagePath, size_t imagePathLength) {
	uint64_t flags = ES_FILE_READ | ES_NODE_FAIL_IF_NOT_FOUND;
	KNodeInformation node = FSNodeOpen(imagePath, imagePathLength, flags);
	bool result = false;

	if (!ES_CHECK_ERROR(node.error)) {
		if (node.node->directoryEntry->type == ES_NODE_FILE) {
			result = ProcessStartWithNode(process, node.node);
		}

		CloseHandleToObject(node.node, KERNEL_OBJECT_NODE, flags);
	}

	if (!result && process->type == PROCESS_DESKTOP) {
		KernelPanic("ProcessStart - Could not load the desktop executable.\n");
	}

	return result;
}

Process *ProcessSpawn(ProcessType processType) {
	if (scheduler.shutdown) return nullptr;

	Process *process = processType == PROCESS_KERNEL ? kernelProcess : (Process *) scheduler.processPool.Add(sizeof(Process));

	if (!process) {
		return nullptr;
	}

	process->vmm = processType == PROCESS_KERNEL ? kernelMMSpace : (MMSpace *) scheduler.mmSpacePool.Add(sizeof(MMSpace));

	if (!process->vmm) {
		scheduler.processPool.Remove(process);
		return nullptr;
	}

	process->id = __sync_fetch_and_add(&scheduler.nextProcessID, 1);
	process->vmm->referenceCount = 1;
	process->allItem.thisItem = process;
	process->handles = 1;
	process->handleTable.process = process;
	process->permissions = ES_PERMISSION_ALL;
	process->type = processType;

	if (processType == PROCESS_KERNEL) {
		EsCRTstrcpy(process->cExecutableName, "Kernel");
		scheduler.allProcesses.InsertEnd(&process->allItem);
	}

	return process; 
}

void ThreadSetTemporaryAddressSpace(MMSpace *space) {
	KSpinlockAcquire(&scheduler.dispatchSpinlock);
	Thread *thread = GetCurrentThread();
	MMSpace *oldSpace = thread->temporaryAddressSpace ?: kernelMMSpace;
	thread->temporaryAddressSpace = space;
	MMSpace *newSpace = space ?: kernelMMSpace;
	MMSpaceOpenReference(newSpace);
	ProcessorSetAddressSpace(&newSpace->data);
	KSpinlockRelease(&scheduler.dispatchSpinlock);
	MMSpaceCloseReference(oldSpace);
}

void AsyncTaskThread() {
	CPULocalStorage *local = GetLocalStorage();

	while (true) {
		if (!local->asyncTaskList.first) {
			ProcessorFakeTimerInterrupt();
		} else {
			KSpinlockAcquire(&scheduler.asyncTaskSpinlock);
			SimpleList *item = local->asyncTaskList.first;
			KAsyncTask *task = EsContainerOf(KAsyncTask, item, item);
			KAsyncTaskCallback callback = task->callback;
			task->callback = nullptr;
			local->inAsyncTask = true;
			item->Remove();
			KSpinlockRelease(&scheduler.asyncTaskSpinlock);
			callback(task); // This may cause the task to be deallocated.
			ThreadSetTemporaryAddressSpace(nullptr); // The task may have modified the address space.
			local->inAsyncTask = false;
		}
	}
}

void Scheduler::CreateProcessorThreads(CPULocalStorage *local) {
	local->asyncTaskThread = ThreadSpawn("AsyncTasks", (uintptr_t) AsyncTaskThread, 0, SPAWN_THREAD_ASYNC_TASK);
	local->currentThread = local->idleThread = ThreadSpawn("Idle", 0, 0, SPAWN_THREAD_IDLE);
	local->processorID = __sync_fetch_and_add(&nextProcessorID, 1);

	if (local->processorID >= K_MAX_PROCESSORS) { 
		KernelPanic("Scheduler::CreateProcessorThreads - Maximum processor count (%d) exceeded.\n", local->processorID);
	}
}

void ProcessRemove(Process *process) {
	KernelLog(LOG_INFO, "Scheduler", "remove process", "Removing process %d.\n", process->id);

	if (process->executableNode) {
		// Close the handle to the executable node.
		CloseHandleToObject(process->executableNode, KERNEL_OBJECT_NODE, ES_FILE_READ);
		process->executableNode = nullptr;
	}

	// Destroy the process's handle table, if it hasn't already been destroyed.
	// For most processes, the handle table is destroyed when the last thread terminates.
	process->handleTable.Destroy();

	// Free all the remaining messages in the message queue.
	// This is done after closing all handles, since closing handles can generate messages.
	process->messageQueue.messages.Free();

	if (process->blockShutdown) {
		if (1 == __sync_fetch_and_sub(&scheduler.blockShutdownProcessCount, 1)) {
			// If this is the last process to exit, set the allProcessesTerminatedEvent.
			KEventSet(&scheduler.allProcessesTerminatedEvent);
		}
	}

	// Free the process.
	MMSpaceCloseReference(process->vmm);
	scheduler.processPool.Remove(process); 
}

void ThreadRemove(Thread *thread) {
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

	scheduler.threadPool.Remove(thread);
}

void ThreadPause(Thread *thread, bool resume) {
	KSpinlockAcquire(&scheduler.dispatchSpinlock);

	if (thread->paused == !resume) {
		return;
	}

	thread->paused = !resume;

	if (!resume && thread->terminatableState == THREAD_TERMINATABLE) {
		if (thread->state == THREAD_ACTIVE) {
			if (thread->executing) {
				if (thread == GetCurrentThread()) {
					KSpinlockRelease(&scheduler.dispatchSpinlock);

					// Yield.
					ProcessorFakeTimerInterrupt();

					if (thread->paused) {
						KernelPanic("ThreadPause - Current thread incorrectly resumed.\n");
					}
				} else {
					// The thread is executing, but on a different processor.
					// Send them an IPI to stop.
					ProcessorSendYieldIPI(thread);
					// TODO The interrupt context might not be set at this point.
				}
			} else {
				// Remove the thread from the active queue, and put it into the paused queue.
				thread->item.RemoveFromList();
				scheduler.AddActiveThread(thread, false);
			}
		} else {
			// The thread doesn't need to be in the paused queue as it won't run anyway.
			// If it is unblocked, then AddActiveThread will put it into the correct queue.
		}
	} else if (resume && thread->item.list == &scheduler.pausedThreads) {
		// Remove the thread from the paused queue, and put it into the active queue.
		scheduler.pausedThreads.Remove(&thread->item);
		scheduler.AddActiveThread(thread, false);
	}

	KSpinlockRelease(&scheduler.dispatchSpinlock);
}

void ProcessPause(Process *process, bool resume) {
	KMutexAcquire(&process->threadsMutex);
	LinkedItem<Thread> *thread = process->threads.firstItem;

	while (thread) {
		Thread *threadObject = thread->thisItem;
		thread = thread->nextItem;
		ThreadPause(threadObject, resume);
	}

	KMutexRelease(&process->threadsMutex);
}

void ProcessCrash(Process *process, EsCrashReason *crashReason) {
	if (process == kernelProcess) {
		KernelPanic("ProcessCrash - Kernel process has crashed (%d).\n", crashReason->errorCode);
	}

	if (process->type != PROCESS_NORMAL) {
		KernelPanic("ProcessCrash - A critical process has crashed (%d).\n", crashReason->errorCode);
	}

	if (GetCurrentThread()->process != process) {
		KernelPanic("ProcessCrash - Attempt to crash process from different process.\n");
	}

	KMutexAcquire(&process->crashMutex);

	if (process->crashed) {
		KMutexRelease(&process->crashMutex);
		return;
	}

	process->crashed = true;

	KernelLog(LOG_ERROR, "Scheduler", "process crashed", "Process %x has crashed! (%d)\n", process, crashReason->errorCode);

	EsMemoryCopy(&process->crashReason, crashReason, sizeof(EsCrashReason));

	if (!scheduler.shutdown) {
		_EsMessageWithObject m;
		EsMemoryZero(&m, sizeof(m));
		m.message.type = ES_MSG_APPLICATION_CRASH;
		m.message.crash.pid = process->id;
		EsMemoryCopy(&m.message.crash.reason, crashReason, sizeof(EsCrashReason));
		DesktopSendMessage(&m);
	}

	KMutexRelease(&process->crashMutex);

	// TODO Shouldn't this be done before sending the desktop message?
	ProcessPause(GetCurrentThread()->process, false);
}

Thread *Scheduler::PickThread(CPULocalStorage *local) {
	KSpinlockAssertLocked(&dispatchSpinlock);

	if ((local->asyncTaskList.first || local->inAsyncTask) && local->asyncTaskThread->state == THREAD_ACTIVE) {
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

	if (!local->processorID) {
		// Update the scheduler's time.
		timeMs = ArchGetTimeMs();
		mmGlobalData->schedulerTimeMs = timeMs;

		// Notify the necessary timers.
		KSpinlockAcquire(&activeTimersSpinlock);
		LinkedItem<KTimer> *_timer = activeTimers.firstItem;

		while (_timer) {
			KTimer *timer = _timer->thisItem;
			LinkedItem<KTimer> *next = _timer->nextItem;

			if (timer->triggerTimeMs <= timeMs) {
				activeTimers.Remove(_timer);
				KEventSet(&timer->event);

				if (timer->callback) {
					KRegisterAsyncTask(&timer->asyncTask, timer->callback);
				}
			} else {
				break; // Timers are kept sorted, so there's no point continuing.
			}

			_timer = next;
		}

		KSpinlockRelease(&activeTimersSpinlock);
	}

	if (local->spinlockCount) {
		KernelPanic("Scheduler::Yield - Spinlocks acquired while attempting to yield.\n");
	}

	ProcessorDisableInterrupts(); // We don't want interrupts to get reenabled after the context switch.
	KSpinlockAcquire(&dispatchSpinlock);

	if (dispatchSpinlock.interruptsEnabled) {
		KernelPanic("Scheduler::Yield - Interrupts were enabled when scheduler lock was acquired.\n");
	}

	if (!local->currentThread->executing) {
		KernelPanic("Scheduler::Yield - Current thread %x marked as not executing (%x).\n", local->currentThread, local);
	}

	MMSpace *oldAddressSpace = local->currentThread->temporaryAddressSpace ?: local->currentThread->process->vmm;

	local->currentThread->interruptContext = context;
	local->currentThread->executing = false;

	bool killThread = local->currentThread->terminatableState == THREAD_TERMINATABLE 
		&& local->currentThread->terminating;
	bool keepThreadAlive = local->currentThread->terminatableState == THREAD_USER_BLOCK_REQUEST
		&& local->currentThread->terminating; // The user can't make the thread block if it is terminating.

	if (killThread) {
		local->currentThread->state = THREAD_TERMINATED;
		KernelLog(LOG_INFO, "Scheduler", "terminate yielded thread", "Terminated yielded thread %x\n", local->currentThread);
		KRegisterAsyncTask(&local->currentThread->killAsyncTask, ThreadKill);
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
					local->currentThread->blocking.events[i]->blockedThreads.InsertEnd(&local->currentThread->blocking.eventItems[i]);
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
	ArchNextTimer(1 /* ms */);

	InterruptContext *newContext = newThread->interruptContext;
	MMSpace *addressSpace = newThread->temporaryAddressSpace ?: newThread->process->vmm;
	MMSpaceOpenReference(addressSpace);
	ArchSwitchContext(newContext, &addressSpace->data, newThread->kernelStack, newThread, oldAddressSpace);
	KernelPanic("Scheduler::Yield - DoContextSwitch unexpectedly returned.\n");
}

void ProcessTerminateAll() {
	KMutexAcquire(&desktopMutex);

	scheduler.shutdown = true;

	// Close our handle to the desktop process.
	CloseHandleToObject(desktopProcess->executableMainThread, KERNEL_OBJECT_THREAD);
	CloseHandleToObject(desktopProcess, KERNEL_OBJECT_PROCESS);
	desktopProcess = nullptr;

	KMutexRelease(&desktopMutex);

	KernelLog(LOG_INFO, "Scheduler", "terminating all processes", "ProcessTerminateAll - Terminating all processes....\n");

	while (true) {
		KMutexAcquire(&scheduler.allProcessesMutex);
		Process *process = scheduler.allProcesses.firstItem->thisItem;

		while (process && (process->preventNewThreads || process == kernelProcess)) {
			LinkedItem<Process> *item = process->allItem.nextItem;
			process = item ? item->thisItem : nullptr;
		}

		KMutexRelease(&scheduler.allProcessesMutex);
		if (!process) break;

		ProcessTerminate(process, -1);
	}

	KEventWait(&scheduler.allProcessesTerminatedEvent);
}

Process *ProcessOpen(uint64_t id) {
	KMutexAcquire(&scheduler.allProcessesMutex);
	LinkedItem<Process> *item = scheduler.allProcesses.firstItem;
	Process *result = nullptr;

	while (item) {
		Process *process = item->thisItem;

		if (process->id == id && process->type != PROCESS_KERNEL /* the kernel process cannot be opened */) {
			OpenHandleToObject(process, KERNEL_OBJECT_PROCESS, ES_FLAGS_DEFAULT);
			result = item->thisItem;
			break;
		}

		item = item->nextItem;
	}

	KMutexRelease(&scheduler.allProcessesMutex);
	return result;
}

bool KThreadCreate(const char *cName, void (*startAddress)(uintptr_t), uintptr_t argument) {
	return ThreadSpawn(cName, (uintptr_t) startAddress, argument) ? true : false;
}

void KThreadTerminate() {
	ThreadTerminate(GetCurrentThread());
}

void KYield() {
	ProcessorFakeTimerInterrupt();
}

bool DesktopSendMessage(_EsMessageWithObject *message) {
	bool result = false;
	KMutexAcquire(&desktopMutex);
	if (desktopProcess) result = desktopProcess->messageQueue.SendMessage(message);
	KMutexRelease(&desktopMutex);
	return result;
}

EsHandle DesktopOpenHandle(void *object, uint32_t flags, KernelObjectType type) {
	EsHandle result = ES_INVALID_HANDLE;
	bool close = false;
	KMutexAcquire(&desktopMutex);
	if (desktopProcess) result = desktopProcess->handleTable.OpenHandle(object, flags, type);
	else close = true;
	KMutexRelease(&desktopMutex);
	if (close) CloseHandleToObject(object, type, flags);
	return result;
}

void DesktopCloseHandle(EsHandle handle) {
	KMutexAcquire(&desktopMutex);
	if (desktopProcess) desktopProcess->handleTable.CloseHandle(handle); // This will check that the handle is still valid.
	KMutexRelease(&desktopMutex);
}

uint64_t KCPUCurrentID() 	{ return GetLocalStorage() ->processorID; }
uint64_t KProcessCurrentID()	{ return GetCurrentThread()->process->id; }
uint64_t KThreadCurrentID()	{ return GetCurrentThread()         ->id; }

#endif
