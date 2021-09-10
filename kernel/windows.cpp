#ifndef IMPLEMENTATION

// TODO Don't send key released messages if the focused window has changed.
// TODO Blur clamping is incorrect with minimal repainting!
// TODO Cursor trails.

// Terminology:
// 	Dynamic resize - flicker-free resizing in container windows with an embedded window owned by a separate process.
// 	Direct update - paint first onto the video card's framebuffer, then onto the window manager's; used to reduce latency.
// 	Fast scroll - scrolling by shifting the bits in the window's surface, rather than repainting the entire area. TODO This is currently a bit broken.

struct EmbeddedWindow {
	void Destroy();
	void Close();
	void SetEmbedOwner(Process *process);

	Process *volatile owner;
	void *volatile apiWindow;
	volatile uint32_t handles;
	struct Window *container;
	EsObjectID id;
	uint32_t resizeClearColor;
	bool closed;
};

struct Window {
	void Update(EsRectangle *region, bool addToModifiedRegion);
	bool UpdateDirect(K_USER_BUFFER void *bits, uintptr_t stride, EsRectangle region);
	void Destroy(); 
	void Close();
	bool Move(EsRectangle newBounds, uint32_t flags);
	void SetEmbed(EmbeddedWindow *window);
	bool IsVisible();
	void ResizeEmbed(); // Send a message to the embedded window telling it to resize.

	// State:
	EsWindowStyle style;
	EsRectangle solidInsets, embedInsets;
	bool solid, noClickActivate, hidden, isMaximised, alwaysOnTop, hoveringOverEmbed, queuedScrollUpdate, activationClick, noBringToFront;
	volatile bool closed;
	EsMessage lastEmbedKeyboardMessage; // The most recent keyboard message sent to the embedded window.

	// Appearance:
	Surface surface;
	EsRectangle opaqueBounds, blurBounds;
	uint8_t alpha, material;
	uint32_t resizeClearColor;

	// Owner and children:
	Process *owner;
	void *apiWindow;
	EmbeddedWindow *embed;
	volatile uint64_t handles;
	EsObjectID id;

	// Location:
	EsPoint position;
	size_t width, height;
};

struct WindowManager {
	void *CreateWindow(Process *process, void *apiWindow, EsWindowStyle style);
	void *CreateEmbeddedWindow(Process *process, void *apiWindow);
	Window *FindWindowAtPosition(int cursorX, int cursorY, EsObjectID exclude = 0);

	void Initialise();

	void MoveCursor(int64_t xMovement, int64_t yMovement);
	void ClickCursor(unsigned buttons);
	void PressKey(unsigned scancode);

	void Redraw(EsPoint position, int width, int height, Window *except = nullptr, int startingAt = 0, bool addToModifiedRegion = true);

	bool ActivateWindow(Window *window); // Returns true if any menus were closed.
	void HideWindow(Window *window);
	Window *FindWindowToActivate(Window *excluding = nullptr);
	uintptr_t GetActivationZIndex();
	void ChangeWindowDepth(Window *window, bool alwaysRedraw, ptrdiff_t newZDepth);
	intptr_t FindWindowDepth(Window *window);
	bool CloseMenus(); // Returns true if any menus were closed.
	
	void StartEyedrop(uintptr_t object, Window *avoid, uint32_t cancelColor);
	void EndEyedrop(bool cancelled);

	bool initialised;

	// Windows:

	Array<Window *, K_FIXED> windows; // Sorted by z.
	Array<EmbeddedWindow *, K_FIXED> embeddedWindows;
	Window *pressedWindow, *activeWindow, *hoverWindow;
	KMutex mutex;
	KEvent windowsToCloseEvent;
	EsObjectID currentWindowID;
	size_t inspectorWindowCount;

	// Cursor:

	int32_t cursorX, cursorY;
	int32_t cursorXPrecise, cursorYPrecise; // Scaled up by a factor of K_CURSOR_MOVEMENT_SCALE.
	uint32_t lastButtons;

	Surface cursorSurface, cursorSwap, cursorTemporary;
	int cursorImageOffsetX, cursorImageOffsetY;
	uintptr_t cursorID;
	bool cursorXOR, cursorShadow;
	bool changedCursorImage;

	uint32_t cursorProperties; 

	// Keyboard:

	bool shift, alt, ctrl, flag;
	bool shift2, alt2, ctrl2, flag2;
	bool numlock;
	uint8_t modifiers;

	// Eyedropper:

	uintptr_t eyedropObject;
	bool eyedropping;
	Process *eyedropProcess;
	uint64_t eyedropAvoidID;
	uint32_t eyedropCancelColor;

	// Miscellaneous:

	EsRectangle workArea;
	bool inSingleCtrlPress;

	// Game controllers:

	KMutex gameControllersMutex;
	EsGameControllerState gameControllers[ES_GAME_CONTROLLER_MAX_COUNT];
	size_t gameControllerCount;
	EsObjectID gameControllerID;

	// Flicker-free resizing:

#define RESIZE_FLICKER_TIMEOUT_MS (40)
#define RESIZE_SLOW_THRESHOLD (RESIZE_FLICKER_TIMEOUT_MS * 3 / 4)
	Window *resizeWindow;
	bool resizeReceivedBitsFromContainer;
	bool resizeReceivedBitsFromEmbed;
	uint64_t resizeStartTimeStampMs;
	EsRectangle resizeQueuedRectangle;
	bool resizeQueued;
	bool resizeSlow; // Set if the previous resize went past RESIZE_FLICKER_SLOW_THRESHOLD; 
			 // when set, the old surface bits are copied on resize, so that if the resize times out the result will be reasonable.
};

WindowManager windowManager;

void SendMessageToWindow(Window *window, EsMessage *message);

#else

bool Window::IsVisible() {
	return !hidden && !closed && (id != windowManager.eyedropAvoidID || !windowManager.eyedropping);
}

