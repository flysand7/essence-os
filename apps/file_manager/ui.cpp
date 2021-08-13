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

	EsListViewEnumerateVisibleItems(instance->list, [] (EsListView *, EsElement *item, uint32_t, EsGeneric index) {
		ListItemCreated(item, index.u, true);
	});
}

bool InstanceLoadFolder(Instance *instance, String path /* takes ownership */, int historyMode) {
	// Check if the path hasn't changed.

	if (instance->folder && !instance->folder->refreshing && StringEquals(path, instance->folder->path)) {
		StringDestroy(&path);
		return true;
	}

	Task task = {};
	
	task.context = historyMode;
	task.string = path;
	task.cDescription = interfaceString_FileManagerOpenFolderTask;

	task.callback = [] (Instance *instance, Task *task) {
		Folder *newFolder = nullptr;
		task->result = FolderAttachInstance(instance, task->string, false, &newFolder);
		task->context2 = newFolder;
		StringDestroy(&task->string);
	};

	task.then = [] (Instance *instance, Task *task) {
		if (ES_CHECK_ERROR(task->result)) {
			EsTextboxSelectAll(instance->breadcrumbBar);
			EsTextboxInsert(instance->breadcrumbBar, STRING(instance->path));
			InstanceReportError(instance, ERROR_LOAD_FOLDER, task->result);
			return;
		}

		int historyMode = task->context.i & 0xFF;
		int flags = task->context.i;
		Folder *folder = (Folder *) task->context2.p;

		// Add the path to the history array.

		HistoryEntry historyEntry = {};
		historyEntry.path = instance->path;

		EsGeneric focusedIndex;

		if (EsListViewGetFocusedItem(instance->list, nullptr, &focusedIndex)) {
			String name = instance->listContents[focusedIndex.u].entry->GetName();
			historyEntry.focusedItem = StringDuplicate(name);
		}

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

		EsCommandSetDisabled(&instance->commandGoBackwards, !instance->pathBackwardHistory.Length());
		EsCommandSetDisabled(&instance->commandGoForwards, !instance->pathForwardHistory.Length());
		EsCommandSetDisabled(&instance->commandGoParent, PathCountSections(folder->path) == 1);
		EsCommandSetDisabled(&instance->commandNewFolder, !folder->itemHandler->createChildFolder);
		EsCommandSetDisabled(&instance->commandRename, true);
		EsCommandSetDisabled(&instance->commandRefresh, false);

		// Detach from the old folder.

		EsMutexAcquire(&loadedFoldersMutex);

		InstanceRemoveContents(instance);
		FolderDetachInstance(instance);

		// Load the view settings for the folder.

		bool foundViewSettings = false;

		if (folder->path.bytes < sizeof(folderViewSettings[0].path)) {
			for (uintptr_t i = 0; i < folderViewSettings.Length(); i++) {
				if (folderViewSettings[i].pathBytes == folder->path.bytes
						&& 0 == EsMemoryCompare(folderViewSettings[i].path, STRING(folder->path))) {
					foundViewSettings = true;
					FolderViewSettingsEntry entry = folderViewSettings[i];
					instance->viewSettings = entry.settings;
					folderViewSettings.Delete(i);
					folderViewSettings.Add(entry); // Update the LRU order.
					break;
				}
			}
		}

		if (!foundViewSettings) {
			if (folder->itemHandler->getDefaultViewSettings) {
				folder->itemHandler->getDefaultViewSettings(folder, &instance->viewSettings);
			} else {
				// TODO Get default from configuration.
				instance->viewSettings.sortColumn = COLUMN_NAME;
				instance->viewSettings.viewType = VIEW_DETAILS;
			}
		}

		InstanceRefreshViewType(instance);

		// Attach to the new folder.

		folder->attachingInstances.FindAndDeleteSwap(instance, true);
		instance->folder = folder;
		folder->attachedInstances.Add(instance);

		EsListViewSetEmptyMessage(instance->list, folder->cEmptyMessage);
		InstanceAddContents(instance, &folder->entries);

		EsMutexRelease(&loadedFoldersMutex);

		InstanceFolderPathChanged(instance, true);

		if (~flags & LOAD_FOLDER_NO_FOCUS) {
			EsElementFocus(instance->list);
		}

		EsListViewInvalidateAll(instance->placesView);
	};

	BlockingTaskQueue(instance, task);
	return true;
}

