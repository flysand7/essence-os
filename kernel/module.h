// TODO Include more functions and definitions.
// TODO Add K- prefix to more identifiers.

#define KERNEL

#ifndef K_PRIVATE
#define K_PRIVATE private:
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>

#include <mmintrin.h>
#include <xmmintrin.h>
#include <emmintrin.h>

#define alloca __builtin_alloca

#define KERNEL_VERSION (1)
typedef uint64_t (*KGetKernelVersionCallback)();
#ifdef KERNEL_MODULE
extern "C" uint64_t GetKernelVersion() { return KERNEL_VERSION; }
#endif

// ---------------------------------------------------------------------------------------------------------------
// API header.
// ---------------------------------------------------------------------------------------------------------------

#define ES_DIRECT_API
#define ES_FORWARD(x) x
#define ES_EXTERN_FORWARD ES_EXTERN_C
#include <essence.h>

// TODO stb's behaviour with null termination is non-standard.
extern "C" int EsCRTsprintf(char *buffer, const char *format, ...);
extern "C" int EsCRTsnprintf(char *buffer, size_t bufferSize, const char *format, ...);
extern "C" int EsCRTvsnprintf(char *buffer, size_t bufferSize, const char *format, va_list arguments);

// ---------------------------------------------------------------------------------------------------------------
// Global defines.
// ---------------------------------------------------------------------------------------------------------------

#define K_VERSION (0x010000)
#define K_USER_BUFFER // Used to mark pointers that (might) point to non-kernel memory.
#define K_MAX_PROCESSORS (256) // See cpu_local_storage_size in x86_64.s.
#define K_MAX_PATH (4096)
#define K_ACCESS_IMPLEMENTATION_DEFINED (2)

// ---------------------------------------------------------------------------------------------------------------
// Heap allocations.
// ---------------------------------------------------------------------------------------------------------------

struct EsHeap;

extern EsHeap heapCore;
extern EsHeap heapFixed;

#define K_CORE  (&heapCore)
#define K_FIXED (&heapFixed)
#define K_PAGED (&heapFixed)

void *EsHeapAllocate(size_t size, bool zeroMemory, EsHeap *kernelHeap);
void EsHeapFree(void *address, size_t expectedSize, EsHeap *kernelHeap);

// ---------------------------------------------------------------------------------------------------------------
// Debug output.
// ---------------------------------------------------------------------------------------------------------------

enum KLogLevel {
	LOG_VERBOSE,
	LOG_INFO,
	LOG_ERROR,
};

void KernelLog(KLogLevel level, const char *subsystem, const char *event, const char *format, ...);
void KernelPanic(const char *format, ...);
void EsPrint(const char *format, ...);

#define EsPanic KernelPanic

// ---------------------------------------------------------------------------------------------------------------
// IRQs.
// ---------------------------------------------------------------------------------------------------------------

typedef bool (*KIRQHandler)(uintptr_t interruptIndex /* tag for MSI */, void *context);

// Interrupts are active high and level triggered, unless overridden by the ACPI MADT table.
bool KRegisterIRQ(intptr_t interruptIndex, KIRQHandler handler, void *context, const char *cOwnerName, 
		struct KPCIDevice *pciDevice = nullptr /* do not use; see KPCIDevice::EnableSingleInterrupt */);

struct KMSIInformation {
	// Both fields are zeroed if the MSI could not be registered.
	uintptr_t address;
	uintptr_t data;
	uintptr_t tag;
};

KMSIInformation KRegisterMSI(KIRQHandler handler, void *context, const char *cOwnerName);
void KUnregisterMSI(uintptr_t tag);

// ---------------------------------------------------------------------------------------------------------------
// Async tasks.
// ---------------------------------------------------------------------------------------------------------------

// Async tasks are executed on the same processor that registered it.
// They can be registered with interrupts disabled (e.g. in IRQ handlers).
// They are executed in the order they were registered.
// They can acquire mutexes, but cannot perform IO.

typedef void (*KAsyncTaskCallback)(EsGeneric argument);
void KRegisterAsyncTask(KAsyncTaskCallback callback, EsGeneric argument, bool needed = true);

// ---------------------------------------------------------------------------------------------------------------
// Common data types, algorithms and things.
// ---------------------------------------------------------------------------------------------------------------

#ifndef K_IN_CORE_KERNEL
#define SHARED_DEFINITIONS_ONLY
#endif

#define SHARED_MATH_WANT_BASIC_UTILITIES
#include <shared/unicode.cpp>
#include <shared/math.cpp>
#include <shared/linked_list.cpp>
#include <shared/hash.cpp>

#ifdef K_IN_CORE_KERNEL
#define SHARED_COMMON_WANT_ALL
#include <shared/strings.cpp>
#include <shared/common.cpp>
#endif

// ---------------------------------------------------------------------------------------------------------------
// Processor instruction wrappers.
// ---------------------------------------------------------------------------------------------------------------

extern "C" struct CPULocalStorage *GetLocalStorage();
extern "C" struct Thread *GetCurrentThread();

extern "C" void ProcessorDisableInterrupts();
extern "C" void ProcessorEnableInterrupts();
extern "C" bool ProcessorAreInterruptsEnabled();
extern "C" void ProcessorHalt();
extern "C" void ProcessorIdle();
extern "C" void ProcessorOut8(uint16_t port, uint8_t value);
extern "C" uint8_t ProcessorIn8(uint16_t port);
extern "C" void ProcessorOut16(uint16_t port, uint16_t value);
extern "C" uint16_t ProcessorIn16(uint16_t port);
extern "C" void ProcessorOut32(uint16_t port, uint32_t value);
extern "C" uint32_t ProcessorIn32(uint16_t port);
extern "C" void ProcessorInvalidatePage(uintptr_t virtualAddress);
extern "C" void ProcessorInvalidateAllPages();
extern "C" void ProcessorAPStartup();
extern "C" void ProcessorMagicBreakpoint(...);
extern "C" void ProcessorBreakpointHelper(...);
extern "C" void ProcessorSetLocalStorage(struct CPULocalStorage *cls);
extern "C" void ProcessorSetThreadStorage(uintptr_t tls);
extern "C" size_t ProcessorSendIPI(uintptr_t interrupt, bool nmi = false, int processorID = -1); // Returns the number of processors the IPI was *not* sent to.
extern "C" void ProcessorDebugOutputByte(uint8_t byte);
extern "C" void ProcessorFakeTimerInterrupt();
extern "C" uint64_t ProcessorReadTimeStamp();
extern "C" void DoContextSwitch(struct InterruptContext *context, 
		uintptr_t virtualAddressSpace, uintptr_t threadKernelStack, struct Thread *newThread);
