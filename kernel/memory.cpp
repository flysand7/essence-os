// TODO Soft page faults.
// TODO Paging file.
// TODO Large pages.
// TODO Locking memory.
// TODO No execute permissions.
// TODO NUMA?
// TODO Cache coloring.

#define GUARD_PAGES

#ifndef IMPLEMENTATION

// A contiguous range of addresses in an MMSpace.

struct MMRegion {
	uintptr_t baseAddress;
	size_t pageCount;
	uint32_t flags;

	struct {
		union {
			struct {
				uintptr_t offset;
			} physical;

			struct {
				struct MMSharedRegion *region;
				uintptr_t offset;
			} shared;

			struct {
				struct FSFile *node;
				EsFileOffset offset;
				size_t zeroedBytes;
				uint64_t fileHandleFlags;
			} file;

			struct {
				RangeSet commit; // TODO Currently guarded by MMSpace::reserveMutex, maybe give it its own mutex?
				size_t commitPageCount;
				MMRegion *guardBefore, *guardAfter;
			} normal;
		};

		KWriterLock pin; // Take SHARED when using the region in a system call. Take EXCLUSIVE to free the region.
		KMutex mapMutex; // Access the page tables for a specific region, e.g. to atomically check if a mapping exists and if not, create it.
	} data;

	union {
		struct {
			AVLItem<MMRegion> itemBase;

			union {
				AVLItem<MMRegion> itemSize;
				LinkedItem<MMRegion> itemNonGuard;
			};
		};

		struct {
			bool used;
		} core;
	};
};

// A virtual address space.
// One per process.

struct MMSpace {
	VIRTUAL_ADDRESS_SPACE_DATA();	// Architecture specific data.

	AVLTree<MMRegion>         	// Key =
		freeRegionsBase, 	// Base address
		freeRegionsSize, 	// Page count
		usedRegions;     	// Base address
	LinkedList<MMRegion> usedRegionsNonGuard;

	KMutex reserveMutex; 		// Acquire to Access the region trees.

	bool user; 			// Regions in the space may be accessed from userspace.
	uint64_t commit; 		// An *approximate* commit in pages. TODO Better memory usage tracking.
	uint64_t reserve;		// The number of reserved pages.
};

// A physical page of memory.

struct MMPageFrame {
	volatile enum : uint8_t {
		// Frames that can't be used.
		UNUSABLE,	// Not connected to RAM.
		BAD,		// Connected to RAM with errors. TODO

		// Frames that aren't referenced.
		ZEROED,		// Cleared with zeros.
		FREE,		// Contains whatever data is had when it was freed.

		// Frames that are referenced by an invalid [shared] page mapping.
		// In shared regions, each invalid page mapping points to the shared page mapping.
		// This means only one variable must be updated to reuse the frame.
		STANDBY,	// Data has been written to page file or backing file. 

		// Frames that are referenced by one or more valid page mappings.
		ACTIVE,
	} state;

	volatile uint8_t flags;

	// The reference to this frame in a CCCachedSection.
	volatile uintptr_t *cacheReference;

	union {
		struct {
			// For STANDBY, MODIFIED, UPDATED, ZEROED and FREE.
			// The frame's position in a list.
			volatile uintptr_t next, *previous;
		} list;

		struct {
			// For ACTIVE.
			// For file pages, this tracks how many valid page table entries point to this frame.
			volatile uintptr_t references;
		} active;
	};
};

// A shared memory region.

struct MMSharedRegion {
	size_t sizeBytes;
	volatile size_t handles;
	KMutex mutex;
	LinkedItem<MMSharedRegion> namedItem;
	char cName[ES_SHARED_MEMORY_NAME_MAX_LENGTH + 1];
	void *data;
};

// An object pool, for fast allocation and deallocation of objects of constant size.
// (There is no guarantee that the objects will be contiguous in memory.)

struct Pool {
	void *Add(size_t elementSize); 		// Aligned to the size of a pointer.
	void Remove(void *element);

	size_t elementSize;
	void *cache[POOL_CACHE_COUNT];
	size_t cacheEntries;
	KMutex mutex;
};

// Physical memory manager state.

struct PMM {
	MMPageFrame *pageFrames;

	uintptr_t firstFreePage;
	uintptr_t firstZeroedPage;
	uintptr_t firstStandbyPage, lastStandbyPage;
	Bitset freeOrZeroedPageBitset; // Used for allocating large pages.

	uintptr_t countZeroedPages, countFreePages, countStandbyPages, countActivePages;

#define MM_REMAINING_COMMIT()			  (pmm.commitLimit - pmm.commitPageable - pmm.commitFixed)
	int64_t commitFixed, commitPageable, 
		  commitFixedLimit, commitLimit;

	                      			// Acquire to:
	KMutex commitMutex,    			// (Un)commit pages.
	      pageFrameMutex; 			// Allocate or free pages.

	KMutex pmManipulationLock;
	KSpinlock pmManipulationProcessorLock;
	void *pmManipulationRegion;

	Thread *zeroPageThread, *balanceThread;
	KEvent zeroPageEvent;

	LinkedList<MMObjectCache> objectCacheList;
	KMutex objectCacheListMutex;

	// Events for when the number of available pages is low.
#define MM_AVAILABLE_PAGES() 			(pmm.countZeroedPages + pmm.countFreePages + pmm.countStandbyPages)
	KEvent availableCritical, availableLow;
	KEvent availableNotCritical;

	// Event for when the object cache should be trimmed.
#define MM_OBJECT_CACHE_SHOULD_TRIM()             (pmm.approximateTotalObjectCacheBytes / K_PAGE_SIZE > MM_OBJECT_CACHE_PAGES_MAXIMUM())
	uintptr_t approximateTotalObjectCacheBytes;
	KEvent trimObjectCaches;

	// These variables will be cleared if the object they point to is removed.
	// See MMUnreserve and Scheduler::RemoveProcess.
	Process *nextProcessToBalance;
	MMRegion *nextRegionToBalance;
	uintptr_t balanceResumePosition;
};

// See MMPhysicalAllocate.
alignas(K_PAGE_SIZE) uint8_t earlyZeroBuffer[K_PAGE_SIZE];

// Memory spaces.
// kernelMMSpace - Whence the kernel allocates memory.
// coreMMSpace - Whence other memory managers allocate memory.

extern MMSpace _kernelMMSpace, _coreMMSpace;
#define kernelMMSpace (&_kernelMMSpace)
#define coreMMSpace (&_coreMMSpace)

// Constants.

// MMArchMapPage.
#define MM_MAP_PAGE_NOT_CACHEABLE 		(1 << 0)
#define MM_MAP_PAGE_USER 			(1 << 1)
#define MM_MAP_PAGE_OVERWRITE 			(1 << 2)
#define MM_MAP_PAGE_COMMIT_TABLES_NOW 		(1 << 3)
#define MM_MAP_PAGE_READ_ONLY			(1 << 4)
#define MM_MAP_PAGE_COPIED			(1 << 5)
#define MM_MAP_PAGE_NO_NEW_TABLES		(1 << 6)
#define MM_MAP_PAGE_FRAME_LOCK_ACQUIRED		(1 << 7)
#define MM_MAP_PAGE_WRITE_COMBINING		(1 << 8)
#define MM_MAP_PAGE_IGNORE_IF_MAPPED		(1 << 9)

// MMArchUnmapPages.
#define MM_UNMAP_PAGES_FREE 			(1 << 0)
#define MM_UNMAP_PAGES_FREE_COPIED		(1 << 1)
#define MM_UNMAP_PAGES_BALANCE_FILE		(1 << 2)

// MMPhysicalAllocate.
// --> Moved to module.h.

// MMHandlePageFault.
#define MM_HANDLE_PAGE_FAULT_WRITE 		(1 << 0)
#define MM_HANDLE_PAGE_FAULT_LOCK_ACQUIRED	(1 << 1)
#define MM_HANDLE_PAGE_FAULT_FOR_SUPERVISOR	(1 << 2)

// MMStandardAllocate - flags passed through into MMReserve.
// --> Moved to module.h.

// MMReserve - region types.
#define MM_REGION_PHYSICAL                      (0x0100) // The region is mapped to device memory.
#define MM_REGION_NORMAL                        (0x0200) // A normal region.
#define MM_REGION_SHARED                        (0x0400) // The region's contents is shared via a MMSharedRegion.
#define MM_REGION_GUARD	                        (0x0800) // A guard region, to make sure we don't accidentally go into other regions.
#define MM_REGION_CACHE	                        (0x1000) // Used for the file cache.
#define MM_REGION_FILE	                        (0x2000) // A mapped file. 

#define MM_SHARED_ENTRY_PRESENT 		(1)

// Architecture-dependent functions.

void MMArchMapPage(MMSpace *space, uintptr_t physicalAddress, uintptr_t virtualAddress, unsigned flags);
void MMArchUnmapPages(MMSpace *space, uintptr_t virtualAddressStart, uintptr_t pageCount, unsigned flags, size_t unmapMaximum = 0, uintptr_t *resumePosition = nullptr);
void MMArchInvalidatePages(uintptr_t virtualAddressStart, uintptr_t pageCount);
bool MMArchHandlePageFault(uintptr_t address, uint32_t flags);
uintptr_t MMArchTranslateAddress(MMSpace *space, uintptr_t virtualAddress, bool writeAccess);
void MMArchInitialiseVAS();
bool MMArchInitialiseUserSpace(MMSpace *space);
bool MMArchCommitPageTables(MMSpace *space, MMRegion *region);
bool MMArchMakePageWritable(MMSpace *space, uintptr_t virtualAddress);
void MMFreeVAS(MMSpace *space);
void MMFinalizeVAS(MMSpace *space);

// Forward declarations.

bool MMHandlePageFault(MMSpace *space, uintptr_t address, unsigned flags);
bool MMUnmapFilePage(uintptr_t frameNumber, bool justLoaded = false); // Returns true if the page became inactive.

// Public memory manager functions.

void *MMMapPhysical(MMSpace *space, uintptr_t address, size_t bytes, uint64_t caching);
void *MMStandardAllocate(MMSpace *space, size_t bytes, uint32_t flags, void *baseAddress, bool commitAll);
bool MMFree(MMSpace *space, void *address, size_t expectedSize, bool userOnly);
MMRegion *MMReserve(MMSpace *space, size_t bytes, unsigned flags, uintptr_t forcedAddress = 0, bool generateGuardPages = false);
bool MMCommit(uint64_t bytes, bool fixed);
void MMDecommit(uint64_t bytes, bool fixed);
bool MMCommitRange(MMSpace *space, MMRegion *region, uintptr_t offset, size_t pages); 
bool MMDecommitRange(MMSpace *space, MMRegion *region, uintptr_t offset, size_t pages); 
uintptr_t MMPhysicalAllocate(unsigned flags, uintptr_t count, uintptr_t align, uintptr_t below);
void MMInitialise();
MMSharedRegion *MMSharedCreateRegion(size_t sizeBytes, bool fixed, uintptr_t below);
MMSharedRegion *MMSharedOpenRegion(const char *name, size_t nameBytes, 
		size_t fallbackSizeBytes /* If the region doesn't exist, the size it should be created with. If 0, it'll fail. */, uint64_t flags);
MMRegion *MMFindAndPinRegion(MMSpace *space, uintptr_t address, uintptr_t size);
void MMUnpinRegion(MMSpace *space, MMRegion *region);
void MMSpaceDestroy(MMSpace *space);
bool MMSpaceInitialise(MMSpace *space);
void MMPhysicalFree(uintptr_t page, bool mutexAlreadyAcquired, size_t count);
void MMUnreserve(MMSpace *space, MMRegion *remove, bool unmapPages, bool guardRegion = false);
MMRegion *MMFindRegion(MMSpace *space, uintptr_t address);
void *MMMapFile(MMSpace *space, struct FSFile *node, EsFileOffset offset, size_t bytes, 
		int protection, void *baseAddress, size_t zeroedBytes = 0, uint32_t additionalFlags = ES_FLAGS_DEFAULT);

// Physical memory information from the bootloader.

struct PhysicalMemoryRegion {
	uint64_t baseAddress;

	// The memory map the BIOS provides gives the information in pages.
	uint64_t pageCount;
};

