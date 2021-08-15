#define NAMESPACE_HANDLER_FILE_SYSTEM (1)
#define NAMESPACE_HANDLER_DRIVES_PAGE (2)
#define NAMESPACE_HANDLER_ROOT (3) // Acts as a container handler where needed.
#define NAMESPACE_HANDLER_INVALID (4) // For when a folder does not exist.

EsMutex loadedFoldersMutex;
Array<Folder *> loadedFolders;

#define MAXIMUM_FOLDERS_WITH_NO_ATTACHED_INSTANCES (20)
Array<Folder *> foldersWithNoAttachedInstances;

/////////////////////////////////

bool FSDirHandlesPath(String path) {
	EsDirectoryChild information;

	if (EsPathQueryInformation(STRING(path), &information)) {
		return information.type == ES_NODE_DIRECTORY;
	} else {
		return false;
	}
}

EsError FSDirCreateChildFolder(Folder *folder, String *name, bool findUniqueName) {
	size_t pathBytes;
	char *path;

	if (findUniqueName) {
		const char *extra = "               ";
		path = EsStringAllocateAndFormat(&pathBytes, "%s%s%z", STRFMT(folder->path), STRFMT((*name)), extra);
		pathBytes = EsPathFindUniqueName(path, pathBytes - EsCStringLength(extra), pathBytes);
		StringDestroy(name);
		*name = StringAllocateAndFormat("%s", pathBytes - folder->path.bytes, path + folder->path.bytes);
	} else {
		path = EsStringAllocateAndFormat(&pathBytes, "%s%s", STRFMT(folder->path), STRFMT((*name)));
	}

	EsError error = EsPathCreate(path, pathBytes, ES_NODE_DIRECTORY, false);
	EsHeapFree(path);
	return error;
}

EsError FSDirRenameItem(Folder *folder, String oldName, String newName) {
	size_t oldPathBytes;
	char *oldPath = EsStringAllocateAndFormat(&oldPathBytes, "%s%s", STRFMT(folder->path), STRFMT(oldName));

	size_t newPathBytes;
	char *newPath = EsStringAllocateAndFormat(&newPathBytes, "%s%s", STRFMT(folder->path), STRFMT(newName));

	EsError error = EsPathMove(oldPath, oldPathBytes, newPath, newPathBytes);

	EsHeapFree(oldPath);
	EsHeapFree(newPath);

	return error;
};

EsError FSDirEnumerate(Folder *folder) {
	// Get the initial directory children.
	// TODO Recurse mode.

	EsNodeType type;

	if (!EsPathExists(STRING(folder->path), &type) || type != ES_NODE_DIRECTORY) {
		return ES_ERROR_FILE_DOES_NOT_EXIST;
	}

	EsDirectoryChild *buffer = nullptr;
	ptrdiff_t _entryCount = EsDirectoryEnumerateChildren(STRING(folder->path), &buffer);

	if (ES_CHECK_ERROR(_entryCount)) {
		EsHeapFree(buffer);
		return (EsError) _entryCount;
	}

	FolderAddEntries(folder, buffer, _entryCount);
	EsHeapFree(buffer);

	return ES_SUCCESS;
}

void FSDirGetTotalSize(Folder *folder) {
	EsDirectoryChild information;

	if (EsPathQueryInformation(STRING(folder->path), &information)) {
		folder->spaceUsed = information.fileSize;
		folder->spaceTotal = 0;
	}
}

String FSDirGetPathForChildFolder(Folder *folder, String item) {
	return StringAllocateAndFormat("%s%s/", STRFMT(folder->path), STRFMT(item));
}

/////////////////////////////////

bool DrivesPageHandlesPath(String path) { 
	return StringEquals(path, StringFromLiteralWithSize(INTERFACE_STRING(FileManagerDrivesPage)));
}

