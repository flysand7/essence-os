// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

#define ES_API
#define ES_FORWARD
#include <essence.h>

#ifdef USE_STB_IMAGE
#define STB_IMAGE_IMPLEMENTATION
#define STBI_MALLOC(sz) EsCRTmalloc(sz)
#define STBI_REALLOC(p,newsz) EsCRTrealloc(p,newsz)
#define STBI_FREE(p) EsCRTfree(p)
#define STBI_ASSERT(x) EsAssert(x)
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_LINEAR
#define STB_IMAGE_STATIC
#include <shared/stb_image.h>
#endif

#define SHARED_COMMON_WANT_ALL
#define SHARED_MATH_WANT_ALL
#include <shared/ini.h>
#include <shared/heap.cpp>
#include <shared/linked_list.cpp>
#include <shared/hash.cpp>
#include <shared/png_decoder.cpp>
#include <shared/hash_table.cpp>
#include <shared/array.cpp>
#include <shared/unicode.cpp>
#include <shared/math.cpp>
#include <shared/strings.cpp>
#include <shared/common.cpp>

struct EnumString { const char *cName; int value; };
#include <bin/generated_code/enum_strings_array.h>

#define DESKTOP_MSG_SET_TITLE                 (1)
#define DESKTOP_MSG_SET_ICON                  (2)
#define DESKTOP_MSG_REQUEST_SAVE              (3)
#define DESKTOP_MSG_COMPLETE_SAVE             (4)
#define DESKTOP_MSG_SHOW_IN_FILE_MANAGER      (5)
#define DESKTOP_MSG_ANNOUNCE_PATH_MOVED       (6)
#define DESKTOP_MSG_RUN_TEMPORARY_APPLICATION (7)
#define DESKTOP_MSG_REQUEST_SHUTDOWN          (8)
#define DESKTOP_MSG_START_APPLICATION         (9)
#define DESKTOP_MSG_CREATE_CLIPBOARD_FILE    (10)
#define DESKTOP_MSG_CLIPBOARD_PUT            (11)
#define DESKTOP_MSG_CLIPBOARD_GET            (12)
#define DESKTOP_MSG_SYSTEM_CONFIGURATION_GET (13)
#define DESKTOP_MSG_FILE_TYPES_GET           (14)
#define DESKTOP_MSG_START_USER_TASK          (16)
#define DESKTOP_MSG_SET_PROGRESS             (17)
#define DESKTOP_MSG_RENAME                   (18)
#define DESKTOP_MSG_SET_MODIFIED             (19)
#define DESKTOP_MSG_QUERY_OPEN_DOCUMENT      (20)
#define DESKTOP_MSG_LIST_OPEN_DOCUMENTS      (21)

struct EsFileStore {
#define FILE_STORE_HANDLE        (1)
#define FILE_STORE_PATH          (2)
#define FILE_STORE_EMBEDDED_FILE (3)
	uint8_t type;

	bool operationComplete;
	uint32_t handles;
	EsError error;

	union {
		EsHandle handle;

		struct {
			const EsBundle *bundle;
			char *path;
			size_t pathBytes;
		};
	};
};

struct ThreadLocalStorage {
	// This must be the first field.
	ThreadLocalStorage *self;

	EsObjectID id;
	uint64_t timerAdjustTicks;
};

struct MountPoint : EsMountPoint {
	EsVolumeInformation information;
	bool removing;
};

struct Timer {
	EsTimer id;
	double afterMs;
	EsTimerCallback callback; 
	EsGeneric argument;
};

struct Work {
	EsWorkCallback callback;
	EsGeneric context;
};

struct EsBundle {
	const BundleHeader *base;
	ptrdiff_t bytes;
};

const EsBundle bundleDefault = {
	.base = (const BundleHeader *) BUNDLE_FILE_MAP_ADDRESS,
	.bytes = -1,
};

const EsBundle bundleDesktop = {
	.base = (const BundleHeader *) BUNDLE_FILE_DESKTOP_MAP_ADDRESS,
	.bytes = -1,
};

struct {
	Array<EsSystemConfigurationGroup> systemConfigurationGroups;
	EsMutex systemConfigurationMutex;

	Array<MountPoint> mountPoints;
	Array<EsMessageDevice> connectedDevices;
	bool foundBootFileSystem;
	EsProcessStartupInformation *startupInformation;
	GlobalData *global;

	EsMutex messageMutex;
	volatile uintptr_t messageMutexThreadID;

	Array<_EsMessageWithObject> postBox;
	EsMutex postBoxMutex;

	Array<Timer> timers;
	EsMutex timersMutex;
	EsHandle timersThread;
	EsHandle timersEvent;

	EsSpinlock performanceTimerStackLock;
#define PERFORMANCE_TIMER_STACK_SIZE (100)
	double performanceTimerStack[PERFORMANCE_TIMER_STACK_SIZE];
	uintptr_t performanceTimerStackCount;

	EsHandle workAvailable;
	EsMutex workMutex;
	Array<Work> workQueue;
	Array<EsHandle> workThreads;
	volatile bool workFinish;

	const uint16_t *keyboardLayout;
	uint16_t keyboardLayoutIdentifier;
} api;

ptrdiff_t tlsStorageOffset;

// Miscellanous forward declarations.
extern "C" void EsUnimplemented();
extern "C" uintptr_t ProcessorTLSRead(uintptr_t offset);
extern "C" uint64_t ProcessorReadTimeStamp();
void UndoManagerDestroy(EsUndoManager *manager);
struct APIInstance *InstanceSetup(EsInstance *instance);
EsTextStyle TextPlanGetPrimaryStyle(EsTextPlan *plan);
EsFileStore *FileStoreCreateFromEmbeddedFile(const EsBundle *bundle, const char *path, size_t pathBytes);
EsFileStore *FileStoreCreateFromPath(const char *path, size_t pathBytes);
EsFileStore *FileStoreCreateFromHandle(EsHandle handle);
void FileStoreCloseHandle(EsFileStore *fileStore);
EsError NodeOpen(const char *path, size_t pathBytes, uint32_t flags, _EsNodeInformation *node);
const char *EnumLookupNameFromValue(const EnumString *array, int value);
EsSystemConfigurationItem *SystemConfigurationGetItem(EsSystemConfigurationGroup *group, const char *key, ptrdiff_t keyBytes, bool createIfNeeded = false);
EsSystemConfigurationGroup *SystemConfigurationGetGroup(const char *section, ptrdiff_t sectionBytes, bool createIfNeeded = false);
uint8_t *ApplicationStartupInformationToBuffer(const _EsApplicationStartupInformation *information, size_t *dataBytes = nullptr);
char *SystemConfigurationGroupReadString(EsSystemConfigurationGroup *group, const char *key, ptrdiff_t keyBytes, size_t *valueBytes = nullptr);
int64_t SystemConfigurationGroupReadInteger(EsSystemConfigurationGroup *group, const char *key, ptrdiff_t keyBytes, int64_t defaultValue = 0);
MountPoint *NodeFindMountPoint(const char *prefix, size_t prefixBytes);
EsWindow *WindowFromWindowID(EsObjectID id);
extern "C" void _init();

struct ProcessMessageTiming {
	double startLogic, endLogic;
	double startLayout, endLayout;
	double startPaint, endPaint;
	double startUpdate, endUpdate;
};

struct EsUndoManager {
	EsInstance *instance;

	Array<uint8_t> undoStack;
	Array<uint8_t> redoStack;

#define UNDO_MANAGER_STATE_NORMAL (0)
#define UNDO_MANAGER_STATE_UNDOING (1)
#define UNDO_MANAGER_STATE_REDOING (2)
	int state;
};

struct APIInstance {
	uintptr_t referenceCount;

	HashStore<uint32_t, EsCommand *> commands;

	_EsApplicationStartupInformation *startupInformation;
	EsHandle mainWindowHandle;

	char *documentPath;
	size_t documentPathBytes;
	uint32_t instanceClass;

	EsCommand commandDelete, 
		  commandSelectAll,
		  commandCopy,
		  commandCut,
		  commandPaste,
		  commandUndo,
		  commandRedo,
		  commandSave,
		  commandShowInFileManager;

	char *newName;
	size_t newNameBytes;

	const char *applicationName;
	size_t applicationNameBytes;

	struct InspectorWindow *attachedInspector;

	EsUndoManager undoManager;
	EsUndoManager *activeUndoManager;

	EsFileStore *fileStore;

	bool closeAfterSaveCompletes;

	union {
		EsInstanceClassEditorSettings editorSettings;
		EsInstanceClassViewerSettings viewerSettings;
	};

	// For the file menu.
	EsPanel *fileMenuNameSwitcher;
	EsPanel *fileMenuNamePanel;
	EsTextbox *fileMenuNameTextbox; // Also used by the file save dialog.
};

#include "syscall.cpp"
#include "profiling.cpp"
#include "renderer.cpp"
#include "theme.cpp"
#include "text.cpp"
#include "gui.cpp"
#include "inspector.cpp"
#include "desktop.cpp"
#include "settings.cpp"

const void *const apiTable[] = {
#include <bin/generated_code/api_array.h>
};

#ifdef PAUSE_ON_USERLAND_CRASH
__attribute__((no_instrument_function))
uintptr_t APISyscallCheckForCrash(uintptr_t argument0, uintptr_t argument1, uintptr_t argument2, uintptr_t unused, uintptr_t argument3, uintptr_t argument4) {
	uintptr_t returnValue = _APISyscall(argument0, argument1, argument2, unused, argument3, argument4);
	EsProcessState state;
	_APISyscall(ES_SYSCALL_PROCESS_GET_STATE, ES_CURRENT_PROCESS, (uintptr_t) &state, 0, 0, 0);
	while (state.flags & ES_PROCESS_STATE_PAUSED_FROM_CRASH);
	return returnValue;
}
#endif

MountPoint *NodeAddMountPoint(const char *prefix, size_t prefixBytes, EsHandle base, bool queryInformation) {
	MountPoint mountPoint = {};
	EsAssert(prefixBytes < sizeof(mountPoint.prefix));
	EsMemoryCopy(mountPoint.prefix, prefix, prefixBytes);
	mountPoint.base = base;
	mountPoint.prefixBytes = prefixBytes;

	if (queryInformation) {
		EsSyscall(ES_SYSCALL_VOLUME_GET_INFORMATION, base, (uintptr_t) &mountPoint.information, 0, 0);
	}

	return api.mountPoints.Add(mountPoint);
}

MountPoint *NodeFindMountPoint(const char *prefix, size_t prefixBytes) {
	for (uintptr_t i = 0; i < api.mountPoints.Length(); i++) {
		MountPoint *mountPoint = &api.mountPoints[i];

		if (prefixBytes >= mountPoint->prefixBytes 
				&& 0 == EsMemoryCompare(prefix, mountPoint->prefix, mountPoint->prefixBytes)
				&& !mountPoint->removing) {
			return mountPoint;
		}
	}

	return nullptr;
}

bool EsMountPointGetVolumeInformation(const char *prefix, size_t prefixBytes, EsVolumeInformation *information) {
	MountPoint *mountPoint = NodeFindMountPoint(prefix, prefixBytes);
	if (!mountPoint) return false;
	EsSyscall(ES_SYSCALL_VOLUME_GET_INFORMATION, mountPoint->base, (uintptr_t) &mountPoint->information, 0, 0);
	EsMemoryCopy(information, &mountPoint->information, sizeof(EsVolumeInformation));
	return true;
}

