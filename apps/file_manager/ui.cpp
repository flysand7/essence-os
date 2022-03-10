// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

// TODO Custom columns.

void InstanceFolderPathChanged(Instance *instance, bool fromLoadFolder) {
	if (fromLoadFolder) {
		// Don't free the old path; it's used in the history stack.
	} else {
		StringDestroy(&instance->path);
	}

	instance->path = StringDuplicate(instance->folder->path); 
	EsTextboxSelectAll(instance->breadcrumbBar);
	EsTextboxInsert(instance->breadcrumbBar, STRING(instance->path));

	uint8_t _buffer[256];
	EsBuffer buffer = { .out = _buffer, .bytes = sizeof(_buffer) };
	instance->folder->containerHandler->getVisibleName(&buffer, instance->path);
	EsWindowSetTitle(instance->window, (char *) buffer.out, buffer.position);
	EsWindowSetIcon(instance->window, knownFileTypes[instance->folder->containerHandler->getFileType(instance->path)].iconID);

	size_t itemCount;
	EsListViewEnumeratedVisibleItem *items = EsListViewEnumerateVisibleItems(instance->list, &itemCount);
	for (uintptr_t i = 0; i < itemCount; i++) ListItemCreated(items[i].element, items[i].index, true);
	EsHeapFree(items);
}

bool InstanceLoadFolder(Instance *instance, String path /* takes ownership */, int historyMode) {
	if (instance->folder && !instance->folder->refreshing && StringEquals(path, instance->folder->path)) {
		// The path hasn't changed; ignore.
		StringDestroy(&path);
		return true;
	}

	instance->issuedPasteTask = nullptr;

	Task task = {};
	task.context = historyMode;
	task.cDescription = interfaceString_FileManagerOpenFolderTask;

	EsListViewIndex focusedIndex;

	if (EsListViewGetFocusedItem(instance->list, nullptr, &focusedIndex)) {
		String name = instance->listContents[focusedIndex].entry->GetName();
		task.string = StringDuplicate(name);
	}

	InstanceRemoveContents(instance);
	FolderAttachInstance(instance, path, false);
	StringDestroy(&path);

	task.callback = [] (Instance *instance, Task *) {
		Folder *folder = instance->folder;
		EsMutexAcquire(&folder->modifyEntriesMutex);

		if (!folder->doneInitialEnumeration) {
			// TODO Reporting errors.
			folder->itemHandler->enumerate(folder);

			if (folder->containerHandler->getTotalSize) {
				folder->containerHandler->getTotalSize(folder);
			}

			folder->driveRemoved = false;
			folder->refreshing = false;
			folder->doneInitialEnumeration = true;
		}

		EsMutexRelease(&folder->modifyEntriesMutex);
	};

	task.then = [] (Instance *instance, Task *task) {
		// TODO Check if folder was marked for refresh.

		if (instance->closed) {
			return;
		}

		int historyMode = task->context.i & 0xFF;
		int flags = task->context.i;
		Folder *folder = instance->folder;
		folder->attachedInstances.Add(instance);

		// Add the path to the history array.

		HistoryEntry historyEntry = {};
		historyEntry.path = instance->path;
		historyEntry.focusedItem = task->string;

		if (historyMode == LOAD_FOLDER_BACK) {
			instance->pathForwardHistory.Add(historyEntry);
		} else if (historyMode == LOAD_FOLDER_FORWARD) {
			instance->pathBackwardHistory.Add(historyEntry);
		} else if (historyMode == LOAD_FOLDER_START || historyMode == LOAD_FOLDER_REFRESH) {
		} else {
			instance->pathBackwardHistory.Add(historyEntry);

			for (int i = 0; i < (int) instance->pathForwardHistory.Length(); i++) {
				StringDestroy(&instance->pathForwardHistory[i].path);
				StringDestroy(&instance->pathForwardHistory[i].focusedItem);
			}

			instance->pathForwardHistory.SetLength(0);
		}

		// Update commands.

		EsCommandSetEnabled(&instance->commandGoBackwards, instance->pathBackwardHistory.Length());
		EsCommandSetEnabled(&instance->commandGoForwards, instance->pathForwardHistory.Length());
		EsCommandSetEnabled(&instance->commandGoParent, PathCountSections(folder->path) > 1);
		EsCommandSetEnabled(&instance->commandNewFolder, folder->itemHandler->createChildFolder && !folder->readOnly);
		EsCommandSetEnabled(&instance->commandRename, false);
		EsCommandSetEnabled(&instance->commandRefresh, true);

		// Load the view settings for the folder.
		// If the folder does not have any settings, inherit from closest ancestor with settings.

		bool foundViewSettings = false;
		size_t lastMatchBytes = 0;
		ptrdiff_t updateLRU = -1;

		if (folder->path.bytes < sizeof(folderViewSettings[0].path)) {
			for (uintptr_t i = 0; i < folderViewSettings.Length(); i++) {
				String path = StringFromLiteralWithSize(folderViewSettings[i].path, folderViewSettings[i].pathBytes);
				bool matchFull = StringEquals(path, folder->path);
				bool matchPartial = matchFull || PathHasPrefix(folder->path, path);

				if (matchFull || (matchPartial && lastMatchBytes < path.bytes)) {
					foundViewSettings = true;
					instance->viewSettings = folderViewSettings[i].settings;
					updateLRU = i;
					if (matchFull) break;
					else lastMatchBytes = path.bytes; // Keep looking for a closer ancestor.
				}
			}
		}

		if (updateLRU != -1) {
			FolderViewSettingsEntry entry = folderViewSettings[updateLRU];
			folderViewSettings.Delete(updateLRU);
			folderViewSettings.Add(entry);
		}

		if (!foundViewSettings) {
			if (folder->itemHandler->getDefaultViewSettings) {
				folder->itemHandler->getDefaultViewSettings(folder, &instance->viewSettings);
			} else {
				// TODO Get default from configuration.
				instance->viewSettings.sortColumn = COLUMN_NAME;
				instance->viewSettings.viewType = VIEW_TILES;
			}
		}

		InstanceRefreshViewType(instance, false);

		// Update the user interface.

		EsListViewSetEmptyMessage(instance->list, folder->cEmptyMessage);
		InstanceAddContents(instance, &folder->entries);
		InstanceFolderPathChanged(instance, true);
		EsListViewInvalidateAll(instance->placesView);
		if (~flags & LOAD_FOLDER_NO_FOCUS) EsElementFocus(instance->list);
	};

	BlockingTaskQueue(instance, task);
	return true;
}

void InstanceRefreshViewType(Instance *instance, bool startTransition) {
	if (startTransition) {
		EsElementStartTransition(instance->list, ES_TRANSITION_FADE, ES_ELEMENT_TRANSITION_CONTENT_ONLY, 1.0f);
	}

	EsCommandSetCheck(&instance->commandViewDetails,    instance->viewSettings.viewType == VIEW_DETAILS    ? ES_CHECK_CHECKED : ES_CHECK_UNCHECKED, false);
	EsCommandSetCheck(&instance->commandViewTiles,      instance->viewSettings.viewType == VIEW_TILES      ? ES_CHECK_CHECKED : ES_CHECK_UNCHECKED, false);
	EsCommandSetCheck(&instance->commandViewThumbnails, instance->viewSettings.viewType == VIEW_THUMBNAILS ? ES_CHECK_CHECKED : ES_CHECK_UNCHECKED, false);

	if (instance->viewSettings.viewType == VIEW_DETAILS) {
		EsListViewChangeStyles(instance->list, EsStyleIntern(&styleFolderView), ES_STYLE_LIST_ITEM, 0, 0, ES_LIST_VIEW_COLUMNS, ES_LIST_VIEW_TILED);
		EsListViewAddAllColumns(instance->list);
	} else if (instance->viewSettings.viewType == VIEW_TILES) {
		EsListViewChangeStyles(instance->list, EsStyleIntern(&styleFolderViewTiled), ES_STYLE_LIST_ITEM_TILE, 0, 0, ES_LIST_VIEW_TILED, ES_LIST_VIEW_COLUMNS);
	} else if (instance->viewSettings.viewType == VIEW_THUMBNAILS) {
		EsListViewChangeStyles(instance->list, EsStyleIntern(&styleFolderViewTiled), EsStyleIntern(&styleFolderItemThumbnail), 0, 0, ES_LIST_VIEW_TILED, ES_LIST_VIEW_COLUMNS);
	}
}