extern "C" void ProcessorSetAddressSpace(uintptr_t virtualAddressSpaceIdentifier);
extern "C" uintptr_t ProcessorGetAddressSpace();
extern "C" void ProcessorFlushCodeCache();
extern "C" void ProcessorFlushCache();

#ifdef ARCH_X86_64
extern "C" uintptr_t ProcessorGetRSP();
extern "C" uintptr_t ProcessorGetRBP();
extern "C" uint64_t ProcessorReadMXCSR();
#endif

// ---------------------------------------------------------------------------------------------------------------
// Kernel core.
// ---------------------------------------------------------------------------------------------------------------

extern "C" uint64_t KGetTimeInMs(); // Scheduler time.

void *KGetRSDP();
size_t KGetCPUCount();
CPULocalStorage *KGetCPULocal(uintptr_t index);
uint64_t KCPUCurrentID();
uint64_t KGetTimeStampTicksPerMs();
uint64_t KGetTimeStampTicksPerUs();

bool KBootedFromEFI();
bool KInIRQ();
void KSwitchThreadAfterIRQ();

void KDebugKeyPressed();
int KWaitKey();

#ifdef ARCH_X86_COMMON
void KPS2SafeToInitialise();
#endif

EsUniqueIdentifier KGetBootIdentifier();

struct KTimeout { 
	uint64_t end; 
	inline KTimeout(int ms) { end = KGetTimeInMs() + ms; } 
	inline bool Hit() { return KGetTimeInMs() >= end; }
};

enum KernelObjectType : uint32_t {
	COULD_NOT_RESOLVE_HANDLE	= 0x00000000,
	KERNEL_OBJECT_NONE		= 0x80000000,

	KERNEL_OBJECT_PROCESS 		= 0x00000001, // A process.
	KERNEL_OBJECT_THREAD		= 0x00000002, // A thread.
	KERNEL_OBJECT_WINDOW		= 0x00000004, // A window.
	KERNEL_OBJECT_SHMEM		= 0x00000008, // A region of shared memory.
	KERNEL_OBJECT_NODE		= 0x00000010, // A file system node (file or directory).
	KERNEL_OBJECT_EVENT		= 0x00000020, // A synchronisation event.
	KERNEL_OBJECT_CONSTANT_BUFFER	= 0x00000040, // A buffer of unmodifiable data stored in the kernel's address space.
#ifdef ENABLE_POSIX_SUBSYSTEM
	KERNEL_OBJECT_POSIX_FD		= 0x00000100, // A POSIX file descriptor, used in the POSIX subsystem.
#endif
	KERNEL_OBJECT_PIPE		= 0x00000200, // A pipe through which data can be sent between processes, blocking when full or empty.
	KERNEL_OBJECT_EMBEDDED_WINDOW	= 0x00000400, // An embedded window object, referencing its container Window.
	KERNEL_OBJECT_EVENT_SINK	= 0x00002000, // An event sink. Events can be forwarded to it, allowing waiting on many objects.
	KERNEL_OBJECT_CONNECTION	= 0x00004000, // A network connection.
	KERNEL_OBJECT_DEVICE		= 0x00008000, // A device.
};

// TODO Rename to KObjectReference and KObjectDereference?
void CloseHandleToObject(void *object, KernelObjectType type, uint32_t flags = 0);
bool OpenHandleToObject(void *object, KernelObjectType type, uint32_t flags = 0, bool maybeHasNoHandles = false);

// ---------------------------------------------------------------------------------------------------------------
// Module loading.
// ---------------------------------------------------------------------------------------------------------------

#define KModuleResolveSymbolCallback (const char *name, size_t nameBytes)
typedef void *(*KModuleResolveSymbolCallbackFunction) KModuleResolveSymbolCallback;

struct KModule {
	const char *path;
	size_t pathBytes;
	KModuleResolveSymbolCallbackFunction resolveSymbol;

	uint8_t *buffer;
};

struct KLoadedExecutable {
	uintptr_t startAddress;

	uintptr_t tlsImageStart;
	uintptr_t tlsImageBytes;
	uintptr_t tlsBytes; // All bytes after the image are to be zeroed.

	bool isDesktop, isBundle;
};

EsError KLoadELF(struct KNode *node, KLoadedExecutable *executable); 
EsError KLoadELFModule(KModule *module);
uintptr_t KFindSymbol(KModule *module, const char *name, size_t nameBytes);

// ---------------------------------------------------------------------------------------------------------------
// Synchronisation primitives.
// ---------------------------------------------------------------------------------------------------------------

struct KSpinlock { // Mutual exclusion. CPU-owned. Disables interrupts. The only synchronisation primitive that can be acquired with interrupts disabled.
	K_PRIVATE
	volatile uint8_t state, ownerCPU;
	volatile bool interruptsEnabled;
#ifdef DEBUG_BUILD
	struct Thread *volatile owner;
	volatile uintptr_t acquireAddress, releaseAddress;
#endif
};

void KSpinlockAcquire(KSpinlock *spinlock);
void KSpinlockRelease(KSpinlock *spinlock, bool force = false);
void KSpinlockAssertLocked(KSpinlock *spinlock);

struct KMutex { // Mutual exclusion. Thread-owned.
	K_PRIVATE
	struct Thread *volatile owner;
#ifdef DEBUG_BUILD
	uintptr_t acquireAddress, releaseAddress, id; 
#endif
	LinkedList<struct Thread> blockedThreads;
};

