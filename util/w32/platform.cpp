#define ES_API
#define ES_FORWARD(x) x
#define ES_EXTERN_FORWARD extern "C"
#define ES_DIRECT_API
#include <essence.h>

#define OS_ESSENCE
#include <util/stb_ds.h>

#define UNICODE
#include <windows.h>
#include <dwmapi.h>
#include <xmmintrin.h>

struct CompositionAttributeData {
	DWORD attribute;
	PVOID pData;
	ULONG dataSize;
};

struct AccentPolicy {
	uint32_t state, flags, color, animation;
};

////////////////////////////////////////

extern "C" int _fltused = 0;
extern "C" void __chkstk() {}
extern "C" void _init() {}

#pragma function(memset) 
extern "C" void *memset(void *s, int c, size_t n) { 
	uint8_t *s8 = (uint8_t *) s;
	for (uintptr_t i = 0; i < n; i++) {
		s8[i] = (uint8_t) c;
	}
	return s;
}

#pragma function(memcpy)
extern "C" void *memcpy(void *dest, const void *src, size_t n) {
	uint8_t *dest8 = (uint8_t *) dest;
	const uint8_t *src8 = (const uint8_t *) src;
	for (uintptr_t i = 0; i < n; i++) {
		dest8[i] = src8[i];
	}
	return dest;
}

#pragma function(strlen)
extern "C" size_t strlen(const char *s) {
	size_t n = 0;
	while (s[n]) n++;
	return n;
}

#pragma function(memcmp)
extern "C" int memcmp(const void *a, const void *b, size_t bytes) {
	if (!bytes) {
		return 0;
	}

	const uint8_t *x = (const uint8_t *) a;
	const uint8_t *y = (const uint8_t *) b;

	for (uintptr_t i = 0; i < bytes; i++) {
		if (x[i] < y[i]) {
			return -1;
		} else if (x[i] > y[i]) {
			return 1;
		}
	}

	return 0;
}

#pragma function(strcmp)
extern "C" int strcmp(const char *s1, const char *s2) {
	while (true) {
		if (*s1 != *s2) {
			if (*s1 == 0) return -1;
			else if (*s2 == 0) return 1;
			return *s1 - *s2;
		}

		if (*s1 == 0) {
			return 0;
		}

		s1++;
		s2++;
	}
}

#pragma function(memmove)
extern "C" void *memmove(void *dest, const void *src, size_t n) { 
	if ((uintptr_t) dest < (uintptr_t) src) {
		return EsCRTmemcpy(dest, src, n);
	} else {
		uint8_t *dest8 = (uint8_t *) dest;
		const uint8_t *src8 = (const uint8_t *) src;
		for (uintptr_t i = n; i; i--) {
			dest8[i - 1] = src8[i - 1];
		}
		return dest;
	}
}

////////////////////////////////////////

HANDLE heap;
size_t heapAllocatedTotal;

void *PlatformHeapAllocate(size_t size, bool zero) {
	heapAllocatedTotal += size;
	return HeapAlloc(heap, zero ? HEAP_ZERO_MEMORY : 0, size);
}

void PlatformHeapFree(void *address) {
	heapAllocatedTotal -= HeapSize(heap, 0, address);
	HeapFree(heap, 0, address);
}

void *PlatformHeapReallocate(void *oldAddress, size_t newAllocationSize, bool zeroNewSpace) {
	heapAllocatedTotal += newAllocationSize - HeapSize(heap, 0, oldAddress);
	return HeapReAlloc(heap, zeroNewSpace ? HEAP_ZERO_MEMORY : 0, oldAddress, newAllocationSize);
}

wchar_t *ToWide(const char *in, size_t bytes) {
	if (!in) return nullptr;
	int characters = MultiByteToWideChar(CP_UTF8, 0, in, bytes, NULL, 0);
	if (!characters) return nullptr;
	wchar_t *out = (wchar_t *) HeapAlloc(heap, 0, characters * 2 + 2);
	out[MultiByteToWideChar(CP_UTF8, 0, in, bytes, out, characters)] = 0;
	return out;
}

char *ToUTF8(const wchar_t *in) {
	if (!in) return nullptr;
	int byteCount = WideCharToMultiByte(CP_UTF8, 0, in, -1, NULL, 0, NULL, NULL);
	if (!byteCount) return nullptr;
	char *out = (char *) HeapAlloc(heap, 0, byteCount + 1);
	out[WideCharToMultiByte(CP_UTF8, 0, in, -1, out, byteCount, NULL, NULL)] = 0;
	return out;
}

////////////////////////////////////////

uint64_t EsTimeStamp() {
	LARGE_INTEGER result;
	QueryPerformanceCounter(&result);
	return result.QuadPart;
}

float EsCRTsqrtf(float x) {
	float y[4];
	_mm_storeu_ps(y, _mm_sqrt_ps(_mm_set_ps(0, 0, 0, x)));
	return y[0];
}

double EsCRTsqrt(double x) {
	double y[2];
	_mm_storeu_pd(y, _mm_sqrt_pd(_mm_set_pd(0, x)));
	return y[0];
}

int _EsCRTsetjmp(EsCRTjmp_buf *env) {
	// TODO.
	return 0;
}

void _EsCRTlongjmp(EsCRTjmp_buf *env, int val) {
	// TODO.
	while (1);
}

DWORD threadLocalStorageIndex;

extern "C" uintptr_t ProcessorTLSRead(uintptr_t offset) {
	return (uintptr_t) TlsGetValue(threadLocalStorageIndex);
}

////////////////////////////////////////