void InstanceRemoveItemSelectionCommands(Instance *instance) {
	EsCommandSetCallback(EsCommandByID(instance, ES_COMMAND_CUT), nullptr);
	EsCommandSetCallback(EsCommandByID(instance, ES_COMMAND_COPY), nullptr);
	EsCommandSetCallback(EsCommandByID(instance, ES_COMMAND_PASTE), nullptr);
}

void InstanceUpdateItemSelectionCountCommands(Instance *instance) {
	EsCommandSetEnabled(&instance->commandRename, instance->selectedItemCount == 1 && instance->folder->itemHandler->renameItem && !instance->folder->readOnly);

#define COMMAND_SET(id, callback, enabled) \
	do { EsCommand *command = EsCommandByID(instance, id); \
	EsCommandSetEnabled(command, enabled); \
	EsCommandSetCallback(command, callback); } while(0)

	if (EsElementIsFocused(instance->list)) {
		COMMAND_SET(ES_COMMAND_CUT, CommandCut, instance->selectedItemCount >= 1 && instance->folder->itemHandler->canCut && !instance->folder->readOnly);
		COMMAND_SET(ES_COMMAND_COPY, CommandCopy, instance->selectedItemCount >= 1 && instance->folder->itemHandler->canCopy);
		COMMAND_SET(ES_COMMAND_PASTE, CommandPaste, instance->folder->itemHandler->canPaste && EsClipboardHasData(ES_CLIPBOARD_PRIMARY) && !instance->folder->readOnly);
	}
}

int InstanceCompareFolderEntries(FolderEntry *left, FolderEntry *right, uint16_t sortColumn) {
	bool descending = sortColumn & (1 << 8);
	sortColumn &= 0xFF;

	int result = 0;

	if (!left->isFolder && right->isFolder) {
		result = 1;
	} else if (!right->isFolder && left->isFolder) {
		result = -1;
	} else {
		if (sortColumn == COLUMN_NAME) {
			result = EsStringCompare(STRING(left->GetName()), STRING(right->GetName()));
		} else if (sortColumn == COLUMN_TYPE) {
			result = EsStringCompare(STRING(left->GetExtension()), STRING(right->GetExtension()));
		} else if (sortColumn == COLUMN_SIZE) {
			if (right->size < left->size) result = 1;
			else if (right->size > left->size) result = -1;
			else result = 0;
		}

		if (!result && sortColumn != COLUMN_NAME) {
			result = EsStringCompare(STRING(left->GetName()), STRING(right->GetName()));
		}

		if (!result) {
			if ((uintptr_t) right > (uintptr_t) left) result = 1;
			else if ((uintptr_t) right < (uintptr_t) left) result = -1;
			else result = 0;
		}
	}

	return descending ? -result : result;
}

bool InstanceAddInternal(Instance *instance, ListEntry *entry) {
	// TODO Filtering.

	entry->entry->handles++;

	if (entry->selected) {
		instance->selectedItemCount++;
		instance->selectedItemsTotalSize += entry->entry->size;
		InstanceUpdateItemSelectionCountCommands(instance);
	}

	return true;
}

void InstanceRemoveInternal(Instance *instance, ListEntry *entry) {
	FolderEntryCloseHandle(instance->folder, entry->entry);

	if (entry->selected) {
		instance->selectedItemCount--;
		instance->selectedItemsTotalSize -= entry->entry->size;
		InstanceUpdateItemSelectionCountCommands(instance);
	}
}

void InstanceUpdateStatusString(Instance *instance) {
	// TODO Localization.

	char buffer[1024];
	size_t bytes;

	size_t itemCount = instance->listContents.Length();

	if (instance->selectedItemCount) {
		bytes = EsStringFormat(buffer, sizeof(buffer), "Selected %d item%z (%D)", 
				instance->selectedItemCount, instance->selectedItemCount == 1 ? "" : "s", instance->selectedItemsTotalSize);
	} else {
		if (itemCount) {
			if (instance->folder->spaceUsed) {
				if (instance->folder->spaceTotal) {
					bytes = EsStringFormat(buffer, sizeof(buffer), "%d item%z " HYPHENATION_POINT " %D out of %D used", 
							itemCount, itemCount == 1 ? "" : "s", instance->folder->spaceUsed, instance->folder->spaceTotal);
				} else {
					bytes = EsStringFormat(buffer, sizeof(buffer), "%d item%z (%D)", 
							itemCount, itemCount == 1 ? "" : "s", instance->folder->spaceUsed);
				}
			} else {
				bytes = EsStringFormat(buffer, sizeof(buffer), "%d item%z", 
						itemCount, itemCount == 1 ? "" : "s");
			}
		} else {
			if (instance->folder && instance->folder->itemHandler->type == NAMESPACE_HANDLER_INVALID) {
				bytes = EsStringFormat(buffer, sizeof(buffer), "Invalid path");
			} else {
				bytes = EsStringFormat(buffer, sizeof(buffer), "Empty folder");
			}
		}
	}

	EsTextDisplaySetContents(instance->status, buffer, bytes);
}

void InstanceAddSingle(Instance *instance, ListEntry entry) {
	// Call with the message mutex acquired.

	if (!InstanceAddInternal(instance, &entry)) {
		return; // Filtered out.
	}

	uintptr_t low = 0, high = instance->listContents.Length();

	while (low < high) {
		uintptr_t middle = (low + high) / 2;
		int compare = InstanceCompareFolderEntries(instance->listContents[middle].entry, entry.entry, instance->viewSettings.sortColumn);

		if (compare == 0) {
			// The entry is already in the list.

			EsListViewInvalidateContent(instance->list, 0, middle);
			InstanceRemoveInternal(instance, &entry);

			ListEntry *existingEntry = &instance->listContents[middle];

			if (existingEntry->selected) {
				instance->selectedItemsTotalSize += existingEntry->entry->size - existingEntry->entry->previousSize;
				InstanceUpdateStatusString(instance);
			}

			return; 
		} else if (compare > 0) {
			high = middle;
		} else {
			low = middle + 1;
		}
	}

	instance->listContents.Insert(entry, low);
	EsListViewInsert(instance->list, 0, low, 1);

	if (entry.selected) {
		EsListViewSelect(instance->list, 0, low);
		EsListViewFocusItem(instance->list, 0, low);
	}

	InstanceUpdateStatusString(instance);
}

ES_MACRO_SORT(InstanceSortListContents, ListEntry, {
	result = InstanceCompareFolderEntries(_left->entry, _right->entry, context);
}, uint16_t);

