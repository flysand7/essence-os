// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

#define ES_INSTANCE_TYPE Instance
#include <essence.h>
#include <shared/strings.cpp>
#include <shared/hash_table.cpp>
#include <shared/array.cpp>
#include <shared/arena.cpp>

// TODO Possible candidates for moving in the core API:
// 	- String/paths utils
// 	- Blocking task systems

// TODO Don't show modals if a folder can't be loaded.
// 	Instead, show a list view with an error message,
// 	and disable interactions.

#define SETTINGS_FILE "|Settings:/Default.ini"

#define ERROR_LOAD_FOLDER (1)
#define ERROR_NEW_FOLDER (2)
#define ERROR_RENAME_ITEM (3)

#define PLACES_VIEW_GROUP_BOOKMARKS (0)
#define PLACES_VIEW_GROUP_DRIVES (1)

#define MESSAGE_BLOCKING_TASK_COMPLETE ((EsMessageType) (ES_MSG_USER_START + 1))
#define MESSAGE_NON_BLOCKING_TASK_COMPLETE ((EsMessageType) (ES_MSG_USER_START + 2))

#define VIEW_DETAILS (0)
#define VIEW_TILES (1)
#define VIEW_THUMBNAILS (2)

const char *errorTypeStrings[] = {
	interfaceString_FileManagerUnknownError,
	interfaceString_FileManagerOpenFolderError,
	interfaceString_FileManagerNewFolderError,
	interfaceString_FileManagerRenameItemError,
};

#include "string.cpp"

#define COLUMN_NAME (0)
#define COLUMN_TYPE (1)
#define COLUMN_SIZE (2)

#define LOAD_FOLDER_BACK     (1)
#define LOAD_FOLDER_FORWARD  (2)
#define LOAD_FOLDER_START    (3)
#define LOAD_FOLDER_REFRESH  (4)
#define LOAD_FOLDER_NO_FOCUS (1 << 8)

struct FolderEntry {
	uint16_t handles;
	bool isFolder, sizeUnknown;
	uint8_t nameBytes, extensionOffset, internalNameBytes; // 0 -> 256.
	char *name, *internalName;
	EsFileOffset size, previousSize;
	uint64_t id;

	inline String GetName() { 
		return { .text = name, .bytes = nameBytes ?: 256u, .allocated = nameBytes ?: 256u }; 
	}

	inline String GetInternalName() { 
		return { .text = internalName, .bytes = internalNameBytes ?: 256u, .allocated = internalNameBytes ?: 256u }; 
	}

	inline String GetExtension() { 
		uintptr_t offset = extensionOffset ?: 256u;
		return { .text = name + offset, .bytes = (nameBytes ?: 256u) - offset }; 
	}
};

struct ListEntry {
	FolderEntry *entry;
	bool selected;
};

struct Task {
	EsGeneric context, context2;
	String string, string2;
	uint64_t id;
	EsError result;
	const char *cDescription;
	Instance *instance;

	void (*callback)(Instance *instance, Task *task);
	void (*then)(Instance *instance, Task *task); // Called on the main thread.
};

struct HistoryEntry {
	String path;
	String focusedItem;
};

struct Drive {
	char *prefix;
	size_t prefixBytes;
	EsObjectID deviceID;
	EsVolumeInformation information;
};

struct FolderViewSettings {
	uint16_t sortColumn;
	uint8_t viewType;
};

struct FolderViewSettingsEntry {
	char path[160];
	FolderViewSettings settings;
	uint8_t pathBytes;
};

struct Thumbnail {
	uint32_t *bits;
	uint32_t width, height;
	uintptr_t referenceCount;
	uint32_t generatingTasksInProgress;
};

struct Instance : EsInstance {
	// Interface elements.

	EsListView *list;
	EsListView *placesView;
	EsTextbox *breadcrumbBar;
	EsButton *newFolderButton;
	EsTextDisplay *status;
	EsDialog *blockingDialog;

	union {
		struct {
			EsTextbox *textbox;
			uintptr_t index;
		} rename;
	};

	// Path and history.

	String path;
	Array<HistoryEntry> pathBackwardHistory;
	Array<HistoryEntry> pathForwardHistory;

	// Commands.

	EsCommand commandGoBackwards, commandGoForwards, commandGoParent;
	EsCommand commandNewFolder, commandRename;
	EsCommand commandViewDetails, commandViewTiles, commandViewThumbnails;
	EsCommand commandRefresh;