struct Object {
	HANDLE handle;
	HWND window;
	void (*close)(Object *);
	bool isEmbedFile, isMenu;
};

struct Timer : Object {
	void *data1, *data2;
	bool set;
	UINT_PTR id;
};

struct ConstantBuffer : Object {
	void *data;
	size_t bytes;
};

EsMountPoint initialMountPoint = {
	.name = "0:",
	.nameBytes = 2,
	.base = ES_INVALID_HANDLE,
};

_EsMessageWithObject *messageQueue;
HCURSOR cursor, blankCursor;
Timer **allTimers;
bool allowSynchronousMessages;
bool inWindows10;
HWND recentMenu;

const char *systemConfiguration = R"(
[ui]
theme=0:$res/Theme.dat
icon_pack=0:$res/Icons/elementary Icons.icon_pack
font_fallback=Inter
font_sans=Inter
font_serif=Inter
font_mono=Hack

[@font Inter]
category=Sans
scripts=Latn,Grek,Cyrl
.1=0:$res/Fonts/Inter Thin.otf
.1i=0:$res/Fonts/Inter Thin Italic.otf
.2=0:$res/Fonts/Inter Extra Light.otf
.2i=0:$res/Fonts/Inter Extra Light Italic.otf
.3=0:$res/Fonts/Inter Light.otf
.3i=0:$res/Fonts/Inter Light Italic.otf
.4=0:$res/Fonts/Inter Regular.otf
.4i=0:$res/Fonts/Inter Regular Italic.otf
.5=0:$res/Fonts/Inter Medium.otf
.5i=0:$res/Fonts/Inter Medium Italic.otf
.6=0:$res/Fonts/Inter Semi Bold.otf
.6i=0:$res/Fonts/Inter Semi Bold Italic.otf
.7=0:$res/Fonts/Inter Bold.otf
.7i=0:$res/Fonts/Inter Bold Italic.otf
.8=0:$res/Fonts/Inter Extra Bold.otf
.8i=0:$res/Fonts/Inter Extra Bold Italic.otf
.9=0:$res/Fonts/Inter Black.otf
.9i=0:$res/Fonts/Inter Black Italic.otf

[@font Hack]
category=Mono
scripts=Latn,Grek,Cyrl
.4=0:$res/Fonts/Hack Regular.ttf
.4i=0:$res/Fonts/Hack Regular Italic.ttf
.7=0:$res/Fonts/Hack Bold.ttf
.7i=0:$res/Fonts/Hack Bold Italic.ttf
)";

////////////////////////////////////////

uint8_t *embed;

uint32_t *EmbedFind(const char *path, size_t pathBytes) {
	uint8_t *position = embed;

	while (true) {
		uint32_t *size = (uint32_t *) (position + EsCStringLength((char *) position) + 1);

		if (position[0] == 0) {
			return nullptr;
		} else if (0 == EsMemoryCompare(position, path, pathBytes) && position[pathBytes] == 0) {
			return size;
		} else {
			position = (uint8_t *) size + *size + sizeof(uint32_t);
		}
	}
}

////////////////////////////////////////

void CALLBACK TimerProcedure(HWND window, UINT message, UINT_PTR id, DWORD time) {
	Timer *timer = nullptr;
	
	for (uintptr_t i = 0; i < arrlenu(allTimers); i++) {
		if (allTimers[i]->id == id) {
			timer = allTimers[i];
			break;
		}
	}
	
	if (!timer) return;
	KillTimer(window, id);
	if (!timer->set) return;
	timer->set = false;
	EsMessage m;
	m.type = ES_MSG_TIMER;
	m._argument = timer->data2;
	EsMessagePost((EsElement *) timer->data1, &m);
}

void UIProcessWindowManagerMessage(EsWindow *window, EsMessage *message, struct ProcessMessageTiming *timing);

void SendInputMessage(EsElement *element, EsMessage *message, bool now = false) {
	if (now && allowSynchronousMessages) {
		EsMessageMutexAcquire();
		UIProcessWindowManagerMessage((EsWindow *) element, message, nullptr);
		EsMessageMutexRelease();
	} else {
		EsMessagePost(element, message);
	}
}

