#ifndef IMPLEMENTATION

// TODO Implement read ahead in CCSpaceAccess.
// TODO Implement dispatch groups in CCSpaceAccess and CCWriteBehindThread.
// TODO Implement better write back algorithm.

// TODO Check that active.references in the page frame database is used safely with threading.

// Describes the physical memory covering a section of a file.

struct CCCachedSection {
	EsFileOffset offset, 			// The offset into the file.
		     pageCount;			// The number of pages in the section.
	volatile size_t mappedRegionsCount; 	// The number of mapped regions that use this section.
	uintptr_t *data;			// A list of page frames covering the section.
};

struct CCActiveSection {
	KEvent loadCompleteEvent, writeCompleteEvent; 
	LinkedItem<CCActiveSection> listItem; // Either in the LRU list, or the modified list. If accessors > 0, it should not be in a list.

	EsFileOffset offset;
	struct CCSpace *cache;

	size_t accessors;
	volatile bool loading, writing, modified, flush;

	uint16_t referencedPageCount; 
	uint8_t referencedPages[CC_ACTIVE_SECTION_SIZE / K_PAGE_SIZE / 8]; // If accessors > 0, then pages cannot be dereferenced.

	uint8_t modifiedPages[CC_ACTIVE_SECTION_SIZE / K_PAGE_SIZE / 8];
};

struct CCActiveSectionReference {
	EsFileOffset offset; // Offset into the file; multiple of CC_ACTIVE_SECTION_SIZE.
	uintptr_t index; // Index of the active section.
};

struct MMActiveSectionManager {
	CCActiveSection *sections;
	size_t sectionCount;
	uint8_t *baseAddress;
	KMutex mutex;
	LinkedList<CCActiveSection> lruList;
	LinkedList<CCActiveSection> modifiedList;
	KEvent modifiedNonEmpty, modifiedNonFull;
	Thread *writeBackThread;
};

// The callbacks for a CCSpace.

struct CCSpaceCallbacks {
	EsError (*readInto)(CCSpace *fileCache, void *buffer, EsFileOffset offset, EsFileOffset count);
	EsError (*writeFrom)(CCSpace *fileCache, const void *buffer, EsFileOffset offset, EsFileOffset count);
};

void CCInitialise();

void CCDereferenceActiveSection(CCActiveSection *section, uintptr_t startingPage = 0);

bool CCSpaceInitialise(CCSpace *cache);
void CCSpaceDestroy(CCSpace *cache);
void CCSpaceFlush(CCSpace *cache);
void CCSpaceTruncate(CCSpace *cache, EsFileOffset newSize);
bool CCSpaceCover(CCSpace *cache, EsFileOffset insertStart, EsFileOffset insertEnd); 
void CCSpaceUncover(CCSpace *cache, EsFileOffset removeStart, EsFileOffset removeEnd);

#define CC_ACCESS_MAP                (1 << 0)
#define CC_ACCESS_READ               (1 << 1)
#define CC_ACCESS_WRITE              (1 << 2)
#define CC_ACCESS_WRITE_BACK         (1 << 3) // Wait for the write to complete before returning.
#define CC_ACCESS_PRECISE            (1 << 4) // Do not write back bytes not touched by this write. (Usually modified tracking is to page granularity.) Requires WRITE_BACK.
#define CC_ACCESS_USER_BUFFER_MAPPED (1 << 5) // Set if the user buffer is memory-mapped to mirror this or another cache.

EsError CCSpaceAccess(CCSpace *cache, K_USER_BUFFER void *buffer, EsFileOffset offset, EsFileOffset count, uint32_t flags, 
		MMSpace *mapSpace = nullptr, unsigned mapFlags = ES_FLAGS_DEFAULT);

#else

CCCachedSection *CCFindCachedSectionContaining(CCSpace *cache, EsFileOffset sectionOffset) {
	KMutexAssertLocked(&cache->cachedSectionsMutex);

	if (!cache->cachedSections.Length()) {
		return nullptr;
	}

	CCCachedSection *cachedSection = nullptr;

	bool found = false;
	intptr_t low = 0, high = cache->cachedSections.Length() - 1;

	while (low <= high) {
		intptr_t i = low + (high - low) / 2;
		cachedSection = &cache->cachedSections[i];

		if (cachedSection->offset + cachedSection->pageCount * K_PAGE_SIZE <= sectionOffset) {
			low = i + 1;
		} else if (cachedSection->offset > sectionOffset) {
			high = i - 1;
		} else {
			found = true;
			break;
		}
	}

	return found ? cachedSection : nullptr;
}