	// Active folder.

	struct Folder *folder;

	// Sorted and filtered list contents.

	Array<ListEntry> listContents;

	size_t selectedItemCount;
	EsFileOffset selectedItemsTotalSize;

	String delayedFocusItem;

	FolderViewSettings viewSettings;

	// Asynchronous tasks.

	struct PasteTask *issuedPasteTask;
	
	// Tasks that block the use of the instance, but display progress and can be (optionally) cancelled.
	// Shows the dialog after some threshold.
#define BLOCKING_TASK_DIALOG_THRESHOLD_MS (100)
	Task blockingTask;
	volatile bool blockingTaskInProgress, blockingTaskReachedTimeout;
	volatile uint32_t blockingTaskID;

	bool closed; // The window has been destroyed (or is about to be).
};

struct NamespaceHandler {
	uint8_t type;
	uint8_t rootContainerHandlerType;

	bool canCut, canCopy, canPaste;

	bool (*handlesPath)(String path);

	uint32_t (*getFileType)(String path);
	void (*getVisibleName)(EsBuffer *buffer, String path);
	void (*getTotalSize)(Folder *folder); // Possibly called on the blocking task thread.
	String (*getPathForChild)(Folder *folder, FolderEntry *entry);
	void (*getDefaultViewSettings)(Folder *folder, FolderViewSettings *settings);

	// Called on the blocking task thread:
	EsError (*createChildFolder)(Folder *folder, String *name, bool findUniqueName); // Optional.
	EsError (*renameItem)(Folder *folder, String oldName, String name); // Optional.
	EsError (*enumerate)(Folder *folder);
};

struct Folder {
	HashTable entries;
	Arena entryArena;

	Array<Instance *> attachedInstances; // NOTE Check Instance::closed is false before accessing the UI!
	uintptr_t referenceCount;

	String path;
	bool recurse;
	bool refreshing;
	bool driveRemoved;
	bool readOnly;

	bool doneInitialEnumeration;
	EsMutex modifyEntriesMutex;

	EsFileOffset spaceTotal;
	EsFileOffset spaceUsed;

	NamespaceHandler *itemHandler, *containerHandler;
	const char *cEmptyMessage;
};

void InstanceReportError(struct Instance *instance, int error, EsError code);
bool InstanceLoadFolder(Instance *instance, String path /* takes ownership */, int historyMode = 0);
void InstanceUpdateStatusString(Instance *instance);
void InstanceViewSettingsUpdated(Instance *instance);
void InstanceRefreshViewType(Instance *instance, bool startTransition);
void InstanceFolderPathChanged(Instance *instance, bool fromLoadFolder);
void InstanceAddContents(struct Instance *instance, HashTable *newEntries);
void InstanceAddSingle(struct Instance *instance, ListEntry newEntry);
void InstanceRemoveContents(struct Instance *instance);
void InstanceSelectByName(Instance *instance, String name, bool addToExistingSelection, bool focusItem);
ListEntry InstanceRemoveSingle(Instance *instance, FolderEntry *folderEntry);
ListEntry *InstanceGetSelectedListEntry(Instance *instance);
void ListItemCreated(EsElement *element, uintptr_t index, bool fromFolderRename);
FolderEntry *FolderAddEntry(Folder *folder, const char *_name, size_t nameBytes, EsDirectoryChild *information, uint64_t id = 0);
void FolderAddEntries(Folder *folder, EsDirectoryChild *buffer, size_t entryCount);
uint32_t NamespaceDefaultGetFileType(String);
void ThumbnailGenerateIfNeeded(Folder *folder, FolderEntry *entry, bool fromFolderRename, bool modified);

Array<Drive> drives;
EsMutex drivesMutex;

Array<Instance *> instances; // NOTE Check Instance::closed is false before accessing the UI!

Array<String> bookmarks;
#define FOLDER_VIEW_SETTINGS_MAXIMUM_ENTRIES (10000)
Array<FolderViewSettingsEntry> folderViewSettings;
HashStore<uint64_t, Thumbnail> thumbnailCache;

Array<String> openDocuments;

// Styles.

const EsStyle styleFolderView = {
	.inherit = ES_STYLE_LIST_VIEW,

	.metrics = {
		.mask = ES_THEME_METRICS_MINIMUM_WIDTH | ES_THEME_METRICS_PREFERRED_WIDTH,
		.preferredWidth = 200,
		.minimumWidth = 150,
	},
};