int ConvertVirtualKeyCode(int key) {
	switch (key) {
		case 0x08: return ES_SCANCODE_BACKSPACE;
		case 0x09: return ES_SCANCODE_TAB;
		case 0x0D: return ES_SCANCODE_ENTER;
		case 0x13: return ES_SCANCODE_PAUSE;
		case 0x14: return ES_SCANCODE_CAPS_LOCK;
		case 0x1B: return ES_SCANCODE_ESCAPE;
		case 0x20: return ES_SCANCODE_SPACE;
		case 0x21: return ES_SCANCODE_PAGE_UP;
		case 0x22: return ES_SCANCODE_PAGE_DOWN;
		case 0x23: return ES_SCANCODE_END;
		case 0x24: return ES_SCANCODE_HOME;
		case 0x25: return ES_SCANCODE_LEFT_ARROW;
		case 0x26: return ES_SCANCODE_UP_ARROW;
		case 0x27: return ES_SCANCODE_RIGHT_ARROW;
		case 0x28: return ES_SCANCODE_DOWN_ARROW;
		case 0x2D: return ES_SCANCODE_INSERT;
		case 0x2E: return ES_SCANCODE_DELETE;
		case 0x30: return ES_SCANCODE_0;
		case 0x31: return ES_SCANCODE_1;
		case 0x32: return ES_SCANCODE_2;
		case 0x33: return ES_SCANCODE_3;
		case 0x34: return ES_SCANCODE_4;
		case 0x35: return ES_SCANCODE_5;
		case 0x36: return ES_SCANCODE_6;
		case 0x37: return ES_SCANCODE_7;
		case 0x38: return ES_SCANCODE_8;
		case 0x39: return ES_SCANCODE_9;
		case 0x41: return ES_SCANCODE_A;
		case 0x42: return ES_SCANCODE_B;
		case 0x43: return ES_SCANCODE_C;
		case 0x44: return ES_SCANCODE_D;
		case 0x45: return ES_SCANCODE_E;
		case 0x46: return ES_SCANCODE_F;
		case 0x47: return ES_SCANCODE_G;
		case 0x48: return ES_SCANCODE_H;
		case 0x49: return ES_SCANCODE_I;
		case 0x4A: return ES_SCANCODE_J;
		case 0x4B: return ES_SCANCODE_K;
		case 0x4C: return ES_SCANCODE_L;
		case 0x4D: return ES_SCANCODE_M;
		case 0x4E: return ES_SCANCODE_N;
		case 0x4F: return ES_SCANCODE_O;
		case 0x50: return ES_SCANCODE_P;
		case 0x51: return ES_SCANCODE_Q;
		case 0x52: return ES_SCANCODE_R;
		case 0x53: return ES_SCANCODE_S;
		case 0x54: return ES_SCANCODE_T;
		case 0x55: return ES_SCANCODE_U;
		case 0x56: return ES_SCANCODE_V;
		case 0x57: return ES_SCANCODE_W;
		case 0x58: return ES_SCANCODE_X;
		case 0x59: return ES_SCANCODE_Y;
		case 0x5A: return ES_SCANCODE_Z;
		case 0x5B: return ES_SCANCODE_LEFT_FLAG;
		case 0x5C: return ES_SCANCODE_LEFT_FLAG;
		case 0x5D: return ES_SCANCODE_CONTEXT_MENU;
		case 0x5F: return ES_SCANCODE_ACPI_SLEEP;
		case 0x70: return ES_SCANCODE_F1;
		case 0x71: return ES_SCANCODE_F2;
		case 0x72: return ES_SCANCODE_F3;
		case 0x73: return ES_SCANCODE_F4;
		case 0x74: return ES_SCANCODE_F5;
		case 0x75: return ES_SCANCODE_F6;
		case 0x76: return ES_SCANCODE_F7;
		case 0x77: return ES_SCANCODE_F8;
		case 0x78: return ES_SCANCODE_F9;
		case 0x79: return ES_SCANCODE_F10;
		case 0x7A: return ES_SCANCODE_F11;
		case 0x7B: return ES_SCANCODE_F12;
		case 0x90: return ES_SCANCODE_NUM_LOCK;
		case 0x91: return ES_SCANCODE_SCROLL_LOCK;
		case 0xA0: return ES_SCANCODE_LEFT_SHIFT;
		case 0xA1: return ES_SCANCODE_RIGHT_SHIFT;
		case 0xA2: return ES_SCANCODE_LEFT_CTRL;
		case 0xA3: return ES_SCANCODE_RIGHT_CTRL;
		case 0xA4: return ES_SCANCODE_LEFT_ALT;
		case 0xA5: return ES_SCANCODE_RIGHT_ALT;
		case 0xA6: return ES_SCANCODE_WWW_BACK;
		case 0xA7: return ES_SCANCODE_WWW_FORWARD;
		case 0xA8: return ES_SCANCODE_WWW_REFRESH;
		case 0xA9: return ES_SCANCODE_WWW_STOP;
		case 0xAA: return ES_SCANCODE_WWW_SEARCH;
		case 0xAB: return ES_SCANCODE_WWW_STARRED;
		case 0xAC: return ES_SCANCODE_WWW_HOME;
		case 0xAD: return ES_SCANCODE_MM_MUTE;
		case 0xAE: return ES_SCANCODE_MM_QUIETER;
		case 0xAF: return ES_SCANCODE_MM_LOUDER;
		case 0xB0: return ES_SCANCODE_MM_NEXT;
		case 0xB1: return ES_SCANCODE_MM_PREVIOUS;
		case 0xB2: return ES_SCANCODE_MM_STOP;
		case 0xB3: return ES_SCANCODE_MM_PAUSE;
		case 0xB4: return ES_SCANCODE_MM_EMAIL;
		case 0xBA: return ES_SCANCODE_PUNCTUATION_3;
		case 0xBB: return ES_SCANCODE_EQUALS;
		case 0xBC: return ES_SCANCODE_COMMA;
		case 0xBD: return ES_SCANCODE_HYPHEN;
		case 0xBE: return ES_SCANCODE_PERIOD;
		case 0xBF: return ES_SCANCODE_SLASH;
		case 0xC0: return ES_SCANCODE_PUNCTUATION_5;
		case 0xDB: return ES_SCANCODE_LEFT_BRACE;
		case 0xDC: return ES_SCANCODE_PUNCTUATION_1;
		case 0xDD: return ES_SCANCODE_RIGHT_BRACE;
		case 0xDE: return ES_SCANCODE_PUNCTUATION_4;
		default:   return 0;
	}
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	EsElement *element = (EsElement *) GetWindowLongPtr(window, GWLP_USERDATA);
	
	if (!element) {
		return DefWindowProc(window, message, wParam, lParam);
	}
	
	if (message == WM_CLOSE) {
		// TODO.
		ExitProcess(0);
	} else if (message == WM_MOVE) {
		if (~GetWindowLongPtr(window, GWL_EXSTYLE) & WS_EX_NOACTIVATE) {
			EsMessageMutexAcquire();
			if (recentMenu) ShowWindow(recentMenu, SW_HIDE);
			EsMenuCloseAll();
			EsMessageMutexRelease();
		}
	} else if (message == WM_SIZE) {
		EsMessage m = {};
		m.type = ES_MSG_WINDOW_RESIZED;
		RECT client;
		GetClientRect(window, &client);
		m.windowResized.content = ES_MAKE_RECTANGLE(0, client.right, 0, client.bottom);
		SendInputMessage(element, &m, true);
	} else if (message == WM_MOUSEMOVE) {
		POINT point, screen;
		static POINT old;
		GetCursorPos(&point);
		screen = point;
		ScreenToClient(window, &point);
		EsMessage m = {};
		m.type = ES_MSG_MOUSE_MOVED;
		m.mouseMoved.newPositionX = point.x;
		m.mouseMoved.newPositionY = point.y;
		m.mouseMoved.newPositionXScreen = screen.x;
		m.mouseMoved.newPositionYScreen = screen.y;
		m.mouseMoved.oldPositionX = old.x;
		m.mouseMoved.oldPositionY = old.y;
		old = point;
		SendInputMessage(element, &m);
	} else if (message == WM_KILLFOCUS) {
		EsMessageMutexAcquire();
		EsMenuCloseAll();
		EsMessageMutexRelease();
		EsMessage m;
		m.type = ES_MSG_WINDOW_DEACTIVATED;
		SendInputMessage(element, &m);
	} else if (message == WM_SETFOCUS) {
		EsMessage m;
		m.type = ES_MSG_WINDOW_ACTIVATED;
		SendInputMessage(element, &m);
	} else if (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) {
		EsMessage m = {};
		m.type = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) ? ES_MSG_KEY_DOWN : ES_MSG_KEY_UP;
		m.keyboard.scancode = ConvertVirtualKeyCode(wParam);
		if (message == WM_KEYDOWN && (lParam & (1 << 30))) m.keyboard.repeat = true;
		m.keyboard.alt = (GetKeyState(VK_MENU) & 0x8000) ? true : false;
		m.keyboard.shift = (GetKeyState(VK_SHIFT) & 0x8000) ? true : false;
		m.keyboard.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) ? true : false;
		if (m.keyboard.scancode == ES_SCANCODE_F4 && m.keyboard.alt) SendMessage(window, WM_CLOSE, 0, 0);
		// TODO Numpad, VK_PACKET, IME.
		if (m.keyboard.scancode) SendInputMessage(element, &m);
	} else if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_RBUTTONDOWN 
			|| message == WM_RBUTTONUP || message == WM_MBUTTONDOWN || message == WM_MBUTTONUP) {
		// TODO clickChainCount.

		if (~GetWindowLongPtr(window, GWL_EXSTYLE) & WS_EX_NOACTIVATE) {
			EsMessageMutexAcquire();
			EsMenuCloseAll();
			EsMessageMutexRelease();
		}

		if (message == WM_LBUTTONDOWN || message == WM_MBUTTONDOWN || message == WM_RBUTTONDOWN) {
			SetCapture(window);
		} else {
			ReleaseCapture();
		}
		
		EsMessage m = {};
		if (message == WM_LBUTTONDOWN) m.type = ES_MSG_MOUSE_LEFT_DOWN;
		if (message == WM_LBUTTONUP)   m.type = ES_MSG_MOUSE_LEFT_UP;
		if (message == WM_RBUTTONDOWN) m.type = ES_MSG_MOUSE_RIGHT_DOWN;
		if (message == WM_RBUTTONUP)   m.type = ES_MSG_MOUSE_RIGHT_UP;
		if (message == WM_MBUTTONDOWN) m.type = ES_MSG_MOUSE_MIDDLE_DOWN;
		if (message == WM_MBUTTONUP)   m.type = ES_MSG_MOUSE_MIDDLE_UP;
		POINT point, screen;
		GetCursorPos(&point);
		screen = point;
		ScreenToClient(window, &point);
		m.mouseDown.clickChainCount = 1;
		m.mouseDown.positionX = point.x;
		m.mouseDown.positionY = point.y;
		m.mouseDown.positionXScreen = screen.x;
		m.mouseDown.positionYScreen = screen.y;
		m.mouseDown.alt = (GetKeyState(VK_MENU) & 0x8000) ? true : false;
		m.mouseDown.shift = (GetKeyState(VK_SHIFT) & 0x8000) ? true : false;
		m.mouseDown.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) ? true : false;
		SendInputMessage(element, &m);
	} else if (message == WM_NCDESTROY) {
		EsMessage m;
		m.type = ES_MSG_WINDOW_DESTROYED;
		SendInputMessage(element, &m);
	} else if (message == WM_MOUSEACTIVATE && (GetWindowLongPtr(window, GWL_EXSTYLE) & WS_EX_NOACTIVATE)) {
		return MA_NOACTIVATE;
	} else if (message == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT) {
		SetCursor(cursor);
		return TRUE;
	} else {
		return DefWindowProc(window, message, wParam, lParam);
	}
	
	return 0;
}