bool CCSpaceCover(CCSpace *cache, EsFileOffset insertStart, EsFileOffset insertEnd) {
	KMutexAssertLocked(&cache->cachedSectionsMutex);

	// TODO Test this thoroughly.
	// TODO Break up really large sections. (maybe into GBs?)

	insertStart = RoundDown(insertStart, K_PAGE_SIZE);
	insertEnd = RoundUp(insertEnd, K_PAGE_SIZE);
	EsFileOffset position = insertStart, lastEnd = 0;
	CCCachedSection *result = nullptr;

	// EsPrint("New: %d, %d\n", insertStart / K_PAGE_SIZE, insertEnd / K_PAGE_SIZE);

	for (uintptr_t i = 0; i < cache->cachedSections.Length(); i++) {
		CCCachedSection *section = &cache->cachedSections[i];

		EsFileOffset sectionStart = section->offset, 
			     sectionEnd = section->offset + section->pageCount * K_PAGE_SIZE;

		// EsPrint("Existing (%d): %d, %d\n", i, sectionStart / K_PAGE_SIZE, sectionEnd / K_PAGE_SIZE);

		if (insertStart > sectionEnd) continue;

		// If the inserted region starts before this section starts, then we need to make a new section before us.

		if (position < sectionStart) {
			CCCachedSection newSection = {};
			newSection.mappedRegionsCount = 0;
			newSection.offset = position;
			newSection.pageCount = ((insertEnd > sectionStart ? sectionStart : insertEnd) - position) / K_PAGE_SIZE;

			if (newSection.pageCount) {
				// EsPrint("\tAdded: %d, %d\n", newSection.offset / K_PAGE_SIZE, newSection.pageCount);
				newSection.data = (uintptr_t *) EsHeapAllocate(sizeof(uintptr_t) * newSection.pageCount, true, K_CORE);

				if (!newSection.data) {
					goto fail;
				}

				if (!cache->cachedSections.Insert(newSection, i)) { 
					EsHeapFree(newSection.data, sizeof(uintptr_t) * newSection.pageCount, K_CORE); 
					goto fail; 
				}

				i++;
			}

		}

		position = sectionEnd;
		if (position > insertEnd) break;
	}

	// Insert the final section if necessary.

	if (position < insertEnd) {
		CCCachedSection newSection = {};
		newSection.mappedRegionsCount = 0;
		newSection.offset = position;
		newSection.pageCount = (insertEnd - position) / K_PAGE_SIZE;
		newSection.data = (uintptr_t *) EsHeapAllocate(sizeof(uintptr_t) * newSection.pageCount, true, K_CORE);
		// EsPrint("\tAdded (at end): %d, %d\n", newSection.offset / K_PAGE_SIZE, newSection.pageCount);

		if (!newSection.data) {
			goto fail;
		}

		if (!cache->cachedSections.Add(newSection)) { 
			EsHeapFree(newSection.data, sizeof(uintptr_t) * newSection.pageCount, K_CORE); 
			goto fail; 
		}
	}

	for (uintptr_t i = 0; i < cache->cachedSections.Length(); i++) {
		CCCachedSection *section = &cache->cachedSections[i];

		EsFileOffset sectionStart = section->offset, 
			     sectionEnd = section->offset + section->pageCount * K_PAGE_SIZE;

		if (sectionStart < lastEnd) KernelPanic("CCSpaceCover - Overlapping MMCachedSections.\n");

		// If the inserted region ends after this section starts, 
		// and starts before this section ends, then it intersects it.

		if (insertEnd > sectionStart && insertStart < sectionEnd) {
			section->mappedRegionsCount++;
			// EsPrint("+ %x %x %d\n", cache, section->data, section->mappedRegionsCount);
			if (result && sectionStart != lastEnd) KernelPanic("CCSpaceCover - Incomplete MMCachedSections.\n");
			if (!result) result = section;
		}

		lastEnd = sectionEnd;
	}

	return true;

	fail:;
	return false; // TODO Remove unused cached sections?
}

void CCSpaceUncover(CCSpace *cache, EsFileOffset removeStart, EsFileOffset removeEnd) {
	KMutexAssertLocked(&cache->cachedSectionsMutex);

	removeStart = RoundDown(removeStart, K_PAGE_SIZE);
	removeEnd = RoundUp(removeEnd, K_PAGE_SIZE);

	CCCachedSection *first = CCFindCachedSectionContaining(cache, removeStart);

	if (!first) {
		KernelPanic("CCSpaceUncover - Range %x->%x was not covered in cache %x.\n", removeStart, removeEnd, cache);
	}

	for (uintptr_t i = first - cache->cachedSections.array; i < cache->cachedSections.Length(); i++) {
		CCCachedSection *section = &cache->cachedSections[i];

		EsFileOffset sectionStart = section->offset, 
			     sectionEnd = section->offset + section->pageCount * K_PAGE_SIZE;

		if (removeEnd > sectionStart && removeStart < sectionEnd) {
			if (!section->mappedRegionsCount) KernelPanic("CCSpaceUncover - Section wasn't mapped.\n");
			section->mappedRegionsCount--;
			// EsPrint("- %x %x %d\n", cache, section->data, section->mappedRegionsCount);
		} else {
			break;
		}
	}
}

void CCWriteSectionPrepare(CCActiveSection *section) {
	KMutexAssertLocked(&activeSectionManager.mutex);
	if (!section->modified) KernelPanic("CCWriteSectionPrepare - Unmodified section %x on modified list.\n", section);
	if (section->accessors) KernelPanic("CCWriteSectionPrepare - Section %x with accessors on modified list.\n", section);
	if (section->writing) KernelPanic("CCWriteSectionPrepare - Section %x already being written.\n", section);
	activeSectionManager.modifiedList.Remove(&section->listItem);
	section->writing = true;
	section->modified = false;
	section->flush = false;
	KEventReset(&section->writeCompleteEvent);
	section->accessors = 1;
}

void CCWriteSection(CCActiveSection *section) {
	// Write any modified pages to the backing store.

	uint8_t *sectionBase = activeSectionManager.baseAddress + (section - activeSectionManager.sections) * CC_ACTIVE_SECTION_SIZE;
	EsError error = ES_SUCCESS;

	for (uintptr_t i = 0; i < CC_ACTIVE_SECTION_SIZE / K_PAGE_SIZE; i++) {
		uintptr_t from = i, count = 0;

		while (i != CC_ACTIVE_SECTION_SIZE / K_PAGE_SIZE 
				&& (section->modifiedPages[i >> 3] & (1 << (i & 7)))) {
			count++, i++;
		}

		if (!count) continue;

		error = section->cache->callbacks->writeFrom(section->cache, sectionBase + from * K_PAGE_SIZE, 
				section->offset + from * K_PAGE_SIZE, count * K_PAGE_SIZE);

		if (error != ES_SUCCESS) {
			break;
		}
	}

	// Return the active section.

	KMutexAcquire(&activeSectionManager.mutex);

	if (!section->accessors) KernelPanic("CCWriteSection - Section %x has no accessors while being written.\n", section);
	if (section->modified) KernelPanic("CCWriteSection - Section %x was modified while being written.\n", section);

	section->accessors--;
	section->writing = false;
	EsMemoryZero(section->modifiedPages, sizeof(section->modifiedPages));
	__sync_synchronize();
	KEventSet(&section->writeCompleteEvent);
	KEventSet(&section->cache->writeComplete, false, true);

	if (!section->accessors) {
		if (section->loading) KernelPanic("CCSpaceAccess - Active section %x with no accessors is loading.", section);
		activeSectionManager.lruList.InsertEnd(&section->listItem);
	}

	KMutexRelease(&activeSectionManager.mutex);
}