extern PhysicalMemoryRegion *physicalMemoryRegions;
extern size_t physicalMemoryRegionsCount;
extern size_t physicalMemoryRegionsPagesCount;
extern size_t physicalMemoryOriginalPagesCount;
extern size_t physicalMemoryRegionsIndex;
extern uintptr_t physicalMemoryHighest;

// Physical memory manipulation.

void PMZero(uintptr_t *pages, size_t pageCount, 
		bool contiguous /* True if `pages[0]` contains the base address and all other are contiguous, false if `pages` contains an array of physical addresses. */);
void PMZeroPartial(uintptr_t page, uintptr_t start, uintptr_t end);
void PMCheckZeroed(uintptr_t page);
void PMCopy(uintptr_t page, void *source, size_t pageCount);

#endif

#ifdef IMPLEMENTATION

// Globals.

MMSpace _kernelMMSpace, _coreMMSpace;
PMM pmm;
MMActiveSectionManager activeSectionManager;

MMRegion *mmCoreRegions = (MMRegion *) MM_CORE_REGIONS_START;
size_t mmCoreRegionCount, mmCoreRegionArrayCommit;

LinkedList<MMSharedRegion> mmNamedSharedRegions;
KMutex mmNamedSharedRegionsMutex;

// Code!

void MMUpdateAvailablePageCount(bool increase) {
	if (MM_AVAILABLE_PAGES() >= MM_CRITICAL_AVAILABLE_PAGES_THRESHOLD) {
		KEventSet(&pmm.availableNotCritical, false, true);
		KEventReset(&pmm.availableCritical);
	} else {
		KEventReset(&pmm.availableNotCritical);
		KEventSet(&pmm.availableCritical, false, true);

		if (!increase) {
			KernelLog(LOG_ERROR, "Memory", "critical page limit hit", 
					"Critical number of available pages remain: %d (%dKB).\n", MM_AVAILABLE_PAGES(), MM_AVAILABLE_PAGES() * K_PAGE_SIZE / 1024);
		}
	}

	if (MM_AVAILABLE_PAGES() >= MM_LOW_AVAILABLE_PAGES_THRESHOLD) {
		KEventReset(&pmm.availableLow);
	} else {
		KEventSet(&pmm.availableLow, false, true);
	}
}

void MMPhysicalInsertZeroedPage(uintptr_t page) {
	if (GetCurrentThread() != pmm.zeroPageThread) {
		KernelPanic("MMPhysicalInsertZeroedPage - Inserting a zeroed page not on the MMZeroPageThread.\n");
	}

	MMPageFrame *frame = pmm.pageFrames + page;
	frame->state = MMPageFrame::ZEROED;

	{
		frame->list.next = pmm.firstZeroedPage;
		frame->list.previous = &pmm.firstZeroedPage;
		if (pmm.firstZeroedPage) pmm.pageFrames[pmm.firstZeroedPage].list.previous = &frame->list.next;
		pmm.firstZeroedPage = page;
	}

	pmm.countZeroedPages++;
	pmm.freeOrZeroedPageBitset.Put(page);

	MMUpdateAvailablePageCount(true);
}

void MMPhysicalInsertFreePage(uintptr_t page) {
	MMPageFrame *frame = pmm.pageFrames + page;
	frame->state = MMPageFrame::FREE;

	{
		frame->list.next = pmm.firstFreePage;
		frame->list.previous = &pmm.firstFreePage;
		if (pmm.firstFreePage) pmm.pageFrames[pmm.firstFreePage].list.previous = &frame->list.next;
		pmm.firstFreePage = page;
	}

	pmm.freeOrZeroedPageBitset.Put(page);
	pmm.countFreePages++;

	if (pmm.countFreePages > MM_ZERO_PAGE_THRESHOLD) {
		KEventSet(&pmm.zeroPageEvent, false, true);
	}

	MMUpdateAvailablePageCount(true);
}

void MMPhysicalActivatePages(uintptr_t pages, uintptr_t count, unsigned flags) {
	(void) flags;

	KMutexAssertLocked(&pmm.pageFrameMutex);

	for (uintptr_t i = 0; i < count; i++) {
		MMPageFrame *frame = pmm.pageFrames + pages + i;

		if (frame->state == MMPageFrame::FREE) {
			pmm.countFreePages--;
		} else if (frame->state == MMPageFrame::ZEROED) {
			pmm.countZeroedPages--;
		} else if (frame->state == MMPageFrame::STANDBY) {
			pmm.countStandbyPages--;

			if (pmm.lastStandbyPage == pages + i) {
				if (frame->list.previous == &pmm.firstStandbyPage) {
					// There are no more pages in the list.
					pmm.lastStandbyPage = 0;
				} else {
					pmm.lastStandbyPage = ((uintptr_t) frame->list.previous - (uintptr_t) pmm.pageFrames) / sizeof(MMPageFrame);
				}
			}
		} else {
			KernelPanic("MMPhysicalActivatePages - Corrupt page frame database (4).\n");
		}

		// Unlink the frame from its list.
		*frame->list.previous = frame->list.next;
		if (frame->list.next) pmm.pageFrames[frame->list.next].list.previous = frame->list.previous;

		EsMemoryZero(frame, sizeof(MMPageFrame));
		frame->state = MMPageFrame::ACTIVE;
	}

	pmm.countActivePages += count;
	MMUpdateAvailablePageCount(false);
}

uintptr_t MMPhysicalAllocate(unsigned flags, uintptr_t count, uintptr_t align, uintptr_t below) {
	bool mutexAlreadyAcquired = flags & MM_PHYSICAL_ALLOCATE_LOCK_ACQUIRED;
	if (!mutexAlreadyAcquired) KMutexAcquire(&pmm.pageFrameMutex);
	else KMutexAssertLocked(&pmm.pageFrameMutex);
	EsDefer(if (!mutexAlreadyAcquired) KMutexRelease(&pmm.pageFrameMutex););

	intptr_t commitNow = count * K_PAGE_SIZE;

	if (flags & MM_PHYSICAL_ALLOCATE_COMMIT_NOW) {
		if (!MMCommit(commitNow, true)) return 0;
	} else commitNow = 0;

	bool simple = count == 1 && align == 1 && below == 0;

	if (physicalMemoryRegionsPagesCount) {
		// Early page allocation before the page frame database is initialised.

		if (!simple) {
			KernelPanic("MMPhysicalAllocate - Non-simple allocation before initialisation of the page frame database.\n");
		}

		uintptr_t i = physicalMemoryRegionsIndex;

		while (!physicalMemoryRegions[i].pageCount) {
			i++;

			if (i == physicalMemoryRegionsCount) {
				KernelPanic("MMPhysicalAllocate - Expected more pages in physical regions.\n");
			}
		}

		PhysicalMemoryRegion *region = physicalMemoryRegions + i;
		uintptr_t returnValue = region->baseAddress;

		region->baseAddress += K_PAGE_SIZE;
		region->pageCount--;
		physicalMemoryRegionsPagesCount--;
		physicalMemoryRegionsIndex = i;

		if (flags & MM_PHYSICAL_ALLOCATE_ZEROED) {
			// TODO Hack!
			MMArchMapPage(coreMMSpace, returnValue, (uintptr_t) earlyZeroBuffer, 
					MM_MAP_PAGE_OVERWRITE | MM_MAP_PAGE_NO_NEW_TABLES | MM_MAP_PAGE_FRAME_LOCK_ACQUIRED);
			EsMemoryZero(earlyZeroBuffer, K_PAGE_SIZE);
		}

		return returnValue;
	} else if (!simple) {
		// Slow path.
		// TODO Use standby pages.

		uintptr_t pages = pmm.freeOrZeroedPageBitset.Get(count, align, below);
		if (pages == (uintptr_t) -1) goto fail;
		MMPhysicalActivatePages(pages, count, flags);
		uintptr_t address = pages << K_PAGE_BITS;
		if (flags & MM_PHYSICAL_ALLOCATE_ZEROED) PMZero(&address, count, true);
		return address;
	} else {
		uintptr_t page = 0;
		bool notZeroed = false;

		if (!page) page = pmm.firstZeroedPage;
		if (!page) page = pmm.firstFreePage, notZeroed = true;
		if (!page) page = pmm.lastStandbyPage, notZeroed = true;
		if (!page) goto fail;

		MMPageFrame *frame = pmm.pageFrames + page;

		if (frame->state == MMPageFrame::ACTIVE) {
			KernelPanic("MMPhysicalAllocate - Corrupt page frame database (2).\n");
		}

		if (frame->state == MMPageFrame::STANDBY) {
			// EsPrint("Clear RT %x\n", frame);

			if (*frame->cacheReference != ((page << K_PAGE_BITS) | MM_SHARED_ENTRY_PRESENT)) {
				KernelPanic("MMPhysicalAllocate - Corrupt shared reference back pointer in frame %x.\n", frame);
			}

			// Clear the entry in the CCCachedSection that referenced this standby frame.
			*frame->cacheReference = 0;

			// TODO If the CCCachedSection is empty, remove it from its CCSpace.
		} else {
			pmm.freeOrZeroedPageBitset.Take(page);
		}

		MMPhysicalActivatePages(page, 1, flags);

		// EsPrint("PAGE FRAME ALLOCATE: %x\n", page << K_PAGE_BITS);

		uintptr_t address = page << K_PAGE_BITS;
		if (notZeroed && (flags & MM_PHYSICAL_ALLOCATE_ZEROED)) PMZero(&address, 1, false);
		// if (!notZeroed) PMCheckZeroed(address);
		return address;
	}

	fail:;

	if (!(flags & MM_PHYSICAL_ALLOCATE_CAN_FAIL)) {
		EsPrint("Out of memory. Committed %d/%d fixed and %d pageable out of a maximum %d.\n", pmm.commitFixed, pmm.commitFixedLimit, pmm.commitPageable, pmm.commitLimit);
		KernelPanic("MMPhysicalAllocate - Out of memory.\n");
	}

	MMDecommit(commitNow, true);
	return 0;
}

void MMPhysicalFree(uintptr_t page, bool mutexAlreadyAcquired, size_t count) {
	if (!page) KernelPanic("MMPhysicalFree - Invalid page.\n");
	if (mutexAlreadyAcquired) KMutexAssertLocked(&pmm.pageFrameMutex);
	else KMutexAcquire(&pmm.pageFrameMutex);
	if (physicalMemoryRegionsPagesCount) KernelPanic("MMPhysicalFree - PMM not yet initialised.\n");

	page >>= K_PAGE_BITS;

	for (uintptr_t i = 0; i < count; i++, page++) {
		MMPageFrame *frame = pmm.pageFrames + page;

		if (frame->state == MMPageFrame::FREE) {
			KernelPanic("MMPhysicalFree - Attempting to free a FREE page.\n");
		}

		if (pmm.commitFixedLimit) {
			pmm.countActivePages--;
		}

		MMPhysicalInsertFreePage(page);
	}

	if (!mutexAlreadyAcquired) KMutexRelease(&pmm.pageFrameMutex);
}

void MMCheckUnusable(uintptr_t physicalStart, size_t bytes) {
	for (uintptr_t i = physicalStart / K_PAGE_SIZE; i < (physicalStart + bytes + K_PAGE_SIZE - 1) / K_PAGE_SIZE
			&& i < physicalMemoryHighest / K_PAGE_SIZE; i++) {
		if (pmm.pageFrames[i].state != MMPageFrame::UNUSABLE) {
			KernelPanic("MMCheckUnusable - Page frame at address %x should be unusable.\n", i * K_PAGE_SIZE);
		}
	}
}

MMRegion *MMFindRegion(MMSpace *space, uintptr_t address) {
	KMutexAssertLocked(&space->reserveMutex);

	if (space == coreMMSpace) {
		for (uintptr_t i = 0; i < mmCoreRegionCount; i++) {
			MMRegion *region = mmCoreRegions + i;

			if (region->core.used && region->baseAddress <= address
					&& region->baseAddress + region->pageCount * K_PAGE_SIZE > address) {
				return region;
			}
		}
	} else {
		AVLItem<MMRegion> *item = TreeFind(&space->usedRegions, MakeShortKey(address), TREE_SEARCH_LARGEST_BELOW_OR_EQUAL);
		if (!item) return nullptr;

		MMRegion *region = item->thisItem;
		if (region->baseAddress > address) KernelPanic("MMFindRegion - Broken usedRegions tree.\n");
		if (region->baseAddress + region->pageCount * K_PAGE_SIZE <= address) return nullptr;
		return region;
	}

	return nullptr;
}