const EsStyle styleFolderViewTiled = {
	.inherit = ES_STYLE_LIST_VIEW,

	.metrics = {
		.mask = ES_THEME_METRICS_MINIMUM_WIDTH | ES_THEME_METRICS_PREFERRED_WIDTH | ES_THEME_METRICS_GAP_WRAP | ES_THEME_METRICS_GAP_MINOR,
		.preferredWidth = 200,
		.minimumWidth = 150,
		.gapMinor = 5,
		.gapWrap = 5,
	},
};

const EsStyle styleFolderItemThumbnail = {
	.inherit = ES_STYLE_LIST_ITEM_TILE,

	.metrics = {
		.mask = ES_THEME_METRICS_PREFERRED_WIDTH | ES_THEME_METRICS_PREFERRED_HEIGHT 
			| ES_THEME_METRICS_ICON_SIZE | ES_THEME_METRICS_LAYOUT_VERTICAL | ES_THEME_METRICS_INSETS | ES_THEME_METRICS_TEXT_ALIGN,
		.insets = ES_RECT_2(5, 10),
		.preferredWidth = 170,
		.preferredHeight = 150,
		.textAlign = ES_TEXT_H_CENTER | ES_TEXT_V_CENTER | ES_TEXT_ELLIPSIS,
		.iconSize = 80,
		.layoutVertical = true,
	},
};

const EsStyle stylePlacesView = {
	.inherit = ES_STYLE_LIST_VIEW,

	.metrics = {
		.mask = ES_THEME_METRICS_MINIMUM_WIDTH | ES_THEME_METRICS_PREFERRED_WIDTH | ES_THEME_METRICS_INSETS
			| ES_THEME_METRICS_GAP_WRAP | ES_THEME_METRICS_GAP_MAJOR,
		.insets = ES_RECT_1(8),
		.preferredWidth = 200,
		.minimumWidth = 150,
		.gapMajor = 16,
		.gapWrap = 16,
	},
};

void BlockingTaskThread(EsGeneric _instance) {
	Instance *instance = (Instance *) _instance.p;
	instance->blockingTask.callback(instance, &instance->blockingTask);
	EsMessage m = { MESSAGE_BLOCKING_TASK_COMPLETE };
	m.user.context1.p = instance;
	m.user.context2.u = instance->blockingTaskID;
	EsMessagePost(nullptr, &m);
}

void BlockingTaskComplete(Instance *instance) {
	EsAssert(instance->blockingTaskInProgress); // Task should have been in progress.
	instance->blockingTaskInProgress = false;

	if (instance->blockingTaskReachedTimeout && instance->blockingDialog) {
		EsDialogClose(instance->blockingDialog);
	}

	instance->blockingDialog = nullptr;
	Task *task = &instance->blockingTask;

	if (task->then) {
		task->then(instance, task);
	}

	EsInstanceCloseReference(instance);
}

void BlockingTaskQueue(Instance *instance, Task task) {
	EsAssert(!instance->blockingTaskInProgress); // Cannot queue a new blocking task if the previous has not finished.

	EsInstanceOpenReference(instance); // Don't destroy the instance while the task is in progress.

	instance->blockingTask = task;
	instance->blockingTaskInProgress = true;

	EsThreadInformation thread;
	EsThreadCreate(BlockingTaskThread, &thread, instance); 

	ptrdiff_t result = EsWait(&thread.handle, 1, BLOCKING_TASK_DIALOG_THRESHOLD_MS);
	EsHandleClose(thread.handle);

	if (result == ES_ERROR_TIMEOUT_REACHED) {
		instance->blockingTaskReachedTimeout = true;
		instance->blockingDialog = EsDialogShow(instance->window, task.cDescription, -1, 
				INTERFACE_STRING(FileManagerOngoingTaskDescription), ES_ICON_TOOLS_TIMER_SYMBOLIC);
		// TODO Progress bar; cancelling tasks.
	} else {
		instance->blockingTaskReachedTimeout = false;
		BlockingTaskComplete(instance);
		instance->blockingTaskID++; // Prevent the task being completed twice.
	}
}

void NonBlockingTaskWrapper(EsGeneric _task) {
	Task *task = (Task *) _task.p;
	task->callback(nullptr, task);
	EsMessage m = { MESSAGE_NON_BLOCKING_TASK_COMPLETE };
	m.user.context2.p = task;
	EsMessagePost(nullptr, &m);
}