uint32_t DrivesPageGetFileType(String path) {
	path = PathRemoveTrailingSlash(path);
	String string = PathGetSection(path, PathCountSections(path));
	EsVolumeInformation volume;

	if (EsMountPointGetVolumeInformation(STRING(string), &volume)) {
		if (volume.driveType == ES_DRIVE_TYPE_HDD             ) return KNOWN_FILE_TYPE_DRIVE_HDD;
		if (volume.driveType == ES_DRIVE_TYPE_SSD             ) return KNOWN_FILE_TYPE_DRIVE_SSD;
		if (volume.driveType == ES_DRIVE_TYPE_CDROM           ) return KNOWN_FILE_TYPE_DRIVE_CDROM;
		if (volume.driveType == ES_DRIVE_TYPE_USB_MASS_STORAGE) return KNOWN_FILE_TYPE_DRIVE_USB_MASS_STORAGE;
		return KNOWN_FILE_TYPE_DRIVE_HDD;
	}

	return KNOWN_FILE_TYPE_DRIVE_HDD;
}

void DrivesPageGetTotalSize(Folder *folder) {
	EsVolumeInformation information;

	if (EsMountPointGetVolumeInformation(STRING(folder->path), &information)) {
		folder->spaceUsed = information.spaceUsed;
		folder->spaceTotal = information.spaceTotal;
	}
}

void DrivesPageGetVisibleName(EsBuffer *buffer, String path) {
	path = PathRemoveTrailingSlash(path);
	String string = PathGetSection(path, PathCountSections(path));
	EsVolumeInformation volume;

	if (EsMountPointGetVolumeInformation(STRING(string), &volume)) {
		EsBufferFormat(buffer, "%s", volume.labelBytes, volume.label);
		return;
	}

	EsBufferFormat(buffer, "%s", STRFMT(string));
}

EsError DrivesPageEnumerate(Folder *folder) {
	EsMutexAcquire(&drivesMutex);
	EsDirectoryChild information = {};
	information.type = ES_NODE_DIRECTORY;

	for (uintptr_t i = 0; i < drives.Length(); i++) {
		Drive *drive = &drives[i];
		information.fileSize = drive->information.spaceTotal;
		FolderAddEntry(folder, drive->prefix, drive->prefixBytes, &information);
	}

	EsMutexRelease(&drivesMutex);
	return ES_SUCCESS;
}

String DrivesPageGetPathForChildFolder(Folder *, String item) {
	return StringAllocateAndFormat("%s/", STRFMT(item));
}

void DrivesPageGetDefaultViewSettings(Folder *, FolderViewSettings *settings) {
	settings->viewType = VIEW_TILES;
}

/////////////////////////////////

uint32_t NamespaceDefaultGetFileType(String) {
	return KNOWN_FILE_TYPE_DIRECTORY;
}

void NamespaceDefaultGetVisibleName(EsBuffer *buffer, String path) {
	String string = PathGetSection(path, -1);
	EsBufferFormat(buffer, "%s", STRFMT(string));
}

uint32_t NamespaceRootGetFileType(String path) {
	if (StringEquals(path, StringFromLiteralWithSize(INTERFACE_STRING(FileManagerDrivesPage)))) {
		return KNOWN_FILE_TYPE_DRIVES_PAGE;
	} else {
		return KNOWN_FILE_TYPE_DIRECTORY;
	}
}

void NamespaceRootGetVisibleName(EsBuffer *buffer, String path) {
	EsBufferFormat(buffer, "%s", STRFMT(StringSlice(path, 0, path.bytes - 1)));
}

EsError NamespaceInvalidEnumerate(Folder *folder) {
	folder->cEmptyMessage = folder->driveRemoved ? interfaceString_FileManagerInvalidDrive : interfaceString_FileManagerInvalidPath;
	return ES_SUCCESS;
}

