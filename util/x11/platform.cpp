#undef _start
#undef ES_FORWARD
#define Font _Font
#define _start _StartDesktop
#undef ARRAY_DEFINITIONS_ONLY
#include <desktop/api.cpp>
#undef _start
#undef Font

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/statvfs.h>
#include <sys/mman.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>

extern "C" void _StartApplication();

struct Object {
#define OBJECT_THREAD  (1)
#define OBJECT_PROCESS (2)
#define OBJECT_EVENT   (3)
#define OBJECT_SHMEM   (4)
#define OBJECT_NODE    (5)
#define OBJECT_WINDOW  (6)
	uint8_t type;
	uint32_t referenceCount;
};

struct Thread : Object {
};

struct Process : Object {
};

struct Event : Object {
	bool autoReset;
	sem_t semaphore;
};

struct SharedMemoryRegion : Object {
	void *pointer;
	size_t bytes;
};

struct Node : Object {
	int fd;
};

struct UIWindow : Object {
	EsObjectID id;
	Window window;
	XImage *image;
	XIC xic;
	void *apiObject;
	int x, y;
	int width, height;
	uint32_t *bits;
};

struct MemoryMapping {
	SharedMemoryRegion *region;
	uintptr_t address;
	size_t bytes;
};

struct DesktopRequest {
	uint8_t *message;
	size_t bytes;
	EsHandle window;
	int pipe;
};

struct {
	Display *display;
	Visual *visual;
	Window rootWindow;
	XIM xim;
	Atom windowClosedID;
	int cursorX, cursorY;
	unsigned int modifiers;
} ui;

__thread uintptr_t tls;
GlobalData globalData;
pthread_mutex_t handlesMutex;
Array<Object *> handles;
Array<EsHandle> unusedHandles;
sem_t messagesAvailable;
pthread_mutex_t messageQueueMutex;
Array<_EsMessageWithObject> messageQueue;
pthread_mutex_t memoryMappingsMutex;
Array<MemoryMapping> memoryMappings;
pthread_mutex_t windowsMutex;
Array<UIWindow *> windows;
pthread_mutex_t desktopRequestsMutex;
Array<DesktopRequest> desktopRequests;
sem_t desktopRequestsAvailable;
volatile EsObjectID objectIDAllocator = 1;
extern BundleHeader _binary_bin_bundle_dat_start;

const char *systemConfiguration = R"(
[ui_fonts]
fallback=Inter
sans=Inter
serif=Inter
mono=Hack

[@font Inter]
category=Sans
scripts=Latn,Grek,Cyrl
license=Inter License.txt
.1=:Inter Thin.otf
.1i=:Inter Thin Italic.otf
.2=:Inter Extra Light.otf
.2i=:Inter Extra Light Italic.otf
.3=:Inter Light.otf
.3i=:Inter Light Italic.otf
.4=:Inter Regular.otf
.4i=:Inter Regular Italic.otf
.5=:Inter Medium.otf
.5i=:Inter Medium Italic.otf
.6=:Inter Semi Bold.otf
.6i=:Inter Semi Bold Italic.otf
.7=:Inter Bold.otf
.7i=:Inter Bold Italic.otf
.8=:Inter Extra Bold.otf
.8i=:Inter Extra Bold Italic.otf
.9=:Inter Black.otf
.9i=:Inter Black Italic.otf

[@font Hack]
category=Mono
scripts=Latn,Grek,Cyrl
license=Hack License.md
.4=:Hack Regular.ttf
.4i=:Hack Regular Italic.ttf
.7=:Hack Bold.ttf
.7i=:Hack Bold Italic.ttf
)";

void *PlatformHeapAllocate(size_t size, bool zero) {
	return zero ? calloc(1, size) : malloc(size);
}

void PlatformHeapFree(void *address) {
	free(address);
}

void *PlatformHeapReallocate(void *oldAddress, size_t newAllocationSize, bool zeroNewSpace) {
	assert(!zeroNewSpace);
	return realloc(oldAddress, newAllocationSize);
}

float EsCRTsqrtf(float x) {
	return sqrtf(x);
}

double EsCRTsqrt(double x) {
	return sqrt(x);
}

uintptr_t ProcessorTLSRead(uintptr_t index) {
	assert(!index);
	return tls;
}

uint64_t ProcessorReadTimeStamp() {
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);
	return (uint64_t) time.tv_sec * 1000000000 + time.tv_nsec;
}

void ReferenceClose(Object *object) {
	pthread_mutex_lock(&handlesMutex);
	assert(object);
	assert(object->referenceCount);
	object->referenceCount--;
	bool destroy = object->referenceCount == 0;
	pthread_mutex_unlock(&handlesMutex);

	if (destroy) {
		if (object->type == OBJECT_SHMEM) {
			EsHeapFree(((SharedMemoryRegion *) object)->pointer);
		} else if (object->type == OBJECT_NODE) {
			close(((Node *) object)->fd);
		} else {
			assert(false); // TODO.
		}

		EsHeapFree(object);
	}
}