void EsMountPointEnumerate(EsMountPointEnumerationCallback callback, EsGeneric context) {
	EsMessageMutexCheck();
	
	for (uintptr_t i = 0; i < api.mountPoints.Length(); i++) {
		MountPoint *mountPoint = &api.mountPoints[i];
		callback(mountPoint->prefix, mountPoint->prefixBytes, context);
	}
}

void EsDeviceEnumerate(EsDeviceEnumerationCallback callback, EsGeneric context) {
	EsMessageMutexCheck();

	for (uintptr_t i = 0; i < api.connectedDevices.Length(); i++) {
		callback(api.connectedDevices[i], context);
	}
}

EsError NodeOpen(const char *path, size_t pathBytes, uint32_t flags, _EsNodeInformation *node) {
	EsMountPoint *mountPoint = NodeFindMountPoint(path, pathBytes);

	if (!mountPoint) {
		return ES_ERROR_PATH_NOT_WITHIN_MOUNTED_VOLUME;
	}

	node->handle = mountPoint->base;
	path += mountPoint->prefixBytes;
	pathBytes -= mountPoint->prefixBytes;

	return EsSyscall(ES_SYSCALL_NODE_OPEN, (uintptr_t) path, pathBytes, flags, (uintptr_t) node);
}

EsSystemConfigurationItem *SystemConfigurationGetItem(EsSystemConfigurationGroup *group, const char *key, ptrdiff_t keyBytes, bool createIfNeeded) {
	if (keyBytes == -1) keyBytes = EsCStringLength(key);

	for (uintptr_t i = 0; i < group->itemCount; i++) {
		if (0 == EsStringCompareRaw(key, keyBytes, group->items[i].key, group->items[i].keyBytes)) {
			return group->items + i;
		}
	}

	if (createIfNeeded) {
		EsSystemConfigurationItem item = {};
		item.key = (char *) EsHeapAllocate(keyBytes, false);
		if (!item.key) return nullptr;
		item.keyBytes = keyBytes;
		EsMemoryCopy(item.key, key, keyBytes);

		Array<EsSystemConfigurationItem> items = { group->items };
		EsSystemConfigurationItem *_item = items.Add(item);
		group->items = items.array;

		if (_item) {
			group->itemCount++;
			return _item;
		} else {
			EsHeapFree(item.key);
		}
	}

	return nullptr;
}

EsSystemConfigurationGroup *SystemConfigurationGetGroup(const char *section, ptrdiff_t sectionBytes, bool createIfNeeded) {
	if (sectionBytes == -1) sectionBytes = EsCStringLength(section);

	for (uintptr_t i = 0; i < api.systemConfigurationGroups.Length(); i++) {
		if (0 == EsStringCompareRaw(section, sectionBytes, api.systemConfigurationGroups[i].section, api.systemConfigurationGroups[i].sectionBytes)
				&& !api.systemConfigurationGroups[i].sectionClassBytes) {
			return &api.systemConfigurationGroups[i];
		}
	}

	if (createIfNeeded) {
		EsSystemConfigurationGroup group = {};
		group.section = (char *) EsHeapAllocate(sectionBytes, false);
		if (!group.section) return nullptr;
		group.sectionBytes = sectionBytes;
		EsMemoryCopy(group.section, section, sectionBytes);
		EsSystemConfigurationGroup *_group = api.systemConfigurationGroups.Add(group);

		if (_group) {
			return _group;
		} else {
			EsHeapFree(group.section);
		}
	}

	return nullptr;
}

char *SystemConfigurationGroupReadString(EsSystemConfigurationGroup *group, const char *key, ptrdiff_t keyBytes, size_t *valueBytes) {
	EsSystemConfigurationItem *item = SystemConfigurationGetItem(group, key, keyBytes);
	if (!item) { if (valueBytes) *valueBytes = 0; return nullptr; }
	if (valueBytes) *valueBytes = item->valueBytes;
	char *copy = (char *) EsHeapAllocate(item->valueBytes + 1, false);
	if (!copy) { if (valueBytes) *valueBytes = 0; return nullptr; }
	copy[item->valueBytes] = 0;
	EsMemoryCopy(copy, item->value, item->valueBytes);
	return copy;
}

int64_t SystemConfigurationGroupReadInteger(EsSystemConfigurationGroup *group, const char *key, ptrdiff_t keyBytes, int64_t defaultValue) {
	EsSystemConfigurationItem *item = SystemConfigurationGetItem(group, key, keyBytes);
	if (!item) return defaultValue;
	return EsIntegerParse(item->value, item->valueBytes); 
}

char *EsSystemConfigurationReadString(const char *section, ptrdiff_t sectionBytes, const char *key, ptrdiff_t keyBytes, size_t *valueBytes) {
	EsMutexAcquire(&api.systemConfigurationMutex);
	EsDefer(EsMutexRelease(&api.systemConfigurationMutex));
	EsSystemConfigurationGroup *group = SystemConfigurationGetGroup(section, sectionBytes);
	if (!group) { if (valueBytes) *valueBytes = 0; return nullptr; }
	return SystemConfigurationGroupReadString(group, key, keyBytes, valueBytes);
}

int64_t EsSystemConfigurationReadInteger(const char *section, ptrdiff_t sectionBytes, const char *key, ptrdiff_t keyBytes, int64_t defaultValue) {
	EsMutexAcquire(&api.systemConfigurationMutex);
	EsDefer(EsMutexRelease(&api.systemConfigurationMutex));
	EsSystemConfigurationGroup *group = SystemConfigurationGetGroup(section, sectionBytes);
	if (!group) return defaultValue;
	return SystemConfigurationGroupReadInteger(group, key, keyBytes, defaultValue);
}

void SystemConfigurationUnload() {
	for (uintptr_t i = 0; i < api.systemConfigurationGroups.Length(); i++) {
		for (uintptr_t j = 0; j < api.systemConfigurationGroups[i].itemCount; j++) {
			EsHeapFree(api.systemConfigurationGroups[i].items[j].key);
			EsHeapFree(api.systemConfigurationGroups[i].items[j].value);
		}

		EsHeapFree(api.systemConfigurationGroups[i].section);
		EsHeapFree(api.systemConfigurationGroups[i].sectionClass);

		Array<EsSystemConfigurationItem> items = { api.systemConfigurationGroups[i].items };
		items.Free();
	}

	api.systemConfigurationGroups.Free();
}

void SystemConfigurationLoad(const char *file, size_t fileBytes) {
	// TODO Detecting duplicate keys?

	EsINIState s = {};
	s.buffer = (char *) file;
	s.bytes = fileBytes;

	EsSystemConfigurationGroup *group = nullptr;

	while (EsINIParse(&s)) {
		if (!s.keyBytes) {
			EsSystemConfigurationGroup _group = {};
			api.systemConfigurationGroups.Add(_group);
			group = &api.systemConfigurationGroups.Last();
			group->section = (char *) EsHeapAllocate(s.sectionBytes, false);
			EsMemoryCopy(group->section, s.section, (group->sectionBytes = s.sectionBytes));
			group->sectionClass = (char *) EsHeapAllocate(s.sectionClassBytes, false);
			EsMemoryCopy(group->sectionClass, s.sectionClass, (group->sectionClassBytes = s.sectionClassBytes));
		} else if (group) {
			EsSystemConfigurationItem item = {};
			item.key = (char *) EsHeapAllocate(s.keyBytes, false);
			EsMemoryCopy(item.key, s.key, (item.keyBytes = s.keyBytes));
			item.value = (char *) EsHeapAllocate(s.valueBytes + 1, false);
			item.value[s.valueBytes] = 0;
			EsMemoryCopy(item.value, s.value, (item.valueBytes = s.valueBytes));
			Array<EsSystemConfigurationItem> items = { group->items };
			items.Add(item);
			group->items = items.array;
			group->itemCount++;
		}
	}
}

uint8_t *ApplicationStartupInformationToBuffer(const _EsApplicationStartupInformation *information, size_t *dataBytes) {
	_EsApplicationStartupInformation copy = *information;
	if (copy.filePathBytes == -1) copy.filePathBytes = EsCStringLength(copy.filePath);

	EsBuffer buffer = { .canGrow = true };
	EsBufferWriteInt8(&buffer, DESKTOP_MSG_START_APPLICATION);
	EsBufferWrite(&buffer, &copy, sizeof(_EsApplicationStartupInformation));
	EsBufferWrite(&buffer, copy.filePath, copy.filePathBytes);
	EsBufferWrite(&buffer, copy.containingFolder, copy.containingFolderBytes);

	if (dataBytes) *dataBytes = buffer.position;
	return buffer.out;
}

_EsApplicationStartupInformation *ApplicationStartupInformationParse(const void *data, size_t dataBytes) {
	EsBuffer buffer = { .in = (const uint8_t *) data, .bytes = dataBytes };
	_EsApplicationStartupInformation *startupInformation = (_EsApplicationStartupInformation *) EsBufferRead(&buffer, sizeof(_EsApplicationStartupInformation));
	startupInformation->filePath = (char *) EsHeapAllocate(startupInformation->filePathBytes, false);
	EsBufferReadInto(&buffer, (char *) startupInformation->filePath, startupInformation->filePathBytes);
	startupInformation->containingFolder = (char *) EsHeapAllocate(startupInformation->containingFolderBytes, false);
	EsBufferReadInto(&buffer, (char *) startupInformation->containingFolder, startupInformation->containingFolderBytes);
	return startupInformation;
}

void EsApplicationStart(EsInstance *instance, const EsApplicationStartupRequest *request) {
	EsApplicationStartupRequest copy = *request;

	if (copy.filePathBytes == -1) {
		copy.filePathBytes = EsCStringLength(copy.filePath);
	}

	EsBuffer buffer = { .canGrow = true };
	EsBufferWriteInt8(&buffer, DESKTOP_MSG_START_APPLICATION);
	EsBufferWrite(&buffer, &copy, sizeof(EsApplicationStartupRequest));
	EsBufferWrite(&buffer, copy.filePath, copy.filePathBytes);

	if (!buffer.error) {
		MessageDesktop(buffer.out, buffer.position, instance ? instance->window->handle : ES_INVALID_HANDLE);
	}

	EsHeapFree(buffer.out);
}

void EsInstanceSetClassEditor(EsInstance *_instance, const EsInstanceClassEditorSettings *settings) {
	APIInstance *instance = (APIInstance *) _instance->_private;
	instance->instanceClass = ES_INSTANCE_CLASS_EDITOR;
	instance->editorSettings = *settings;

	if (instance->editorSettings.newDocumentTitleBytes == -1) {
		instance->editorSettings.newDocumentTitleBytes = EsCStringLength(instance->editorSettings.newDocumentTitle);
	}

	if (instance->editorSettings.newDocumentFileNameBytes == -1) {
		instance->editorSettings.newDocumentFileNameBytes = EsCStringLength(instance->editorSettings.newDocumentFileName);
	}
}

void EsInstanceSetClassViewer(EsInstance *_instance, const EsInstanceClassViewerSettings *) {
	APIInstance *instance = (APIInstance *) _instance->_private;
	instance->instanceClass = ES_INSTANCE_CLASS_VIEWER;
}