void SendMessageToWindow(Window *window, EsMessage *message) {
	KMutexAssertLocked(&windowManager.mutex);

	if (window->closed) {
		return;
	}

	if (!window->owner->handles) {
		KernelPanic("SendMessageToWindow - (%x:%d/%x:%d) No handles.\n", window, window->handles, window->owner, window->owner->handles);
	}

	if (window->style != ES_WINDOW_CONTAINER || !window->embed) {
		window->owner->messageQueue.SendMessage(window->apiWindow, message);
		return;
	}

	if (message->type == ES_MSG_WINDOW_RESIZED) {
		message->windowResized.content = ES_RECT_2S(window->width, window->height);
		window->owner->messageQueue.SendMessage(window->apiWindow, message);
		message->windowResized.content = ES_RECT_2S(window->width - window->embedInsets.l - window->embedInsets.r, 
				window->height - window->embedInsets.t - window->embedInsets.b);
		window->embed->owner->messageQueue.SendMessage(window->embed->apiWindow, message);
	} else if (message->type == ES_MSG_WINDOW_DEACTIVATED || message->type == ES_MSG_WINDOW_ACTIVATED) {
		window->owner->messageQueue.SendMessage(window->apiWindow, message);
		window->embed->owner->messageQueue.SendMessage(window->embed->apiWindow, message);
	} else if (message->type == ES_MSG_MOUSE_MOVED) {
		EsRectangle embedRegion = ES_RECT_4(window->embedInsets.l, window->width - window->embedInsets.r, 
				window->embedInsets.t, window->height - window->embedInsets.b);
		bool inEmbed = windowManager.pressedWindow ? window->hoveringOverEmbed 
			: EsRectangleContains(embedRegion, message->mouseMoved.newPositionX, message->mouseMoved.newPositionY);

		if (inEmbed) {
			message->mouseMoved.newPositionX -= window->embedInsets.l;
			message->mouseMoved.newPositionY -= window->embedInsets.t;

			window->embed->owner->messageQueue.SendMessage(window->embed->apiWindow, message);

			if (!windowManager.pressedWindow && !window->hoveringOverEmbed) {
				message->type = ES_MSG_MOUSE_EXIT;
				window->owner->messageQueue.SendMessage(window->apiWindow, message);
			}
		} else {
			window->owner->messageQueue.SendMessage(window->apiWindow, message);

			if (!windowManager.pressedWindow && window->hoveringOverEmbed) {
				message->type = ES_MSG_MOUSE_EXIT;
				window->embed->owner->messageQueue.SendMessage(window->embed->apiWindow, message);
			}
		}

		if (!windowManager.pressedWindow) {
			window->hoveringOverEmbed = inEmbed;
		}
	} else if (message->type >= ES_MSG_MOUSE_LEFT_DOWN && message->type <= ES_MSG_MOUSE_MIDDLE_UP) {
		if (window->hoveringOverEmbed) {
			message->mouseDown.positionX -= window->embedInsets.l;
			message->mouseDown.positionY -= window->embedInsets.t;

			if (!window->activationClick) {
				window->embed->owner->messageQueue.SendMessage(window->embed->apiWindow, message);
			}
		} else {
			window->owner->messageQueue.SendMessage(window->apiWindow, message);
		}
	} else if (message->type == ES_MSG_KEY_DOWN || message->type == ES_MSG_KEY_UP) {
		// If the embedded window doesn't handle the key event,
		// then the container window can get it from here.
		// See DESKTOP_MSG_UNHANDLED_KEY_EVENT and ES_SYSCALL_WINDOW_GET_EMBED_KEYBOARD.
		EsMemoryCopy(&window->lastEmbedKeyboardMessage, message, sizeof(EsMessage));

		window->embed->owner->messageQueue.SendMessage(window->embed->apiWindow, message);
	} else if (message->type == ES_MSG_MOUSE_EXIT) {
		window->embed->owner->messageQueue.SendMessage(window->embed->apiWindow, message);
		window->owner->messageQueue.SendMessage(window->apiWindow, message);
	} else {
		window->owner->messageQueue.SendMessage(window->apiWindow, message);
	}
}

Window *WindowManager::FindWindowAtPosition(int cursorX, int cursorY, EsObjectID exclude) {
	KMutexAssertLocked(&mutex);

	for (intptr_t i = windows.Length() - 1; i >= 0; i--) {
		Window *window = windows[i];
		EsRectangle bounds = ES_RECT_4PD(window->position.x, window->position.y, window->width, window->height);

		if (window->solid && !window->hidden && exclude != window->id
				&& EsRectangleContains(EsRectangleAdd(bounds, window->solidInsets), cursorX, cursorY)
				&& (!window->isMaximised || EsRectangleContains(workArea, cursorX, cursorY))) {
			return window;
		}
	}

	return nullptr;
}

void WindowManager::EndEyedrop(bool cancelled) {
	KMutexAssertLocked(&mutex);

	eyedropping = false;

	EsMessage m = { ES_MSG_EYEDROP_REPORT };
	uint32_t color = *(uint32_t *) ((uint8_t *) graphics.frameBuffer.bits + 4 * cursorX + graphics.frameBuffer.stride * cursorY) | 0xFF000000;
	m.eyedrop.color = cancelled ? eyedropCancelColor : color;
	m.eyedrop.cancelled = cancelled;
	eyedropProcess->messageQueue.SendMessage((void *) eyedropObject, &m);
	CloseHandleToObject(eyedropProcess, KERNEL_OBJECT_PROCESS);
	eyedropProcess = nullptr;

	Redraw(ES_POINT(0, 0), graphics.width, graphics.height);
}