NamespaceHandler namespaceHandlers[] = {
	{
		.type = NAMESPACE_HANDLER_DRIVES_PAGE,
		.rootContainerHandlerType = NAMESPACE_HANDLER_ROOT,
		.handlesPath = DrivesPageHandlesPath,
		.getFileType = DrivesPageGetFileType,
		.getVisibleName = DrivesPageGetVisibleName,
		.getTotalSize = DrivesPageGetTotalSize,
		.getPathForChildFolder = DrivesPageGetPathForChildFolder,
		.getDefaultViewSettings = DrivesPageGetDefaultViewSettings,
		.enumerate = DrivesPageEnumerate,
	},

	{
		.type = NAMESPACE_HANDLER_FILE_SYSTEM,
		.rootContainerHandlerType = NAMESPACE_HANDLER_DRIVES_PAGE,
		.handlesPath = FSDirHandlesPath,
		.getFileType = NamespaceDefaultGetFileType,
		.getVisibleName = NamespaceDefaultGetVisibleName,
		.getTotalSize = FSDirGetTotalSize,
		.getPathForChildFolder = FSDirGetPathForChildFolder,
		.createChildFolder = FSDirCreateChildFolder,
		.renameItem = FSDirRenameItem,
		.enumerate = FSDirEnumerate,
	}, 

	{
		.type = NAMESPACE_HANDLER_ROOT,
		.handlesPath = [] (String) { return false; },
		.getFileType = NamespaceRootGetFileType,
		.getVisibleName = NamespaceRootGetVisibleName,
	}, 

	{
		.type = NAMESPACE_HANDLER_INVALID,
		.rootContainerHandlerType = NAMESPACE_HANDLER_ROOT,
		.handlesPath = [] (String) { return true; },
		.getFileType = NamespaceDefaultGetFileType,
		.getVisibleName = NamespaceDefaultGetVisibleName,
		.enumerate = NamespaceInvalidEnumerate,
	},
};

void NamespaceFindHandlersForPath(NamespaceHandler **itemHandler, NamespaceHandler **containerHandler, String path) {
	String pathContainer = PathGetParent(path);
	*itemHandler = *containerHandler = nullptr;

	for (uintptr_t i = 0; i < sizeof(namespaceHandlers) / sizeof(namespaceHandlers[0]); i++) {
		if (!(*itemHandler) && namespaceHandlers[i].handlesPath(path)) {
			*itemHandler = &namespaceHandlers[i];
		} 
		
		if (pathContainer.bytes && !(*containerHandler) && namespaceHandlers[i].handlesPath(pathContainer)) {
			*containerHandler = &namespaceHandlers[i];
		}
	}

	if (!pathContainer.bytes && (*itemHandler)) {
		for (uintptr_t i = 0; i < sizeof(namespaceHandlers) / sizeof(namespaceHandlers[0]); i++) {
			if (namespaceHandlers[i].type == (*itemHandler)->rootContainerHandlerType) {
				*containerHandler = &namespaceHandlers[i];
				break;
			}
		}
	}
}

uint32_t NamespaceGetIcon(String path) {
	NamespaceHandler *itemHandler, *containerHandler;
	NamespaceFindHandlersForPath(&itemHandler, &containerHandler, path);

	if (containerHandler) {
		return knownFileTypes[containerHandler->getFileType(path)].iconID;
	} else {
		return ES_ICON_FOLDER;
	}
}

void NamespaceGetVisibleName(EsBuffer *buffer, String path) {
	NamespaceHandler *itemHandler, *containerHandler;
	NamespaceFindHandlersForPath(&itemHandler, &containerHandler, path);

	if (containerHandler) {
		containerHandler->getVisibleName(buffer, path);
	} else {
		String string = PathGetSection(path, -1);
		EsBufferFormat(buffer, "%s", STRFMT(string));
	}
}

/////////////////////////////////

void BookmarkAdd(String path, bool saveConfiguration = true) {
	bookmarks.Add(StringDuplicate(path));
	if (saveConfiguration) ConfigurationSave();

	for (uintptr_t i = 0; i < instances.Length(); i++) {
		EsListViewInsert(instances[i]->placesView, PLACES_VIEW_GROUP_BOOKMARKS, bookmarks.Length(), 1);
	}
}