#ifdef DEBUG_BUILD
bool _KMutexAcquire(KMutex *mutex, const char *cMutexString, const char *cFile, int line);
void _KMutexRelease(KMutex *mutex, const char *cMutexString, const char *cFile, int line);
#define KMutexAcquire(mutex) _KMutexAcquire(mutex, #mutex, __FILE__, __LINE__)
#define KMutexRelease(mutex) _KMutexRelease(mutex, #mutex, __FILE__, __LINE__)
#else
bool KMutexAcquire(KMutex *mutex);
void KMutexRelease(KMutex *mutex);
#endif
void KMutexAssertLocked(KMutex *mutex);

struct KEvent { // Waiting and notifying. Can wait on multiple at once. Can be set and reset with interrupts disabled.
	volatile bool autoReset; // This should be first field in the structure,
			         // so that the type of KEvent can be easily declared with {autoReset}.
	volatile uintptr_t state;

	K_PRIVATE

	LinkedList<Thread> blockedThreads;
	volatile size_t handles;
	struct EventSinkTable *sinkTable;
};

bool KEventSet(KEvent *event, bool schedulerAlreadyLocked = false, bool maybeAlreadySet = false);
void KEventReset(KEvent *event); 
bool KEventPoll(KEvent *event); // TODO Remove this! Currently it is only used by KAudioFillBuffersFromMixer.
bool KEventWait(KEvent *event, uint64_t timeoutMs = ES_WAIT_NO_TIMEOUT); // See KWaitEvents to wait for multiple events. Returns false if the wait timed out.

struct KWriterLock { // One writer or many readers.
	K_PRIVATE
	LinkedList<Thread> blockedThreads;
	volatile int64_t state; // -1: exclusive; >0: shared owners.
#ifdef DEBUG_BUILD
	volatile Thread *exclusiveOwner;
#endif
};

#define K_LOCK_EXCLUSIVE (true)
#define K_LOCK_SHARED (false)
bool KWriterLockTake(KWriterLock *lock, bool write, bool poll = false);
void KWriterLockReturn(KWriterLock *lock, bool write);
void KWriterLockConvertExclusiveToShared(KWriterLock *lock);
void KWriterLockAssertExclusive(KWriterLock *lock);
void KWriterLockAssertShared(KWriterLock *lock);
void KWriterLockAssertLocked(KWriterLock *lock);

struct KSemaphore { // Exclusion with a multiple units.
	KEvent available;
	volatile uintptr_t units;

	K_PRIVATE

	KMutex mutex; // TODO Make this a spinlock?
	uintptr_t _custom;
	uintptr_t lastTaken;
};

bool KSemaphoreTake(KSemaphore *semaphore, uintptr_t units = 1, uintptr_t timeoutMs = ES_WAIT_NO_TIMEOUT);
void KSemaphoreReturn(KSemaphore *semaphore, uintptr_t units = 1);
bool KSemaphorePoll(KSemaphore *semaphore);
void KSemaphoreSet(KSemaphore *semaphore, uintptr_t units = 1);

struct KTimer {
	KEvent event;
	K_PRIVATE
	LinkedItem<KTimer> item;
	uint64_t triggerTimeMs;
	KAsyncTaskCallback callback;
	EsGeneric argument;
};

void KTimerSet(KTimer *timer, uint64_t triggerInMs, KAsyncTaskCallback callback = nullptr, EsGeneric argument = 0);
void KTimerRemove(KTimer *timer); // Timers with callbacks cannot be removed (it'd race with async task delivery).

// ---------------------------------------------------------------------------------------------------------------
// Window manager.
// ---------------------------------------------------------------------------------------------------------------

struct KMouseUpdateData {
	int32_t xMovement, yMovement;
	bool xIsAbsolute, yIsAbsolute;
	int32_t xFrom, xTo, yFrom, yTo;
	int32_t xScroll, yScroll;
	uint32_t buttons;
};

#define K_CURSOR_MOVEMENT_SCALE (0x100)
void KMouseUpdate(const KMouseUpdateData *data);
void KKeyboardUpdate(uint16_t *keysDown, size_t keysDownCount);
void KKeyPress(uint32_t scancode);

uint64_t KGameControllerConnect();
void KGameControllerDisconnect(uint64_t id);
void KGameControllerUpdate(EsGameControllerState *state);

#define K_SCANCODE_KEY_RELEASED (1 << 15)
#define K_SCANCODE_KEY_PRESSED  (0 << 15)

#define K_LEFT_BUTTON   (1)
#define K_MIDDLE_BUTTON (2)
#define K_RIGHT_BUTTON  (4)

// ---------------------------------------------------------------------------------------------------------------
// Memory manager.
// ---------------------------------------------------------------------------------------------------------------

#ifdef ARCH_X86_64
#define K_PAGE_BITS (12)
#define K_PAGE_SIZE ((uintptr_t) 1 << K_PAGE_BITS)
#define K_USER_ADDRESS_SPACE_START 	(0x0000000000000000ULL)
#define K_USER_ADDRESS_SPACE_END 	(0x0000800000000000ULL)
#define K_KERNEL_ADDRESS_SPACE_START 	(0xFFFF800000000000ULL)
#define K_KERNEL_ADDRESS_SPACE_END 	(0xFFFFFFFFFFFFFFFFULL)
#define K_STACK_GROWS_DOWN
#endif

struct MMSpace;
MMSpace *MMGetKernelSpace();
MMSpace *MMGetCurrentProcessSpace();

#define MM_REGION_FIXED              (0x01) // A region where all the physical pages are allocated up-front, and cannot be removed from the working set.
#define MM_REGION_NOT_CACHEABLE      (0x02) // Do not cache the pages in the region.
#define MM_REGION_NO_COMMIT_TRACKING (0x04) // Page committing is manually tracked.
#define MM_REGION_READ_ONLY	     (0x08) // Generate page faults when written to.
#define MM_REGION_COPY_ON_WRITE	     (0x10) // Copy on write.
#define MM_REGION_WRITE_COMBINING    (0x20) // Write combining caching is enabled. Incompatible with MM_REGION_NOT_CACHEABLE.
#define MM_REGION_EXECUTABLE         (0x40) 
#define MM_REGION_USER               (0x80) // The application created it, and is therefore allowed to modify it.
// Limited by region type flags.