void WindowManager::PressKey(unsigned scancode) {
	if (!initialised) {
		return;
	}

	bool moveCursorNone = false;

	KMutexAcquire(&mutex);

	if (scancode == ES_SCANCODE_NUM_DIVIDE) {
		KernelPanic("WindowManager::PressKey - Panic key pressed.\n");
	}

	if (scancode == ES_SCANCODE_LEFT_CTRL) {
		inSingleCtrlPress = true;
	} else if (scancode == (ES_SCANCODE_LEFT_CTRL | K_SCANCODE_KEY_RELEASED) && inSingleCtrlPress) {
		EsMessage m;
		EsMemoryZero(&m, sizeof(EsMessage));
		m.type = ES_MSG_SINGLE_CTRL_PRESS;
		desktopProcess->messageQueue.SendMessage(nullptr, &m); 
	} else {
		inSingleCtrlPress = false;
	}

	if (eyedropping) {
		if (scancode == (ES_SCANCODE_ESCAPE | K_SCANCODE_KEY_RELEASED)) {
			EndEyedrop(true);
			moveCursorNone = true;
		}

		goto done;
	}

	// TODO Caps lock.

	if (scancode == ES_SCANCODE_LEFT_CTRL) ctrl = true;
	if (scancode == (ES_SCANCODE_LEFT_CTRL | K_SCANCODE_KEY_RELEASED)) ctrl = false;
	if (scancode == ES_SCANCODE_LEFT_SHIFT) shift = true;
	if (scancode == (ES_SCANCODE_LEFT_SHIFT | K_SCANCODE_KEY_RELEASED)) shift = false;
	if (scancode == ES_SCANCODE_LEFT_ALT) alt = true;
	if (scancode == (ES_SCANCODE_LEFT_ALT | K_SCANCODE_KEY_RELEASED)) alt = false;
	if (scancode == ES_SCANCODE_LEFT_FLAG) flag = true;
	if (scancode == (ES_SCANCODE_LEFT_FLAG | K_SCANCODE_KEY_RELEASED)) flag = false;

	if (scancode == ES_SCANCODE_RIGHT_CTRL) ctrl2 = true;
	if (scancode == (ES_SCANCODE_RIGHT_CTRL | K_SCANCODE_KEY_RELEASED)) ctrl2 = false;
	if (scancode == ES_SCANCODE_RIGHT_SHIFT) shift2 = true;
	if (scancode == (ES_SCANCODE_RIGHT_SHIFT | K_SCANCODE_KEY_RELEASED)) shift2 = false;
	if (scancode == ES_SCANCODE_RIGHT_ALT) alt2 = true;
	if (scancode == (ES_SCANCODE_RIGHT_ALT | K_SCANCODE_KEY_RELEASED)) alt2 = false;
	if (scancode == ES_SCANCODE_RIGHT_FLAG) flag2 = true;
	if (scancode == (ES_SCANCODE_RIGHT_FLAG | K_SCANCODE_KEY_RELEASED)) flag2 = false;

	modifiers = ((alt | alt2) ? ES_MODIFIER_ALT : 0) 
		| ((ctrl | ctrl2) ? ES_MODIFIER_CTRL : 0) 
		| ((shift | shift2) ? ES_MODIFIER_SHIFT : 0)
		| ((flag | flag2) ? ES_MODIFIER_FLAG : 0);

	if (activeWindow) {
		Window *window = activeWindow;

		EsMessage message;
		EsMemoryZero(&message, sizeof(EsMessage));
		message.type = (scancode & K_SCANCODE_KEY_RELEASED) ? ES_MSG_KEY_UP : ES_MSG_KEY_DOWN;
		message.keyboard.modifiers = modifiers;
		message.keyboard.scancode = scancode & ~K_SCANCODE_KEY_RELEASED;
		message.keyboard.numlock = numlock;

		static uint8_t heldKeys[512 / 8] = {};

		if (message.keyboard.scancode >= 512) {
			KernelPanic("WindowManager::PressKey - Scancode outside valid range.\n");
		}
		
		if (message.type == ES_MSG_KEY_DOWN && (heldKeys[message.keyboard.scancode / 8] & (1 << (message.keyboard.scancode % 8)))) {
			message.keyboard.repeat = true;
		}

		if (message.type == ES_MSG_KEY_DOWN) {
			heldKeys[message.keyboard.scancode / 8] |= (1 << (message.keyboard.scancode % 8));
		} else {
			heldKeys[message.keyboard.scancode / 8] &= ~(1 << (message.keyboard.scancode % 8));
		}

		SendMessageToWindow(window, &message);
	}

	done:;
	if (moveCursorNone) MoveCursor(0, 0);
	KMutexRelease(&mutex);
}

Window *WindowManager::FindWindowToActivate(Window *excluding) {
	for (uintptr_t i = windows.Length(); i > 0; i--) {
		if (!windows[i - 1]->hidden 
				&& windows[i - 1]->style == ES_WINDOW_CONTAINER 
				&& windows[i - 1] != excluding) {
			return windows[i - 1];
		}
	}

	return nullptr;
}

uintptr_t WindowManager::GetActivationZIndex() {
	for (uintptr_t i = windows.Length(); i > 0; i--) {
		if (!windows[i - 1]->alwaysOnTop) {
			return i;
		}
	}

	return 0;
}

void WindowManager::HideWindow(Window *window) {
	KMutexAssertLocked(&mutex);

	if (window->hidden) return;
	window->hidden = true;

	if (window == activeWindow) {
		ActivateWindow(FindWindowToActivate());
	} 

	Redraw(ES_POINT(window->position.x, window->position.y), window->width, window->height);
}

intptr_t WindowManager::FindWindowDepth(Window *window) {
	KMutexAssertLocked(&windowManager.mutex);

	for (uintptr_t i = 0; i < windows.Length(); i++) {
		if (windows[i] == window) {
			return i;
		}
	}

	return -1;
}

void WindowManager::ChangeWindowDepth(Window *window, bool redraw, ptrdiff_t _newZDepth) {
	KMutexAssertLocked(&windowManager.mutex);

	// Reorder the windows in the array, and update the depth buffer.
	intptr_t oldZDepth = FindWindowDepth(window);
	if (oldZDepth == -1) KernelPanic("WindowManager::ChangeWindowDepth - Window %x was not in array.\n", window);
	windows.Delete(oldZDepth);
	intptr_t newZDepth = _newZDepth != -1 ? _newZDepth : GetActivationZIndex();
	windows.Insert(window, newZDepth);

	if (oldZDepth != newZDepth || redraw) {
		// Redraw the modified area of the screen.
		windowManager.Redraw(ES_POINT(window->position.x, window->position.y), window->width, window->height);
	}
}

bool WindowManager::CloseMenus() {
	KMutexAssertLocked(&mutex);

	EsMessage message;
	EsMemoryZero(&message, sizeof(EsMessage));

	bool result = false;

	for (uintptr_t i = 0; i < windows.Length(); i++) {
		// Close any open menus.

		if (windows[i]->style == ES_WINDOW_MENU) {
			message.type = ES_MSG_WINDOW_DEACTIVATED;
			SendMessageToWindow(windows[i], &message);
			result = true;
		}
	}

	return result;
}