void InstanceRefreshViewType(Instance *instance) {
	if (instance->viewSettings.viewType == VIEW_DETAILS) {
		EsCommandSetCheck(&instance->commandViewDetails, ES_CHECK_CHECKED, false);
		EsCommandSetCheck(&instance->commandViewTiles, ES_CHECK_UNCHECKED, false);
		EsCommandSetCheck(&instance->commandViewThumbnails, ES_CHECK_UNCHECKED, false);

		EsListViewChangeStyles(instance->list, &styleFolderView, ES_STYLE_LIST_ITEM, nullptr, nullptr, ES_LIST_VIEW_COLUMNS, ES_LIST_VIEW_TILED);
		EsListViewSetColumns(instance->list, folderOutputColumns, sizeof(folderOutputColumns) / sizeof(folderOutputColumns[0]));
	} else if (instance->viewSettings.viewType == VIEW_TILES) {
		EsCommandSetCheck(&instance->commandViewTiles, ES_CHECK_CHECKED, false);
		EsCommandSetCheck(&instance->commandViewDetails, ES_CHECK_UNCHECKED, false);
		EsCommandSetCheck(&instance->commandViewThumbnails, ES_CHECK_UNCHECKED, false);

		EsListViewChangeStyles(instance->list, &styleFolderViewTiled, ES_STYLE_LIST_ITEM_TILE, nullptr, nullptr, ES_LIST_VIEW_TILED, ES_LIST_VIEW_COLUMNS);
	} else if (instance->viewSettings.viewType == VIEW_THUMBNAILS) {
		EsCommandSetCheck(&instance->commandViewThumbnails, ES_CHECK_CHECKED, false);
		EsCommandSetCheck(&instance->commandViewTiles, ES_CHECK_UNCHECKED, false);
		EsCommandSetCheck(&instance->commandViewDetails, ES_CHECK_UNCHECKED, false);

		EsListViewChangeStyles(instance->list, &styleFolderViewTiled, &styleFolderItemThumbnail, nullptr, nullptr, ES_LIST_VIEW_TILED, ES_LIST_VIEW_COLUMNS);
	}
}

void InstanceUpdateItemSelectionCountCommands(Instance *instance) {
	EsCommandSetDisabled(&instance->commandRename, instance->selectedItemCount != 1 || !instance->folder->itemHandler->renameItem);
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
	// Call with loadedFoldersMutex and message mutex.

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
	EsListViewInsert(instance->list, 0, low, low);

	if (entry.selected) {
		EsListViewSelect(instance->list, 0, low);
		EsListViewFocusItem(instance->list, 0, low);
	}

	InstanceUpdateStatusString(instance);
}

ES_MACRO_SORT(InstanceSortListContents, ListEntry, {
	result = InstanceCompareFolderEntries(_left->entry, _right->entry, context);
}, uint16_t);