////////////////////////////////////////

static DWORD CALLBACK ThreadEntry(void *parameter) {
	uintptr_t *_arguments = (uintptr_t *) parameter;
	uintptr_t arguments[4];
	EsMemoryCopy(arguments, _arguments, sizeof(arguments));
	EsHeapFree(_arguments);
	((void (*)(uintptr_t, uintptr_t)) arguments[0])(arguments[3], arguments[1]);
	return 0;
}

uintptr_t _APISyscall(uintptr_t type, uintptr_t argument0, uintptr_t argument1, uintptr_t unused, uintptr_t argument2, uintptr_t argument3) {
	if (type == ES_SYSCALL_PRINT) {
		char buffer[256];
		buffer[EsStringFormat(buffer, sizeof(buffer) - 1, "%s", argument1, argument0)] = 0;
		OutputDebugStringA(buffer);
	} else if (type == ES_SYSCALL_CREATE_EVENT) {
		Object *object = (Object *) HeapAlloc(heap, HEAP_ZERO_MEMORY, sizeof(Object));
		object->handle = CreateEvent(NULL, !argument0, FALSE, NULL);
		return (uintptr_t) object;
	} else if (type == ES_SYSCALL_GET_SYSTEM_CONSTANTS) {
		uint64_t *systemConstants = (uint64_t *) argument0;
		LARGE_INTEGER performanceFrequency;
		QueryPerformanceFrequency(&performanceFrequency);
		systemConstants[ES_SYSTEM_CONSTANT_TIME_STAMP_UNITS_PER_MICROSECOND] = performanceFrequency.QuadPart / 1000000;
		systemConstants[ES_SYSTEM_CONSTANT_UI_SCALE] = 100; // TODO Proper scaling support.
	} else if (type == ES_SYSCALL_ALLOCATE) {
		// TODO Protection.
		return (uintptr_t) VirtualAlloc(NULL, argument0, MEM_RESERVE | ((argument1 & ES_MEMORY_RESERVE_COMMIT_ALL) ? MEM_COMMIT : 0), PAGE_READWRITE);
	} else if (type == ES_SYSCALL_FREE) {
		VirtualFree((void *) argument0, 0, MEM_RELEASE);
	} else if (type == ES_SYSCALL_SYSTEM_CONFIGURATION_READ) {
		ConstantBuffer *object = (ConstantBuffer *) HeapAlloc(heap, HEAP_ZERO_MEMORY, sizeof(ConstantBuffer));
		object->bytes = *(size_t *) argument2 = EsCRTstrlen(systemConfiguration);
		object->data = (void *) systemConfiguration;
		object->close = [] (Object *) {};
		return (uintptr_t) object;
	} else if (type == ES_SYSCALL_READ_CONSTANT_BUFFER) {
		ConstantBuffer *buffer = (ConstantBuffer *) argument0;
		if (!argument1) return buffer->bytes;
		EsMemoryCopy((void *) argument1, buffer->data, buffer->bytes);
	} else if (type == ES_SYSCALL_CLOSE_HANDLE) {
		Object *object = (Object *) argument0;
		if (!object->close) CloseHandle(object->handle);
		HeapFree(heap, 0, object);
	} else if (type == ES_SYSCALL_TIMER_CREATE) {
		Timer *object = (Timer *) HeapAlloc(heap, HEAP_ZERO_MEMORY, sizeof(Timer));
		arrput(allTimers, object);
		return (uintptr_t) object;
	} else if (type == ES_SYSCALL_TIMER_SET) {
		Timer *timer = (Timer *) argument0;
		timer->data1 = (void *) argument2;
		timer->data2 = (void *) argument3;
		timer->set = true;
		timer->id = SetTimer(NULL, 0, argument1, TimerProcedure);
	} else if (type == ES_SYSCALL_OPEN_NODE) {
		// TODO Error handling and opening other node types.
		
		const char *path = (const char *) argument0;
		size_t pathLength = (size_t) argument1;
		uint64_t flags = (uint64_t) argument2;	
		_EsNodeInformation *information = (_EsNodeInformation *) argument3;
		Object *object = (Object *) HeapAlloc(heap, HEAP_ZERO_MEMORY, sizeof(Object));
		information->handle = (EsHandle) object; 
		information->type = ES_NODE_FILE;
		
		if (path[0] == '$') {
			uint32_t *data = EmbedFind(path + 1, pathLength - 1);
			object->handle = (HANDLE) (data + 1);
			object->isEmbedFile = true;
			object->close = [] (Object *) {};
			information->fileSize = *data;
		} else {
			wchar_t *pathWide = ToWide(path, pathLength);
			object->handle = CreateFileW(pathWide, 
					GENERIC_READ | ((flags & (ES_FILE_WRITE | ES_FILE_WRITE_EXCLUSIVE)) ? GENERIC_WRITE : 0),
					(flags & (ES_FILE_WRITE | ES_FILE_READ_SHARED)) ? (FILE_SHARE_READ | FILE_SHARE_READ)
					: (flags & ES_FILE_READ) ? FILE_SHARE_READ : 0, NULL, 
					(flags & ES_NODE_FAIL_IF_FOUND) ? CREATE_NEW : (flags & ES_NODE_FAIL_IF_NOT_FOUND) ? OPEN_EXISTING : CREATE_NEW,
					FILE_ATTRIBUTE_NORMAL, NULL);
			HeapFree(heap, 0, pathWide);
			DWORD fileSizeHigh;
			DWORD fileSizeLow = GetFileSize(object->handle, &fileSizeHigh);
			information->fileSize = (uint64_t) fileSizeLow | ((uint64_t) fileSizeHigh << 32);
		}
	} else if (type == ES_SYSCALL_MAP_OBJECT) {
		// TODO Protection.
		// TODO Mapping handle is leaked.
		if (!argument0) return 0;
		Object *object = (Object *) argument0;

		if (object->isEmbedFile) {
			return (uintptr_t) object->handle;
		} else {
			HANDLE mapping = CreateFileMapping(object->handle, NULL, PAGE_READONLY, 0, 0, NULL);
			return (uintptr_t) MapViewOfFile(mapping, FILE_MAP_READ, (DWORD) (argument1 >> 32), (DWORD) argument1, argument2);
		}
	} else if (type == ES_SYSCALL_OPEN_SHARED_MEMORY) {
		// TODO.
		return 0;
	} else if (type == ES_SYSCALL_GET_THREAD_ID) {
		// TODO Other threads and processes.
		return GetCurrentThreadId();
	} else if (type == ES_SYSCALL_SET_TLS) {
		TlsSetValue(threadLocalStorageIndex, (void *) argument0);
	} else if (type == ES_SYSCALL_GET_MESSAGE) {
		allowSynchronousMessages = true;
		
		while (true) {
			MSG message;
			
			if (!PeekMessage(&message, NULL, 0, 0, TRUE)) {
				break;
			}
			
			TranslateMessage(&message);
			DispatchMessage(&message);
		}
		
		allowSynchronousMessages = false;
		
		if (!arrlen(messageQueue)) {
			return ES_ERROR_NO_MESSAGES_AVAILABLE;
		}
		
		*(_EsMessageWithObject *) argument0 = messageQueue[0];
		arrdel(messageQueue, 0);
	} else if (type == ES_SYSCALL_WAIT_MESSAGE) {
		// TODO Timeout.
		
		allowSynchronousMessages = true;
		
		if (!arrlen(messageQueue)) {
			MSG message;
			GetMessage(&message, NULL, 0, 0);
			TranslateMessage(&message);
			DispatchMessage(&message);
		}
		
		allowSynchronousMessages = false;
	} else if (type == ES_SYSCALL_WINDOW_CREATE) {
		// TODO Other window types.
		
		Object *object = (Object *) HeapAlloc(heap, HEAP_ZERO_MEMORY, sizeof(Object));
		
		if (argument0 == ES_WINDOW_MENU) {
			DWORD exStyle = WS_EX_TOPMOST | WS_EX_NOACTIVATE;
			object->window = CreateWindowEx(exStyle, L"host", 0, WS_POPUP, 
				0, 0, 0, 0, NULL, NULL, NULL, NULL);
			MARGINS margins = { -1 };
			DwmExtendFrameIntoClientArea(object->window, &margins);
			object->isMenu = true;
			recentMenu = object->window;
		} else {
			object->window = CreateWindow(L"host", NULL, WS_OVERLAPPEDWINDOW | WS_VISIBLE, 
				CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 
				NULL, NULL, NULL, NULL);
		}
		
		SetWindowLongPtr(object->window, GWLP_USERDATA, argument2);
		return (uintptr_t) object;
	} else if (type == ES_SYSCALL_POST_MESSAGE) {
		_EsMessageWithObject message;
		message.object = (void *) argument1;
		message.message = *(EsMessage *) argument0;
		arrput(messageQueue, message);
	} else if (type == ES_SYSCALL_WINDOW_SET_OBJECT) {
		Object *object = (Object *) argument0;
		SetWindowLongPtr(object->window, GWLP_USERDATA, argument2);
		RECT client;
		GetClientRect(object->window, &client);
		EsMessage message = {};
		message.type = ES_MSG_WINDOW_RESIZED;
		message.windowResized.content = ES_MAKE_RECTANGLE(0, client.right, 0, client.bottom);
		EsMessagePost((EsElement *) argument2, &message);
	} else if (type == ES_SYSCALL_WINDOW_SET_RESIZE_CLEAR_COLOR) {
	} else if (type == ES_SYSCALL_EMBED_WINDOW_SEND_MESSAGE) {
		Object *object = (Object *) argument2;
		const uint8_t *buffer = (const uint8_t *) argument0;
		size_t bytes = (size_t) argument1;
		
		if (buffer[0] == 1 /* EMBED_WINDOW_MESSAGE_SET_TITLE */) {
			wchar_t *title = ToWide((const char *) buffer + 1, bytes - 1);
			SetWindowTextW(object->window, title);
			HeapFree(heap, 0, title);
		}
	} else if (type == ES_SYSCALL_GET_CURSOR_POSITION) {
		POINT point = {};
		GetCursorPos(&point);
		EsPoint *result = (EsPoint *) argument0;
		result->x = point.x;
		result->y = point.y;
	} else if (type == ES_SYSCALL_WINDOW_GET_BOUNDS) {
		Object *object = (Object *) argument0;
		EsRectangle *result = (EsRectangle *) argument1;
		RECT bounds;
		GetClientRect(object->window, &bounds);
		MapWindowPoints(object->window, NULL, (POINT *) &bounds, 2);
		result->l = bounds.left;
		result->r = bounds.right;
		result->t = bounds.top;
		result->b = bounds.bottom;
	} else if (type == ES_SYSCALL_WINDOW_SET_CURSOR) {
		if (cursor && cursor != blankCursor) {
			DestroyCursor(cursor);
		}
		
		switch ((EsCursorStyle) (argument3 >> 24)) {
			case ES_CURSOR_NORMAL           : cursor = LoadCursor(NULL, IDC_ARROW);    break;
			case ES_CURSOR_TEXT             : cursor = LoadCursor(NULL, IDC_IBEAM);    break;
			case ES_CURSOR_RESIZE_VERTICAL  : cursor = LoadCursor(NULL, IDC_SIZENS);   break;
			case ES_CURSOR_RESIZE_HORIZONTAL: cursor = LoadCursor(NULL, IDC_SIZEWE);   break;
			case ES_CURSOR_RESIZE_DIAGONAL_1: cursor = LoadCursor(NULL, IDC_SIZENESW); break;
			case ES_CURSOR_RESIZE_DIAGONAL_2: cursor = LoadCursor(NULL, IDC_SIZENWSE); break;
			case ES_CURSOR_SPLIT_VERTICAL   : cursor = LoadCursor(NULL, IDC_SIZEWE);   break;
			case ES_CURSOR_SPLIT_HORIZONTAL : cursor = LoadCursor(NULL, IDC_SIZENS);   break;
			case ES_CURSOR_HAND_HOVER       : cursor = LoadCursor(NULL, IDC_HAND);     break;
			case ES_CURSOR_HAND_DRAG        : cursor = LoadCursor(NULL, IDC_HAND);     break;
			case ES_CURSOR_HAND_POINT       : cursor = LoadCursor(NULL, IDC_HAND);     break;
			case ES_CURSOR_SCROLL_UP_LEFT   : cursor = LoadCursor(NULL, IDC_SIZEALL);  break;
			case ES_CURSOR_SCROLL_UP        : cursor = LoadCursor(NULL, IDC_SIZEALL);  break;
			case ES_CURSOR_SCROLL_UP_RIGHT  : cursor = LoadCursor(NULL, IDC_SIZEALL);  break;
			case ES_CURSOR_SCROLL_LEFT      : cursor = LoadCursor(NULL, IDC_SIZEALL);  break;
			case ES_CURSOR_SCROLL_CENTER    : cursor = LoadCursor(NULL, IDC_SIZEALL);  break;
			case ES_CURSOR_SCROLL_RIGHT     : cursor = LoadCursor(NULL, IDC_SIZEALL);  break;
			case ES_CURSOR_SCROLL_DOWN_LEFT : cursor = LoadCursor(NULL, IDC_SIZEALL);  break;
			case ES_CURSOR_SCROLL_DOWN      : cursor = LoadCursor(NULL, IDC_SIZEALL);  break;
			case ES_CURSOR_SCROLL_DOWN_RIGHT: cursor = LoadCursor(NULL, IDC_SIZEALL);  break;
			case ES_CURSOR_SELECT_LINES     : cursor = LoadCursor(NULL, IDC_ARROW);    break;
			case ES_CURSOR_DROP_TEXT        : cursor = LoadCursor(NULL, IDC_ARROW);    break;
			case ES_CURSOR_CROSS_HAIR_PICK  : cursor = LoadCursor(NULL, IDC_CROSS);    break;
			case ES_CURSOR_CROSS_HAIR_RESIZE: cursor = LoadCursor(NULL, IDC_CROSS);    break;
			case ES_CURSOR_MOVE_HOVER       : cursor = LoadCursor(NULL, IDC_SIZEALL);  break;
			case ES_CURSOR_MOVE_DRAG        : cursor = LoadCursor(NULL, IDC_SIZEALL);  break;
			case ES_CURSOR_ROTATE_HOVER     : cursor = LoadCursor(NULL, IDC_ARROW);    break;
			case ES_CURSOR_ROTATE_DRAG      : cursor = LoadCursor(NULL, IDC_ARROW);    break;
			case ES_CURSOR_BLANK            : cursor = blankCursor;                    break;
			default                         : cursor = NULL;                           break;
		}

		SetCursor(cursor);
	} else if (type == ES_SYSCALL_WINDOW_SET_BITS) {
		Object *object = (Object *) argument0;
		EsRectangle *region = (EsRectangle *) argument1;
		HDC dc = GetDC(object->window);
		BITMAPINFO information = {};
		information.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		information.bmiHeader.biWidth = region->r - region->l;
		information.bmiHeader.biHeight = region->t - region->b;
		information.bmiHeader.biPlanes = 1;
		information.bmiHeader.biBitCount = 32;

		uint32_t *bits = (uint32_t *) argument2;

		if (object->isMenu) {
			for (intptr_t i = 0; i < (region->r - region->l) * (region->b - region->t); i++) {
				// Pre-multiply alpha.
				uint32_t alpha = bits[i] >> 24;
				uint32_t c0 = bits[i] >> 16, c1 = bits[i] >> 8, c2 = bits[i] >> 0;
				c0 = (c0 & 0xFF) * alpha / 255, c1 = (c1 & 0xFF) * alpha / 255, c2 = (c2 & 0xFF) * alpha / 255;
				bits[i] = (alpha << 24) | (c0 << 16) | (c1 << 8) | c2;
			}
		}

		StretchDIBits(dc, region->l, region->t, region->r - region->l, region->b - region->t, 
			0, 0, region->r - region->l, region->b - region->t, 
			bits, &information, DIB_RGB_COLORS, SRCCOPY);
		ReleaseDC(object->window, dc);
	} else if (type == ES_SYSCALL_FORCE_SCREEN_UPDATE) {
	} else if (type == ES_SYSCALL_WINDOW_SET_BLUR_BOUNDS) {
		Object *object = (Object *) argument0;
		EsRectangle *region = (EsRectangle *) argument1;

		if (inWindows10) {
			auto setAttribute = (void (*)(HWND, void *)) GetProcAddress(LoadLibrary(L"user32.dll"), "SetWindowCompositionAttribute");
			AccentPolicy policy = { 3 };
			CompositionAttributeData data = { 19, &policy, sizeof(policy) };
			setAttribute(object->window, &data);
			// TODO Only blur the specified region.
		} else {
			DWM_BLURBEHIND blur;
			blur.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
			blur.fEnable = TRUE;
			blur.hRgnBlur = CreateRectRgn(region->l, region->t, region->r, region->b);
			DwmEnableBlurBehindWindow(object->window, &blur);
			DeleteObject(blur.hRgnBlur);
		}
	} else if (type == ES_SYSCALL_WINDOW_SET_OPAQUE_BOUNDS) {
		// TODO.
	} else if (type == ES_SYSCALL_WINDOW_SET_SOLID) {
		// TODO.
	} else if (type == ES_SYSCALL_WINDOW_CLOSE) {
		Object *object = (Object *) argument0;
		if (object->window == recentMenu) recentMenu = NULL;
		DestroyWindow(object->window);
	} else if (type == ES_SYSCALL_WINDOW_MOVE) {
		Object *object = (Object *) argument0;
		EsRectangle *region = (EsRectangle *) argument1;
		
		if (argument3 & ES_MOVE_WINDOW_HIDDEN) {
			ShowWindow(object->window, SW_HIDE);
		} else {
			MoveWindow(object->window, region->l, region->t, region->r - region->l, region->b - region->t, FALSE);
			ShowWindow(object->window, SW_SHOWNOACTIVATE);
		}
	} else if (type == ES_SYSCALL_CLIPBOARD_HAS) {
		return false; // TODO.
	} else if (type == ES_SYSCALL_CREATE_THREAD) {
		EsThreadInformation *thread = (EsThreadInformation *) argument2;
		EsMemoryZero(thread, sizeof(EsThreadInformation));

		Object *object = (Object *) HeapAlloc(heap, HEAP_ZERO_MEMORY, sizeof(Object));
		uintptr_t *arguments = (uintptr_t *) EsHeapAllocate(sizeof(uintptr_t) * 4, true);
		arguments[0] = argument0;
		arguments[1] = argument1;
		arguments[2] = argument2;
		arguments[3] = argument3;
		object->handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) ThreadEntry, arguments, 0, (LPDWORD) &thread->tid);
		thread->handle = (EsHandle) object;
	} else if (type == ES_SYSCALL_TERMINATE_THREAD) {
		if (argument0 == ES_CURRENT_THREAD) {
			TerminateThread(GetCurrentThread(), 0);
		} else {
			Object *object = (Object *) argument0;
			TerminateThread(object->handle, 0);
		}
	} else if (type == ES_SYSCALL_WAIT) {
		Object **handles = (Object **) argument0;
		size_t handleCount = argument1;
		int timeout = argument2;
		HANDLE handles2[ES_MAX_WAIT_COUNT];

		for (uintptr_t i = 0; i < handleCount; i++) {
			handles2[i] = handles[i]->handle;
		}

		int result = WaitForMultipleObjects(handleCount, handles2, FALSE, timeout == ES_WAIT_NO_TIMEOUT ? INFINITE : timeout);
		return result == WAIT_TIMEOUT ? ES_ERROR_TIMEOUT_REACHED : result;
	} else if (type == ES_SYSCALL_SET_CURSOR_POSITION) {
		SetCursorPos(argument0, argument1);
	} else if (type == ES_SYSCALL_GET_CREATION_ARGUMENT) {
		if (argument1 == CREATION_ARGUMENT_INITIAL_MOUNT_POINTS) {
			ConstantBuffer *object = (ConstantBuffer *) HeapAlloc(heap, HEAP_ZERO_MEMORY, sizeof(ConstantBuffer));
			object->bytes = sizeof(EsMountPoint);
			object->data = (void *) &initialMountPoint;
			object->close = [] (Object *) {};
			return (uintptr_t) object;
		} else {
			return 0;
		}
	} else if (type == ES_SYSCALL_MEMORY_FAULT_RANGE) {
	} else if (type == ES_SYSCALL_WINDOW_SET_MATERIAL) {
	} else {
		MessageBox(NULL, L"Unimplemented system call.", L"Error", MB_OK);
		ExitProcess(1);
	}
	
	return ES_SUCCESS;
}