bool WindowManager::ActivateWindow(Window *window) {
	KMutexAssertLocked(&mutex);

	if (window && (window->style == ES_WINDOW_TIP || window->style == ES_WINDOW_MENU || window->closed)) {
		return false;
	}

	Window *oldWindow = activeWindow;

	EsMessage message;
	EsMemoryZero(&message, sizeof(EsMessage));

	bool result = CloseMenus();

	// Set the active window, unless it hasn't changed.

	if (activeWindow == window && (!window || !window->hidden)) {
		// Bring the window to the front anyway.
		if (window && !window->noBringToFront) ChangeWindowDepth(window, false, -1);
		return result;
	}

	activeWindow = window;
	if (window) window->hidden = false;

	if (window) {
		// Bring the window to the front.
		if (!window->noBringToFront) ChangeWindowDepth(window, true, -1);

		// Activate the new window.
		message.type = ES_MSG_WINDOW_ACTIVATED;
		SendMessageToWindow(window, &message);
	} else {
		// No window is active.
	}

	if (oldWindow && oldWindow != window) {
		// Deactivate the old window.

		if (oldWindow != FindWindowAtPosition(cursorX, cursorY)) {
			message.type = ES_MSG_MOUSE_EXIT;
			SendMessageToWindow(oldWindow, &message);
		}

		message.type = ES_MSG_WINDOW_DEACTIVATED;
		SendMessageToWindow(oldWindow, &message);
	}

	return result;
}

void WindowManager::ClickCursor(unsigned buttons) {
	KMutexAcquire(&mutex);

	unsigned delta = lastButtons ^ buttons;
	lastButtons = buttons;

	bool moveCursorNone = false;

	if (eyedropping && delta) {
		if ((delta & K_LEFT_BUTTON) && (~buttons & K_LEFT_BUTTON)) {
			EndEyedrop(false);
			moveCursorNone = true;
		}
	} else if (delta) {
		// Send a mouse pressed message to the window the cursor is over.
		Window *window = FindWindowAtPosition(cursorX, cursorY);

		bool activationClick = false, activationClickFromMenu = false;

		if (buttons == delta) {
			if (activeWindow != window) {
				activationClick = true;
			}

			if (window && window->noClickActivate) {
				if (!window->noBringToFront) {
					ChangeWindowDepth(window, false, -1);
				}

				if (window->style != ES_WINDOW_MENU) {
					activationClickFromMenu = CloseMenus();
				}
			} else {
				activationClickFromMenu = ActivateWindow(window);
			}
		}

		{
			EsMessage message;
			EsMemoryZero(&message, sizeof(EsMessage));

			if (delta & K_LEFT_BUTTON) message.type = (buttons & K_LEFT_BUTTON) ? ES_MSG_MOUSE_LEFT_DOWN : ES_MSG_MOUSE_LEFT_UP;
			if (delta & K_RIGHT_BUTTON) message.type = (buttons & K_RIGHT_BUTTON) ? ES_MSG_MOUSE_RIGHT_DOWN : ES_MSG_MOUSE_RIGHT_UP;
			if (delta & K_MIDDLE_BUTTON) message.type = (buttons & K_MIDDLE_BUTTON) ? ES_MSG_MOUSE_MIDDLE_DOWN : ES_MSG_MOUSE_MIDDLE_UP;

			// TODO Setting pressedWindow if holding with other mouse buttons.

			if (message.type == ES_MSG_MOUSE_LEFT_DOWN) {
				pressedWindow = window;
			} else if (message.type == ES_MSG_MOUSE_LEFT_UP) {
				if (pressedWindow) {
					// Always send the messages to the pressed window, if there is one.
					window = pressedWindow;
				}

				pressedWindow = nullptr;
				moveCursorNone = true; // We might have moved outside the window.
			}
			
			if (window) {
				message.mouseDown.positionX = cursorX - window->position.x;
				message.mouseDown.positionY = cursorY - window->position.y;
				window->activationClick = activationClick || activationClickFromMenu;
				SendMessageToWindow(window, &message);
			}
		}
	}

	if (moveCursorNone) {
		MoveCursor(0, 0);
	} else {
		GraphicsUpdateScreen();
	}

	KMutexRelease(&mutex);
}

void WindowManager::MoveCursor(int64_t xMovement, int64_t yMovement) {
	KMutexAssertLocked(&mutex);

	if ((xMovement * xMovement + yMovement * yMovement > 50 * K_CURSOR_MOVEMENT_SCALE * K_CURSOR_MOVEMENT_SCALE) && (cursorProperties & CURSOR_USE_ACCELERATION)) {
		// Apply cursor acceleration.
		xMovement *= 2;
		yMovement *= 2;
	}

	// Apply cursor speed. Introduces another factor of K_CURSOR_MOVEMENT_SCALE.
	xMovement *= CURSOR_SPEED(cursorProperties);
	yMovement *= CURSOR_SPEED(cursorProperties);

	if (alt && (cursorProperties & CURSOR_USE_ALT_SLOW)) {
		// Apply cursor slowdown.
		xMovement /= 5, yMovement /= 5;
	}

	// Update cursor position.
	cursorXPrecise = ClampInteger(0, graphics.width * K_CURSOR_MOVEMENT_SCALE - 1, cursorXPrecise + xMovement / K_CURSOR_MOVEMENT_SCALE);
	cursorYPrecise = ClampInteger(0, graphics.height * K_CURSOR_MOVEMENT_SCALE - 1, cursorYPrecise + yMovement / K_CURSOR_MOVEMENT_SCALE);
	cursorX = cursorXPrecise / K_CURSOR_MOVEMENT_SCALE;
	cursorY = cursorYPrecise / K_CURSOR_MOVEMENT_SCALE;

	if (eyedropping) {
		EsMessage m = { ES_MSG_EYEDROP_REPORT };
		uint32_t color = *(uint32_t *) ((uint8_t *) graphics.frameBuffer.bits + 4 * cursorX + graphics.frameBuffer.stride * cursorY);
		m.eyedrop.color = color;
		m.eyedrop.cancelled = false;
		eyedropProcess->messageQueue.SendMessage((void *) eyedropObject, &m);
	} else {
		Window *window = pressedWindow;

		if (!window) {
			// Work out which window the mouse is now over.
			window = FindWindowAtPosition(cursorX, cursorY);

			if (hoverWindow != window && hoverWindow) {
				EsMessage message;
				EsMemoryZero(&message, sizeof(EsMessage));
				message.type = ES_MSG_MOUSE_EXIT;
				SendMessageToWindow(hoverWindow, &message);
			}

			hoverWindow = window;
		}

		if (window) {
			EsMessage message;
			EsMemoryZero(&message, sizeof(EsMessage));
			message.type = ES_MSG_MOUSE_MOVED;
			message.mouseMoved.newPositionX = cursorX - window->position.x;
			message.mouseMoved.newPositionY = cursorY - window->position.y;
			SendMessageToWindow(window, &message);
		}
	}

	GraphicsUpdateScreen();
}