EsHandle HandleOpen(Object *object) {
	EsHandle handle;
	bool closeReference = false;
	pthread_mutex_lock(&handlesMutex);

	if (unusedHandles.Length()) {
		handle = unusedHandles.Last();
		handles[handle] = object;
		unusedHandles.Pop();
	} else {
		handle = handles.Length();

		if (!handles.Add(object)) {
			handle = ES_ERROR_INSUFFICIENT_RESOURCES;
			closeReference = true;
		}
	}

	pthread_mutex_unlock(&handlesMutex);
	if (closeReference) ReferenceClose(object);
	return handle;
}

Object *HandleResolve(EsHandle handle, uint8_t type) {
	pthread_mutex_lock(&handlesMutex);
	assert(handle < handles.Length());
	Object *object = handles[handle];
	assert(object);
	assert(object->referenceCount);
	if (type) assert(object->type == type);
	pthread_mutex_unlock(&handlesMutex);
	return object;
}

void HandleClose(EsHandle handle) {
	pthread_mutex_lock(&handlesMutex);
	assert(handle < handles.Length());
	Object *object = handles[handle];
	handles[handle] = nullptr;
	unusedHandles.Add(handle);
	pthread_mutex_unlock(&handlesMutex);
	ReferenceClose(object);
}

bool MessagePost(_EsMessageWithObject *message) {
	pthread_mutex_lock(&messageQueueMutex);

	if (message->message.type == ES_MSG_MOUSE_MOVED || message->message.type == ES_MSG_WINDOW_RESIZED) {
		for (uintptr_t i = 0; i < messageQueue.Length(); i++) {
			if (messageQueue[i].object == message->object && messageQueue[i].message.type == message->message.type) {
				EsMemoryCopy(&messageQueue[i], message, sizeof(_EsMessageWithObject));
				pthread_mutex_unlock(&messageQueueMutex);
				return true;
			}
		}
	}

	bool result = messageQueue.AddPointer(message);
	sem_post(&messagesAvailable);
	pthread_mutex_unlock(&messageQueueMutex);
	return result;
}