const char *EnumLookupNameFromValue(const EnumString *array, int value) {
	if (array[0].value == -1) {
		return array[value].cName;
	} else {
		// The values are non-linear, and we have to search the array.
		
		for (uintptr_t i = 0; array[i].cName; i++) {
			if (array[i].value == value) {
				return array[i].cName;
			}
		}

		EsAssert(false);
		return nullptr;
	}
}

void EsMessageMutexAcquire() {
	EsMutexAcquire(&api.messageMutex);
	api.messageMutexThreadID = EsThreadGetID(ES_CURRENT_THREAD);
}

void EsMessageMutexRelease() {
	api.messageMutexThreadID = 0;
	EsMutexRelease(&api.messageMutex);
}

void EsMessageMutexCheck() {
	EsAssert(api.messageMutexThreadID == EsThreadGetID(ES_CURRENT_THREAD)); // Expected message mutex to be acquired.
}

int EsMessageSend(EsElement *element, EsMessage *message) {
	int response = 0;

	if (element->messageUser) {
		response = element->messageUser(element, message);
	}

	if (response == 0 && element->messageClass) {
		response = element->messageClass(element, message);
	}

	if (message->type >= ES_MSG_STATE_CHANGE_MESSAGE_START && message->type <= ES_MSG_STATE_CHANGE_MESSAGE_END) {
		((EsElement *) element)->MaybeRefreshStyle();
	}

	return response;
}

void _EsPathAnnouncePathMoved(const char *oldPath, ptrdiff_t oldPathBytes, const char *newPath, ptrdiff_t newPathBytes) {
	if (oldPathBytes == -1) oldPathBytes = EsCStringLength(oldPath);
	if (newPathBytes == -1) newPathBytes = EsCStringLength(newPath);
	size_t bufferBytes = 1 + sizeof(uintptr_t) * 2 + oldPathBytes + newPathBytes;
	char *buffer = (char *) EsHeapAllocate(bufferBytes, false);

	if (buffer) {
		buffer[0] = DESKTOP_MSG_ANNOUNCE_PATH_MOVED;
		EsMemoryCopy(buffer + 1, &oldPathBytes, sizeof(uintptr_t));
		EsMemoryCopy(buffer + 1 + sizeof(uintptr_t), &newPathBytes, sizeof(uintptr_t));
		EsMemoryCopy(buffer + 1 + sizeof(uintptr_t) * 2, oldPath, oldPathBytes);
		EsMemoryCopy(buffer + 1 + sizeof(uintptr_t) * 2 + oldPathBytes, newPath, newPathBytes);
		MessageDesktop(buffer, bufferBytes);
		EsHeapFree(buffer);
	}
}

void EsOpenDocumentQueryInformation(const char *path, ptrdiff_t pathBytes, EsOpenDocumentInformation *information) {
	if (pathBytes == -1) pathBytes = EsCStringLength(path);
	char *buffer = (char *) EsHeapAllocate(pathBytes + 1, false);

	if (buffer) {
		buffer[0] = DESKTOP_MSG_QUERY_OPEN_DOCUMENT;
		EsMemoryCopy(buffer + 1, path, pathBytes);
		EsBuffer response = { .out = (uint8_t *) information, .bytes = sizeof(EsOpenDocumentInformation) };
		MessageDesktop(buffer, pathBytes + 1, ES_INVALID_HANDLE, &response);
		EsHeapFree(buffer);
	}
}

void _EsOpenDocumentEnumerate(EsBuffer *outputBuffer) {
	uint8_t m = DESKTOP_MSG_LIST_OPEN_DOCUMENTS;
	MessageDesktop(&m, 1, ES_INVALID_HANDLE, outputBuffer);
}

void EsApplicationRunTemporary(EsInstance *instance, const char *path, ptrdiff_t pathBytes) {
	if (pathBytes == -1) pathBytes = EsCStringLength(path);
	char *buffer = (char *) EsHeapAllocate(pathBytes + 1, false);

	if (buffer) {
		buffer[0] = DESKTOP_MSG_RUN_TEMPORARY_APPLICATION;
		EsMemoryCopy(buffer + 1, path, pathBytes);
		MessageDesktop(buffer, pathBytes + 1, instance->window->handle);
		EsHeapFree(buffer);
	}
}

void EsSystemShowShutdownDialog() {
	uint8_t message = DESKTOP_MSG_REQUEST_SHUTDOWN;
	MessageDesktop(&message, 1);
}

void EsSystemConfigurationReadFileTypes(EsBuffer *buffer) {
	uint8_t m = DESKTOP_MSG_FILE_TYPES_GET;
	MessageDesktop(&m, 1, ES_INVALID_HANDLE, buffer);
}

void InstanceSave(EsInstance *_instance) {
	APIInstance *instance = (APIInstance *) _instance->_private;
	EsAssert(instance->instanceClass == ES_INSTANCE_CLASS_EDITOR);
	size_t bufferBytes = instance->editorSettings.newDocumentFileNameBytes + 1;
	char *buffer = (char *) EsHeapAllocate(bufferBytes, false);

	if (buffer) {
		buffer[0] = DESKTOP_MSG_REQUEST_SAVE;
		EsMemoryCopy(buffer + 1, instance->editorSettings.newDocumentFileName, instance->editorSettings.newDocumentFileNameBytes);
		MessageDesktop(buffer, bufferBytes, _instance->window->handle);
		EsHeapFree(buffer);
	}
}

void InstanceClose(EsInstance *instance) {
	if (!EsCommandByID(instance, ES_COMMAND_SAVE)->enabled) {
		EsInstanceClose(instance);
		return;
	}

	// The document has unsaved changes.
	// Ask the user if they want to save.

	// TODO Handling shutdown.

	DialogDismissAll(instance->window); // Dismiss any dialogs that are already open, if they have cancel buttons.

	APIInstance *apiInstance = (APIInstance *) instance->_private;
	char content[512];
	size_t contentBytes;
	const char *cTitle;

	if (apiInstance->startupInformation->filePathBytes) {
		cTitle = interfaceString_FileCloseWithModificationsTitle;
		contentBytes = EsStringFormat(content, sizeof(content), interfaceString_FileCloseWithModificationsContent, 
				apiInstance->startupInformation->filePathBytes, apiInstance->startupInformation->filePath);
	} else {
		cTitle = interfaceString_FileCloseNewTitle;
		contentBytes = EsStringFormat(content, sizeof(content), interfaceString_FileCloseNewContent, 
				apiInstance->applicationNameBytes, apiInstance->applicationName);
	}

	EsDialog *dialog = EsDialogShow(instance->window, cTitle, -1, content, contentBytes, ES_ICON_DIALOG_WARNING);

	if (!apiInstance->startupInformation->filePathBytes) {
		EsPanel *row = EsPanelCreate(dialog->contentArea, ES_PANEL_HORIZONTAL, ES_STYLE_PANEL_FORM_TABLE);
		EsTextDisplayCreate(row, ES_FLAGS_DEFAULT, ES_STYLE_TEXT_LABEL, INTERFACE_STRING(FileCloseNewName));
		EsTextbox *textbox = EsTextboxCreate(row);
		EsInstanceClassEditorSettings *editorSettings = &apiInstance->editorSettings;
		EsTextboxInsert(textbox, editorSettings->newDocumentFileName, editorSettings->newDocumentFileNameBytes);
		EsElementFocus(textbox);
		TextboxSelectSectionBeforeFileExtension(textbox, editorSettings->newDocumentFileName, editorSettings->newDocumentFileNameBytes);
		apiInstance->fileMenuNameTextbox = textbox;
	}

	EsDialogAddButton(dialog, ES_BUTTON_CANCEL, 0, INTERFACE_STRING(CommonCancel), 
			[] (EsInstance *instance, EsElement *, EsCommand *) { 
		EsDialogClose(instance->window->dialogs.Last()); 
	});

	EsDialogAddButton(dialog, ES_FLAGS_DEFAULT, ES_STYLE_PUSH_BUTTON_DANGEROUS, INTERFACE_STRING(FileCloseWithModificationsDelete), 
			[] (EsInstance *instance, EsElement *, EsCommand *) { 
		EsDialogClose(instance->window->dialogs.Last()); 
		EsInstanceClose(instance);
	});

	EsButton *button = EsDialogAddButton(dialog, ES_BUTTON_DEFAULT, 0, INTERFACE_STRING(FileCloseWithModificationsSave), 
			[] (EsInstance *instance, EsElement *, EsCommand *) { 
		EsDialogClose(instance->window->dialogs.Last()); 

		APIInstance *apiInstance = (APIInstance *) instance->_private;

		if (apiInstance->startupInformation->filePathBytes) {
			InstanceSave(instance);
		} else {
			InstanceRenameFromTextbox(instance->window, apiInstance, apiInstance->fileMenuNameTextbox);
		}
	
		apiInstance->closeAfterSaveCompletes = true;
	});

	if (apiInstance->startupInformation->filePathBytes) {
		EsElementFocus(button);
	}
}

void FileStoreCloseHandle(EsFileStore *fileStore) {
	EsMessageMutexCheck(); // TODO Remove this limitation?
	EsAssert(fileStore->handles < 0x80000000);

	if (--fileStore->handles) {
		return;
	}

	if (fileStore->type == FILE_STORE_HANDLE) {
		if (fileStore->handle) {
			EsHandleClose(fileStore->handle);
		}
	} else if (fileStore->type == FILE_STORE_PATH || fileStore->type == FILE_STORE_EMBEDDED_FILE) {
		// The path is stored after the file store allocation.
	}

	EsHeapFree(fileStore);
}

EsFileStore *FileStoreCreateFromPath(const char *path, size_t pathBytes) {
	EsFileStore *fileStore = (EsFileStore *) EsHeapAllocate(sizeof(EsFileStore) + pathBytes, false);
	if (!fileStore) return nullptr;
	EsMemoryZero(fileStore, sizeof(EsFileStore));
	fileStore->type = FILE_STORE_PATH;
	fileStore->handles = 1;
	fileStore->error = ES_SUCCESS;
	fileStore->path = (char *) (fileStore + 1);
	fileStore->pathBytes = pathBytes;
	EsMemoryCopy(fileStore->path, path, pathBytes);
	return fileStore;
}

EsFileStore *FileStoreCreateFromHandle(EsHandle handle) {
	EsFileStore *fileStore = (EsFileStore *) EsHeapAllocate(sizeof(EsFileStore), true);
	if (!fileStore) return nullptr;
	fileStore->type = FILE_STORE_HANDLE;
	fileStore->handles = 1;
	fileStore->error = ES_SUCCESS;
	fileStore->handle = handle;
	return fileStore;
}

EsFileStore *FileStoreCreateFromEmbeddedFile(const EsBundle *bundle, const char *name, size_t nameBytes) {
	EsFileStore *fileStore = (EsFileStore *) EsHeapAllocate(sizeof(EsFileStore) + nameBytes, false);
	if (!fileStore) return nullptr;
	EsMemoryZero(fileStore, sizeof(EsFileStore));
	fileStore->type = FILE_STORE_EMBEDDED_FILE;
	fileStore->handles = 1;
	fileStore->error = ES_SUCCESS;
	fileStore->path = (char *) (fileStore + 1);
	fileStore->pathBytes = nameBytes;
	fileStore->bundle = bundle;
	EsMemoryCopy(fileStore->path, name, nameBytes);
	return fileStore;
}

void InstanceCreateFileStore(APIInstance *instance, EsHandle handle) {
	if (instance->fileStore) FileStoreCloseHandle(instance->fileStore);
	instance->fileStore = FileStoreCreateFromHandle(handle);
}