void _CloseWindows(uintptr_t) {
	while (true) {
		KEventWait(&windowManager.windowsToCloseEvent);
		KMutexAcquire(&windowManager.mutex);

		for (uintptr_t i = 0; i < windowManager.windows.Length(); i++) {
			Window *window = windowManager.windows[i];

			if (window->handles > 1) {
				continue;
			}

			// Remove the window from the array.
			windowManager.windows.Delete(i);

			if (!window->closed) {
				// Only the window manager's handle to the window remains, 
				// but the window has not been closed.
				// This probably means the process crashed before it could close its windows;
				// we should close the window ourselves.
				window->Close();
			}

			// Close the window manager's handle to the window.
			CloseHandleToObject(window, KERNEL_OBJECT_WINDOW);

			i--;
		}

		for (uintptr_t i = 0; i < windowManager.embeddedWindows.Length(); i++) {
			// Apply a similar set of operations to embedded windows.
			EmbeddedWindow *window = windowManager.embeddedWindows[i];
			if (window->handles > 1) continue;
			if (!window->closed) window->Close();
			windowManager.embeddedWindows.Delete(i);
			CloseHandleToObject(window, KERNEL_OBJECT_EMBEDDED_WINDOW);
			i--;
		}

		KMutexRelease(&windowManager.mutex);
	}
}

void WindowManager::Initialise() {
	windowsToCloseEvent.autoReset = true;
	cursorProperties = K_CURSOR_MOVEMENT_SCALE << 16;
	KThreadCreate("CloseWindows", _CloseWindows);
	KMutexAcquire(&mutex);
	MoveCursor(graphics.width / 2 * K_CURSOR_MOVEMENT_SCALE, graphics.height / 2 * K_CURSOR_MOVEMENT_SCALE);
	GraphicsUpdateScreen();
	initialised = true;
	KMutexRelease(&mutex);
}

void *WindowManager::CreateEmbeddedWindow(Process *process, void *apiWindow) {
	EmbeddedWindow *window = (EmbeddedWindow *) EsHeapAllocate(sizeof(EmbeddedWindow), true, K_PAGED);
	if (!window) return nullptr;

	if (!embeddedWindows.Add(window)) {
		EsHeapFree(window, sizeof(EmbeddedWindow), K_PAGED);
		return nullptr;
	}

	window->apiWindow = apiWindow;
	window->owner = process;
	window->handles = 2;  // One handle for the window manager, and one handle for the calling process.

	KMutexAcquire(&mutex);
	window->id = ++currentWindowID;
	KMutexRelease(&mutex);

	OpenHandleToObject(window->owner, KERNEL_OBJECT_PROCESS);

	return window;
}

void *WindowManager::CreateWindow(Process *process, void *apiWindow, EsWindowStyle style) {
	KMutexAcquire(&mutex);
	EsDefer(KMutexRelease(&mutex));

	// Allocate and initialise the window object.

	Window *window = (Window *) EsHeapAllocate(sizeof(Window), true, K_PAGED);
	if (!window) return nullptr;

	window->style = style;
	window->apiWindow = apiWindow;
	window->alpha = 0xFF;
	window->position = ES_POINT(-8, -8);
	window->owner = process;
	window->handles = 2; // One handle for the window manager, and one handle for the calling process.
	window->hidden = true;
	window->id = ++currentWindowID;

	if (style == ES_WINDOW_INSPECTOR) windowManager.inspectorWindowCount++;

	// Insert the window into the window array.

	uintptr_t insertionPoint = GetActivationZIndex();

	if (!windows.Insert(window, insertionPoint)) {
		EsHeapFree(window->surface.bits, 0, K_PAGED);
		EsHeapFree(window, sizeof(Window), K_PAGED);
		return nullptr;
	}

	// Get a handle to the owner process.

	OpenHandleToObject(window->owner, KERNEL_OBJECT_PROCESS);

	MoveCursor(0, 0);
	return window;
}

bool Window::Move(EsRectangle rectangle, uint32_t flags) {
	KMutexAssertLocked(&windowManager.mutex);

	if (closed) {
		return false;
	}

	if ((flags & ES_WINDOW_MOVE_DYNAMIC) 
			&& (isMaximised == !!(flags & ES_WINDOW_MOVE_MAXIMISED) /* cannot queue resize if changing isMaximised */)
			&& windowManager.resizeWindow == this
			&& windowManager.resizeStartTimeStampMs + RESIZE_FLICKER_TIMEOUT_MS > KGetTimeInMs()) {
		windowManager.resizeQueued = true;
		windowManager.resizeQueuedRectangle = rectangle;
		return false;
	}

	bool result = true;

	isMaximised = flags & ES_WINDOW_MOVE_MAXIMISED;
	alwaysOnTop = flags & ES_WINDOW_MOVE_ALWAYS_ON_TOP;

	// TS("Move window\n");

	if (flags & ES_WINDOW_MOVE_ADJUST_TO_FIT_SCREEN) {
		if (rectangle.r > (int32_t) graphics.frameBuffer.width) {
			rectangle.l -= rectangle.r - graphics.frameBuffer.width;
			rectangle.r -= rectangle.r - graphics.frameBuffer.width;
		}

		if (rectangle.b > (int32_t) graphics.frameBuffer.height) {
			rectangle.t -= rectangle.b - graphics.frameBuffer.height;
			rectangle.b -= rectangle.b - graphics.frameBuffer.height;
		}

		if (rectangle.l < 0) {
			rectangle.r -= rectangle.l - 0;
			rectangle.l = 0;
		}

		if (rectangle.t < 0) {
			rectangle.b -= rectangle.t - 0;
			rectangle.t = 0;
		}
	}

	size_t newWidth = rectangle.r - rectangle.l;
	size_t newHeight = rectangle.b - rectangle.t;

	if (newWidth < 4 || newHeight < 4 
			|| rectangle.l > rectangle.r 
			|| rectangle.t > rectangle.b
			|| newWidth > graphics.width * 2
			|| newHeight > graphics.height * 2) {
		return false;
	}

	if (!hidden) {
		// TS("Clear previous image\n");
		windowManager.Redraw(ES_POINT(position.x, position.y), width, height, this);
	}

	hidden = false;
	position = ES_POINT(rectangle.l, rectangle.t);
	bool changedSize = width != newWidth || height != newHeight;
	width = newWidth, height = newHeight;

	if (changedSize) {
		if (style == ES_WINDOW_CONTAINER && embed) {
			surface.Resize(width, height, 0, windowManager.resizeSlow);
		} else {
			surface.Resize(width, height);
		}

		EsMessage message;
		EsMemoryZero(&message, sizeof(EsMessage));
		message.type = ES_MSG_WINDOW_RESIZED;
		message.windowResized.content = ES_RECT_4(0, width, 0, height);
		SendMessageToWindow(this, &message);

		if (flags & ES_WINDOW_MOVE_DYNAMIC) {
			// Don't redraw the screen until both the container and embedded window have painted
			// (or the timeout completes).
			windowManager.resizeWindow = this;
			windowManager.resizeReceivedBitsFromContainer = false;
			windowManager.resizeReceivedBitsFromEmbed = embed == nullptr;
			windowManager.resizeStartTimeStampMs = KGetTimeInMs();
		}
	}

	if (flags & ES_WINDOW_MOVE_AT_BOTTOM) {
		windowManager.ChangeWindowDepth(this, false, 0);
	}

	if ((flags & ES_WINDOW_MOVE_DYNAMIC) && changedSize && style == ES_WINDOW_CONTAINER && !windowManager.resizeSlow) {
		// Don't redraw anything yet.
	} else {
		windowManager.Redraw(position, width, height, nullptr);
	}

	return result;
}