void CCSpaceFlush(CCSpace *cache) {
	while (true) {
		bool complete = true;

		KMutexAcquire(&cache->activeSectionsMutex);
		KMutexAcquire(&activeSectionManager.mutex);

		for (uintptr_t i = 0; i < cache->activeSections.Length(); i++) {
			CCActiveSection *section = activeSectionManager.sections + cache->activeSections[i].index;

			if (section->cache == cache && section->offset == cache->activeSections[i].offset) {
				if (section->writing) {
					// The section is being written; wait for it to complete.
					complete = false;
				} else if (section->modified) {
					if (section->accessors) {
						// Someone is accessing this section; mark it to be written back once they are done.
						section->flush = true;
						complete = false;
					} else {
						// Nobody is accessing the section; we can write it ourselves.
						complete = false;
						CCWriteSectionPrepare(section);
						KMutexRelease(&activeSectionManager.mutex);
						KMutexRelease(&cache->activeSectionsMutex);
						CCWriteSection(section);
						KMutexAcquire(&cache->activeSectionsMutex);
						KMutexAcquire(&activeSectionManager.mutex);
					}
				}
			}

		}

		KMutexRelease(&activeSectionManager.mutex);
		KMutexRelease(&cache->activeSectionsMutex);

		if (!complete) {
			KEventWait(&cache->writeComplete);
		} else {
			break;
		}
	}
}

void CCActiveSectionReturnToLists(CCActiveSection *section, bool writeBack) {
	bool waitNonFull = false;

	if (section->flush) {
		writeBack = true;
	}

	while (true) {
		// If modified, wait for the modified list to be below a certain size.

		if (section->modified && waitNonFull) {
			KEventWait(&activeSectionManager.modifiedNonFull);
		}

		// Decrement the accessors count.

		KMutexAcquire(&activeSectionManager.mutex);
		EsDefer(KMutexRelease(&activeSectionManager.mutex));

		if (!section->accessors) KernelPanic("CCSpaceAccess - Active section %x has no accessors.\n", section);

		if (section->accessors == 1) {
			if (section->loading) KernelPanic("CCSpaceAccess - Active section %x with no accessors is loading.", section);

			// If nobody is accessing the section, put it at the end of the LRU list.

			if (section->modified) {
				if (activeSectionManager.modifiedList.count > CC_MAX_MODIFIED) {
					waitNonFull = true;
					continue;
				} else if (activeSectionManager.modifiedList.count == CC_MAX_MODIFIED) {
					KEventReset(&activeSectionManager.modifiedNonFull);
				}

				activeSectionManager.modifiedList.InsertEnd(&section->listItem);
				KEventSet(&activeSectionManager.modifiedNonEmpty, false, true);
			} else {
				activeSectionManager.lruList.InsertEnd(&section->listItem);
			}
		}

		section->accessors--;

		if (writeBack && !section->accessors && section->modified) {
			CCWriteSectionPrepare(section);
		} else {
			writeBack = false;
		}

		break;
	}

	if (writeBack) {
		CCWriteSection(section);
	}
}