void BookmarkRemove(String path) {
	for (uintptr_t i = 0; i < bookmarks.Length(); i++) {
		if (0 == EsStringCompareRaw(STRING(bookmarks[i]), STRING(path))) {
			bookmarks.Delete(i);

			for (uintptr_t i = 0; i < instances.Length(); i++) {
				EsListViewRemove(instances[i]->placesView, PLACES_VIEW_GROUP_BOOKMARKS, i + 1, 1);
			}

			ConfigurationSave();
			return;
		}
	}

	EsAssert(false);
}

/////////////////////////////////

void FolderEntryCloseHandle(Folder *folder, FolderEntry *entry) {
	entry->handles--;

	if (!entry->handles) {
		if (entry->name != entry->internalName) {
			EsHeapFree(entry->internalName);
		}

		EsHeapFree(entry->name);
		EsArenaFree(&folder->entryArena, entry);
	}
}

void FolderDestroy(Folder *folder) {
	for (uintptr_t i = 0; i < folder->entries.slotCount; i++) {
		if (folder->entries.slots[i].key.used) {
			FolderEntryCloseHandle(folder, (FolderEntry *) folder->entries.slots[i].value);
		}
	}

	EsHeapFree(folder);
}

void FolderDetachInstance(Instance *instance) {
	Folder *folder = instance->folder;
	if (!folder) return;
	instance->folder = nullptr;
	folder->attachedInstances.FindAndDeleteSwap(instance, true);

	if (!folder->attachedInstances.Length() && !folder->attachingInstances.Length()) {
		foldersWithNoAttachedInstances.Add(folder);

		if (foldersWithNoAttachedInstances.Length() > MAXIMUM_FOLDERS_WITH_NO_ATTACHED_INSTANCES) {
			Folder *leastRecentlyUsed = foldersWithNoAttachedInstances[0];
			loadedFolders.FindAndDeleteSwap(leastRecentlyUsed, true);
			foldersWithNoAttachedInstances.Delete(0);
			FolderDestroy(leastRecentlyUsed);
		}
	}
}

FolderEntry *FolderAddEntry(Folder *folder, const char *_name, size_t nameBytes, EsDirectoryChild *information, uint64_t id) {
	char *name = (char *) EsHeapAllocate(nameBytes + 1, false);
	name[nameBytes] = 0;
	EsMemoryCopy(name, _name, nameBytes);

	FolderEntry *entry = (FolderEntry *) HashTableGetLong(&folder->entries, name, nameBytes);

	if (!entry) {
		entry = (FolderEntry *) EsArenaAllocate(&folder->entryArena, true);
		HashTablePutLong(&folder->entries, name, nameBytes, entry, false);

		static uint64_t nextEntryID = 1;
		entry->id = id ?: __sync_fetch_and_add(&nextEntryID, 1);
		entry->handles = 1; 
		entry->name = name;
		entry->nameBytes = nameBytes;
		entry->extensionOffset = PathGetExtension(entry->GetName()).text - name;
		entry->internalName = name;
		entry->internalNameBytes = nameBytes;

		if (folder->itemHandler->getVisibleName != NamespaceDefaultGetVisibleName) {
			String path = StringAllocateAndFormat("%s%s", STRFMT(folder->path), STRFMT(entry->GetName()));
			uint8_t _buffer[256];
			EsBuffer buffer = { .out = _buffer, .bytes = sizeof(_buffer) };
			folder->itemHandler->getVisibleName(&buffer, path);
			entry->nameBytes = buffer.position;
			entry->name = (char *) EsHeapAllocate(buffer.position, false);
			EsMemoryCopy(entry->name, _buffer, buffer.position);
			StringDestroy(&path);
		}
	} else {
		EsHeapFree(name);
		name = entry->name;
		entry->previousSize = entry->size;
	}

	entry->size = information->fileSize;
	entry->isFolder = information->type == ES_NODE_DIRECTORY;
	entry->sizeUnknown = entry->isFolder && information->directoryChildren == ES_DIRECTORY_CHILDREN_UNKNOWN;

	return entry;
}