void *MMMapPhysical(MMSpace *space, uintptr_t address, size_t bytes, uint64_t caching);
void *MMStandardAllocate(MMSpace *space, size_t bytes, uint32_t flags, void *baseAddress = nullptr, bool commitAll = true);
bool MMFree(MMSpace *space, void *address, size_t expectedSize = 0, bool userOnly = false);
void MMAllowWriteCombiningCaching(MMSpace *space, void *virtualAddress);
size_t MMGetRegionPageCount(MMSpace *space, void *virtualAddress);

uint64_t MMNumberOfUsablePhysicalPages();

// Returns 0 if not mapped. Rounds address down to nearest page.
uintptr_t MMArchTranslateAddress(MMSpace *space, uintptr_t virtualAddress, bool writeAccess = false /* if true, return 0 if address not writable */); 

// Must be done with interrupts disabled; does not invalidate the page on other processors.
void MMArchRemap(MMSpace *space, const void *virtualAddress, uintptr_t newPhysicalAddress);

#define MM_PHYSICAL_ALLOCATE_CAN_FAIL		(1 << 0)	// Don't panic if the allocation fails.
#define MM_PHYSICAL_ALLOCATE_COMMIT_NOW 	(1 << 1)	// Commit (fixed) the allocated pages.
#define MM_PHYSICAL_ALLOCATE_ZEROED		(1 << 2)	// Zero the pages.
#define MM_PHYSICAL_ALLOCATE_LOCK_ACQUIRED	(1 << 3)	// The page frame mutex is already acquired.

uintptr_t /* Returns physical address of first page, or 0 if none were available. */ MMPhysicalAllocate(unsigned flags, 
		uintptr_t count = 1 /* Number of contiguous pages to allocate. */, 
		uintptr_t align = 1 /* Alignment, in pages. */, 
		uintptr_t below = 0 /* Upper limit of physical address, in pages. E.g. for 32-bit pages only, pass (0x100000000 >> K_PAGE_BITS). */);
void MMPhysicalFree(uintptr_t page /* Physical address. */, 
		bool mutexAlreadyAcquired = false /* Internal use. Pass false. */, 
		size_t count = 1 /* Number of consecutive pages to free. */);

bool MMPhysicalAllocateAndMap(size_t sizeBytes, size_t alignmentBytes, size_t maximumBits, bool zeroed, 
		uint64_t caching, uint8_t **virtualAddress, uintptr_t *physicalAddress);
void MMPhysicalFreeAndUnmap(void *virtualAddress, uintptr_t physicalAddress);

#define MM_SHARED_ENTRY_PRESENT (1)
struct MMSharedRegion;
MMSharedRegion *MMSharedCreateRegion(size_t sizeBytes, bool fixed = false, uintptr_t below = 0 /* See fixed = true, passed to MMPhysicalAllocate. */);
uintptr_t MMSharedLookupPage(MMSharedRegion *region, uintptr_t pageIndex);
void *MMMapShared(MMSpace *space, MMSharedRegion *sharedRegion, uintptr_t offset, size_t bytes, uint32_t additionalFlags = ES_FLAGS_DEFAULT, void *baseAddresses = nullptr);

// Check that the range of physical memory is unusable.
// Panics on failure.
void MMCheckUnusable(uintptr_t physicalStart, size_t bytes);

#define ARRAY_DEFINITIONS_ONLY
#include <shared/array.cpp>
#undef ARRAY_DEFINITIONS_ONLY

typedef SimpleList MMObjectCacheItem;

struct MMObjectCache {
	K_PRIVATE
	KSpinlock lock; // Used instead of a mutex to keep accesses to the list lightweight.
	SimpleList items;
	size_t count;
	bool (*trim)(MMObjectCache *cache); // Return true if an object was trimmed.
	KWriterLock trimLock; // Open in shared access to trim the cache.
	LinkedItem<MMObjectCache> item;
	size_t averageObjectBytes;
};

void MMObjectCacheInsert(MMObjectCache *cache, MMObjectCacheItem *item);
void MMObjectCacheRemove(MMObjectCache *cache, MMObjectCacheItem *item, bool alreadyLocked = false);
MMObjectCacheItem *MMObjectCacheRemoveLRU(MMObjectCache *cache);
void MMObjectCacheRegister(MMObjectCache *cache, bool (*trimCallback)(MMObjectCache *), size_t averageObjectBytes);
void MMObjectCacheUnregister(MMObjectCache *cache);
void MMObjectCacheFlush(MMObjectCache *cache);

// ---------------------------------------------------------------------------------------------------------------
// Scheduler.
// ---------------------------------------------------------------------------------------------------------------

uint64_t KProcessCurrentID();
uint64_t KThreadCurrentID();

bool KThreadCreate(const char *cName, void (*startAddress)(uintptr_t), uintptr_t argument = 0);
extern "C" void KThreadTerminate(); // Terminates the current thread. Kernel threads can only be terminated by themselves.
void KYield();

uintptr_t KWaitEvents(KEvent **events, size_t count);

struct KWorkGroup {
	inline void Initialise() {
		remaining = 1;
		success = 1;
		KEventReset(&event);
	}

	inline bool Wait() {
		if (__sync_fetch_and_sub(&remaining, 1) != 1) {
			KEventWait(&event);
		}

		if (remaining) {
			KernelPanic("KWorkGroup::Wait - Expected remaining operations to be 0 after event set.\n");
		}

		return success ? true : false;
	}

	inline void Start() {
		if (__sync_fetch_and_add(&remaining, 1) == 0) {
			KernelPanic("KWorkGroup::Start - Could not start operation on completed dispatch group.\n");
		}
	}

