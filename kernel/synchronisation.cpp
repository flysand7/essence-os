#ifdef IMPLEMENTATION

#ifdef DEBUG_BUILD
uintptr_t nextMutexID;
#endif

void KSpinlockAcquire(KSpinlock *spinlock) {
	if (scheduler.panic) return;

	bool _interruptsEnabled = ProcessorAreInterruptsEnabled();
	ProcessorDisableInterrupts();

	CPULocalStorage *storage = GetLocalStorage();

#ifdef DEBUG_BUILD
	if (storage && storage->currentThread && spinlock->owner && spinlock->owner == storage->currentThread) {
		KernelPanic("KSpinlock::Acquire - Attempt to acquire a spinlock owned by the current thread (%x/%x, CPU: %d/%d).\nAcquired at %x.\n", 
				storage->currentThread, spinlock->owner, storage->processorID, spinlock->ownerCPU, spinlock->acquireAddress);
	}
#endif

	if (storage) {
		storage->spinlockCount++;
	}

	while (__sync_val_compare_and_swap(&spinlock->state, 0, 1));
	__sync_synchronize();

	spinlock->interruptsEnabled = _interruptsEnabled;

	if (storage) {
#ifdef DEBUG_BUILD
		spinlock->owner = storage->currentThread;
#endif
		spinlock->ownerCPU = storage->processorID;
	} else {
		// Because spinlocks can be accessed very early on in initialisation there may not be
		// a CPULocalStorage available for the current processor. Therefore, just set this field to nullptr.

#ifdef DEBUG_BUILD
		spinlock->owner = nullptr;
#endif
	}

#ifdef DEBUG_BUILD
	spinlock->acquireAddress = (uintptr_t) __builtin_return_address(0);
#endif
}

void KSpinlockRelease(KSpinlock *spinlock, bool force) {
	if (scheduler.panic) return;

	CPULocalStorage *storage = GetLocalStorage();

	if (storage) {
		storage->spinlockCount--;
	}

	if (!force) {
		KSpinlockAssertLocked(spinlock);
	}
	
	volatile bool wereInterruptsEnabled = spinlock->interruptsEnabled;

#ifdef DEBUG_BUILD
	spinlock->owner = nullptr;
#endif
	__sync_synchronize();
	spinlock->state = 0;

	if (wereInterruptsEnabled) ProcessorEnableInterrupts();

#ifdef DEBUG_BUILD
	spinlock->releaseAddress = (uintptr_t) __builtin_return_address(0);
#endif
}