void FolderAddEntries(Folder *folder, EsDirectoryChild *buffer, size_t entryCount) {
	for (uintptr_t i = 0; i < entryCount; i++) {
		FolderAddEntry(folder, buffer[i].name, buffer[i].nameBytes, &buffer[i]);
	}
}

void FolderAddEntryAndUpdateInstances(Folder *folder, const char *name, size_t nameBytes, 
		EsDirectoryChild *information, Instance *selectItem, bool mutexAlreadyAcquired, uint64_t id = 0) {
	if (!mutexAlreadyAcquired) EsMutexAcquire(&loadedFoldersMutex);

	if (folder->containerHandler->getTotalSize) {
		folder->containerHandler->getTotalSize(folder);
	}

	FolderEntry *entry = FolderAddEntry(folder, name, nameBytes, information, id);
	ListEntry listEntry = { .entry = entry };

	for (uintptr_t i = 0; i < folder->attachedInstances.Length(); i++) {
		Instance *instance = folder->attachedInstances[i];
		listEntry.selected = instance == selectItem;
		InstanceAddSingle(instance, listEntry);
		InstanceUpdateStatusString(instance);
	}

	if (!mutexAlreadyAcquired) EsMutexRelease(&loadedFoldersMutex);
}

uint64_t FolderRemoveEntryAndUpdateInstances(Folder *folder, const char *name, size_t nameBytes) {
	EsMutexAcquire(&loadedFoldersMutex);

	FolderEntry *entry = (FolderEntry *) HashTableGetLong(&folder->entries, name, nameBytes);
	uint64_t id = 0;

	if (entry) {
		for (uintptr_t i = 0; i < folder->attachedInstances.Length(); i++) {
			InstanceRemoveSingle(folder->attachedInstances[i], entry);
		}

		id = entry->id;
		HashTableDeleteLong(&folder->entries, name, nameBytes);
		FolderEntryCloseHandle(folder, entry);
	}

	EsMutexRelease(&loadedFoldersMutex);
	return id;
}

void FolderPathMoved(Instance *instance, String oldPath, String newPath) {
	_EsPathAnnouncePathMoved(instance, STRING(oldPath), STRING(newPath));

	EsMutexAcquire(&loadedFoldersMutex);

	for (uintptr_t i = 0; i < loadedFolders.Length(); i++) {
		Folder *folder = loadedFolders[i];

		if (PathReplacePrefix(&folder->path, oldPath, newPath)) {
			for (uintptr_t i = 0; i < folder->attachedInstances.Length(); i++) {
				InstanceFolderPathChanged(folder->attachedInstances[i], false);
			}
		}
	}

	EsMutexRelease(&loadedFoldersMutex);

	for (uintptr_t i = 0; i < bookmarks.Length(); i++) {
		PathReplacePrefix(&bookmarks[i], oldPath, newPath);
	}

	for (uintptr_t i = 0; i < instances.Length(); i++) {
		EsListViewInvalidateAll(instances[i]->placesView);

		for (uintptr_t j = 0; j < instances[i]->pathBackwardHistory.Length(); j++) {
			HistoryEntry *entry = &instances[i]->pathBackwardHistory[j];
			PathReplacePrefix(&entry->path, oldPath, newPath);

			if (StringStartsWith(oldPath, entry->path) && StringEquals(entry->focusedItem, StringSlice(oldPath, entry->path.bytes, -1))) {
				StringDestroy(&entry->focusedItem);
				entry->focusedItem = StringDuplicate(StringSlice(newPath, entry->path.bytes, -1));
			}
		}

		for (uintptr_t j = 0; j < instances[i]->pathForwardHistory.Length(); j++) {
			HistoryEntry *entry = &instances[i]->pathForwardHistory[j];
			PathReplacePrefix(&entry->path, oldPath, newPath);

			if (StringStartsWith(oldPath, entry->path) && StringEquals(entry->focusedItem, StringSlice(oldPath, entry->path.bytes, -1))) {
				StringDestroy(&entry->focusedItem);
				entry->focusedItem = StringDuplicate(StringSlice(newPath, entry->path.bytes, -1));
			}
		}
	}

	for (uintptr_t i = 0; i < folderViewSettings.Length(); i++) {
		String knownPath = StringFromLiteralWithSize(folderViewSettings[i].path, folderViewSettings[i].pathBytes);

		if (knownPath.bytes - oldPath.bytes + newPath.bytes >= sizeof(folderViewSettings[i].path)) {
			continue;
		}

		if (!PathHasPrefix(knownPath, oldPath)) {
			continue;
		}

		String after = StringSlice(knownPath, oldPath.bytes, -1);
		folderViewSettings[i].pathBytes = EsStringFormat(folderViewSettings[i].path, sizeof(folderViewSettings[i].path), 
				"%s%s", STRFMT(newPath), STRFMT(after));
	}

	ConfigurationSave();
}