void NonBlockingTaskQueue(Task _task) {
	// NOTE We can't store instances in tasks on the non-blocking queue thread,
	// because the instances might be destroyed while the task is in progress!
	Task *task = (Task *) EsHeapAllocate(sizeof(Task), false);
	EsMemoryCopy(task, &_task, sizeof(Task));
	EsWorkQueue(NonBlockingTaskWrapper, task);
}

void NonBlockingTaskComplete(EsMessage *message) {
	Task *task = (Task *) message->user.context2.p;
	if (task->then) task->then(nullptr, task);
	EsHeapFree(task);
}

void ConfigurationSave() {
	EsBuffer buffer = {};
	buffer.canGrow = true;

	EsBufferFormat(&buffer, "[bookmarks]\n");

	for (uintptr_t i = 0; i < bookmarks.Length(); i++) {
		EsBufferFormat(&buffer, "=%s\n", STRFMT(bookmarks[i]));
	}

	for (uintptr_t i = 0; i < folderViewSettings.Length(); i++) {
		FolderViewSettingsEntry *entry = &folderViewSettings[i];
		EsBufferFormat(&buffer, "\n[folder]\npath=%z\nsort_column=%d\nview_type=%d\n", 
				entry->path, entry->settings.sortColumn, entry->settings.viewType);
	}

	EsFileWriteAll(EsLiteral(SETTINGS_FILE), buffer.out, buffer.position);
	EsHeapFree(buffer.out);
}

#include "type_database.cpp"
#include "folder.cpp"
#include "commands.cpp"
#include "ui.cpp"

void DriveRefreshFolders(bool removed, const char *prefix, size_t prefixBytes) {
	Array<Folder *> foldersToRefresh = {};

	for (uintptr_t i = 0; i < loadedFolders.Length(); i++) {
		Folder *folder = loadedFolders[i];

		if (folder->itemHandler->type == NAMESPACE_HANDLER_DRIVES_PAGE || StringStartsWith(folder->path, StringFromLiteralWithSize(prefix, prefixBytes))) {
			foldersToRefresh.Add(folder);
		}
	}
	
	for (uintptr_t i = 0; i < foldersToRefresh.Length(); i++) {
		foldersToRefresh[i]->driveRemoved = removed;
		FolderRefresh(foldersToRefresh[i]);
	}

	foldersToRefresh.Free();
}

void DriveRemove(EsObjectID deviceID) {
	char *prefix = nullptr;
	size_t prefixBytes = 0;
	EsMutexAcquire(&drivesMutex);
	bool found = false;

	for (uintptr_t index = 0; index < drives.Length(); index++) {
		if (drives[index].deviceID == deviceID) {
			prefix = drives[index].prefix;
			prefixBytes = drives[index].prefixBytes;
			drives.Delete(index);
			found = true;

			for (uintptr_t i = 0; i < instances.Length(); i++) {
				if (instances[i]->closed) continue;
				EsListViewRemove(instances[i]->placesView, PLACES_VIEW_GROUP_DRIVES, index, 1);
			}

			break;
		}
	}

	EsAssert(found);
	EsMutexRelease(&drivesMutex);
	DriveRefreshFolders(true, prefix, prefixBytes);
	EsHeapFree(prefix);
}

void DriveAdd(EsHandle deviceHandle, EsObjectID deviceID) {
	// TODO Proper prefix allocation algorithm for drives.
	static int nextPrefix = 1;
	char prefix[16];
	bool isBootFileSystem = EsDeviceControl(deviceHandle, ES_DEVICE_CONTROL_FS_IS_BOOT, 0, nullptr) == 1;
	size_t prefixBytes = EsStringFormat(prefix, sizeof(prefix), "%fd:", ES_STRING_FORMAT_SIMPLE, isBootFileSystem ? 0 : nextPrefix);
	if (!isBootFileSystem) nextPrefix++;
	
	EsMutexAcquire(&drivesMutex);

	Drive drive = {};
	drive.prefix = (char *) EsHeapAllocate(prefixBytes, false);
	EsMemoryCopy(drive.prefix, prefix, prefixBytes);
	drive.prefixBytes = prefixBytes;
	drive.deviceID = deviceID;
	EsMountPointAdd(prefix, prefixBytes, deviceHandle);
	EsMountPointGetVolumeInformation(prefix, prefixBytes, &drive.information);
	drives.Add(drive);

	for (uintptr_t i = 0; i < instances.Length(); i++) {
		if (instances[i]->closed) continue;
		EsListViewInsert(instances[i]->placesView, PLACES_VIEW_GROUP_DRIVES, drives.Length(), 1);
	}

	EsMutexRelease(&drivesMutex);
	DriveRefreshFolders(false, prefix, prefixBytes);
}

