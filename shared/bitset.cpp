#ifndef IMPLEMENTATION

struct Bitset {
	void Initialise(size_t count, bool mapAll = false);
	void PutAll();
	uintptr_t Get(size_t count = 1, uintptr_t align = 1, uintptr_t below = 0);
	void Put(uintptr_t index);
	void Take(uintptr_t index);
	bool Read(uintptr_t index);

#define BITSET_GROUP_SIZE (4096)
	uint32_t *singleUsage;
	uint16_t *groupUsage;

	size_t singleCount; 
	size_t groupCount;

#ifdef DEBUG_BUILD
	bool modCheck;
#endif
};

#else

void Bitset::Initialise(size_t count, bool mapAll) {
	singleCount = (count + 31) & ~31;
	groupCount = singleCount / BITSET_GROUP_SIZE + 1;

	singleUsage = (uint32_t *) MMStandardAllocate(kernelMMSpace, (singleCount >> 3) + (groupCount * 2), mapAll ? MM_REGION_FIXED : 0);
	groupUsage = (uint16_t *) singleUsage + (singleCount >> 4);
}

void Bitset::PutAll() {
	for (uintptr_t i = 0; i < singleCount; i += 32) {
		groupUsage[i / BITSET_GROUP_SIZE] += 32;
		singleUsage[i / 32] |= 0xFFFFFFFF;
	}
}

uintptr_t Bitset::Get(size_t count, uintptr_t align, uintptr_t below) {
#ifdef DEBUG_BUILD
	if (modCheck) KernelPanic("Bitset::Allocate - Concurrent modification.\n");
	modCheck = true; EsDefer({modCheck = false;});
#endif

	uintptr_t returnValue = (uintptr_t) -1;

	if (below) {
		if (below < count) goto done;
		below -= count;
	}

	if (count == 1 && align == 1) {
		for (uintptr_t i = 0; i < groupCount; i++) {
			if (groupUsage[i]) {
				for (uintptr_t j = 0; j < BITSET_GROUP_SIZE; j++) {
					uintptr_t index = i * BITSET_GROUP_SIZE + j;
					if (below && index >= below) goto done;

					if (singleUsage[index >> 5] & (1 << (index & 31))) {
						singleUsage[index >> 5] &= ~(1 << (index & 31));
						groupUsage[i]--;
						returnValue = index;
						goto done;
					}
				}
			}
		}
	} else if (count == 16 && align == 16) {
		for (uintptr_t i = 0; i < groupCount; i++) {
			if (groupUsage[i] >= 16) {
				for (uintptr_t j = 0; j < BITSET_GROUP_SIZE; j += 16) {
					uintptr_t index = i * BITSET_GROUP_SIZE + j;
					if (below && index >= below) goto done;

					if (((uint16_t *) singleUsage)[index >> 4] == (uint16_t) (-1)) {
						((uint16_t *) singleUsage)[index >> 4] = 0;
						groupUsage[i] -= 16;
						returnValue = index;
						goto done;
					}
				}
			}
		}
	} else if (count == 32 && align == 32) {
		for (uintptr_t i = 0; i < groupCount; i++) {
			if (groupUsage[i] >= 32) {
				for (uintptr_t j = 0; j < BITSET_GROUP_SIZE; j += 32) {
					uintptr_t index = i * BITSET_GROUP_SIZE + j;
					if (below && index >= below) goto done;

					if (singleUsage[index >> 5] == (uint32_t) (-1)) {
						singleUsage[index >> 5] = 0;
						groupUsage[i] -= 32;
						returnValue = index;
						goto done;
					}
				}
			}
		}
	} else {
		// TODO Optimise this?

		size_t found = 0;
		uintptr_t start = 0;

		for (uintptr_t i = 0; i < groupCount; i++) {
			if (!groupUsage[i]) {
				found = 0;
				continue;
			}

			for (uintptr_t j = 0; j < BITSET_GROUP_SIZE; j++) {
				uintptr_t index = i * BITSET_GROUP_SIZE + j;

				if (singleUsage[index >> 5] & (1 << (index & 31))) {
					if (!found) {
						if (index >= below && below) goto done;
						if (index  % align)          continue;

						start = index;
					}

					found++;
				} else {
					found = 0;
				}

				if (found == count) {
					returnValue = start;

					for (uintptr_t i = 0; i < count; i++) {
						uintptr_t index = start + i;
						singleUsage[index >> 5] &= ~(1 << (index & 31));
					}

					goto done;
				}
			}
		}
	}

	done:;
	return returnValue;
}

bool Bitset::Read(uintptr_t index) {
	// We don't want to set modCheck, 
	// since we allow other bits to be modified while this bit is being read,
	// and we allow multiple readers of this bit.
	return singleUsage[index >> 5] & (1 << (index & 31));
}

void Bitset::Take(uintptr_t index) {
#ifdef DEBUG_BUILD
	if (modCheck) KernelPanic("Bitset::Take - Concurrent modification.\n");
	modCheck = true; EsDefer({modCheck = false;});
#endif

	uintptr_t group = index / BITSET_GROUP_SIZE;

#ifdef DEBUG_BUILD
	if (!(singleUsage[index >> 5] & (1 << (index & 31)))) {
		KernelPanic("Bitset::Take - Attempting to take a entry that has already been taken.\n");
	}
#endif

	groupUsage[group]--;
	singleUsage[index >> 5] &= ~(1 << (index & 31));
}

void Bitset::Put(uintptr_t index) {
#ifdef DEBUG_BUILD
	if (modCheck) KernelPanic("Bitset::Put - Concurrent modification.\n");
	modCheck = true; EsDefer({modCheck = false;});

	if (index > singleCount) {
		KernelPanic("Bitset::Put - Index greater than single code.\n");
	}

	if (singleUsage[index >> 5] & (1 << (index & 31))) {
		KernelPanic("Bitset::Put - Duplicate entry.\n");
	}
#endif

	{
		singleUsage[index >> 5] |= 1 << (index & 31);
		groupUsage[index / BITSET_GROUP_SIZE]++;
	}
}

#endif