uintptr_t MMSharedLookupPage(MMSharedRegion *region, uintptr_t pageIndex) {
	KMutexAcquire(&region->mutex);
	if (pageIndex >= region->sizeBytes >> K_PAGE_BITS) KernelPanic("MMSharedLookupPage - Page %d out of range in region %x.\n", pageIndex, region);
	uintptr_t entry = ((uintptr_t *) region->data)[pageIndex];
	KMutexRelease(&region->mutex);
	return entry;
}

bool MMHandlePageFault(MMSpace *space, uintptr_t address, unsigned faultFlags) {
	// EsPrint("HandlePageFault: %x/%x/%x\n", space, address, faultFlags);

	address &= ~(K_PAGE_SIZE - 1);

	bool lockAcquired = faultFlags & MM_HANDLE_PAGE_FAULT_LOCK_ACQUIRED;
	MMRegion *region;

	if (!lockAcquired && MM_AVAILABLE_PAGES() < MM_CRITICAL_AVAILABLE_PAGES_THRESHOLD && GetCurrentThread() && !GetCurrentThread()->isPageGenerator) {
		KernelLog(LOG_ERROR, "Memory", "waiting for non-critical state", "Page fault on non-generator thread, waiting for more available pages.\n");
		KEventWait(&pmm.availableNotCritical);
	}

	{
		if (!lockAcquired) KMutexAcquire(&space->reserveMutex);
		else KMutexAssertLocked(&space->reserveMutex);
		EsDefer(if (!lockAcquired) KMutexRelease(&space->reserveMutex));

		// Find the region, and pin it (so it can't be freed).
		region = MMFindRegion(space, address);
		if (!region) return false;
		if (!KWriterLockTake(&region->data.pin, K_LOCK_SHARED, true /* poll */)) return false;
	}

	EsDefer(KWriterLockReturn(&region->data.pin, K_LOCK_SHARED));
	KMutexAcquire(&region->data.mapMutex);
	EsDefer(KMutexRelease(&region->data.mapMutex));

	if (MMArchTranslateAddress(space, address, faultFlags & MM_HANDLE_PAGE_FAULT_WRITE)) {
		// Spurious page fault.
		return true;
	}

	bool copyOnWrite = false, markModified = false;

	if (faultFlags & MM_HANDLE_PAGE_FAULT_WRITE) {
		if (region->flags & MM_REGION_COPY_ON_WRITE) {
			// This is copy-on-write page that needs to be copied.
			copyOnWrite = true;
		} else if (region->flags & MM_REGION_READ_ONLY) {
			// The page was read-only.
			KernelLog(LOG_ERROR, "Memory", "read only page fault", "MMHandlePageFault - Page was read only.\n");
			return false;
		} else {
			// We mapped the page as read-only so we could track whether it has been written to.
			// It has now been written to.
			markModified = true;
		}
	}

	uintptr_t offsetIntoRegion = address - region->baseAddress;
	uint64_t needZeroPages = 0;
	bool zeroPage = true;

	if (space->user) {
		needZeroPages = MM_PHYSICAL_ALLOCATE_ZEROED;
		zeroPage = false;
	}

	unsigned flags = ES_FLAGS_DEFAULT;

	if (space->user) flags |= MM_MAP_PAGE_USER;
	if (region->flags & MM_REGION_NOT_CACHEABLE) flags |= MM_MAP_PAGE_NOT_CACHEABLE;
	if (region->flags & MM_REGION_WRITE_COMBINING) flags |= MM_MAP_PAGE_WRITE_COMBINING;
	if (!markModified && !(region->flags & MM_REGION_FIXED) && (region->flags & MM_REGION_FILE)) flags |= MM_MAP_PAGE_READ_ONLY;

	if (region->flags & MM_REGION_PHYSICAL) {
		MMArchMapPage(space, region->data.physical.offset + address - region->baseAddress, address, flags);
		return true;
	} else if (region->flags & MM_REGION_SHARED) {
		MMSharedRegion *sharedRegion = region->data.shared.region;

		if (!sharedRegion->handles) {
			KernelPanic("MMHandlePageFault - Shared region has no handles.\n");
		}

		KMutexAcquire(&sharedRegion->mutex);

		uintptr_t offset = address - region->baseAddress + region->data.shared.offset;

		if (offset >= sharedRegion->sizeBytes) {
			KMutexRelease(&sharedRegion->mutex);
			KernelLog(LOG_ERROR, "Memory", "outside shared size", "MMHandlePageFault - Attempting to access shared memory past end of region.\n");
			return false;
		}

		uintptr_t *entry = (uintptr_t *) sharedRegion->data + (offset / K_PAGE_SIZE);

		if (*entry & MM_SHARED_ENTRY_PRESENT) zeroPage = false;
		else *entry = MMPhysicalAllocate(needZeroPages) | MM_SHARED_ENTRY_PRESENT;

		MMArchMapPage(space, *entry & ~(K_PAGE_SIZE - 1), address, flags);
		if (zeroPage) EsMemoryZero((void *) address, K_PAGE_SIZE); 
		KMutexRelease(&sharedRegion->mutex);
		return true;
	} else if (region->flags & MM_REGION_FILE) {
		if (address >= region->baseAddress + (region->pageCount << K_PAGE_BITS) - region->data.file.zeroedBytes) {
			// EsPrint("%x:%d\n", address, needZeroPages);
			MMArchMapPage(space, MMPhysicalAllocate(needZeroPages), address, (flags & ~MM_MAP_PAGE_READ_ONLY) | MM_MAP_PAGE_COPIED);
			if (zeroPage) EsMemoryZero((void *) address, K_PAGE_SIZE); 
			return true;
		}

		if (copyOnWrite) {
			doCopyOnWrite:;
			uintptr_t existingTranslation = MMArchTranslateAddress(space, address);

			if (existingTranslation) {
				// We need to use PMCopy, because as soon as the page is mapped for the user,
				// other threads can access it.
				uintptr_t page = MMPhysicalAllocate(ES_FLAGS_DEFAULT);
				PMCopy(page, (void *) address, 1);
				MMArchUnmapPages(space, address, 1, MM_UNMAP_PAGES_BALANCE_FILE);
				MMArchMapPage(space, page, address, (flags & ~MM_MAP_PAGE_READ_ONLY) | MM_MAP_PAGE_COPIED);
				return true;
			} else {
				// EsPrint("Write to unmapped, %x.\n", address);
			}
		}

		KMutexRelease(&region->data.mapMutex);

		size_t pagesToRead = 16;

		if (region->pageCount - (offsetIntoRegion + region->data.file.zeroedBytes) / K_PAGE_SIZE < pagesToRead) {
			pagesToRead = region->pageCount - (offsetIntoRegion + region->data.file.zeroedBytes) / K_PAGE_SIZE;
		}

		EsError error = CCSpaceAccess(&region->data.file.node->cache, (void *) address, 
				offsetIntoRegion + region->data.file.offset, pagesToRead * K_PAGE_SIZE, 
				CC_ACCESS_MAP, space, flags);

		KMutexAcquire(&region->data.mapMutex);

		if (error != ES_SUCCESS) {
			KernelLog(LOG_ERROR, "Memory", "mapped file read error", "MMHandlePageFault - Could not read from file %x. Error: %d.\n", 
					region->data.file.node, error);
			return false;
		}

		if (copyOnWrite) {
			goto doCopyOnWrite;
		}

		return true;
	} else if (region->flags & MM_REGION_NORMAL) {
		if (!(region->flags & MM_REGION_NO_COMMIT_TRACKING)) {
			if (!region->data.normal.commit.Contains(offsetIntoRegion >> K_PAGE_BITS)) {
				KernelLog(LOG_ERROR, "Memory", "outside commit range", "MMHandlePageFault - Attempting to access uncommitted memory in reserved region.\n");
				return false;
			}
		}

		MMArchMapPage(space, MMPhysicalAllocate(needZeroPages), address, flags);
		if (zeroPage) EsMemoryZero((void *) address, K_PAGE_SIZE); 
		return true;
	} else if (region->flags & MM_REGION_GUARD) {
		KernelLog(LOG_ERROR, "Memory", "guard page hit", "MMHandlePageFault - Guard page hit!\n");
		return false;
	} else {
		KernelLog(LOG_ERROR, "Memory", "cannot fault in region", "MMHandlePageFault - Unsupported region type (flags: %x).\n", region->flags);
		return false;
	}
}

