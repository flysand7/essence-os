// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

#ifndef IMPLEMENTATION

struct Arena {
	// Arenas are not thread-safe!
	// You can use different arenas in different threads, though.
	void *firstEmptySlot, *firstBlock;
	size_t slotsPerBlock, slotSize, blockSize;
};

void *ArenaAllocate(Arena *arena, bool zero); // Not thread-safe.
void ArenaFree(Arena *arena, void *pointer); // Not thread-safe.
void ArenaInitialise(Arena *arena, size_t blockSize, size_t itemSize);

#else

struct ArenaSlot {
	uintptr_t indexInBlock;
	ArenaSlot *nextEmpty, **previousEmpty;
};

struct ArenaBlock {
	struct Arena *arena;
	size_t usedSlots;
	uint8_t *data;
	ArenaBlock *nextBlock;
};

void ArenaFree(Arena *arena, void *pointer) {
	if (!pointer) return;

	ArenaBlock **blockReference = (ArenaBlock **) &arena->firstBlock;
	ArenaBlock *block = (ArenaBlock *) arena->firstBlock;

	while (true) {
		if (block->data <= (uint8_t *) pointer && block->data + arena->blockSize > (uint8_t *) pointer) {
			break;
		}

		blockReference = &block->nextBlock;
		block = block->nextBlock;
		EsAssert(block);
	}

	uintptr_t indexInBlock = ((uint8_t *) pointer - block->data) / arena->slotSize; 
	EsAssert(indexInBlock < arena->slotsPerBlock);
	ArenaSlot *slot = (ArenaSlot *) (block + 1) + indexInBlock;
	EsAssert(slot->indexInBlock == indexInBlock);
	
	slot->nextEmpty = (ArenaSlot *) arena->firstEmptySlot;
	if (arena->firstEmptySlot) ((ArenaSlot *) arena->firstEmptySlot)->previousEmpty = &slot->nextEmpty;
	arena->firstEmptySlot = slot;
	slot->previousEmpty = (ArenaSlot **) &arena->firstEmptySlot;
	
	if (!(--block->usedSlots)) {
		ArenaSlot *slot = (ArenaSlot *) (block + 1);
		
		for (uintptr_t i = 0; i < arena->slotsPerBlock; i++, slot++) {
			if (slot->nextEmpty) slot->nextEmpty->previousEmpty = slot->previousEmpty;
			*slot->previousEmpty = slot->nextEmpty;
		}
		
		*blockReference = block->nextBlock;

#ifdef KERNEL
		MMFree(kernelMMSpace, block->data);
		EsHeapFree(block, 0, K_FIXED);
#else
		EsMemoryUnreserve(block->data);
		EsHeapFree(block);
#endif
	}
}

void *ArenaAllocate(Arena *arena, bool zero) {
	if (!arena->firstEmptySlot) {
#ifdef KERNEL
		ArenaBlock *block = (ArenaBlock *) EsHeapAllocate(arena->slotsPerBlock * sizeof(ArenaSlot) + sizeof(ArenaBlock), false, K_FIXED);
		block->data = (uint8_t *) MMStandardAllocate(kernelMMSpace, arena->blockSize, ES_FLAGS_DEFAULT, nullptr, true /* commitAll */);
#else
		ArenaBlock *block = (ArenaBlock *) EsHeapAllocate(arena->slotsPerBlock * sizeof(ArenaSlot) + sizeof(ArenaBlock), false);
		block->data = (uint8_t *) EsMemoryReserve(arena->blockSize, ES_MEMORY_PROTECTION_READ_WRITE); 
#endif

		ArenaSlot *slots = (ArenaSlot *) (block + 1);
		
		for (uintptr_t i = 0; i < arena->slotsPerBlock; i++) {
			ArenaSlot *slot = slots + i;
			slot->indexInBlock = i;
			slot->nextEmpty = i == arena->slotsPerBlock - 1 ? (ArenaSlot *) arena->firstEmptySlot : (slot + 1);
			slot->previousEmpty = i ? &slot[-1].nextEmpty : (ArenaSlot **) &arena->firstEmptySlot;
		}
		
		block->arena = arena;
		block->usedSlots = 0;
		block->nextBlock = (ArenaBlock *) arena->firstBlock;

		arena->firstEmptySlot = slots;
		arena->firstBlock = block;
	}
	
	ArenaSlot *slot = (ArenaSlot *) arena->firstEmptySlot;
	arena->firstEmptySlot = slot->nextEmpty;
	if (arena->firstEmptySlot) ((ArenaSlot *) arena->firstEmptySlot)->previousEmpty = (ArenaSlot **) &arena->firstEmptySlot;
	EsAssert(slot->previousEmpty == (ArenaSlot **) &arena->firstEmptySlot); // Incorrect first empty slot back link.
	ArenaBlock *block = (ArenaBlock *) (slot - slot->indexInBlock) - 1;
	block->usedSlots++;
	void *pointer = block->data + arena->slotSize * slot->indexInBlock;
	if (zero) EsMemoryZero(pointer, arena->slotSize);
	return pointer;
}

void ArenaInitialise(Arena *arena, size_t blockSize, size_t itemSize) {
	EsAssert(!arena->slotSize && itemSize);
	arena->slotSize = itemSize;
	arena->slotsPerBlock = blockSize / arena->slotSize;
	if (arena->slotsPerBlock < 32) arena->slotsPerBlock = 32;
	arena->blockSize = arena->slotsPerBlock * arena->slotSize;
}

#endif