void CCSpaceTruncate(CCSpace *cache, size_t newSize) {
	// Concurrent calls to CCSpaceAccess must be prevented;
	// this only handles concurrent calls to CCWriteSection.

	uintptr_t newSizePages = (newSize + K_PAGE_SIZE - 1) / K_PAGE_SIZE;
	bool doneActiveSections = false;

	while (!doneActiveSections) {
		bool waitForWritingToComplete = false;
		CCActiveSection *section = nullptr;

		KMutexAcquire(&cache->activeSectionsMutex);
		KMutexAcquire(&activeSectionManager.mutex);

		if (cache->activeSections.Length()) {
			// Get the last active section.
			CCActiveSectionReference reference = cache->activeSections.Last();
			section = activeSectionManager.sections + reference.index;

			if (section->cache != cache || section->offset != reference.offset) {
				// Remove invalid section.
				// TODO This code path has not been tested.
				cache->activeSections.SetLength(cache->activeSections.Length() - 1);
				section = nullptr;
			} else {
				if (reference.offset + CC_ACTIVE_SECTION_SIZE <= newSize) {
					// We've cleared all the active sections that were past the truncation point.
					doneActiveSections = true;
					section = nullptr;
				} else {
					if (section->accessors) {
						// We want to remove part/all of this section, but it's being written,
						// so we'll wait for that to complete first.

						if (!section->writing) {
							KernelPanic("CCSpaceTruncate - Active section %x in space %x has accessors but isn't being written.\n",
									section, cache);
						}

						waitForWritingToComplete = true;
					} else {
						section->listItem.RemoveFromList();
					}

					if (section->loading) {
						// TODO How will this interact with read-ahead, once implemented?
						KernelPanic("CCSpaceTruncate - Active section %x in space %x is in the loading state.\n",
								section, cache);
					}

					section->accessors++;

					if (section->offset >= newSize) {
						cache->activeSections.SetLength(cache->activeSections.Length() - 1);
					}
				}

			}
		} else {
			doneActiveSections = true;
		}

		KMutexRelease(&activeSectionManager.mutex);
		KMutexRelease(&cache->activeSectionsMutex);

		if (section) {
			if (waitForWritingToComplete) {
				KEventWait(&section->writeCompleteEvent);
			}

			if (section->offset >= newSize) {
				// Remove active sections completely past the truncation point.
				KMutexAcquire(&cache->cachedSectionsMutex);
				CCSpaceUncover(cache, section->offset, section->offset + CC_ACTIVE_SECTION_SIZE);
				KMutexRelease(&cache->cachedSectionsMutex);
				KMutexAcquire(&activeSectionManager.mutex);
				CCDereferenceActiveSection(section);
				section->cache = nullptr;
				section->accessors = 0;
				section->modified = false; // Don't try to write this section back!
				activeSectionManager.lruList.InsertStart(&section->listItem);
				KMutexRelease(&activeSectionManager.mutex);
			} else {
				// Remove part of the active section containing the truncation point.
				KMutexAcquire(&activeSectionManager.mutex);
				CCDereferenceActiveSection(section, newSizePages - section->offset / K_PAGE_SIZE);
				KMutexRelease(&activeSectionManager.mutex);
				CCActiveSectionReturnToLists(section, false);
				break;
			}
		}
	}

	KMutexAcquire(&cache->cachedSectionsMutex);

	while (cache->cachedSections.Length()) {
		CCCachedSection *section = &cache->cachedSections.Last();

		uintptr_t firstPage = 0;

		if (section->offset / K_PAGE_SIZE < newSizePages) {
			firstPage = newSizePages - section->offset / K_PAGE_SIZE;
		}

		if (firstPage > section->pageCount) {
			// Don't early exit if firstPage = section->pageCount, since there could be a partialPage to clear.
			break;
		}

		for (uintptr_t i = firstPage; i < section->pageCount; i++) {
			KMutexAcquire(&pmm.pageFrameMutex);

			if (section->data[i] & MM_SHARED_ENTRY_PRESENT) {
				uintptr_t page = section->data[i] & ~(K_PAGE_SIZE - 1);

				if (pmm.pageFrames[page >> K_PAGE_BITS].state != MMPageFrame::ACTIVE) {
					MMPhysicalActivatePages(page >> K_PAGE_BITS, 1, ES_FLAGS_DEFAULT);
				}

				// Deallocate physical memory no longer in use.
				MMPhysicalFree(page, true, 1);

				section->data[i] = 0;
			}

			KMutexRelease(&pmm.pageFrameMutex);
		}

		if (firstPage) {
			if (newSize & (K_PAGE_SIZE - 1)) {
				uintptr_t partialPage = (newSize - section->offset) / K_PAGE_SIZE;

				if (partialPage >= section->pageCount) {
					KernelPanic("CCSpaceTruncate - partialPage %x outside range in CCSpace %x with new size %x.\n", 
							partialPage, cache, newSize);
				}
				
				if (section->data[partialPage] & MM_SHARED_ENTRY_PRESENT) {
					// Zero the inaccessible part of the last page still accessible after truncation.
					PMZeroPartial(section->data[partialPage] & ~(K_PAGE_SIZE - 1), newSize & (K_PAGE_SIZE - 1), K_PAGE_SIZE);
				}
			}

			break;
		} else {
			if (section->mappedRegionsCount) {
				KernelPanic("CCSpaceTruncate - Section %x has positive mappedRegionsCount in CCSpace %x.", 
						section, cache);
			}
			
			EsHeapFree(section->data, sizeof(uintptr_t) * section->pageCount, K_CORE);
			cache->cachedSections.SetLength(cache->cachedSections.Length() - 1);
		}
	}

	KMutexRelease(&cache->cachedSectionsMutex);
}

void CCSpaceDestroy(CCSpace *cache) {
	CCSpaceFlush(cache);

	for (uintptr_t i = 0; i < cache->activeSections.Length(); i++) {
		KMutexAcquire(&activeSectionManager.mutex);

		CCActiveSection *section = activeSectionManager.sections + cache->activeSections[i].index;

		if (section->cache == cache && section->offset == cache->activeSections[i].offset) {
			CCDereferenceActiveSection(section);
			section->cache = nullptr;

			if (section->accessors || section->modified || section->listItem.list != &activeSectionManager.lruList) {
				KernelPanic("CCSpaceDestroy - Section %x has invalid state to destroy cache space %x.\n",
						section, cache);
			}

			section->listItem.RemoveFromList();
			activeSectionManager.lruList.InsertStart(&section->listItem);
		}

		KMutexRelease(&activeSectionManager.mutex);
	}

	for (uintptr_t i = 0; i < cache->cachedSections.Length(); i++) {
		CCCachedSection *section = &cache->cachedSections[i];

		for (uintptr_t i = 0; i < section->pageCount; i++) {
			KMutexAcquire(&pmm.pageFrameMutex);

			if (section->data[i] & MM_SHARED_ENTRY_PRESENT) {
				uintptr_t page = section->data[i] & ~(K_PAGE_SIZE - 1);

				if (pmm.pageFrames[page >> K_PAGE_BITS].state != MMPageFrame::ACTIVE) {
				       MMPhysicalActivatePages(page >> K_PAGE_BITS, 1, ES_FLAGS_DEFAULT);
				}

				MMPhysicalFree(page, true, 1);
			}

			KMutexRelease(&pmm.pageFrameMutex);
		}

		EsHeapFree(section->data, sizeof(uintptr_t) * section->pageCount, K_CORE);
	}

	cache->cachedSections.Free();
	cache->activeSections.Free();
}

bool CCSpaceInitialise(CCSpace *cache) {
	cache->writeComplete.autoReset = true;
	return true;
}