APIInstance *InstanceSetup(EsInstance *instance) {
	APIInstance *apiInstance = (APIInstance *) EsHeapAllocate(sizeof(APIInstance), true);

	instance->_private = apiInstance;
	apiInstance->referenceCount = 1;

	instance->undoManager = &apiInstance->undoManager;
	instance->undoManager->instance = instance;
	apiInstance->activeUndoManager = instance->undoManager;

	EsCommandRegister(&apiInstance->commandDelete, instance, INTERFACE_STRING(CommonSelectionDelete), nullptr, ES_COMMAND_DELETE, "Del");
	EsCommandRegister(&apiInstance->commandSelectAll, instance, INTERFACE_STRING(CommonSelectionSelectAll), nullptr, ES_COMMAND_SELECT_ALL, "Ctrl+A");
	EsCommandRegister(&apiInstance->commandCopy, instance, INTERFACE_STRING(CommonClipboardCopy), nullptr, ES_COMMAND_COPY, "Ctrl+C|Ctrl+Ins");
	EsCommandRegister(&apiInstance->commandCut, instance, INTERFACE_STRING(CommonClipboardCut), nullptr, ES_COMMAND_CUT, "Ctrl+X|Shift+Del");
	EsCommandRegister(&apiInstance->commandPaste, instance, INTERFACE_STRING(CommonClipboardPaste), nullptr, ES_COMMAND_PASTE, "Ctrl+V|Shift+Ins");
	EsCommandRegister(&apiInstance->commandUndo, instance, INTERFACE_STRING(CommonUndo), nullptr, ES_COMMAND_UNDO, "Ctrl+Z");
	EsCommandRegister(&apiInstance->commandRedo, instance, INTERFACE_STRING(CommonRedo), nullptr, ES_COMMAND_REDO, "Ctrl+Y");
	EsCommandRegister(&apiInstance->commandSave, instance, INTERFACE_STRING(CommonFileSave), nullptr, ES_COMMAND_SAVE, "Ctrl+S");
	EsCommandRegister(&apiInstance->commandShowInFileManager, instance, INTERFACE_STRING(CommonFileShowInFileManager), nullptr, ES_COMMAND_SHOW_IN_FILE_MANAGER, "Ctrl+Shift+O");

	EsCommandSetCallback(&apiInstance->commandUndo, [] (EsInstance *instance, EsElement *, EsCommand *) {
		EsUndoInvokeGroup(((APIInstance *) instance->_private)->activeUndoManager, false);
	});

	EsCommandSetCallback(&apiInstance->commandRedo, [] (EsInstance *instance, EsElement *, EsCommand *) {
		EsUndoInvokeGroup(((APIInstance *) instance->_private)->activeUndoManager, true);
	});

	EsCommandSetCallback(&apiInstance->commandSave, [] (EsInstance *instance, EsElement *, EsCommand *) {
		InstanceSave(instance);
	});

	EsCommandSetCallback(&apiInstance->commandShowInFileManager, [] (EsInstance *instance, EsElement *, EsCommand *) {
		char buffer[1];
		buffer[0] = DESKTOP_MSG_SHOW_IN_FILE_MANAGER;
		MessageDesktop(buffer, 1, instance->window->handle);
	});

	return apiInstance;
}

EsInstance *_EsInstanceCreate(size_t bytes, EsMessage *message, const char *applicationName, ptrdiff_t applicationNameBytes) {
	if (applicationNameBytes == -1) {
		applicationNameBytes = EsCStringLength(applicationName);
	}

	EsInstance *instance = (EsInstance *) EsHeapAllocate(bytes, true);
	EsAssert(bytes >= sizeof(EsInstance));
	APIInstance *apiInstance = InstanceSetup(instance);
	apiInstance->applicationName = applicationName;
	apiInstance->applicationNameBytes = applicationNameBytes;

	if (message && message->createInstance.data != ES_INVALID_HANDLE && message->createInstance.dataBytes > 1) {
		apiInstance->startupInformation = (_EsApplicationStartupInformation *) EsHeapAllocate(message->createInstance.dataBytes, false);

		if (apiInstance->startupInformation) {
			void *buffer = EsHeapAllocate(message->createInstance.dataBytes, false);
			EsConstantBufferRead(message->createInstance.data, buffer);
			EsMemoryCopy(apiInstance->startupInformation, (char *) buffer + 1, message->createInstance.dataBytes - 1);
			EsHeapFree(buffer);

			if (!ApplicationStartupInformationParse(apiInstance->startupInformation, message->createInstance.dataBytes - 1)) {
				EsHeapFree(apiInstance->startupInformation);
				apiInstance->startupInformation = nullptr;
			}
		}
	}

	if (message) {
		apiInstance->mainWindowHandle = message->createInstance.window;
		instance->window = EsWindowCreate(instance, ES_WINDOW_NORMAL);
		EsInstanceOpenReference(instance);
		EsWindowSetTitle(instance->window, nullptr, 0);

		if (apiInstance->startupInformation && apiInstance->startupInformation->readHandle) {
			InstanceCreateFileStore(apiInstance, apiInstance->startupInformation->readHandle);
			EsWindowSetTitle(instance->window, apiInstance->startupInformation->filePath, apiInstance->startupInformation->filePathBytes);
			EsCommandSetDisabled(&apiInstance->commandShowInFileManager, false);

			// HACK Delay sending the instance open message so that application has a chance to initialise the instance.
			// TODO Change how this works!!
			// TODO Can the posted message be raced by a ES_MSG_INSTANCE_DOCUMENT_UPDATED?
			EsMessage m = { ES_MSG_INSTANCE_OPEN_DELAYED };
			m._argument = instance;
			EsMessagePost(nullptr, &m);
		}
	}

	return instance;
}

EsApplicationStartupRequest EsInstanceGetStartupRequest(EsInstance *_instance) {
	APIInstance *instance = (APIInstance *) _instance->_private;
	EsApplicationStartupRequest request = {};

	if (instance->startupInformation) {
		request.id = instance->startupInformation->id;
		request.filePath = instance->startupInformation->filePath;
		request.filePathBytes = instance->startupInformation->filePathBytes;
		request.flags = instance->startupInformation->flags;
	}

	return request;
}

void EsInstanceOpenReference(EsInstance *_instance) {
	EsMessageMutexCheck();
	APIInstance *instance = (APIInstance *) _instance->_private;
	EsAssert(instance->referenceCount);
	instance->referenceCount++;
}

void EsInstanceCloseReference(EsInstance *_instance) {
	EsMessageMutexCheck();
	APIInstance *instance = (APIInstance *) _instance->_private;
	instance->referenceCount--;

	if (!instance->referenceCount) {
		if (_instance->callback) {
			EsMessage m = {};
			m.type = ES_MSG_INSTANCE_DESTROY;
			_instance->callback(_instance, &m);
		}

		if (instance->startupInformation) {
			EsHeapFree((void *) instance->startupInformation->filePath);
			EsHeapFree((void *) instance->startupInformation->containingFolder);
		}

		EsHeapFree(instance->startupInformation);
		EsHeapFree(instance->documentPath);
		EsHeapFree(instance->newName);

		for (uintptr_t i = 0; i < instance->commands.Count(); i++) {
			EsCommand *command = instance->commands[i];
			EsAssert(command->registered);
			EsAssert(!ArrayLength(command->elements));
			Array<EsElement *> elements = { command->elements };
			elements.Free();
		}

		instance->commands.Free();
		if (instance->fileStore) FileStoreCloseHandle(instance->fileStore);
		EsHeapFree(instance);
		EsHeapFree(_instance);
	}
}

void EsInstanceClose(EsInstance *instance) {
	EsMessageMutexCheck();

	if (instance->callback) {
		EsMessage m = {};
		m.type = ES_MSG_INSTANCE_CLOSE;
		instance->callback(instance, &m);
	}

	InspectorWindow **inspector = &((APIInstance *) instance->_private)->attachedInspector;

	if (*inspector) {
		EsInstance *instance2 = *inspector;
		UndoManagerDestroy(instance2->undoManager);
		EsAssert(instance2->window->instance == instance2);
		EsElementDestroy(instance2->window);
		EsInstanceCloseReference(instance2);
		instance2->window->InternalDestroy();
		*inspector = nullptr;
	}

	UndoManagerDestroy(instance->undoManager);
	EsAssert(instance->window->instance == instance);
	EsElementDestroy(instance->window);
	EsInstanceCloseReference(instance);
}

EsWindow *WindowFromWindowID(EsObjectID id) {
	for (uintptr_t i = 0; i < gui.allWindows.Length(); i++) {
		if (gui.allWindows[i]->id == id) {
			return gui.allWindows[i];
		}
	}

	return nullptr;
}

EsInstance *InstanceFromWindowID(EsObjectID id) {
	EsWindow *window = WindowFromWindowID(id);
	return window ? window->instance : nullptr;
}

void InstanceSendOpenMessage(EsInstance *instance, bool update) {
	APIInstance *apiInstance = (APIInstance *) instance->_private;

	EsMessage m = { .type = ES_MSG_INSTANCE_OPEN };
	m.instanceOpen.name = apiInstance->startupInformation->filePath;
	m.instanceOpen.nameBytes = apiInstance->startupInformation->filePathBytes;
	m.instanceOpen.file = apiInstance->fileStore;
	m.instanceOpen.update = update;

	int response = instance->callback ? instance->callback(instance, &m) : 0;
	if (!response) EsInstanceOpenComplete(instance, m.instanceOpen.file, true); // Ignored.

	// TODO Support multithreaded file operations.
	EsAssert(m.instanceOpen.file->operationComplete);
}

EsError GetMessage(_EsMessageWithObject *message) {
	// Process posted messages first,
	// so that messages like ES_MSG_WINDOW_DESTROYED are received last.

	bool gotMessage = false;
	EsMutexAcquire(&api.postBoxMutex);

	if (api.postBox.Length()) {
		*message = api.postBox[0];
		api.postBox.Delete(0);
		gotMessage = true;
	}

	EsMutexRelease(&api.postBoxMutex);
	if (gotMessage) return ES_SUCCESS;

	return EsSyscall(ES_SYSCALL_MESSAGE_GET, (uintptr_t) message, 0, 0, 0);
}