EsError FolderAttachInstance(Instance *instance, String path, bool recurse, Folder **newFolder) {
	// TODO Don't modify attachedInstances/attachingInstances/loadedFolders without the message mutex!
	// 	And then we can remove loadedFoldersMutex.

	// (Called on the blocking task thread.)
	
	Folder *folder = nullptr;
	NamespaceHandler *itemHandler = nullptr, *containerHandler = nullptr;
	EsError error;
	bool driveRemoved = false;

	// Check if we've already loaded the folder.

	EsMutexAcquire(&loadedFoldersMutex);

	for (uintptr_t i = 0; i < loadedFolders.Length(); i++) {
		if (StringEquals(loadedFolders[i]->path, path) && loadedFolders[i]->recurse == recurse) {
			if (!loadedFolders[i]->refreshing) {
				folder = loadedFolders[i];
				goto success;
			} else {
				driveRemoved = loadedFolders[i]->driveRemoved;
			}
		}
	}

	EsMutexRelease(&loadedFoldersMutex);

	// Find the handler for the path.

	NamespaceFindHandlersForPath(&itemHandler, &containerHandler, path);

	if (!itemHandler || !containerHandler) {
		return ES_ERROR_FILE_DOES_NOT_EXIST;
	}

	// Load a new folder.

	folder = (Folder *) EsHeapAllocate(sizeof(Folder), true);

	folder->path = StringDuplicate(path);
	folder->recurse = recurse;
	folder->itemHandler = itemHandler;
	folder->containerHandler = containerHandler;
	folder->cEmptyMessage = interfaceString_FileManagerEmptyFolderView;
	folder->driveRemoved = driveRemoved;

	EsArenaInitialise(&folder->entryArena, 1048576, sizeof(FolderEntry));

	// TODO Make this asynchronous for some folder providers, or recursive requests 
	// 	(that is, immediately present to the user while streaming data in).
	error = itemHandler->enumerate(folder);

	folder->driveRemoved = false;
	folder->refreshing = false;

	if (error != ES_SUCCESS) {
		StringDestroy(&folder->path);
		EsHeapFree(folder);
		return error;
	}

	if (containerHandler->getTotalSize) {
		containerHandler->getTotalSize(folder);
	}

	EsMutexAcquire(&loadedFoldersMutex);
	loadedFolders.Add(folder);

	success:;

	foldersWithNoAttachedInstances.FindAndDeleteSwap(folder, false);
	folder->attachingInstances.Add(instance);

	EsMutexRelease(&loadedFoldersMutex);

	*newFolder = folder;
	return ES_SUCCESS;
}

void FolderRefresh(Folder *folder) {
	// EsMutexAssertLocked(&loadedFoldersMutex);

	if (folder->refreshing) {
		return;
	}

	folder->refreshing = true;

	for (uintptr_t i = 0; i < folder->attachedInstances.Length(); i++) {
		InstanceLoadFolder(folder->attachedInstances[i], StringDuplicate(folder->path), LOAD_FOLDER_REFRESH);
	}
}