	inline void End(bool _success) {
		if (!_success) {
			success = false;
			__sync_synchronize();
		}

		if (__sync_fetch_and_sub(&remaining, 1) == 1) {
			KEventSet(&event);
		}
	}

	K_PRIVATE

	volatile uintptr_t remaining;
	volatile uintptr_t success;
	KEvent event;
};

// ---------------------------------------------------------------------------------------------------------------
// Device management.
// ---------------------------------------------------------------------------------------------------------------

struct KInstalledDriver {
	char *name;                   // The name of the driver.
	size_t nameBytes;
	char *parent;                 // The name of the parent driver.
	size_t parentBytes;
	char *config;                 // The driver's configuration, taken from kernel/config.ini.
	size_t configBytes;
	bool builtin;                 // True if the driver is builtin to the kernel executable.
	struct KDriver *loadedDriver; // The corresponding driver, if it has been loaded.
};

struct KDevice {
	const char *cDebugName;

	KDevice *parent;                    // The parent device.
	Array<KDevice *, K_FIXED> children; // Child devices.

#define K_DEVICE_REMOVED         (1 << 0)
#define K_DEVICE_VISIBLE_TO_USER (1 << 1)   // A ES_MSG_DEVICE_CONNECTED message was sent to Desktop for this device.
	uint8_t flags;
	uint32_t handles;
	EsDeviceType type;
	EsObjectID objectID;

	// These callbacks are called with the deviceTreeMutex locked, and are all optional.
	void (*shutdown)(KDevice *device);  // Called when the computer is about to shutdown.
	void (*dumpState)(KDevice *device); // Dump the entire state of the device for debugging.
	void (*removed)(KDevice *device);   // Called when the device is removed. Called after the children are informed.
	void (*destroy)(KDevice *device);   // Called just before the device is destroyed.
};

struct KDriver {
	// Called when a new device the driver implements is attached.
	// You should pass this the parent device to `KDeviceCreate`.
	// The parent device pointer cannot be used after the function returns.
	void (*attach)(KDevice *parent); 
};

typedef bool KDriverIsImplementorCallback(KInstalledDriver *driver, KDevice *device); // Return true if the child driver implements the device.

// Searches for a driver that implements the device. Loads the driver if necessary (possibly asynchronously), and calls `attach`.
// Returns true if a matching driver was found; a maximum of one driver will be called.
// Example usage: 
// 	- A bus driver finds a function with a connected device.
// 	- It creates a device for that function with KDeviceCreate, and sets the parent to the bus controller device.
// 	- It calls KDeviceAttach on the function device.
// 	- A suitable driver is found, which creates a device with that as its parent.
bool KDeviceAttach(KDevice *parentDevice, const char *cParentDriver /* match the parent field in the config */, KDriverIsImplementorCallback callback);
// Similar to KDeviceAttach, except it calls `attach` for every driver that matches the parent field.
void KDeviceAttachAll(KDevice *parentDevice, const char *cParentDriver);
// Similar to KDeviceAttach, except it calls `attach` only for the driver matching the provided name. Returns true if the driver was found.
bool KDeviceAttachByName(KDevice *parentDevice, const char *cName);

KDevice *KDeviceCreate(const char *cDebugName, KDevice *parent, size_t bytes /* must be at least the size of a KDevice */);
void KDeviceOpenHandle(KDevice *device);
void KDeviceDestroy(KDevice *device); // Call if initialisation of the device failed. Otherwise use KDeviceCloseHandle.
void KDeviceCloseHandle(KDevice *device); // The device creator is responsible for one handle after the creating it. The device is destroyed once all handles are closed.
void KDeviceRemoved(KDevice *device); // Call when a child device is removed. Must be called only once!
void KDeviceSendConnectedMessage(KDevice *device, EsDeviceType type); // Send a message to Desktop to inform it the device was connected.

#include <bin/kernel_config.h>

// ---------------------------------------------------------------------------------------------------------------
// Direct memory access.
// ---------------------------------------------------------------------------------------------------------------

struct KDMASegment {
	uintptr_t physicalAddress;
	size_t byteCount;
	bool isLast;
};

struct KDMABuffer;
uintptr_t KDMABufferGetVirtualAddress(KDMABuffer *buffer); // TODO Temporary.
size_t KDMABufferGetTotalByteCount(KDMABuffer *buffer);
KDMASegment KDMABufferNextSegment(KDMABuffer *buffer, bool peek = false); 
bool KDMABufferIsComplete(KDMABuffer *buffer); // Returns true if the end of the transfer buffer has been reached.

// ---------------------------------------------------------------------------------------------------------------
// Block devices.
// ---------------------------------------------------------------------------------------------------------------

#define K_ACCESS_READ (0)
#define K_ACCESS_WRITE (1)

struct KBlockDeviceAccessRequest {
	struct KBlockDevice *device;
	EsFileOffset offset;
	size_t count;
	int operation;
	KDMABuffer *buffer;
	uint64_t flags;
	KWorkGroup *dispatchGroup;
};

typedef void (*KDeviceAccessCallbackFunction)(KBlockDeviceAccessRequest request);

struct KBlockDevice : KDevice {
	KDeviceAccessCallbackFunction access; // Don't call directly; see KFileSystem::Access.
	EsBlockDeviceInformation information;
	size_t maxAccessSectorCount;

	K_PRIVATE

	uint8_t *signatureBlock; // Signature block. Only valid during fileSystem detection.
	KMutex detectFileSystemMutex;
};

void FSPartitionDeviceCreate(KBlockDevice *parent, EsFileOffset offset, EsFileOffset sectorCount, uint32_t flags, const char *name, size_t nameBytes);

// ---------------------------------------------------------------------------------------------------------------
// PCI.
// ---------------------------------------------------------------------------------------------------------------