void InstanceSelectByName(Instance *instance, String name, bool addToExistingSelection, bool focusItem) {
	for (uintptr_t i = 0; i < instance->listContents.Length(); i++) {
		if (0 == EsStringCompareRaw(STRING(instance->listContents[i].entry->GetName()), STRING(name))) {
			EsListViewSelect(instance->list, 0, i, addToExistingSelection);
			if (focusItem) EsListViewFocusItem(instance->list, 0, i);
			break;
		}
	}
}

void InstanceAddContents(Instance *instance, HashTable *newEntries) {
	// Call with the message mutex acquired.

	size_t oldListEntryCount = instance->listContents.Length();

	for (uintptr_t i = 0; i < newEntries->slotCount; i++) {
		if (!newEntries->slots[i].key.used) {
			continue;
		}

		ListEntry entry = {};
		entry.entry = (FolderEntry *) newEntries->slots[i].value;

		if (InstanceAddInternal(instance, &entry)) {
			instance->listContents.Add(entry);
		}
	}

	if (oldListEntryCount) {
		EsListViewRemove(instance->list, 0, 0, oldListEntryCount);
	}

	InstanceSortListContents(instance->listContents.array, instance->listContents.Length(), instance->viewSettings.sortColumn);

	if (instance->listContents.Length()) {
		EsListViewInsert(instance->list, 0, 0, instance->listContents.Length());

		if (instance->delayedFocusItem.bytes) {
			InstanceSelectByName(instance, instance->delayedFocusItem, false, true);
			StringDestroy(&instance->delayedFocusItem);
		}
	}

	InstanceUpdateStatusString(instance);
}

ListEntry InstanceRemoveSingle(Instance *instance, FolderEntry *folderEntry) {
	uintptr_t low = 0, high = instance->listContents.Length();

	while (low <= high) {
		uintptr_t middle = (low + high) / 2;
		int compare = InstanceCompareFolderEntries(instance->listContents[middle].entry, folderEntry, instance->viewSettings.sortColumn);

		if (compare == 0) {
			ListEntry entry = instance->listContents[middle];
			InstanceRemoveInternal(instance, &entry);
			EsListViewRemove(instance->list, 0, middle, 1);
			instance->listContents.Delete(middle);
			InstanceUpdateStatusString(instance);
			return entry;
		} else if (compare > 0) {
			high = middle;
		} else {
			low = middle + 1;
		}
	}

	// It wasn't in the list.
	return {};
}

void InstanceRemoveContents(Instance *instance) {
	for (uintptr_t i = 0; i < instance->listContents.Length(); i++) {
		InstanceRemoveInternal(instance, &instance->listContents[i]);
	}

	EsAssert(instance->selectedItemCount == 0); // After removing all items none should be selected.

	if (instance->listContents.Length()) {
		EsListViewRemove(instance->list, 0, 0, instance->listContents.Length());
		EsListViewContentChanged(instance->list);
	}

	instance->listContents.Free();
	InstanceUpdateStatusString(instance);
}

ListEntry *InstanceGetSelectedListEntry(Instance *instance) {
	for (uintptr_t i = 0; i < instance->listContents.Length(); i++) {
		ListEntry *entry = &instance->listContents[i];

		if (entry->selected) {
			return entry;
		}
	}

	return nullptr;
}

void InstanceViewSettingsUpdated(Instance *instance) {
	if (!instance->folder) return; // We were called early in initialization of the instance; ignore.

	if (instance->path.bytes < sizeof(folderViewSettings[0].path)) {
		bool foundViewSettings = false;

		for (uintptr_t i = 0; i < folderViewSettings.Length(); i++) {
			if (folderViewSettings[i].pathBytes == instance->path.bytes
					&& 0 == EsMemoryCompare(folderViewSettings[i].path, STRING(instance->path))) {
				foundViewSettings = true;
				folderViewSettings[i].settings = instance->viewSettings;
				break;
			}
		}

		if (!foundViewSettings) {
			if (folderViewSettings.Length() == FOLDER_VIEW_SETTINGS_MAXIMUM_ENTRIES) {
				folderViewSettings.Delete(0);
			}

			FolderViewSettingsEntry *entry = folderViewSettings.Add();
			EsMemoryCopy(entry->path, STRING(instance->path));
			entry->pathBytes = instance->path.bytes;
			entry->settings = instance->viewSettings;
		}
	}

	ConfigurationSave();
}

void InstanceChangeSortColumn(EsMenu *menu, EsGeneric context) {
	Instance *instance = menu->instance;
	instance->viewSettings.sortColumn = context.u;
	InstanceViewSettingsUpdated(instance);
	InstanceSortListContents(instance->listContents.array, instance->listContents.Length(), instance->viewSettings.sortColumn);
	EsListViewContentChanged(instance->list);
}

ES_FUNCTION_OPTIMISE_O3
void ThumbnailResize(uint32_t *bits, uint32_t originalWidth, uint32_t originalHeight, uint32_t targetWidth, uint32_t targetHeight) {
	// NOTE Modifies the original bits!
	// NOTE It looks like this only gets vectorised in -O3.
	// TODO Move into the API.

	float cx = (float) originalWidth / targetWidth;
	float cy = (float) originalHeight / targetHeight;

	for (uint32_t i = 0; i < originalHeight; i++) {
		uint32_t *output = bits + i * originalWidth;
		uint32_t *input = output;

		for (uint32_t j = 0; j < targetWidth; j++) {
			uint32_t sumAlpha = 0, sumRed = 0, sumGreen = 0, sumBlue = 0;
			uint32_t count = (uint32_t) ((j + 1) * cx) - (uint32_t) (j * cx);

			for (uint32_t k = 0; k < count; k++, input++) {
				uint32_t pixel = *input;
				sumAlpha += (pixel >> 24) & 0xFF;
				sumRed   += (pixel >> 16) & 0xFF;
				sumGreen += (pixel >>  8) & 0xFF;
				sumBlue  += (pixel >>  0) & 0xFF;
			}

			sumAlpha /= count;
			sumRed   /= count;
			sumGreen /= count;
			sumBlue  /= count;

			*output = (sumAlpha << 24) | (sumRed << 16) | (sumGreen << 8) | (sumBlue << 0);
			output++;
		}
	}

	for (uint32_t i = 0; i < targetWidth; i++) {
		uint32_t *output = bits + i;
		uint32_t *input = output;

		for (uint32_t j = 0; j < targetHeight; j++) {
			uint32_t sumAlpha = 0, sumRed = 0, sumGreen = 0, sumBlue = 0;
			uint32_t count = (uint32_t) ((j + 1) * cy) - (uint32_t) (j * cy);

			for (uint32_t k = 0; k < count; k++, input += originalWidth) {
				uint32_t pixel = *input;
				sumAlpha += (pixel >> 24) & 0xFF;
				sumRed   += (pixel >> 16) & 0xFF;
				sumGreen += (pixel >>  8) & 0xFF;
				sumBlue  += (pixel >>  0) & 0xFF;
			}

			sumAlpha /= count;
			sumRed   /= count;
			sumGreen /= count;
			sumBlue  /= count;

			*output = (sumAlpha << 24) | (sumRed << 16) | (sumGreen << 8) | (sumBlue << 0);
			output += originalWidth;
		}
	}

	for (uint32_t i = 0; i < targetHeight; i++) {
		for (uint32_t j = 0; j < targetWidth; j++) {
			bits[i * targetWidth + j] = bits[i * originalWidth + j];
		}
	}
}

