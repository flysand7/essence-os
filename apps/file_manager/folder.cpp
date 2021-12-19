// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

#define NAMESPACE_HANDLER_FILE_SYSTEM (1)
#define NAMESPACE_HANDLER_DRIVES_PAGE (2)
#define NAMESPACE_HANDLER_ROOT (3) // Acts as a container handler where needed.
#define NAMESPACE_HANDLER_INVALID (4) // For when a folder does not exist.

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
	// TODO Recurse mode.
	
	EsNodeType type;

	if (!EsPathExists(STRING(folder->path), &type) || type != ES_NODE_DIRECTORY) {
		return ES_ERROR_FILE_DOES_NOT_EXIST;
	}

	EsVolumeInformation volume;
	folder->readOnly = true;

	if (EsMountPointGetVolumeInformation(STRING(folder->path), &volume)) {
		folder->readOnly = volume.flags & ES_VOLUME_READ_ONLY;
	}

	EsDirectoryChild *buffer = nullptr;
	ptrdiff_t _entryCount = EsDirectoryEnumerateChildren(STRING(folder->path), &buffer);

	if (!ES_CHECK_ERROR(_entryCount)) {
		for (intptr_t i = 0; i < _entryCount; i++) {
			FolderAddEntry(folder, buffer[i].name, buffer[i].nameBytes, &buffer[i]);
		}

		_entryCount = ES_SUCCESS;
	}

	EsHeapFree(buffer);
	return (EsError) _entryCount;
}

void FSDirGetTotalSize(Folder *folder) {
	EsDirectoryChild information;

	if (EsPathQueryInformation(STRING(folder->path), &information)) {
		folder->spaceUsed = information.fileSize;
		folder->spaceTotal = 0;
	}
}

String FSDirGetPathForChild(Folder *folder, FolderEntry *entry) {
	String item = entry->GetInternalName();
	return StringAllocateAndFormat("%s%s%z", STRFMT(folder->path), STRFMT(item), entry->isFolder ? "/" : "");
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

String DrivesPageGetPathForChild(Folder *, FolderEntry *entry) {
	String item = entry->GetInternalName();
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
		.getPathForChild = DrivesPageGetPathForChild,
		.getDefaultViewSettings = DrivesPageGetDefaultViewSettings,
		.enumerate = DrivesPageEnumerate,
	},

	{
		.type = NAMESPACE_HANDLER_FILE_SYSTEM,
		.rootContainerHandlerType = NAMESPACE_HANDLER_DRIVES_PAGE,
		.canCut = true,
		.canCopy = true,
		.canPaste = true,
		.handlesPath = FSDirHandlesPath,
		.getFileType = NamespaceDefaultGetFileType,
		.getVisibleName = NamespaceDefaultGetVisibleName,
		.getTotalSize = FSDirGetTotalSize,
		.getPathForChild = FSDirGetPathForChild,
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

	EsAssert(*itemHandler && *containerHandler); // NAMESPACE_HANDLER_INVALID should handle failure cases.
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
		if (instances[i]->closed) continue;
		EsListViewInsert(instances[i]->placesView, PLACES_VIEW_GROUP_BOOKMARKS, bookmarks.Length(), 1);
	}
}