void EmbeddedWindow::Destroy() {
	KernelLog(LOG_VERBOSE, "Window Manager", "destroy embedded window", "EmbeddedWindow::Destroy - Destroying embedded window.\n");
	EsHeapFree(this, sizeof(EmbeddedWindow), K_PAGED);
}

void EmbeddedWindow::Close() {
	KMutexAssertLocked(&windowManager.mutex);

	EsMessage message;
	EsMemoryZero(&message, sizeof(EsMessage));
	message.type = ES_MSG_WINDOW_DESTROYED;
	owner->messageQueue.SendMessage(apiWindow, &message); 
	SetEmbedOwner(nullptr);

	if (!scheduler.shutdown) {
		message.type = ES_MSG_EMBEDDED_WINDOW_DESTROYED;
		message.desktop.windowID = id;
		desktopProcess->messageQueue.SendMessage(nullptr, &message); 
	}

	if (container && container->embed == this) {
		container->SetEmbed(nullptr);
	}

	closed = true;
}

void Window::Destroy() {
	if (!closed) {
		KernelPanic("Window::Destroy - Window %x has not been closed.\n", this);
	}

	EsHeapFree(this, sizeof(Window), K_PAGED);
}

void Window::Close() {
	KMutexAssertLocked(&windowManager.mutex);
	
	SetEmbed(nullptr);

	// Send the destroy message - the last message sent to the window.
	EsMessage message;
	EsMemoryZero(&message, sizeof(EsMessage));
	message.type = ES_MSG_WINDOW_DESTROYED;
	owner->messageQueue.SendMessage(apiWindow, &message); 

	Process *_owner = owner;
	owner = nullptr;
	CloseHandleToObject(_owner, KERNEL_OBJECT_PROCESS);

	hidden = true;
	solid = false;

	bool findActiveWindow = false;

	if (windowManager.pressedWindow == this) windowManager.pressedWindow = nullptr;
	if (windowManager.hoverWindow   == this) windowManager.hoverWindow   = nullptr; 
	if (windowManager.resizeWindow  == this) windowManager.resizeWindow  = nullptr;
	if (windowManager.activeWindow  == this) windowManager.activeWindow  = nullptr, findActiveWindow = true;

	if (style == ES_WINDOW_INSPECTOR) windowManager.inspectorWindowCount--;

	windowManager.Redraw(ES_POINT(position.x, position.y), width, height);

	if (findActiveWindow) {
		windowManager.ActivateWindow(windowManager.FindWindowToActivate());
	}

	windowManager.MoveCursor(0, 0);
	GraphicsUpdateScreen();

	__sync_fetch_and_sub(&graphics.totalSurfaceBytes, surface.width * surface.height * 4);
	EsHeapFree(surface.bits, 0, K_PAGED);
	surface.bits = nullptr;

	__sync_synchronize();
	closed = true;
}

void WindowManager::Redraw(EsPoint position, int width, int height, Window *except, int startingAt, bool addToModifiedRegion) {
	// TS("Window manager redraw at [%d, %d] with size [%d, %d]\n", position.x, position.y, width, height);

	KMutexAssertLocked(&mutex);

	if (!width || !height) return;
	if (scheduler.shutdown) return;

	for (int index = startingAt; index < (int) windows.Length(); index++) {
		Window *window = windows[index];

		if (!window->IsVisible() || window == except) {
			continue;
		}

		if (position.x >= window->position.x + window->opaqueBounds.l
				&& position.x + width <= window->position.x + window->opaqueBounds.r
				&& position.y >= window->position.y + window->opaqueBounds.t
				&& position.y + height <= window->position.y + window->opaqueBounds.b) {
			startingAt = index;
		}
	}

	for (int index = startingAt; index < (int) windows.Length(); index++) {
		Window *window = windows[index];

		if (!window->IsVisible() || window == except) continue;

		EsRectangle rectangle = ES_RECT_4PD(window->position.x, window->position.y, window->width, window->height);
		rectangle = EsRectangleIntersection(rectangle, ES_RECT_2S(graphics.frameBuffer.width, graphics.frameBuffer.height));
		rectangle = EsRectangleIntersection(rectangle, ES_RECT_4PD(position.x, position.y, width, height));
		if (window->isMaximised) rectangle = EsRectangleIntersection(rectangle, workArea);
		rectangle = Translate(rectangle, -window->position.x, -window->position.y);

		if (!ES_RECT_VALID(rectangle)) continue;

		EsPoint point = ES_POINT(window->position.x + rectangle.l, window->position.y + rectangle.t);
		Surface *surface = &window->surface;

		if (window->opaqueBounds.l <= rectangle.l && window->opaqueBounds.r >= rectangle.r 
				&& window->opaqueBounds.t <= rectangle.t && window->opaqueBounds.b >= rectangle.b) {
			graphics.frameBuffer.Copy(surface, point, rectangle, addToModifiedRegion);
		} else {
			EsRectangle blurRegion = Translate(EsRectangleIntersection(window->blurBounds, rectangle), window->position.x, window->position.y);
			graphics.frameBuffer.BlendWindow(surface, point, rectangle, window->material, window->alpha, blurRegion);
		}
	}
}

