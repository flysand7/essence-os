// TODO Rewrite. Make faster!
// TODO EsHeapAllocateNearby.
// TODO Larger heap blocks.

#ifdef DEBUG_BUILD
#define MAYBE_VALIDATE_HEAP() HeapValidate(&heap)
#else
#define MAYBE_VALIDATE_HEAP() 
#endif

#ifndef KERNEL
// #define MEMORY_LEAK_DETECTOR
#endif

#define LARGE_ALLOCATION_THRESHOLD (32768)
#define USED_HEAP_REGION_MAGIC (0xABCD)

struct HeapRegion {
	union {
		uint16_t next;
		uint16_t size;
	};

	uint16_t previous;
	uint16_t offset;
	uint16_t used;

	union {
		uintptr_t allocationSize;

		// Valid if the region is not in use.
		HeapRegion *regionListNext;
	};

	// Free regions only:
	HeapRegion **regionListReference;
#define USED_HEAP_REGION_HEADER_SIZE (sizeof(HeapRegion) - sizeof(HeapRegion **))
#define FREE_HEAP_REGION_HEADER_SIZE (sizeof(HeapRegion))
};

static uintptr_t HeapCalculateIndex(uintptr_t size) {
	int x = __builtin_clz(size);
	uintptr_t msb = sizeof(unsigned int) * 8 - x - 1;
	return msb - 4;
}

#ifdef MEMORY_LEAK_DETECTOR
extern "C" uint64_t ProcessorRBPRead();

struct MemoryLeakDetectorEntry {
	void *address;
	size_t bytes;
	uintptr_t stack[8];
	size_t seenCount;
};
#else
#define MemoryLeakDetectorAdd(...)
#define MemoryLeakDetectorRemove(...)
#define MemoryLeakDetectorCheckpoint(...)
#endif

struct EsHeap {
#ifdef KERNEL
	KMutex mutex;
#else
	EsMutex mutex;
#endif

	HeapRegion *regions[12];
	volatile size_t allocationsCount, size, blockCount;
	void *blocks[16];

	bool cannotValidate;

#ifdef MEMORY_LEAK_DETECTOR
	MemoryLeakDetectorEntry leakDetectorEntries[4096];
#endif
};

// TODO Better heap panic messages.
#define HEAP_PANIC(n, x, y) EsPanic("Heap panic (%d/%x/%x).\n", n, x, y)

#ifdef KERNEL
EsHeap heapCore, heapFixed;
#define HEAP_ACQUIRE_MUTEX(a) KMutexAcquire(&(a))
#define HEAP_RELEASE_MUTEX(a) KMutexRelease(&(a))
#define HEAP_ALLOCATE_CALL(x) MMStandardAllocate(_heap == &heapCore ? coreMMSpace : kernelMMSpace, x, MM_REGION_FIXED)
#define HEAP_FREE_CALL(x) MMFree(_heap == &heapCore ? coreMMSpace : kernelMMSpace, x)
#else
EsHeap heap;
#define HEAP_ACQUIRE_MUTEX(a) EsMutexAcquire(&(a))
#define HEAP_RELEASE_MUTEX(a) EsMutexRelease(&(a))
#define HEAP_ALLOCATE_CALL(x) EsMemoryReserve(x)
#define HEAP_FREE_CALL(x) EsMemoryUnreserve(x)
#endif

#define HEAP_REGION_HEADER(region) ((HeapRegion *) ((uint8_t *) region - USED_HEAP_REGION_HEADER_SIZE))
#define HEAP_REGION_DATA(region) ((uint8_t *) region + USED_HEAP_REGION_HEADER_SIZE)
#define HEAP_REGION_NEXT(region) ((HeapRegion *) ((uint8_t *) region + region->next))
#define HEAP_REGION_PREVIOUS(region) (region->previous ? ((HeapRegion *) ((uint8_t *) region - region->previous)) : nullptr)

#ifdef USE_PLATFORM_HEAP
void *PlatformHeapAllocate(size_t size, bool zero);
void PlatformHeapFree(void *address);
void *PlatformHeapReallocate(void *oldAddress, size_t newAllocationSize, bool zeroNewSpace);
#endif