MMRegion *MMReserve(MMSpace *space, size_t bytes, unsigned flags, uintptr_t forcedAddress, bool generateGuardPages) {
	MMRegion *outputRegion = nullptr;
	size_t pagesNeeded = ((bytes + K_PAGE_SIZE - 1) & ~(K_PAGE_SIZE - 1)) / K_PAGE_SIZE;

	if (!pagesNeeded) return nullptr;

	KMutexAssertLocked(&space->reserveMutex);

	if (space == coreMMSpace) {
		if (mmCoreRegionCount == MM_CORE_REGIONS_COUNT) {
			return nullptr;
		}

		if (forcedAddress) {
			KernelPanic("MMReserve - Using a forced address in coreMMSpace.\n");
		}

		{
			uintptr_t newRegionCount = mmCoreRegionCount + 1;
			uintptr_t commitPagesNeeded = newRegionCount * sizeof(MMRegion) / K_PAGE_SIZE + 1;

			while (mmCoreRegionArrayCommit < commitPagesNeeded) {
				if (!MMCommit(K_PAGE_SIZE, true)) return nullptr;
				mmCoreRegionArrayCommit++;
			}
		}

		for (uintptr_t i = 0; i < mmCoreRegionCount; i++) {
			MMRegion *region = mmCoreRegions + i;

			if (!region->core.used && region->pageCount >= pagesNeeded) {
				if (region->pageCount > pagesNeeded) {
					MMRegion *split = mmCoreRegions + mmCoreRegionCount++;
					EsMemoryCopy(split, region, sizeof(MMRegion));
					split->baseAddress += pagesNeeded * K_PAGE_SIZE;
					split->pageCount -= pagesNeeded;
				}

				region->core.used = true;
				region->pageCount = pagesNeeded;

				region->flags = flags;
				EsMemoryZero(&region->data, sizeof(region->data));
				outputRegion = region;
				goto done;
			}
		}
	} else if (forcedAddress) {
		AVLItem<MMRegion> *item;

		// EsPrint("reserve forced: %x\n", forcedAddress);

		// Check for a collision.
		item = TreeFind(&space->usedRegions, MakeShortKey(forcedAddress), TREE_SEARCH_EXACT);
		if (item) return nullptr;
		item = TreeFind(&space->usedRegions, MakeShortKey(forcedAddress), TREE_SEARCH_SMALLEST_ABOVE_OR_EQUAL);
		if (item && item->thisItem->baseAddress < forcedAddress + pagesNeeded * K_PAGE_SIZE) return nullptr;
		item = TreeFind(&space->usedRegions, MakeShortKey(forcedAddress + pagesNeeded * K_PAGE_SIZE - 1), TREE_SEARCH_LARGEST_BELOW_OR_EQUAL);
		if (item && item->thisItem->baseAddress + item->thisItem->pageCount * K_PAGE_SIZE > forcedAddress) return nullptr;
		item = TreeFind(&space->freeRegionsBase, MakeShortKey(forcedAddress), TREE_SEARCH_EXACT);
		if (item) return nullptr;
		item = TreeFind(&space->freeRegionsBase, MakeShortKey(forcedAddress), TREE_SEARCH_SMALLEST_ABOVE_OR_EQUAL);
		if (item && item->thisItem->baseAddress < forcedAddress + pagesNeeded * K_PAGE_SIZE) return nullptr;
		item = TreeFind(&space->freeRegionsBase, MakeShortKey(forcedAddress + pagesNeeded * K_PAGE_SIZE - 1), TREE_SEARCH_LARGEST_BELOW_OR_EQUAL);
		if (item && item->thisItem->baseAddress + item->thisItem->pageCount * K_PAGE_SIZE > forcedAddress) return nullptr;

		// EsPrint("(no collisions)\n");

		MMRegion *region = (MMRegion *) EsHeapAllocate(sizeof(MMRegion), true, K_CORE);
		region->baseAddress = forcedAddress;
		region->pageCount = pagesNeeded;
		region->flags = flags;
		TreeInsert(&space->usedRegions, &region->itemBase, region, MakeShortKey(region->baseAddress));

		EsMemoryZero(&region->data, sizeof(region->data));
		outputRegion = region;
	} else {
#ifdef GUARD_PAGES
		size_t guardPagesNeeded = generateGuardPages ? 2 : 0;
#else
		size_t guardPagesNeeded = 0;
#endif

		AVLItem<MMRegion> *item = TreeFind(&space->freeRegionsSize, MakeShortKey(pagesNeeded + guardPagesNeeded), TREE_SEARCH_SMALLEST_ABOVE_OR_EQUAL);

		if (!item) {
			goto done;
		}

		MMRegion *region = item->thisItem;
		TreeRemove(&space->freeRegionsBase, &region->itemBase);
		TreeRemove(&space->freeRegionsSize, &region->itemSize);

		if (region->pageCount > pagesNeeded + guardPagesNeeded) {
			MMRegion *split = (MMRegion *) EsHeapAllocate(sizeof(MMRegion), true, K_CORE);
			EsMemoryCopy(split, region, sizeof(MMRegion));

			split->baseAddress += (pagesNeeded + guardPagesNeeded) * K_PAGE_SIZE;
			split->pageCount -= (pagesNeeded + guardPagesNeeded);

			TreeInsert(&space->freeRegionsBase, &split->itemBase, split, MakeShortKey(split->baseAddress));
			TreeInsert(&space->freeRegionsSize, &split->itemSize, split, MakeShortKey(split->pageCount), AVL_DUPLICATE_KEYS_ALLOW);
		}

		EsMemoryZero(&region->data, sizeof(region->data));

		region->pageCount = pagesNeeded;
		region->flags = flags;

		if (guardPagesNeeded) {
			MMRegion *guardBefore = (MMRegion *) EsHeapAllocate(sizeof(MMRegion), true, K_CORE);
			MMRegion *guardAfter =  (MMRegion *) EsHeapAllocate(sizeof(MMRegion), true, K_CORE);

			EsMemoryCopy(guardBefore, region, sizeof(MMRegion));
			EsMemoryCopy(guardAfter,  region, sizeof(MMRegion));

			guardAfter->baseAddress += K_PAGE_SIZE * (pagesNeeded + 1);
			guardBefore->pageCount = guardAfter->pageCount = 1;
			guardBefore->flags     = guardAfter->flags     = MM_REGION_GUARD;

			region->baseAddress += K_PAGE_SIZE;
			region->data.normal.guardBefore = guardBefore;
			region->data.normal.guardAfter  = guardAfter;

			EsMemoryZero(&guardBefore->itemNonGuard, sizeof(guardBefore->itemNonGuard));
			EsMemoryZero(&guardAfter->itemNonGuard,  sizeof(guardAfter->itemNonGuard));

			TreeInsert(&space->usedRegions, &guardBefore->itemBase, guardBefore, MakeShortKey(guardBefore->baseAddress));
			TreeInsert(&space->usedRegions, &guardAfter ->itemBase, guardAfter,  MakeShortKey(guardAfter->baseAddress));

#if 0
			EsPrint("Guarded region: %x->%x/%x->%x/%x->%x\n", guardBefore->baseAddress, guardBefore->pageCount * K_PAGE_SIZE + guardBefore->baseAddress,
					region->baseAddress, region->pageCount * K_PAGE_SIZE + region->baseAddress,
					guardAfter->baseAddress, guardAfter->pageCount * K_PAGE_SIZE + guardAfter->baseAddress);
#endif
		}

		TreeInsert(&space->usedRegions, &region->itemBase, region, MakeShortKey(region->baseAddress));

		outputRegion = region;
		goto done;
	}

	done:;
	// EsPrint("reserve: %x -> %x\n", address, (uintptr_t) address + pagesNeeded * K_PAGE_SIZE);

	if (outputRegion) {
		// We've now got an address range for the region.
		// So we should commit the page tables that will be needed to map it.

		if (!MMArchCommitPageTables(space, outputRegion)) {
			// We couldn't commit the leading page tables.
			// So we'll have to unreserve the region.
			MMUnreserve(space, outputRegion, false);
			return nullptr;
		}

		if (space != coreMMSpace) {
			EsMemoryZero(&outputRegion->itemNonGuard, sizeof(outputRegion->itemNonGuard));
			outputRegion->itemNonGuard.thisItem = outputRegion;
			space->usedRegionsNonGuard.InsertEnd(&outputRegion->itemNonGuard); 
		}

		space->reserve += pagesNeeded;
	}

	// EsPrint("Reserve: %x->%x\n", outputRegion->baseAddress, outputRegion->pageCount * K_PAGE_SIZE + outputRegion->baseAddress);
	return outputRegion;
}

void MMUnreserve(MMSpace *space, MMRegion *remove, bool unmapPages, bool guardRegion) {
	// EsPrint("unreserve: %x, %x, %d, %d\n", remove->baseAddress, remove->flags, unmapPages, guardRegion);
	// EsDefer(EsPrint("unreserve complete\n"););

	KMutexAssertLocked(&space->reserveMutex);

	if (pmm.nextRegionToBalance == remove) {
		// If the balance thread paused while balancing this region,
		// switch to the next region.
		pmm.nextRegionToBalance = remove->itemNonGuard.nextItem ? remove->itemNonGuard.nextItem->thisItem : nullptr;
		pmm.balanceResumePosition = 0;
	}

	if (!remove) {
		KernelPanic("MMUnreserve - Region to remove was null.\n");
	}

	if (remove->flags & MM_REGION_NORMAL) {
		if (remove->data.normal.guardBefore) MMUnreserve(space, remove->data.normal.guardBefore, false, true);
		if (remove->data.normal.guardAfter)  MMUnreserve(space, remove->data.normal.guardAfter,  false, true);
	} else if ((remove->flags & MM_REGION_GUARD) && !guardRegion) {
		// You can't free a guard region!
		// TODO Error.
		KernelLog(LOG_ERROR, "Memory", "attempt to unreserve guard page", "MMUnreserve - Attempting to unreserve a guard page.\n");
		return;
	}

	if (remove->itemNonGuard.list && !guardRegion) {
		// EsPrint("Remove item non guard...\n");
		remove->itemNonGuard.RemoveFromList();
		// EsPrint("Removed.\n");
	}

	if (unmapPages) {
		MMArchUnmapPages(space, remove->baseAddress,
				remove->pageCount, ES_FLAGS_DEFAULT);
	}
	
	space->reserve += remove->pageCount;

	if (space == coreMMSpace) {
		remove->core.used = false;

		intptr_t remove1 = -1, remove2 = -1;

		for (uintptr_t i = 0; i < mmCoreRegionCount && (remove1 != -1 || remove2 != 1); i++) {
			MMRegion *r = mmCoreRegions + i;

			if (r->core.used) continue;
			if (r == remove) continue;

			if (r->baseAddress == remove->baseAddress + (remove->pageCount << K_PAGE_BITS)) {
				remove->pageCount += r->pageCount;
				remove1 = i;
			} else if (remove->baseAddress == r->baseAddress + (r->pageCount << K_PAGE_BITS)) { 
				remove->pageCount += r->pageCount;
				remove->baseAddress = r->baseAddress;
				remove2 = i;
			}
		}

		if (remove1 != -1) {
			mmCoreRegions[remove1] = mmCoreRegions[--mmCoreRegionCount];
			if ((uintptr_t) remove2 == mmCoreRegionCount) remove2 = remove1;
		}

		if (remove2 != -1) {
			mmCoreRegions[remove2] = mmCoreRegions[--mmCoreRegionCount];
		}
	} else {
		TreeRemove(&space->usedRegions, &remove->itemBase);
		uintptr_t address = remove->baseAddress;

		{
			AVLItem<MMRegion> *before = TreeFind(&space->freeRegionsBase, MakeShortKey(address), TREE_SEARCH_LARGEST_BELOW_OR_EQUAL);

			if (before && before->thisItem->baseAddress + before->thisItem->pageCount * K_PAGE_SIZE == remove->baseAddress) {
				remove->baseAddress = before->thisItem->baseAddress;
				remove->pageCount += before->thisItem->pageCount;
				TreeRemove(&space->freeRegionsBase, before);
				TreeRemove(&space->freeRegionsSize, &before->thisItem->itemSize);
				EsHeapFree(before->thisItem, sizeof(MMRegion), K_CORE);
			}
		}

		{
			AVLItem<MMRegion> *after = TreeFind(&space->freeRegionsBase, MakeShortKey(address), TREE_SEARCH_SMALLEST_ABOVE_OR_EQUAL);

			if (after && remove->baseAddress + remove->pageCount * K_PAGE_SIZE == after->thisItem->baseAddress) {
				remove->pageCount += after->thisItem->pageCount;
				TreeRemove(&space->freeRegionsBase, after);
				TreeRemove(&space->freeRegionsSize, &after->thisItem->itemSize);
				EsHeapFree(after->thisItem, sizeof(MMRegion), K_CORE);
			}
		}

		TreeInsert(&space->freeRegionsBase, &remove->itemBase, remove, MakeShortKey(remove->baseAddress));
		TreeInsert(&space->freeRegionsSize, &remove->itemSize, remove, MakeShortKey(remove->pageCount), AVL_DUPLICATE_KEYS_ALLOW);
	}
}

void *MMMapFile(MMSpace *space, FSFile *node, EsFileOffset offset, size_t bytes, int protection, void *baseAddress, size_t zeroedBytes, uint32_t additionalFlags) {
	if (protection != ES_MAP_OBJECT_READ_ONLY && protection != ES_MAP_OBJECT_COPY_ON_WRITE) {
		return nullptr;
	}

	if (node->directoryEntry->type != ES_NODE_FILE) {
		return nullptr;
	}

	MMRegion *region = nullptr;
	uint64_t fileHandleFlags = ES_NODE_PREVENT_RESIZE 
		| (protection == ES_MAP_OBJECT_READ_WRITE ? ES_FILE_WRITE : ES_FILE_READ_SHARED);
	bool decommit = false;

	// Register a handle to the node.
	// If this successes, past this point we file cannot be resized.

	if (!OpenHandleToObject(node, KERNEL_OBJECT_NODE, fileHandleFlags)) {
		return nullptr;
	}

	// Acquire the file's cache mutex before the space's reserve mutex, 
	// so that we can handle page faults without locking the reserve mutex.

	KMutexAcquire(&node->cache.cachedSectionsMutex);
	EsDefer(KMutexRelease(&node->cache.cachedSectionsMutex));
	KMutexAcquire(&space->reserveMutex);
	EsDefer(KMutexRelease(&space->reserveMutex));

	// Check the parameters.

	uintptr_t offsetIntoPage = offset & (K_PAGE_SIZE - 1);

	if (node->directoryEntry->totalSize <= offset) {
		goto fail;
	}

	if (offsetIntoPage) bytes += offset & (K_PAGE_SIZE - 1); 

	// Reserve the region.

	region = MMReserve(space, bytes + zeroedBytes, MM_REGION_FILE | additionalFlags
			| ((protection == ES_MAP_OBJECT_READ_ONLY || protection == ES_MAP_OBJECT_COPY_ON_WRITE) ? MM_REGION_READ_ONLY : 0)
			| (protection == ES_MAP_OBJECT_COPY_ON_WRITE ? MM_REGION_COPY_ON_WRITE : 0),
			baseAddress ? (uintptr_t) baseAddress - offsetIntoPage : 0);

	if (!region) {
		goto fail;
	}

	// Commit copy on write regions.

	if (protection == ES_MAP_OBJECT_COPY_ON_WRITE) {
		if (!MMCommit(bytes + zeroedBytes, false)) {
			goto fail;
		}

		decommit = true;
	}

	// Initialise data.

	region->data.file.node = node;
	region->data.file.offset = offset - offsetIntoPage;
	region->data.file.zeroedBytes = zeroedBytes;
	region->data.file.fileHandleFlags = fileHandleFlags;

	// Make sure the region is covered by MMCachedSections in the file.
	// (This is why we needed the file's cache mutex.)
	// We'll allocate in coreMMSpace as needed.

	if (!CCSpaceCover(&node->cache, region->data.file.offset, region->data.file.offset + bytes)) {
		goto fail;
	}

	return (uint8_t *) region->baseAddress + offsetIntoPage;

	fail:;
	if (region) MMUnreserve(space, region, false /* No pages have been mapped. */);
	if (decommit) MMDecommit(bytes + zeroedBytes, false);
	CloseHandleToObject(node, KERNEL_OBJECT_NODE, fileHandleFlags);
	return nullptr;
}