bool Window::UpdateDirect(K_USER_BUFFER void *bits, uintptr_t stride, EsRectangle region) {
	KMutexAssertLocked(&windowManager.mutex);

	intptr_t z = windowManager.FindWindowDepth(this);

	if (z == -1) {
		return false;
	}

	if (hidden) {
		return false;
	}

	if (windowManager.changedCursorImage) {
		// The entire cursor needs to be redrawn about its image changes,
		// which might not happen with a direct update.
		return false;
	}

	if (region.l + position.x < 0) {
		bits = (K_USER_BUFFER uint8_t *) bits + 4 * (-position.x - region.l);
		region.l = -position.x;
	}

	if (region.t + position.y < 0) {
		bits = (K_USER_BUFFER uint8_t *) bits + stride * (-position.y - region.t);
		region.t = -position.y;
	}

	if ((uint32_t) (region.r + position.x) > graphics.width) {
		region.r = graphics.width - position.x;
	}

	if ((uint32_t) (region.b + position.y) > graphics.height) {
		region.b = graphics.height - position.y;
	}

	// If the update region is completely obscured, then we don't need to update.

	for (uintptr_t i = z + 1; i < windowManager.windows.Length(); i++) {
		Window *other = windowManager.windows[i];
		EsRectangle otherBounds = Translate(other->opaqueBounds, other->position.x, other->position.y);

		if (region.l + position.x > otherBounds.l && region.r + position.x < otherBounds.r 
				&& region.t + position.y > otherBounds.t && region.b + position.y < otherBounds.b
				&& other->alpha == 0xFF && other->IsVisible()) {
			return true;
		} 
	}

	// If the update region isn't opaque, we cannot do a direct update.

	if (region.l < opaqueBounds.l || region.r > opaqueBounds.r || region.t < opaqueBounds.t || region.b > opaqueBounds.b
			|| alpha != 0xFF) {
		return false;
	}

	// If any window overlaps the update region, we cannot do a direct update.

	EsRectangle thisBounds = ES_RECT_4(position.x, position.x + width, position.y, position.y + height);

	for (uintptr_t i = z + 1; i < windowManager.windows.Length(); i++) {
		Window *other = windowManager.windows[i];
		EsRectangle otherBounds = ES_RECT_4(other->position.x, other->position.x + other->width,
				other->position.y, other->position.y + other->height);

		if (EsRectangleClip(thisBounds, otherBounds, nullptr)) {
			return false;
		} 
	}

	region = Translate(region, position.x, position.y);

	// Write the updated bits directly to the frame buffer!
	GraphicsUpdateScreen(bits, &region, stride);
	return true;
}

void Window::Update(EsRectangle *_region, bool addToModifiedRegion) {
	KMutexAssertLocked(&windowManager.mutex);

	intptr_t z = windowManager.FindWindowDepth(this);

	if (z == -1) {
		return;
	}

	// TS("Update window %x within %R\n", this, _region ? *_region : ES_RECT_1(0));

	EsRectangle region = _region ? *_region : ES_RECT_4(0, width, 0, height);

	// Is this updated region completely obscured?
	for (uintptr_t i = z + 1; i < windowManager.windows.Length(); i++) {
		Window *other = windowManager.windows[i];
		EsRectangle otherBounds = Translate(other->opaqueBounds, other->position.x, other->position.y);

		if (region.l + position.x > otherBounds.l && region.r + position.x < otherBounds.r 
				&& region.t + position.y > otherBounds.t && region.b + position.y < otherBounds.b
				&& other->alpha == 0xFF && other->IsVisible()) {
			return;
		} 
	}

	EsRectangle r;
	// EsPrint("Update window width region %d/%d/%d/%d\n", region.left, region.right, region.top, region.bottom);

	if (alpha == 0xFF) {
		EsRectangle translucentBorder = opaqueBounds;

		// Draw the opaque region, then everything above it.
		EsRectangleClip(region, ES_RECT_4(translucentBorder.l, translucentBorder.r, translucentBorder.t, translucentBorder.b), &r);
		windowManager.Redraw(ES_POINT(position.x + r.l, position.y + r.t), r.r - r.l, r.b - r.t, nullptr, z, addToModifiedRegion);

		// Draw the transparent regions after everything below them, then everything above them.
		EsRectangleClip(region, ES_RECT_4(0, translucentBorder.l, 0, height), &r);
		windowManager.Redraw(ES_POINT(position.x + r.l, position.y + r.t), r.r - r.l, r.b - r.t, nullptr, 0, addToModifiedRegion);
		EsRectangleClip(region, ES_RECT_4(translucentBorder.r, width, 0, height), &r);
		windowManager.Redraw(ES_POINT(position.x + r.l, position.y + r.t), r.r - r.l, r.b - r.t, nullptr, 0, addToModifiedRegion);
		EsRectangleClip(region, ES_RECT_4(translucentBorder.l, translucentBorder.r, 0, translucentBorder.t), &r);
		windowManager.Redraw(ES_POINT(position.x + r.l, position.y + r.t), r.r - r.l, r.b - r.t, nullptr, 0, addToModifiedRegion);
		EsRectangleClip(region, ES_RECT_4(translucentBorder.l, translucentBorder.r, translucentBorder.b, height), &r);
		windowManager.Redraw(ES_POINT(position.x + r.l, position.y + r.t), r.r - r.l, r.b - r.t, nullptr, 0, addToModifiedRegion);
	} else {
		// The whole window is translucent; draw it.
		EsRectangleClip(region, ES_RECT_4(0, width, 0, height), &r);
		windowManager.Redraw(ES_POINT(position.x + r.l, position.y + r.t), r.r - r.l, r.b - r.t, nullptr, 0, addToModifiedRegion);
	}
}

void EmbeddedWindow::SetEmbedOwner(Process *process) {
	KMutexAssertLocked(&windowManager.mutex);

	apiWindow = nullptr;

	if (process) {
		OpenHandleToObject(process, KERNEL_OBJECT_PROCESS);
	}

	if (owner) {
		CloseHandleToObject(owner, KERNEL_OBJECT_PROCESS);
	}

	owner = process;
}

void Window::ResizeEmbed() {
	KMutexAssertLocked(&windowManager.mutex);
	if (!embed || !embed->apiWindow) return;
	EsMessage message;
	EsMemoryZero(&message, sizeof(EsMessage));
	message.type = ES_MSG_WINDOW_RESIZED;
	message.windowResized.content = ES_RECT_2S(width - embedInsets.l - embedInsets.r, height - embedInsets.t - embedInsets.b);
	embed->owner->messageQueue.SendMessage(embed->apiWindow, &message);
}

