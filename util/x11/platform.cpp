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
#include <errno.h>
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
	uintptr_t parameters[3];
};

struct Process : Object {
};

struct Event : Object {
	bool autoReset;

	union {
		struct {
			bool state;
			pthread_mutex_t mutex;
			pthread_cond_t condition;
		};

		struct {
			sem_t semaphore;
		};
	};
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
int desktopRequestPipe, desktopResponsePipe;
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

void EventInitialise(Event *event, bool autoReset) {
	if (autoReset) {
		sem_init(&event->semaphore, 0, 0);
		event->autoReset = true;
	} else {
		pthread_mutex_init(&event->mutex, nullptr);
		pthread_cond_init(&event->condition, nullptr);
		event->autoReset = false;
	}
}

void EventDestroy(Event *event) {
	if (event->autoReset) {
		sem_destroy(&event->semaphore);
	} else {
		pthread_mutex_destroy(&event->mutex);
		pthread_cond_destroy(&event->condition);
	}
}

void EventSet(Event *event) {
	if (event->autoReset) {
		int value;
		sem_getvalue(&event->semaphore, &value);
		if (!value) sem_post(&event->semaphore);
		else assert(value == 1);
	} else {
		pthread_mutex_lock(&event->mutex);
		event->state = true;
		pthread_cond_broadcast(&event->condition);
		pthread_mutex_unlock(&event->mutex);
	}
}

void EventReset(Event *event) {
	if (event->autoReset) {
		sem_trywait(&event->semaphore);
	} else {
		event->state = false;
	}
}

bool EventWait(Event *event, int timeoutMs) {
	if (timeoutMs != -1) {
		struct timespec endTime;
		clock_gettime(CLOCK_REALTIME, &endTime);
		uint64_t x = (uint64_t) endTime.tv_sec * 1000000000 
			+ (uint64_t) endTime.tv_nsec 
			+ (uint64_t) timeoutMs * 1000000;
		endTime.tv_sec = x / 1000000000;
		endTime.tv_nsec = x % 1000000000;

		if (event->autoReset) {
			while (sem_timedwait(&event->semaphore, &endTime)) {
				if (errno == ETIMEDOUT) {
					return false;
				}
			}
		} else {
			pthread_mutex_lock(&event->mutex);

			while (!event->state) {
				if (pthread_cond_timedwait(&event->condition, &event->mutex, &endTime)) {
					if (errno == ETIMEDOUT) {
						return false;
					}
				}
			}

			pthread_mutex_unlock(&event->mutex);
		}
	} else {
		if (event->autoReset) {
			while (sem_wait(&event->semaphore));
		} else {
			pthread_mutex_lock(&event->mutex);

			while (!event->state) {
				pthread_cond_wait(&event->condition, &event->mutex);
			}

			pthread_mutex_unlock(&event->mutex);
		}
	}

	return true;
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

void *ThreadUser(void *object) {
	Thread *thread = (Thread *) object;
	((void (*)(uintptr_t, uintptr_t)) thread->parameters[0])(thread->parameters[1], thread->parameters[2]);
	ReferenceClose(thread);
	return nullptr;
}

uintptr_t _APISyscall(uintptr_t index, uintptr_t argument0, uintptr_t argument1, uintptr_t, uintptr_t argument2, uintptr_t argument3) {
	// TODO.
	
	if (index == ES_SYSCALL_THREAD_GET_ID) {
		// TODO.
		return 0;
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
		EventInitialise(event, argument0);
		return HandleOpen(event);	
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
	} else if (index == ES_SYSCALL_PIPE_READ) {
		Node *node = (Node *) HandleResolve(argument0, OBJECT_NODE);
		ssize_t x = read(node->fd, (void *) argument1, argument2);
		return x < 0 ? ES_ERROR_UNKNOWN : x;
	} else if (index == ES_SYSCALL_PIPE_WRITE) {
		Node *node = (Node *) HandleResolve(argument0, OBJECT_NODE);
		ssize_t x = write(node->fd, (void *) argument1, argument2);
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
		pthread_mutex_lock(&windowsMutex);

		if (property == ES_WINDOW_PROPERTY_OBJECT) {
			window->apiObject = (void *) argument2;
			_EsMessageWithObject m = {};
			m.object = window->apiObject;
			m.message.type = ES_MSG_WINDOW_RESIZED;
			m.message.windowResized.content = ES_RECT_2S(window->width, window->height);
			MessagePost(&m);
		} else if (property == ES_WINDOW_PROPERTY_SOLID 
				|| property == ES_WINDOW_PROPERTY_MATERIAL
				|| property == ES_WINDOW_PROPERTY_BLUR_BOUNDS
				|| property == ES_WINDOW_PROPERTY_ALPHA) {
			// TODO.
		} else {
			fprintf(stderr, "Unimplemented window property %d.\n", property);
			exit(1);
			return ES_ERROR_UNSUPPORTED_FEATURE;
		}

		pthread_mutex_unlock(&windowsMutex);
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
	} else if (index == ES_SYSCALL_WINDOW_CREATE) {
		EsWindowStyle style = (EsWindowStyle) argument0;
		void *apiObject = (void *) argument2;

		UIWindow *window = (UIWindow *) EsHeapAllocate(sizeof(UIWindow), true);
		window->type = OBJECT_WINDOW;
		window->referenceCount = 1;
		window->id = __sync_fetch_and_add(&objectIDAllocator, 1);
		window->apiObject = apiObject;

		pthread_mutex_lock(&windowsMutex);
		XSetWindowAttributes attributes = {};
		attributes.override_redirect = style == ES_WINDOW_MENU;
		window->window = XCreateWindow(ui.display, DefaultRootWindow(ui.display), 0, 0, 800, 600, 0, 0, 
				InputOutput, CopyFromParent, CWOverrideRedirect, &attributes);
		XSelectInput(ui.display, window->window, SubstructureNotifyMask | ExposureMask | PointerMotionMask 
				| ButtonPressMask | ButtonReleaseMask | KeyPressMask | KeyReleaseMask | StructureNotifyMask
				| EnterWindowMask | LeaveWindowMask | ButtonMotionMask | KeymapStateMask | FocusChangeMask);
		XSetWMProtocols(ui.display, window->window, &ui.windowClosedID, 1);
		window->image = XCreateImage(ui.display, ui.visual, 24, ZPixmap, 0, NULL, 10, 10, 32, 0);
		window->xic = XCreateIC(ui.xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, window->window, 
				XNFocusWindow, window->window, NULL);
		windows.Add(window);

		if (argument2 == ES_WINDOW_MENU) {
			Atom properties[] = {
				XInternAtom(ui.display, "_NET_WM_WINDOW_TYPE", true),
				XInternAtom(ui.display, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", true),
				XInternAtom(ui.display, "_MOTIF_WM_HINTS", true),
			};

			XChangeProperty(ui.display, window->window, properties[0], XA_ATOM, 32, PropModeReplace, (uint8_t *) properties, 2);
			XSetTransientForHint(ui.display, window->window, DefaultRootWindow(ui.display));

			struct Hints {
				int flags;
				int functions;
				int decorations;
				int inputMode;
				int status;
			};

			struct Hints hints = { 0 };
			hints.flags = 2;
			XChangeProperty(ui.display, window->window, properties[2], properties[2], 32, PropModeReplace, (uint8_t *) &hints, 5);
		}

		pthread_mutex_unlock(&windowsMutex);

		return HandleOpen(window);
	} else if (index == ES_SYSCALL_WINDOW_MOVE) {
		UIWindow *window = (UIWindow *) HandleResolve(argument0, OBJECT_WINDOW);
		EsRectangle point = *(EsRectangle *) argument1;
		int width = ES_RECT_WIDTH(point), height = ES_RECT_HEIGHT(point);
		pthread_mutex_lock(&windowsMutex);

		if (argument3 & ES_WINDOW_MOVE_HIDDEN) {
			// TODO.
		} else {
			XMapWindow(ui.display, window->window);
		}

		if (argument3 & ES_WINDOW_MOVE_MAXIMIZED) {
			// TODO.
		}

		if (argument3 & ES_WINDOW_MOVE_ADJUST_TO_FIT_SCREEN) {
			for (int i = 0; i < ScreenCount(ui.display); i++) {
				Screen *screen = ScreenOfDisplay(ui.display, i);

				int x, y;
				Window child;
				XTranslateCoordinates(ui.display, screen->root, DefaultRootWindow(ui.display), 0, 0, &x, &y, &child);

				if (point.l >= x && point.l < x + screen->width 
						&& point.t >= y && point.t < y + screen->height) {
					if (point.l + width > x + screen->width) point.l = x + screen->width - width;
					if (point.t + height > y + screen->height) point.t = y + screen->height - height;
					if (point.l < x) point.l = x;
					if (point.t < y) point.t = y;
					if (point.l + width > x + screen->width) width = x + screen->width - point.l;
					if (point.t + height > y + screen->height) height = y + screen->height - point.t;
					break;
				}
			}
		}

		XMoveResizeWindow(ui.display, window->window, point.l, point.t, width, height);
		pthread_mutex_unlock(&windowsMutex);
		return ES_SUCCESS;
	} else if (index == ES_SYSCALL_THREAD_CREATE) {
		Thread *threadObject = (Thread *) EsHeapAllocate(sizeof(Thread), true);
		if (!threadObject) return ES_ERROR_INSUFFICIENT_RESOURCES;

		threadObject->type = OBJECT_THREAD;
		threadObject->referenceCount = 2;
		threadObject->parameters[0] = argument0;
		threadObject->parameters[1] = argument3;
		threadObject->parameters[2] = argument1;

		EsThreadInformation *thread = (EsThreadInformation *) argument2;
		EsMemoryZero(thread, sizeof(EsThreadInformation));

		pthread_t pthread;
		pthread_create(&pthread, nullptr, ThreadUser, threadObject);
		// TODO Handling errors.
		// TODO Saving and closing this handle.

		thread->tid = 0; // TODO.
		thread->handle = HandleOpen(threadObject);

		return ES_SUCCESS;
	} else if (index == ES_SYSCALL_EVENT_SET) {
		Event *event = (Event *) HandleResolve(argument0, OBJECT_EVENT);
		EventSet(event);
		return ES_SUCCESS;
	} else if (index == ES_SYSCALL_EVENT_RESET) {
		Event *event = (Event *) HandleResolve(argument0, OBJECT_EVENT);
		EventReset(event);
		return ES_SUCCESS;
	} else if (index == ES_SYSCALL_WAIT) {
		assert(argument1 == 1); // TODO Waiting on multiple object.
		Object *object = HandleResolve(*(EsHandle *) argument0, 0);
		assert(object->type == OBJECT_EVENT); // TODO Waiting on other object types.
		Event *event = (Event *) object;
		return EventWait(event, argument2) ? ES_ERROR_TIMEOUT_REACHED : 0;
	} else if (index == ES_SYSCALL_PROCESS_CRASH) {
		assert(false); // Do an assertion failure so it can be caught by an attached debugger.
	} else if (index == ES_SYSCALL_YIELD_SCHEDULER) {
		return ES_SUCCESS;
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
	window->xic = XCreateIC(ui.xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, window->window, 
			XNFocusWindow, window->window, NULL);
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
		uint32_t length;
		EsObjectID embeddedWindowID;
		if (sizeof(length) != read(desktopRequestPipe, &length, sizeof(length))) break;
		if (sizeof(embeddedWindowID) != read(desktopRequestPipe, &embeddedWindowID, sizeof(embeddedWindowID))) break;
		uint8_t *message = (uint8_t *) malloc(length);
		if (length != read(desktopRequestPipe, message, length)) break;
		uint32_t responseLength = 0;

		if (message[0] == DESKTOP_MSG_SYSTEM_CONFIGURATION_GET) {
			// TODO List all fonts.
			responseLength = EsCRTstrlen(systemConfiguration);
			write(desktopResponsePipe, &responseLength, sizeof(responseLength));
			write(desktopResponsePipe, systemConfiguration, responseLength);
		} else if (message[0] == DESKTOP_MSG_SET_TITLE) {
			pthread_mutex_lock(&windowsMutex);

			UIWindow *window = nullptr;

			for (uintptr_t i = 0; i < windows.Length(); i++) {
				if (windows[i]->id == embeddedWindowID) {
					window = windows[i];
					break;
				}
			}

			if (window) {
				char cTitle[256];
				snprintf(cTitle, sizeof(cTitle), "%.*s", (int) length - 1, message + 1);
				XStoreName(ui.display, window->window, cTitle);
			}

			pthread_mutex_unlock(&windowsMutex);
			write(desktopResponsePipe, &responseLength, sizeof(responseLength));
		} else if (message[0] == DESKTOP_MSG_SET_ICON) {
			// TODO.
			write(desktopResponsePipe, &responseLength, sizeof(responseLength));
		} else if (message[0] == DESKTOP_MSG_SET_MODIFIED) {
			// TODO.
			write(desktopResponsePipe, &responseLength, sizeof(responseLength));
		} else if (message[0] == DESKTOP_MSG_CLIPBOARD_GET) {
			ClipboardInformation clipboardInformation = {};
			EsHandle fileHandle = ES_INVALID_HANDLE;
			responseLength = sizeof(clipboardInformation) + sizeof(fileHandle);
			write(desktopResponsePipe, &responseLength, sizeof(responseLength));
			write(desktopResponsePipe, &clipboardInformation, sizeof(clipboardInformation));
			write(desktopResponsePipe, &fileHandle, sizeof(fileHandle));
		} else {
			fprintf(stderr, "Unimplemented desktop message %d.\n", message[0]);
			exit(1);
		}

		free(message);
	}

	return nullptr;
}

int main() {
	pthread_mutex_init(&memoryMappingsMutex, nullptr);
	pthread_mutex_init(&handlesMutex, nullptr);
	pthread_mutex_init(&messageQueueMutex, nullptr);
	pthread_mutex_init(&windowsMutex, nullptr);
	sem_init(&messagesAvailable, 0, 0);

	XInitThreads();

	for (uintptr_t i = 0; i < 0x20; i++) {
		// Prevent the first few handles from being used.
		HandleOpen(nullptr);
	}

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

	int pipes[2];
	Node *pipeObject;
	pipe(pipes);
	desktopRequestPipe = pipes[0]; 
	pipeObject = (Node *) EsHeapAllocate(sizeof(Node), true);
	pipeObject->type = OBJECT_NODE;
	pipeObject->referenceCount = 1;
	pipeObject->fd = pipes[1];
	startupHeader->desktopRequestPipe = HandleOpen(pipeObject);
	pipe(pipes);
	desktopResponsePipe = pipes[1]; 
	pipeObject = (Node *) EsHeapAllocate(sizeof(Node), true);
	pipeObject->type = OBJECT_NODE;
	pipeObject->referenceCount = 1;
	pipeObject->fd = pipes[0];
	startupHeader->desktopResponsePipe = HandleOpen(pipeObject);

	pthread_t uiThread, desktopThread;
	pthread_create(&uiThread, nullptr, UIThread, nullptr);
	pthread_create(&desktopThread, nullptr, DesktopThread, nullptr);

	bundleDesktop.base = &_binary_bin_bundle_dat_start;

	EsProcessStartupInformation startupInformation = {};
	startupInformation.applicationStartAddress = (uintptr_t) _StartApplication;
	startupInformation.timeStampTicksPerMs = 1000000;
	startupInformation.globalDataRegion = HandleOpen(globalDataRegion);
	startupInformation.data.systemData = HandleOpen(startupData);
	_StartDesktop(&startupInformation);

	return 0;
}