void *MMMapShared(MMSpace *space, MMSharedRegion *sharedRegion, uintptr_t offset, size_t bytes, uint32_t additionalFlags, void *baseAddress) {
	MMRegion *region;
	OpenHandleToObject(sharedRegion, KERNEL_OBJECT_SHMEM);

	KMutexAcquire(&space->reserveMutex);
	EsDefer(KMutexRelease(&space->reserveMutex));

	if (offset & (K_PAGE_SIZE - 1)) bytes += offset & (K_PAGE_SIZE - 1); 
	if (sharedRegion->sizeBytes <= offset) goto fail;
	if (sharedRegion->sizeBytes < offset + bytes) goto fail;

	region = MMReserve(space, bytes, MM_REGION_SHARED | additionalFlags, (uintptr_t) baseAddress);
	if (!region) goto fail;

	if (!(region->flags & MM_REGION_SHARED)) KernelPanic("MMMapShared - Cannot commit into non-shared region.\n");
	if (region->data.shared.region) KernelPanic("MMMapShared - A shared region has already been bound.\n");

	region->data.shared.region = sharedRegion;
	region->data.shared.offset = offset & ~(K_PAGE_SIZE - 1);

	return (uint8_t *) region->baseAddress + (offset & (K_PAGE_SIZE - 1));

	fail:;
	CloseHandleToObject(sharedRegion, KERNEL_OBJECT_SHMEM);
	return nullptr;
}

void MMDecommit(uint64_t bytes, bool fixed) {
	// EsPrint("De-Commit %d %d\n", bytes, fixed);

	if (bytes & (K_PAGE_SIZE - 1)) KernelPanic("MMDecommit - Expected multiple of K_PAGE_SIZE bytes.\n");
	int64_t pagesNeeded = bytes / K_PAGE_SIZE;

	KMutexAcquire(&pmm.commitMutex);
	EsDefer(KMutexRelease(&pmm.commitMutex));

	if (fixed) {
		if (pmm.commitFixed < pagesNeeded) KernelPanic("MMDecommit - Decommitted too many pages.\n");
		pmm.commitFixed -= pagesNeeded;
	} else {
		if (pmm.commitPageable < pagesNeeded) KernelPanic("MMDecommit - Decommitted too many pages.\n");
		pmm.commitPageable -= pagesNeeded;
	}

	KernelLog(LOG_VERBOSE, "Memory", "decommit", "Decommit %D%z. Now at %D.\n", bytes, fixed ? ", fixed" : "", (pmm.commitFixed + pmm.commitPageable) << K_PAGE_BITS);
}

bool MMCommit(uint64_t bytes, bool fixed) {
	if (bytes & (K_PAGE_SIZE - 1)) KernelPanic("MMCommit - Expected multiple of K_PAGE_SIZE bytes.\n");
	int64_t pagesNeeded = bytes / K_PAGE_SIZE;

	KMutexAcquire(&pmm.commitMutex);
	EsDefer(KMutexRelease(&pmm.commitMutex));

	if (pmm.commitLimit) {
		if (fixed) {
			if (pagesNeeded > pmm.commitFixedLimit - pmm.commitFixed) {
				KernelLog(LOG_ERROR, "Memory", "failed fixed commit", "Failed fixed commit %d pages (currently %d/%d).\n",
						pagesNeeded, pmm.commitFixed, pmm.commitFixedLimit);
				return false;
			}

			if (MM_AVAILABLE_PAGES() - pagesNeeded < MM_CRITICAL_AVAILABLE_PAGES_THRESHOLD && !GetCurrentThread()->isPageGenerator) {
				KernelLog(LOG_ERROR, "Memory", "failed fixed commit", 
						"Failed fixed commit %d as the available pages (%d, %d, %d) would cross the critical threshold, %d.\n.",
						pagesNeeded, pmm.countZeroedPages, pmm.countFreePages, pmm.countStandbyPages, MM_CRITICAL_AVAILABLE_PAGES_THRESHOLD);
				return false;
			}

			pmm.commitFixed += pagesNeeded;
		} else {
			if (pagesNeeded > MM_REMAINING_COMMIT() - (intptr_t) (GetCurrentThread()->isPageGenerator ? 0 : MM_CRITICAL_REMAINING_COMMIT_THRESHOLD)) {
				KernelLog(LOG_ERROR, "Memory", "failed pageable commit",
						"Failed pageable commit %d pages (currently %d/%d).\n",
						pagesNeeded, pmm.commitPageable + pmm.commitFixed, pmm.commitLimit);
				return false;
			}

			pmm.commitPageable += pagesNeeded;
		}

		if (MM_OBJECT_CACHE_SHOULD_TRIM()) {
			KEventSet(&pmm.trimObjectCaches, false, true);
		}
	} else {
		// We haven't started tracking commit counts yet.
	}

	KernelLog(LOG_VERBOSE, "Memory", "commit", "Commit %D%z. Now at %D.\n", bytes, fixed ? ", fixed" : "", (pmm.commitFixed + pmm.commitPageable) << K_PAGE_BITS);
		
	return true;
}

bool MMCommitRange(MMSpace *space, MMRegion *region, uintptr_t pageOffset, size_t pageCount) {
	KMutexAssertLocked(&space->reserveMutex);

	if (region->flags & MM_REGION_NO_COMMIT_TRACKING) {
		KernelPanic("MMCommitRange - Region does not support commit tracking.\n");
	}

	if (pageOffset >= region->pageCount || pageCount > region->pageCount - pageOffset) {
		KernelPanic("MMCommitRange - Invalid region offset and page count.\n");
	}

	if (~region->flags & MM_REGION_NORMAL) {
		KernelPanic("MMCommitRange - Cannot commit into non-normal region.\n");
	}

	intptr_t delta = 0;
	region->data.normal.commit.Set(pageOffset, pageOffset + pageCount, &delta, false);

	if (delta < 0) {
		KernelPanic("MMCommitRange - Invalid delta calculation adding %x, %x to %x.\n", pageOffset, pageCount, region);
	}

	if (delta == 0) {
		return true;
	}

	{
		if (!MMCommit(delta * K_PAGE_SIZE, region->flags & MM_REGION_FIXED)) {
			return false;
		}

		region->data.normal.commitPageCount += delta;
		space->commit += delta;

		if (region->data.normal.commitPageCount > region->pageCount) {
			KernelPanic("MMCommitRange - Invalid delta calculation increases region %x commit past page count.\n", region);
		}
	}

	if (!region->data.normal.commit.Set(pageOffset, pageOffset + pageCount, nullptr, true)) {
		MMDecommit(delta * K_PAGE_SIZE, region->flags & MM_REGION_FIXED);
		region->data.normal.commitPageCount -= delta;
		space->commit -= delta;
		return false;
	}

	if (region->flags & MM_REGION_FIXED) {
		for (uintptr_t i = pageOffset; i < pageOffset + pageCount; i++) {
			// TODO Don't call into MMHandlePageFault. I don't like MM_HANDLE_PAGE_FAULT_LOCK_ACQUIRED.

			if (!MMHandlePageFault(space, region->baseAddress + i * K_PAGE_SIZE, MM_HANDLE_PAGE_FAULT_LOCK_ACQUIRED)) {
				KernelPanic("MMCommitRange - Unable to fix pages.\n");
			}
		}
	}

	return true;
}

bool MMDecommitRange(MMSpace *space, MMRegion *region, uintptr_t pageOffset, size_t pageCount) {
	KMutexAssertLocked(&space->reserveMutex);

	if (region->flags & MM_REGION_NO_COMMIT_TRACKING) {
		KernelPanic("MMDecommitRange - Region does not support commit tracking.\n");
	}

	if (pageOffset >= region->pageCount || pageCount > region->pageCount - pageOffset) {
		KernelPanic("MMDecommitRange - Invalid region offset and page count.\n");
	}

	if (~region->flags & MM_REGION_NORMAL) {
		KernelPanic("MMDecommitRange - Cannot decommit from non-normal region.\n");
	}

	intptr_t delta = 0;

	if (!region->data.normal.commit.Clear(pageOffset, pageOffset + pageCount, &delta, true)) {
		return false;
	}

	if (delta > 0) {
		KernelPanic("MMDecommitRange - Invalid delta calculation removing %x, %x from %x.\n", pageOffset, pageCount, region);
	}

	delta = -delta;

	if (region->data.normal.commitPageCount < (size_t) delta) {
		KernelPanic("MMDecommitRange - Invalid delta calculation decreases region %x commit below zero.\n", region);
	}

	// EsPrint("\tdecommit = %x\n", pagesRemoved);

	MMDecommit(delta * K_PAGE_SIZE, region->flags & MM_REGION_FIXED);
	space->commit -= delta;
	region->data.normal.commitPageCount -= delta;
	MMArchUnmapPages(space, region->baseAddress + pageOffset * K_PAGE_SIZE, pageCount, MM_UNMAP_PAGES_FREE);

	return true;
}

void MMAllowWriteCombiningCaching(MMSpace *space, void *virtualAddress) {
	if (!space) space = kernelMMSpace;

	KMutexAcquire(&space->reserveMutex);

	MMRegion *region = MMFindRegion(space, (uintptr_t) virtualAddress);

	if (!(region->flags & MM_REGION_NOT_CACHEABLE)) {
		KernelPanic("MMAllowWriteCombiningCaching - Region was cachable.\n");
	}

	region->flags &= ~MM_REGION_NOT_CACHEABLE;
	region->flags |= MM_REGION_WRITE_COMBINING;
	MMArchUnmapPages(space, region->baseAddress, region->pageCount, ES_FLAGS_DEFAULT);

	KMutexRelease(&space->reserveMutex);

	for (uintptr_t i = 0; i < region->pageCount; i++) {
		MMHandlePageFault(space, region->baseAddress + i * K_PAGE_SIZE, ES_FLAGS_DEFAULT);
	}
}

size_t MMGetRegionPageCount(MMSpace *space, void *address) {
	if (!space) space = kernelMMSpace;

	KMutexAcquire(&space->reserveMutex);
	EsDefer(KMutexRelease(&space->reserveMutex));

	MMRegion *region = MMFindRegion(space, (uintptr_t) address);
	return region->pageCount;
}

void *MMMapPhysical(MMSpace *space, uintptr_t offset, size_t bytes, uint64_t caching) {
	if (!space) space = kernelMMSpace;

	uintptr_t offset2 = offset & (K_PAGE_SIZE - 1);
	offset -= offset2;
	if (offset2) bytes += K_PAGE_SIZE;

	MMRegion *region;

	{
		KMutexAcquire(&space->reserveMutex);
		EsDefer(KMutexRelease(&space->reserveMutex));

		region = MMReserve(space, bytes, MM_REGION_PHYSICAL | MM_REGION_FIXED | caching);
		if (!region) return nullptr;

		region->data.physical.offset = offset;
	}

	for (uintptr_t i = 0; i < region->pageCount; i++) {
		MMHandlePageFault(space, region->baseAddress + i * K_PAGE_SIZE, ES_FLAGS_DEFAULT);
	}

	return (uint8_t *) region->baseAddress + offset2;
}