void LoadSettings() {
	EsINIState state = { (char *) EsFileReadAll(EsLiteral(SETTINGS_FILE), &state.bytes) };
	FolderViewSettings *folder = nullptr;

	while (EsINIParse(&state)) {
		if (state.value && 0 == EsStringCompareRaw(state.section, state.sectionBytes, EsLiteral("bookmarks"))) {
			String string = {};
			string.text = state.value, string.bytes = state.valueBytes;
			BookmarkAdd(string, false);
		} else if (0 == EsStringCompareRaw(state.section, state.sectionBytes, EsLiteral("folder"))) {
			if (0 == EsStringCompareRaw(state.key, state.keyBytes, EsLiteral("path"))) {
				if (state.keyBytes < sizeof(folderViewSettings[0].path)) {
					FolderViewSettingsEntry *entry = folderViewSettings.Add();
					EsMemoryCopy(entry->path, state.value, state.valueBytes);
					entry->pathBytes = state.valueBytes;
					folder = &entry->settings;
				}
			} else if (folder && 0 == EsStringCompareRaw(state.key, state.keyBytes, EsLiteral("sort_column"))) {
				folder->sortColumn = EsIntegerParse(state.value, state.valueBytes);
			} else if (folder && 0 == EsStringCompareRaw(state.key, state.keyBytes, EsLiteral("view_type"))) {
				folder->viewType = EsIntegerParse(state.value, state.valueBytes);
			}
		}
	}
}

int InstanceCallback(Instance *instance, EsMessage *message) {
	if (message->type == ES_MSG_INSTANCE_CLOSE) {
		EsAssert(!instance->closed);
		instance->closed = true;
		instance->list = nullptr;
		instance->placesView = nullptr;
		instance->breadcrumbBar = nullptr;
		instance->newFolderButton = nullptr;
		instance->status = nullptr;
		instance->blockingDialog = nullptr;
	} else if (message->type == ES_MSG_INSTANCE_DESTROY) {
		EsApplicationStartupRequest request = EsInstanceGetStartupRequest(instance);

		if (request.flags & ES_APPLICATION_STARTUP_BACKGROUND_SERVICE) {
			// No cleanup to do.
		} else {
			InstanceDestroy(instance);
			instances.FindAndDeleteSwap(instance, true);
		}
	}

	return 0;
}