void Window::SetEmbed(EmbeddedWindow *newEmbed) {
	KMutexAssertLocked(&windowManager.mutex);

	if (newEmbed && (newEmbed->container || newEmbed->closed)) {
		return;
	}

	if (newEmbed == embed) {
		return;
	}

	if (embed) {
		if (embed->closed) {
			KernelPanic("Window::SetEmbed - Previous embed %x was closed.\n");
		}

		embed->container = nullptr;

		EsMessage message;
		EsMemoryZero(&message, sizeof(EsMessage));
		message.type = ES_MSG_WINDOW_RESIZED;
		message.windowResized.content = ES_RECT_4(0, 0, 0, 0);
		message.windowResized.hidden = true;

		if (embed->owner) {
			embed->owner->messageQueue.SendMessage(embed->apiWindow, &message);
		}
	}

	embed = newEmbed;

	if (embed) {
		embed->container = this;
		ResizeEmbed();

		EsMessage message;
		EsMemoryZero(&message, sizeof(message));
		message.type = windowManager.activeWindow == this ? ES_MSG_WINDOW_ACTIVATED : ES_MSG_WINDOW_DEACTIVATED;
		embed->owner->messageQueue.SendMessage(embed->apiWindow, &message);
	}
}

void WindowManager::StartEyedrop(uintptr_t object, Window *avoid, uint32_t cancelColor) {
	KMutexAcquire(&mutex);

	if (!eyedropping) {
		eyedropObject = object;
		eyedropping = true;
		eyedropAvoidID = avoid->id;
		eyedropCancelColor = cancelColor;
		Redraw(avoid->position, avoid->width, avoid->height);

		if (hoverWindow) {
			EsMessage message;
			EsMemoryZero(&message, sizeof(EsMessage));
			message.type = ES_MSG_MOUSE_EXIT;
			SendMessageToWindow(hoverWindow, &message);
		}

		hoverWindow = pressedWindow = nullptr;

		eyedropProcess = GetCurrentThread()->process;
		OpenHandleToObject(eyedropProcess, KERNEL_OBJECT_PROCESS);
	}

	GraphicsUpdateScreen();
	KMutexRelease(&mutex);
}

void KCursorUpdate(int xMovement, int yMovement, unsigned buttons) {
	if (!windowManager.initialised) {
		return;
	}

	if (xMovement || yMovement) {
		if (xMovement * xMovement + yMovement * yMovement < 10 && buttons != windowManager.lastButtons) {
			// This seems to be movement noise generated when the buttons were pressed/released.
		} else {
			KMutexAcquire(&windowManager.mutex);
			windowManager.MoveCursor(xMovement, yMovement);
			KMutexRelease(&windowManager.mutex);
		}
	} 

	windowManager.ClickCursor(buttons);
}

void KKeyPress(unsigned scancode) {
	windowManager.PressKey(scancode);
}

void KKeyboardUpdate(uint16_t *keysDown, size_t keysDownCount) {
	// TODO Key repeat.

	static uint16_t previousKeysDown[32] = {};
	static size_t previousKeysDownCount = 0;
	static KMutex mutex = {};

	KMutexAcquire(&mutex);
	EsDefer(KMutexRelease(&mutex));

	if (keysDownCount > 32) {
		keysDownCount = 32;
	}

	for (uintptr_t i = 0; i < keysDownCount; i++) {
		bool found = false;

		for (uintptr_t j = 0; j < previousKeysDownCount; j++) {
			if (keysDown[i] == previousKeysDown[j]) {
				found = true;
				break;
			}
		}

		if (!found && keysDown[i]) {
			if (keysDown[i] == ES_SCANCODE_PAUSE) {
				KDebugKeyPressed(); // TODO Doesn't work if scheduler not functioning correctly.
			}

			KKeyPress(keysDown[i] | K_SCANCODE_KEY_PRESSED);
		}
	}

	for (uintptr_t i = 0; i < previousKeysDownCount; i++) {
		bool found = false;

		for (uintptr_t j = 0; j < keysDownCount; j++) {
			if (keysDown[j] == previousKeysDown[i]) {
				found = true;
				break;
			}
		}

		if (!found) {
			KKeyPress(previousKeysDown[i] | K_SCANCODE_KEY_RELEASED);
		}
	}

	previousKeysDownCount = keysDownCount;
	EsMemoryCopy(previousKeysDown, keysDown, sizeof(uint16_t) * keysDownCount);
}

uint64_t KGameControllerConnect() {
	KMutexAcquire(&windowManager.gameControllersMutex);

	EsObjectID id = ++windowManager.gameControllerID;

	if (windowManager.gameControllerCount != ES_GAME_CONTROLLER_MAX_COUNT) {
		windowManager.gameControllers[windowManager.gameControllerCount++].id = id;
	} else {
		id = 0;
	}

	KMutexRelease(&windowManager.gameControllersMutex);

	return id;
}

void KGameControllerDisconnect(uint64_t id) {
	KMutexAcquire(&windowManager.gameControllersMutex);
	
	for (uintptr_t i = 0; i < windowManager.gameControllerCount; i++) {
		if (windowManager.gameControllers[i].id == id) {
			EsMemoryMove(windowManager.gameControllers + i + 1, 
					windowManager.gameControllers + windowManager.gameControllerCount, 
					-sizeof(EsGameControllerState), false);
			windowManager.gameControllerCount--;
			break;
		}
	}

	KMutexRelease(&windowManager.gameControllersMutex);
}

void KGameControllerUpdateState(EsGameControllerState *state) {
	KMutexAcquire(&windowManager.gameControllersMutex);
	
	for (uintptr_t i = 0; i < windowManager.gameControllerCount; i++) {
		if (windowManager.gameControllers[i].id == state->id) {
			windowManager.gameControllers[i] = *state;
#if 0
			EsPrint("game controller %d: buttons %x analog %d %d %d %d %d %d dpad %d\n",
				state->id, state->buttons, state->analog[0].x, state->analog[0].y, state->analog[0].z, 
				state->analog[1].x, state->analog[1].y, state->analog[1].z, state->directionalPad);
#endif
			break;
		}
	}

	KMutexRelease(&windowManager.gameControllersMutex);
}

#endif