bool MMSharedResizeRegion(MMSharedRegion *region, size_t sizeBytes) {
	KMutexAcquire(&region->mutex);
	EsDefer(KMutexRelease(&region->mutex));

	sizeBytes = (sizeBytes + K_PAGE_SIZE - 1) & ~(K_PAGE_SIZE - 1);

	size_t pages = sizeBytes / K_PAGE_SIZE;
	size_t oldPages = region->sizeBytes / K_PAGE_SIZE;
	void *oldData = region->data;

	void *newData = EsHeapAllocate(pages * sizeof(void *), true, K_CORE);

	if (!newData && pages) {
		return false;
	}

	if (oldPages > pages) {
		MMDecommit(K_PAGE_SIZE * (oldPages - pages), true);
	} else if (pages > oldPages) {
		if (!MMCommit(K_PAGE_SIZE * (pages - oldPages), true)) {
			EsHeapFree(newData, pages * sizeof(void *), K_CORE);
			return false;
		}
	}

	region->sizeBytes = sizeBytes;
	region->data = newData; 

	// The old shared memory region was empty.
	if (!oldData) return true;

	if (oldPages > pages) {
		for (uintptr_t i = pages; i < oldPages; i++) {
			uintptr_t *addresses = (uintptr_t *) oldData;
			uintptr_t address = addresses[i];
			if (address & MM_SHARED_ENTRY_PRESENT) MMPhysicalFree(address);
		}
	}

	uintptr_t copy = oldPages > pages ? pages : oldPages;
	EsMemoryCopy(region->data, oldData, sizeof(void *) * copy);
	EsHeapFree(oldData, oldPages * sizeof(void *), K_CORE); 

	return true;
}

MMSharedRegion *MMSharedOpenRegion(const char *name, size_t nameBytes, size_t fallbackSizeBytes, uint64_t flags) {
	if (nameBytes > ES_SHARED_MEMORY_NAME_MAX_LENGTH) return nullptr;

	KMutexAcquire(&mmNamedSharedRegionsMutex);
	EsDefer(KMutexRelease(&mmNamedSharedRegionsMutex));

	LinkedItem<MMSharedRegion> *item = mmNamedSharedRegions.firstItem;

	while (item) {
		MMSharedRegion *region = item->thisItem;

		if (EsCStringLength(region->cName) == nameBytes && 0 == EsMemoryCompare(region->cName, name, nameBytes)) {
			if (flags & ES_MEMORY_OPEN_FAIL_IF_FOUND) return nullptr;
			OpenHandleToObject(region, KERNEL_OBJECT_SHMEM);
			return region;
		}

		item = item->nextItem;
	}

	if (flags & ES_MEMORY_OPEN_FAIL_IF_NOT_FOUND) return nullptr;

	MMSharedRegion *region = MMSharedCreateRegion(fallbackSizeBytes);
	if (!region) return nullptr;
	EsMemoryCopy(region->cName, name, nameBytes);

	region->namedItem.thisItem = region;
	mmNamedSharedRegions.InsertEnd(&region->namedItem);

	return region;
}

MMSharedRegion *MMSharedCreateRegion(size_t sizeBytes, bool fixed, uintptr_t below) {
	if (!sizeBytes) return nullptr;

	MMSharedRegion *region = (MMSharedRegion *) EsHeapAllocate(sizeof(MMSharedRegion), true, K_CORE);
	if (!region) return nullptr;
	region->handles = 1;

	if (!MMSharedResizeRegion(region, sizeBytes)) {
		EsHeapFree(region, 0, K_CORE);
		return nullptr;
	}

	if (fixed) {
		for (uintptr_t i = 0; i < region->sizeBytes >> K_PAGE_BITS; i++) {
			((uintptr_t *) region->data)[i] = MMPhysicalAllocate(MM_PHYSICAL_ALLOCATE_ZEROED, 1, 1, below) | MM_SHARED_ENTRY_PRESENT;
		}
	}

	return region;
}

void MMSharedDestroyRegion(MMSharedRegion *region) {
	MMSharedResizeRegion(region, 0); // TODO Check leaks.
	EsHeapFree(region, 0, K_CORE);
}

void *MMStandardAllocate(MMSpace *space, size_t bytes, unsigned flags, void *baseAddress, bool commitAll) {
	if (!space) space = kernelMMSpace;

	KMutexAcquire(&space->reserveMutex);
	EsDefer(KMutexRelease(&space->reserveMutex));

	MMRegion *region = MMReserve(space, bytes, flags | MM_REGION_NORMAL, (uintptr_t) baseAddress, true);
	if (!region) return nullptr;

	if (commitAll) {
		if (!MMCommitRange(space, region, 0, region->pageCount)) {
			MMUnreserve(space, region, false /* No pages have been mapped. */);
			return nullptr;
		}
	}

	return (void *) region->baseAddress;
}

bool MMFree(MMSpace *space, void *address, size_t expectedSize, bool userOnly) {
	if (!space) space = kernelMMSpace;

	MMSharedRegion *sharedRegionToFree = nullptr;
	FSFile *nodeToFree = nullptr;
	uint64_t fileHandleFlags = 0;

	{
		KMutexAcquire(&space->reserveMutex);
		EsDefer(KMutexRelease(&space->reserveMutex));

		MMRegion *region = MMFindRegion(space, (uintptr_t) address);

		if (!region) {
			return false;
		}

		if (userOnly && (~region->flags & MM_REGION_USER)) {
			KernelLog(LOG_ERROR, "Memory", "attempt to free non-user region", 
					"MMFree - A user process is attempting to free a region %x that is not marked with MM_REGION_USER.\n", region);
			return false;
		}

		if (!KWriterLockTake(&region->data.pin, K_LOCK_EXCLUSIVE, true /* poll */)) {
			KernelLog(LOG_ERROR, "Memory", "attempt to free in use region", 
					"MMFree - Attempting to free a region %x that is currently in by a system call.\n", region);
			return false;
		}

		if (region->baseAddress != (uintptr_t) address && (~region->flags & MM_REGION_PHYSICAL /* physical region bases are not page aligned */)) {
			KernelLog(LOG_ERROR, "Memory", "incorrect base address", "MMFree - Passed the address %x to free region %x, which has baseAddress of %x.\n",
					address, region, region->baseAddress);
			return false;
		}

		if (expectedSize && (expectedSize + K_PAGE_SIZE - 1) / K_PAGE_SIZE != region->pageCount) {
			KernelLog(LOG_ERROR, "Memory", "incorrect free size", "MMFree - The region page count is %d, but the expected free size is %d.\n",
					region->pageCount, (expectedSize + K_PAGE_SIZE - 1) / K_PAGE_SIZE);
			return false;
		}

		bool unmapPages = true;

		if (region->flags & MM_REGION_NORMAL) {
			if (!MMDecommitRange(space, region, 0, region->pageCount)) {
				KernelPanic("MMFree - Could not decommit entire region %x (should not fail).\n", region);
			}

			if (region->data.normal.commitPageCount) {
				KernelPanic("MMFree - After decommiting range covering the entire region (%x), some pages were still commited.\n", region);
			}

			region->data.normal.commit.ranges.Free();
			unmapPages = false;
		} else if (region->flags & MM_REGION_SHARED) {
			sharedRegionToFree = region->data.shared.region;
		} else if (region->flags & MM_REGION_FILE) {
			MMArchUnmapPages(space, region->baseAddress,
					region->pageCount, MM_UNMAP_PAGES_FREE_COPIED | MM_UNMAP_PAGES_BALANCE_FILE);
			unmapPages = false;

			FSFile *node = region->data.file.node;
			EsFileOffset removeStart = RoundDown(region->data.file.offset, K_PAGE_SIZE);
			EsFileOffset removeEnd = RoundUp(removeStart + (region->pageCount << K_PAGE_BITS) - region->data.file.zeroedBytes, K_PAGE_SIZE);
			nodeToFree = node;
			fileHandleFlags = region->data.file.fileHandleFlags;

			KMutexAcquire(&node->cache.cachedSectionsMutex);
			CCSpaceUncover(&node->cache, removeStart, removeEnd);
			KMutexRelease(&node->cache.cachedSectionsMutex);

			if (region->flags & MM_REGION_COPY_ON_WRITE) {
				MMDecommit(region->pageCount << K_PAGE_BITS, false);
			}
		} else if (region->flags & MM_REGION_PHYSICAL) {
		} else if (region->flags & MM_REGION_GUARD) {
			KernelLog(LOG_ERROR, "Memory", "attempt to free guard region", "MMFree - Attempting to free a guard region!\n");
			return false;
		} else {
			KernelPanic("MMFree - Unsupported region type.\n");
		}

		MMUnreserve(space, region, unmapPages);
	}

	if (sharedRegionToFree) CloseHandleToObject(sharedRegionToFree, KERNEL_OBJECT_SHMEM);
	if (nodeToFree && fileHandleFlags) CloseHandleToObject(nodeToFree, KERNEL_OBJECT_NODE, fileHandleFlags);

	return true;
}

bool MMSpaceInitialise(MMSpace *space) {
	// EsPrint("... Committed %d/%d fixed and %d pageable out of a maximum %d.\n", pmm.commitFixed, pmm.commitFixedLimit, pmm.commitPageable, pmm.commitLimit);

	space->user = true;

	MMRegion *region = (MMRegion *) EsHeapAllocate(sizeof(MMRegion), true, K_CORE);

	if (!region) {
		return false;
	}

	if (!MMArchInitialiseUserSpace(space)) {
		EsHeapFree(region, sizeof(MMRegion), K_CORE);
		return false;
	}

	region->baseAddress = MM_USER_SPACE_START; 
	region->pageCount = MM_USER_SPACE_SIZE / K_PAGE_SIZE;
	TreeInsert(&space->freeRegionsBase, &region->itemBase, region, MakeShortKey(region->baseAddress));
	TreeInsert(&space->freeRegionsSize, &region->itemSize, region, MakeShortKey(region->pageCount), AVL_DUPLICATE_KEYS_ALLOW);

	return true;
}

void MMSpaceDestroy(MMSpace *space) {
	LinkedItem<MMRegion> *item = space->usedRegionsNonGuard.firstItem;

	while (item) {
		MMRegion *region = item->thisItem;
		item = item->nextItem;
		MMFree(space, (void *) region->baseAddress);
	}

	while (true) {
		AVLItem<MMRegion> *item = TreeFind(&space->freeRegionsBase, MakeShortKey(0), TREE_SEARCH_SMALLEST_ABOVE_OR_EQUAL);
		if (!item) break;
		TreeRemove(&space->freeRegionsBase, &item->thisItem->itemBase);
		TreeRemove(&space->freeRegionsSize, &item->thisItem->itemSize);
		EsHeapFree(item->thisItem, sizeof(MMRegion), K_CORE);
	}

	MMFreeVAS(space);
}

bool MMUnmapFilePage(uintptr_t frameNumber, bool justLoaded) {
	KMutexAssertLocked(&pmm.pageFrameMutex);

	MMPageFrame *frame = pmm.pageFrames + frameNumber;

	if (!justLoaded) {
		if (frame->state != MMPageFrame::ACTIVE || !frame->active.references) {
			KernelPanic("MMUnmapFilePage - Corrupt page frame database (%d/%x).\n", frameNumber, frame);
		}

		// Decrease the reference count.
		frame->active.references--;
	}

	if (!frame->active.references) {
		// If there are no more references, then the frame can be moved to the standby or modified list.

		// EsPrint("Unmap file page: %x\n", frameNumber << K_PAGE_BITS);

		{
			frame->state = MMPageFrame::STANDBY;
			pmm.countStandbyPages++;

			if (*frame->cacheReference != ((frameNumber << K_PAGE_BITS) | MM_SHARED_ENTRY_PRESENT)) {
				KernelPanic("MMUnmapFilePage - Corrupt shared reference back pointer in frame %x.\n", frame);
			}

			frame->list.next = pmm.firstStandbyPage;
			frame->list.previous = &pmm.firstStandbyPage;
			if (pmm.firstStandbyPage) pmm.pageFrames[pmm.firstStandbyPage].list.previous = &frame->list.next;
			if (!pmm.lastStandbyPage) pmm.lastStandbyPage = frameNumber;
			pmm.firstStandbyPage = frameNumber;

			MMUpdateAvailablePageCount(true);
		}

		pmm.countActivePages--;
		return true;
	}

	return false;
}