////////////////////////////////////////

extern "C" void _ApplicationStart();
extern "C" void _start(EsProcessStartupInformation *startupInformation);

void WinMainCRTStartup() {
	heap = GetProcessHeap();
	threadLocalStorageIndex = TlsAlloc();

        embed = (uint8_t *) LockResource(LoadResource(nullptr, FindResource(nullptr, MAKEINTRESOURCEW(101), L"TEXT")));

	uint8_t bitmask1 = 0xFF, bitmask2 = 0;
	blankCursor = CreateCursor(nullptr, 0, 0, 1, 1, &bitmask1, &bitmask2);

	OSVERSIONINFOEXW version = { sizeof(version) };
	((LONG (*)(PRTL_OSVERSIONINFOEXW)) GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))(&version);
	inWindows10 = version.dwMajorVersion >= 10;
	
	WNDCLASSW windowClass = {};
	windowClass.lpfnWndProc = WindowProcedure;
	windowClass.lpszClassName = L"host";
	RegisterClassW(&windowClass);
	
	EsMessage m = {};
	m.type = ES_MSG_INSTANCE_CREATE;
	m.createInstance.window = EsSyscall(ES_SYSCALL_WINDOW_CREATE, ES_WINDOW_NORMAL, 0, 0, 0);
	EsMessagePost(nullptr, &m);
	
	EsProcessStartupInformation startupInformation = {};
	startupInformation.applicationStartAddress = (uintptr_t) _ApplicationStart;
	_start(&startupInformation);
}