EsMessage *EsMessageReceive() {
	static _EsMessageWithObject message = {};
	EsMessageMutexCheck();

	while (true) {
		if (message.message.type == ES_MSG_INSTANCE_CREATE) {
			if (message.message.createInstance.data != ES_INVALID_HANDLE) {
				EsHandleClose(message.message.createInstance.data);
			}
		} else if (message.message.type == ES_MSG_APPLICATION_EXIT) {
			if (api.startupInformation->isDesktop) {
				// Desktop tracks the number of instances it owns, so it needs to know when it exits.
				ApplicationProcessTerminated(EsProcessGetID(ES_CURRENT_PROCESS));
			} else {
				api.workFinish = true;
				if (api.workAvailable) EsEventSet(api.workAvailable);
				EsMessageMutexRelease();

				for (uintptr_t i = 0; i < api.workThreads.Length(); i++) {
					EsWaitSingle(api.workThreads[i]);
					EsHandleClose(api.workThreads[i]);
				}

#ifdef DEBUG_BUILD
				EsMessageMutexAcquire();
				FontDatabaseFree();
				FreeUnusedStyles(true /* include permanent styles */);
				theming.loadedStyles.Free();
				SystemConfigurationUnload();
				api.mountPoints.Free();
				api.postBox.Free();
				api.timers.Free();
				gui.animatingElements.Free();
				gui.accessKeys.entries.Free();
				gui.allWindows.Free();
				calculator.Free();
				HashTableFree(&gui.keyboardShortcutNames, false);
				if (api.workAvailable) EsHandleClose(api.workAvailable);
				EsAssert(!api.workQueue.Length());
				api.workThreads.Free();
				api.workQueue.Free();
				MemoryLeakDetectorCheckpoint(&heap);
				EsPrint("ES_MSG_APPLICATION_EXIT - Heap allocation count: %d (%d from malloc).\n", heap.allocationsCount, mallocCount);
#endif
				EsProcessTerminateCurrent();
			}
		} else if (message.message.type == ES_MSG_UNREGISTER_FILE_SYSTEM) {
			for (uintptr_t i = 0; i < api.mountPoints.Length(); i++) {
				if (api.mountPoints[i].information.id == message.message.unregisterFileSystem.id) {
					EsHandleClose(api.mountPoints[i].base);
					api.mountPoints.Delete(i);
				}
			}
		}

		EsMessageMutexRelease();

		EsError error = GetMessage(&message);

		while (error != ES_SUCCESS) {
			if (!gui.animationSleep && gui.animatingElements.Length()) {
				EsMessageMutexAcquire();
				ProcessAnimations();
				EsMessageMutexRelease();
			} else {
				EsSyscall(ES_SYSCALL_MESSAGE_WAIT, ES_WAIT_NO_TIMEOUT, 0, 0, 0);
			}

			error = GetMessage(&message);
		}

		EsMessageMutexAcquire();

		EsMessageType type = message.message.type;

		if (type == ES_MSG_EYEDROP_REPORT) {
			EsMessageSend((EsElement *) message.object, &message.message);
		} else if (type == ES_MSG_TIMER) {
			((EsTimerCallback) message.message.user.context1.p)(message.message.user.context2);
		} else if (type >= ES_MSG_WM_START && type <= ES_MSG_WM_END && message.object) {
#if 0
			ProcessMessageTiming timing = {};
			double start = EsTimeStampMs();
			UIProcessWindowManagerMessage((EsWindow *) message.object, &message.message, &timing);
			EsPrint("Processed message from WM %x in %Fms (%Fms logic, %Fms layout, %Fms paint, %Fms update screen).\n", 
					type, EsTimeStampMs() - start, 
					timing.endLogic - timing.startLogic, 
					timing.endLayout - timing.startLayout,
					timing.endPaint - timing.startPaint,
					timing.endUpdate - timing.startUpdate);
#else
			UIProcessWindowManagerMessage((EsWindow *) message.object, &message.message, nullptr);
#endif
		} else if (type == ES_MSG_TAB_INSPECT_UI) {
			EsInstance *_instance = InstanceFromWindowID(message.message.tabOperation.id);

			if (_instance) {
				APIInstance *instance = (APIInstance *) _instance->_private;

				if (instance->attachedInspector) {
					EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, instance->attachedInspector->window->handle, 0, 0, ES_WINDOW_PROPERTY_FOCUSED);
				} else {
					EsWindowCreate(_instance, ES_WINDOW_INSPECTOR);
				}
			}
		} else if (type == ES_MSG_TAB_CLOSE_REQUEST) {
			EsInstance *instance = InstanceFromWindowID(message.message.tabOperation.id);

			if (instance) {
				InstanceClose(instance);
			}
		} else if (type == ES_MSG_INSTANCE_SAVE_RESPONSE) {
			// TODO Support multithreaded file operations.

			EsMessage m = {};
			m.type = ES_MSG_INSTANCE_SAVE;
			m.instanceSave.file = (EsFileStore *) EsHeapAllocate(sizeof(EsFileStore), true);

			if (m.instanceSave.file) {
				EsInstance *_instance = InstanceFromWindowID(message.message.tabOperation.id);
				APIInstance *instance = (APIInstance *) _instance->_private;

				m.instanceSave.file->error = message.message.tabOperation.error;
				m.instanceSave.file->handle = message.message.tabOperation.handle;
				m.instanceSave.file->type = FILE_STORE_HANDLE;
				m.instanceSave.file->handles = 1;

				m.instanceSave.name = instance->startupInformation->filePath;
				m.instanceSave.nameBytes = instance->startupInformation->filePathBytes;

				if (m.instanceSave.file->error == ES_SUCCESS && _instance->callback && _instance->callback(_instance, &m)) {
					// The instance callback will have called EsInstanceSaveComplete.
				} else {
					EsInstanceSaveComplete(_instance, m.instanceSave.file, false);
				}
			} else {
				if (message.message.tabOperation.handle) {
					EsHandleClose(message.message.tabOperation.handle);
				}
			}

			EsAssert(m.instanceSave.file->operationComplete);
			FileStoreCloseHandle(m.instanceSave.file);
		} else if (type == ES_MSG_INSTANCE_RENAME_RESPONSE) {
			EsInstance *instance = InstanceFromWindowID(message.message.tabOperation.id);

			if (instance) {
				APIInstance *apiInstance = (APIInstance *) instance->_private;

				if (message.message.tabOperation.error == ES_SUCCESS) {
					EsAnnouncementShow(instance->window, ES_FLAGS_DEFAULT, -1, -1, INTERFACE_STRING(FileRenameSuccess));
				} else {
					char buffer[512];
					const char *errorMessage = interfaceString_FileSaveErrorUnknown;
					ptrdiff_t errorMessageBytes = -1;

					switch (message.message.tabOperation.error) {
						case ES_ERROR_FILE_DOES_NOT_EXIST: 
						case ES_ERROR_NODE_DELETED: 
						case ES_ERROR_PERMISSION_NOT_GRANTED: 
						case ES_ERROR_INCORRECT_NODE_TYPE:
							errorMessage = interfaceString_FileSaveErrorFileDeleted;
							break;
						case ES_ERROR_DRIVE_ERROR_FILE_DAMAGED:
							errorMessage = interfaceString_FileSaveErrorCorrupt;
							break;
						case ES_ERROR_DRIVE_CONTROLLER_REPORTED:
							errorMessage = interfaceString_FileSaveErrorDrive;
							break;
						case ES_ERROR_INSUFFICIENT_RESOURCES:
							errorMessage = interfaceString_FileSaveErrorResourcesLow;
							break;

						case ES_ERROR_FILE_ALREADY_EXISTS: {
							errorMessage = buffer;
							errorMessageBytes = EsStringFormat(buffer, sizeof(buffer), interfaceString_FileSaveErrorAlreadyExists, 
									apiInstance->newNameBytes, apiInstance->newName);
						} break;
					}

					EsDialogShow(instance->window, INTERFACE_STRING(FileCannotRename), 
							errorMessage, errorMessageBytes, ES_ICON_DIALOG_ERROR, ES_DIALOG_ALERT_OK_BUTTON);
				}

				EsHeapFree(apiInstance->newName);
				apiInstance->newName = nullptr;
			}
		} else if (type == ES_MSG_INSTANCE_DOCUMENT_RENAMED) {
			char *buffer = (char *) EsHeapAllocate(message.message.tabOperation.bytes, false);

			if (buffer) {
				EsConstantBufferRead(message.message.tabOperation.handle, buffer);
				EsInstance *_instance = InstanceFromWindowID(message.message.tabOperation.id);

				if (_instance) {
					APIInstance *instance = (APIInstance *) _instance->_private;
					EsHeapFree((void *) instance->startupInformation->filePath);
					EsHeapFree((void *) instance->startupInformation->containingFolder);
					EsMemoryCopy(&instance->startupInformation->filePathBytes, buffer, sizeof(ptrdiff_t));
					EsMemoryCopy(&instance->startupInformation->containingFolderBytes, buffer + sizeof(ptrdiff_t), sizeof(ptrdiff_t));
					char *filePath = (char *) EsHeapAllocate(instance->startupInformation->filePathBytes, false);
					char *containingFolder = (char *) EsHeapAllocate(instance->startupInformation->containingFolderBytes, false);
					EsMemoryCopy(filePath, buffer + sizeof(ptrdiff_t) * 2, instance->startupInformation->filePathBytes);
					EsMemoryCopy(containingFolder, buffer + sizeof(ptrdiff_t) * 2 + instance->startupInformation->filePathBytes, 
							instance->startupInformation->containingFolderBytes);
					instance->startupInformation->filePath = filePath;
					instance->startupInformation->containingFolder = containingFolder;
					EsWindowSetTitle(_instance->window, filePath, instance->startupInformation->filePathBytes);
				}
			}

			EsHeapFree(buffer);
			EsHandleClose(message.message.tabOperation.handle);
		} else if (type == ES_MSG_INSTANCE_DOCUMENT_UPDATED) {
			EsInstance *_instance = InstanceFromWindowID(message.message.tabOperation.id);

			if (_instance) {
				APIInstance *instance = (APIInstance *) _instance->_private;

				InstanceCreateFileStore(instance, message.message.tabOperation.handle);

				if (!message.message.tabOperation.isSource) {
					InstanceSendOpenMessage(_instance, true);
				}
			} else {
				EsHandleClose(message.message.tabOperation.handle);
			}
		} else if (type == ES_MSG_INSTANCE_OPEN_DELAYED) {
			InstanceSendOpenMessage((EsInstance *) message.message._argument, false);
		} else if (type == ES_MSG_PRIMARY_CLIPBOARD_UPDATED) {
			EsInstance *instance = InstanceFromWindowID(message.message.tabOperation.id);
			if (instance) UIRefreshPrimaryClipboard(instance->window);
		} else if (type == ES_MSG_UI_SCALE_CHANGED) {
			if (theming.scale != api.global->uiScale) {
				theming.scale = api.global->uiScale;
				gui.accessKeys.hintStyle = nullptr;
				// TODO Clear old keepAround styles.

				for (uintptr_t i = 0; i < gui.allWindows.Length(); i++) {
					UIScaleChanged(gui.allWindows[i], &message.message);
					EsElementRelayout(gui.allWindows[i]);
				}

				return &message.message;
			}
		} else if (type == ES_MSG_REGISTER_FILE_SYSTEM) {
			EsMessageRegisterFileSystem *m = &message.message.registerFileSystem;

			int64_t index = 1; // TODO This is incorrect!

			while (true) {
				bool seen = false;

				for (uintptr_t i = 0; i < api.mountPoints.Length(); i++) {
					if (index == EsIntegerParse(api.mountPoints[i].prefix, api.mountPoints[i].prefixBytes)) {
						seen = true;
						break;
					}
				}

				if (seen) {
					index++;
				} else {
					break;
				}
			}

			bool isBootFileSystem = m->isBootFileSystem;
			char prefix[16];
			size_t prefixBytes = EsStringFormat(prefix, sizeof(prefix), "%fd:", ES_STRING_FORMAT_SIMPLE, isBootFileSystem ? 0 : index);

			m->mountPoint = NodeAddMountPoint(prefix, prefixBytes, m->rootDirectory, true);

			if (isBootFileSystem) {
				api.foundBootFileSystem = true;
			}

			if (m->mountPoint) {
				return &message.message;
			}
		} else if (type == ES_MSG_UNREGISTER_FILE_SYSTEM) {
			for (uintptr_t i = 0; i < api.mountPoints.Length(); i++) {
				if (api.mountPoints[i].information.id == message.message.unregisterFileSystem.id) {
					message.message.unregisterFileSystem.mountPoint = &api.mountPoints[i];
					api.mountPoints[i].removing = true;
					return &message.message;
				}
			}
		} else if (type == ES_MSG_DEVICE_CONNECTED) {
			api.connectedDevices.Add(message.message.device);
			return &message.message;
		} else if (type == ES_MSG_DEVICE_DISCONNECTED) {
			for (uintptr_t i = 0; i < api.connectedDevices.Length(); i++) {
				if (api.connectedDevices[i].id == message.message.device.id) {
					EsHandleClose(api.connectedDevices[i].handle);
					api.connectedDevices.Delete(i);
					return &message.message;
				}
			}
		} else {
			return &message.message;
		}
	}
}