void CCDereferenceActiveSection(CCActiveSection *section, uintptr_t startingPage) {
	KMutexAssertLocked(&activeSectionManager.mutex);

	if (!startingPage) {
		MMArchUnmapPages(kernelMMSpace, 
				(uintptr_t) activeSectionManager.baseAddress + (section - activeSectionManager.sections) * CC_ACTIVE_SECTION_SIZE, 
				CC_ACTIVE_SECTION_SIZE / K_PAGE_SIZE, MM_UNMAP_PAGES_BALANCE_FILE);
		EsMemoryZero(section->referencedPages, sizeof(section->referencedPages));
		EsMemoryZero(section->modifiedPages, sizeof(section->modifiedPages));
		section->referencedPageCount = 0;
	} else {
		MMArchUnmapPages(kernelMMSpace, 
				(uintptr_t) activeSectionManager.baseAddress 
					+ (section - activeSectionManager.sections) * CC_ACTIVE_SECTION_SIZE 
					+ startingPage * K_PAGE_SIZE, 
				(CC_ACTIVE_SECTION_SIZE / K_PAGE_SIZE - startingPage), MM_UNMAP_PAGES_BALANCE_FILE);

		for (uintptr_t i = startingPage; i < CC_ACTIVE_SECTION_SIZE / K_PAGE_SIZE; i++) {
			if (section->referencedPages[i >> 3] & (1 << (i & 7))) {
				section->referencedPages[i >> 3] &= ~(1 << (i & 7));
				section->modifiedPages[i >> 3] &= ~(1 << (i & 7));
				section->referencedPageCount--;
			}
		}
	}
}