void InstanceAddContents(Instance *instance, HashTable *newEntries) {
	// Call with loadedFoldersMutex and message mutex.

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
		EsListViewRemove(instance->list, 0, 0, oldListEntryCount - 1);
	}

	InstanceSortListContents(instance->listContents.array, instance->listContents.Length(), instance->viewSettings.sortColumn);

	if (instance->listContents.Length()) {
		EsListViewInsert(instance->list, 0, 0, instance->listContents.Length() - 1);

		if (instance->delayedFocusItem.bytes) {
			for (uintptr_t i = 0; i < instance->listContents.Length(); i++) {
				if (0 == EsStringCompareRaw(STRING(instance->listContents[i].entry->GetName()), STRING(instance->delayedFocusItem))) {
					EsListViewSelect(instance->list, 0, i);
					EsListViewFocusItem(instance->list, 0, i);
					break;
				}
			}

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
			EsListViewRemove(instance->list, 0, middle, middle);
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
		EsListViewRemove(instance->list, 0, 0, instance->listContents.Length() - 1);
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

__attribute__((optimize("-O3"))) 
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

void ListItemGenerateThumbnailTask(Instance *, Task *task) {
	EsMessageMutexAcquire();
	Thumbnail *thumbnail = thumbnailCache.Get(&task->id);
	bool cancelTask = !thumbnail || thumbnail->referenceCount == 0;
	EsMessageMutexRelease();

	if (cancelTask) {
		return; // There are no longer any list items visible for this file.
	}

	size_t fileBytes;
	void *file = EsFileReadAll(STRING(task->string), &fileBytes);

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
	uint32_t thumbnailMaximumWidth = 120;
	uint32_t thumbnailMaximumHeight = 80;
	EsRectangle targetRectangle = EsRectangleFit(ES_RECT_2S(thumbnailMaximumWidth, thumbnailMaximumHeight), ES_RECT_2S(originalWidth, originalHeight), false);
	uint32_t targetWidth = ES_RECT_WIDTH(targetRectangle), targetHeight = ES_RECT_HEIGHT(targetRectangle);
	uint32_t *targetBits;

	if (targetWidth == originalWidth && targetHeight == originalHeight) {
		targetBits = originalBits;
	} else {
		targetBits = (uint32_t *) EsHeapAllocate(targetWidth * targetHeight * 4, false);

		if (!targetBits) {
			EsHeapFree(originalBits);
			return; // Allocation failure; could not resize the image.
		}

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

void ListItemGenerateThumbnailTaskComplete(Instance *, Task *task) {
	Thumbnail *thumbnail = thumbnailCache.Get(&task->id);

	if (thumbnail) {
		thumbnail->generatingTasksInProgress--;
	}

	String parent = PathGetParent(task->string);

	for (uintptr_t i = 0; i < instances.Length(); i++) {
		if (StringEquals(parent, instances[i]->path)) { // TODO Support recursive views.
			EsListViewInvalidateAll(instances[i]->list);
		}
	}

	StringDestroy(&task->string);
}

Thumbnail *ListItemGetThumbnail(EsElement *element) {
	Instance *instance = element->instance;
	ListEntry *entry = &instance->listContents[EsListViewGetIndexFromItem(element).u];
	Thumbnail *thumbnail = thumbnailCache.Get(&entry->entry->id);
	return thumbnail;
}

void ListItemCreated(EsElement *element, uintptr_t index, bool fromFolderRename) {
	Instance *instance = element->instance;

	if (instance->viewSettings.viewType != VIEW_THUMBNAILS && instance->viewSettings.viewType != VIEW_TILES) {
		return; // The current view does not display thumbnails.
	}

	ListEntry *listEntry = &instance->listContents[index];
	FolderEntry *entry = listEntry->entry;
	FileType *fileType = FolderEntryGetType(instance->folder, entry);

	if (!fileType->hasThumbnailGenerator) {
		return; // The file type does not support thumbnail generation.
	}

	Thumbnail *thumbnail = thumbnailCache.Put(&entry->id);

	if (!fromFolderRename) {
		thumbnail->referenceCount++;
	}

	// TODO Remove from LRU if needed.

	if ((thumbnail->generatingTasksInProgress && !fromFolderRename) || thumbnail->bits) {
		return; // The thumbnail is already being/has already been generated.
	}

	thumbnail->generatingTasksInProgress++;

	String path = StringAllocateAndFormat("%s%s", STRFMT(instance->path), STRFMT(entry->GetName()));

	Task task = {
		.string = path,
		.id = entry->id,
		.callback = ListItemGenerateThumbnailTask,
		.then = ListItemGenerateThumbnailTaskComplete,
	};

	NonBlockingTaskQueue(task);
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
				destination = EsRectangleFit(destination, source, false);
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

	if (message->type == ES_MSG_LIST_VIEW_GET_CONTENT) {
		int column = message->getContent.column, index = message->getContent.index.i;
		EsAssert(index < (int) instance->listContents.Length() && index >= 0);
		ListEntry *listEntry = &instance->listContents[index];
		FolderEntry *entry = listEntry->entry;
		FileType *fileType = FolderEntryGetType(instance->folder, entry);

		if (column == COLUMN_NAME) {
			String name = entry->GetName();
			EsBufferFormat(message->getContent.buffer, "%s", name.bytes, name.text);
			message->getContent.icon = fileType->iconID;
		} else if (column == COLUMN_TYPE) {
			EsBufferFormat(message->getContent.buffer, "%z", fileType->name);
		} else if (column == COLUMN_SIZE) {
			if (!entry->sizeUnknown) {
				EsBufferFormat(message->getContent.buffer, "%D", entry->size);
			}
		}
	} else if (message->type == ES_MSG_LIST_VIEW_GET_SUMMARY) {
		int index = message->getContent.index.i;
		EsAssert(index < (int) instance->listContents.Length() && index >= 0);
		ListEntry *listEntry = &instance->listContents[index];
		FolderEntry *entry = listEntry->entry;
		FileType *fileType = FolderEntryGetType(instance->folder, entry);
		String name = entry->GetName();
		EsBufferFormat(message->getContent.buffer, "%s\n\a2]%z " HYPHENATION_POINT " %D", name.bytes, name.text, fileType->name, entry->size);
		message->getContent.icon = fileType->iconID;
		message->getContent.richText = true;
	} else if (message->type == ES_MSG_LIST_VIEW_SELECT_RANGE) {
		for (intptr_t i = message->selectRange.fromIndex.i; i <= message->selectRange.toIndex.i; i++) {
			ListEntry *entry = &instance->listContents[i];
			if (entry->selected) { instance->selectedItemCount--; instance->selectedItemsTotalSize -= entry->entry->size; }
			entry->selected = message->selectRange.toggle ? !entry->selected : message->selectRange.select;
			if (entry->selected) { instance->selectedItemCount++; instance->selectedItemsTotalSize += entry->entry->size; }
		}

		InstanceUpdateItemSelectionCountCommands(instance);
		StringDestroy(&instance->delayedFocusItem);
		InstanceUpdateStatusString(instance);
	} else if (message->type == ES_MSG_LIST_VIEW_SELECT) {
		ListEntry *entry = &instance->listContents[message->selectItem.index.i];
		if (entry->selected) { instance->selectedItemCount--; instance->selectedItemsTotalSize -= entry->entry->size; }
		entry->selected = message->selectItem.isSelected;
		if (entry->selected) { instance->selectedItemCount++; instance->selectedItemsTotalSize += entry->entry->size; }
		InstanceUpdateItemSelectionCountCommands(instance);
		StringDestroy(&instance->delayedFocusItem);
		InstanceUpdateStatusString(instance);
	} else if (message->type == ES_MSG_LIST_VIEW_IS_SELECTED) {
		ListEntry *entry = &instance->listContents[message->selectItem.index.i];
		message->selectItem.isSelected = entry->selected;
	} else if (message->type == ES_MSG_LIST_VIEW_CHOOSE_ITEM) {
		ListEntry *listEntry = &instance->listContents[message->chooseItem.index.i];

		if (listEntry) {
			FolderEntry *entry = listEntry->entry;

			if (entry->isFolder) {
				String path = instance->folder->itemHandler->getPathForChildFolder(instance->folder, entry->GetInternalName());
				InstanceLoadFolder(instance, path);
			} else {
				FileType *fileType = FolderEntryGetType(instance->folder, entry);

				if (fileType->openHandler) {
					String path = StringAllocateAndFormat("%s%s", STRFMT(instance->folder->path), STRFMT(entry->GetInternalName()));
					EsApplicationStartupInformation information = {};
					information.id = fileType->openHandler;
					information.filePath = path.text;
					information.filePathBytes = path.bytes;
					EsApplicationStart(&information);
					StringDestroy(&path);
				} else {
					EsDialogShowAlert(instance->window, INTERFACE_STRING(FileManagerOpenFileError),
							INTERFACE_STRING(FileManagerNoRegisteredApplicationsForFile),
							ES_ICON_DIALOG_ERROR, ES_DIALOG_ALERT_OK_BUTTON); 
				}
			}
		}
	} else if (message->type == ES_MSG_LIST_VIEW_COLUMN_MENU) {
		EsMenu *menu = EsMenuCreate(message->columnMenu.source);
		uint32_t index = (uint32_t) message->columnMenu.index;
		EsMenuAddItem(menu, instance->viewSettings.sortColumn == index ? ES_MENU_ITEM_CHECKED : 0, 
				INTERFACE_STRING(CommonSortAscending), InstanceChangeSortColumn, index);
		EsMenuAddItem(menu, instance->viewSettings.sortColumn == (index | (1 << 8)) ? ES_MENU_ITEM_CHECKED : 0, 
				INTERFACE_STRING(CommonSortDescending), InstanceChangeSortColumn, index | (1 << 8));
		EsMenuShow(menu);
	} else if (message->type == ES_MSG_LIST_VIEW_GET_COLUMN_SORT) {
		if (message->getColumnSort.index == (instance->viewSettings.sortColumn & 0xFF)) {
			return (instance->viewSettings.sortColumn & (1 << 8)) ? ES_LIST_VIEW_COLUMN_SORT_DESCENDING : ES_LIST_VIEW_COLUMN_SORT_ASCENDING;
		}
	} else if (message->type == ES_MSG_LIST_VIEW_CREATE_ITEM) {
		EsElement *element = message->createItem.item;
		element->messageUser = ListItemMessage;
		ListItemCreated(element, message->createItem.index.u, false);
	} else if (message->type == ES_MSG_MOUSE_RIGHT_CLICK) {
		EsMenu *menu = EsMenuCreate(element, ES_MENU_AT_CURSOR);

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

		EsMenuNextColumn(menu);

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
		int index = message->getContent.index.i;

		if (group == PLACES_VIEW_GROUP_DRIVES) {
			// TODO Use namespace lookup.

			if (index == 0) {
				EsBufferFormat(message->getContent.buffer, "%z", interfaceString_FileManagerPlacesDrives);
				message->getContent.icon = ES_ICON_COMPUTER_LAPTOP;
			} else {
				Drive *drive = &drives[index - 1];
				EsBufferFormat(message->getContent.buffer, "%s", drive->information.labelBytes, drive->information.label);
				message->getContent.icon = IconFromDriveType(drive->information.driveType);
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
			if (message->selectItem.index.i == 0) {
				InstanceLoadFolder(instance, StringAllocateAndFormat("%z", interfaceString_FileManagerDrivesPage), LOAD_FOLDER_NO_FOCUS);
			} else {
				Drive *drive = &drives[message->selectItem.index.i - 1];
				InstanceLoadFolder(instance, StringAllocateAndFormat("%s/", drive->prefixBytes, drive->prefix), LOAD_FOLDER_NO_FOCUS);
			}
		} else if (message->selectItem.group == PLACES_VIEW_GROUP_BOOKMARKS && message->selectItem.index.i) {
			String string = bookmarks[message->selectItem.index.i - 1];
			InstanceLoadFolder(instance, StringAllocateAndFormat("%s", STRFMT(string)), LOAD_FOLDER_NO_FOCUS);
		}
	} else if (message->type == ES_MSG_LIST_VIEW_IS_SELECTED) {
		if (message->selectItem.group == PLACES_VIEW_GROUP_DRIVES) {
			if (message->selectItem.index.i == 0) {
				message->selectItem.isSelected = 0 == EsStringCompareRaw(INTERFACE_STRING(FileManagerDrivesPage), 
						instance->path.text, instance->path.bytes);
			} else {
				Drive *drive = &drives[message->selectItem.index.i - 1];
				message->selectItem.isSelected = 0 == EsStringCompareRaw(drive->prefix, drive->prefixBytes, 
						instance->path.text, instance->path.bytes - 1);
			}
		} else if (message->selectItem.group == PLACES_VIEW_GROUP_BOOKMARKS && message->selectItem.index.i) {
			String string = bookmarks[message->selectItem.index.i - 1];
			message->selectItem.isSelected = 0 == EsStringCompareRaw(string.text, string.bytes, 
					instance->path.text, instance->path.bytes);
		}
	} else if (message->type == ES_MSG_LIST_VIEW_CONTEXT_MENU) {
		if (message->selectItem.group == PLACES_VIEW_GROUP_BOOKMARKS && !message->selectItem.index.i) {
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

	if (message->type == ES_MSG_TEXTBOX_ACTIVATE_BREADCRUMB) {
		String section = PathGetSection(instance->folder->path, message->activateBreadcrumb);
		size_t bytes = section.text + section.bytes - instance->folder->path.text;

		String path = StringAllocateAndFormat("%s%z", 
				bytes, instance->folder->path.text,
				instance->folder->path.text[bytes - 1] != '/' ? "/" : "");
		InstanceLoadFolder(instance, path);
	} else if (message->type == ES_MSG_TEXTBOX_EDIT_END) {
		String section;
		section.text = EsTextboxGetContents(instance->breadcrumbBar, &section.bytes);
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
		return ES_HANDLED;
	}

	return 0;
}

#define ADD_BUTTON_TO_TOOLBAR(_command, _label, _icon, _accessKey, _name) \
	{ \
		_name = EsButtonCreate(toolbar, ES_FLAGS_DEFAULT, 0, _label); \
		EsButtonSetIcon(_name, _icon);  \
		EsCommandAddButton(&instance->_command, _name); \
		_name->accessKey = _accessKey; \
	}

#define ADD_BUTTON_TO_STATUS_BAR(_command, _label, _icon, _accessKey, _name) \
	{ \
		_name = EsButtonCreate(statusBar, ES_FLAGS_DEFAULT, ES_STYLE_PUSH_BUTTON_STATUS_BAR, _label); \
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
			&stylePlacesView, ES_STYLE_LIST_ITEM, ES_STYLE_LIST_ITEM, nullptr);
	instance->placesView->accessKey = 'P';
	instance->placesView->messageUser = PlacesViewCallback;
	EsListViewInsertGroup(instance->placesView, PLACES_VIEW_GROUP_BOOKMARKS, ES_LIST_VIEW_GROUP_HAS_HEADER | ES_LIST_VIEW_GROUP_INDENT);
	EsListViewInsertGroup(instance->placesView, PLACES_VIEW_GROUP_DRIVES, ES_LIST_VIEW_GROUP_HAS_HEADER | ES_LIST_VIEW_GROUP_INDENT);
	if (bookmarks.Length()) EsListViewInsert(instance->placesView, PLACES_VIEW_GROUP_BOOKMARKS, 1, bookmarks.Length());
	EsListViewInsert(instance->placesView, PLACES_VIEW_GROUP_DRIVES, 1, drives.Length());

	// Main list:

	instance->list = EsListViewCreate(splitter, ES_CELL_FILL | ES_LIST_VIEW_COLUMNS | ES_LIST_VIEW_MULTI_SELECT, &styleFolderView);
	instance->list->accessKey = 'L';
	instance->list->messageUser = ListCallback;
	EsListViewSetColumns(instance->list, folderOutputColumns, sizeof(folderOutputColumns) / sizeof(folderOutputColumns[0]));
	EsListViewInsertGroup(instance->list, 0);

	// Toolbar:

	EsElement *toolbar = EsWindowGetToolbar(instance->window);
	ADD_BUTTON_TO_TOOLBAR(commandGoBackwards, nullptr, ES_ICON_GO_PREVIOUS_SYMBOLIC, 'B', button);
	ADD_BUTTON_TO_TOOLBAR(commandGoForwards, nullptr, ES_ICON_GO_NEXT_SYMBOLIC, 'F', button);
	ADD_BUTTON_TO_TOOLBAR(commandGoParent, nullptr, ES_ICON_GO_UP_SYMBOLIC, 'U', button);
	EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT);
	instance->breadcrumbBar = EsTextboxCreate(toolbar, ES_CELL_H_FILL | ES_TEXTBOX_EDIT_BASED | ES_TEXTBOX_REJECT_EDIT_IF_LOST_FOCUS, {});
	instance->breadcrumbBar->messageUser = BreadcrumbBarMessage;
	EsTextboxUseBreadcrumbOverlay(instance->breadcrumbBar);
	ADD_BUTTON_TO_TOOLBAR(commandNewFolder, interfaceString_FileManagerNewFolderToolbarItem, ES_ICON_FOLDER_NEW_SYMBOLIC, 'N', instance->newFolderButton);

	// Status bar:

	EsPanel *statusBar = EsPanelCreate(rootPanel, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL, ES_STYLE_PANEL_STATUS_BAR);
	instance->status = EsTextDisplayCreate(statusBar, ES_CELL_H_FILL);
	ADD_BUTTON_TO_STATUS_BAR(commandViewDetails, nullptr, ES_ICON_VIEW_LIST_SYMBOLIC, 0, button);
	ADD_BUTTON_TO_STATUS_BAR(commandViewTiles, nullptr, ES_ICON_VIEW_LIST_COMPACT_SYMBOLIC, 0, button);
	ADD_BUTTON_TO_STATUS_BAR(commandViewThumbnails, nullptr, ES_ICON_VIEW_GRID_SYMBOLIC, 0, button);

	// Load initial folder:

	const EsApplicationStartupInformation *startupInformation = EsInstanceGetStartupInformation(instance);
	String path;

	if (startupInformation && (startupInformation->flags & ES_APPLICATION_STARTUP_MANUAL_PATH)) {
		uintptr_t directoryEnd = startupInformation->filePathBytes;

		for (uintptr_t i = 0; i < (uintptr_t) startupInformation->filePathBytes; i++) {
			if (startupInformation->filePath[i] == '/') {
				directoryEnd = i + 1;
			}
		}

		instance->delayedFocusItem = StringAllocateAndFormat("%s", startupInformation->filePathBytes - directoryEnd, startupInformation->filePath + directoryEnd);
		path = StringAllocateAndFormat("%s", directoryEnd, startupInformation->filePath);
	} else {
		path = StringAllocateAndFormat("0:/");
	}

	InstanceLoadFolder(instance, path, LOAD_FOLDER_START);
}

void InstanceReportError(Instance *instance, int error, EsError code) {
	EsPrint("FM error %d/%d.\n", error, code);

	// TODO Error messages.

	const char *message = interfaceString_FileManagerGenericError;

	if (code == ES_ERROR_FILE_ALREADY_EXISTS) {
		message = interfaceString_FileManagerItemAlreadyExistsError;
	} else if (code == ES_ERROR_FILE_DOES_NOT_EXIST) {
		message = interfaceString_FileManagerItemDoesNotExistError;
	} else if (code == ES_ERROR_FILE_PERMISSION_NOT_GRANTED) {
		message = interfaceString_FileManagerPermissionNotGrantedError;
	}

	EsDialogShowAlert(instance->window, errorTypeStrings[error], -1, message, -1, 
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