void MMBalanceThread() {
	size_t targetAvailablePages = 0;

	while (true) {
		if (MM_AVAILABLE_PAGES() >= targetAvailablePages) {
			// Wait for there to be a low number of available pages.
			KEventWait(&pmm.availableLow);
			targetAvailablePages = MM_LOW_AVAILABLE_PAGES_THRESHOLD + MM_PAGES_TO_FIND_BALANCE;
		}

		// EsPrint("--> Balance!\n");

		// Find a process to balance.
		
		KSpinlockAcquire(&scheduler.lock);

		Process *process = nullptr;

		while (true) {
			if (pmm.nextProcessToBalance) {
				process = pmm.nextProcessToBalance;
			} else {
				process = scheduler.allProcesses.firstItem->thisItem;
			}

			pmm.nextProcessToBalance = process->allItem.nextItem ? process->allItem.nextItem->thisItem : nullptr;

			if (process->handles) {
				process->handles++;
				break;
			}
		}

		KSpinlockRelease(&scheduler.lock);

		// For every memory region...

		MMSpace *space = process->vmm;
		GetCurrentThread()->SetAddressSpace(space);
		KMutexAcquire(&space->reserveMutex);
		LinkedItem<MMRegion> *item = pmm.nextRegionToBalance ? &pmm.nextRegionToBalance->itemNonGuard : space->usedRegionsNonGuard.firstItem;

		while (item && MM_AVAILABLE_PAGES() < targetAvailablePages) {
			MMRegion *region = item->thisItem;

			// EsPrint("process = %x, region = %x, offset = %x\n", process, region, pmm.balanceResumePosition);

			KMutexAcquire(&region->data.mapMutex);

			bool canResume = false;

			if (region->flags & MM_REGION_FILE) {
				canResume = true;
				MMArchUnmapPages(space, 
						region->baseAddress, region->pageCount, 
						MM_UNMAP_PAGES_BALANCE_FILE, 
						targetAvailablePages - MM_AVAILABLE_PAGES(), &pmm.balanceResumePosition);
			} else if (region->flags & MM_REGION_CACHE) {
				// TODO Trim the cache's active sections and cached sections lists.

				KMutexAcquire(&activeSectionManager.mutex);

				LinkedItem<CCActiveSection> *item = activeSectionManager.lruList.firstItem;

				while (item && MM_AVAILABLE_PAGES() < targetAvailablePages) {
					CCActiveSection *section = item->thisItem;
					if (section->cache && section->referencedPageCount) CCDereferenceActiveSection(section);
					item = item->nextItem;
				}

				KMutexRelease(&activeSectionManager.mutex);
			}

			KMutexRelease(&region->data.mapMutex);

			if (MM_AVAILABLE_PAGES() >= targetAvailablePages && canResume) {
				// We have enough to pause.
				break;
			}

			item = item->nextItem;
			pmm.balanceResumePosition = 0;
		}

		if (item) {
			// Continue with this region next time.
			pmm.nextRegionToBalance = item->thisItem;
			pmm.nextProcessToBalance = process;
			// EsPrint("will resume at %x\n", pmm.balanceResumePosition);
		} else {
			// Go to the next process.
			pmm.nextRegionToBalance = nullptr;
			pmm.balanceResumePosition = 0;
		}

		KMutexRelease(&space->reserveMutex);
		GetCurrentThread()->SetAddressSpace(nullptr);
		CloseHandleToObject(process, KERNEL_OBJECT_PROCESS);
	}
}

void MMZeroPageThread() {
	while (true) {
		KEventWait(&pmm.zeroPageEvent);
		KEventWait(&pmm.availableNotCritical);

		bool done = false;

		while (!done) {
			uintptr_t pages[PHYSICAL_MEMORY_MANIPULATION_REGION_PAGES]; 
			int i = 0;

			{
				KMutexAcquire(&pmm.pageFrameMutex);
				EsDefer(KMutexRelease(&pmm.pageFrameMutex));

				for (; i < PHYSICAL_MEMORY_MANIPULATION_REGION_PAGES; i++) {
					if (pmm.firstFreePage) {
						pages[i] = pmm.firstFreePage;
						MMPhysicalActivatePages(pages[i], 1, ES_FLAGS_DEFAULT);
					} else {
						done = true;
						break;
					}

					MMPageFrame *frame = pmm.pageFrames + pages[i];
					frame->state = MMPageFrame::ACTIVE;
					pmm.freeOrZeroedPageBitset.Take(pages[i]);
				}
			}

			for (int j = 0; j < i; j++) pages[j] <<= K_PAGE_BITS;
			if (i) PMZero(pages, i, false);

			{
				KMutexAcquire(&pmm.pageFrameMutex);
				pmm.countActivePages -= i;
				while (i--) MMPhysicalInsertZeroedPage(pages[i] >> K_PAGE_BITS);
				KMutexRelease(&pmm.pageFrameMutex);
			}
		}
	}
}

#if 0
void PMCheckZeroed(uintptr_t page) {
	pmm.pmManipulationLock.Acquire();

	{
		MMSpace *vas = coreMMSpace;
		void *region = pmm.pmManipulationRegion;

		vas->data.mapMutex.Acquire();
		MMArchMapPage(vas, page, (uintptr_t) region, MM_MAP_PAGE_OVERWRITE);
		vas->data.mapMutex.Release();

		pmm.pmManipulationProcessorLock.Acquire();
		ProcessorInvalidatePage((uintptr_t) region);

		for (uintptr_t i = 0; i < K_PAGE_SIZE; i++) {
			if (((uint8_t *) region)[i]) {
				KernelPanic("PMCheckZeroed - Supposedly zeroed page %x was not zeroed.\n", page);
			}
		}

		pmm.pmManipulationProcessorLock.Release();
	}

	pmm.pmManipulationLock.Release();
}
#endif

void PMZeroPartial(uintptr_t page, uintptr_t start, uintptr_t end) {
	KMutexAcquire(&pmm.pmManipulationLock);
	MMSpace *vas = coreMMSpace;
	void *region = pmm.pmManipulationRegion;
	MMArchMapPage(vas, page, (uintptr_t) region, MM_MAP_PAGE_OVERWRITE | MM_MAP_PAGE_NO_NEW_TABLES);
	KSpinlockAcquire(&pmm.pmManipulationProcessorLock);
	ProcessorInvalidatePage((uintptr_t) region);
	EsMemoryZero((uint8_t *) region + start, end - start);
	KSpinlockRelease(&pmm.pmManipulationProcessorLock);
	KMutexRelease(&pmm.pmManipulationLock);
}

void PMZero(uintptr_t *pages, size_t pageCount, bool contiguous) {
	KMutexAcquire(&pmm.pmManipulationLock);

	repeat:;
	size_t doCount = pageCount > PHYSICAL_MEMORY_MANIPULATION_REGION_PAGES ? PHYSICAL_MEMORY_MANIPULATION_REGION_PAGES : pageCount;
	pageCount -= doCount;

	{
		MMSpace *vas = coreMMSpace;
		void *region = pmm.pmManipulationRegion;

		for (uintptr_t i = 0; i < doCount; i++) {
			MMArchMapPage(vas, contiguous ? pages[0] + (i << K_PAGE_BITS) : pages[i], 
					(uintptr_t) region + K_PAGE_SIZE * i, MM_MAP_PAGE_OVERWRITE | MM_MAP_PAGE_NO_NEW_TABLES);
		}

		KSpinlockAcquire(&pmm.pmManipulationProcessorLock);

		for (uintptr_t i = 0; i < doCount; i++) {
			ProcessorInvalidatePage((uintptr_t) region + i * K_PAGE_SIZE);
		}

		EsMemoryZero(region, doCount * K_PAGE_SIZE);

		KSpinlockRelease(&pmm.pmManipulationProcessorLock);
	}

	if (pageCount) {
		if (!contiguous) pages += PHYSICAL_MEMORY_MANIPULATION_REGION_PAGES;
		goto repeat;
	}

	// if (pageNumbers) EsPrint("w%d\n", pmm.pmManipulationLock.blockedThreads.count);
	KMutexRelease(&pmm.pmManipulationLock);
}

void PMCopy(uintptr_t page, void *_source, size_t pageCount) {
	uint8_t *source = (uint8_t *) _source;
	KMutexAcquire(&pmm.pmManipulationLock);

	repeat:;
	size_t doCount = pageCount > PHYSICAL_MEMORY_MANIPULATION_REGION_PAGES ? PHYSICAL_MEMORY_MANIPULATION_REGION_PAGES : pageCount;
	pageCount -= doCount;

	{
		MMSpace *vas = coreMMSpace;
		void *region = pmm.pmManipulationRegion;

		for (uintptr_t i = 0; i < doCount; i++) {
			MMArchMapPage(vas, page + K_PAGE_SIZE * i, (uintptr_t) region + K_PAGE_SIZE * i, MM_MAP_PAGE_OVERWRITE | MM_MAP_PAGE_NO_NEW_TABLES);
		}

		KSpinlockAcquire(&pmm.pmManipulationProcessorLock);

		for (uintptr_t i = 0; i < doCount; i++) {
			ProcessorInvalidatePage((uintptr_t) region + i * K_PAGE_SIZE);
		}

		EsMemoryCopy(region, source, doCount * K_PAGE_SIZE);

		KSpinlockRelease(&pmm.pmManipulationProcessorLock);
	}

	if (pageCount) {
		page += PHYSICAL_MEMORY_MANIPULATION_REGION_PAGES * K_PAGE_SIZE;
		source += PHYSICAL_MEMORY_MANIPULATION_REGION_PAGES * K_PAGE_SIZE;
		goto repeat;
	}

	KMutexRelease(&pmm.pmManipulationLock);
}

void PMRead(uintptr_t page, void *_source, size_t pageCount) {
	uint8_t *source = (uint8_t *) _source;
	KMutexAcquire(&pmm.pmManipulationLock);

	repeat:;
	size_t doCount = pageCount > PHYSICAL_MEMORY_MANIPULATION_REGION_PAGES ? PHYSICAL_MEMORY_MANIPULATION_REGION_PAGES : pageCount;
	pageCount -= doCount;

	{
		MMSpace *vas = coreMMSpace;
		void *region = pmm.pmManipulationRegion;

		for (uintptr_t i = 0; i < doCount; i++) {
			MMArchMapPage(vas, page + K_PAGE_SIZE * i, (uintptr_t) region + K_PAGE_SIZE * i, MM_MAP_PAGE_OVERWRITE | MM_MAP_PAGE_NO_NEW_TABLES);
		}

		KSpinlockAcquire(&pmm.pmManipulationProcessorLock);

		for (uintptr_t i = 0; i < doCount; i++) {
			ProcessorInvalidatePage((uintptr_t) region + i * K_PAGE_SIZE);
		}

		EsMemoryCopy(source, region, doCount * K_PAGE_SIZE);

		KSpinlockRelease(&pmm.pmManipulationProcessorLock);
	}

	if (pageCount) {
		page += PHYSICAL_MEMORY_MANIPULATION_REGION_PAGES * K_PAGE_SIZE;
		source += PHYSICAL_MEMORY_MANIPULATION_REGION_PAGES * K_PAGE_SIZE;
		goto repeat;
	}

	KMutexRelease(&pmm.pmManipulationLock);
}

void *Pool::Add(size_t _elementSize) {
	KMutexAcquire(&mutex);
	EsDefer(KMutexRelease(&mutex));

	if (elementSize && _elementSize != elementSize) KernelPanic("Pool::Add - Pool element size mismatch.\n");
	elementSize = _elementSize;

	void *address;

#if 1
	if (cacheEntries) {
		address = cache[--cacheEntries];
		EsMemoryZero(address, elementSize);
	} else {
		address = EsHeapAllocate(elementSize, true, K_FIXED);
	}
#else
	address = EsHeapAllocate(elementSize, true);
#endif

	return address;
}

void Pool::Remove(void *address) {
	KMutexAcquire(&mutex);
	EsDefer(KMutexRelease(&mutex));

	if (!address) return;

#if 1
	if (cacheEntries == POOL_CACHE_COUNT) {
		EsHeapFree(address, elementSize, K_FIXED);
	} else {
		cache[cacheEntries++] = address;
	}
#else
	EsHeapFree(address, elementSize);
#endif
}