void ThumbnailGenerateTask(Instance *, Task *task) {
	EsMessageMutexAcquire();
	Thumbnail *thumbnail = thumbnailCache.Get(&task->id);
	bool cancelTask = !thumbnail || thumbnail->referenceCount == 0 || EsWorkIsExiting();
	EsMessageMutexRelease();

	if (cancelTask) {
		return; // There are no longer any list items visible for this file.
	}

	EsFileInformation information = EsFileOpen(STRING(task->string), ES_FILE_READ | ES_NODE_FAIL_IF_NOT_FOUND);

	if (ES_SUCCESS != information.error) {
		return; // The file could not be loaded.
	}

	if (information.size > 64 * 1024 * 1024) {
		EsHandleClose(information.handle);
		return; // The file is too large.
	}

	size_t fileBytes;
	void *file = EsFileReadAllFromHandle(information.handle, &fileBytes);
	EsHandleClose(information.handle);

	if (!file) {
		return; // The file could not be loaded.
	}

	// TODO Allow applications to register their own thumbnail generators.
	uint32_t originalWidth, originalHeight;
	uint32_t *originalBits = (uint32_t *) EsImageLoad(file, fileBytes, &originalWidth, &originalHeight, 4);
	EsHeapFree(file);

	if (!originalBits) {
		return; // The image could not be loaded.
	}

	// TODO Determine the best value for these constants -- maybe base it off the current UI scale factor?
	uint32_t thumbnailMaximumWidth = 143;
	uint32_t thumbnailMaximumHeight = 80;
	EsRectangle targetRectangle = EsRectangleFit(ES_RECT_2S(thumbnailMaximumWidth, thumbnailMaximumHeight), ES_RECT_2S(originalWidth, originalHeight), false);
	uint32_t targetWidth = ES_RECT_WIDTH(targetRectangle), targetHeight = ES_RECT_HEIGHT(targetRectangle);
	uint32_t *targetBits;

	if (targetWidth == originalWidth && targetHeight == originalHeight) {
		targetBits = originalBits;
	} else {
		ThumbnailResize(originalBits, originalWidth, originalHeight, targetWidth, targetHeight);
		targetBits = (uint32_t *) EsHeapReallocate(originalBits, targetWidth * targetHeight * 4, false);
	}

	EsMessageMutexAcquire();

	thumbnail = thumbnailCache.Get(&task->id);

	if (thumbnail) {
		if (thumbnail->bits) EsHeapFree(thumbnail->bits);
		thumbnail->bits = targetBits;
		thumbnail->width = targetWidth;
		thumbnail->height = targetHeight;
		// TODO Submit width/height properties.
	}

	EsMessageMutexRelease();
}

void ThumbnailGenerateTaskComplete(Instance *, Task *task) {
	Thumbnail *thumbnail = thumbnailCache.Get(&task->id);

	if (thumbnail) {
		thumbnail->generatingTasksInProgress--;
	}

	String parent = PathGetParent(task->string);

	for (uintptr_t i = 0; i < instances.Length(); i++) {
		if (!instances[i]->closed && StringEquals(parent, instances[i]->path)) { // TODO Support recursive views.
			EsListViewInvalidateAll(instances[i]->list);
		}
	}

	StringDestroy(&task->string);
}

void ThumbnailGenerateIfNeeded(Folder *folder, FolderEntry *entry, bool fromFolderRename, bool modified) {
	FileType *fileType = FolderEntryGetType(folder, entry);

	if (!fileType->hasThumbnailGenerator) {
		return; // The file type does not support thumbnail generation.
	}

	Thumbnail *thumbnail;

	// TODO Remove from LRU if needed.

	if (modified) {
		thumbnail = thumbnailCache.Get(&entry->id);

		if (!thumbnail || (!thumbnail->generatingTasksInProgress && !thumbnail->bits)) {
			return; // The thumbnail is not in use.
		}
	} else {
		thumbnail = thumbnailCache.Put(&entry->id);

		if (!fromFolderRename) {
			thumbnail->referenceCount++;
		}

		if ((thumbnail->generatingTasksInProgress && !fromFolderRename) || thumbnail->bits) {
			return; // The thumbnail is already being/has already been generated.
		}
	}

	thumbnail->generatingTasksInProgress++;

	String path = StringAllocateAndFormat("%s%s", STRFMT(folder->path), STRFMT(entry->GetInternalName()));

	Task task = {
		.string = path,
		.id = entry->id,
		.callback = ThumbnailGenerateTask,
		.then = ThumbnailGenerateTaskComplete,
	};

	NonBlockingTaskQueue(task);
}

void ListItemCreated(EsElement *element, uintptr_t index, bool fromFolderRename) {
	Instance *instance = element->instance;

	if (instance->viewSettings.viewType != VIEW_THUMBNAILS && instance->viewSettings.viewType != VIEW_TILES) {
		return; // The current view does not display thumbnails.
	}

	ThumbnailGenerateIfNeeded(instance->folder, instance->listContents[index].entry, fromFolderRename, false /* not modified */);
}

Thumbnail *ListItemGetThumbnail(EsElement *element) {
	Instance *instance = element->instance;
	ListEntry *entry = &instance->listContents[EsListViewGetIndexFromItem(element)];
	Thumbnail *thumbnail = thumbnailCache.Get(&entry->entry->id);
	return thumbnail;
}

int ListItemMessage(EsElement *element, EsMessage *message) {
	if (message->type == ES_MSG_DESTROY) {
		Thumbnail *thumbnail = ListItemGetThumbnail(element);

		if (thumbnail) {
			thumbnail->referenceCount--;

			// TODO When the referenceCount drops to 0, put it on a LRU list.
		}
	} else if (message->type == ES_MSG_PAINT_ICON) {
		if (element->instance->viewSettings.viewType == VIEW_THUMBNAILS || element->instance->viewSettings.viewType == VIEW_TILES) {
			Thumbnail *thumbnail = ListItemGetThumbnail(element);

			if (thumbnail && thumbnail->bits) {
				EsRectangle destination = EsPainterBoundsClient(message->painter);
				EsRectangle source = ES_RECT_2S(thumbnail->width, thumbnail->height);
				destination = EsRectangleFit(destination, source, true /* allow scaling up */);
				// EsDrawBlock(message->painter, EsRectangleAdd(destination, ES_RECT_1(2)), 0x20000000);
				EsDrawBitmapScaled(message->painter, destination, source, thumbnail->bits, thumbnail->width * 4, 0xFF);
				return ES_HANDLED;
			}
		}
	}

	return 0;
}