struct KPCIDevice : KDevice {
	void WriteBAR8(uintptr_t index, uintptr_t offset, uint8_t value);
	uint8_t ReadBAR8(uintptr_t index, uintptr_t offset);
	void WriteBAR16(uintptr_t index, uintptr_t offset, uint16_t value);
	uint16_t ReadBAR16(uintptr_t index, uintptr_t offset);
	void WriteBAR32(uintptr_t index, uintptr_t offset, uint32_t value);
	uint32_t ReadBAR32(uintptr_t index, uintptr_t offset);
	void WriteBAR64(uintptr_t index, uintptr_t offset, uint64_t value);
	uint64_t ReadBAR64(uintptr_t index, uintptr_t offset);

	void WriteConfig8(uintptr_t offset, uint8_t value);
	uint8_t ReadConfig8(uintptr_t offset);
	void WriteConfig16(uintptr_t offset, uint16_t value);
	uint16_t ReadConfig16(uintptr_t offset);
	void WriteConfig32(uintptr_t offset, uint32_t value);
	uint32_t ReadConfig32(uintptr_t offset);

#define K_PCI_FEATURE_BAR_0                     (1 <<  0)
#define K_PCI_FEATURE_BAR_1                     (1 <<  1)
#define K_PCI_FEATURE_BAR_2                     (1 <<  2)
#define K_PCI_FEATURE_BAR_3                     (1 <<  3)
#define K_PCI_FEATURE_BAR_4                     (1 <<  4)
#define K_PCI_FEATURE_BAR_5                     (1 <<  5)
#define K_PCI_FEATURE_INTERRUPTS 		(1 <<  8)
#define K_PCI_FEATURE_BUSMASTERING_DMA 		(1 <<  9)
#define K_PCI_FEATURE_MEMORY_SPACE_ACCESS 	(1 << 10)
#define K_PCI_FEATURE_IO_PORT_ACCESS		(1 << 11)
	bool EnableFeatures(uint64_t features);
	bool EnableSingleInterrupt(KIRQHandler irqHandler, void *context, const char *cOwnerName); 

	uint32_t deviceID, subsystemID, domain;
	uint8_t  classCode, subclassCode, progIF;
	uint8_t  bus, slot, function;
	uint8_t  interruptPin, interruptLine;

	uint8_t  *baseAddressesVirtual[6];
	uintptr_t baseAddressesPhysical[6];
	size_t    baseAddressesSizes[6];

	uint32_t baseAddresses[6];

	K_PRIVATE
	bool EnableMSI(KIRQHandler irqHandler, void *context, const char *cOwnerName); 
};

uint32_t KPCIReadConfig(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, int size = 32);
void KPCIWriteConfig(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value, int size = 32);

// ---------------------------------------------------------------------------------------------------------------
// USB.
// ---------------------------------------------------------------------------------------------------------------

struct KUSBDescriptorHeader {
	uint8_t length;
	uint8_t descriptorType;
} ES_STRUCT_PACKED;

struct KUSBConfigurationDescriptor : KUSBDescriptorHeader {
	uint16_t totalLength;
	uint8_t interfaceCount;
	uint8_t configurationIndex;
	uint8_t configurationString;
	uint8_t attributes;
	uint8_t maximumPower;
} ES_STRUCT_PACKED;

struct KUSBInterfaceDescriptor : KUSBDescriptorHeader {
	uint8_t interfaceIndex;
	uint8_t alternateSetting;
	uint8_t endpointCount;
	uint8_t interfaceClass;
	uint8_t interfaceSubclass;
	uint8_t interfaceProtocol;
	uint8_t interfaceString;
} ES_STRUCT_PACKED;

struct KUSBDeviceDescriptor : KUSBDescriptorHeader {
	uint16_t specificationVersion;
	uint8_t deviceClass;
	uint8_t deviceSubclass;
	uint8_t deviceProtocol;
	uint8_t maximumPacketSize;
	uint16_t vendorID;
	uint16_t productID;
	uint16_t deviceVersion;
	uint8_t manufacturerString;
	uint8_t productString;
	uint8_t serialNumberString;
	uint8_t configurationCount;
} ES_STRUCT_PACKED;

struct KUSBEndpointCompanionDescriptor : KUSBDescriptorHeader {
	uint8_t maxBurst;
	uint8_t attributes;
	uint16_t bytesPerInterval;

	inline uint8_t GetMaximumStreams() { return attributes & 0x1F; }
	inline bool HasISOCompanion() { return attributes & (1 << 7); }
} ES_STRUCT_PACKED;

struct KUSBEndpointIsochronousCompanionDescriptor : KUSBDescriptorHeader {
	uint16_t reserved;
	uint32_t bytesPerInterval;
} ES_STRUCT_PACKED;

struct KUSBEndpointDescriptor : KUSBDescriptorHeader {
	uint8_t address;
	uint8_t attributes;
	uint16_t maximumPacketSize;
	uint8_t pollInterval;

	inline bool IsControl()     { return (attributes & 3) == 0; }
	inline bool IsIsochronous() { return (attributes & 3) == 1; }
	inline bool IsBulk()        { return (attributes & 3) == 2; }
	inline bool IsInterrupt()   { return (attributes & 3) == 3; }
	inline bool IsInput()       { return  (address & 0x80); }
	inline bool IsOutput()      { return !(address & 0x80); }
	inline uint8_t GetAddress() { return address & 0x0F; }
	inline uint16_t GetMaximumPacketSize() { return maximumPacketSize & 0x7FF; }
} ES_STRUCT_PACKED;

typedef void (*KUSBTransferCallback)(ptrdiff_t bytesNotTransferred /* -1 if error */, EsGeneric context);

struct KUSBDevice : KDevice {
	bool GetString(uint8_t index, char *buffer, size_t bufferBytes);
	KUSBDescriptorHeader *GetCommonDescriptor(uint8_t type, uintptr_t index);
	bool RunTransfer(KUSBEndpointDescriptor *endpoint, void *buffer, size_t bufferBytes, size_t *bytesNotTransferred /* if null, fails if positive */);