MMRegion *MMFindAndPinRegion(MMSpace *space, uintptr_t address, uintptr_t size) {
	if (address + size < address) {
		return nullptr;
	}

	KMutexAcquire(&space->reserveMutex);
	EsDefer(KMutexRelease(&space->reserveMutex));

	MMRegion *region = MMFindRegion(space, address);

	if (!region) {
		return nullptr;
	}

	if (region->baseAddress > address) {
		return nullptr;
	}

	if (region->baseAddress + region->pageCount * K_PAGE_SIZE < address + size) {
		return nullptr;
	}

	if (!KWriterLockTake(&region->data.pin, K_LOCK_SHARED, true /* poll */)) {
		return nullptr;
	}

	return region;
}

void MMUnpinRegion(MMSpace *space, MMRegion *region) {
	KMutexAcquire(&space->reserveMutex);
	KWriterLockReturn(&region->data.pin, K_LOCK_SHARED);
	KMutexRelease(&space->reserveMutex);
}

bool MMPhysicalAllocateAndMap(size_t sizeBytes, size_t alignmentBytes, size_t maximumBits, bool zeroed, 
		uint64_t caching, uint8_t **_virtualAddress, uintptr_t *_physicalAddress) {
	if (!sizeBytes) sizeBytes = 1;
	if (!alignmentBytes) alignmentBytes = 1;

	bool noBelow = false;

#ifdef ARCH_32
	if (!maximumBits || maximumBits >= 32) noBelow = true;
#endif

#ifdef ARCH_64
	if (!maximumBits || maximumBits >= 64) noBelow = true;
#endif

	uintptr_t sizePages = (sizeBytes + K_PAGE_SIZE - 1) >> K_PAGE_BITS;

	uintptr_t physicalAddress = MMPhysicalAllocate(MM_PHYSICAL_ALLOCATE_CAN_FAIL | MM_PHYSICAL_ALLOCATE_COMMIT_NOW, 
			sizePages, (alignmentBytes + K_PAGE_SIZE - 1) >> K_PAGE_BITS, 
			noBelow ? 0 : ((size_t) 1 << maximumBits));

	if (!physicalAddress) {
		return false;
	}

	void *virtualAddress = MMMapPhysical(kernelMMSpace, physicalAddress, sizeBytes, caching);

	if (!virtualAddress) {
		MMPhysicalFree(physicalAddress, false, sizePages);
		return false;
	}

	if (zeroed) {
		EsMemoryZero(virtualAddress, sizeBytes);
	}

	*_virtualAddress = (uint8_t *) virtualAddress;
	*_physicalAddress = physicalAddress;
	return true;
}

void MMPhysicalFreeAndUnmap(void *virtualAddress, uintptr_t physicalAddress) {
	KMutexAcquire(&kernelMMSpace->reserveMutex);
	MMRegion *region = MMFindRegion(kernelMMSpace, (uintptr_t) virtualAddress);

	if (!region || (~region->flags & MM_REGION_PHYSICAL) || region->data.physical.offset != physicalAddress) {
		KernelPanic("MMPhysicalFreeAndUnmap - Virtual address %x did not point to a region of physical memory mapping %x.\n", virtualAddress, physicalAddress);
	}

	size_t pageCount = region->pageCount;
	MMUnreserve(kernelMMSpace, region, true);
	KMutexRelease(&kernelMMSpace->reserveMutex);

	MMPhysicalFree(physicalAddress, false, pageCount);
}

MMSpace *MMGetKernelSpace() {
	return kernelMMSpace;
}

MMSpace *MMGetCurrentProcessSpace() {
	return GetCurrentThread()->process->vmm;
}

void MMArchRemap(MMSpace *space, const void *virtualAddress, uintptr_t newPhysicalAddress) {
	if (ProcessorAreInterruptsEnabled()) {
		KernelPanic("MMArchRemap - Cannot remap address with interrupts enabled (does not invalidate the page on other processors).\n");
	}

	MMArchMapPage(space, newPhysicalAddress, (uintptr_t) virtualAddress, MM_MAP_PAGE_OVERWRITE | MM_MAP_PAGE_NO_NEW_TABLES);
}

void MMObjectCacheInsert(MMObjectCache *cache, MMObjectCacheItem *item) {
	KSpinlockAcquire(&cache->lock);
	cache->items.Insert(item, false /* end */);
	cache->count++;
	__sync_fetch_and_add(&pmm.approximateTotalObjectCacheBytes, cache->averageObjectBytes);

	if (MM_OBJECT_CACHE_SHOULD_TRIM()) {
		KEventSet(&pmm.trimObjectCaches, false, true);
	}

	KSpinlockRelease(&cache->lock);
}

void MMObjectCacheRemove(MMObjectCache *cache, MMObjectCacheItem *item, bool alreadyLocked) {
	if (!alreadyLocked) KSpinlockAcquire(&cache->lock);
	else KSpinlockAssertLocked(&cache->lock);
	item->Remove();
	cache->count--;
	__sync_fetch_and_sub(&pmm.approximateTotalObjectCacheBytes, cache->averageObjectBytes);

	if (!alreadyLocked) KSpinlockRelease(&cache->lock);
}

MMObjectCacheItem *MMObjectCacheRemoveLRU(MMObjectCache *cache) {
	MMObjectCacheItem *item = nullptr;
	KSpinlockAcquire(&cache->lock);

	if (cache->count) {
		item = cache->items.first;
		MMObjectCacheRemove(cache, item, true);
	}

	KSpinlockRelease(&cache->lock);
	return item;
}

void MMObjectCacheRegister(MMObjectCache *cache, bool (*trim)(MMObjectCache *), size_t averageObjectBytes) {
	KMutexAcquire(&pmm.objectCacheListMutex);
	cache->trim = trim;
	cache->averageObjectBytes = averageObjectBytes;
	cache->item.thisItem = cache;
	pmm.objectCacheList.InsertEnd(&cache->item);
	KMutexRelease(&pmm.objectCacheListMutex);
}

void MMObjectCacheUnregister(MMObjectCache *cache) {
	KMutexAcquire(&pmm.objectCacheListMutex);
	pmm.objectCacheList.Remove(&cache->item);
	KMutexRelease(&pmm.objectCacheListMutex);

	// Wait for any trim threads still using the cache to finish.
	KWriterLockTake(&cache->trimLock, K_LOCK_EXCLUSIVE);
	KWriterLockReturn(&cache->trimLock, K_LOCK_EXCLUSIVE);
}

void MMObjectCacheFlush(MMObjectCache *cache) {
	if (cache->item.list) KernelPanic("MMObjectCacheFlush - Cache %x must be unregistered before flushing.\n", cache);

	// Wait for any trim threads still using the cache to finish.
	KWriterLockTake(&cache->trimLock, K_LOCK_EXCLUSIVE);

	// Trim the cache until it is empty.
	// The trim callback is allowed to increase cache->count,
	// but nobody else should be increasing it once it has been unregistered.
	while (cache->count) cache->trim(cache);

	// Return the trim lock.
	KWriterLockReturn(&cache->trimLock, K_LOCK_EXCLUSIVE);
}

void MMObjectCacheTrimThread() {
	MMObjectCache *cache = nullptr;

	while (true) {
		while (!MM_OBJECT_CACHE_SHOULD_TRIM()) {
			KEventReset(&pmm.trimObjectCaches);
			KEventWait(&pmm.trimObjectCaches);
		}

		KMutexAcquire(&pmm.objectCacheListMutex);

		// TODO Is there a faster way to find the next cache?
		// This needs to work with multiple producers and consumers.
		// And I don't want to put the caches in an array, because then registering a cache could fail.

		MMObjectCache *previousCache = cache;
		cache = nullptr;

		LinkedItem<MMObjectCache> *item = pmm.objectCacheList.firstItem;

		while (item) {
			if (item->thisItem == previousCache && item->nextItem) {
				cache = item->nextItem->thisItem;
				break;
			}

			item = item->nextItem;
		}

		if (!cache && pmm.objectCacheList.firstItem) {
			cache = pmm.objectCacheList.firstItem->thisItem;
		}

		if (cache) {
			KWriterLockTake(&cache->trimLock, K_LOCK_SHARED);
		}
		
		KMutexRelease(&pmm.objectCacheListMutex);

		if (!cache) {
			continue;
		}

		for (uintptr_t i = 0; i < MM_OBJECT_CACHE_TRIM_GROUP_COUNT; i++) {
			if (!cache->trim(cache)) {
				break;
			}
		}

		KWriterLockReturn(&cache->trimLock, K_LOCK_SHARED);
	}
}

void MMInitialise() {
	{
		// Initialise coreMMSpace.

		MMArchInitialiseVAS();
		mmCoreRegions[0].baseAddress = MM_CORE_SPACE_START;
		mmCoreRegions[0].pageCount = MM_CORE_SPACE_SIZE / K_PAGE_SIZE;
		mmCoreRegions[0].core.used = false;
		mmCoreRegionCount = 1;
	}

	{
		// Initialise kernelMMSpace.

		KMutexAcquire(&coreMMSpace->reserveMutex);
		kernelMMSpace->data.l1Commit = (uint8_t *) MMReserve(coreMMSpace, L1_COMMIT_SIZE_BYTES, MM_REGION_NORMAL | MM_REGION_NO_COMMIT_TRACKING | MM_REGION_FIXED)->baseAddress;
		KMutexRelease(&coreMMSpace->reserveMutex);

		MMRegion *region = (MMRegion *) EsHeapAllocate(sizeof(MMRegion), true, K_CORE);
		region->baseAddress = MM_KERNEL_SPACE_START; 
		region->pageCount = MM_KERNEL_SPACE_SIZE / K_PAGE_SIZE;
		TreeInsert(&kernelMMSpace->freeRegionsBase, &region->itemBase, region, MakeShortKey(region->baseAddress));
		TreeInsert(&kernelMMSpace->freeRegionsSize, &region->itemSize, region, MakeShortKey(region->pageCount), AVL_DUPLICATE_KEYS_ALLOW);
	}

	{
		// Initialise physical memory management.

		KMutexAcquire(&kernelMMSpace->reserveMutex);
		pmm.pmManipulationRegion = (void *) MMReserve(kernelMMSpace, PHYSICAL_MEMORY_MANIPULATION_REGION_PAGES * K_PAGE_SIZE, ES_FLAGS_DEFAULT)->baseAddress; 
		KMutexRelease(&kernelMMSpace->reserveMutex);

		physicalMemoryHighest += K_PAGE_SIZE << 3;
		pmm.pageFrames = (MMPageFrame *) MMStandardAllocate(kernelMMSpace, (physicalMemoryHighest >> K_PAGE_BITS) * sizeof(MMPageFrame), MM_REGION_FIXED);
		pmm.freeOrZeroedPageBitset.Initialise(physicalMemoryHighest >> K_PAGE_BITS, true);

		uint64_t commitLimit = 0;

		while (physicalMemoryRegionsPagesCount) {
			// TODO This loop is a bit slow...
			MMPhysicalInsertFreePage(MMPhysicalAllocate(ES_FLAGS_DEFAULT) >> K_PAGE_BITS);
			commitLimit++;
		}

		pmm.commitLimit = pmm.commitFixedLimit = commitLimit;
		KernelLog(LOG_INFO, "Memory", "pmm initialised", "MMInitialise - PMM initialised with a fixed commit limit of %d pages.\n", pmm.commitLimit);
	}

	{
		// Initialise file cache.

		CCInitialise();
	}

	{
		// Thread initialisation.

		pmm.zeroPageEvent.autoReset = true;
		MMCommit(PHYSICAL_MEMORY_MANIPULATION_REGION_PAGES * K_PAGE_SIZE, true);
		pmm.zeroPageThread = scheduler.SpawnThread("MMZero", (uintptr_t) MMZeroPageThread, 0, SPAWN_THREAD_LOW_PRIORITY);
		pmm.balanceThread = scheduler.SpawnThread("MMBalance", (uintptr_t) MMBalanceThread, 0, ES_FLAGS_DEFAULT);
		pmm.balanceThread->isPageGenerator = true;
		scheduler.SpawnThread("MMObjTrim", (uintptr_t) MMObjectCacheTrimThread, 0, ES_FLAGS_DEFAULT);
	}
}

#endif