int ListCallback(EsElement *element, EsMessage *message) {
	Instance *instance = element->instance;

	if (message->type == ES_MSG_FOCUSED_START || message->type == ES_MSG_PRIMARY_CLIPBOARD_UPDATED) {
		InstanceUpdateItemSelectionCountCommands(instance);
		return 0;
	} else if (message->type == ES_MSG_DESTROY) {
		if (EsElementIsFocused(element)) {
			InstanceRemoveItemSelectionCommands(instance);
		}
	} else if (message->type == ES_MSG_FOCUSED_END) {
		InstanceRemoveItemSelectionCommands(instance);
		return 0;
	} else if (message->type == ES_MSG_LIST_VIEW_GET_CONTENT) {
		int column = message->getContent.columnID, index = message->getContent.index;
		EsAssert(index < (int) instance->listContents.Length() && index >= 0);
		ListEntry *listEntry = &instance->listContents[index];
		FolderEntry *entry = listEntry->entry;
		FileType *fileType = FolderEntryGetType(instance->folder, entry);

		if (column == COLUMN_NAME) {
			String name = entry->GetName();
			EsBufferFormat(message->getContent.buffer, "%s", name.bytes, name.text);
			message->getContent.icon = fileType->iconID;
		} else if (column == COLUMN_TYPE) {
			EsBufferFormat(message->getContent.buffer, "%s", fileType->nameBytes, fileType->name);
		} else if (column == COLUMN_SIZE) {
			if (!entry->sizeUnknown) {
				EsBufferFormat(message->getContent.buffer, "%D", entry->size);
			}
		}
	} else if (message->type == ES_MSG_LIST_VIEW_GET_ITEM_DATA) {
		int column = message->getItemData.columnID, index = message->getItemData.index;
		EsAssert(index < (int) instance->listContents.Length() && index >= 0);
		ListEntry *listEntry = &instance->listContents[index];
		FolderEntry *entry = listEntry->entry;
		FileType *fileType = FolderEntryGetType(instance->folder, entry);

		if (column == COLUMN_NAME) {
			String name = entry->GetName();
			message->getItemData.s = name.text;
			message->getItemData.sBytes = name.bytes;
		} else if (column == COLUMN_TYPE) {
			message->getItemData.s = fileType->name;
			message->getItemData.sBytes = fileType->nameBytes;
		} else if (column == COLUMN_SIZE) {
			message->getItemData.i = entry->size;
		}
	} else if (message->type == ES_MSG_LIST_VIEW_GET_SUMMARY) {
		int index = message->getContent.index;
		EsAssert(index < (int) instance->listContents.Length() && index >= 0);
		ListEntry *listEntry = &instance->listContents[index];
		FolderEntry *entry = listEntry->entry;
		FileType *fileType = FolderEntryGetType(instance->folder, entry);
		String name = entry->GetName();
		bool isOpen = false;

		{
			// Check if the file is an open document.
			// TODO Is this slow?

			String path = instance->folder->itemHandler->getPathForChild(instance->folder, entry);

			for (uintptr_t i = 0; i < openDocuments.Length(); i++) {
				if (StringEquals(openDocuments[i], path)) {
					isOpen = true;
					break;
				}
			}

			StringDestroy(&path);
		}

		EsBufferFormat(message->getContent.buffer, "%z%s\n\a2w4]%s " HYPHENATION_POINT " %D", 
				isOpen ? "\aw6]" : "",
				name.bytes, name.text, fileType->nameBytes, fileType->name, entry->size);
		message->getContent.icon = fileType->iconID;
		message->getContent.drawContentFlags = ES_DRAW_CONTENT_RICH_TEXT;
	} else if (message->type == ES_MSG_LIST_VIEW_SELECT_RANGE) {
		for (intptr_t i = message->selectRange.fromIndex; i <= message->selectRange.toIndex; i++) {
			ListEntry *entry = &instance->listContents[i];
			if (entry->selected) { instance->selectedItemCount--; instance->selectedItemsTotalSize -= entry->entry->size; }
			entry->selected = message->selectRange.toggle ? !entry->selected : message->selectRange.select;
			if (entry->selected) { instance->selectedItemCount++; instance->selectedItemsTotalSize += entry->entry->size; }
		}

		InstanceUpdateItemSelectionCountCommands(instance);
		StringDestroy(&instance->delayedFocusItem);
		InstanceUpdateStatusString(instance);
	} else if (message->type == ES_MSG_LIST_VIEW_SELECT) {
		ListEntry *entry = &instance->listContents[message->selectItem.index];
		if (entry->selected) { instance->selectedItemCount--; instance->selectedItemsTotalSize -= entry->entry->size; }
		entry->selected = message->selectItem.isSelected;
		if (entry->selected) { instance->selectedItemCount++; instance->selectedItemsTotalSize += entry->entry->size; }
		InstanceUpdateItemSelectionCountCommands(instance);
		StringDestroy(&instance->delayedFocusItem);
		InstanceUpdateStatusString(instance);
	} else if (message->type == ES_MSG_LIST_VIEW_IS_SELECTED) {
		ListEntry *entry = &instance->listContents[message->selectItem.index];
		message->selectItem.isSelected = entry->selected;
	} else if (message->type == ES_MSG_LIST_VIEW_CHOOSE_ITEM) {
		ListEntry *listEntry = &instance->listContents[message->chooseItem.index];

		if (listEntry) {
			FolderEntry *entry = listEntry->entry;

			if (entry->isFolder) {
				String path = instance->folder->itemHandler->getPathForChild(instance->folder, entry);

				if (EsKeyboardIsCtrlHeld() || message->chooseItem.source == ES_LIST_VIEW_CHOOSE_ITEM_MIDDLE_CLICK) {
					EsApplicationStartupRequest request = {};
					request.id = EsInstanceGetStartupRequest(instance).id;
					request.filePath = path.text;
					request.filePathBytes = path.bytes;
					request.flags = ES_APPLICATION_STARTUP_IN_SAME_CONTAINER | ES_APPLICATION_STARTUP_NO_DOCUMENT;
					EsApplicationStart(instance, &request);
					StringDestroy(&path);
				} else {
					InstanceLoadFolder(instance, path);
				}
			} else if (StringEquals(entry->GetExtension(), StringFromLiteral("esx"))) {
				// TODO Temporary.
				String path = StringAllocateAndFormat("%s%s", STRFMT(instance->folder->path), STRFMT(entry->GetInternalName()));

				if (StringEquals(path, StringFromLiteral("0:/Essence/Desktop.esx")) 
						|| StringEquals(path, StringFromLiteral("0:/Essence/Kernel.esx"))) {
					EsDialogShow(instance->window, INTERFACE_STRING(FileManagerOpenFileError),
							INTERFACE_STRING(FileManagerCannotOpenSystemFile),
							ES_ICON_DIALOG_ERROR, ES_DIALOG_ALERT_OK_BUTTON); 
				} else {
					EsApplicationRunTemporary(STRING(path));
				}

				StringDestroy(&path);
			} else {
				FileType *fileType = FolderEntryGetType(instance->folder, entry);

				if (fileType->openHandler) {
					String path = StringAllocateAndFormat("%s%s", STRFMT(instance->folder->path), STRFMT(entry->GetInternalName()));
					EsApplicationStartupRequest request = {};
					request.id = fileType->openHandler;
					request.filePath = path.text;
					request.filePathBytes = path.bytes;
					request.flags = EsKeyboardIsCtrlHeld() || message->chooseItem.source == ES_LIST_VIEW_CHOOSE_ITEM_MIDDLE_CLICK 
						? ES_APPLICATION_STARTUP_IN_SAME_CONTAINER : ES_FLAGS_DEFAULT;
					EsApplicationStart(instance, &request);
					StringDestroy(&path);
				} else {
					EsDialogShow(instance->window, INTERFACE_STRING(FileManagerOpenFileError),
							INTERFACE_STRING(FileManagerNoRegisteredApplicationsForFile),
							ES_ICON_DIALOG_ERROR, ES_DIALOG_ALERT_OK_BUTTON); 
				}
			}
		}
	} else if (message->type == ES_MSG_LIST_VIEW_COLUMN_MENU) {
		EsMenu *menu = EsMenuCreate(message->columnMenu.source);
		uint32_t index = message->columnMenu.columnID;
		const char *ascending = nullptr;
		const char *descending = nullptr;

		if (index == COLUMN_NAME) {
			ascending = interfaceString_CommonSortAToZ;
			descending = interfaceString_CommonSortZToA;
		} else if (index == COLUMN_TYPE) {
			ascending = interfaceString_CommonSortAToZ;
			descending = interfaceString_CommonSortZToA;
		} else if (index == COLUMN_SIZE) {
			ascending = interfaceString_CommonSortSmallToLarge;
			descending = interfaceString_CommonSortLargeToSmall;
		}

#define COLUMN_NAME (0)
#define COLUMN_TYPE (1)
#define COLUMN_SIZE (2)

		EsMenuAddItem(menu, ES_MENU_ITEM_HEADER, INTERFACE_STRING(CommonSortHeader));
		EsMenuAddItem(menu, instance->viewSettings.sortColumn == index ? ES_MENU_ITEM_CHECKED : 0, 
				ascending, -1, InstanceChangeSortColumn, index);
		EsMenuAddItem(menu, instance->viewSettings.sortColumn == (index | (1 << 8)) ? ES_MENU_ITEM_CHECKED : 0, 
				descending, -1, InstanceChangeSortColumn, index | (1 << 8));
		EsMenuShow(menu);
	} else if (message->type == ES_MSG_LIST_VIEW_GET_COLUMN_SORT) {
		if (message->getColumnSort.index == (instance->viewSettings.sortColumn & 0xFF)) {
			return (instance->viewSettings.sortColumn & (1 << 8)) ? ES_LIST_VIEW_COLUMN_SORT_DESCENDING : ES_LIST_VIEW_COLUMN_SORT_ASCENDING;
		}
	} else if (message->type == ES_MSG_LIST_VIEW_CREATE_ITEM) {
		EsElement *element = message->createItem.item;
		element->messageUser = ListItemMessage;
		ListItemCreated(element, message->createItem.index, false);
	} else if (message->type == ES_MSG_LIST_VIEW_CONTEXT_MENU) {
		EsMenu *menu = EsMenuCreate(element, ES_MENU_AT_CURSOR);
		EsOpenDocumentInformation information;
		ListEntry *entry = &instance->listContents[message->selectItem.index];
		String path = instance->folder->itemHandler->getPathForChild(instance->folder, entry->entry);
		EsOpenDocumentQueryInformation(STRING(path), &information);
		StringDestroy(&path);

		if (information.isOpen) {
			char buffer[256];
			size_t bytes = EsStringFormat(buffer, sizeof(buffer), interfaceString_FileManagerFileOpenIn, 
					(size_t) information.applicationNameBytes, (const char *) information.applicationName);
			EsMenuAddItem(menu, ES_MENU_ITEM_HEADER, buffer, bytes);
			EsMenuAddSeparator(menu);
		}

		EsMenuAddCommand(menu, ES_FLAGS_DEFAULT, INTERFACE_STRING(CommonClipboardCut), EsCommandByID(instance, ES_COMMAND_CUT));
		EsMenuAddCommand(menu, ES_FLAGS_DEFAULT, INTERFACE_STRING(CommonClipboardCopy), EsCommandByID(instance, ES_COMMAND_COPY));
		EsMenuAddSeparator(menu);
		EsMenuAddCommand(menu, ES_FLAGS_DEFAULT, INTERFACE_STRING(FileManagerRenameAction), &instance->commandRename);
		EsMenuShow(menu);
	} else if (message->type == ES_MSG_MOUSE_RIGHT_CLICK) {
		EsMenu *menu = EsMenuCreate(element, ES_MENU_AT_CURSOR);

		EsMenuAddItem(menu, ES_MENU_ITEM_HEADER, INTERFACE_STRING(FileManagerListContextActions));
		EsMenuAddCommand(menu, ES_FLAGS_DEFAULT, INTERFACE_STRING(CommonClipboardPaste),            EsCommandByID(instance, ES_COMMAND_PASTE));
		EsMenuAddCommand(menu, ES_FLAGS_DEFAULT, INTERFACE_STRING(CommonSelectionSelectAll),        EsCommandByID(instance, ES_COMMAND_SELECT_ALL));
		EsMenuAddCommand(menu, ES_FLAGS_DEFAULT, INTERFACE_STRING(FileManagerNewFolderToolbarItem), &instance->commandNewFolder);
		EsMenuAddCommand(menu, ES_FLAGS_DEFAULT, INTERFACE_STRING(FileManagerRefresh),              &instance->commandRefresh);

		EsMenuAddSeparator(menu);

#define ADD_VIEW_TYPE_MENU_ITEM(_command, _string) \
		EsMenuAddCommand(menu, ES_FLAGS_DEFAULT, INTERFACE_STRING(_string), _command)
		EsMenuAddItem(menu, ES_MENU_ITEM_HEADER, INTERFACE_STRING(CommonListViewType));
		ADD_VIEW_TYPE_MENU_ITEM(&instance->commandViewThumbnails, CommonListViewTypeThumbnails);
		ADD_VIEW_TYPE_MENU_ITEM(&instance->commandViewTiles,      CommonListViewTypeTiles);
		ADD_VIEW_TYPE_MENU_ITEM(&instance->commandViewDetails,    CommonListViewTypeDetails);
#undef ADD_VIEW_TYPE_MENU_ITEM

		EsMenuNextColumn(menu);

#define ADD_SORT_COLUMN_MENU_ITEM(_column, _string) \
		EsMenuAddItem(menu, instance->viewSettings.sortColumn == (_column) ? ES_MENU_ITEM_CHECKED : ES_FLAGS_DEFAULT, \
				INTERFACE_STRING(_string), InstanceChangeSortColumn, _column)
		EsMenuAddItem(menu, ES_MENU_ITEM_HEADER, INTERFACE_STRING(CommonSortAscending));
		ADD_SORT_COLUMN_MENU_ITEM(COLUMN_NAME, FileManagerColumnName);
		ADD_SORT_COLUMN_MENU_ITEM(COLUMN_TYPE, FileManagerColumnType);
		ADD_SORT_COLUMN_MENU_ITEM(COLUMN_SIZE, FileManagerColumnSize);
		EsMenuAddSeparator(menu);
		EsMenuAddItem(menu, ES_MENU_ITEM_HEADER, INTERFACE_STRING(CommonSortDescending));
		ADD_SORT_COLUMN_MENU_ITEM(COLUMN_NAME | (1 << 8), FileManagerColumnName);
		ADD_SORT_COLUMN_MENU_ITEM(COLUMN_TYPE | (1 << 8), FileManagerColumnType);
		ADD_SORT_COLUMN_MENU_ITEM(COLUMN_SIZE | (1 << 8), FileManagerColumnSize);
#undef ADD_SORT_COLUMN_MENU_ITEM

		EsMenuShow(menu);
	} else {
		return 0;
	}

	return ES_HANDLED;
}

