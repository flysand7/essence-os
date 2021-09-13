#define ES_API
#define ES_FORWARD(x) x
#define ES_EXTERN_FORWARD extern "C"
#define ES_DIRECT_API
#include <essence.h>

#define alloca __builtin_alloca
#define FT_EXPORT(x) extern "C" x

#ifdef USE_STB_IMAGE
#define STB_IMAGE_IMPLEMENTATION
#define STBI_MALLOC(sz)           EsCRTmalloc(sz)
#define STBI_REALLOC(p,newsz)     EsCRTrealloc(p,newsz)
#define STBI_FREE(p)              EsCRTfree(p)
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_LINEAR
#define STB_IMAGE_STATIC
#include <shared/stb_image.h>
#endif

#include <emmintrin.h>

#define SHARED_COMMON_WANT_ALL
#include <shared/ini.h>
#include <shared/avl_tree.cpp>
#include <shared/heap.cpp>
#include <shared/arena.cpp>
#include <shared/linked_list.cpp>
#include <shared/hash.cpp>
#include <shared/png_decoder.cpp>
#include <shared/hash_table.cpp>
#include <shared/array.cpp>
#include <shared/unicode.cpp>
#include <shared/math.cpp>
#include <shared/strings.cpp>
#include <shared/common.cpp>

#define IMPLEMENTATION
#include <shared/array.cpp>
#undef IMPLEMENTATION

struct EnumString { const char *cName; int value; };
#include <bin/enum_strings_array.h>

#define ANIMATION_TIME_SCALE (1)

#define DESKTOP_MSG_SET_TITLE                 (1)
#define DESKTOP_MSG_SET_ICON                  (2)
#define DESKTOP_MSG_REQUEST_SAVE              (3)
#define DESKTOP_MSG_COMPLETE_SAVE             (4)
#define DESKTOP_MSG_SHOW_IN_FILE_MANAGER      (5)
#define DESKTOP_MSG_ANNOUNCE_PATH_MOVED       (6)
#define DESKTOP_MSG_RUN_TEMPORARY_APPLICATION (7)
#define DESKTOP_MSG_REQUEST_SHUTDOWN          (8)
#define DESKTOP_MSG_START_APPLICATION         (9)
#define DESKTOP_MSG_CREATE_CLIPBOARD_FILE     (10)
#define DESKTOP_MSG_CLIPBOARD_PUT             (11)
#define DESKTOP_MSG_CLIPBOARD_GET             (12)
#define DESKTOP_MSG_SYSTEM_CONFIGURATION_GET  (13)
#define DESKTOP_MSG_FILE_TYPES_GET            (14)
#define DESKTOP_MSG_UNHANDLED_KEY_EVENT       (15)
#define DESKTOP_MSG_START_USER_TASK           (16)
#define DESKTOP_MSG_SET_PROGRESS              (17)

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
			char *path;
			size_t pathBytes;
		};
	};
};

struct GlobalData {
	volatile int32_t clickChainTimeoutMs;
	volatile float uiScale;
	volatile bool swapLeftAndRightButtons;
	volatile bool showCursorShadow;
};

struct ThreadLocalStorage {
	// This must be the first field.
	ThreadLocalStorage *self;

	uint64_t id;
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

	ThreadLocalStorage firstThreadLocalStorage;

	size_t openInstanceCount; // Also counts user tasks.
} api;

ptrdiff_t tlsStorageOffset;

// Miscellanous forward declarations.
extern "C" void EsUnimplemented();
extern "C" uintptr_t ProcessorTLSRead(uintptr_t offset);
void MaybeDestroyElement(EsElement *element);
const char *GetConstantString(const char *key);
void UndoManagerDestroy(EsUndoManager *manager);
int TextGetStringWidth(EsElement *element, const EsTextStyle *style, const char *string, size_t stringBytes);
struct APIInstance *InstanceSetup(EsInstance *instance);
EsTextStyle TextPlanGetPrimaryStyle(EsTextPlan *plan);
EsFileStore *FileStoreCreateFromEmbeddedFile(const char *path, size_t pathBytes);
EsFileStore *FileStoreCreateFromPath(const char *path, size_t pathBytes);
EsFileStore *FileStoreCreateFromHandle(EsHandle handle);
void FileStoreCloseHandle(EsFileStore *fileStore);
EsError NodeOpen(const char *path, size_t pathBytes, uint32_t flags, _EsNodeInformation *node);