EsError CCSpaceAccess(CCSpace *cache, K_USER_BUFFER void *_buffer, EsFileOffset offset, EsFileOffset count, uint32_t flags, 
		MMSpace *mapSpace, unsigned mapFlags) {
	// TODO Reading in multiple active sections at the same time - will this give better performance on AHCI/NVMe?
	// 	- Each active section needs to be separately committed.
	// TODO Read-ahead.

	// Commit CC_ACTIVE_SECTION_SIZE bytes, since we require an active section to be active at a time.

	if (!MMCommit(CC_ACTIVE_SECTION_SIZE, true)) {
		return ES_ERROR_INSUFFICIENT_RESOURCES;
	}

	EsDefer(MMDecommit(CC_ACTIVE_SECTION_SIZE, true));

	K_USER_BUFFER uint8_t *buffer = (uint8_t *) _buffer;

	EsFileOffset firstSection = RoundDown(offset, CC_ACTIVE_SECTION_SIZE),
		      lastSection = RoundUp(offset + count, CC_ACTIVE_SECTION_SIZE);

	uintptr_t guessedActiveSectionIndex = 0;

	bool writeBack = (flags & CC_ACCESS_WRITE_BACK) && (~flags & CC_ACCESS_PRECISE);
	bool preciseWriteBack = (flags & CC_ACCESS_WRITE_BACK) && (flags & CC_ACCESS_PRECISE);

	for (EsFileOffset sectionOffset = firstSection; sectionOffset < lastSection; sectionOffset += CC_ACTIVE_SECTION_SIZE) {
		if (MM_AVAILABLE_PAGES() < MM_CRITICAL_AVAILABLE_PAGES_THRESHOLD && !GetCurrentThread()->isPageGenerator) {
			KernelLog(LOG_ERROR, "Memory", "waiting for non-critical state", "File cache read on non-generator thread, waiting for more available pages.\n");
			KEventWait(&pmm.availableNotCritical);
		}

		EsFileOffset start = sectionOffset < offset ? offset - sectionOffset : 0;
		EsFileOffset   end = sectionOffset + CC_ACTIVE_SECTION_SIZE > offset + count ? offset + count - sectionOffset : CC_ACTIVE_SECTION_SIZE;

		EsFileOffset pageStart = RoundDown(start, K_PAGE_SIZE) / K_PAGE_SIZE;
		EsFileOffset   pageEnd =   RoundUp(end,   K_PAGE_SIZE) / K_PAGE_SIZE;

		// Find the section in the active sections list.

		KMutexAcquire(&cache->activeSectionsMutex);

		bool found = false;
		uintptr_t index = 0;

		if (guessedActiveSectionIndex < cache->activeSections.Length()
				&& cache->activeSections[guessedActiveSectionIndex].offset == sectionOffset) {
			index = guessedActiveSectionIndex;
			found = true;
		}

		if (!found && cache->activeSections.Length()) {
			intptr_t low = 0, high = cache->activeSections.Length() - 1;

			while (low <= high) {
				intptr_t i = low + (high - low) / 2;

				if (cache->activeSections[i].offset < sectionOffset) {
					low = i + 1;
				} else if (cache->activeSections[i].offset > sectionOffset) {
					high = i - 1;
				} else {
					index = i;
					found = true;
					break;
				}
			}

			if (!found) {
				index = low;
				if (high + 1 != low) KernelPanic("CCSpaceAccess - Bad binary search.\n");
			}
		}

		if (found) {
			guessedActiveSectionIndex = index + 1;
		}

		KMutexAcquire(&activeSectionManager.mutex);

		CCActiveSection *section;

		// Replace active section in list if it has been used for something else.

		bool replace = false;

		if (found) {
			CCActiveSection *section = activeSectionManager.sections + cache->activeSections[index].index;

			if (section->cache != cache || section->offset != sectionOffset) {
				replace = true, found = false;
			}
		}

		if (!found) {
			// Allocate a new active section.

			if (!activeSectionManager.lruList.count) {
				KMutexRelease(&activeSectionManager.mutex);
				KMutexRelease(&cache->activeSectionsMutex);
				return ES_ERROR_INSUFFICIENT_RESOURCES;
			}

			section = activeSectionManager.lruList.firstItem->thisItem;

			// Add it to the file cache's list of active sections.

			CCActiveSectionReference reference = { .offset = sectionOffset, .index = (uintptr_t) (section - activeSectionManager.sections) };

			if (replace) {
				cache->activeSections[index] = reference;
			} else {
				if (!cache->activeSections.Insert(reference, index)) {
					KMutexRelease(&activeSectionManager.mutex);
					KMutexRelease(&cache->activeSectionsMutex);
					return ES_ERROR_INSUFFICIENT_RESOURCES;
				}
			}

			if (section->cache) {
				// Dereference pages.
					
				if (section->accessors) {
					KernelPanic("CCSpaceAccess - Attempting to dereference active section %x with %d accessors.\n", 
							section, section->accessors);
				}

				CCDereferenceActiveSection(section);

				// Uncover the section's previous contents.

				KMutexAcquire(&section->cache->cachedSectionsMutex);
				CCSpaceUncover(section->cache, section->offset, section->offset + CC_ACTIVE_SECTION_SIZE);
				KMutexRelease(&section->cache->cachedSectionsMutex);

				section->cache = nullptr;
			}

			// Make sure there are cached sections covering the region of the active section.

			KMutexAcquire(&cache->cachedSectionsMutex);

			if (!CCSpaceCover(cache, sectionOffset, sectionOffset + CC_ACTIVE_SECTION_SIZE)) {
				KMutexRelease(&cache->cachedSectionsMutex);
				cache->activeSections.Delete(index);
				KMutexRelease(&activeSectionManager.mutex);
				KMutexRelease(&cache->activeSectionsMutex);
				return ES_ERROR_INSUFFICIENT_RESOURCES;
			}

			KMutexRelease(&cache->cachedSectionsMutex);

			// Remove it from the LRU list.

			activeSectionManager.lruList.Remove(activeSectionManager.lruList.firstItem);

			// Setup the section.

			if (section->accessors) KernelPanic("CCSpaceAccess - Active section %x in the LRU list had accessors.\n", section);
			if (section->loading) KernelPanic("CCSpaceAccess - Active section %x in the LRU list was loading.\n", section);

			section->accessors = 1;
			section->offset = sectionOffset;
			section->cache = cache;

#if 0
			{
				Node *node = EsContainerOf(Node, file.cache, cache);
				EsPrint("active section %d: %s, %x\n", reference.index, node->nameBytes, node->nameBuffer, section->offset);
			}
#endif

#ifdef DEBUG_BUILD
			for (uintptr_t i = 1; i < cache->activeSections.Length(); i++) {
				if (cache->activeSections[i - 1].offset >= cache->activeSections[i].offset) {
					KernelPanic("CCSpaceAccess - Active sections list in cache %x unordered.\n", cache);
				}
			}
#endif
		} else {
			// Remove the active section from the LRU/modified list, if present, 
			// and increment the accessors count.
			// Don't bother keeping track of its place in the modified list.

			section = activeSectionManager.sections + cache->activeSections[index].index;

			if (!section->accessors) {
				if (section->writing) KernelPanic("CCSpaceAccess - Active section %x in list is being written.\n", section);
				section->listItem.RemoveFromList();
			} else if (section->listItem.list) {
				KernelPanic("CCSpaceAccess - Active section %x in list had accessors (2).\n", section);
			}

			section->accessors++;
		}

		KMutexRelease(&activeSectionManager.mutex);
		KMutexRelease(&cache->activeSectionsMutex);

		if ((flags & CC_ACCESS_WRITE) && section->writing) {
			// If writing, wait for any in progress write-behinds to complete.
			// Note that, once this event is set, a new write can't be started until accessors is 0.
		
			KEventWait(&section->writeCompleteEvent);
		}

		uint8_t *sectionBase = activeSectionManager.baseAddress + (section - activeSectionManager.sections) * CC_ACTIVE_SECTION_SIZE;

		// Check if all the pages are already referenced (and hence loaded and mapped).

		bool allReferenced = true;

		for (uintptr_t i = pageStart; i < pageEnd; i++) {
			if (~section->referencedPages[i >> 3] & (1 << (i & 7))) {
				allReferenced = false;
				break;
			}
		}

		uint8_t alreadyWritten[CC_ACTIVE_SECTION_SIZE / K_PAGE_SIZE / 8] = {};

		if (allReferenced) {
			goto copy;
		}

		while (true) {
			KMutexAcquire(&cache->cachedSectionsMutex);

			// Find the first cached section covering this active section.

			CCCachedSection *cachedSection = CCFindCachedSectionContaining(cache, sectionOffset);

			if (!cachedSection) {
				KernelPanic("CCSpaceAccess - Active section %x not covered.\n", section);
			}

			// Find where the requested region is located.

			uintptr_t pagesToSkip = pageStart + (sectionOffset - cachedSection->offset) / K_PAGE_SIZE,
				  pageInCachedSectionIndex = 0;

			while (pagesToSkip) {
				if (pagesToSkip >= cachedSection->pageCount) {
					pagesToSkip -= cachedSection->pageCount;
					cachedSection++;
				} else {
					pageInCachedSectionIndex = pagesToSkip;
					pagesToSkip = 0;
				}
			}

			if (pageInCachedSectionIndex >= cachedSection->pageCount 
					|| cachedSection >= cache->cachedSections.array + cache->cachedSections.Length()) {
				KernelPanic("CCSpaceAccess - Invalid requested region search result.\n");
			}

			// Reference all loaded pages, and record the ones we need to load.

			uintptr_t *pagesToLoad[CC_ACTIVE_SECTION_SIZE / K_PAGE_SIZE];
			uintptr_t loadCount = 0;

			for (uintptr_t i = pageStart; i < pageEnd; i++) {
				if (cachedSection == cache->cachedSections.array + cache->cachedSections.Length()) {
					KernelPanic("CCSpaceAccess - Not enough cached sections.\n");
				}

				KMutexAcquire(&pmm.pageFrameMutex);

				uintptr_t entry = cachedSection->data[pageInCachedSectionIndex];
				pagesToLoad[i] = nullptr;

				if ((entry & MM_SHARED_ENTRY_PRESENT) && (~section->referencedPages[i >> 3] & (1 << (i & 7)))) {
					MMPageFrame *frame = pmm.pageFrames + (entry >> K_PAGE_BITS);

					if (frame->state == MMPageFrame::STANDBY) {
						// The page was mapped out from all MMSpaces, and therefore was placed on the standby list.
						// Mark the page as active before we map it.
						MMPhysicalActivatePages(entry / K_PAGE_SIZE, 1, ES_FLAGS_DEFAULT);
						frame->cacheReference = cachedSection->data + pageInCachedSectionIndex;
					} else if (!frame->active.references) {
						KernelPanic("CCSpaceAccess - Active page frame %x had no references.\n", frame);
					}

					frame->active.references++;

					MMArchMapPage(kernelMMSpace, entry & ~(K_PAGE_SIZE - 1), (uintptr_t) sectionBase + i * K_PAGE_SIZE, MM_MAP_PAGE_FRAME_LOCK_ACQUIRED);
					__sync_synchronize();
					section->referencedPages[i >> 3] |= 1 << (i & 7);
					section->referencedPageCount++;
				} else if (~entry & MM_SHARED_ENTRY_PRESENT) {
					if (section->referencedPages[i >> 3] & (1 << (i & 7))) {
						KernelPanic("CCSpaceAccess - Referenced page was not present.\n");
					}

					pagesToLoad[i] = cachedSection->data + pageInCachedSectionIndex;
					loadCount++;
				}

				KMutexRelease(&pmm.pageFrameMutex);

				pageInCachedSectionIndex++;

				if (pageInCachedSectionIndex == cachedSection->pageCount) {
					pageInCachedSectionIndex = 0;
					cachedSection++;
				}
			}

			if (!loadCount) {
				KMutexRelease(&cache->cachedSectionsMutex);
				goto copy;
			}

			// If another thread is already trying to load pages into the active section,
			// then wait for it to complete.

			bool loadCollision = section->loading;

			if (!loadCollision) {
				section->loading = true;
				KEventReset(&section->loadCompleteEvent);
			}

			KMutexRelease(&cache->cachedSectionsMutex);

			if (loadCollision) {
				KEventWait(&section->loadCompleteEvent);
				continue;
			}

			// Allocate, reference and map physical pages.

			uintptr_t pageFrames[CC_ACTIVE_SECTION_SIZE / K_PAGE_SIZE];

			for (uintptr_t i = pageStart; i < pageEnd; i++) {
				if (!pagesToLoad[i]) {
					continue;
				}

				pageFrames[i] = MMPhysicalAllocate(ES_FLAGS_DEFAULT);

				MMPageFrame *frame = pmm.pageFrames + (pageFrames[i] >> K_PAGE_BITS);
				frame->active.references = 1;
				frame->cacheReference = pagesToLoad[i];

				MMArchMapPage(kernelMMSpace, pageFrames[i], (uintptr_t) sectionBase + i * K_PAGE_SIZE, ES_FLAGS_DEFAULT);
			}

			// Read from the cache's backing store.

			EsError error = ES_SUCCESS;

			if ((flags & CC_ACCESS_WRITE) && (~flags & CC_ACCESS_USER_BUFFER_MAPPED)) {
				bool loadedStart = false;

				if (error == ES_SUCCESS && (start & (K_PAGE_SIZE - 1)) && pagesToLoad[pageStart]) {
					// Left side of the accessed region is not page aligned, so we need to load in the page.

					error = cache->callbacks->readInto(cache, sectionBase + pageStart * K_PAGE_SIZE, 
							section->offset + pageStart * K_PAGE_SIZE, K_PAGE_SIZE);
					loadedStart = true;
				}

				if (error == ES_SUCCESS && (end & (K_PAGE_SIZE - 1)) && !(pageStart == pageEnd - 1 && loadedStart) && pagesToLoad[pageEnd - 1]) {
					// Right side of the accessed region is not page aligned, so we need to load in the page.

					error = cache->callbacks->readInto(cache, sectionBase + (pageEnd - 1) * K_PAGE_SIZE, 
							section->offset + (pageEnd - 1) * K_PAGE_SIZE, K_PAGE_SIZE);
				}

				K_USER_BUFFER uint8_t *buffer2 = buffer;

				// Initialise the rest of the contents HERE, before referencing the pages.
				// The user buffer cannot be mapped otherwise we could deadlock while reading from it,
				// as we have marked the active section in the loading state.
				
				for (uintptr_t i = pageStart; i < pageEnd; i++) {
					uintptr_t left = i == pageStart ? (start & (K_PAGE_SIZE - 1)) : 0;
					uintptr_t right = i == pageEnd - 1 ? (end & (K_PAGE_SIZE - 1)) : K_PAGE_SIZE;
					if (!right) right = K_PAGE_SIZE;

					if (pagesToLoad[i]) {
						EsMemoryCopy(sectionBase + i * K_PAGE_SIZE + left, buffer2, right - left);
						alreadyWritten[i >> 3] |= 1 << (i & 7);
					}

					buffer2 += right - left;
				}

				if (buffer + (end - start) != buffer2) {
					KernelPanic("CCSpaceAccess - Incorrect page left/right calculation.\n");
				}
			} else {
				for (uintptr_t i = pageStart; i < pageEnd; i++) {
					uintptr_t from = i, count = 0;

					while (i != pageEnd && pagesToLoad[i]) {
						count++, i++;
					}

					if (!count) continue;

					error = cache->callbacks->readInto(cache, sectionBase + from * K_PAGE_SIZE, 
							section->offset + from * K_PAGE_SIZE, count * K_PAGE_SIZE);

					if (error != ES_SUCCESS) {
						break;
					}
				}
			}

			if (error != ES_SUCCESS) {
				// Free and unmap the pages we allocated if there was an error.

				for (uintptr_t i = pageStart; i < pageEnd; i++) {
					if (!pagesToLoad[i]) continue;
					MMArchUnmapPages(kernelMMSpace, (uintptr_t) sectionBase + i * K_PAGE_SIZE, 1, ES_FLAGS_DEFAULT);
					MMPhysicalFree(pageFrames[i], false, 1);
				}
			}

			KMutexAcquire(&cache->cachedSectionsMutex);

			// Write the pages to the cached sections, and mark them as referenced.

			if (error == ES_SUCCESS) {
				for (uintptr_t i = pageStart; i < pageEnd; i++) {
					if (pagesToLoad[i]) {
						*pagesToLoad[i] = pageFrames[i] | MM_SHARED_ENTRY_PRESENT;
						section->referencedPages[i >> 3] |= 1 << (i & 7);
						section->referencedPageCount++;
					}
				}
			}

			// Return active section to normal state, and set the load complete event.

			section->loading = false;
			KEventSet(&section->loadCompleteEvent);

			KMutexRelease(&cache->cachedSectionsMutex);

			if (error != ES_SUCCESS) {
				return error;
			}

			break;
		}

		copy:;

		// Copy into/from the user's buffer.

		if (buffer) {
			if (flags & CC_ACCESS_MAP) {
				if ((start & (K_PAGE_SIZE - 1)) || (end & (K_PAGE_SIZE - 1)) || ((uintptr_t) buffer & (K_PAGE_SIZE - 1))) {
					KernelPanic("CCSpaceAccess - Passed READ_MAP flag, but start/end/buffer misaligned.\n");
				}

				for (uintptr_t i = start; i < end; i += K_PAGE_SIZE) {
					uintptr_t physicalAddress = MMArchTranslateAddress(kernelMMSpace, (uintptr_t) sectionBase + i, false);
					KMutexAcquire(&pmm.pageFrameMutex);
					pmm.pageFrames[physicalAddress / K_PAGE_SIZE].active.references++;
					KMutexRelease(&pmm.pageFrameMutex);
					MMArchMapPage(mapSpace, physicalAddress, (uintptr_t) buffer, 
							mapFlags | MM_MAP_PAGE_IGNORE_IF_MAPPED /* since this isn't locked */);
					buffer += K_PAGE_SIZE;
				}
			} else if (flags & CC_ACCESS_READ) {
				EsMemoryCopy(buffer, sectionBase + start, end - start);
				buffer += end - start;
			} else if (flags & CC_ACCESS_WRITE) {
				for (uintptr_t i = pageStart; i < pageEnd; i++) {
					uintptr_t left = i == pageStart ? (start & (K_PAGE_SIZE - 1)) : 0;
					uintptr_t right = i == pageEnd - 1 ? (end & (K_PAGE_SIZE - 1)) : K_PAGE_SIZE;
					if (!right) right = K_PAGE_SIZE;

					if (~alreadyWritten[i >> 3] & (1 << (i & 7))) {
						EsMemoryCopy(sectionBase + i * K_PAGE_SIZE + left, buffer, right - left);
					}

					buffer += right - left;

					if (!preciseWriteBack) {
						__sync_fetch_and_or(section->modifiedPages + (i >> 3), 1 << (i & 7));
					}
				}

				if (!preciseWriteBack) {
					section->modified = true;
				} else {
					uint8_t *sectionBase = activeSectionManager.baseAddress + (section - activeSectionManager.sections) * CC_ACTIVE_SECTION_SIZE;
					EsError error = section->cache->callbacks->writeFrom(section->cache, sectionBase + start, section->offset + start, end - start);

					if (error != ES_SUCCESS) {
						CCActiveSectionReturnToLists(section, writeBack);
						return error;
					}
				}
			}
		}

		CCActiveSectionReturnToLists(section, writeBack);
	}

	return ES_SUCCESS;
}

