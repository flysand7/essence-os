// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

struct Range {
	uintptr_t from, to;
};

struct RangeSet {
#ifdef KERNEL
	Array<Range, K_CORE> ranges;
#else
	Array<Range> ranges;
#endif
	uintptr_t contiguous;

	Range *Find(uintptr_t offset, bool touching);
	bool Contains(uintptr_t offset);
	void Validate();
	bool Normalize();

	bool Set(uintptr_t from, uintptr_t to, intptr_t *delta, bool modify);
	bool Clear(uintptr_t from, uintptr_t to, intptr_t *delta, bool modify);
};

Range *RangeSet::Find(uintptr_t offset, bool touching) {
	if (!ranges.Length()) return nullptr;

	intptr_t low = 0, high = ranges.Length() - 1;

	while (low <= high) {
		intptr_t i = low + (high - low) / 2;
		Range *range = &ranges[i];

		if (range->from <= offset && (offset < range->to || (touching && offset <= range->to))) {
			return range;
		} else if (range->from <= offset) {
			low = i + 1;
		} else {
			high = i - 1;
		}
	}
	
	return nullptr;
}

bool RangeSet::Contains(uintptr_t offset) {
	if (ranges.Length()) {
		return Find(offset, false);
	} else {
		return offset < contiguous;
	}
}

void RangeSet::Validate() {
#ifdef DEBUG_BUILD
	uintptr_t previousTo = 0;

	if (!ranges.Length()) return;
	
	for (uintptr_t i = 0; i < ranges.Length(); i++) {
		Range *range = &ranges[i];
		
		if (previousTo && range->from <= previousTo) {
			EsPanic("RangeSet::Validate - Range %d in set %x is not placed after the prior range.\n", i, this);
		}

		if (range->from >= range->to) {
			EsPanic("RangeSet::Validate - Range %d in set %x is invalid.\n", i, this);
		}
		
		previousTo = range->to;
	}
#endif
}

bool RangeSet::Normalize() {
#if 0
	KernelLog(LOG_INFO, "RangeSet", "normalize", "Normalizing range set %x...\n", this);
#endif

	if (contiguous) {
		uintptr_t oldContiguous = contiguous;
		contiguous = 0;

		if (!Set(0, oldContiguous, nullptr, true)) {
			return false;
		}
	}
	
	return true;
}

bool RangeSet::Set(uintptr_t from, uintptr_t to, intptr_t *delta, bool modify) {
	if (to <= from) {
		EsPanic("RangeSet::Set - Invalid range %x to %x.\n", from, to);
	}

	// Can we store this as a single contiguous range?

	if (!ranges.Length()) {
		if (delta) {
			if (from >= contiguous) {
				*delta = to - from;
			} else if (to >= contiguous) {
				*delta = to - contiguous;
			} else {
				*delta = 0;
			}
		}

		if (!modify) {
			return true;
		}

		if (from <= contiguous) {
			if (to > contiguous) {
				contiguous = to;
			}

			return true;
		}

		if (!Normalize()) {
			return false;
		}
	}
	
	// Calculate the contiguous range covered.
	
	Range newRange = {};
	
	{
		Range *left = Find(from, true);
		Range *right = Find(to, true);
		
		newRange.from = left ? left->from : from;
		newRange.to = right ? right->to : to;
	}
	
	// Insert the new range.
	
	uintptr_t i = 0;
	
	for (; i <= ranges.Length(); i++) {
		if (i == ranges.Length() || ranges[i].to > newRange.from) {
			if (modify) {
				if (!ranges.Insert(newRange, i)) {
					return false;
				}

				i++;
			}

			break;
		}
	}
	
	// Remove overlapping ranges.
	
	uintptr_t deleteStart = i;
	size_t deleteCount = 0;
	uintptr_t deleteTotal = 0;
	
	for (; i < ranges.Length(); i++) {
		Range *range = &ranges[i];
		
		bool overlap = (range->from >= newRange.from && range->from <= newRange.to) 
			|| (range->to >= newRange.from && range->to <= newRange.to);
			
		if (overlap) {
			deleteCount++;
			deleteTotal += range->to - range->from;
		} else {
			break;
		}
	}

	if (modify) {
		ranges.DeleteMany(deleteStart, deleteCount);
	}
	
	Validate();

	if (delta) {
		*delta = newRange.to - newRange.from - deleteTotal;
	}

	return true;
}