#include "syscall.cpp"

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
	HashStore<uint32_t, EsCommand *> commands;

	EsApplicationStartupInformation *startupInformation;
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

	const char *applicationName;
	size_t applicationNameBytes;

	struct InspectorWindow *attachedInspector;

	EsUndoManager undoManager;
	EsUndoManager *activeUndoManager;

	EsFileStore *fileStore;

	// Do not propagate messages about this instance to the application. 
	// Currently only used for inspectors.
	bool internalOnly; 

	union {
		EsInstanceClassEditorSettings editorSettings;
		EsInstanceClassViewerSettings viewerSettings;
	};
};

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

EsSystemConfigurationItem *SystemConfigurationGetItem(EsSystemConfigurationGroup *group, const char *key, ptrdiff_t keyBytes, bool createIfNeeded = false) {
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

EsSystemConfigurationGroup *SystemConfigurationGetGroup(const char *section, ptrdiff_t sectionBytes, bool createIfNeeded = false) {
	if (sectionBytes == -1) sectionBytes = EsCStringLength(section);

	for (uintptr_t i = 0; i < api.systemConfigurationGroups.Length(); i++) {
		if (0 == EsStringCompareRaw(section, sectionBytes, api.systemConfigurationGroups[i].section, api.systemConfigurationGroups[i].sectionBytes)) {
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

char *EsSystemConfigurationGroupReadString(EsSystemConfigurationGroup *group, const char *key, ptrdiff_t keyBytes, size_t *valueBytes = nullptr) {
	EsSystemConfigurationItem *item = SystemConfigurationGetItem(group, key, keyBytes);
	if (!item) { if (valueBytes) *valueBytes = 0; return nullptr; }
	if (valueBytes) *valueBytes = item->valueBytes;
	char *copy = (char *) EsHeapAllocate(item->valueBytes + 1, false);
	if (!copy) { if (valueBytes) *valueBytes = 0; return nullptr; }
	copy[item->valueBytes] = 0;
	EsMemoryCopy(copy, item->value, item->valueBytes);
	return copy;
}

int64_t EsSystemConfigurationGroupReadInteger(EsSystemConfigurationGroup *group, const char *key, ptrdiff_t keyBytes, int64_t defaultValue = 0) {
	EsSystemConfigurationItem *item = SystemConfigurationGetItem(group, key, keyBytes);
	if (!item) return defaultValue;
	return EsIntegerParse(item->value, item->valueBytes); 
}

char *EsSystemConfigurationReadString(const char *section, ptrdiff_t sectionBytes, const char *key, ptrdiff_t keyBytes, size_t *valueBytes) {
	EsMutexAcquire(&api.systemConfigurationMutex);
	EsDefer(EsMutexRelease(&api.systemConfigurationMutex));
	EsSystemConfigurationGroup *group = SystemConfigurationGetGroup(section, sectionBytes);
	if (!group) { if (valueBytes) *valueBytes = 0; return nullptr; }
	return EsSystemConfigurationGroupReadString(group, key, keyBytes, valueBytes);
}

int64_t EsSystemConfigurationReadInteger(const char *section, ptrdiff_t sectionBytes, const char *key, ptrdiff_t keyBytes, int64_t defaultValue) {
	EsMutexAcquire(&api.systemConfigurationMutex);
	EsDefer(EsMutexRelease(&api.systemConfigurationMutex));
	EsSystemConfigurationGroup *group = SystemConfigurationGetGroup(section, sectionBytes);
	if (!group) return defaultValue;
	return EsSystemConfigurationGroupReadInteger(group, key, keyBytes, defaultValue);
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

void SystemConfigurationLoad(char *file, size_t fileBytes) {
	EsINIState s = {};
	s.buffer = file;
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

	EsHeapFree(file);
}

uint8_t *ApplicationStartupInformationToBuffer(const EsApplicationStartupInformation *information, size_t *dataBytes = nullptr) {
	EsApplicationStartupInformation copy = *information;
	if (copy.filePathBytes == -1) copy.filePathBytes = EsCStringLength(copy.filePath);
	size_t bytes = 1 + sizeof(EsApplicationStartupInformation) + copy.filePathBytes;
	uint8_t *buffer = (uint8_t *) EsHeapAllocate(bytes, false);
	buffer[0] = DESKTOP_MSG_START_APPLICATION;
	EsMemoryCopy(buffer + 1, &copy, sizeof(EsApplicationStartupInformation));
	EsMemoryCopy(buffer + 1 + sizeof(EsApplicationStartupInformation), copy.filePath, copy.filePathBytes);
	if (dataBytes) *dataBytes = bytes;
	return buffer;
}

void EsApplicationStart(const EsApplicationStartupInformation *information) {
	size_t bufferBytes;
	uint8_t *buffer = ApplicationStartupInformationToBuffer(information, &bufferBytes);
	MessageDesktop(buffer, bufferBytes);
}

EsApplicationStartupInformation *ApplicationStartupInformationParse(const void *data, size_t dataBytes) {
	EsApplicationStartupInformation *startupInformation = (EsApplicationStartupInformation *) data;

	if (sizeof(EsApplicationStartupInformation) <= dataBytes) {
		dataBytes -= sizeof(EsApplicationStartupInformation);
		if ((size_t) startupInformation->filePathBytes > dataBytes) goto error;
		dataBytes -= startupInformation->filePathBytes;
		if (dataBytes) goto error;
		startupInformation->filePath = (const char *) (startupInformation + 1);
	} else {
		error:;
		EsPrint("Warning: received corrupted startup information.\n");
		return nullptr;
	}

	return startupInformation;
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

#define CHARACTER_MONO     (1) // 1 bit per pixel.
#define CHARACTER_SUBPIXEL (2) // 24 bits per pixel; each byte specifies the alpha of each RGB channel.
#define CHARACTER_IMAGE    (3) // 32 bits per pixel, ARGB.
#define CHARACTER_RECOLOR  (4) // 32 bits per pixel, AXXX.

#include "renderer.cpp"
#include "theme.cpp"

#define TEXT_RENDERER
#include "text.cpp"
#undef TEXT_RENDERER

#include "gui.cpp"

#ifndef NO_API_TABLE
const void *const apiTable[] = {
#include <bin/api_array.h>
};
#endif

extern "C" void _init();
typedef void (*StartFunction)();

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

	bool handledByUser = response;

	if (response == 0 && element->messageClass) {
		response = element->messageClass(element, message);
	}

	if (element->state & UI_STATE_INSPECTING) {
		InspectorNotifyElementEvent(element, "message", "Element processed message '%z' with response %i%z.\n", 
				EnumLookupNameFromValue(enumStrings_EsMessageType, message->type), response, handledByUser ? " (from user callback)" : "");
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

EsFileStore *FileStoreCreateFromEmbeddedFile(const char *name, size_t nameBytes) {
	EsFileStore *fileStore = (EsFileStore *) EsHeapAllocate(sizeof(EsFileStore) + nameBytes, false);
	if (!fileStore) return nullptr;
	EsMemoryZero(fileStore, sizeof(EsFileStore));
	fileStore->type = FILE_STORE_EMBEDDED_FILE;
	fileStore->handles = 1;
	fileStore->error = ES_SUCCESS;
	fileStore->path = (char *) (fileStore + 1);
	fileStore->pathBytes = nameBytes;
	EsMemoryCopy(fileStore->path, name, nameBytes);
	return fileStore;
}

void InstanceCreateFileStore(APIInstance *instance, EsHandle handle) {
	if (instance->fileStore) FileStoreCloseHandle(instance->fileStore);
	instance->fileStore = FileStoreCreateFromHandle(handle);
}

void InstancePostOpenMessage(EsInstance *_instance, bool update) {
	APIInstance *instance = (APIInstance *) _instance->_private;
	EsMessage m = { ES_MSG_INSTANCE_OPEN };
	m.instanceOpen.instance = _instance;
	m.instanceOpen.name = instance->startupInformation->filePath;
	m.instanceOpen.nameBytes = instance->startupInformation->filePathBytes;
	m.instanceOpen.file = instance->fileStore;
	m.instanceOpen.update = update;
	EsMessagePost(nullptr, &m);
}

APIInstance *InstanceSetup(EsInstance *instance) {
	APIInstance *apiInstance = (APIInstance *) EsHeapAllocate(sizeof(APIInstance), true);

	instance->_private = apiInstance;

	instance->undoManager = &apiInstance->undoManager;
	instance->undoManager->instance = instance;
	apiInstance->activeUndoManager = instance->undoManager;

	EsCommandRegister(&apiInstance->commandDelete, instance, nullptr, ES_COMMAND_DELETE, "Del");
	EsCommandRegister(&apiInstance->commandSelectAll, instance, nullptr, ES_COMMAND_SELECT_ALL, "Ctrl+A");
	EsCommandRegister(&apiInstance->commandCopy, instance, nullptr, ES_COMMAND_COPY, "Ctrl+C|Ctrl+Ins");
	EsCommandRegister(&apiInstance->commandCut, instance, nullptr, ES_COMMAND_CUT, "Ctrl+X|Shift+Del");
	EsCommandRegister(&apiInstance->commandPaste, instance, nullptr, ES_COMMAND_PASTE, "Ctrl+V|Shift+Ins");
	EsCommandRegister(&apiInstance->commandUndo, instance, nullptr, ES_COMMAND_UNDO, "Ctrl+Z");
	EsCommandRegister(&apiInstance->commandRedo, instance, nullptr, ES_COMMAND_REDO, "Ctrl+Y");
	EsCommandRegister(&apiInstance->commandSave, instance, nullptr, ES_COMMAND_SAVE, "Ctrl+S");
	EsCommandRegister(&apiInstance->commandShowInFileManager, instance, nullptr, ES_COMMAND_SHOW_IN_FILE_MANAGER, "Ctrl+Shift+O");

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
	api.openInstanceCount++;

	if (message && message->createInstance.data != ES_INVALID_HANDLE && message->createInstance.dataBytes > 1) {
		apiInstance->startupInformation = (EsApplicationStartupInformation *) EsHeapAllocate(message->createInstance.dataBytes, false);

		if (apiInstance->startupInformation) {
			void *buffer = EsHeapAllocate(message->createInstance.dataBytes, false);
			EsConstantBufferRead(message->createInstance.data, buffer);
			EsMemoryCopy(apiInstance->startupInformation, (char *) buffer + 1, message->createInstance.dataBytes - 1);
			EsHeapFree(buffer);

			if (!ApplicationStartupInformationParse(apiInstance->startupInformation, message->createInstance.dataBytes - 1)) {
				EsHeapFree(apiInstance->startupInformation);
				apiInstance->startupInformation = nullptr;
			} else {
				// Duplicate the file path, so that it can be modified in response to ES_MSG_INSTANCE_DOCUMENT_RENAMED messages.
				char *filePath = (char *) EsHeapAllocate(apiInstance->startupInformation->filePathBytes, false);
				EsMemoryCopy(filePath, apiInstance->startupInformation->filePath, apiInstance->startupInformation->filePathBytes);
				apiInstance->startupInformation->filePath = filePath;
			}
		}
	}

	if (message) {
		apiInstance->mainWindowHandle = message->createInstance.window;
		instance->window = EsWindowCreate(instance, ES_WINDOW_NORMAL);
		EsWindowSetTitle(instance->window, nullptr, 0);

		if (apiInstance->startupInformation && apiInstance->startupInformation->readHandle) {
			InstanceCreateFileStore(apiInstance, apiInstance->startupInformation->readHandle);
			InstancePostOpenMessage(instance, false);
			EsWindowSetTitle(instance->window, apiInstance->startupInformation->filePath, apiInstance->startupInformation->filePathBytes);
			EsCommandSetDisabled(&apiInstance->commandShowInFileManager, false);
		}
	}

	return instance;
}

const EsApplicationStartupInformation *EsInstanceGetStartupInformation(EsInstance *_instance) {
	APIInstance *instance = (APIInstance *) _instance->_private;
	return instance->startupInformation;
}

void EsInstanceDestroy(EsInstance *instance) {
	InspectorWindow **inspector = &((APIInstance *) instance->_private)->attachedInspector;

	if (*inspector) {
		EsInstanceDestroy(*inspector);
		(*inspector)->window->InternalDestroy();
		*inspector = nullptr;
	}

	UndoManagerDestroy(instance->undoManager);
	EsAssert(instance->window->instance == instance);
	instance->window->destroyInstanceAfterClose = true;
	EsElementDestroy(instance->window);
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
		TS("Process message\n");

		if (message.message.type == ES_MSG_INSTANCE_CREATE) {
			if (message.message.createInstance.data != ES_INVALID_HANDLE) {
				EsHandleClose(message.message.createInstance.data);
			}
		} else if (message.message.type == ES_MSG_INSTANCE_OPEN) {
			// TODO Support multithreaded file operations.
			EsAssert(message.message.instanceOpen.file->operationComplete);
		} else if (message.message.type == ES_MSG_INSTANCE_SAVE) {
			// TODO Support multithreaded file operations.
			EsAssert(message.message.instanceSave.file->operationComplete);
			FileStoreCloseHandle(message.message.instanceSave.file);
		} else if (message.message.type == ES_MSG_APPLICATION_EXIT) {
#ifdef DEBUG_BUILD
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
			MemoryLeakDetectorCheckpoint(&heap);
			EsPrint("ES_MSG_APPLICATION_EXIT - Heap allocation count: %d (%d from malloc).\n", heap.allocationsCount, mallocCount);
#endif
			EsProcessTerminateCurrent();
		} else if (message.message.type == ES_MSG_INSTANCE_DESTROY) {
			api.openInstanceCount--;

			APIInstance *instance = (APIInstance *) message.message.instanceDestroy.instance->_private;

			if (instance->startupInformation && (instance->startupInformation->flags & ES_APPLICATION_STARTUP_SINGLE_INSTANCE_IN_PROCESS)
					&& !api.openInstanceCount) {
				EsMessage m = { ES_MSG_APPLICATION_EXIT };
				EsMessagePost(nullptr, &m);
			}

			if (instance->startupInformation) {
				EsHeapFree((void *) instance->startupInformation->filePath);
			}

			EsHeapFree(instance->startupInformation);
			EsHeapFree(instance->documentPath);

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
			EsHeapFree(message.message.instanceDestroy.instance);
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
			if (instance) EsInstanceDestroy(instance);
		} else if (type == ES_MSG_INSTANCE_SAVE_RESPONSE) {
			EsMessage m = {};
			m.type = ES_MSG_INSTANCE_SAVE;
			m.instanceSave.file = (EsFileStore *) EsHeapAllocate(sizeof(EsFileStore), true);

			if (m.instanceSave.file) {
				m.instanceSave.file->error = message.message.tabOperation.error;
				m.instanceSave.file->handle = message.message.tabOperation.handle;
				m.instanceSave.file->type = FILE_STORE_HANDLE;
				m.instanceSave.file->handles = 1;
				m.instanceSave.instance = InstanceFromWindowID(message.message.tabOperation.id);

				if (m.instanceSave.file->error == ES_SUCCESS) {
					EsMemoryCopy(&message.message, &m, sizeof(EsMessage));
					return &message.message;
				} else {
					EsInstanceSaveComplete(&m, false);
				}

				EsMemoryCopy(&message.message, &m, sizeof(EsMessage));
			} else {
				if (message.message.tabOperation.handle) {
					EsHandleClose(message.message.tabOperation.handle);
				}
			}
		} else if (type == ES_MSG_INSTANCE_DOCUMENT_RENAMED) {
			char *buffer = (char *) EsHeapAllocate(message.message.tabOperation.bytes, false);

			if (buffer) {
				EsConstantBufferRead(message.message.tabOperation.handle, buffer);
				EsInstance *_instance = InstanceFromWindowID(message.message.tabOperation.id);

				if (_instance) {
					APIInstance *instance = (APIInstance *) _instance->_private;
					EsHeapFree((void *) instance->startupInformation->filePath);
					instance->startupInformation->filePath = buffer;
					instance->startupInformation->filePathBytes = message.message.tabOperation.bytes;
					EsWindowSetTitle(_instance->window, buffer, message.message.tabOperation.bytes);
				} else {
					EsHeapFree(buffer);
				}
			}

			EsHandleClose(message.message.tabOperation.handle);
		} else if (type == ES_MSG_INSTANCE_DOCUMENT_UPDATED) {
			EsInstance *_instance = InstanceFromWindowID(message.message.tabOperation.id);

			if (_instance) {
				APIInstance *instance = (APIInstance *) _instance->_private;

				InstanceCreateFileStore(instance, message.message.tabOperation.handle);

				if (!message.message.tabOperation.isSource) {
					InstancePostOpenMessage(_instance, true);
				}
			} else {
				EsHandleClose(message.message.tabOperation.handle);
			}
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
					gui.allWindows[i]->state |= UI_STATE_RELAYOUT;
					UIWindowNeedsUpdate(gui.allWindows[i]);
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
		} else if (type == ES_MSG_INSTANCE_DESTROY) {
			APIInstance *instance = (APIInstance *) message.message.instanceDestroy.instance->_private;

			if (!instance->internalOnly) {
				return &message.message;
			}
		} else {
			return &message.message;
		}
	}
}

void EsInstanceOpenComplete(EsMessage *message, bool success, const char *errorText, ptrdiff_t errorTextBytes) {
	EsInstance *instance = message->instanceOpen.instance;

	if (!success || message->instanceOpen.file->error != ES_SUCCESS) {
		if (errorTextBytes) {
			EsDialogShowAlert(instance->window, INTERFACE_STRING(FileCannotOpen),
					errorText, errorTextBytes,
					ES_ICON_DIALOG_ERROR, ES_DIALOG_ALERT_OK_BUTTON);
		} else {
			const char *errorMessage = interfaceString_FileLoadErrorUnknown;

			switch (message->instanceOpen.file->error) {
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

			EsDialogShowAlert(instance->window, INTERFACE_STRING(FileCannotOpen), 
					errorMessage, -1, ES_ICON_DIALOG_ERROR, ES_DIALOG_ALERT_OK_BUTTON);
		}

		// TODO Close the instance.
	} else {
		if (!message->instanceOpen.update) {
			EsUndoClear(instance->undoManager);
		}

		EsCommandSetDisabled(EsCommandByID(instance, ES_COMMAND_SAVE), true);
	}

	EsAssert(!message->instanceOpen.file->operationComplete);
	message->instanceOpen.file->operationComplete = true;
}

void EsInstanceSaveComplete(EsMessage *message, bool success) {
	if (message->instanceSave.file->error != ES_SUCCESS) {
		success = false;
	}

	if (success) {
		message->instanceSave.file->error = EsFileControl(message->instanceSave.file->handle, ES_FILE_CONTROL_FLUSH);

		if (message->instanceSave.file->error != ES_SUCCESS) {
			success = false;
		}
	}

	EsInstance *instance = message->instanceSave.instance;
	APIInstance *apiInstance = (APIInstance *) instance->_private;

	if (instance) {
		char buffer[1];
		buffer[0] = DESKTOP_MSG_COMPLETE_SAVE;
		MessageDesktop(buffer, 1, instance->window->handle);

		if (success) {
			EsCommandSetDisabled(EsCommandByID(instance, ES_COMMAND_SAVE), true);
			EsRectangle bounds = EsElementGetWindowBounds(instance->window->toolbarSwitcher);
			size_t messageBytes;
			char *message = EsStringAllocateAndFormat(&messageBytes, "Saved to %s", // TODO Localization.
					apiInstance->startupInformation->filePathBytes, apiInstance->startupInformation->filePath); 
			EsAnnouncementShow(instance->window, ES_FLAGS_DEFAULT, (bounds.l + bounds.r) / 2, bounds.b, message, messageBytes);
			EsHeapFree(message);
		} else {
			const char *errorMessage = interfaceString_FileSaveErrorUnknown;

			switch (message->instanceSave.file->error) {
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
				case ES_ERROR_FILE_ALREADY_EXISTS:
					errorMessage = interfaceString_FileSaveErrorAlreadyExists;
					break;
			}

			EsDialogShowAlert(instance->window, INTERFACE_STRING(FileCannotSave), 
					errorMessage, -1, ES_ICON_DIALOG_ERROR, ES_DIALOG_ALERT_OK_BUTTON);
		}
	}

	EsAssert(!message->instanceSave.file->operationComplete);
	message->instanceSave.file->operationComplete = true;
}

uintptr_t EsSystemGetOptimalWorkQueueThreadCount() {
	return api.startupInformation->optimalWorkQueueThreadCount;
}

void ThreadInitialise(ThreadLocalStorage *local) {
	EsMemoryZero(local, sizeof(ThreadLocalStorage));
	local->id = EsSyscall(ES_SYSCALL_THREAD_GET_ID, ES_CURRENT_THREAD, 0, 0, 0);
	local->self = local;
	EsSyscall(ES_SYSCALL_PROCESS_SET_TLS, (uintptr_t) local - tlsStorageOffset, 0, 0, 0);
}

#include "desktop.cpp"

extern "C" void _start(EsProcessStartupInformation *_startupInformation) {
	api.startupInformation = _startupInformation;
	bool desktop = api.startupInformation->isDesktop;
	
#ifndef NO_API_TABLE
	if (desktop) {
		// Initialise the API table.

		EsAssert(sizeof(apiTable) <= 0xF000); // API table is too large.
		EsMemoryCopy(ES_API_BASE, apiTable, sizeof(apiTable));
	}
#endif

	{
		// Initialise the API.

		_init();
		EsRandomSeed(EsTimeStamp());
		ThreadInitialise(&api.firstThreadLocalStorage);
		EsMessageMutexAcquire();

		api.global = (GlobalData *) EsObjectMap(EsMemoryOpen(sizeof(GlobalData), EsLiteral("Desktop.Global"), ES_FLAGS_DEFAULT), 
				0, sizeof(GlobalData), desktop ? ES_MAP_OBJECT_READ_WRITE : ES_MAP_OBJECT_READ_ONLY);
	}

	bool uiProcess = true; // TODO Determine this properly.

	if (desktop) {
		EsPrint("Reached Desktop process.\n");

		// Process messages until we find the boot file system.

		while (!api.foundBootFileSystem) {
			EsMessage *message = EsMessageReceive();
			DesktopMessage(message);
		}

		size_t fileSize;
		void *file = EsFileReadAll(K_SYSTEM_CONFIGURATION, -1, &fileSize);
		EsAssert(file); 
		SystemConfigurationLoad((char *) file, fileSize);

		_EsNodeInformation node;
		char *path;

		path = EsSystemConfigurationReadString(EsLiteral("general"), EsLiteral("fonts_path"));
		NodeOpen(path, EsCStringLength(path), ES_NODE_DIRECTORY, &node);
		NodeAddMountPoint(EsLiteral("|Fonts:"), node.handle, false);
		EsHeapFree(path);

		SettingsUpdateGlobalAndWindowManager();
		SettingsWindowColorUpdated();
	} else {
		EsHandle initialMountPointsBuffer = api.startupInformation->data.initialMountPoints;
		size_t initialMountPointCount = EsConstantBufferGetSize(initialMountPointsBuffer) / sizeof(EsMountPoint);
		EsMountPoint *initialMountPoints = (EsMountPoint *) EsHeapAllocate(initialMountPointCount * sizeof(EsMountPoint), false);
		EsConstantBufferRead(initialMountPointsBuffer, initialMountPoints);

		for (uintptr_t i = 0; i < initialMountPointCount; i++) {
			NodeAddMountPoint(initialMountPoints[i].prefix, initialMountPoints[i].prefixBytes, initialMountPoints[i].base, true);
		}

		EsHeapFree(initialMountPoints);
		EsHandleClose(initialMountPointsBuffer);

		EsHandle initialDevicesBuffer = api.startupInformation->data.initialDevices;
		size_t initialDevicesCount = EsConstantBufferGetSize(initialDevicesBuffer) / sizeof(EsMessageDevice);
		EsMessageDevice *initialDevices = (EsMessageDevice *) EsHeapAllocate(initialDevicesCount * sizeof(EsMessageDevice), false);
		EsConstantBufferRead(initialDevicesBuffer, initialDevices);

		for (uintptr_t i = 0; i < initialDevicesCount; i++) {
			api.connectedDevices.Add(initialDevices[i]);
		}

		EsHeapFree(initialDevices);
		EsHandleClose(initialDevicesBuffer);

		uint8_t m = DESKTOP_MSG_SYSTEM_CONFIGURATION_GET;
		EsBuffer responseBuffer = { .canGrow = true };
		MessageDesktop(&m, 1, ES_INVALID_HANDLE, &responseBuffer);
		SystemConfigurationLoad((char *) responseBuffer.out, responseBuffer.bytes);
	}

	if (uiProcess) {
		EsAssert(ThemeInitialise());
	}

	if (desktop) {
		DesktopEntry(); 
	} else {
		((StartFunction) api.startupInformation->applicationStartAddress)();
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
	EsButtonOnCommand(button, command->callback, command);
	button->state |= UI_STATE_COMMAND_BUTTON;
	EsElementSetDisabled(button, command->disabled);
	EsButtonSetCheck(button, command->check);
}

EsCommand *EsCommandRegister(EsCommand *command, EsInstance *_instance, EsCommandCallback callback, uint32_t stableID, 
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
	command->disabled = !enabled;
	EsAssert(!instance->commands.Get(&stableID)); // Command already registered.
	*instance->commands.Put(&stableID) = command;
	return command;
}

void EsCommandSetDisabled(EsCommand *command, bool disabled) {
	EsAssert(command->registered); // Command has not been registered.

	if (disabled != command->disabled) {
		command->disabled = disabled;

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

void EsPerformanceTimerPush() {
	EsSpinlockAcquire(&api.performanceTimerStackLock);

	if (api.performanceTimerStackCount < PERFORMANCE_TIMER_STACK_SIZE) {
		api.performanceTimerStack[api.performanceTimerStackCount++] = EsTimeStampMs();
	}

	EsSpinlockRelease(&api.performanceTimerStackLock);
}

double EsPerformanceTimerPop() {
	double result = 0;
	EsSpinlockAcquire(&api.performanceTimerStackLock);

	if (api.performanceTimerStackCount) {
		double start = api.performanceTimerStack[--api.performanceTimerStackCount];
		result = (EsTimeStampMs() - start) / 1000.0 /* ms to seconds */; 
	}

	EsSpinlockRelease(&api.performanceTimerStackLock);
	return result;
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

void EsUndoPush(EsUndoManager *manager, EsUndoCallback callback, const void *item, size_t itemBytes) {
	EsMessageMutexCheck();
	EsInstanceSetActiveUndoManager(manager->instance, manager);

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

	EsCommandSetDisabled(EsCommandByID(manager->instance, ES_COMMAND_UNDO), !manager->undoStack.Length());
	EsCommandSetDisabled(EsCommandByID(manager->instance, ES_COMMAND_REDO), !manager->redoStack.Length());

	if (manager->instance->undoManager == manager) {
		EsCommandSetDisabled(EsCommandByID(manager->instance, ES_COMMAND_SAVE), false);
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

const void *EsEmbeddedFileGet(const char *_name, ptrdiff_t nameBytes, size_t *byteCount) {
	if (nameBytes == -1) {
		nameBytes = EsCStringLength(_name);
	}

	const BundleHeader *header = (const BundleHeader *) BUNDLE_FILE_MAP_ADDRESS;

	if (nameBytes > 9 && 0 == EsMemoryCompare(_name, "$Desktop/", 9)) {
		header = (const BundleHeader *) BUNDLE_FILE_DESKTOP_MAP_ADDRESS;
		_name += 9, nameBytes -= 9;
	}

	const BundleFile *files = (const BundleFile *) (header + 1);
	uint64_t name = CalculateCRC64(_name, nameBytes);

	for (uintptr_t i = 0; i < header->fileCount; i++) {
		if (files[i].nameCRC64 == name) {
			if (byteCount) {
				*byteCount = files[i].bytes;
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
	api.openInstanceCount--; 
	// TODO Send ES_MSG_APPLICATION_EXIT if needed.
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
		api.openInstanceCount++;
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