void EsInstanceSetModified(EsInstance *instance, bool modified) {
	EsCommandSetEnabled(EsCommandByID(instance, ES_COMMAND_SAVE), modified);

	uint8_t m[2];
	m[0] = DESKTOP_MSG_SET_MODIFIED;
	m[1] = modified;
	MessageDesktop(m, 2, instance->window->handle);
}

void EsInstanceOpenComplete(EsInstance *instance, EsFileStore *file, bool success, const char *errorText, ptrdiff_t errorTextBytes) {
	if (!success || file->error != ES_SUCCESS) {
		if (errorText && errorTextBytes) {
			EsDialogShow(instance->window, INTERFACE_STRING(FileCannotOpen),
					errorText, errorTextBytes,
					ES_ICON_DIALOG_ERROR, ES_DIALOG_ALERT_OK_BUTTON);
		} else {
			const char *errorMessage = interfaceString_FileLoadErrorUnknown;

			switch (file->error) {
				case ES_ERROR_DRIVE_ERROR_FILE_DAMAGED:
					errorMessage = interfaceString_FileLoadErrorCorrupt;
					break;
				case ES_ERROR_DRIVE_CONTROLLER_REPORTED:
					errorMessage = interfaceString_FileLoadErrorDrive;
					break;
				case ES_ERROR_INSUFFICIENT_RESOURCES:
					errorMessage = interfaceString_FileLoadErrorResourcesLow;
					break;
			}

			EsDialogShow(instance->window, INTERFACE_STRING(FileCannotOpen), 
					errorMessage, -1, ES_ICON_DIALOG_ERROR, ES_DIALOG_ALERT_OK_BUTTON);
		}

		// TODO Close the instance after the dialog is closed?
	} else {
#if 0
		if (!message->instanceOpen.update) {
			EsUndoClear(instance->undoManager);
		}
#endif

		EsInstanceSetModified(instance, false);
	}

	EsAssert(!file->operationComplete);
	file->operationComplete = true;
}

void EsInstanceSaveComplete(EsInstance *instance, EsFileStore *file, bool success) {
	if (file->error != ES_SUCCESS) {
		success = false;
	}

	if (success) {
		file->error = EsFileControl(file->handle, ES_FILE_CONTROL_FLUSH);

		if (file->error != ES_SUCCESS) {
			success = false;
		}
	}

	APIInstance *apiInstance = (APIInstance *) instance->_private;

	if (instance) {
		char buffer[1];
		buffer[0] = DESKTOP_MSG_COMPLETE_SAVE;
		MessageDesktop(buffer, 1, instance->window->handle);

		if (success) {
			EsInstanceSetModified(instance, false);
			size_t messageBytes;
			char *message = EsStringAllocateAndFormat(&messageBytes, "Saved to %s", // TODO Localization.
					apiInstance->startupInformation->filePathBytes, apiInstance->startupInformation->filePath); 
			EsAnnouncementShow(instance->window, ES_FLAGS_DEFAULT, -1, -1, message, messageBytes);
			EsHeapFree(message);
			EsCommandSetDisabled(&apiInstance->commandShowInFileManager, false);

			if (apiInstance->closeAfterSaveCompletes) {
				EsInstanceClose(instance);
			}
		} else {
			apiInstance->closeAfterSaveCompletes = false;

			char buffer[512];
			const char *errorMessage = interfaceString_FileSaveErrorUnknown;
			ptrdiff_t errorMessageBytes = -1;

			switch (file->error) {
				case ES_ERROR_FILE_DOES_NOT_EXIST: 
				case ES_ERROR_NODE_DELETED: 
				case ES_ERROR_PERMISSION_NOT_GRANTED: 
				case ES_ERROR_INCORRECT_NODE_TYPE:
					errorMessage = interfaceString_FileSaveErrorFileDeleted;
					break;
				case ES_ERROR_DRIVE_ERROR_FILE_DAMAGED:
					errorMessage = interfaceString_FileSaveErrorCorrupt;
					break;
				case ES_ERROR_DRIVE_CONTROLLER_REPORTED:
					errorMessage = interfaceString_FileSaveErrorDrive;
					break;
				case ES_ERROR_COULD_NOT_RESIZE_FILE:
				case ES_ERROR_FILE_TOO_FRAGMENTED:
					errorMessage = interfaceString_FileSaveErrorTooLarge;
					break;
				case ES_ERROR_FILE_IN_EXCLUSIVE_USE:
				case ES_ERROR_FILE_CANNOT_GET_EXCLUSIVE_USE:
				case ES_ERROR_FILE_HAS_WRITERS:
					errorMessage = interfaceString_FileSaveErrorConcurrentAccess;
					break;
				case ES_ERROR_DRIVE_FULL:
					errorMessage = interfaceString_FileSaveErrorDriveFull;
					break;
				case ES_ERROR_INSUFFICIENT_RESOURCES:
					errorMessage = interfaceString_FileSaveErrorResourcesLow;
					break;
				case ES_ERROR_TOO_MANY_FILES_WITH_NAME:
					errorMessage = interfaceString_FileSaveErrorTooManyFiles;
					break;

				case ES_ERROR_FILE_ALREADY_EXISTS: {
					errorMessage = buffer;
					errorMessageBytes = EsStringFormat(buffer, sizeof(buffer), interfaceString_FileSaveErrorAlreadyExists, 
							apiInstance->newNameBytes, apiInstance->newName);
				} break;
			}

			EsDialogShow(instance->window, INTERFACE_STRING(FileCannotSave), 
					errorMessage, errorMessageBytes, ES_ICON_DIALOG_ERROR, ES_DIALOG_ALERT_OK_BUTTON);
		}
	}

	EsAssert(!file->operationComplete);
	file->operationComplete = true;
}

uintptr_t EsSystemGetOptimalWorkQueueThreadCount() {
	return EsSyscall(ES_SYSCALL_PROCESSOR_COUNT, 0, 0, 0, 0);
}

__attribute__((no_instrument_function))
void ThreadInitialise(ThreadLocalStorage *local) {
	EsMemoryZero(local, sizeof(ThreadLocalStorage));
	EsSyscall(ES_SYSCALL_THREAD_GET_ID, ES_CURRENT_THREAD, (uintptr_t) &local->id, 0, 0);
	local->self = local;
	EsSyscall(ES_SYSCALL_THREAD_SET_TLS, (uintptr_t) local - tlsStorageOffset, 0, 0, 0);
	EsSyscall(ES_SYSCALL_THREAD_SET_TIMER_ADJUST_ADDRESS, (uintptr_t) &local->timerAdjustTicks, 0, 0, 0);
}

extern "C" void _start(EsProcessStartupInformation *_startupInformation) {
	ThreadLocalStorage threadLocalStorage;

	api.startupInformation = _startupInformation;
	bool isDesktop = api.startupInformation->isDesktop;
	
	if (isDesktop) {
		// Initialise the API table.

		EsAssert(sizeof(apiTable) <= 0xF000); // API table is too large.
		EsMemoryCopy(ES_API_BASE, apiTable, sizeof(apiTable));
	}

	// Initialise the API.

	_init();
	EsRandomSeed(ProcessorReadTimeStamp());
	ThreadInitialise(&threadLocalStorage);
	EsMessageMutexAcquire();

	api.global = (GlobalData *) EsMemoryMapObject(api.startupInformation->globalDataRegion, 
			0, sizeof(GlobalData), isDesktop ? ES_MEMORY_MAP_OBJECT_READ_WRITE : ES_MEMORY_MAP_OBJECT_READ_ONLY);
	theming.scale = api.global->uiScale; // We'll receive ES_MSG_UI_SCALE_CHANGED when this changes.

#ifdef PROFILE_DESKTOP_FUNCTIONS
	size_t profilingBufferSize = 64 * 1024 * 1024;
	GfProfilingInitialise((ProfilingEntry *) EsHeapAllocate(profilingBufferSize, true), 
			profilingBufferSize / sizeof(ProfilingEntry), api.startupInformation->timeStampTicksPerMs);
#endif

	if (isDesktop) {
		EsPrint("Reached Desktop process.\n");

		// Process messages until we find the boot file system.

		while (!api.foundBootFileSystem) {
			EsMessage *message = EsMessageReceive();
			DesktopSendMessage(message);
		}

		size_t fileSize;
		void *file = EsFileReadAll(K_SYSTEM_CONFIGURATION, -1, &fileSize);
		EsAssert(file); 
		SystemConfigurationLoad((char *) file, fileSize);
		EsHeapFree(file);

		_EsNodeInformation node;
		char *path;

		path = EsSystemConfigurationReadString(EsLiteral("paths"), EsLiteral("fonts"));
		NodeOpen(path, EsCStringLength(path), ES_NODE_DIRECTORY, &node);
		NodeAddMountPoint(EsLiteral("|Fonts:"), node.handle, false);
		EsHeapFree(path);

		SettingsLoadDefaults();
		SettingsUpdateGlobalAndWindowManager();
		SettingsWindowColorUpdated();

		ThemeInitialise();

		DesktopEntry(); 
	} else {
		EsBuffer buffer = {};
		buffer.bytes = EsConstantBufferGetSize(api.startupInformation->data.systemData);
		void *_data = EsHeapAllocate(buffer.bytes, false);
		EsConstantBufferRead(api.startupInformation->data.systemData, _data);
		EsHandleClose(api.startupInformation->data.systemData);
		buffer.in = (const uint8_t *) _data;

		const SystemStartupDataHeader *header = (const SystemStartupDataHeader *) EsBufferRead(&buffer, sizeof(SystemStartupDataHeader));
		theming.cursorData = header->themeCursorData;

		for (uintptr_t i = 0; i < header->initialMountPointCount; i++) {
			const EsMountPoint *mountPoint = (const EsMountPoint *) EsBufferRead(&buffer, sizeof(EsMountPoint));
			NodeAddMountPoint(mountPoint->prefix, mountPoint->prefixBytes, mountPoint->base, true);
		}

		for (uintptr_t i = 0; i < header->initialDeviceCount; i++) {
			const EsMessageDevice *device = (const EsMessageDevice *) EsBufferRead(&buffer, sizeof(EsMessageDevice));
			api.connectedDevices.Add(*device);
		}

		EsHeapFree(_data);

		uint8_t m = DESKTOP_MSG_SYSTEM_CONFIGURATION_GET;
		EsBuffer responseBuffer = { .canGrow = true };
		MessageDesktop(&m, 1, ES_INVALID_HANDLE, &responseBuffer);
		SystemConfigurationLoad((char *) responseBuffer.out, responseBuffer.bytes);
		EsHeapFree(responseBuffer.out);

		((void (*)()) api.startupInformation->applicationStartAddress)();
	}

	EsThreadTerminate(ES_CURRENT_THREAD);
}