#ifdef MEMORY_LEAK_DETECTOR
static void MemoryLeakDetectorAdd(EsHeap *heap, void *address, size_t bytes) {
	if (!address || !bytes) {
		return;
	}

	for (uintptr_t i = 0; i < sizeof(heap->leakDetectorEntries) / sizeof(heap->leakDetectorEntries[0]); i++) {
		MemoryLeakDetectorEntry *entry = &heap->leakDetectorEntries[i];

		if (entry->address) {
			continue;
		}

		entry->address = address;
		entry->bytes = bytes;
		entry->seenCount = 0;

		uint64_t rbp = ProcessorRBPRead();
		uintptr_t traceDepth = 0;

		while (rbp && traceDepth < sizeof(entry->stack) / sizeof(entry->stack[0])) {
			uint64_t value = *(uint64_t *) (rbp + 8);
			entry->stack[traceDepth++] = value;
			if (!value) break;
			rbp = *(uint64_t *) rbp;
		}

		break;
	}
}

static void MemoryLeakDetectorRemove(EsHeap *heap, void *address) {
	if (!address) {
		return;
	}

	for (uintptr_t i = 0; i < sizeof(heap->leakDetectorEntries) / sizeof(heap->leakDetectorEntries[0]); i++) {
		if (heap->leakDetectorEntries[i].address == address) {
			heap->leakDetectorEntries[i].address = nullptr;
			break;
		}
	}
}

static void MemoryLeakDetectorCheckpoint(EsHeap *heap) {
	EsPrint("--- MemoryLeakDetectorCheckpoint ---\n");

	for (uintptr_t i = 0; i < sizeof(heap->leakDetectorEntries) / sizeof(heap->leakDetectorEntries[0]); i++) {
		MemoryLeakDetectorEntry *entry = &heap->leakDetectorEntries[i];
		if (!entry->address) continue;
		entry->seenCount++;
		EsPrint("  %d %d %x %d\n", i, entry->seenCount, entry->address, entry->bytes);
	}
}
#endif

static void HeapRemoveFreeRegion(HeapRegion *region) {
	if (!region->regionListReference || region->used) {
		HEAP_PANIC(50, region, 0);
	}

	*region->regionListReference = region->regionListNext;

	if (region->regionListNext) {
		region->regionListNext->regionListReference = region->regionListReference;
	}

	region->regionListReference = nullptr;
}

static void HeapAddFreeRegion(HeapRegion *region, HeapRegion **heapRegions) {
	if (region->used || region->size < 32) {
		HEAP_PANIC(1, region, heapRegions);
	}

	int index = HeapCalculateIndex(region->size);
	region->regionListNext = heapRegions[index];
	if (region->regionListNext) region->regionListNext->regionListReference = &region->regionListNext;
	heapRegions[index] = region;
	region->regionListReference = heapRegions + index;
}

static void HeapValidate(EsHeap *heap) {
	if (heap->cannotValidate) return;

	for (uintptr_t i = 0; i < heap->blockCount; i++) {
		HeapRegion *start = (HeapRegion *) heap->blocks[i];
		if (!start) continue;

		HeapRegion *end = (HeapRegion *) ((uint8_t *) heap->blocks[i] + 65536);
		HeapRegion *previous = nullptr;
		HeapRegion *region = start;

		while (region < end) {
			if (previous && previous != HEAP_REGION_PREVIOUS(region)) {
				HEAP_PANIC(21, previous, region);
			}

			if (!previous && region->previous) {
				HEAP_PANIC(23, previous, region);
			}

			if (region->size & 31) {
				HEAP_PANIC(51, region, start);
			}

			if ((char *) region - (char *) start != region->offset) {
				HEAP_PANIC(22, region, start);
			}

			if (region->used != USED_HEAP_REGION_MAGIC && region->used != 0x0000) {
				HEAP_PANIC(24, region, region->used);
			}

			if (region->used == 0x0000 && !region->regionListReference) {
				HEAP_PANIC(25, region, region->regionListReference);
			}

			if (region->used == 0x0000 && region->regionListNext && region->regionListNext->regionListReference != &region->regionListNext) {
				HEAP_PANIC(26, region->regionListNext, region);
			}
				
			previous = region;
			region = HEAP_REGION_NEXT(region);
		}

		if (region != end) {
			HEAP_PANIC(20, region, end);
		}
	}
}

static void HeapPrintAllocatedRegions(EsHeap *heap) {
	EsPrint("--- Heap (%d allocations, %d bytes, %d blocks) ---\n", heap->allocationsCount, heap->size, heap->blockCount);
	HeapValidate(heap);
	if (heap->cannotValidate) return;

	for (uintptr_t i = 0; i < heap->blockCount; i++) {
		HeapRegion *start = (HeapRegion *) heap->blocks[i];
		if (!start) continue;

		HeapRegion *end = (HeapRegion *) ((uint8_t *) heap->blocks[i] + 65536);
		HeapRegion *region = start;

		while (region < end) {
			if (region->used == USED_HEAP_REGION_MAGIC) {
				EsPrint("%x %d\n", HEAP_REGION_DATA(region), region->size);
			}

			region = HEAP_REGION_NEXT(region);
		}
	}

	MemoryLeakDetectorCheckpoint(heap);
}