	// Callbacks provided by the host controller:
	// NOTE These do not provide mutual exclusion; you must ensure this manually.
	bool (*controlTransfer)(KUSBDevice *device, uint8_t flags, uint8_t request, uint16_t value, uint16_t index, 
			void *buffer, uint16_t length, int operation /* K_ACCESS_READ/WRITE */, uint16_t *transferred);
	bool (*queueTransfer)(KUSBDevice *device, KUSBEndpointDescriptor *endpoint, KUSBTransferCallback callback, 
			void *buffer, size_t bufferBytes, EsGeneric context);
	bool (*selectConfigurationAndInterface)(KUSBDevice *device);

	uint8_t *configurationDescriptors;
	size_t configurationDescriptorsBytes;
	uintptr_t selectedConfigurationOffset;

	KUSBDeviceDescriptor deviceDescriptor;
	KUSBConfigurationDescriptor configurationDescriptor;
	KUSBInterfaceDescriptor interfaceDescriptor;
};

void KRegisterUSBDevice(KUSBDevice *device); // Takes ownership of the device's main handle.

// ---------------------------------------------------------------------------------------------------------------
// File systems.
// ---------------------------------------------------------------------------------------------------------------

struct CCSpace {
	// A sorted list of the cached sections in the file.
	// Maps offset -> physical address.
	KMutex cachedSectionsMutex;
	Array<struct CCCachedSection, K_CORE> cachedSections;

	// A sorted list of the active sections.
	// Maps offset -> virtual address.
	KMutex activeSectionsMutex;
	Array<struct CCActiveSectionReference, K_CORE> activeSections;

	// Used by CCSpaceFlush.
	KEvent writeComplete;

	// Callbacks.
	const struct CCSpaceCallbacks *callbacks;
};

struct KNodeMetadata {
	// Metadata stored in the node's directory entry.
	EsNodeType type;
	bool removingNodeFromCache, removingThisFromCache;
	EsFileOffset totalSize;
	EsFileOffsetDifference directoryChildren; // ES_DIRECTORY_CHILDREN_UNKNOWN if not supported by the file system.
};

struct KNode {
	void *driverNode;

	K_PRIVATE

	volatile size_t handles;
	struct FSDirectoryEntry *directoryEntry;
	struct KFileSystem *fileSystem;
	uint64_t id;
	KWriterLock writerLock; // Acquire before the parent's.
	EsError error;
	volatile uint32_t flags;
	MMObjectCacheItem cacheItem;
};

struct KFileSystem : KDevice {
	KBlockDevice *block; // Gives the sector size and count.

	KNode *rootDirectory;

	// Only use this for file system metadata that isn't cached in a Node. 
	// This must be used consistently, i.e. if you ever read a region cached, then you must always write that region cached, and vice versa.
#define FS_BLOCK_ACCESS_CACHED (1) 
#define FS_BLOCK_ACCESS_SOFT_ERRORS (2)
	// Access the block device. Returns true on success.
	// Offset and count must be sector aligned. Buffer must be DWORD aligned.
	EsError Access(EsFileOffset offset, size_t count, int operation, void *buffer, uint32_t flags, KWorkGroup *dispatchGroup = nullptr);

	// Fill these fields in before registering the file system:

	char name[64];
	size_t nameBytes;

	size_t directoryEntryDataBytes; // The size of the driverData passed to FSDirectoryEntryFound and received in the load callback.
	size_t nodeDataBytes; // The average bytes allocated by the driver per node (used for managing cache sizes).

	EsFileOffsetDifference rootDirectoryInitialChildren;
	EsFileOffset spaceTotal, spaceUsed;
	EsUniqueIdentifier identifier;

	size_t  	(*read)		(KNode *node, void *buffer, EsFileOffset offset, EsFileOffset count);
	size_t  	(*write)	(KNode *node, const void *buffer, EsFileOffset offset, EsFileOffset count);
	void  		(*sync)		(KNode *directory, KNode *node); // TODO Error reporting?
	EsError		(*scan)		(const char *name, size_t nameLength, KNode *directory); // Add the entry with FSDirectoryEntryFound.
	EsError		(*load)		(KNode *directory, KNode *node, KNodeMetadata *metadata /* for if you need to update it */, 
						const void *entryData /* driverData passed to FSDirectoryEntryFound */);
	EsFileOffset  	(*resize)	(KNode *file, EsFileOffset newSize, EsError *error);
	EsError		(*create)	(const char *name, size_t nameLength, EsNodeType type, KNode *parent, KNode *node, void *driverData);
	EsError 	(*enumerate)	(KNode *directory); // Add the entries with FSDirectoryEntryFound.
	EsError		(*remove)	(KNode *directory, KNode *file);
	EsError  	(*move)		(KNode *oldDirectory, KNode *file, KNode *newDirectory, const char *newName, size_t newNameLength);
	void  		(*close)	(KNode *node);
	void		(*unmount)	(KFileSystem *fileSystem);

	// TODO Normalizing file names, for case-insensitive filesystems.
	// void *       (*normalize)    (const char *name, size_t nameLength, size_t *resultLength); 

	// Internals.

	KMutex moveMutex;
	bool isBootFileSystem, unmounting;
	EsUniqueIdentifier installationIdentifier;
	volatile uint64_t totalHandleCount;
	CCSpace cacheSpace;

	MMObjectCache cachedDirectoryEntries, // Directory entries without a loaded node.
		      cachedNodes; // Nodes with no handles or directory entries.
};

EsError FSDirectoryEntryFound(KNode *parentDirectory, KNodeMetadata *metadata /* ignored if the entry is already cached */, 
		const void *driverData /* if update is false and the entry is already cached, this must match the previous driverData */,
		const void *name, size_t nameBytes,
		bool update /* set to true if you don't want to insert an new entry if it isn't already cached; returns ES_SUCCESS or ES_ERROR_FILE_DOES_NOT_EXIST only */,
		KNode **node = nullptr /* set if scanning to immediately load; call FSNodeScanAndLoadComplete afterwards */);

// Call if you are scanning and used immediate load with FSDirectoryEntryFound.
void FSNodeScanAndLoadComplete(KNode *node, bool success);