void BookmarkRemove(String path) {
	for (uintptr_t i = 0; i < bookmarks.Length(); i++) {
		if (0 == EsStringCompareRaw(STRING(bookmarks[i]), STRING(path))) {
			bookmarks.Delete(i);

			for (uintptr_t i = 0; i < instances.Length(); i++) {
				if (instances[i]->closed) continue;
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
		ArenaFree(&folder->entryArena, entry);
	}
}

void FolderDestroy(Folder *folder) {
	for (uintptr_t i = 0; i < folder->entries.slotCount; i++) {
		if (folder->entries.slots[i].key.used) {
			FolderEntryCloseHandle(folder, (FolderEntry *) folder->entries.slots[i].value);
		}
	}

	StringDestroy(&folder->path);
	folder->attachedInstances.Free();
	HashTableFree(&folder->entries, false);
	EsMutexDestroy(&folder->modifyEntriesMutex);
	EsHeapFree(folder);
}

void FolderDetachInstance(Instance *instance) {
	Folder *folder = instance->folder;
	if (!folder) return;
	instance->folder = nullptr;
	folder->attachedInstances.FindAndDeleteSwap(instance, false /* in case the instance stopped loading the folder before it was ready */);
	EsAssert(folder->referenceCount);
	folder->referenceCount--;

	if (!folder->referenceCount) {
		if (folder->refreshing) {
			loadedFolders.FindAndDeleteSwap(folder, true);
			FolderDestroy(folder);
		} else {
			EsAssert(!folder->attachedInstances.Length());
			foldersWithNoAttachedInstances.Add(folder);

			if (foldersWithNoAttachedInstances.Length() > MAXIMUM_FOLDERS_WITH_NO_ATTACHED_INSTANCES) {
				Folder *leastRecentlyUsed = foldersWithNoAttachedInstances[0];
				loadedFolders.FindAndDeleteSwap(leastRecentlyUsed, true);
				foldersWithNoAttachedInstances.Delete(0);
				FolderDestroy(leastRecentlyUsed);
			}
		}
	}
}

FolderEntry *FolderAddEntry(Folder *folder, const char *_name, size_t nameBytes, EsDirectoryChild *information, uint64_t id) {
	char *name = (char *) EsHeapAllocate(nameBytes + 1, false);
	name[nameBytes] = 0;
	EsMemoryCopy(name, _name, nameBytes);

	FolderEntry *entry = (FolderEntry *) HashTableGetLong(&folder->entries, name, nameBytes);

	if (!entry) {
		entry = (FolderEntry *) ArenaAllocate(&folder->entryArena, true);
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

void FolderAddEntryAndUpdateInstances(Folder *folder, const char *name, size_t nameBytes, 
		EsDirectoryChild *information, Instance *selectItem, uint64_t id = 0) {
	if (folder->containerHandler->getTotalSize) {
		folder->containerHandler->getTotalSize(folder);
	}

	FolderEntry *entry = FolderAddEntry(folder, name, nameBytes, information, id);
	ListEntry listEntry = { .entry = entry };

	ThumbnailGenerateIfNeeded(folder, entry, false, true /* modified */);

	for (uintptr_t i = 0; i < folder->attachedInstances.Length(); i++) {
		Instance *instance = folder->attachedInstances[i];
		if (instance->closed) continue;
		listEntry.selected = instance == selectItem;
		InstanceAddSingle(instance, listEntry);
		InstanceUpdateStatusString(instance);
	}
}

uint64_t FolderRemoveEntryAndUpdateInstances(Folder *folder, const char *name, size_t nameBytes) {
	FolderEntry *entry = (FolderEntry *) HashTableGetLong(&folder->entries, name, nameBytes);
	uint64_t id = 0;

	if (entry) {
		for (uintptr_t i = 0; i < folder->attachedInstances.Length(); i++) {
			if (folder->attachedInstances[i]->closed) continue;
			InstanceRemoveSingle(folder->attachedInstances[i], entry);
		}

		id = entry->id;
		HashTableDeleteLong(&folder->entries, name, nameBytes);
		FolderEntryCloseHandle(folder, entry);
	}

	return id;
}

void FolderFileUpdatedAtPath(String path, Instance *instance) {
	path = PathRemoveTrailingSlash(path);
	String file = PathGetName(path);
	String folder = PathGetParent(path);
	EsDirectoryChild information = {};
	bool add = EsPathQueryInformation(STRING(path), &information);

	for (uintptr_t i = 0; i < loadedFolders.Length(); i++) {
		if (loadedFolders[i]->itemHandler->type != NAMESPACE_HANDLER_FILE_SYSTEM) continue;
		if (EsStringCompareRaw(STRING(loadedFolders[i]->path), STRING(folder))) continue;

		EsMutexAcquire(&loadedFolders[i]->modifyEntriesMutex);

		if (loadedFolders[i]->doneInitialEnumeration) {
			if (add) {
				FolderAddEntryAndUpdateInstances(loadedFolders[i], file.text, file.bytes, &information, instance);
			} else {
				FolderRemoveEntryAndUpdateInstances(loadedFolders[i], file.text, file.bytes);
			}
		}

		EsMutexRelease(&loadedFolders[i]->modifyEntriesMutex);
	}
}

void FolderPathMoved(String oldPath, String newPath, bool saveConfiguration) {
	_EsPathAnnouncePathMoved(STRING(oldPath), STRING(newPath));

	for (uintptr_t i = 0; i < loadedFolders.Length(); i++) {
		Folder *folder = loadedFolders[i];

		if (PathReplacePrefix(&folder->path, oldPath, newPath)) {
			for (uintptr_t i = 0; i < folder->attachedInstances.Length(); i++) {
				if (folder->attachedInstances[i]->closed) continue;
				InstanceFolderPathChanged(folder->attachedInstances[i], false);
			}
		}
	}

	for (uintptr_t i = 0; i < openDocuments.Length(); i++) {
		PathReplacePrefix(&openDocuments[i], oldPath, newPath);
	}

	for (uintptr_t i = 0; i < bookmarks.Length(); i++) {
		PathReplacePrefix(&bookmarks[i], oldPath, newPath);
	}

	for (uintptr_t i = 0; i < instances.Length(); i++) {
		if (instances[i]->closed) continue;
		EsListViewInvalidateAll(instances[i]->placesView);

		for (uintptr_t j = 0; j < instances[i]->pathBackwardHistory.Length(); j++) {
			HistoryEntry *entry = &instances[i]->pathBackwardHistory[j];
			PathReplacePrefix(&entry->path, oldPath, newPath);
		}

		for (uintptr_t j = 0; j < instances[i]->pathForwardHistory.Length(); j++) {
			HistoryEntry *entry = &instances[i]->pathForwardHistory[j];
			PathReplacePrefix(&entry->path, oldPath, newPath);
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

	if (saveConfiguration) {
		ConfigurationSave();
	}
}

void FolderAttachInstance(Instance *instance, String path, bool recurse) {
	Folder *folder = nullptr;
	bool driveRemoved = false;

	// Check if we've already loaded the folder.

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

	folder = (Folder *) EsHeapAllocate(sizeof(Folder), true);
	folder->path = StringDuplicate(path);
	folder->recurse = recurse;
	NamespaceFindHandlersForPath(&folder->itemHandler, &folder->containerHandler, path);
	folder->cEmptyMessage = interfaceString_FileManagerEmptyFolderView;
	folder->driveRemoved = driveRemoved;
	ArenaInitialise(&folder->entryArena, 1048576, sizeof(FolderEntry));
	loadedFolders.Add(folder);

	success:;

	foldersWithNoAttachedInstances.FindAndDeleteSwap(folder, false);
	folder->referenceCount++;
	FolderDetachInstance(instance);
	instance->folder = folder;
}

void FolderRefresh(Folder *folder) {
	if (folder->refreshing) {
		return;
	}

	folder->refreshing = true;

	Array<Instance *> instancesToRefresh = {};

	for (uintptr_t i = 0; i < folder->attachedInstances.Length(); i++) {
		if (folder->attachedInstances[i]->closed) continue;
		instancesToRefresh.Add(folder->attachedInstances[i]);
	}

	for (uintptr_t i = 0; i < instancesToRefresh.Length(); i++) {
		InstanceLoadFolder(instancesToRefresh[i], StringDuplicate(folder->path), LOAD_FOLDER_REFRESH);
	}

	instancesToRefresh.Free();
}