void _start() {
	_init();

	AddKnownFileTypes();
	LoadSettings();

	// Enumerate drives.

	size_t deviceCount;
	EsMessageDevice *devices = EsDeviceEnumerate(&deviceCount);
	for (uintptr_t i = 0; i < deviceCount; i++) if (devices[i].type == ES_DEVICE_FILE_SYSTEM) DriveAdd(devices[i].handle, devices[i].id);
	EsHeapFree(devices);

	// Process messages.

	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_INSTANCE_CREATE) {
			Instance *instance = EsInstanceCreate(message, INTERFACE_STRING(FileManagerTitle));
			instance->callback = InstanceCallback;

			EsApplicationStartupRequest request = EsInstanceGetStartupRequest(instance);
			
			if (request.flags & ES_APPLICATION_STARTUP_BACKGROUND_SERVICE) {
				// Keep the instance alive so that we always receive PATH_MOVED messages and such.
			} else {
				instances.Add(instance);
				InstanceCreateUI(instance);
			}
		} else if (message->type == ES_MSG_APPLICATION_EXIT) {
#ifdef DEBUG_BUILD
			for (uintptr_t i = 0; i < drives.Length(); i++) {
				EsHeapFree(drives[i].prefix);
			}

			for (uintptr_t i = 0; i < bookmarks.Length(); i++) {
				StringDestroy(&bookmarks[i]);
			}

			for (uintptr_t i = 0; i < thumbnailCache.Count(); i++) {
				EsHeapFree(thumbnailCache[i].bits);
			}

			for (uintptr_t i = 0; i < loadedFolders.Length(); i++) {
				FolderDestroy(loadedFolders[i]);
			}

			for (uintptr_t i = 0; i < openDocuments.Length(); i++) {
				StringDestroy(&openDocuments[i]);
			}

			EsAssert(!instances.Length());
			EsHeapFree(fileTypesBuffer.out);

			openDocuments.Free();
			bookmarks.Free();
			drives.Free();
			folderViewSettings.Free();
			foldersWithNoAttachedInstances.Free();
			instances.Free();
			knownFileTypes.Free();
			knownFileTypesByExtension.Free();
			loadedFolders.Free();
			thumbnailCache.Free();
#endif
		} else if (message->type == ES_MSG_DEVICE_CONNECTED && message->device.type == ES_DEVICE_FILE_SYSTEM) {
			DriveAdd(message->device.handle, message->device.id);
		} else if (message->type == ES_MSG_DEVICE_DISCONNECTED && message->device.type == ES_DEVICE_FILE_SYSTEM) {
			DriveRemove(message->device.id);
		} else if (message->type == ES_MSG_FILE_MANAGER_FILE_MODIFIED) {
			char *_path = (char *) EsHeapAllocate(message->user.context2.u, false);
			EsConstantBufferRead(message->user.context1.u, _path); 
			String fullPath = StringFromLiteralWithSize(_path, message->user.context2.u);
			size_t pathSectionCount = PathCountSections(fullPath);

			for (uintptr_t i = 0; i < pathSectionCount; i++) {
				FolderFileUpdatedAtPath(PathGetParent(fullPath, i + 1), nullptr);
			}

			EsHandleClose(message->user.context1.u);
			EsHeapFree(_path);
		} else if (message->type == ES_MSG_FILE_MANAGER_PATH_MOVED) {
			char *data = (char *) EsHeapAllocate(message->user.context2.u, false);
			uintptr_t *bytes = (uintptr_t *) data;
			char *paths = data + sizeof(size_t) * 2;
			EsConstantBufferRead(message->user.context1.u, data); 
			String oldPath = StringFromLiteralWithSize(paths, bytes[0]);
			String newPath = StringFromLiteralWithSize(paths + bytes[0], bytes[1]);
			size_t pathSectionCount;

			if (oldPath.bytes) {
				FolderPathMoved(oldPath, newPath, false);
			} else {
				FolderRefreshAllFrom(newPath);
			}

			pathSectionCount = PathCountSections(oldPath);

			for (uintptr_t i = 0; i < pathSectionCount; i++) {
				FolderFileUpdatedAtPath(PathGetParent(oldPath, i + 1), nullptr);
			}

			pathSectionCount = PathCountSections(newPath);

			for (uintptr_t i = 0; i < pathSectionCount; i++) {
				FolderFileUpdatedAtPath(PathGetParent(newPath, i + 1), nullptr);
			}

			EsHeapFree(data);
		} else if (message->type == ES_MSG_FILE_MANAGER_DOCUMENT_UPDATE) {
			EsBuffer buffer = { .canGrow = true };
			_EsOpenDocumentEnumerate(&buffer);

			size_t count = 0;
			EsBufferReadInto(&buffer, &count, sizeof(size_t));

			for (uintptr_t i = 0; i < openDocuments.Length(); i++) {
				StringDestroy(&openDocuments[i]);
			}

			openDocuments.Free();

			for (uintptr_t i = 0; i < count; i++) {
				size_t pathBytes = 0;
				EsBufferReadInto(&buffer, &pathBytes, sizeof(size_t));
				char *path = (char *) EsHeapAllocate(pathBytes, false);
				EsBufferReadInto(&buffer, path, pathBytes);
				String string = { .text = path, .bytes = pathBytes, .allocated = pathBytes };
				openDocuments.Add(string);
			}

			for (uintptr_t i = 0; i < instances.Length(); i++) {
				if (instances[i]->closed) continue;
				EsListViewInvalidateAll(instances[i]->list);
			}

			EsHeapFree(buffer.out);
			EsAssert(!buffer.error);
		} else if (message->type == MESSAGE_BLOCKING_TASK_COMPLETE) {
			Instance *instance = (Instance *) message->user.context1.p;
			if (message->user.context2.u == instance->blockingTaskID) BlockingTaskComplete(instance);
		} else if (message->type == MESSAGE_NON_BLOCKING_TASK_COMPLETE) {
			NonBlockingTaskComplete(message);
		}
	}
}