void *EsHeapAllocate(size_t size, bool zeroMemory, EsHeap *_heap) {
#ifndef KERNEL
	if (!_heap) _heap = &heap;
#endif
	EsHeap &heap = *(EsHeap *) _heap;
	if (!size) return nullptr;

#ifdef USE_PLATFORM_HEAP
	return PlatformHeapAllocate(size, zeroMemory);
#endif

	size_t largeAllocationThreshold = LARGE_ALLOCATION_THRESHOLD;

#ifndef KERNEL
	// EsPrint("Allocate: %d\n", size);
#else
	// EsPrint("%z: %d\n", mmvmm ? "CORE" : "KERN", size);
#endif

	size_t originalSize = size;

	if ((ptrdiff_t) size < 0) {
		HEAP_PANIC(0, 0, 0);
	}

	size += USED_HEAP_REGION_HEADER_SIZE; // Region metadata.
	size = (size + 0x1F) & ~0x1F; // Allocation granularity: 32 bytes.

	if (size >= largeAllocationThreshold) {
		// This is a very large allocation, so allocate it by itself.
		// We don't need to zero this memory. (It'll be done by the PMM).
		HeapRegion *region = (HeapRegion *) HEAP_ALLOCATE_CALL(size);
		if (!region) return nullptr; 
		region->used = USED_HEAP_REGION_MAGIC;
		region->size = 0;
		region->allocationSize = originalSize;
		__sync_fetch_and_add(&heap.size, originalSize);
		MemoryLeakDetectorAdd(&heap, HEAP_REGION_DATA(region), originalSize);
		return HEAP_REGION_DATA(region);
	}

	HEAP_ACQUIRE_MUTEX(heap.mutex);

	MAYBE_VALIDATE_HEAP();

	HeapRegion *region = nullptr;

	for (int i = HeapCalculateIndex(size); i < 12; i++) {
		if (heap.regions[i] == nullptr || heap.regions[i]->size < size) {
			continue;
		}

		region = heap.regions[i];
		HeapRemoveFreeRegion(region);
		goto foundRegion;
	}

	region = (HeapRegion *) HEAP_ALLOCATE_CALL(65536);
	if (heap.blockCount < 16) heap.blocks[heap.blockCount] = region;
	else heap.cannotValidate = true;
	heap.blockCount++;
	if (!region) {
		HEAP_RELEASE_MUTEX(heap.mutex);
		return nullptr; 
	}
	region->size = 65536 - 32;

	// Prevent EsHeapFree trying to merge off the end of the block.
	{
		HeapRegion *endRegion = HEAP_REGION_NEXT(region);
		endRegion->used = USED_HEAP_REGION_MAGIC;
		endRegion->offset = 65536 - 32;
		endRegion->next = 32;
		*((EsHeap **) HEAP_REGION_DATA(endRegion)) = &heap;
	}

	foundRegion:

	if (region->used || region->size < size) {
		HEAP_PANIC(4, region, size);
	}

	heap.allocationsCount++;
	__sync_fetch_and_add(&heap.size, size);

	if (region->size == size) {
		// If the size of this region is equal to the size of the region we're trying to allocate,
		// return this region immediately.
		region->used = USED_HEAP_REGION_MAGIC;
		region->allocationSize = originalSize;
		HEAP_RELEASE_MUTEX(heap.mutex);
		uint8_t *address = (uint8_t *) HEAP_REGION_DATA(region);
		if (zeroMemory) EsMemoryZero(address, originalSize);
#ifdef DEBUG_BUILD
		else EsMemoryFill(address, (uint8_t *) address + originalSize, 0xA1);
#endif
		MemoryLeakDetectorAdd(&heap, address, originalSize);
		return address;
	}

	// Split the region into 2 parts.
	
	HeapRegion *allocatedRegion = region;
	size_t oldSize = allocatedRegion->size;
	allocatedRegion->size = size;
	allocatedRegion->used = USED_HEAP_REGION_MAGIC;

	HeapRegion *freeRegion = HEAP_REGION_NEXT(allocatedRegion);
	freeRegion->size = oldSize - size;
	freeRegion->previous = size;
	freeRegion->offset = allocatedRegion->offset + size;
	freeRegion->used = false;
	HeapAddFreeRegion(freeRegion, heap.regions);

	HeapRegion *nextRegion = HEAP_REGION_NEXT(freeRegion);
	nextRegion->previous = freeRegion->size;

	MAYBE_VALIDATE_HEAP();

	region->allocationSize = originalSize;

	HEAP_RELEASE_MUTEX(heap.mutex);

	void *address = HEAP_REGION_DATA(region);

	if (zeroMemory) EsMemoryZero(address, originalSize);
#ifdef DEBUG_BUILD
	else EsMemoryFill(address, (uint8_t *) address + originalSize, 0xA1);
#endif

	MemoryLeakDetectorAdd(&heap, address, originalSize);
	return address;
}