bool RangeSet::Clear(uintptr_t from, uintptr_t to, intptr_t *delta, bool modify) {
	if (to <= from) {
		EsPanic("RangeSet::Clear - Invalid range %x to %x.\n", from, to);
	}

	if (!ranges.Length()) {
		if (from < contiguous && contiguous) {
			if (to < contiguous) {
				if (modify) {
					if (!Normalize()) return false;
				} else {
					if (delta) *delta = from - to;
					return true;
				}
			} else {
				if (delta) *delta = from - contiguous;
				if (modify) contiguous = from;
				return true;
			}
		} else {
			if (delta) *delta = 0;
			return true;
		}
	}
	
	if (!ranges.Length()) {
		ranges.Free();
		if (delta) *delta = 0;
		return true;
	}

	if (to <= ranges.First().from || from >= ranges.Last().to) {
		if (delta) *delta = 0;
		return true;
	}

	if (from <= ranges.First().from && to >= ranges.Last().to) {
		if (delta) {
			intptr_t total = 0;

			for (uintptr_t i = 0; i < ranges.Length(); i++) {
				total += ranges[i].to - ranges[i].from;
			}

			*delta = -total;
		}

		if (modify) {
			ranges.Free();
		}

		return true;
	}

	// Find the first and last overlapping regions.
	
	uintptr_t overlapStart = ranges.Length();
	size_t overlapCount = 0;
	
	for (uintptr_t i = 0; i < ranges.Length(); i++) {
		Range *range = &ranges[i];
		
		if (range->to > from && range->from < to) {
			overlapStart = i;
			overlapCount = 1;
			break;
		}
	}
	
	for (uintptr_t i = overlapStart + 1; i < ranges.Length(); i++) {
		Range *range = &ranges[i];
		
		if (range->to >= from && range->from < to) {
			overlapCount++;
		} else {
			break;
		}
	}
	
	// Remove the overlapping sections.

	intptr_t _delta = 0;
	
	if (overlapCount == 1) {
		Range *range = &ranges[overlapStart];
		
		if (range->from < from && range->to > to) {
			Range newRange = { to, range->to };
			_delta -= to - from;

			if (modify) {
				if (!ranges.Insert(newRange, overlapStart + 1)) {
					return false;
				}

				ranges[overlapStart].to = from;
			}
		} else if (range->from < from) {
			_delta -= range->to - from;
			if (modify) range->to = from;
		} else if (range->to > to) {
			_delta -= to - range->from;
			if (modify) range->from = to;
		} else {
			_delta -= range->to - range->from;
			if (modify) ranges.Delete(overlapStart);
		}
	} else if (overlapCount > 1) {
		Range *left = &ranges[overlapStart];
		Range *right = &ranges[overlapStart + overlapCount - 1];
		
		if (left->from < from) {
			_delta -= left->to - from;
			if (modify) left->to = from;
			overlapStart++, overlapCount--;
		}

		if (right->to > to) {
			_delta -= to - right->from;
			if (modify) right->from = to;
			overlapCount--;
		}

		if (delta) {
			for (uintptr_t i = overlapStart; i < overlapStart + overlapCount; i++) {
				_delta -= ranges[i].to - ranges[i].from;
			}
		}
		
		if (modify) {
			ranges.DeleteMany(overlapStart, overlapCount);
		}
	}

	if (delta) {
		*delta = _delta;
	}
	
	Validate();
	return true;
}