void KSpinlockAssertLocked(KSpinlock *spinlock) {
	if (scheduler.panic) return;

#ifdef DEBUG_BUILD
	CPULocalStorage *storage = GetLocalStorage();

	if (!spinlock->state || ProcessorAreInterruptsEnabled() 
			|| (storage && spinlock->owner != storage->currentThread)) {
#else
	if (!spinlock->state || ProcessorAreInterruptsEnabled()) {
#endif
		KernelPanic("KSpinlock::AssertLocked - KSpinlock not correctly acquired\n"
				"Return address = %x.\n"
				"state = %d, ProcessorAreInterruptsEnabled() = %d, this = %x\n",
				__builtin_return_address(0), spinlock->state, 
				ProcessorAreInterruptsEnabled(), spinlock);
	}
}

#ifdef DEBUG_BUILD
bool _KMutexAcquire(KMutex *mutex, const char *cMutexString, const char *cFile, int line) {
#else
bool KMutexAcquire(KMutex *mutex) {
#endif
	if (scheduler.panic) return false;

	Thread *currentThread = GetCurrentThread();
	bool hasThread = currentThread;

	if (!currentThread) {
		currentThread = (Thread *) 1;
	} else {
		if (currentThread->terminatableState == THREAD_TERMINATABLE) {
			KernelPanic("KMutex::Acquire - Thread is terminatable.\n");
		}
	}

	if (hasThread && mutex->owner && mutex->owner == currentThread) {
#ifdef DEBUG_BUILD
		KernelPanic("KMutex::Acquire - Attempt to acquire mutex (%x) at %x owned by current thread (%x) acquired at %x.\n", 
				mutex, __builtin_return_address(0), currentThread, mutex->acquireAddress);
#else
		KernelPanic("KMutex::Acquire - Attempt to acquire mutex (%x) at %x owned by current thread (%x).\n", 
				mutex, __builtin_return_address(0), currentThread);
#endif
	}

	if (!ProcessorAreInterruptsEnabled()) {
		KernelPanic("KMutex::Acquire - Trying to acquire a mutex while interrupts are disabled.\n");
	}

	while (true) {
		KSpinlockAcquire(&scheduler.dispatchSpinlock);
		Thread *old = mutex->owner;
		if (!old) mutex->owner = currentThread;
		KSpinlockRelease(&scheduler.dispatchSpinlock);
		if (!old) break;

		__sync_synchronize();

		if (GetLocalStorage() && GetLocalStorage()->schedulerReady) {
			// Instead of spinning on the lock, 
			// let's tell the scheduler to not schedule this thread
			// until it's released.
			scheduler.WaitMutex(mutex);

			if (currentThread->terminating && currentThread->terminatableState == THREAD_USER_BLOCK_REQUEST) {
				// We didn't acquire the mutex because the thread is terminating.
				return false;
			}
		}
	}

	__sync_synchronize();

	if (mutex->owner != currentThread) {
		KernelPanic("KMutex::Acquire - Invalid owner thread (%x, expected %x).\n", mutex->owner, currentThread);
	}

#ifdef DEBUG_BUILD
	mutex->acquireAddress = (uintptr_t) __builtin_return_address(0);
	KMutexAssertLocked(mutex);

	if (!mutex->id) {
		mutex->id = __sync_fetch_and_add(&nextMutexID, 1);
	}

	if (currentThread && scheduler.threadEventLog) {
		uintptr_t position = __sync_fetch_and_add(&scheduler.threadEventLogPosition, 1);

		if (position < scheduler.threadEventLogAllocated) {
			EsThreadEventLogEntry *entry = scheduler.threadEventLog + position;
			entry->event = ES_THREAD_EVENT_MUTEX_ACQUIRE;
			entry->objectID = mutex->id;
			entry->threadID = currentThread->id;
			entry->line = line;
			entry->fileBytes = EsCStringLength(cFile);
			if (entry->fileBytes > sizeof(entry->file)) entry->fileBytes = sizeof(entry->file);
			entry->expressionBytes = EsCStringLength(cMutexString);
			if (entry->expressionBytes > sizeof(entry->expression)) entry->expressionBytes = sizeof(entry->expression);
			EsMemoryCopy(entry->file, cFile, entry->fileBytes);
			EsMemoryCopy(entry->expression, cMutexString, entry->expressionBytes);
		}
	}
#endif

	return true;
}

#ifdef DEBUG_BUILD
void _KMutexRelease(KMutex *mutex, const char *cMutexString, const char *cFile, int line) {
#else
void KMutexRelease(KMutex *mutex) {
#endif
	if (scheduler.panic) return;

	KMutexAssertLocked(mutex);
	Thread *currentThread = GetCurrentThread();
	KSpinlockAcquire(&scheduler.dispatchSpinlock);

#ifdef DEBUG_BUILD
	// EsPrint("$%x:%x:0\n", owner, id);
#endif

	if (currentThread) {
		Thread *temp = __sync_val_compare_and_swap(&mutex->owner, currentThread, nullptr);
		if (currentThread != temp) KernelPanic("KMutex::Release - Invalid owner thread (%x, expected %x).\n", temp, currentThread);
	} else mutex->owner = nullptr;

	volatile bool preempt = mutex->blockedThreads.count;

	if (scheduler.started) {
		// NOTE We unblock all waiting threads, because of how blockedThreadPriorities works.
		scheduler.NotifyObject(&mutex->blockedThreads, true, currentThread); 
	}

	KSpinlockRelease(&scheduler.dispatchSpinlock);
	__sync_synchronize();

#ifdef DEBUG_BUILD
	mutex->releaseAddress = (uintptr_t) __builtin_return_address(0);

	if (currentThread && scheduler.threadEventLog) {
		uintptr_t position = __sync_fetch_and_add(&scheduler.threadEventLogPosition, 1);

		if (position < scheduler.threadEventLogAllocated) {
			EsThreadEventLogEntry *entry = scheduler.threadEventLog + position;
			entry->event = ES_THREAD_EVENT_MUTEX_RELEASE;
			entry->objectID = mutex->id;
			entry->threadID = currentThread->id;
			entry->line = line;
			entry->fileBytes = EsCStringLength(cFile);
			if (entry->fileBytes > sizeof(entry->file)) entry->fileBytes = sizeof(entry->file);
			entry->expressionBytes = EsCStringLength(cMutexString);
			if (entry->expressionBytes > sizeof(entry->expression)) entry->expressionBytes = sizeof(entry->expression);
			EsMemoryCopy(entry->file, cFile, entry->fileBytes);
			EsMemoryCopy(entry->expression, cMutexString, entry->expressionBytes);
		}
	}
#endif

	if (preempt) ProcessorFakeTimerInterrupt();
}

void KMutexAssertLocked(KMutex *mutex) {
	Thread *currentThread = GetCurrentThread();

	if (!currentThread) {
		currentThread = (Thread *) 1;
	}

	if (mutex->owner != currentThread) {
#ifdef DEBUG_BUILD
		KernelPanic("KMutex::AssertLocked - Mutex not correctly acquired\n"
				"currentThread = %x, owner = %x\nthis = %x\nReturn %x/%x\nLast used from %x->%x\n", 
				currentThread, mutex->owner, mutex, __builtin_return_address(0), __builtin_return_address(1), 
				mutex->acquireAddress, mutex->releaseAddress);
#else
		KernelPanic("KMutex::AssertLocked - Mutex not correctly acquired\n"
				"currentThread = %x, owner = %x\nthis = %x\nReturn %x\n", 
				currentThread, mutex->owner, mutex, __builtin_return_address(0));
#endif
	}
}

bool KSemaphorePoll(KSemaphore *semaphore) {
	bool success = false;
	KMutexAcquire(&semaphore->mutex);
	if (semaphore->units) { success = true; semaphore->units--; }
	if (!semaphore->units && semaphore->available.state) KEventReset(&semaphore->available);
	KMutexRelease(&semaphore->mutex);
	return success;
}

bool KSemaphoreTake(KSemaphore *semaphore, uintptr_t u, uintptr_t timeoutMs) {
	// All-or-nothing approach to prevent deadlocks.

	uintptr_t taken = 0;

	while (u) {
		if (!KEventWait(&semaphore->available, timeoutMs)) {
			KSemaphoreReturn(semaphore, taken);
			return false;
		}

		KMutexAcquire(&semaphore->mutex);
		if (semaphore->units >= u) { semaphore->units -= u; u = 0; taken += u; }
		if (!semaphore->units && semaphore->available.state) KEventReset(&semaphore->available);
		KMutexRelease(&semaphore->mutex);

		semaphore->lastTaken = (uintptr_t) __builtin_return_address(0);
	}

	return true;
}

void KSemaphoreReturn(KSemaphore *semaphore, uintptr_t u) {
	KMutexAcquire(&semaphore->mutex);
	if (!semaphore->available.state) KEventSet(&semaphore->available);
	semaphore->units += u;
	KMutexRelease(&semaphore->mutex);
}

void KSemaphoreSet(KSemaphore *semaphore, uintptr_t u) {
	KMutexAcquire(&semaphore->mutex);
	if (!semaphore->available.state && u) KEventSet(&semaphore->available);
	else if (semaphore->available.state && !u) KEventReset(&semaphore->available);
	semaphore->units = u;
	KMutexRelease(&semaphore->mutex);
}

bool KEventSet(KEvent *event, bool maybeAlreadySet) {
	if (event->state && !maybeAlreadySet) {
		KernelLog(LOG_ERROR, "Synchronisation", "event already set", "KEventSet - Attempt to set a event that had already been set\n");
	}

	KSpinlockAcquire(&scheduler.dispatchSpinlock);
	volatile bool unblockedThreads = false;

	if (!event->state) {
		event->state = true;

		if (scheduler.started) {
			if (event->blockedThreads.count) {
				unblockedThreads = true;
			}

			// If this is a manually reset event, unblock all the waiting threads.
			scheduler.NotifyObject(&event->blockedThreads, !event->autoReset);
		}
	}

	KSpinlockRelease(&scheduler.dispatchSpinlock);
	return unblockedThreads;
}

void KEventReset(KEvent *event) {
	if (event->blockedThreads.firstItem && event->state) {
		// TODO Is it possible for this to happen?
		KernelLog(LOG_ERROR, "Synchronisation", "reset event with threads blocking", 
				"KEvent::Reset - Attempt to reset a event while threads are blocking on the event\n");
	}

	event->state = false;
}

bool KEventPoll(KEvent *event) {
	if (event->autoReset) {
		return __sync_val_compare_and_swap(&event->state, true, false);
	} else {
		return event->state;
	}
}

bool KEventWait(KEvent *_this, uint64_t timeoutMs) {
	KEvent *events[2];
	events[0] = _this;

	if (timeoutMs == (uint64_t) ES_WAIT_NO_TIMEOUT) {
		int index = scheduler.WaitEvents(events, 1);
		return index == 0;
	} else {
		KTimer timer = {};
		KTimerSet(&timer, timeoutMs);
		events[1] = &timer.event;
		int index = scheduler.WaitEvents(events, 2);
		KTimerRemove(&timer);
		return index == 0;
	}
}

void KWriterLockAssertLocked(KWriterLock *lock) {
	if (lock->state == 0) {
		KernelPanic("KWriterLock::AssertLocked - Unlocked.\n");
	}
}

void KWriterLockAssertShared(KWriterLock *lock) {
	if (lock->state == 0) {
		KernelPanic("KWriterLock::AssertShared - Unlocked.\n");
	} else if (lock->state < 0) {
		KernelPanic("KWriterLock::AssertShared - In exclusive mode.\n");
	}
}

void KWriterLockAssertExclusive(KWriterLock *lock) {
	if (lock->state == 0) {
		KernelPanic("KWriterLock::AssertExclusive - Unlocked.\n");
	} else if (lock->state > 0) {
		KernelPanic("KWriterLock::AssertExclusive - In shared mode, with %d readers.\n", lock->state);
	}
}

void KWriterLockReturn(KWriterLock *lock, bool write) {
	KSpinlockAcquire(&scheduler.dispatchSpinlock);

	if (lock->state == -1) {
		if (!write) {
			KernelPanic("KWriterLock::Return - Attempting to return shared access to an exclusively owned lock.\n");
		}

		lock->state = 0;
	} else if (lock->state == 0) {
		KernelPanic("KWriterLock::Return - Attempting to return access to an unowned lock.\n");
	} else {
		if (write) {
			KernelPanic("KWriterLock::Return - Attempting to return exclusive access to an shared lock.\n");
		}

		lock->state--;
	}

	if (!lock->state) {
		scheduler.NotifyObject(&lock->blockedThreads, true);
	}

	KSpinlockRelease(&scheduler.dispatchSpinlock);
}

bool KWriterLockTake(KWriterLock *lock, bool write, bool poll) {
	// TODO Preventing exclusive access starvation.
	// TODO Do this without taking the scheduler's lock?

	bool done = false;

	Thread *thread = GetCurrentThread();

	if (thread) {
		thread->blocking.writerLock = lock;
		thread->blocking.writerLockType = write;
		__sync_synchronize();
	}

	while (true) {
		KSpinlockAcquire(&scheduler.dispatchSpinlock);

		if (write) {
			if (lock->state == 0) {
				lock->state = -1;
				done = true;
#ifdef DEBUG_BUILD
				lock->exclusiveOwner = thread;
#endif
			}
		} else {
			if (lock->state >= 0) {
				lock->state++;
				done = true;
			}
		}

		KSpinlockRelease(&scheduler.dispatchSpinlock);

		if (poll || done) {
			break;
		} else {
			if (!thread) {
				KernelPanic("KWriterLock::Take - Scheduler not ready yet.\n");
			}

			thread->state = THREAD_WAITING_WRITER_LOCK;
			ProcessorFakeTimerInterrupt();
			thread->state = THREAD_ACTIVE;
		}
	}

	return done;
}

void KWriterLockTakeMultiple(KWriterLock **locks, size_t lockCount, bool write) {
	uintptr_t i = 0, taken = 0;

	while (taken != lockCount) {
		if (KWriterLockTake(locks[i], write, taken)) {
			taken++, i++;
			if (i == lockCount) i = 0;
		} else {
			intptr_t j = i - 1;

			while (taken) {
				if (j == -1) j = lockCount - 1;
				KWriterLockReturn(locks[j], write);
				j--, taken--;
			}
		}
	}
}

void KWriterLockConvertExclusiveToShared(KWriterLock *lock) {
	KSpinlockAcquire(&scheduler.dispatchSpinlock);
	KWriterLockAssertExclusive(lock);
	lock->state = 1;
	scheduler.NotifyObject(&lock->blockedThreads, true);
	KSpinlockRelease(&scheduler.dispatchSpinlock);
}

#if 0

volatile int testState;
KWriterLock testWriterLock;

void TestWriterLocksThread1(uintptr_t) {
	KEvent wait = {};
	testWriterLock.Take(K_LOCK_SHARED);
	EsPrint("-->1\n");
	testState = 1;
	while (testState != 2);
	wait.Wait(1000);
	EsPrint("-->3\n");
	testWriterLock.Return(K_LOCK_SHARED);
	testState = 3;
}

void TestWriterLocksThread2(uintptr_t) {
	while (testState != 1);
	testWriterLock.Take(K_LOCK_SHARED);
	EsPrint("-->2\n");
	testState = 2;
	while (testState != 3);
	testWriterLock.Return(K_LOCK_SHARED);
}

void TestWriterLocksThread3(uintptr_t) {
	while (testState < 1);
	testWriterLock.Take(K_LOCK_EXCLUSIVE);
	EsPrint("!!!!!!!!!!!!!!!!!!! %d\n", testState);
	testWriterLock.Return(K_LOCK_EXCLUSIVE);
	testState = 5;
}

#define TEST_WRITER_LOCK_THREADS (4)

void TestWriterLocksThread4(uintptr_t) {
	__sync_fetch_and_add(&testState, 1);

	while (testState < 6 + TEST_WRITER_LOCK_THREADS) {
		bool type = EsRandomU8() < 0xC0;
		testWriterLock.Take(type);
		testWriterLock.Return(type);
	}
	
	__sync_fetch_and_add(&testState, 1);
}

void TestWriterLocks() {
	testState = 0;
	EsPrint("TestWriterLocks...\n");
	KThreadCreate("Test1", TestWriterLocksThread1);
	KThreadCreate("Test2", TestWriterLocksThread2);
	KThreadCreate("Test3", TestWriterLocksThread3);
	EsPrint("waiting for state 5...\n");
	while (testState != 5);
	while (true) {
		testState = 5;
		for (int i = 0; i < TEST_WRITER_LOCK_THREADS; i++) {
			KThreadCreate("Test", TestWriterLocksThread4, i);
		}
		while (testState != TEST_WRITER_LOCK_THREADS + 5);
		EsPrint("All threads ready.\n");
		KEvent wait = {};
		wait.Wait(10000);
		testState++;
		while (testState != TEST_WRITER_LOCK_THREADS * 2 + 6);
		EsPrint("Test complete!\n");
	}
}

#endif

void KTimerSet(KTimer *timer, uint64_t triggerInMs, KAsyncTaskCallback _callback, EsGeneric _argument) {
	KSpinlockAcquire(&scheduler.activeTimersSpinlock);

	// Reset the timer state.

	if (timer->item.list) {
		scheduler.activeTimers.Remove(&timer->item);
	}

	KEventReset(&timer->event);

	// Set the timer information.

	timer->triggerTimeMs = triggerInMs + scheduler.timeMs;
	timer->callback = _callback;
	timer->argument = _argument;
	timer->item.thisItem = timer;

	// Add the timer to the list of active timers, keeping the list sorted by trigger time.

	LinkedItem<KTimer> *_timer = scheduler.activeTimers.firstItem;

	while (_timer) {
		KTimer *timer2 = _timer->thisItem;
		LinkedItem<KTimer> *next = _timer->nextItem;

		if (timer2->triggerTimeMs > timer->triggerTimeMs) {
			break; // Insert before this timer.
		}

		_timer = next;
	}

	if (_timer) {
		scheduler.activeTimers.InsertBefore(&timer->item, _timer);
	} else {
		scheduler.activeTimers.InsertEnd(&timer->item);
	}

	KSpinlockRelease(&scheduler.activeTimersSpinlock);
}

void KTimerRemove(KTimer *timer) {
	KSpinlockAcquire(&scheduler.activeTimersSpinlock);

	if (timer->callback) {
		KernelPanic("KTimer::Remove - Timers with callbacks cannot be removed.\n");
	}

	if (timer->item.list) {
		scheduler.activeTimers.Remove(&timer->item);
	}

	KSpinlockRelease(&scheduler.activeTimersSpinlock);
}

void Scheduler::WaitMutex(KMutex *mutex) {
	Thread *thread = GetCurrentThread();

	if (thread->state != THREAD_ACTIVE) {
		KernelPanic("Scheduler::WaitMutex - Attempting to wait on a mutex in a non-active thread.\n");
	}

	thread->blocking.mutex = mutex;
	__sync_synchronize();
	thread->state = THREAD_WAITING_MUTEX;

	KSpinlockAcquire(&dispatchSpinlock);
	// Is the owner of this mutex executing?
	// If not, there's no point in spinning on it.
	bool spin = mutex && mutex->owner && mutex->owner->executing;
	KSpinlockRelease(&dispatchSpinlock);

	if (!spin && thread->blocking.mutex->owner) {
		ProcessorFakeTimerInterrupt();
	}

	// Early exit if this is a user request to block the thread and the thread is terminating.
	while ((!thread->terminating || thread->terminatableState != THREAD_USER_BLOCK_REQUEST) && mutex->owner) {
		thread->state = THREAD_WAITING_MUTEX;
	}

	thread->state = THREAD_ACTIVE;
}

uintptr_t Scheduler::WaitEvents(KEvent **events, size_t count) {
	if (count > ES_MAX_WAIT_COUNT) {
		KernelPanic("Scheduler::WaitEvents - count (%d) > ES_MAX_WAIT_COUNT (%d)\n", count, ES_MAX_WAIT_COUNT);
	} else if (!count) {
		KernelPanic("Scheduler::WaitEvents - Count is 0.\n");
	} else if (!ProcessorAreInterruptsEnabled()) {
		KernelPanic("Scheduler::WaitEvents - Interrupts disabled.\n");
	}

	Thread *thread = GetCurrentThread();
	thread->blocking.eventCount = count;

	LinkedItem<Thread> eventItems[count]; // Max size 16 * 32 = 512.
	EsMemoryZero(eventItems, count * sizeof(LinkedItem<Thread>));
	thread->blocking.eventItems = eventItems;
	EsDefer(thread->blocking.eventItems = nullptr);

	for (uintptr_t i = 0; i < count; i++) {
		eventItems[i].thisItem = thread;
		thread->blocking.events[i] = events[i];
	}

	while (!thread->terminating || thread->terminatableState != THREAD_USER_BLOCK_REQUEST) {
		thread->state = THREAD_WAITING_EVENT;

		for (uintptr_t i = 0; i < count; i++) {
			if (events[i]->autoReset) {
				if (events[i]->state) {
					thread->state = THREAD_ACTIVE;

					if (__sync_val_compare_and_swap(&events[i]->state, true, false)) {
						return i;
					}

					thread->state = THREAD_WAITING_EVENT;
				}
			} else {
				if (events[i]->state) {
					thread->state = THREAD_ACTIVE;
					return i;
				}
			}
		}

		ProcessorFakeTimerInterrupt();
	}

	return -1; // Exited from termination.
}

uintptr_t KWaitEvents(KEvent **events, size_t count) {
	return scheduler.WaitEvents(events, count);
}

void Scheduler::UnblockThread(Thread *unblockedThread, Thread *previousMutexOwner) {
	KSpinlockAssertLocked(&dispatchSpinlock);

	if (unblockedThread->state == THREAD_WAITING_MUTEX) {
		if (unblockedThread->item.list) {
			// If we get here from KMutex::Release -> Scheduler::NotifyObject -> Scheduler::UnblockedThread,
			// the mutex owner has already been cleared to nullptr, so use the previousMutexOwner parameter.
			// But if we get here from Scheduler::TerminateThread, the mutex wasn't released;
			// rather, the waiting thread was unblocked as it is in the WAIT system call, but needs to terminate.

			if (!previousMutexOwner) {
				KMutex *mutex = EsContainerOf(KMutex, blockedThreads, unblockedThread->item.list);

				if (&mutex->blockedThreads != unblockedThread->item.list) {
					KernelPanic("Scheduler::UnblockThread - Unblocked thread %x was not in a mutex blockedThreads list.\n", 
							unblockedThread);
				}

				previousMutexOwner = mutex->owner;
			}

			if (!previousMutexOwner->blockedThreadPriorities[unblockedThread->priority]) {
				KernelPanic("Scheduler::UnblockThread - blockedThreadPriorities was zero (%x/%x).\n", 
						unblockedThread, previousMutexOwner);
			}

			previousMutexOwner->blockedThreadPriorities[unblockedThread->priority]--;
			MaybeUpdateActiveList(previousMutexOwner);

			unblockedThread->item.RemoveFromList();
		}
	} else if (unblockedThread->state == THREAD_WAITING_EVENT) {
		for (uintptr_t i = 0; i < unblockedThread->blocking.eventCount; i++) {
			if (unblockedThread->blocking.eventItems[i].list) {
				unblockedThread->blocking.eventItems[i].RemoveFromList();
			}
		}
	} else if (unblockedThread->state == THREAD_WAITING_WRITER_LOCK) {
		if (unblockedThread->item.list) {
			KWriterLock *lock = EsContainerOf(KWriterLock, blockedThreads, unblockedThread->item.list);

			if (&lock->blockedThreads != unblockedThread->item.list) {
				KernelPanic("Scheduler::UnblockThread - Unblocked thread %x was not in a writer lock blockedThreads list.\n", 
						unblockedThread);
			}

			if ((unblockedThread->blocking.writerLockType == K_LOCK_SHARED && lock->state >= 0)
					|| (unblockedThread->blocking.writerLockType == K_LOCK_EXCLUSIVE && lock->state == 0)) {
				unblockedThread->item.RemoveFromList();
			}
		}
	} else {
		KernelPanic("Scheduler::UnblockedThread - Blocked thread in invalid state %d.\n", 
				unblockedThread->state);
	}

	unblockedThread->state = THREAD_ACTIVE;

	if (!unblockedThread->executing) {
		// Put the unblocked thread at the start of the activeThreads list
		// so that it is immediately executed when the scheduler yields.
		AddActiveThread(unblockedThread, true);
	} 

	// TODO If any processors are idleing, send them a yield IPI.
}

void Scheduler::NotifyObject(LinkedList<Thread> *blockedThreads, bool unblockAll, Thread *previousMutexOwner) {
	KSpinlockAssertLocked(&dispatchSpinlock);

	LinkedItem<Thread> *unblockedItem = blockedThreads->firstItem;

	if (!unblockedItem) {
		// There weren't any threads blocking on the object.
		return; 
	}

	do {
		LinkedItem<Thread> *nextUnblockedItem = unblockedItem->nextItem;
		Thread *unblockedThread = unblockedItem->thisItem;
		UnblockThread(unblockedThread, previousMutexOwner);
		unblockedItem = nextUnblockedItem;
	} while (unblockAll && unblockedItem);
}

#endif