void EsPanic(const char *cFormat, ...) {
	char buffer[256];
	va_list arguments;
	va_start(arguments, cFormat);
	size_t bytes = EsStringFormatV(buffer, sizeof(buffer), cFormat, arguments); 
	va_end(arguments);
	EsPrintDirect(buffer, bytes);
	EsSyscall(ES_SYSCALL_PROCESS_CRASH, 0, 0, 0, 0);
}

void EsAssertionFailure(const char *cFile, int line) {
	EsPanic("Assertion failure at %z:%d.\n", cFile, line);
}

void EsUnimplemented() {
	EsAssert(false);
}

void EsCommandAddButton(EsCommand *command, EsButton *button) {
	EsAssert(command->registered); // Command has not been registered.
	Array<EsElement *> elements = { command->elements };
	elements.Add(button);
	command->elements = elements.array;
	button->state |= UI_STATE_COMMAND_BUTTON;
	EsElementSetEnabled(button, command->enabled);
	EsButtonSetCheck(button, command->check); // Set the check before setting the callback, so that it doesn't get called.
	EsButtonOnCommand(button, command->callback, command);
}

EsCommand *EsCommandRegister(EsCommand *command, EsInstance *_instance, 
		const char *title, ptrdiff_t titleBytes,
		EsCommandCallback callback, uint32_t stableID, 
		const char *cDefaultKeyboardShortcut, bool enabled) {
	if (!command) {
		command = (EsCommand *) EsHeapAllocate(sizeof(EsCommand), true);
		if (!command) return nullptr;
		command->allocated = true;
	}

	APIInstance *instance = (APIInstance *) _instance->_private;
	command->callback = callback;
	command->registered = true;
	command->stableID = stableID;
	command->cKeyboardShortcut = cDefaultKeyboardShortcut;
	command->enabled = enabled;
	command->title = title;
	command->titleBytes = titleBytes == -1 ? EsCStringLength(title) : titleBytes;
	EsAssert(!instance->commands.Get(&stableID)); // Command already registered.
	*instance->commands.Put(&stableID) = command;
	return command;
}

void EsCommandSetDisabled(EsCommand *command, bool disabled) {
	EsAssert(command->registered); // Command has not been registered.

	if (command->enabled != !disabled) {
		command->enabled = !disabled;

		for (uintptr_t i = 0; i < ArrayLength(command->elements); i++) {
			EsElementSetDisabled(command->elements[i], disabled);
		}
	}
}

void EsCommandSetCheck(EsCommand *command, EsCheckState check, bool sendUpdatedMessage) {
	EsAssert(command->registered); // Command has not been registered.

	if (check != command->check) {
		command->check = check;

		for (uintptr_t i = 0; i < ArrayLength(command->elements); i++) {
			EsButtonSetCheck((EsButton *) command->elements[i], check, sendUpdatedMessage);
		}
	}
}

void EsCommandSetCallback(EsCommand *command, EsCommandCallback callback) {
	EsAssert(command->registered); // Command has not been registered.

	if (callback != command->callback) {
		command->callback = callback;

		for (uintptr_t i = 0; i < ArrayLength(command->elements); i++) {
			if (command->elements[i]->state & UI_STATE_COMMAND_BUTTON) {
				EsButtonOnCommand((EsButton *) command->elements[i], callback, command);
			}
		}
	}

	if (!callback) {
		EsCommandSetDisabled(command, true);
	}
}

EsCommand *EsCommandByID(EsInstance *_instance, uint32_t id) {
	APIInstance *instance = (APIInstance *) _instance->_private;
	EsCommand *command = instance->commands.Get1(&id);
	EsAssert(command); // Invalid command ID.
	return command;
}

uint32_t EsIconIDFromDriveType(uint8_t driveType) {
	if (driveType == ES_DRIVE_TYPE_HDD             ) return ES_ICON_DRIVE_HARDDISK;
	if (driveType == ES_DRIVE_TYPE_SSD             ) return ES_ICON_DRIVE_HARDDISK_SOLIDSTATE;
	if (driveType == ES_DRIVE_TYPE_CDROM           ) return ES_ICON_MEDIA_OPTICAL;
	if (driveType == ES_DRIVE_TYPE_USB_MASS_STORAGE) return ES_ICON_DRIVE_REMOVABLE_MEDIA_USB;
	return ES_ICON_DRIVE_HARDDISK;
}

uint32_t EsIconIDFromString(const char *string, ptrdiff_t stringBytes) {
	if (!string) {
		return 0;
	}

	for (uintptr_t i = 0; i < sizeof(enumStrings_EsStandardIcon) / sizeof(enumStrings_EsStandardIcon[0]) - 1; i++) {
		if (0 == EsStringCompare(enumStrings_EsStandardIcon[i].cName + 3, -1, string, stringBytes)) {
			return i;
		}
	}

	return 0;
}

struct UndoItemFooter {
	EsUndoCallback callback;
	ptrdiff_t bytes;
	bool endOfGroup;
};

bool EsUndoPeek(EsUndoManager *manager, EsUndoCallback *callback, const void **item) {
	EsMessageMutexCheck();
	Array<uint8_t> *stack = manager->state == UNDO_MANAGER_STATE_UNDOING ? &manager->redoStack : &manager->undoStack;

	if (!stack->Length()) {
		return false;
	}

	UndoItemFooter *footer = (UndoItemFooter *) (stack->array + stack->Length()) - 1;
	*callback = footer->callback;
	*item = (uint8_t *) footer - footer->bytes;
	return true;
}

void EsUndoPop(EsUndoManager *manager) {
	EsMessageMutexCheck();
	EsInstanceSetActiveUndoManager(manager->instance, manager);
	Array<uint8_t> *stack = manager->state == UNDO_MANAGER_STATE_UNDOING ? &manager->redoStack : &manager->undoStack;
	size_t oldLength = stack->Length();
	EsAssert(oldLength);
	UndoItemFooter *footer = (UndoItemFooter *) (stack->array + stack->Length()) - 1;
	EsMessage m = {};
	m.type = ES_MSG_UNDO_CANCEL;
	footer->callback((uint8_t *) footer - footer->bytes, manager, &m);
	stack->SetLength(oldLength - footer->bytes - sizeof(UndoItemFooter));
	EsCommandSetDisabled(EsCommandByID(manager->instance, ES_COMMAND_UNDO), !manager->undoStack.Length());
	EsCommandSetDisabled(EsCommandByID(manager->instance, ES_COMMAND_REDO), !manager->redoStack.Length());
}

void EsUndoClear(EsUndoManager *manager) {
	Array<uint8_t> *stack = manager->state == UNDO_MANAGER_STATE_UNDOING ? &manager->redoStack : &manager->undoStack;

	while (stack->Length()) {
		EsUndoPop(manager);
	}
}

void EsUndoPush(EsUndoManager *manager, EsUndoCallback callback, const void *item, size_t itemBytes, bool setAsActiveUndoManager) {
	EsMessageMutexCheck();

	if (setAsActiveUndoManager) {
		EsInstanceSetActiveUndoManager(manager->instance, manager);
	}

	Array<uint8_t> *stack = manager->state == UNDO_MANAGER_STATE_UNDOING ? &manager->redoStack : &manager->undoStack;

	UndoItemFooter footer = {};
	footer.callback = callback;
	footer.bytes = (itemBytes + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1);

	size_t oldLength = stack->Length();
	stack->SetLength(footer.bytes + sizeof(footer) + oldLength);
	EsMemoryCopy(stack->array + oldLength, item, itemBytes);
	EsMemoryCopy(stack->array + oldLength + footer.bytes, &footer, sizeof(footer));

	if (manager->state == UNDO_MANAGER_STATE_NORMAL) {
		// Clear the redo stack.
		manager->state = UNDO_MANAGER_STATE_UNDOING;
		EsUndoClear(manager);
		manager->state = UNDO_MANAGER_STATE_NORMAL;
	}

	if (((APIInstance *) manager->instance->_private)->activeUndoManager == manager) {
		EsCommandSetDisabled(EsCommandByID(manager->instance, ES_COMMAND_UNDO), !manager->undoStack.Length());
		EsCommandSetDisabled(EsCommandByID(manager->instance, ES_COMMAND_REDO), !manager->redoStack.Length());
	}

	if (manager->instance->undoManager == manager) {
		EsInstanceSetModified(manager->instance, true);
	}
}

void EsUndoContinueGroup(EsUndoManager *manager) {
	EsMessageMutexCheck();
	EsAssert(manager->state == UNDO_MANAGER_STATE_NORMAL);
	Array<uint8_t> stack = manager->undoStack;
	if (!stack.Length()) return;
	UndoItemFooter *footer = (UndoItemFooter *) (stack.array + stack.Length()) - 1;
	footer->endOfGroup = false;
}

void EsUndoEndGroup(EsUndoManager *manager) {
	EsMessageMutexCheck();
	EsAssert(manager->state == UNDO_MANAGER_STATE_NORMAL);
	Array<uint8_t> stack = manager->undoStack;
	if (!stack.Length()) return;
	UndoItemFooter *footer = (UndoItemFooter *) (stack.array + stack.Length()) - 1;
	footer->endOfGroup = true;
}

bool EsUndoIsEmpty(EsUndoManager *manager, bool redo) {
	EsMessageMutexCheck();
	EsAssert(manager->state == UNDO_MANAGER_STATE_NORMAL);
	Array<uint8_t> stack = redo ? manager->redoStack : manager->undoStack;
	return stack.Length() == 0;
}

void EsUndoInvokeGroup(EsUndoManager *manager, bool redo) {
	EsMessageMutexCheck();
	EsInstanceSetActiveUndoManager(manager->instance, manager);
	EsAssert(manager->state == UNDO_MANAGER_STATE_NORMAL);
	manager->state = redo ? UNDO_MANAGER_STATE_REDOING : UNDO_MANAGER_STATE_UNDOING;

	Array<uint8_t> *stack = redo ? &manager->redoStack : &manager->undoStack;
	EsAssert(stack->Length());
	bool first = true;

	while (stack->Length()) {
		size_t oldLength = stack->Length();
		UndoItemFooter *footer = (UndoItemFooter *) (stack->array + oldLength) - 1;

		if (!first && footer->endOfGroup) break;
		first = false;

		EsMessage m = {};
		m.type = ES_MSG_UNDO_INVOKE;
		footer->callback((uint8_t *) footer - footer->bytes, manager, &m);
		stack->SetLength(oldLength - footer->bytes - sizeof(UndoItemFooter));
	}

	{
		Array<uint8_t> stack = redo ? manager->undoStack : manager->redoStack;
		EsAssert(stack.Length());
		UndoItemFooter *footer = (UndoItemFooter *) (stack.array + stack.Length()) - 1;
		footer->endOfGroup = true;
	}

	manager->state = UNDO_MANAGER_STATE_NORMAL;

	EsCommandSetDisabled(EsCommandByID(manager->instance, ES_COMMAND_UNDO), !manager->undoStack.Length());
	EsCommandSetDisabled(EsCommandByID(manager->instance, ES_COMMAND_REDO), !manager->redoStack.Length());
}

bool EsUndoInUndo(EsUndoManager *manager) {
	EsMessageMutexCheck();
	return manager->state != UNDO_MANAGER_STATE_NORMAL;
}