void CCWriteBehindThread() {
	while (true) {
		// Wait for an active section to be modified, and have no accessors.

		KEventWait(&activeSectionManager.modifiedNonEmpty);

		if (MM_AVAILABLE_PAGES() > MM_LOW_AVAILABLE_PAGES_THRESHOLD && !scheduler.shutdown) {
			// If there are sufficient available pages, wait before we start writing sections.
			KEventWait(&pmm.availableLow, CC_WAIT_FOR_WRITE_BEHIND);
		}

		while (true) {
			// Take a section, and mark it as being written.

			CCActiveSection *section = nullptr;
			KMutexAcquire(&activeSectionManager.mutex);

			if (activeSectionManager.modifiedList.count) {
				section = activeSectionManager.modifiedList.firstItem->thisItem;
				CCWriteSectionPrepare(section);
			}

			KEventSet(&activeSectionManager.modifiedNonFull, false, true);
			KMutexRelease(&activeSectionManager.mutex);

			if (!section) {
				break;
			} else {
				CCWriteSection(section);
			}
		}
	}
}

void CCInitialise() {
	activeSectionManager.sectionCount = CC_SECTION_BYTES / CC_ACTIVE_SECTION_SIZE;
	activeSectionManager.sections = (CCActiveSection *) EsHeapAllocate(activeSectionManager.sectionCount * sizeof(CCActiveSection), true, K_FIXED);

	KMutexAcquire(&kernelMMSpace->reserveMutex);
	activeSectionManager.baseAddress = (uint8_t *) MMReserve(kernelMMSpace, activeSectionManager.sectionCount * CC_ACTIVE_SECTION_SIZE, MM_REGION_CACHE)->baseAddress;
	KMutexRelease(&kernelMMSpace->reserveMutex);

	for (uintptr_t i = 0; i < activeSectionManager.sectionCount; i++) {
		activeSectionManager.sections[i].listItem.thisItem = &activeSectionManager.sections[i];
		activeSectionManager.lruList.InsertEnd(&activeSectionManager.sections[i].listItem);
	}

	KernelLog(LOG_INFO, "Memory", "cache initialised", "MMInitialise - Active section manager initialised with a maximum of %d of entries.\n", 
			activeSectionManager.sectionCount);

	activeSectionManager.modifiedNonEmpty.autoReset = true;
	KEventSet(&activeSectionManager.modifiedNonFull);
	activeSectionManager.writeBackThread = scheduler.SpawnThread("CCWriteBehind", (uintptr_t) CCWriteBehindThread, 0, ES_FLAGS_DEFAULT);
	activeSectionManager.writeBackThread->isPageGenerator = true;
}

#endif