// Equivalent to FSDirectoryEntryFound with update set to true, 
// but lets you pass an arbitrary KNode instead of a [directory, file name] pair.
void FSNodeUpdateDriverData(KNode *node, const void *newDriverData);

bool FSFileSystemInitialise(KFileSystem *fileSystem); // Do not attempt to load the file system if this returns false; the file system will be destroyed.

// All these functions take ownership of the device's main handle.
void FSRegisterBlockDevice(KBlockDevice *blockDevice);
void FSRegisterFileSystem(KFileSystem *fileSystem);
void FSRegisterBootFileSystem(KFileSystem *fileSystem, EsUniqueIdentifier identifier);

#define K_SIGNATURE_BLOCK_SIZE (65536)

struct KNodeInformation {
	EsError error;
	KNode *node;
};

KNodeInformation FSNodeOpen(const char *path, size_t pathBytes, uint32_t flags, KNode *baseDirectory = nullptr);

EsFileOffset FSNodeGetTotalSize(KNode *node);

char *FSNodeGetName(KNode *node, size_t *bytes); // For debugging use only.

// Do not pass memory-mapped buffers.
#define FS_FILE_ACCESS_USER_BUFFER_MAPPED (1 << 0)
ptrdiff_t FSFileReadSync(KNode *node, K_USER_BUFFER void *buffer, EsFileOffset offset, EsFileOffset bytes, uint32_t flags);
ptrdiff_t FSFileWriteSync(KNode *node, const K_USER_BUFFER void *buffer, EsFileOffset offset, EsFileOffset bytes, uint32_t flags);

// ---------------------------------------------------------------------------------------------------------------
// Graphics.
// ---------------------------------------------------------------------------------------------------------------

struct KGraphicsTarget : KDevice {
	size_t screenWidth, screenHeight;
	bool reducedColors; // Set to true if using less than 15 bit color.

	void (*updateScreen)(K_USER_BUFFER const uint8_t *source, uint32_t sourceWidth, uint32_t sourceHeight, uint32_t sourceStride, 
			uint32_t destinationX, uint32_t destinationY);
	void (*debugPutBlock)(uintptr_t x, uintptr_t y, bool toggle);
	void (*debugClearScreen)();
};

// TODO Locking for these functions?
void KRegisterGraphicsTarget(KGraphicsTarget *target);
bool KGraphicsIsTargetRegistered();

// Shared implementation of updating the screen for targets that use 32-bit linear buffers.
void GraphicsUpdateScreen32(K_USER_BUFFER const uint8_t *source, uint32_t sourceWidth, uint32_t sourceHeight, uint32_t sourceStride,
		uint32_t destinationX, uint32_t destinationY,
		uint32_t width, uint32_t height, uint32_t stride, volatile uint8_t *pixel);
void GraphicsUpdateScreen24(K_USER_BUFFER const uint8_t *_source, uint32_t sourceWidth, uint32_t sourceHeight, uint32_t sourceStride, 
		uint32_t destinationX, uint32_t destinationY,
		uint32_t width, uint32_t height, uint32_t stride, volatile uint8_t *pixel);
void GraphicsDebugPutBlock32(uintptr_t x, uintptr_t y, bool toggle,
		unsigned screenWidth, unsigned screenHeight, unsigned stride, volatile uint8_t *linearBuffer);
void GraphicsDebugClearScreen32(unsigned screenWidth, unsigned screenHeight, unsigned stride, volatile uint8_t *linearBuffer);

// ---------------------------------------------------------------------------------------------------------------
// Networking.
// ---------------------------------------------------------------------------------------------------------------

struct KIPAddress {
	uint8_t d[4];
};

struct KMACAddress {
	uint8_t d[6];
};

struct NetTask {
	void (*callback)(NetTask *task, void *receivedData);
	struct NetInterface *interface;
	uint16_t index;
	int16_t error;
	uint8_t step;
	bool completed;
};

struct NetAddressSetupTask : NetTask {
	uint32_t dhcpTransactionID;
	bool changedState;
};

struct NetInterface : KDevice {
	KIPAddress ipAddress;

	// Set by driver before registering:

	bool (*transmit)(NetInterface *self, void *dataVirtual, uintptr_t dataPhysical, size_t dataBytes); 

	union {
		KMACAddress macAddress;
		uint64_t macAddress64;
	};

	// Internals:

	K_PRIVATE

	SimpleList item;
	NetAddressSetupTask addressSetupTask;

	Array<struct ARPEntry, K_FIXED> arpTable;
	Array<struct ARPRequest, K_FIXED> arpRequests;
	KWriterLock arpTableLock; 

	// Changing the connection status and cancelling packets requires exclusive access. 
	// NetTaskBegin and NetInterfaceReceive (and hence all NetTask callbacks) run with shared access.
	KWriterLock connectionLock; 

	bool connected, hasIP;
	uint16_t ipIdentification;
	KIPAddress serverIdentifier;
	KIPAddress dnsServerIP;
	KIPAddress routerIP;
};

enum NetPacketType {
	NET_PACKET_ETHERNET,
};

void NetTransmitBufferReturn(void *data); // Once a driver is finished with a transmit buffer, it should return it here. If the driver returns false from the transmit callback, then the driver must *not* return the buffer.

void NetTaskBegin(NetTask *task);
void NetTaskComplete(NetTask *task, EsError error);

void KRegisterNetInterface(NetInterface *interface);
void NetInterfaceReceive(NetInterface *interface, const uint8_t *data, size_t dataBytes, NetPacketType packetType); // NOTE Currently this can be only called on one thread for each NetInterface. (This restriction will hopefully be removed soon.)
void NetInterfaceSetConnected(NetInterface *interface, bool connected); // NOTE This shouldn't be called by more than one thread.
void NetInterfaceShutdown(NetInterface *interface); // NOTE This doesn't do any disconnecting/cancelling of tasks. Currently it only sends a DHCP request to release the IP address, and is expected to be called at the final stages of system shutdown.