uintptr_t _APISyscall(uintptr_t index, uintptr_t argument0, uintptr_t argument1, uintptr_t, uintptr_t argument2, uintptr_t argument3) {
	// TODO.
	
	if (index == ES_SYSCALL_THREAD_GET_ID) {
		return pthread_self();
	} else if (index == ES_SYSCALL_THREAD_SET_TLS) {
		tls = argument0;
		return ES_SUCCESS;
	} else if (index == ES_SYSCALL_THREAD_SET_TIMER_ADJUST_ADDRESS) {
		return ES_SUCCESS;
	} else if (index == ES_SYSCALL_EVENT_CREATE) {
		Event *event = (Event *) EsHeapAllocate(sizeof(Event), true);
		if (!event) return ES_ERROR_INSUFFICIENT_RESOURCES;

		event->type = OBJECT_EVENT;
		event->referenceCount = 1;
		event->autoReset = argument0;

		if (0 != sem_init(&event->semaphore, 0, 0)) { 
			EsHeapFree(event); 
			return ES_ERROR_UNKNOWN; 
		} else {
			return HandleOpen(event);	
		}
	} else if (index == ES_SYSCALL_MEMORY_MAP_OBJECT) {
		SharedMemoryRegion *region = (SharedMemoryRegion *) HandleResolve(argument0, OBJECT_SHMEM);
		assert(argument3 != ES_MEMORY_MAP_OBJECT_READ_WRITE || argument3 != ES_MEMORY_MAP_OBJECT_READ_ONLY);
		MemoryMapping mapping = {};
		mapping.region = region;
		mapping.bytes = region->bytes;
		mapping.address = (uintptr_t) region->pointer;
		pthread_mutex_lock(&memoryMappingsMutex);
		memoryMappings.Add(mapping);
		pthread_mutex_unlock(&memoryMappingsMutex);
		return mapping.address;
	} else if (index == ES_SYSCALL_CONSTANT_BUFFER_READ) {
		SharedMemoryRegion *buffer = (SharedMemoryRegion *) HandleResolve(argument0, OBJECT_SHMEM);
		if (!argument1) return buffer->bytes;
		EsMemoryCopy((void *) argument1, buffer->pointer, buffer->bytes);
		return ES_SUCCESS;
	} else if (index == ES_SYSCALL_HANDLE_CLOSE) {
		HandleClose(argument0);
		return ES_SUCCESS;
	} else if (index == ES_SYSCALL_VOLUME_GET_INFORMATION) {
		Node *node = (Node *) HandleResolve(argument0, OBJECT_NODE);
		EsVolumeInformation *information = (EsVolumeInformation *) argument1;
		EsMemoryZero(information, sizeof(EsVolumeInformation));
		struct statvfs s;
		fstatvfs(node->fd, &s);
		information->spaceTotal = s.f_frsize * s.f_blocks;
		information->spaceUsed = information->spaceTotal - s.f_bavail * s.f_bsize;
		information->id = s.f_fsid;
		information->flags = (s.f_flag & ST_RDONLY) ? ES_VOLUME_READ_ONLY : ES_FLAGS_DEFAULT;
		information->driveType = ES_DRIVE_TYPE_OTHER;
		EsMemoryCopy(information->label, "Volume", 6);
		information->labelBytes = 6;
		return ES_SUCCESS;
	} else if (index == ES_SYSCALL_PIPE_CREATE) {
		Node *readEnd = (Node *) EsHeapAllocate(sizeof(Node), true);
		Node *writeEnd = (Node *) EsHeapAllocate(sizeof(Node), true);
		int fds[2];
		int pipeStatus = pipe(fds);

		if (!readEnd || !writeEnd || pipeStatus) {
			EsHeapFree(readEnd);
			EsHeapFree(writeEnd);
			if (!pipeStatus) { close(fds[0]); close(fds[1]); }
			return ES_ERROR_INSUFFICIENT_RESOURCES;
		}

		readEnd->type = OBJECT_NODE;
		readEnd->referenceCount = 1;
		readEnd->fd = fds[0];
		writeEnd->type = OBJECT_NODE;
		writeEnd->referenceCount = 1;
		writeEnd->fd = fds[1];

		// TODO Proper error handling.
		*(EsHandle *) argument0 = HandleOpen(readEnd);
		*(EsHandle *) argument1 = HandleOpen(writeEnd);

		return ES_SUCCESS;
	} else if (index == ES_SYSCALL_MESSAGE_DESKTOP) {
		const uint8_t *message = (const uint8_t *) argument0;
		size_t messageBytes = argument1;
		DesktopRequest request = {};
		request.message = (uint8_t *) EsHeapAllocate(messageBytes, false);
		if (!request.message) return ES_ERROR_INSUFFICIENT_RESOURCES;
		EsMemoryCopy(request.message, message, messageBytes);
		request.bytes = messageBytes;
		request.window = argument2;
		request.pipe = argument3 ? dup(((Node *) HandleResolve(argument3, OBJECT_NODE))->fd) : -1;
		pthread_mutex_lock(&desktopRequestsMutex);
		desktopRequests.Add(request);
		sem_post(&desktopRequestsAvailable);
		pthread_mutex_unlock(&desktopRequestsMutex);
		return ES_SUCCESS;
	} else if (index == ES_SYSCALL_PIPE_READ) {
		Node *node = (Node *) HandleResolve(argument0, OBJECT_NODE);
		ssize_t x = read(node->fd, (void *) argument1, argument2);
		return x < 0 ? ES_ERROR_UNKNOWN : x;
	} else if (index == ES_SYSCALL_PRINT) {
		fwrite((void *) argument0, 1, argument1, stderr);
		return ES_SUCCESS;
	} else if (index == ES_SYSCALL_MESSAGE_GET) {
		bool gotMessage = false;
		pthread_mutex_lock(&messageQueueMutex);

		if (messageQueue.Length()) {
			sem_wait(&messagesAvailable);
			*(_EsMessageWithObject *) argument0 = messageQueue.First();
			messageQueue.Delete(0);
			gotMessage = true;
		}

		pthread_mutex_unlock(&messageQueueMutex);
		return gotMessage ? ES_SUCCESS : ES_ERROR_NO_MESSAGES_AVAILABLE;
	} else if (index == ES_SYSCALL_MESSAGE_WAIT) {
		sem_wait(&messagesAvailable);
		sem_post(&messagesAvailable);
		return ES_SUCCESS;
	} else if (index == ES_SYSCALL_WINDOW_GET_ID) {
		return ((UIWindow *) HandleResolve(argument0, OBJECT_WINDOW))->id;
	} else if (index == ES_SYSCALL_MEMORY_ALLOCATE) {
		if (argument3) {
			SharedMemoryRegion *region = (SharedMemoryRegion *) EsHeapAllocate(sizeof(SharedMemoryRegion), true);
			if (!region) return ES_ERROR_INSUFFICIENT_RESOURCES;
			region->type = OBJECT_SHMEM;
			region->referenceCount = 1;
			region->pointer = EsHeapAllocate(argument0, true);
			region->bytes = argument0;
			if (!region->pointer) { EsHeapFree(region); return ES_ERROR_INSUFFICIENT_RESOURCES; }
			return HandleOpen(region);
		} else {
			MemoryMapping mapping = {};
			mapping.bytes = argument0;
			mapping.address = (uintptr_t) mmap(nullptr, mapping.bytes, PROT_READ | (argument2 == ES_MEMORY_PROTECTION_READ_WRITE ? PROT_WRITE 
						: argument2 == ES_MEMORY_PROTECTION_EXECUTABLE ? PROT_EXEC : 0), MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
			pthread_mutex_lock(&memoryMappingsMutex);
			if (mapping.address) memoryMappings.Add(mapping);
			pthread_mutex_unlock(&memoryMappingsMutex);
			return mapping.address;
		}
	} else if (index == ES_SYSCALL_MEMORY_FREE) {
		size_t bytes = 0;
		SharedMemoryRegion *region = nullptr;
		pthread_mutex_lock(&memoryMappingsMutex);

		for (uintptr_t i = 0; i < memoryMappings.Length(); i++) {
			if (memoryMappings[i].address == argument0) {
				bytes = memoryMappings[i].bytes;
				region = memoryMappings[i].region;
				memoryMappings.Delete(i);
				break;
			}
		}

		pthread_mutex_unlock(&memoryMappingsMutex);
		assert(bytes);
		if (argument1) assert(bytes == argument1);
		if (!region) munmap((void *) argument0, bytes);
		return ES_SUCCESS;
	} else if (index == ES_SYSCALL_MESSAGE_POST) {
		assert(argument2 == ES_CURRENT_PROCESS);
		_EsMessageWithObject m;
		m.object = (void *) argument1;
		EsMemoryCopy(&m.message, (const void *) argument0, sizeof(EsMessage));
		return MessagePost(&m) ? ES_SUCCESS : ES_ERROR_MESSAGE_QUEUE_FULL;
	} else if (index == ES_SYSCALL_WINDOW_SET_PROPERTY) {
		uint8_t property = argument3;
		UIWindow *window = (UIWindow *) HandleResolve(argument0, OBJECT_WINDOW);

		if (property == ES_WINDOW_PROPERTY_OBJECT) {
			pthread_mutex_lock(&windowsMutex);
			window->apiObject = (void *) argument2;
			_EsMessageWithObject m = {};
			m.object = window->apiObject;
			m.message.type = ES_MSG_WINDOW_RESIZED;
			m.message.windowResized.content = ES_RECT_2S(window->width, window->height);
			MessagePost(&m);
			pthread_mutex_unlock(&windowsMutex);
		} else {
			fprintf(stderr, "Unimplemented window property %d.\n", property);
			exit(1);
			return ES_ERROR_UNSUPPORTED_FEATURE;
		}

		return ES_SUCCESS;
	} else if (index == ES_SYSCALL_WINDOW_SET_CURSOR) {
		// TODO.
		return ES_SUCCESS;
	} else if (index == ES_SYSCALL_SCREEN_FORCE_UPDATE) {
		return ES_SUCCESS;
	} else if (index == ES_SYSCALL_CURSOR_POSITION_GET) {
		EsPoint *point = (EsPoint *) argument0;
		pthread_mutex_lock(&windowsMutex);
		point->x = ui.cursorX;
		point->y = ui.cursorY;
		pthread_mutex_unlock(&windowsMutex);
		return ES_SUCCESS;
	} else if (index == ES_SYSCALL_WINDOW_GET_BOUNDS) {
		UIWindow *window = (UIWindow *) HandleResolve(argument0, OBJECT_WINDOW);
		EsRectangle *rectangle = (EsRectangle *) argument1;
		pthread_mutex_lock(&windowsMutex);
		rectangle->l = window->x;
		rectangle->r = window->x + window->width;
		rectangle->t = window->y;
		rectangle->b = window->y + window->height;
		pthread_mutex_unlock(&windowsMutex);
		return ES_SUCCESS;
	} else if (index == ES_SYSCALL_MEMORY_FAULT_RANGE) {
		return ES_SUCCESS;
	} else if (index == ES_SYSCALL_WINDOW_SET_BITS) {
		UIWindow *window = (UIWindow *) HandleResolve(argument0, OBJECT_WINDOW);
		EsRectangle region = *(EsRectangle *) argument1;
		uint32_t *data = (uint32_t *) argument2;
		size_t dataWidth = ES_RECT_WIDTH(region);
		size_t dataOffset = region.t * dataWidth + region.l;

		pthread_mutex_lock(&windowsMutex);
		region = EsRectangleIntersection(region, ES_RECT_2S(window->width, window->height));

		if (ES_RECT_VALID(region)) {
			for (int y = region.t; y < region.b; y++) {
				for (int x = region.l; x < region.r; x++) {
					window->bits[x + y * window->width] = data[x + y * dataWidth - dataOffset];
				}
			}

			XPutImage(ui.display, window->window, DefaultGC(ui.display, 0), window->image, 
					region.l, region.t, region.l, region.t,
					region.r - region.l, region.b - region.t);
		}

		pthread_mutex_unlock(&windowsMutex);
		return ES_SUCCESS;
	} else if (index == ES_SYSCALL_PROCESS_CRASH) {
		assert(false);
	} else {
		fprintf(stderr, "Unimplemented system call '%s' (%ld).\n", EnumLookupNameFromValue(enumStrings_EsSyscallType, index), index);
		exit(1);
		return ES_ERROR_UNSUPPORTED_FEATURE;
	}
}

UIWindow *UIFindWindow(Window window) {
	for (uintptr_t i = 0; i < windows.Length(); i++) {
		if (windows[i]->window == window) {
			return windows[i];
		}
	}

	return nullptr;
}

uint32_t UILookupScancode(uint32_t keycode) {
	const uint32_t remap[] = {
		0, 0, 0, 0, 
		0, 0, 0, 0,
		0, ES_SCANCODE_ESCAPE, ES_SCANCODE_1, ES_SCANCODE_2, 
		ES_SCANCODE_3, ES_SCANCODE_4, ES_SCANCODE_5, ES_SCANCODE_6,
		ES_SCANCODE_7, ES_SCANCODE_8, ES_SCANCODE_9, ES_SCANCODE_0, 
		ES_SCANCODE_HYPHEN, ES_SCANCODE_EQUALS, ES_SCANCODE_BACKSPACE, ES_SCANCODE_TAB,
		ES_SCANCODE_Q, ES_SCANCODE_W, ES_SCANCODE_E, ES_SCANCODE_R, 
		ES_SCANCODE_T, ES_SCANCODE_Y, ES_SCANCODE_U, ES_SCANCODE_I,
		ES_SCANCODE_O, ES_SCANCODE_P, ES_SCANCODE_LEFT_BRACE, ES_SCANCODE_RIGHT_BRACE, 
		ES_SCANCODE_ENTER, ES_SCANCODE_LEFT_CTRL, ES_SCANCODE_A, ES_SCANCODE_S,
		ES_SCANCODE_D, ES_SCANCODE_F, ES_SCANCODE_G, ES_SCANCODE_H, 
		ES_SCANCODE_J, ES_SCANCODE_K, ES_SCANCODE_L, ES_SCANCODE_PUNCTUATION_3, 
		ES_SCANCODE_PUNCTUATION_4, ES_SCANCODE_PUNCTUATION_5, ES_SCANCODE_LEFT_SHIFT, ES_SCANCODE_PUNCTUATION_1, 
		ES_SCANCODE_Z, ES_SCANCODE_X, ES_SCANCODE_C, ES_SCANCODE_V,
		ES_SCANCODE_B, ES_SCANCODE_N, ES_SCANCODE_M, ES_SCANCODE_COMMA, 
		ES_SCANCODE_PERIOD, ES_SCANCODE_SLASH, ES_SCANCODE_RIGHT_SHIFT, ES_SCANCODE_NUM_MULTIPLY,
		ES_SCANCODE_LEFT_ALT, ES_SCANCODE_SPACE, ES_SCANCODE_CAPS_LOCK, ES_SCANCODE_F1, 
		ES_SCANCODE_F2, ES_SCANCODE_F3, ES_SCANCODE_F4, ES_SCANCODE_F5,
		ES_SCANCODE_F6, ES_SCANCODE_F7, ES_SCANCODE_F8, ES_SCANCODE_F9, 
		ES_SCANCODE_F10, ES_SCANCODE_NUM_LOCK, ES_SCANCODE_SCROLL_LOCK, ES_SCANCODE_NUM_7,
		ES_SCANCODE_NUM_8, ES_SCANCODE_NUM_9, ES_SCANCODE_NUM_SUBTRACT, ES_SCANCODE_NUM_4, 
		ES_SCANCODE_NUM_5, ES_SCANCODE_NUM_6, ES_SCANCODE_NUM_ADD, ES_SCANCODE_NUM_1, 
		ES_SCANCODE_NUM_2, ES_SCANCODE_NUM_3, ES_SCANCODE_NUM_0, ES_SCANCODE_NUM_POINT, 
		0, 0, ES_SCANCODE_PUNCTUATION_6, ES_SCANCODE_F11,
		ES_SCANCODE_F12, 0, ES_SCANCODE_KATAKANA, ES_SCANCODE_HIRAGANA, 
		0, 0, 0, 0,
		ES_SCANCODE_NUM_ENTER, ES_SCANCODE_RIGHT_CTRL, ES_SCANCODE_NUM_DIVIDE, ES_SCANCODE_PRINT_SCREEN, 
		ES_SCANCODE_RIGHT_ALT, 0, ES_SCANCODE_HOME, ES_SCANCODE_UP_ARROW,
		ES_SCANCODE_PAGE_UP, ES_SCANCODE_LEFT_ARROW, ES_SCANCODE_RIGHT_ARROW, ES_SCANCODE_END, 
		ES_SCANCODE_DOWN_ARROW, ES_SCANCODE_PAGE_DOWN, ES_SCANCODE_INSERT, ES_SCANCODE_DELETE,
		0, ES_SCANCODE_MM_MUTE, ES_SCANCODE_MM_QUIETER, ES_SCANCODE_MM_LOUDER, 
		ES_SCANCODE_ACPI_POWER, ES_SCANCODE_NUM_EQUALS, 0, ES_SCANCODE_PAUSE,
	};

	if (keycode < sizeof(remap) / sizeof(remap[0])) {
		return remap[keycode];
	}

	return 0;
}

void UIProcessEvent(XEvent *event) {
	// TODO Other input events.

	if (event->type == ConfigureNotify || event->type == MotionNotify || event->type == ButtonPress || event->type == ButtonRelease
			|| event->type == KeyPress || event->type == KeyRelease) {
		Window rootReturn, childReturn;
		int winXReturn, winYReturn;
		XQueryPointer(ui.display, ui.rootWindow, &rootReturn, &childReturn, &ui.cursorX, &ui.cursorY, &winXReturn, &winYReturn, &ui.modifiers); 
	}

	if (event->type == ClientMessage && (Atom) event->xclient.data.l[0] == ui.windowClosedID) {
		// TODO Properly exiting.
		exit(0);
	} else if (event->type == ConfigureNotify) {
		UIWindow *window = UIFindWindow(event->xconfigure.window);

		window->x = event->xconfigure.x;
		window->y = event->xconfigure.y;

		if (window->width != event->xconfigure.width || window->height != event->xconfigure.height) {
			window->width = event->xconfigure.width;
			window->height = event->xconfigure.height;
			window->bits = (uint32_t *) EsHeapReallocate(window->bits, window->width * window->height * 4, false); // TODO Copying old bits correctly.
			window->image->width = window->width;
			window->image->height = window->height;
			window->image->bytes_per_line = window->width * 4;
			window->image->data = (char *) window->bits;

			if (window->apiObject) {
				_EsMessageWithObject m = {};
				m.object = window->apiObject;
				m.message.type = ES_MSG_WINDOW_RESIZED;
				m.message.windowResized.content = ES_RECT_2S(window->width, window->height);
				MessagePost(&m);
			}
		}
	} else if (event->type == Expose) {
		UIWindow *window = UIFindWindow(event->xexpose.window);
		if (!window) return;
		XPutImage(ui.display, window->window, DefaultGC(ui.display, 0), window->image, 0, 0, 0, 0, window->width, window->height);
	} else if (event->type == MotionNotify) {
		UIWindow *window = UIFindWindow(event->xmotion.window);
		if (!window || !window->apiObject) return;
		_EsMessageWithObject m = {};
		m.object = window->apiObject;
		m.message.type = ES_MSG_MOUSE_MOVED;
		m.message.mouseMoved.newPositionX = event->xmotion.x;
		m.message.mouseMoved.newPositionY = event->xmotion.y;
		MessagePost(&m);
	} else if (event->type == ButtonPress || event->type == ButtonRelease) {
		UIWindow *window = UIFindWindow(event->xbutton.window);
		if (!window || !window->apiObject) return;

		if (event->xbutton.button >= 1 && event->xbutton.button <= 3) {
			_EsMessageWithObject m = {};
			m.object = window->apiObject;
			if (event->xbutton.button == 1 && event->type == ButtonPress  ) m.message.type = ES_MSG_MOUSE_LEFT_DOWN;
			if (event->xbutton.button == 1 && event->type == ButtonRelease) m.message.type = ES_MSG_MOUSE_LEFT_UP;
			if (event->xbutton.button == 3 && event->type == ButtonPress  ) m.message.type = ES_MSG_MOUSE_RIGHT_DOWN;
			if (event->xbutton.button == 3 && event->type == ButtonRelease) m.message.type = ES_MSG_MOUSE_RIGHT_UP;
			if (event->xbutton.button == 2 && event->type == ButtonPress  ) m.message.type = ES_MSG_MOUSE_MIDDLE_DOWN;
			if (event->xbutton.button == 2 && event->type == ButtonRelease) m.message.type = ES_MSG_MOUSE_MIDDLE_UP;
			m.message.mouseDown.clickChainCount = 1;
			m.message.mouseDown.positionX = event->xbutton.x;
			m.message.mouseDown.positionY = event->xbutton.y;
			MessagePost(&m);
		}
	} else if (event->type == KeyPress || event->type == KeyRelease) {
		UIWindow *window = UIFindWindow(event->xkey.window);
		if (!window || !window->apiObject) return;
		_EsMessageWithObject m = {};
		m.object = window->apiObject;
		m.message.type = event->type == KeyPress ? ES_MSG_KEY_DOWN : ES_MSG_KEY_UP;
		if (ui.modifiers & ControlMask) m.message.keyboard.modifiers |= ES_MODIFIER_CTRL;
		if (ui.modifiers & ShiftMask) m.message.keyboard.modifiers |= ES_MODIFIER_SHIFT;
		if (ui.modifiers & Mod1Mask) m.message.keyboard.modifiers |= ES_MODIFIER_ALT;
		if (ui.modifiers & Mod4Mask) m.message.keyboard.modifiers |= ES_MODIFIER_FLAG;
		// if (ui.modifiers & AltGrMask) m.message.keyboard.modifiers |= ES_MODIFIER_ALT_GR;
		// if (ui.modifiers & NumLockMask) m.message.keyboard.numlock = true;
		m.message.keyboard.scancode = UILookupScancode(event->xkey.keycode);
		m.message.keyboard.repeat = false; // TODO.
		MessagePost(&m);
	}
}

void *UIThread(void *) {
	UIWindow *window = (UIWindow *) EsHeapAllocate(sizeof(UIWindow), true);
	window->type = OBJECT_WINDOW;
	window->referenceCount = 1;
	window->id = __sync_fetch_and_add(&objectIDAllocator, 1);
	XSetWindowAttributes attributes = {};
	window->window = XCreateWindow(ui.display, DefaultRootWindow(ui.display), 0, 0, 800, 600, 0, 0, 
			InputOutput, CopyFromParent, CWOverrideRedirect, &attributes);
	XSelectInput(ui.display, window->window, SubstructureNotifyMask | ExposureMask | PointerMotionMask 
			| ButtonPressMask | ButtonReleaseMask | KeyPressMask | KeyReleaseMask | StructureNotifyMask
			| EnterWindowMask | LeaveWindowMask | ButtonMotionMask | KeymapStateMask | FocusChangeMask);
	XMapRaised(ui.display, window->window);
	XSetWMProtocols(ui.display, window->window, &ui.windowClosedID, 1);
	window->image = XCreateImage(ui.display, ui.visual, 24, ZPixmap, 0, NULL, 10, 10, 32, 0);
	window->xic = XCreateIC(ui.xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, window->window, XNFocusWindow, window->window, NULL);
	windows.Add(window);

	_EsMessageWithObject m = {};
	m.message.type = ES_MSG_INSTANCE_CREATE;
	m.message.createInstance.window = HandleOpen(window);
	MessagePost(&m);

	while (true) {
		XEvent events[64];
		XNextEvent(ui.display, events + 0);

		int p = 1;

		int configureIndex = -1, motionIndex = -1, exposeIndex = -1;

		while (p < 64 && XPending(ui.display)) {
			XNextEvent(ui.display, events + p);

#define _UI_MERGE_EVENTS(a, b) \
		if (events[p].type == a) { \
			if (b != -1) events[b].type = 0; \
			b = p; \
		}

			_UI_MERGE_EVENTS(ConfigureNotify, configureIndex);
			_UI_MERGE_EVENTS(MotionNotify, motionIndex);
			_UI_MERGE_EVENTS(Expose, exposeIndex);

			p++;
		}

		for (int i = 0; i < p; i++) {
			if (!events[i].type) continue;
			pthread_mutex_lock(&windowsMutex);
			UIProcessEvent(events + i);
			pthread_mutex_unlock(&windowsMutex);
		}
	}
}

void *DesktopThread(void *) {
	while (true) {
		sem_wait(&desktopRequestsAvailable);
		pthread_mutex_lock(&desktopRequestsMutex);
		assert(desktopRequests.Length());
		DesktopRequest request = desktopRequests.First();
		desktopRequests.Delete(0);
		pthread_mutex_unlock(&desktopRequestsMutex);

		uint8_t *message = request.message;
		size_t messageBytes = request.bytes;
		EsHandle windowHandle = request.window;
		int pipe = request.pipe;

		if (message[0] == DESKTOP_MSG_SYSTEM_CONFIGURATION_GET) {
			// TODO List fonts.
			write(pipe, systemConfiguration, EsCRTstrlen(systemConfiguration));
			assert(pipe != -1);
		} else if (message[0] == DESKTOP_MSG_SET_TITLE) {
			UIWindow *window = (UIWindow *) HandleResolve(windowHandle, OBJECT_WINDOW);
			pthread_mutex_lock(&windowsMutex);
			char cTitle[256];
			snprintf(cTitle, sizeof(cTitle), "%.*s", (int) messageBytes - 1, message + 1);
			XStoreName(ui.display, window->window, cTitle);
			pthread_mutex_unlock(&windowsMutex);
		} else if (message[0] == DESKTOP_MSG_SET_ICON) {
			// TODO.
		} else if (message[0] == DESKTOP_MSG_SET_MODIFIED) {
			// TODO.
		} else if (message[0] == DESKTOP_MSG_CLIPBOARD_GET) {
			ClipboardInformation clipboardInformation = {};
			EsHandle fileHandle = ES_INVALID_HANDLE;
			write(pipe, &clipboardInformation, sizeof(clipboardInformation));
			write(pipe, &fileHandle, sizeof(fileHandle));
		} else {
			fprintf(stderr, "Unimplemented desktop message %d.\n", message[0]);
			exit(1);
		}

		EsHeapFree(message);
		if (pipe != -1) close(pipe);
	}
}

int main() {
	pthread_mutex_init(&memoryMappingsMutex, nullptr);
	pthread_mutex_init(&handlesMutex, nullptr);
	pthread_mutex_init(&messageQueueMutex, nullptr);
	pthread_mutex_init(&windowsMutex, nullptr);
	pthread_mutex_init(&desktopRequestsMutex, nullptr);
	sem_init(&desktopRequestsAvailable, 0, 0);
	sem_init(&messagesAvailable, 0, 0);

	XInitThreads();

	ui.display = XOpenDisplay(NULL);
	ui.visual = XDefaultVisual(ui.display, 0);
	ui.windowClosedID = XInternAtom(ui.display, "WM_DELETE_WINDOW", 0);
	ui.rootWindow = DefaultRootWindow(ui.display);

	XSetLocaleModifiers("");
	ui.xim = XOpenIM(ui.display, 0, 0, 0);

	if (!ui.xim) {
		XSetLocaleModifiers("@im=none");
		ui.xim = XOpenIM(ui.display, 0, 0, 0);
	}

	pthread_t uiThread, desktopThread;
	pthread_create(&uiThread, nullptr, UIThread, nullptr);
	pthread_create(&desktopThread, nullptr, DesktopThread, nullptr);

	globalData.clickChainTimeoutMs = 300;
	globalData.uiScale = 1.0f;
	globalData.useSmartQuotes = true;
	globalData.enableHoverState = true;
	globalData.animationTimeMultiplier = 1.0f;
	globalData.keyboardLayout = (uint16_t) 'u' | ((uint16_t) 's' << 8);

	SharedMemoryRegion *globalDataRegion = (SharedMemoryRegion *) EsHeapAllocate(sizeof(SharedMemoryRegion), true);
	globalDataRegion->type = OBJECT_SHMEM;
	globalDataRegion->referenceCount = 1;
	globalDataRegion->pointer = &globalData;
	globalDataRegion->bytes = sizeof(globalData);

	Node *baseMountPoint = (Node *) EsHeapAllocate(sizeof(Node), true);
	baseMountPoint->type = OBJECT_NODE;
	baseMountPoint->referenceCount = 1;
	baseMountPoint->fd = open("/", O_DIRECTORY | O_PATH);

	SharedMemoryRegion *startupData = (SharedMemoryRegion *) EsHeapAllocate(sizeof(SharedMemoryRegion), true);
	startupData->type = OBJECT_SHMEM;
	startupData->referenceCount = 1;
	startupData->bytes = sizeof(SystemStartupDataHeader) + sizeof(EsMountPoint);
	startupData->pointer = EsHeapAllocate(startupData->bytes, true);
	SystemStartupDataHeader *startupHeader = (SystemStartupDataHeader *) startupData->pointer;
	startupHeader->initialMountPointCount = 1;
	EsMountPoint *initialMountPoint = (EsMountPoint *) (startupHeader + 1);
	initialMountPoint->prefixBytes = 2;
	initialMountPoint->base = HandleOpen(baseMountPoint);
	EsCRTstrcpy(initialMountPoint->prefix, "0:");
	// TODO Settings mount point.

	bundleDesktop.base = &_binary_bin_bundle_dat_start;

	EsProcessStartupInformation startupInformation = {};
	startupInformation.applicationStartAddress = (uintptr_t) _StartApplication;
	startupInformation.timeStampTicksPerMs = 1000000;
	startupInformation.globalDataRegion = HandleOpen(globalDataRegion);
	startupInformation.data.systemData = HandleOpen(startupData);
	_StartDesktop(&startupInformation);

	return 0;
}