void EsHeapFree(void *address, size_t expectedSize, EsHeap *_heap) {
#ifndef KERNEL
	if (!_heap) _heap = &heap;
#endif
	EsHeap &heap = *(EsHeap *) _heap;

	if (!address && expectedSize) HEAP_PANIC(10, address, expectedSize);
	if (!address) return;

#ifdef USE_PLATFORM_HEAP
	PlatformHeapFree(address);
	return;
#endif

	MemoryLeakDetectorRemove(&heap, address);

	HeapRegion *region = HEAP_REGION_HEADER(address);
	if (region->used != USED_HEAP_REGION_MAGIC) HEAP_PANIC(region->used, region, nullptr);
	if (expectedSize && region->allocationSize != expectedSize) HEAP_PANIC(6, region, expectedSize);

	if (!region->size) {
		// The region was allocated by itself.
		__sync_fetch_and_sub(&heap.size, region->allocationSize);
		HEAP_FREE_CALL(region);
		return;
	}

#ifdef DEBUG_BUILD
	EsMemoryFill(address, (uint8_t *) address + region->allocationSize, 0xB1);
#endif

	// Check this is the correct heap.

	if (*(EsHeap **) HEAP_REGION_DATA((uint8_t *) region - region->offset + 65536 - 32) != &heap) {
		HEAP_PANIC(52, address, 0);
	}

	HEAP_ACQUIRE_MUTEX(heap.mutex);

	MAYBE_VALIDATE_HEAP();

	region->used = false;

	if (region->offset < region->previous) {
		HEAP_PANIC(31, address, 0);
	}

	heap.allocationsCount--;
	__sync_fetch_and_sub(&heap.size, region->size);

	// Attempt to merge with the next region.

	HeapRegion *nextRegion = HEAP_REGION_NEXT(region);

	if (nextRegion && !nextRegion->used) {
		HeapRemoveFreeRegion(nextRegion);

		// Merge the regions.
		region->size += nextRegion->size;
		HEAP_REGION_NEXT(nextRegion)->previous = region->size;
	}

	// Attempt to merge with the previous region.

	HeapRegion *previousRegion = HEAP_REGION_PREVIOUS(region);

	if (previousRegion && !previousRegion->used) {
		HeapRemoveFreeRegion(previousRegion);

		// Merge the regions.
		previousRegion->size += region->size;
		HEAP_REGION_NEXT(region)->previous = previousRegion->size;
		region = previousRegion;
	}

	if (region->size == 65536 - 32) {
		if (region->offset) HEAP_PANIC(7, region, region->offset);

		// The memory block is empty.
		heap.blockCount--;

		if (!heap.cannotValidate) {
			bool found = false;

			for (uintptr_t i = 0; i <= heap.blockCount; i++) {
				if (heap.blocks[i] == region) {
					heap.blocks[i] = heap.blocks[heap.blockCount];
					found = true;
					break;
				}
			}

			EsAssert(found);
		}

		HEAP_FREE_CALL(region);
		HEAP_RELEASE_MUTEX(heap.mutex);
		return;
	}

	// Put the free region in the region list.
	HeapAddFreeRegion(region, heap.regions);

	MAYBE_VALIDATE_HEAP();

	HEAP_RELEASE_MUTEX(heap.mutex);
}