void UndoManagerDestroy(EsUndoManager *manager) {
	EsMessageMutexCheck();

	EsAssert(manager->state == UNDO_MANAGER_STATE_NORMAL);
	EsUndoClear(manager);
	manager->state = UNDO_MANAGER_STATE_UNDOING;
	EsUndoClear(manager);
	manager->undoStack.Free();
	manager->redoStack.Free();
	manager->state = UNDO_MANAGER_STATE_NORMAL;

	if (((APIInstance *) manager->instance->_private)->activeUndoManager == manager) {
		EsInstanceSetActiveUndoManager(manager->instance, manager->instance->undoManager);
	}
}

EsInstance *EsUndoGetInstance(EsUndoManager *manager) {
	return manager->instance;
}

void EsInstanceSetActiveUndoManager(EsInstance *_instance, EsUndoManager *manager) {
	EsMessageMutexCheck();
	APIInstance *instance = (APIInstance *) _instance->_private;

	if (instance->activeUndoManager == manager) {
		return;
	}

	EsUndoEndGroup(instance->activeUndoManager);
	instance->activeUndoManager = manager;

	EsCommandSetDisabled(EsCommandByID(manager->instance, ES_COMMAND_UNDO), !manager->undoStack.Length());
	EsCommandSetDisabled(EsCommandByID(manager->instance, ES_COMMAND_REDO), !manager->redoStack.Length());
}

const void *EsBundleFind(const EsBundle *bundle, const char *_name, ptrdiff_t nameBytes, size_t *byteCount) {
	if (!bundle) {
		bundle = &bundleDefault;
	}

	if (nameBytes == -1) {
		nameBytes = EsCStringLength(_name);
	}

	if (bundle->bytes != -1) {
		if ((size_t) bundle->bytes < sizeof(BundleHeader) 
				|| (size_t) (bundle->bytes - sizeof(BundleHeader)) / sizeof(BundleFile) < bundle->base->fileCount
				|| bundle->base->signature != BUNDLE_SIGNATURE || bundle->base->version != 1) {
			return nullptr;
		}
	}

	const BundleHeader *header = bundle->base;
	const BundleFile *files = (const BundleFile *) (header + 1);
	uint64_t name = CalculateCRC64(_name, nameBytes, 0);

	for (uintptr_t i = 0; i < header->fileCount; i++) {
		if (files[i].nameCRC64 == name) {
			if (byteCount) {
				*byteCount = files[i].bytes;
			}

			if (bundle->bytes != -1) {
				if (files[i].offset >= (size_t) bundle->bytes || files[i].bytes > (size_t) (bundle->bytes - files[i].offset)) {
					return nullptr;
				}
			}

			return (const uint8_t *) header + files[i].offset;
		}
	}

	return nullptr;
}

struct EsUserTask {
	EsUserTaskCallback callback;
	EsGeneric data;
	EsHandle taskHandle;

#define USER_TASK_MINIMUM_TIME_BETWEEN_PROGRESS_MESSAGES_MS (50)
	double lastProgressMs;
};

void UserTaskThread(EsGeneric _task) {
	EsUserTask *task = (EsUserTask *) _task.p;
	task->callback(task, task->data);
	EsMessageMutexAcquire();
	EsMessageMutexRelease();
	EsSyscall(ES_SYSCALL_WINDOW_CLOSE, task->taskHandle, 0, 0, 0);
	EsHandleClose(task->taskHandle);
	EsHeapFree(task);
}

void EsUserTaskSetProgress(EsUserTask *task, double progress, EsFileOffsetDifference bytesPerSecond) {
	(void) bytesPerSecond;
	double timeMs = EsTimeStampMs();

	if (timeMs - task->lastProgressMs >= USER_TASK_MINIMUM_TIME_BETWEEN_PROGRESS_MESSAGES_MS) {
		task->lastProgressMs = timeMs;
		progress = ClampDouble(0.0, 1.0, progress);
		uint8_t buffer[1 + sizeof(double)];
		buffer[0] = DESKTOP_MSG_SET_PROGRESS;
		EsMemoryCopy(buffer + 1, &progress, sizeof(double));
		MessageDesktop(buffer, sizeof(buffer), task->taskHandle);
	}
}

bool EsUserTaskIsRunning(EsUserTask *task) {
	(void) task;
	return true; // TODO.
}

EsError EsUserTaskStart(EsUserTaskCallback callback, EsGeneric data, const char *title, ptrdiff_t titleBytes, uint32_t iconID) {
	// TODO Only tell the Desktop about the task if it's going to take >1 seconds.
	// 	Maybe check after 500ms if the task is <50% complete?

	EsMessageMutexCheck();

	EsUserTask *task = (EsUserTask *) EsHeapAllocate(sizeof(EsUserTask), true);

	if (!task) {
		return ES_ERROR_INSUFFICIENT_RESOURCES;
	}

	uint8_t m = DESKTOP_MSG_START_USER_TASK;
	EsBuffer response = { .canGrow = true };
	MessageDesktop(&m, 1, ES_INVALID_HANDLE, &response);
	EsHandle handle;
	EsBufferReadInto(&response, &handle, sizeof(handle));
	EsHeapFree(response.out);

	if (!handle) {
		EsHeapFree(task);
		return ES_ERROR_INSUFFICIENT_RESOURCES;
	}

	{
		char buffer[4096];
		buffer[0] = DESKTOP_MSG_SET_ICON;
		EsMemoryCopy(buffer + 1, &iconID, sizeof(uint32_t));
		MessageDesktop(buffer, 1 + sizeof(uint32_t), handle);
		size_t bytes = EsStringFormat(buffer, 4096, "%c%s", DESKTOP_MSG_SET_TITLE, titleBytes == -1 ? EsCStringLength(title) : titleBytes, title);
		MessageDesktop(buffer, bytes, handle);
	}

	task->callback = callback;
	task->data = data;
	task->taskHandle = handle;
	EsThreadInformation information;
	EsError error = EsThreadCreate(UserTaskThread, &information, task);

	if (error == ES_SUCCESS) {
		EsHandleClose(information.handle);
	} else {
		EsSyscall(ES_SYSCALL_WINDOW_CLOSE, task->taskHandle, 0, 0, 0);
		EsHandleClose(task->taskHandle);
		EsHeapFree(task);
	}

	return error;
}

void TimersThread(EsGeneric) {
	// TODO Maybe terminate this thread after ~10 seconds of no timers?

	double oldTimeMs = EsTimeStampMs();

	while (true) {
		double newTimeMs = EsTimeStampMs();
		double deltaTimeMs = newTimeMs - oldTimeMs;
		oldTimeMs = newTimeMs;

		int64_t waitFor = ES_WAIT_NO_TIMEOUT;

		EsMutexAcquire(&api.timersMutex);

		for (uintptr_t i = 0; i < api.timers.Length(); i++) {
			Timer *timer = &api.timers[i];
			timer->afterMs -= deltaTimeMs; // Update time remaining.

			if (timer->afterMs <= 0) {
				EsMessage m = { ES_MSG_TIMER };
				m.user.context1.p = (void *) timer->callback;
				m.user.context2 = timer->argument;
				EsMessagePost(nullptr, &m);
				api.timers.DeleteSwap(i);
				i--;
			} else if (waitFor == ES_WAIT_NO_TIMEOUT || timer->afterMs < waitFor) {
				waitFor = timer->afterMs;
			}
		}

		EsMutexRelease(&api.timersMutex);

		EsWait(&api.timersEvent /* timer set/canceled */, 1, waitFor /* until the next timer */);
	}
}

EsTimer EsTimerSet(uint64_t afterMs, EsTimerCallback callback, EsGeneric argument) {
	EsMutexAcquire(&api.timersMutex);

	static EsTimer _id = 0;
	EsTimer id = ++_id;

	Timer timer = { .id = id, .afterMs = (double) afterMs, .callback = callback, .argument = argument };

	if (!api.timers.Add(timer)) {
		id = 0; // Insufficient resources.
	} else {
		if (!api.timersEvent) {
			api.timersEvent = EsEventCreate(true);
		}

		if (!api.timersThread) {
			EsThreadInformation information;
			api.timersThread = EsThreadCreate(TimersThread, &information, 0);
		}

		EsEventSet(api.timersEvent); // Wake up the timer thread.
	}

	EsMutexRelease(&api.timersMutex);

	return id;
}

void EsTimerCancel(EsTimer id) {
	EsMutexAcquire(&api.timersMutex);

	for (uintptr_t i = 0; i < api.timers.Length(); i++) {
		if (api.timers[i].id == id) {
			api.timers.DeleteSwap(i);

			if (api.timersEvent) {
				EsEventSet(api.timersEvent); // Wake up the timer thread.
			}

			break;
		}
	}

	EsMutexRelease(&api.timersMutex);
}

bool EsWorkIsExiting() {
	return api.workFinish;
}

void WorkThread(EsGeneric) {
	while (true) {
		EsWait(&api.workAvailable, 1, ES_WAIT_NO_TIMEOUT);

		while (true) {
			EsMutexAcquire(&api.workMutex);

			if (api.workQueue.Length()) {
				Work work = api.workQueue[0];
				api.workQueue.Delete(0);
				EsMutexRelease(&api.workMutex);
				work.callback(work.context);
			} else {
				EsMutexRelease(&api.workMutex);

				if (api.workFinish) {
					EsEventSet(api.workAvailable); // Wake up another thread.
					return;
				} else {
					break;
				}
			}
		}
	}
}

EsError EsWorkQueue(EsWorkCallback callback, EsGeneric context) {
	EsMutexAcquire(&api.workMutex);

	if (!api.workAvailable) {
		api.workAvailable = EsEventCreate(true /* autoReset */);
	}

	EsThreadInformation thread = {};

	while (api.workThreads.Length() < EsSystemGetOptimalWorkQueueThreadCount()) {
		EsError error = EsThreadCreate(WorkThread, &thread, nullptr);
		if (error != ES_SUCCESS) return error;
		api.workThreads.Add(thread.handle);
	}

	Work work = { callback, context };
	bool success = api.workQueue.Add(work);
	EsMutexRelease(&api.workMutex);
	EsEventSet(api.workAvailable);
	return success ? ES_SUCCESS : ES_ERROR_INSUFFICIENT_RESOURCES;
}

#ifndef ENABLE_POSIX_SUBSYSTEM
void EsPOSIXInitialise(int *, char ***) {
	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_INSTANCE_CREATE) {
			EsInstance *instance = EsInstanceCreate(message, INTERFACE_STRING(POSIXTitle));
			EsPanel *panel = EsPanelCreate((EsElement *) instance->window, ES_PANEL_VERTICAL | ES_CELL_FILL, ES_STYLE_PANEL_WINDOW_BACKGROUND);
			EsTextDisplayCreate(panel, ES_CELL_H_CENTER | ES_CELL_V_FILL | ES_TEXT_DISPLAY_RICH_TEXT, nullptr, INTERFACE_STRING(POSIXUnavailable));
		}
	}
}

long EsPOSIXSystemCall(long, long, long, long, long, long, long) {
	EsAssert(false);
	return 0;
}

char *EsPOSIXConvertPath(const char *, size_t *, bool) {
	EsAssert(false);
	return nullptr;
}
#else
EsProcessStartupInformation *ProcessGetStartupInformation() {
	return api.startupInformation;
}
#endif