int PlacesViewCallback(EsElement *element, EsMessage *message) {
	Instance *instance = element->instance;

	if (message->type == ES_MSG_LIST_VIEW_GET_CONTENT) {
		int group = message->getContent.group;
		int index = message->getContent.index;

		if (group == PLACES_VIEW_GROUP_DRIVES) {
			// TODO Use namespace lookup.

			if (index == 0) {
				EsBufferFormat(message->getContent.buffer, "%z", interfaceString_FileManagerPlacesDrives);
				message->getContent.icon = ES_ICON_COMPUTER_LAPTOP;
			} else {
				Drive *drive = &drives[index - 1];
				EsBufferFormat(message->getContent.buffer, "%s", drive->information.labelBytes, drive->information.label);
				message->getContent.icon = EsIconIDFromDriveType(drive->information.driveType);
			}
		} else if (group == PLACES_VIEW_GROUP_BOOKMARKS) {
			if (index == 0) {
				EsBufferFormat(message->getContent.buffer, "%z", interfaceString_FileManagerPlacesBookmarks);
				message->getContent.icon = ES_ICON_HELP_ABOUT;
			} else {
				// TODO The namespace lookup might be expensive. Perhaps these should be cached?
				NamespaceGetVisibleName(message->getContent.buffer, bookmarks[index - 1]);
				message->getContent.icon = NamespaceGetIcon(bookmarks[index - 1]);
			}
		}
	} else if (message->type == ES_MSG_LIST_VIEW_SELECT && message->selectItem.isSelected) {
		if (message->selectItem.group == PLACES_VIEW_GROUP_DRIVES) {
			if (message->selectItem.index == 0) {
				InstanceLoadFolder(instance, StringAllocateAndFormat("%z", interfaceString_FileManagerDrivesPage), LOAD_FOLDER_NO_FOCUS);
			} else {
				Drive *drive = &drives[message->selectItem.index - 1];
				InstanceLoadFolder(instance, StringAllocateAndFormat("%s/", drive->prefixBytes, drive->prefix), LOAD_FOLDER_NO_FOCUS);
			}
		} else if (message->selectItem.group == PLACES_VIEW_GROUP_BOOKMARKS && message->selectItem.index) {
			String string = bookmarks[message->selectItem.index - 1];
			InstanceLoadFolder(instance, StringAllocateAndFormat("%s", STRFMT(string)), LOAD_FOLDER_NO_FOCUS);
		}
	} else if (message->type == ES_MSG_LIST_VIEW_IS_SELECTED) {
		if (message->selectItem.group == PLACES_VIEW_GROUP_DRIVES) {
			if (message->selectItem.index == 0) {
				message->selectItem.isSelected = 0 == EsStringCompareRaw(INTERFACE_STRING(FileManagerDrivesPage), 
						instance->path.text, instance->path.bytes);
			} else {
				Drive *drive = &drives[message->selectItem.index - 1];
				message->selectItem.isSelected = 0 == EsStringCompareRaw(drive->prefix, drive->prefixBytes, 
						instance->path.text, instance->path.bytes - 1);
			}
		} else if (message->selectItem.group == PLACES_VIEW_GROUP_BOOKMARKS && message->selectItem.index) {
			String string = bookmarks[message->selectItem.index - 1];
			message->selectItem.isSelected = 0 == EsStringCompareRaw(string.text, string.bytes, 
					instance->path.text, instance->path.bytes);
		}
	} else if (message->type == ES_MSG_LIST_VIEW_CONTEXT_MENU) {
		if (message->selectItem.group == PLACES_VIEW_GROUP_BOOKMARKS && !message->selectItem.index) {
			bool isCurrentFolderBookmarked = false;

			for (uintptr_t i = 0; i < bookmarks.Length(); i++) {
				if (0 == EsStringCompareRaw(STRING(bookmarks[i]), STRING(instance->path))) {
					isCurrentFolderBookmarked = true;
					break;
				}
			}

			EsMenu *menu = EsMenuCreate(element, ES_MENU_AT_CURSOR);

			if (isCurrentFolderBookmarked) {
				EsMenuAddItem(menu, ES_FLAGS_DEFAULT, INTERFACE_STRING(FileManagerBookmarksRemoveHere), [] (EsMenu *menu, EsGeneric) {
					BookmarkRemove(menu->instance->path);
				});
			} else {
				EsMenuAddItem(menu, ES_FLAGS_DEFAULT, INTERFACE_STRING(FileManagerBookmarksAddHere), [] (EsMenu *menu, EsGeneric) {
					BookmarkAdd(menu->instance->path);
				});
			}

			EsMenuShow(menu);
		}
	} else {
		return 0;
	}

	return ES_HANDLED;
}