void *EsHeapReallocate(void *oldAddress, size_t newAllocationSize, bool zeroNewSpace, EsHeap *_heap) {
#ifndef KERNEL
	if (!_heap) _heap = &heap;
#endif
	EsHeap &heap = *(EsHeap *) _heap;

	/*
		Test with:
			void *a = EsHeapReallocate(nullptr, 128, true);
			a = EsHeapReallocate(a, 256, true);
			a = EsHeapReallocate(a, 128, true);
			a = EsHeapReallocate(a, 65536, true);
			a = EsHeapReallocate(a, 128, true);
			a = EsHeapReallocate(a, 128, true);
			void *b = EsHeapReallocate(nullptr, 64, true);
			void *c = EsHeapReallocate(nullptr, 64, true);
			EsHeapReallocate(b, 0, true);
			a = EsHeapReallocate(a, 128 + 88, true);
			a = EsHeapReallocate(a, 128, true);
			EsHeapReallocate(a, 0, true);
			EsHeapReallocate(c, 0, true);
	*/

	if (!oldAddress) {
		return EsHeapAllocate(newAllocationSize, zeroNewSpace, _heap);
	} else if (!newAllocationSize) {
		EsHeapFree(oldAddress, 0, _heap);
		return nullptr;
	}

#ifdef USE_PLATFORM_HEAP
	return PlatformHeapReallocate(oldAddress, newAllocationSize, zeroNewSpace);
#endif

	HeapRegion *region = HEAP_REGION_HEADER(oldAddress);

	if (region->used != USED_HEAP_REGION_MAGIC) {
		HEAP_PANIC(region->used, region, nullptr);
	}

	size_t oldAllocationSize = region->allocationSize;
	size_t oldRegionSize = region->size;
	size_t newRegionSize = (newAllocationSize + USED_HEAP_REGION_HEADER_SIZE + 0x1F) & ~0x1F;
	void *newAddress = oldAddress;
	bool inHeapBlock = region->size;
	bool canMerge = true;

	if (inHeapBlock) {
		HEAP_ACQUIRE_MUTEX(heap.mutex);
		MAYBE_VALIDATE_HEAP();

		HeapRegion *adjacent = HEAP_REGION_NEXT(region);

		if (oldRegionSize < newRegionSize) {
			if (!adjacent->used && newRegionSize < oldRegionSize + adjacent->size - FREE_HEAP_REGION_HEADER_SIZE) {
				HeapRegion *post = HEAP_REGION_NEXT(adjacent);
				HeapRemoveFreeRegion(adjacent);
				region->size = newRegionSize;
				adjacent = HEAP_REGION_NEXT(region);
				adjacent->next = (uint8_t *) post - (uint8_t *) adjacent;
				adjacent->used = 0;
				adjacent->offset = region->offset + region->size;
				post->previous = adjacent->next;
				adjacent->previous = region->next;
				HeapAddFreeRegion(adjacent, heap.regions);
			} else if (!adjacent->used && newRegionSize <= oldRegionSize + adjacent->size) {
				HeapRegion *post = HEAP_REGION_NEXT(adjacent);
				HeapRemoveFreeRegion(adjacent);
				region->size = newRegionSize;
				post->previous = region->next;
			} else {
				canMerge = false;
			}
		} else if (newRegionSize < oldRegionSize) {
			if (!adjacent->used) {
				HeapRegion *post = HEAP_REGION_NEXT(adjacent);
				HeapRemoveFreeRegion(adjacent);
				region->size = newRegionSize;
				adjacent = HEAP_REGION_NEXT(region);
				adjacent->next = (uint8_t *) post - (uint8_t *) adjacent;
				adjacent->used = 0;
				adjacent->offset = region->offset + region->size;
				post->previous = adjacent->next;
				adjacent->previous = region->next;
				HeapAddFreeRegion(adjacent, heap.regions);
			} else if (newRegionSize + USED_HEAP_REGION_HEADER_SIZE <= oldRegionSize) {
				region->size = newRegionSize;
				HeapRegion *middle = HEAP_REGION_NEXT(region);
				middle->size = oldRegionSize - newRegionSize;
				middle->used = 0;
				middle->previous = region->size;
				middle->offset = region->offset + region->size;
				adjacent->previous = middle->size;
				HeapAddFreeRegion(middle, heap.regions);
			}
		}

		MAYBE_VALIDATE_HEAP();
		HEAP_RELEASE_MUTEX(heap.mutex);
	} else {
		canMerge = false;
	}

	if (!canMerge) {
		newAddress = EsHeapAllocate(newAllocationSize, false, _heap);
		EsMemoryCopy(newAddress, oldAddress, oldAllocationSize > newAllocationSize ? newAllocationSize : oldAllocationSize);
		EsHeapFree(oldAddress, 0, _heap);
	} else {
		HEAP_REGION_HEADER(newAddress)->allocationSize = newAllocationSize;
		__sync_fetch_and_add(&heap.size, newRegionSize - oldRegionSize);
	}

	if (zeroNewSpace && newAllocationSize > oldAllocationSize) {
		EsMemoryZero((uint8_t *) newAddress + oldAllocationSize, newAllocationSize - oldAllocationSize);
	}
	return newAddress;
}

#ifndef KERNEL
void EsHeapValidate() {
	HeapValidate(&heap);
}
#endif