int BreadcrumbBarMessage(EsElement *element, EsMessage *message) {
	Instance *instance = element->instance;
	EsTextbox *textbox = (EsTextbox *) element;

	if (message->type == ES_MSG_TEXTBOX_ACTIVATE_BREADCRUMB) {
		String section = PathGetSection(instance->folder->path, message->activateBreadcrumb);
		size_t bytes = section.text + section.bytes - instance->folder->path.text;

		String path = StringAllocateAndFormat("%s%z", 
				bytes, instance->folder->path.text,
				instance->folder->path.text[bytes - 1] != '/' ? "/" : "");
		InstanceLoadFolder(instance, path);
	} else if (message->type == ES_MSG_TEXTBOX_EDIT_END) {
		String section;
		section.text = EsTextboxGetContents(textbox, &section.bytes);
		section.allocated = section.bytes;

		String path = StringAllocateAndFormat("%s%z", 
				section.bytes, section.text,
				section.text[section.bytes - 1] != '/' ? "/" : "");

		if (!InstanceLoadFolder(instance, path)) {
			StringDestroy(&section);
			return ES_REJECTED;
		}

		StringDestroy(&section);
	} else if (message->type == ES_MSG_TEXTBOX_GET_BREADCRUMB) {
		if (!instance->folder || PathCountSections(instance->folder->path) == message->getBreadcrumb.index) {
			return ES_REJECTED;
		}

		String path = PathGetParent(instance->folder->path, message->getBreadcrumb.index);
		NamespaceGetVisibleName(message->getBreadcrumb.buffer, path);
		message->getBreadcrumb.icon = message->getBreadcrumb.index ? 0 : NamespaceGetIcon(path);
		return ES_HANDLED;
	}

	return 0;
}

#define ADD_BUTTON_TO_TOOLBAR(_command, _label, _icon, _accessKey, _name) \
	{ \
		_name = EsButtonCreate(buttonGroup, ES_FLAGS_DEFAULT, 0, _label); \
		EsButtonSetIcon(_name, _icon);  \
		EsCommandAddButton(&instance->_command, _name); \
		_name->accessKey = _accessKey; \
	}

#define ADD_BUTTON_TO_STATUS_BAR(_command, _label, _icon, _accessKey, _name) \
	{ \
		_name = EsButtonCreate(buttonGroup, ES_FLAGS_DEFAULT, ES_STYLE_PUSH_BUTTON_STATUS_BAR, _label); \
		EsButtonSetIcon(_name, _icon);  \
		EsCommandAddButton(&instance->_command, _name); \
		_name->accessKey = _accessKey; \
	}

void InstanceCreateUI(Instance *instance) {
	EsButton *button;

	EsWindowSetIcon(instance->window, ES_ICON_SYSTEM_FILE_MANAGER);
	InstanceRegisterCommands(instance);

	EsPanel *rootPanel = EsPanelCreate(instance->window, ES_CELL_FILL | ES_PANEL_VERTICAL);
	EsSplitter *splitter = EsSplitterCreate(rootPanel, ES_SPLITTER_HORIZONTAL | ES_CELL_FILL, ES_STYLE_PANEL_WINDOW_WITH_STATUS_BAR_CONTENT);

	// Places:

	instance->placesView = EsListViewCreate(splitter, ES_CELL_EXPAND | ES_CELL_COLLAPSABLE | ES_LIST_VIEW_SINGLE_SELECT | ES_CELL_V_FILL, 
			EsStyleIntern(&stylePlacesView), ES_STYLE_LIST_ITEM, ES_STYLE_LIST_ITEM, 0);
	instance->placesView->accessKey = 'P';
	instance->placesView->messageUser = PlacesViewCallback;
	EsListViewInsertGroup(instance->placesView, PLACES_VIEW_GROUP_BOOKMARKS, ES_LIST_VIEW_GROUP_HAS_HEADER | ES_LIST_VIEW_GROUP_INDENT);
	EsListViewInsertGroup(instance->placesView, PLACES_VIEW_GROUP_DRIVES, ES_LIST_VIEW_GROUP_HAS_HEADER | ES_LIST_VIEW_GROUP_INDENT);
	if (bookmarks.Length()) EsListViewInsert(instance->placesView, PLACES_VIEW_GROUP_BOOKMARKS, 1, bookmarks.Length());
	EsListViewInsert(instance->placesView, PLACES_VIEW_GROUP_DRIVES, 1, drives.Length());

	// Main list:

	instance->list = EsListViewCreate(splitter, ES_CELL_FILL | ES_LIST_VIEW_COLUMNS | ES_LIST_VIEW_MULTI_SELECT, EsStyleIntern(&styleFolderView));
	instance->list->accessKey = 'L';
	instance->list->messageUser = ListCallback;
	EsListViewRegisterColumn(instance->list, COLUMN_NAME, INTERFACE_STRING(FileManagerColumnName), ES_LIST_VIEW_COLUMN_HAS_MENU);
	EsListViewRegisterColumn(instance->list, COLUMN_TYPE, INTERFACE_STRING(FileManagerColumnType), ES_LIST_VIEW_COLUMN_HAS_MENU);
	EsListViewRegisterColumn(instance->list, COLUMN_SIZE, INTERFACE_STRING(FileManagerColumnSize), 
			ES_LIST_VIEW_COLUMN_HAS_MENU | ES_TEXT_H_RIGHT | ES_LIST_VIEW_COLUMN_DATA_INTEGERS | ES_LIST_VIEW_COLUMN_FORMAT_BYTES);
	EsListViewAddAllColumns(instance->list);
	EsListViewInsertGroup(instance->list, 0);

	// Toolbar:

	EsElement *toolbar = EsWindowGetToolbar(instance->window);
	EsPanel *buttonGroup = EsPanelCreate(toolbar, ES_PANEL_HORIZONTAL | ES_ELEMENT_AUTO_GROUP);
	ADD_BUTTON_TO_TOOLBAR(commandGoBackwards, nullptr, ES_ICON_GO_PREVIOUS_SYMBOLIC, 'B', button);
	EsSpacerCreate(buttonGroup, ES_CELL_V_FILL, ES_STYLE_TOOLBAR_BUTTON_GROUP_SEPARATOR);
	ADD_BUTTON_TO_TOOLBAR(commandGoForwards, nullptr, ES_ICON_GO_NEXT_SYMBOLIC, 'F', button);
	EsSpacerCreate(buttonGroup, ES_CELL_V_FILL, ES_STYLE_TOOLBAR_BUTTON_GROUP_SEPARATOR);
	ADD_BUTTON_TO_TOOLBAR(commandGoParent, nullptr, ES_ICON_GO_UP_SYMBOLIC, 'U', button);
	EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT, ES_STYLE_TOOLBAR_SPACER);
	instance->breadcrumbBar = EsTextboxCreate(toolbar, ES_CELL_H_FILL | ES_TEXTBOX_EDIT_BASED | ES_TEXTBOX_REJECT_EDIT_IF_LOST_FOCUS);
	instance->breadcrumbBar->messageUser = BreadcrumbBarMessage;
	instance->breadcrumbBar->accessKey = 'A';
	EsTextboxUseBreadcrumbOverlay(instance->breadcrumbBar);
	EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT, ES_STYLE_TOOLBAR_SPACER);
	buttonGroup = EsPanelCreate(toolbar, ES_PANEL_HORIZONTAL);
	ADD_BUTTON_TO_TOOLBAR(commandNewFolder, interfaceString_FileManagerNewFolderToolbarItem, ES_ICON_FOLDER_NEW_SYMBOLIC, 'N', instance->newFolderButton);

	// Status bar:

	EsPanel *statusBar = EsPanelCreate(rootPanel, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL, ES_STYLE_PANEL_STATUS_BAR);
	instance->status = EsTextDisplayCreate(statusBar, ES_CELL_H_FILL);
	buttonGroup = EsPanelCreate(statusBar, ES_PANEL_HORIZONTAL | ES_ELEMENT_AUTO_GROUP);
	ADD_BUTTON_TO_STATUS_BAR(commandViewDetails, nullptr, ES_ICON_VIEW_LIST_SYMBOLIC, 0, button);
	EsSpacerCreate(buttonGroup, ES_CELL_V_FILL, ES_STYLE_TOOLBAR_BUTTON_GROUP_SEPARATOR);
	ADD_BUTTON_TO_STATUS_BAR(commandViewTiles, nullptr, ES_ICON_VIEW_LIST_COMPACT_SYMBOLIC, 0, button);
	EsSpacerCreate(buttonGroup, ES_CELL_V_FILL, ES_STYLE_TOOLBAR_BUTTON_GROUP_SEPARATOR);
	ADD_BUTTON_TO_STATUS_BAR(commandViewThumbnails, nullptr, ES_ICON_VIEW_GRID_SYMBOLIC, 0, button);

	// Load initial folder:

	EsApplicationStartupRequest startupRequest = EsInstanceGetStartupRequest(instance);
	String path;

	if (startupRequest.filePathBytes) {
		uintptr_t directoryEnd = startupRequest.filePathBytes;

		for (uintptr_t i = 0; i < (uintptr_t) startupRequest.filePathBytes; i++) {
			if (startupRequest.filePath[i] == '/') {
				directoryEnd = i + 1;
			}
		}

		instance->delayedFocusItem = StringAllocateAndFormat("%s", startupRequest.filePathBytes - directoryEnd, startupRequest.filePath + directoryEnd);
		path = StringAllocateAndFormat("%s", directoryEnd, startupRequest.filePath);
	} else {
		path = StringAllocateAndFormat("0:/");
	}

	InstanceLoadFolder(instance, path, LOAD_FOLDER_START);
}

void InstanceReportError(Instance *instance, int error, EsError code) {
	EsPrint("FM error %d/%d.\n", error, code);

	// TODO Error messages.

	const char *message = interfaceString_FileManagerGenericError;

	if (code == ES_ERROR_ALREADY_EXISTS) {
		message = interfaceString_FileManagerItemAlreadyExistsError;
	} else if (code == ES_ERROR_FILE_DOES_NOT_EXIST) {
		message = interfaceString_FileManagerItemDoesNotExistError;
	} else if (code == ES_ERROR_PERMISSION_NOT_GRANTED) {
		message = interfaceString_FileManagerPermissionNotGrantedError;
	}

	EsDialogShow(instance->window, errorTypeStrings[error], -1, message, -1, 
			ES_ICON_DIALOG_ERROR, ES_DIALOG_ALERT_OK_BUTTON); 
}

void InstanceDestroy(Instance *instance) {
	StringDestroy(&instance->path);
	StringDestroy(&instance->delayedFocusItem);

	for (uintptr_t i = 0; i < instance->pathBackwardHistory.Length(); i++) {
		StringDestroy(&instance->pathBackwardHistory[i].path);
		StringDestroy(&instance->pathBackwardHistory[i].focusedItem);
	}

	for (uintptr_t i = 0; i < instance->pathForwardHistory.Length(); i++) {
		StringDestroy(&instance->pathForwardHistory[i].path);
		StringDestroy(&instance->pathForwardHistory[i].focusedItem);
	}

	for (uintptr_t i = 0; i < instance->listContents.Length(); i++) {
		FolderEntryCloseHandle(instance->folder, instance->listContents[i].entry);
	}

	FolderDetachInstance(instance);

	instance->pathBackwardHistory.Free();
	instance->pathForwardHistory.Free();
	instance->listContents.Free();
}
