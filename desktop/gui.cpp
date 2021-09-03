// TODO Styling features:
// 	- Specifying aspect ratio of element.
// 	- Animation parts (list of keyframes).
// 	- Ripple animations.
// 	- Exiting animations.
// 	- Morph from/to entrance/exit animations.
// TODO Close menus within menus (bug).
// TODO Keyboard navigation - menus; escape to restore default focus.
// TODO Middle click panning.
// TODO Scrollbar middle click and zooming; scroll wheel.
// TODO Textboxes: date/time overlays, keyboard shortcut overlay, custom overlays.
// TODO Breadcrumb bar overflow menu; keep hover after recreating UI.
// TODO Textbox embedded objects.
// TODO Closing windows in menu/access key mode.
// TODO Ignore ES_MSG_LAYOUT in panels if layout.sizeChanged is false?

// Behaviour of activation clicks. --> Only ignore activation clicks from menus.
// Behaviour of the scroll wheel with regards to focused/hovered elements --> Scroll the hovered element only.

#define WINDOW_INSET 			((int) api.systemConstants[ES_SYSTEM_CONSTANT_WINDOW_INSET])
#define CONTAINER_TAB_BAND_HEIGHT 	((int) api.systemConstants[ES_SYSTEM_CONSTANT_CONTAINER_TAB_BAND_HEIGHT])
#define BORDER_THICKNESS 		((int) api.systemConstants[ES_SYSTEM_CONSTANT_BORDER_THICKNESS])

// #define TRACE_LAYOUT

struct AccessKeyEntry {
	char character;
	int number;
	EsElement *element;
	EsRectangle bounds;
};

struct {
	// Animations.
	EsTimer animationSleepTimer;
	bool animationSleep;
	Array<EsElement *> animatingElements;

	// Input.
	bool draggingStarted, mouseButtonDown;
	uint8_t leftModifiers, rightModifiers;
	int lastClickX, lastClickY, lastClickButton, resizeType;

	// Menus.
	bool menuMode;

	// Access keys.
	bool accessKeyMode, unhandledAltPress;

	struct {
		Array<AccessKeyEntry> entries;
		int numbers[26];
		struct UIStyle *hintStyle;
		EsWindow *window;
		char typedCharacter;
	} accessKeys;

	// Misc.
	Array<EsWindow *> allWindows;
	HashTable keyboardShortcutNames;
	EsCursorStyle resizeCursor;
	bool resizing;
	EsElement *insertAfter;

	// Click chains.
	double clickChainStartMs;
	int clickChainCount;
	EsElement *clickChainElement;
} gui;

struct TableCell {
	uint16_t from[2], to[2];
};

// Miscellanous forward declarations.
void UIWindowPaintNow(EsWindow *window, ProcessMessageTiming *timing, bool afterResize);
void UIWindowLayoutNow(EsWindow *window, ProcessMessageTiming *timing);
EsElement *WindowGetMainPanel(EsWindow *window);
int AccessKeyLayerMessage(EsElement *element, EsMessage *message);
void AccessKeyModeExit();
int ProcessButtonMessage(EsElement *element, EsMessage *message);
void UIMousePressReleased(EsWindow *window, EsMessage *message, bool sendClick);
void UIMaybeRemoveFocusedElement(EsWindow *window);
EsTextStyle TextPlanGetPrimaryStyle(EsTextPlan *plan);
EsElement *UIFindHoverElementRecursively(EsElement *element, int offsetX, int offsetY, EsPoint position);
const EsStyle *UIGetDefaultStyleVariant(const EsStyle *style, EsElement *parent);
void AccessKeysCenterHint(EsElement *element, EsMessage *message);

void InspectorSetup(EsWindow *window);
void InspectorNotifyElementEvent(EsElement *element, const char *cCategory, const char *cFormat, ...);
void InspectorNotifyElementCreated(EsElement *element);
void InspectorNotifyElementDestroyed(EsElement *element);
void InspectorNotifyElementMoved(EsElement *element, EsRectangle takenBounds);
void InspectorNotifyElementPainted(EsElement *element, EsPainter *painter);
void InspectorNotifyElementContentChanged(EsElement *element);

#define UI_STATE_RELAYOUT 		(1 <<  2)
#define UI_STATE_RELAYOUT_CHILD		(1 <<  3)
#define UI_STATE_DESTROYING		(1 <<  4)
#define UI_STATE_DESTROYING_CHILD	(1 <<  5)

#define UI_STATE_HOVERED		(1 <<  6)
#define UI_STATE_LEFT_PRESSED		(1 <<  7)
#define UI_STATE_STRONG_PRESSED		(1 <<  8)
#define UI_STATE_FOCUS_WITHIN		(1 <<  9)
#define UI_STATE_FOCUSED		(1 << 10)
#define UI_STATE_LOST_STRONG_FOCUS	(1 << 11)
#define UI_STATE_MENU_SOURCE		(1 << 12)

#define UI_STATE_ANIMATING		(1 << 13)
#define UI_STATE_ENTERED		(1 << 14)
#define UI_STATE_EXITING		(1 << 15)
#define UI_STATE_BLOCK_INTERACTION	(1 << 16)

#define UI_STATE_TEMP			(1 << 17)
#define UI_STATE_Z_STACK		(1 << 18)
#define UI_STATE_COMMAND_BUTTON		(1 << 19)
#define UI_STATE_USE_MEASUREMENT_CACHE	(1 << 20)
#define UI_STATE_CHECK_VISIBLE		(1 << 21)
#define UI_STATE_INSPECTING		(1 << 22)

struct EsElement : EsElementPublic {
	EsUICallback messageClass;
	EsElement *parent;
	Array<EsElement *> children; 
	uint32_t state;

	uint8_t transitionType;
	uint16_t customStyleState; // ORed to the style state in RefreshStyle.
	uint16_t previousStyleState; // Set by RefreshStyleState.
	uint16_t transitionDurationMs, transitionTimeMs;
	uint64_t lastTimeStamp;
	UIStyle *currentStyle; 
	UIStyleKey currentStyleKey;
	ThemeAnimation animation; 
	EsPaintTarget *previousTransitionFrame; 

	int width, height, offsetX, offsetY;
	uint8_t internalOffsetLeft, internalOffsetRight, internalOffsetTop, internalOffsetBottom;
	TableCell tableCell;

	void Destroy(bool manual = true);
	void PrintTree(int depth = 0);

	inline size_t GetChildCount() {
		return children.Length();
	}

	inline EsElement *GetChild(uintptr_t index) {
		EsAssert(index < children.Length()); // Invalid child index.
		return children[index];
	}

	inline EsElement *GetChildByZ(uintptr_t index) {
		EsMessage m = { ES_MSG_Z_ORDER };
		m.zOrder.index = index, m.zOrder.child = GetChild(index);
		if (m.zOrder.child->flags & ES_ELEMENT_NON_CLIENT) return m.zOrder.child;
		if (ES_REJECTED == EsMessageSend(this, &m)) return nullptr;
		EsAssert(!m.zOrder.child || m.zOrder.child->parent == this); // Child obtained from ES_MSG_Z_ORDER had different parent.
		return m.zOrder.child;
	}

	void BringToFront() {
		for (uintptr_t i = 0; i < parent->children.Length(); i++) {
			if (parent->children[i] == this) {
				InspectorNotifyElementDestroyed(this);
				EsElement *swap = parent->children.Last();
				parent->children.Last() = this;
				parent->children[i] = swap;
				InspectorNotifyElementCreated(this);
				return;
			}
		}

		EsAssert(false);
	}

	bool IsFocusable() {
		if ((~flags & ES_ELEMENT_FOCUSABLE) || (flags & ES_ELEMENT_DISABLED) || (state & UI_STATE_DESTROYING)) {
			return false;
		}

		EsElement *element = this;

		while (element) {
			if ((element->flags & ES_ELEMENT_BLOCK_FOCUS) || (element->state & UI_STATE_BLOCK_INTERACTION) || (element->flags & ES_ELEMENT_HIDDEN)) {
				return false;
			}

			element = element->parent;
		}

		return true;
	}

	bool RefreshStyleState(); // Returns true if any observed bits have changed.
	void RefreshStyle(UIStyleKey *oldStyleKey = nullptr, bool alreadyRefreshStyleState = false, bool force = false);
	bool StartAnimating();
	void SetStyle(const EsStyle *stylePart, bool refreshIfChanged = true);

	inline void MaybeRefreshStyle() {
		if (RefreshStyleState()) {
			RefreshStyle(nullptr, true);
		}
	}

	inline EsRectangle GetWindowBounds(bool client = true) { return EsElementGetWindowBounds(this, client); }
	inline EsRectangle GetScreenBounds(bool client = true) { return EsElementGetScreenBounds(this, client); }
	inline EsRectangle GetBounds() { return ES_RECT_2S(width - internalOffsetLeft - internalOffsetRight, height - internalOffsetTop - internalOffsetBottom); }

#define PAINT_SHADOW        (1 << 0) // Paint the shadow layers.
#define PAINT_NO_OFFSET     (1 << 1) // Don't add the element's offset to the painter.
#define PAINT_NO_TRANSITION (1 << 2) // Ignore entrance/exit transitions.
#define PAINT_OVERLAY       (1 << 3) // Paint the overlay layers.
	void InternalPaint(EsPainter *painter, int flags);

	void InternalMove(int _width, int _height, int _offsetX, int _offsetY); // Non-client offset.
	void InternalCalculateRepaintRegion(int x, int y, bool forwards, bool overlappedBySibling = false);
	bool InternalDestroy(); // Called after processing each message, to destroy any elements marked by ::Destroy.

	int GetWidth(int height);
	int GetHeight(int width);

	void Repaint(bool all, EsRectangle region = ES_RECT_1(0) /* client coordinates */);

	void Initialise(EsElement *_parent, uint64_t _flags, EsUICallback _classCallback, const EsStyle *style);
};

struct MeasurementCache {
	int width0, width2, width2Height;
	int height0, height2, height2Width;

	bool Get(EsMessage *message, uint32_t *state) {
		if (~(*state) & UI_STATE_USE_MEASUREMENT_CACHE) {
			width0 = 0, width2 = 0, width2Height = 0;
			height0 = 0, height2 = 0, height2Width = 0;
			*state |= UI_STATE_USE_MEASUREMENT_CACHE;
		}

		if (message->type == ES_MSG_GET_WIDTH) {
			if (message->measure.height && message->measure.height == width2Height) {
				message->measure.width = width2;
			} else if (!message->measure.height && width0) {
				message->measure.width = width0;
			} else {
				return false;
			}
		} else {
			if (message->measure.width && message->measure.width == height2Width) {
				message->measure.height = height2;
			} else if (!message->measure.width && height0) {
				message->measure.height = height0;
			} else {
				return false;
			}
		}

		return true;
	}

	void Store(EsMessage *message) {
		if (message->type == ES_MSG_GET_WIDTH) {
			if (message->measure.height) {
				width2 = message->measure.width;
				width2Height = message->measure.height;
			} else {
				width0 = message->measure.width;
			}
		} else {
			if (message->measure.width) {
				height2Width = message->measure.width;
				height2 = message->measure.height;
			} else {
				height0 = message->measure.height;
			}
		}
	}
};

struct EsButton : EsElement {
	char *label;
	size_t labelBytes;
	EsGeneric menuItemContext;
	uint32_t iconID;
	MeasurementCache measurementCache;
	EsCommand *command;
	EsCommandCallback onCommand;
	EsElement *checkBuddy;
};

struct ScrollPane {
	EsElement *parent, *pad;
	EsScrollbar *bar[2];
	double position[2];
	int64_t limit[2];
	int32_t fixedViewport[2];
	bool autoScrollbars[2];
	bool dragScrolling;

#define SCROLL_MODE_NONE    (0)      // No scrolling takes place on this axis.
#define SCROLL_MODE_HIDDEN  (1)      // Scrolling takes place, but there is no visible scrollbar.
#define SCROLL_MODE_FIXED   (2)      // The scrollbar is always visible.
#define SCROLL_MODE_AUTO    (3)      // The scrollbar is only visible if the content is larger than the viewport.
	uint8_t mode[2];
#define SCROLL_X_DRAG (1 << 0)
#define SCROLL_Y_DRAG (1 << 1)
	uint16_t flags;

	void Setup(EsElement *parent, uint8_t xMode, uint8_t yMode, uint16_t flags);
	void SetPosition(int axis, double newPosition, bool sendMovedMessage = true);
	void Refresh();
	void ReceivedMessage(EsMessage *message);

	inline void SetX(double scrollX, bool sendMovedMessage = true) { SetPosition(0, scrollX, sendMovedMessage); }
	inline void SetY(double scrollY, bool sendMovedMessage = true) { SetPosition(1, scrollY, sendMovedMessage); }

	// Internal.
	bool RefreshLimit(int axis, int64_t *content);
};

struct PanelMovementItem {
	EsElement *element;
	EsRectangle oldBounds;
	bool wasHidden;
};

struct EsPanel : EsElement {
	// TODO Make this structure smaller?
	
	ScrollPane scroll;

	const EsStyle *separatorStylePart;
	bool addingSeparator;
	uint64_t separatorFlags;
	
	uint16_t bandCount[2];
	EsPanelBand *bands[2];
	uintptr_t tableIndex;

	uint16_t transitionType;
	uint32_t transitionTimeMs,
		 transitionLengthMs;

	bool destroyPreviousAfterTransitionCompletes;
	EsElement *switchedTo, 
		  *switchedFrom;

	Array<PanelMovementItem> movementItems;

	MeasurementCache measurementCache;

	int GetGapMajor() {
		return currentStyle->gapMajor;
	}

	int GetGapMinor() {
		return currentStyle->gapMinor;
	}

	EsRectangle GetInsets() {
		return currentStyle->insets;
	}

	int GetInsetWidth() {
		EsRectangle insets = GetInsets();
		return insets.l + insets.r;
	}

	int GetInsetHeight() {
		EsRectangle insets = GetInsets();
		return insets.t + insets.b;
	}
};

struct EsTextDisplay : EsElement {
	EsTextPlanProperties properties; 
	EsTextRun *textRuns;
	size_t textRunCount;
	char *contents;

	bool usingSyntaxHighlighting;

	MeasurementCache measurementCache;
	EsTextPlan *plan;
	int planWidth, planHeight;
};

struct ColorPickerHost {
	struct EsElement *well;
	bool *indeterminate;
	bool hasOpacity;
};

void ColorPickerCreate(EsElement *parent, ColorPickerHost host, uint32_t initialColor, bool showTextbox);

void HeapDuplicate(void **pointer, const void *data, size_t bytes) {
	if (*pointer) {
		EsHeapFree(*pointer);
	}

	if (!data && !bytes) {
		*pointer = nullptr;
	} else {
		void *buffer = EsHeapAllocate(bytes, false);
		EsMemoryCopy(buffer, data, bytes);
		*pointer = buffer;
	}
}

// --------------------------------- Windows.

struct EsWindow : EsElement {
	EsHandle handle;
	uint32_t windowWidth, windowHeight;

	bool willUpdate, toolbarFillMode, destroyInstanceAfterClose, hasDialog, doNotPaint;
	bool restoreOnNextMove, resetPositionOnNextMove, receivedFirstResize, isMaximised;
	bool hovering, activated;
	bool visualizeRepaints, visualizeLayoutBounds, visualizePaintSteps; // Inspector properties.

	EsPoint mousePosition;

	EsElement *mainPanel, *toolbar;
	EsPanel *toolbarSwitcher;

	EsElement *hovered, 
		  *pressed, 
		  *focused,
		  *inactiveFocus,
		  *dragged,
		  *ensureVisible;

	EsButton *enterButton, 
		 *escapeButton, 
		 *defaultEnterButton;

	// An array of elements that we check are visible after every layout.
	// e.g. image displays that want to unload the decoded bitmap when they are scrolled off-screen.
	// TODO Support a more advanced queueing system for scroll panes asynchronous tasks.
	Array<EsElement *> checkVisible;
	bool processCheckVisible;

	EsElement *dialogOverlay, *dialogPanel;
	EsWindowStyle windowStyle;
	EsRectangle beforeMaximiseBounds;

	EsRectangle updateRegion;
	EsRectangle updateRegionInProgress; // For visualizePaintSteps.

	Array<struct SizeAlternative> sizeAlternatives;

	EsElement *source; // Menu source.
	EsWindow *targetMenu; // The menu that keyboard events should be sent to.

	EsPoint announcementBase;
	double announcementTimeMs;
};

struct SizeAlternative {
	EsElement *small, *big;
	int widthThreshold, heightThreshold;
};

// --------------------------------- Container windows.

#define RESIZE_LEFT 		(1)
#define RESIZE_RIGHT 		(2)
#define RESIZE_TOP 		(4)
#define RESIZE_BOTTOM 		(8)
#define RESIZE_TOP_LEFT 	(5)
#define RESIZE_TOP_RIGHT 	(6)
#define RESIZE_BOTTOM_LEFT 	(9)
#define RESIZE_BOTTOM_RIGHT 	(10)
#define RESIZE_MOVE		(0)

#define SNAP_EDGE_MAXIMIZE (1)
#define SNAP_EDGE_LEFT (2)
#define SNAP_EDGE_RIGHT (3)

void WindowSnap(EsWindow *window, bool restored, bool dragging, uint8_t edge) {
	if (window->isMaximised) {
		return;
	}

	EsRectangle screen;
	EsSyscall(ES_SYSCALL_SCREEN_WORK_AREA_GET, 0, (uintptr_t) &screen, 0, 0);

	if (!window->restoreOnNextMove && !restored) {
		window->beforeMaximiseBounds = EsWindowGetBounds(window);
	}

	window->restoreOnNextMove = true;
	window->isMaximised = edge == SNAP_EDGE_MAXIMIZE;

	EsRectangle bounds;

	if (edge == SNAP_EDGE_MAXIMIZE) {
		bounds.t = screen.t - 16 * theming.scale;
		bounds.b = screen.b + 19 * theming.scale;
		bounds.l = screen.l - 19 * theming.scale;
		bounds.r = screen.r + 19 * theming.scale;
	} else if (edge == SNAP_EDGE_LEFT) {
		bounds.t = screen.t;
		bounds.b = screen.b;
		bounds.l = screen.l;
		bounds.r = (screen.r + screen.l) / 2;
	} else if (edge == SNAP_EDGE_RIGHT) {
		bounds.t = screen.t;
		bounds.b = screen.b;
		bounds.l = (screen.r + screen.l) / 2;
		bounds.r = screen.r;
	}

	EsSyscall(ES_SYSCALL_WINDOW_MOVE, window->handle, (uintptr_t) &bounds, 0, 
			ES_WINDOW_MOVE_DYNAMIC | (edge == SNAP_EDGE_MAXIMIZE ? ES_WINDOW_MOVE_MAXIMISED : 0));

	if (!dragging) {
		window->resetPositionOnNextMove = true;
	}
}

void WindowRestore(EsWindow *window) {
	if (!window->restoreOnNextMove) {
		return;
	}

	window->isMaximised = false;
	window->restoreOnNextMove = false;
	EsSyscall(ES_SYSCALL_WINDOW_MOVE, window->handle, (uintptr_t) &window->beforeMaximiseBounds, 0, ES_WINDOW_MOVE_DYNAMIC);
}

void WindowChangeBounds(int direction, int newX, int newY, int *originalX, int *originalY, EsWindow *window) {
	EsRectangle bounds = EsWindowGetBounds(window), bounds2;
	bounds2 = bounds;

	int oldWidth = bounds.r - bounds.l;
	int oldHeight = bounds.b - bounds.t;
	bool restored = false, canSnap = true;

	if (window->restoreOnNextMove) {
		window->restoreOnNextMove = false;
		oldWidth = window->beforeMaximiseBounds.r - window->beforeMaximiseBounds.l;
		oldHeight = window->beforeMaximiseBounds.b - window->beforeMaximiseBounds.t;
		restored = true;
	}

	if (direction & RESIZE_LEFT)   bounds.l = newX + BORDER_THICKNESS / 2 - WINDOW_INSET;
	if (direction & RESIZE_RIGHT)  bounds.r = newX - BORDER_THICKNESS / 2 + WINDOW_INSET;
	if (direction & RESIZE_TOP)    bounds.t = newY + BORDER_THICKNESS / 2 - WINDOW_INSET;
	if (direction & RESIZE_BOTTOM) bounds.b = newY - BORDER_THICKNESS / 2 + WINDOW_INSET;

	EsRectangle screen;
	EsSyscall(ES_SYSCALL_SCREEN_WORK_AREA_GET, 0, (uintptr_t) &screen, 0, 0);

	int newWidth = bounds.r - bounds.l;
	int newHeight = bounds.b - bounds.t;

	int windowSnapRange = GetConstantNumber("windowSnapRange");
	int windowMinimumWidth = GetConstantNumber("windowMinimumWidth");
	int windowMinimumHeight = GetConstantNumber("windowMinimumHeight");
	int windowRestoreDragYPosition = GetConstantNumber("windowRestoreDragYPosition");

	window->isMaximised = false;

	if (newWidth  < windowMinimumWidth  && direction & RESIZE_LEFT)   bounds.l = bounds.r - windowMinimumWidth;
	if (newWidth  < windowMinimumWidth  && direction & RESIZE_RIGHT)  bounds.r = bounds.l + windowMinimumWidth;
	if (newHeight < windowMinimumHeight && direction & RESIZE_TOP)    bounds.t = bounds.b - windowMinimumHeight;
	if (newHeight < windowMinimumHeight && direction & RESIZE_BOTTOM) bounds.b = bounds.t + windowMinimumHeight;

	if (direction == RESIZE_MOVE) {
		if (newY < screen.t + windowSnapRange && canSnap) {
			WindowSnap(window, restored, true, SNAP_EDGE_MAXIMIZE);
			return;
		} else if (newX < screen.l + windowSnapRange && canSnap) {
			WindowSnap(window, restored, true, SNAP_EDGE_LEFT);
			return;
		} else if (newX >= screen.r - windowSnapRange && canSnap) {
			WindowSnap(window, restored, true, SNAP_EDGE_RIGHT);
			return;
		} else {
			if (restored && window->resetPositionOnNextMove) {
				// The user previously snapped/maximised the window in a previous operation.
				// Therefore, the movement anchor won't be what the user expects.
				// Try to put it in the center.
				int positionAlongWindow = *originalX - bounds2.l;
				int maxPosition = bounds2.r - bounds2.l;
				if (positionAlongWindow > maxPosition - oldWidth / 2) *originalX = gui.lastClickX = positionAlongWindow - maxPosition + oldWidth;
				else if (positionAlongWindow > oldWidth / 2) *originalX = gui.lastClickX = oldWidth / 2;
				*originalY = gui.lastClickY = windowRestoreDragYPosition;
				window->resetPositionOnNextMove = false;
			}

			bounds.l = newX - *originalX;
			bounds.t = newY - *originalY;
			bounds.r = bounds.l + oldWidth;
			bounds.b = bounds.t + oldHeight;
		}
	} else {
		window->resetPositionOnNextMove = window->restoreOnNextMove = false;
	}

	EsSyscall(ES_SYSCALL_WINDOW_MOVE, window->handle, (uintptr_t) &bounds, 0, ES_WINDOW_MOVE_DYNAMIC);
}

int ProcessWindowBorderMessage(EsWindow *window, EsMessage *message, EsRectangle bounds, int from, int to) {
	if (message->type == ES_MSG_GET_CURSOR) {
		EsPoint position = EsMouseGetPosition(window);
		message->cursorStyle = ES_CURSOR_NORMAL;

		if (window->isMaximised) {
			gui.resizeType = 0;
			gui.resizeCursor = message->cursorStyle;
		} else {
			bool left = position.x < to, right = position.x >= bounds.r - to, 
			     top = position.y < to, bottom = position.y >= bounds.b - to;

			if (gui.resizing) {
				message->cursorStyle = gui.resizeCursor;
			} else if (position.x < from || position.y < from 
					|| position.x >= bounds.r - from || position.y >= bounds.b - from) {
			} else if ((right && top) || (bottom && left)) {
				message->cursorStyle = ES_CURSOR_RESIZE_DIAGONAL_1;
			} else if ((left && top) || (bottom && right)) {
				message->cursorStyle = ES_CURSOR_RESIZE_DIAGONAL_2;
			} else if (left || right) {
				message->cursorStyle = ES_CURSOR_RESIZE_HORIZONTAL;
			} else if (top || bottom) {
				message->cursorStyle = ES_CURSOR_RESIZE_VERTICAL;
			}

			if (!window->pressed && !gui.mouseButtonDown) {
				gui.resizeType = (left ? RESIZE_LEFT : 0) | (right ? RESIZE_RIGHT : 0) | (top ? RESIZE_TOP : 0) | (bottom ? RESIZE_BOTTOM : 0);
				gui.resizeCursor = message->cursorStyle;
			}
		}

	} else if (message->type == ES_MSG_MOUSE_LEFT_DOWN) {
	} else if (message->type == ES_MSG_MOUSE_LEFT_DRAG) {
		EsPoint screenPosition = EsMouseGetPosition(nullptr);

		if (!window->isMaximised || gui.resizeType == RESIZE_MOVE) {
			WindowChangeBounds(gui.resizeType, 
					screenPosition.x, screenPosition.y, 
					&gui.lastClickX, &gui.lastClickY, window);
			gui.resizing = true;
		}
	} else if (message->type == ES_MSG_MOUSE_LEFT_UP) {
		if (window->restoreOnNextMove) {
			window->resetPositionOnNextMove = true;
		}

		gui.resizing = false;
	} else {
		return 0;
	}

	return ES_HANDLED;
}

// --------------------------------- Dialogs.

void EsDialogClose(EsWindow *window) {
	EsMessageMutexCheck();
	EsAssert(window->hasDialog); // The window does not have an open dialog.
	window->dialogPanel->Destroy();
	window->dialogOverlay->Destroy(); // TODO This looks bad if we immediately open a new dialog. But maybe it'll look alright with exiting transitions?
	window->dialogOverlay = window->dialogPanel = nullptr;
	window->children[0]->children[0]->flags &= ~ES_ELEMENT_BLOCK_FOCUS;
	window->children[1]->state &= ~UI_STATE_BLOCK_INTERACTION;
	window->hasDialog = false;
}

EsElement *EsDialogShowAlert(EsWindow *window, const char *title, ptrdiff_t titleBytes, 
		const char *content, ptrdiff_t contentBytes, uint32_t iconID, uint32_t flags) {
	EsElement *dialog = EsDialogShow(window);
	EsPanel *heading = EsPanelCreate(dialog, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL, ES_STYLE_DIALOG_HEADING);

	if (iconID) {
		EsIconDisplayCreate(heading, ES_FLAGS_DEFAULT, {}, iconID);
	}

	EsTextDisplayCreate(heading, ES_CELL_H_FILL | ES_CELL_V_CENTER, ES_STYLE_TEXT_HEADING2, 
			title, titleBytes)->cName = "dialog heading";
	EsTextDisplayCreate(EsPanelCreate(dialog, ES_CELL_H_FILL | ES_PANEL_VERTICAL, ES_STYLE_DIALOG_CONTENT), 
			ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, 
			content, contentBytes)->cName = "dialog contents";

	EsPanel *buttonArea = EsPanelCreate(dialog, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL | ES_PANEL_REVERSE, ES_STYLE_DIALOG_BUTTON_AREA);

	if (flags & ES_DIALOG_ALERT_OK_BUTTON) {
		EsButton *button = EsButtonCreate(buttonArea, ES_BUTTON_DEFAULT, 0, "OK");
		EsButtonOnCommand(button, [] (EsInstance *instance, EsElement *, EsCommand *) { EsDialogClose(instance->window); });
		EsElementFocus(button);
	}

	return buttonArea;
}

EsElement *EsDialogShow(EsWindow *window) {
	EsAssert(window->windowStyle == ES_WINDOW_NORMAL); // Can only show dialogs on normal windows.
	EsAssert(!window->hasDialog); // Cannot nest dialogs.

	EsElement *mainStack = window->children[0];
	mainStack->children[0]->flags |= ES_ELEMENT_BLOCK_FOCUS;
	window->children[1]->state |= UI_STATE_BLOCK_INTERACTION;
	window->hasDialog = true;
	window->dialogOverlay = EsPanelCreate(mainStack, ES_CELL_FILL, ES_STYLE_PANEL_MODAL_OVERLAY);
	window->dialogOverlay->cName = "modal overlay";
	window->dialogPanel = EsPanelCreate(mainStack, ES_PANEL_VERTICAL | ES_CELL_CENTER | ES_CELL_SHRINK, ES_STYLE_PANEL_DIALOG_ROOT);
	window->dialogPanel->cName = "dialog";

	return window->dialogPanel;
}

// --------------------------------- Windows.

void UIWindowNeedsUpdate(EsWindow *window) {
	if (!window->willUpdate) {
		EsMessage m = { ES_MSG_UPDATE_WINDOW };
		// Don't use the userland posted message queue, since we don't want this to block WM messages.
		EsSyscall(ES_SYSCALL_MESSAGE_POST, (uintptr_t) &m, (uintptr_t) window, ES_CURRENT_PROCESS, 0);
		window->willUpdate = true;
	}
}

void UIWindowDestroy(EsWindow *window) {
	gui.allWindows.FindAndDeleteSwap(window, true);
	AccessKeyModeExit();
	EsSyscall(ES_SYSCALL_WINDOW_CLOSE, window->handle, 0, 0, 0);
	EsHandleClose(window->handle);
	window->checkVisible.Free();
	window->handle = ES_INVALID_HANDLE;
}

EsElement *WindowGetMainPanel(EsWindow *window) {
	if (window->windowStyle == ES_WINDOW_MENU) {
		if (!window->children[0]->GetChildCount()) {
			EsMenuNextColumn((EsMenu *) window, ES_FLAGS_DEFAULT);
		}

		return window->children[0]->GetChild(window->children[0]->GetChildCount() - 1);
	}

	return window->mainPanel;
}

int ProcessRootMessage(EsElement *element, EsMessage *message) {
	EsWindow *window = (EsWindow *) element;
	EsRectangle bounds = window->GetBounds();
	int response = 0;

	if (window->windowStyle == ES_WINDOW_CONTAINER) {
		if (message->type == ES_MSG_LAYOUT) {
			EsElementMove(window->GetChild(0), WINDOW_INSET, WINDOW_INSET, bounds.r - WINDOW_INSET * 2, CONTAINER_TAB_BAND_HEIGHT);
		} else {
			response = ProcessWindowBorderMessage(window, message, bounds, WINDOW_INSET - BORDER_THICKNESS, WINDOW_INSET);
		}
	} else if (window->windowStyle == ES_WINDOW_MENU) {
		if (message->type == ES_MSG_PAINT_BACKGROUND) {
			EsDrawClear(message->painter, message->painter->clip);
		} else if (message->type == ES_MSG_LAYOUT) {
			if (window->GetChildCount()) {
				EsElementMove(window->GetChild(0), 0, 0, bounds.r, bounds.b);
			}
		}
	} else if (window->windowStyle == ES_WINDOW_INSPECTOR) {
		if (message->type == ES_MSG_LAYOUT) {
			if (window->GetChildCount()) {
				EsElementMove(window->GetChild(0), BORDER_THICKNESS, BORDER_THICKNESS + 30,
						bounds.r - BORDER_THICKNESS * 2, bounds.b - BORDER_THICKNESS * 2 - 30);
			}
		} else {
			response = ProcessWindowBorderMessage(window, message, bounds, 0, BORDER_THICKNESS);
		}
	} else if (window->windowStyle == ES_WINDOW_NORMAL) {
		if (message->type == ES_MSG_LAYOUT) {
			if (window->GetChildCount()) {
				EsElement *toolbar = window->toolbarSwitcher;

				if (window->toolbarFillMode) {
					EsElementMove(toolbar, 0, 0, bounds.r, bounds.b);
				} else {
					int toolbarHeight = toolbar->GetChildCount() ? toolbar->GetHeight(bounds.r) 
						: toolbar->currentStyle->metrics->minimumHeight;
					EsElementMove(window->GetChild(0), 0, toolbarHeight, bounds.r, bounds.b - toolbarHeight);
					EsElementMove(toolbar, 0, 0, bounds.r, toolbarHeight);
					EsElementMove(window->GetChild(2), 0, 0, bounds.r, bounds.b);
				}
			}
		}
	} else if (window->windowStyle == ES_WINDOW_TIP || window->windowStyle == ES_WINDOW_PLAIN) {
		if (message->type == ES_MSG_LAYOUT) {
			if (window->GetChildCount()) {
				EsElementMove(window->GetChild(0), 0, 0, bounds.r, bounds.b);
			}
		}
	}

	if (message->type == ES_MSG_DESTROY) {
		if (window->windowStyle != ES_WINDOW_NORMAL) {
			EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, ES_FLAGS_DEFAULT, 0, ES_WINDOW_PROPERTY_SOLID);
		}

		if (window->windowStyle == ES_WINDOW_MENU) {
			window->source->state &= ~UI_STATE_MENU_SOURCE;
			window->source->MaybeRefreshStyle();
			EsAssert(window->source->window->targetMenu == window);
			window->source->window->targetMenu = nullptr;
		}
	}

	return response;
}

EsWindow *EsWindowCreate(EsInstance *instance, EsWindowStyle style) {
	EsMessageMutexCheck();

	for (uintptr_t i = 0; i < gui.allWindows.Length(); i++) {
		UIMousePressReleased(gui.allWindows[i], nullptr, false);
	}

	EsWindow *window = (EsWindow *) EsHeapAllocate(sizeof(EsWindow), true);
	gui.allWindows.Add(window);
	window->instance = instance;

	if (style == ES_WINDOW_NORMAL) {
		window->handle = ((APIInstance *) instance->_private)->mainWindowHandle;
	} else {
		window->handle = EsSyscall(ES_SYSCALL_WINDOW_CREATE, style, 0, (uintptr_t) window, 0);
	}

	window->Initialise(nullptr, ES_CELL_FILL, ProcessRootMessage, nullptr);
	window->cName = "window";
	window->window = window;
	window->width = window->windowWidth, window->height = window->windowHeight;
	window->hovered = window;
	window->windowStyle = style;
	window->RefreshStyle();

	if (style == ES_WINDOW_NORMAL) {
		EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, 0, (uintptr_t) window, ES_WINDOW_PROPERTY_OBJECT);
		EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, 0xFF000000 | GetConstantNumber("windowFillColor"), 0, ES_WINDOW_PROPERTY_RESIZE_CLEAR_COLOR);
		window->activated = true;
		EsPanel *panel = EsPanelCreate(window, ES_ELEMENT_NON_CLIENT | ES_CELL_FILL | ES_PANEL_Z_STACK, ES_STYLE_PANEL_NORMAL_WINDOW_ROOT);
		panel->cName = "window stack";
		window->mainPanel = EsPanelCreate(panel, ES_CELL_FILL, ES_STYLE_PANEL_NORMAL_WINDOW_ROOT);
		window->mainPanel->cName = "window root";
		window->toolbarSwitcher = EsPanelCreate(window, ES_ELEMENT_NON_CLIENT | ES_PANEL_SWITCHER | ES_CELL_FILL, ES_STYLE_PANEL_TOOLBAR_ROOT);
		window->toolbarSwitcher->cName = "toolbar";
		EsElement *accessKeyLayer = EsCustomElementCreate(window, ES_ELEMENT_NON_CLIENT | ES_CELL_FILL | ES_ELEMENT_NO_HOVER, nullptr);
		accessKeyLayer->cName = "access key layer";
		accessKeyLayer->messageUser = AccessKeyLayerMessage;
		window->state |= UI_STATE_Z_STACK;
	} else if (style == ES_WINDOW_CONTAINER) {
		window->windowWidth = GetConstantNumber("windowDefaultWidth");
		window->windowHeight = GetConstantNumber("windowDefaultHeight");
		static int cascadeX = -1, cascadeY = -1;
		EsRectangle workArea;
		EsSyscall(ES_SYSCALL_SCREEN_WORK_AREA_GET, 0, (uintptr_t) &workArea, 0, 0);
		int cascadeMargin = GetConstantNumber("windowCascadeMargin");
		int cascadeOffset = GetConstantNumber("windowCascadeOffset");
		if (cascadeX == -1 || cascadeX + (int) window->windowWidth > workArea.r - cascadeMargin) cascadeX = workArea.l + cascadeMargin;
		if (cascadeY == -1 || cascadeY + (int) window->windowHeight > workArea.b - cascadeMargin) cascadeY = workArea.t + cascadeMargin;
		EsRectangle bounds = ES_RECT_4(cascadeX, cascadeX + window->windowWidth, cascadeY, cascadeY + window->windowHeight);
		if (bounds.r > workArea.r - cascadeMargin) bounds.r = workArea.r - cascadeMargin;
		if (bounds.b > workArea.b - cascadeMargin) bounds.b = workArea.b - cascadeMargin;
		cascadeX += cascadeOffset, cascadeY += cascadeOffset;
		EsSyscall(ES_SYSCALL_WINDOW_MOVE, window->handle, (uintptr_t) &bounds, 0, ES_FLAGS_DEFAULT);
		EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, 0, 0, ES_WINDOW_PROPERTY_FOCUSED);
		EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, ES_WINDOW_SOLID_TRUE, 10, ES_WINDOW_PROPERTY_SOLID);
		window->mainPanel = EsPanelCreate(window, ES_ELEMENT_NON_CLIENT | ES_CELL_FILL, ES_STYLE_PANEL_CONTAINER_WINDOW_ROOT);
		window->SetStyle(ES_STYLE_CONTAINER_WINDOW_ACTIVE);
	} else if (style == ES_WINDOW_INSPECTOR) {
		EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, ES_WINDOW_SOLID_TRUE, 0, ES_WINDOW_PROPERTY_SOLID);
		window->SetStyle(ES_STYLE_PANEL_INSPECTOR_WINDOW_ROOT);
		window->mainPanel = EsPanelCreate(window, ES_ELEMENT_NON_CLIENT | ES_CELL_FILL, ES_STYLE_PANEL_INSPECTOR_WINDOW_CONTAINER);
		EsRectangle bounds = { 10, 600, 10, 500 };
		EsSyscall(ES_SYSCALL_WINDOW_MOVE, window->handle, (uintptr_t) &bounds, 0, ES_WINDOW_MOVE_ADJUST_TO_FIT_SCREEN);
	} else if (style == ES_WINDOW_TIP || style == ES_WINDOW_PLAIN) {
		EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, style == ES_WINDOW_PLAIN ? ES_WINDOW_SOLID_TRUE : ES_FLAGS_DEFAULT, 0, ES_WINDOW_PROPERTY_SOLID);
		window->mainPanel = EsPanelCreate(window, ES_ELEMENT_NON_CLIENT | ES_CELL_FILL, nullptr);
	} else if (style == ES_WINDOW_MENU) {
		EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, ES_WINDOW_SOLID_TRUE, 9 * theming.scale, ES_WINDOW_PROPERTY_SOLID);
		EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, BLEND_WINDOW_MATERIAL_GLASS, 0, ES_WINDOW_PROPERTY_MATERIAL);

		window->SetStyle(ES_STYLE_PANEL_MENU_ROOT);

		EsPanel *panel = EsPanelCreate(window, ES_ELEMENT_NON_CLIENT | ES_PANEL_HORIZONTAL | ES_CELL_FILL, ES_STYLE_PANEL_MENU_CONTAINER);
		panel->cName = "menu";
		panel->separatorStylePart = ES_STYLE_MENU_SEPARATOR_VERTICAL;
		panel->separatorFlags = ES_CELL_V_FILL;

		panel->messageUser = [] (EsElement *element, EsMessage *message) {
			if (message->type == ES_MSG_PAINT) {
				EsPainter *painter = message->painter;
				EsRectangle blurBounds = element->GetWindowBounds();
				blurBounds = EsRectangleAddBorder(blurBounds, ((UIStyle *) painter->style)->insets);
				EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, element->window->handle, (uintptr_t) &blurBounds, 0, ES_WINDOW_PROPERTY_BLUR_BOUNDS);
			}

			return 0;
		};
		
		window->mainPanel = panel;
	}

	if (style == ES_WINDOW_INSPECTOR) {
		InspectorSetup(window);
	}

	return window;
}

void EsWindowAddSizeAlternative(EsWindow *window, EsElement *small, EsElement *big, int widthThreshold, int heightThreshold) {
	EsMessageMutexCheck();
	EsAssert(small->window == window && big->window == window);

	SizeAlternative alternative = {};
	alternative.small = small, alternative.big = big;
	alternative.widthThreshold = widthThreshold, alternative.heightThreshold = heightThreshold;
	window->sizeAlternatives.Add(alternative);

	bool belowThreshold = window->width < widthThreshold || window->height < heightThreshold;
	EsElementSetHidden(small, !belowThreshold);
	EsElementSetHidden(big, belowThreshold);
}

// --------------------------------- Menus.

struct EsMenu : EsWindow {};

void EsMenuAddSeparator(EsMenu *menu) {
	EsCustomElementCreate(menu, ES_CELL_H_FILL, ES_STYLE_MENU_SEPARATOR_HORIZONTAL)->cName = "menu separator";
}

void EsMenuNextColumn(EsMenu *menu, uint64_t flags) {
	EsPanelCreate(menu->children[0], ES_PANEL_VERTICAL | ES_CELL_V_TOP | flags, ES_STYLE_PANEL_MENU_COLUMN);
}

EsElement *EsMenuGetSource(EsMenu *menu) {
	return ((EsWindow *) menu)->source;
}

EsMenu *EsMenuCreate(EsElement *source, uint64_t flags) {
	EsWindow *menu = (EsWindow *) EsWindowCreate(source->instance, ES_WINDOW_MENU);
	menu->flags |= flags;
	menu->activated = true;
	menu->source = source;
	return (EsMenu *) menu;
}

void EsMenuAddCommandsFromToolbar(EsMenu *menu, EsElement *element) {
	for (uintptr_t i = 0; i < element->GetChildCount(); i++) {
		EsElement *child = element->GetChild(i);

		if (child->flags & ES_ELEMENT_NON_CLIENT) {
			continue;
		}

		if (child->messageClass == ProcessButtonMessage) {
			EsButton *button = (EsButton *) child;

			if (button->command) {
				EsMenuAddCommand(menu, button->command->check, button->label, button->labelBytes, button->command);
			}
		} else {
			EsMenuAddCommandsFromToolbar(menu, element);
		}
	}
}

void EsMenuShow(EsMenu *menu, int fixedWidth, int fixedHeight) {
	EsAssert(!menu->source->window->targetMenu);
	EsAssert(~menu->source->state & UI_STATE_MENU_SOURCE);
	menu->source->state |= UI_STATE_MENU_SOURCE;
	menu->source->MaybeRefreshStyle();
	menu->source->window->targetMenu = menu;

	EsRectangle menuInsets = menu->GetChild(0)->currentStyle->insets;
	if (fixedWidth) fixedWidth += menuInsets.l + menuInsets.r;
	if (fixedHeight) fixedHeight += menuInsets.t + menuInsets.b;
	int width = fixedWidth ?: menu->GetChild(0)->GetWidth(fixedHeight);
	int height = menu->GetChild(0)->GetHeight(width);

	if (fixedHeight) {
		if (menu->flags & ES_MENU_MAXIMUM_HEIGHT) {
			if (height >= fixedHeight) {
				height = fixedHeight;
			}
		} else {
			height = fixedHeight;
		}
	}

	EsPoint position;

	if (~menu->flags & ES_MENU_AT_CURSOR) {
		EsRectangle bounds = menu->source->GetScreenBounds(false);

		position = ES_POINT(bounds.l - (width + menuInsets.l - menuInsets.r) / 2 + menu->source->width / 2, 
				bounds.b - menuInsets.t);

		// Try to the keep the menu within the bounds of the source window.

		EsRectangle windowBounds = menu->source->window->GetScreenBounds();

		if (position.x + width >= windowBounds.r) {
			position.x = windowBounds.r - width - 1;
		}

		if (position.x < windowBounds.l) {
			position.x = windowBounds.l;
		} 
			
		if (position.y + height >= windowBounds.b) {
			position.y = windowBounds.b - height - 1;
		}
		
		if (position.y < windowBounds.t) {
			position.y = windowBounds.t;
		} 
	} else {
		position = EsMouseGetPosition();
		position.x -= menuInsets.l, position.y -= menuInsets.t;
	}

	menu->width = width, menu->height = height;
	EsRectangle bounds = ES_RECT_4(position.x, position.x + width, position.y, position.y + height);
	EsSyscall(ES_SYSCALL_WINDOW_MOVE, menu->handle, (uintptr_t) &bounds, 0, ES_WINDOW_MOVE_ADJUST_TO_FIT_SCREEN | ES_WINDOW_MOVE_ALWAYS_ON_TOP);
}

void EsMenuCloseAll() {
	for (uintptr_t i = 0; i < gui.allWindows.Length(); i++) {
		if (gui.allWindows[i]->windowStyle == ES_WINDOW_MENU) {
			EsElementDestroy(gui.allWindows[i]);
		}
	}
}

// --------------------------------- File menu.

const EsStyle styleFileMenuDocumentInformationPanel1 = {
	.metrics = {
		.mask = ES_THEME_METRICS_INSETS | ES_THEME_METRICS_GAP_MAJOR,
		.insets = ES_RECT_4(10, 10, 5, 5),
		.gapMajor = 5,
	},
};

const EsStyle styleFileMenuDocumentInformationPanel2 = {
	.metrics = {
		.mask = ES_THEME_METRICS_GAP_MAJOR,
		.gapMajor = 5,
	},
};

void FileMenuCreate(EsInstance *_instance, EsElement *element, EsCommand *) {
	// TODO Make this user-customizable?

	// const EsFileMenuSettings *settings = (const EsFileMenuSettings *) element->userData.p;
	APIInstance *instance = (APIInstance *) _instance->_private;
	EsAssert(instance->instanceClass == ES_INSTANCE_CLASS_EDITOR);
	EsInstanceClassEditorSettings *editorSettings = &instance->editorSettings;
	EsMenu *menu = EsMenuCreate(element, ES_FLAGS_DEFAULT);
	EsPanel *panel1 = EsPanelCreate(menu, ES_PANEL_HORIZONTAL | ES_CELL_H_LEFT, &styleFileMenuDocumentInformationPanel1);

	bool newDocument = !instance->startupInformation || !instance->startupInformation->filePath;

	{
		// TODO Get this icon from the file type database?
		// 	We'll probably need Desktop to send this via EsApplicationStartupInformation and when the file is renamed.

		EsIconDisplayCreate(panel1, ES_FLAGS_DEFAULT, 0, editorSettings->documentIconID);
		EsSpacerCreate(panel1, ES_FLAGS_DEFAULT, 0, 5, 0);

		EsPanel *panel2 = EsPanelCreate(panel1, ES_FLAGS_DEFAULT, &styleFileMenuDocumentInformationPanel2);
		EsPanel *panel3 = EsPanelCreate(panel2, ES_PANEL_HORIZONTAL | ES_PANEL_H_LEFT, &styleFileMenuDocumentInformationPanel2);
		
		if (newDocument) {
			EsTextDisplayCreate(panel3, ES_FLAGS_DEFAULT, ES_STYLE_TEXT_LABEL, 
					editorSettings->newDocumentTitle, editorSettings->newDocumentTitleBytes);
		} else {
			EsTextDisplayCreate(panel3, ES_FLAGS_DEFAULT, ES_STYLE_TEXT_LABEL, 
					instance->startupInformation->filePath, instance->startupInformation->filePathBytes);
		}

		EsButton *renameButton = EsButtonCreate(panel3, ES_BUTTON_TOOLBAR); // TODO.
		EsButtonSetIcon(renameButton, ES_ICON_DOCUMENT_EDIT_SYMBOLIC);

		if (!newDocument) {
			EsPanel *panel4 = EsPanelCreate(panel2, ES_PANEL_TABLE | ES_PANEL_HORIZONTAL | ES_CELL_H_LEFT, &styleFileMenuDocumentInformationPanel2);
			EsPanelSetBands(panel4, 2 /* columns */);

			char buffer[64];
			size_t bytes;

			bytes = EsStringFormat(buffer, sizeof(buffer), "%D", EsFileStoreGetSize(instance->fileStore));
			EsTextDisplayCreate(panel4, ES_CELL_H_RIGHT, ES_STYLE_TEXT_LABEL_SECONDARY, INTERFACE_STRING(CommonFileMenuFileSize));
			EsTextDisplayCreate(panel4, ES_CELL_H_LEFT, ES_STYLE_TEXT_LABEL, buffer, bytes);

			// TODO Modification date, author, etc.
		}
	}

	EsMenuAddSeparator(menu);

	if (instance->instanceClass == ES_INSTANCE_CLASS_EDITOR) {
		if (instance->commandSave.disabled) {
			EsMenuAddItem(menu, ES_ELEMENT_DISABLED, INTERFACE_STRING(CommonFileUnchanged));
		} else {
			EsMenuAddCommand(menu, ES_FLAGS_DEFAULT, INTERFACE_STRING(CommonFileSave), &instance->commandSave);
		}

		EsMenuAddItem(menu, newDocument ? ES_ELEMENT_DISABLED : ES_FLAGS_DEFAULT, INTERFACE_STRING(CommonFileMakeCopy)); // TODO.
		EsMenuAddSeparator(menu);
	}

	EsMenuAddItem(menu, newDocument ? ES_ELEMENT_DISABLED : ES_FLAGS_DEFAULT, INTERFACE_STRING(CommonFileShare)); // TODO.
	EsMenuAddItem(menu, newDocument ? ES_ELEMENT_DISABLED : ES_FLAGS_DEFAULT, INTERFACE_STRING(CommonFileVersionHistory)); // TODO.
	EsMenuAddCommand(menu, ES_FLAGS_DEFAULT, INTERFACE_STRING(CommonFileShowInFileManager), &instance->commandShowInFileManager);
	EsMenuShow(menu);
}

void EsToolbarAddFileMenu(EsElement *element, const EsFileMenuSettings *settings) {
	EsButton *button = EsButtonCreate(element, ES_BUTTON_DROPDOWN, 0, INTERFACE_STRING(CommonFileMenu));
	button->accessKey = 'F';
	button->userData = (void *) settings;
	EsButtonOnCommand(button, FileMenuCreate);
}

// --------------------------------- Paint targets.

bool EsPaintTargetTake(EsPaintTarget *target, size_t width, size_t height, bool hasAlphaChannel = true) {
	EsMemoryZero(target, sizeof(EsPaintTarget));
	target->fullAlpha = hasAlphaChannel;
	target->width = width;
	target->height = height;
	target->stride = width * 4;
	target->bits = EsHeapAllocate(target->stride * target->height, true);
	return target->bits != nullptr;
}

EsPaintTarget *EsPaintTargetCreate(size_t width, size_t height, bool hasAlphaChannel) {
	EsPaintTarget *target = (EsPaintTarget *) EsHeapAllocate(sizeof(EsPaintTarget), true);

	if (!target) {
		return nullptr;
	} else if (EsPaintTargetTake(target, width, height, hasAlphaChannel)) {
		return target;
	} else {
		EsHeapFree(target);
		return nullptr;
	}
}

EsPaintTarget *EsPaintTargetCreateFromBitmap(uint32_t *bits, size_t width, size_t height, bool hasAlphaChannel) {
	EsPaintTarget *target = (EsPaintTarget *) EsHeapAllocate(sizeof(EsPaintTarget), true);

	if (!target) {
		return nullptr;
	}

	target->bits = bits;
	target->width = width;
	target->height = height;
	target->stride = width * 4;
	target->fullAlpha = hasAlphaChannel;
	target->fromBitmap = true;
	return target;
}

void EsPaintTargetClear(EsPaintTarget *t) {
	EsPainter painter = {};
	painter.clip.r = t->width;
	painter.clip.b = t->height;
	painter.target = t;
	EsDrawClear(&painter, painter.clip);
}

void EsPaintTargetReturn(EsPaintTarget *target) {
	EsHeapFree(target->bits);
}

void EsPaintTargetDestroy(EsPaintTarget *target) {
	if (!target->fromBitmap) EsHeapFree(target->bits);
	EsHeapFree(target);
}

void EsPaintTargetEndDirectAccess(EsPaintTarget *target) {
	// Don't need to do anything, currently.
	(void) target;
}

void EsPaintTargetGetSize(EsPaintTarget *target, size_t *width, size_t *height) {
	if (width) *width = target->width;
	if (height) *height = target->height;
}

void EsPaintTargetStartDirectAccess(EsPaintTarget *target, uint32_t **bits, size_t *width, size_t *height, size_t *stride) {
	if (bits) *bits = (uint32_t *) target->bits;
	if (width) *width = target->width;
	if (height) *height = target->height;
	if (stride) *stride = target->stride;
}

// --------------------------------- Transitions.

EsRectangle UIGetTransitionEffectRectangle(EsRectangle bounds, EsTransitionType type, double progress, bool to) {
	int width = Width(bounds), height = Height(bounds);
	double ratio = (double) height / (double) width;

	if (!to) {
		if (type == ES_TRANSITION_SLIDE_UP) {
			return ES_RECT_4(bounds.l, bounds.r, 
					bounds.t - progress * height / 2, bounds.b - progress * height / 2);
		} else if (type == ES_TRANSITION_SLIDE_DOWN) {
			return ES_RECT_4(bounds.l, bounds.r, 
					bounds.t + progress * height / 2, bounds.b + progress * height / 2); 
		} else if (type == ES_TRANSITION_COVER_UP) {
			return ES_RECT_4(bounds.l, bounds.r, 
					bounds.t, bounds.b);
		} else if (type == ES_TRANSITION_COVER_DOWN) {
			return ES_RECT_4(bounds.l, bounds.r, 
					bounds.t, bounds.b);
		} else if (type == ES_TRANSITION_SQUISH_UP || type == ES_TRANSITION_REVEAL_UP) {
			return ES_RECT_4(bounds.l, bounds.r, 
					bounds.t, bounds.t + height * (1 - progress));
		} else if (type == ES_TRANSITION_SQUISH_DOWN || type == ES_TRANSITION_REVEAL_DOWN) {
			return ES_RECT_4(bounds.l, bounds.r, 
					bounds.t + height * progress, bounds.b);
		} else if (type == ES_TRANSITION_ZOOM_OUT) {
			return ES_RECT_4(bounds.l + 20 * progress, bounds.r - 20 * progress, 
					bounds.t + 20 * progress * ratio, bounds.b - 20 * progress * ratio);
		} else if (type == ES_TRANSITION_ZOOM_IN) {
			return ES_RECT_4(bounds.l - 20 * progress, bounds.r + 20 * progress, 
					bounds.t - 20 * progress * ratio, bounds.b + 20 * progress * ratio);
		} else if (type == ES_TRANSITION_ZOOM_OUT_LIGHT) {
			return ES_RECT_4(bounds.l + 5 * progress, bounds.r - 5 * progress, 
					bounds.t + 5 * progress * ratio, bounds.b - 5 * progress * ratio);
		} else if (type == ES_TRANSITION_ZOOM_IN_LIGHT) {
			return ES_RECT_4(bounds.l - 5 * progress, bounds.r + 5 * progress, 
					bounds.t - 5 * progress * ratio, bounds.b + 5 * progress * ratio);
		} else if (type == ES_TRANSITION_FADE_IN || type == ES_TRANSITION_FADE_OUT) {
			return bounds;
		} else if (type == ES_TRANSITION_SLIDE_UP_OVER) {
			return ES_RECT_4(bounds.l, bounds.r, 
					bounds.t - progress * height / 4, bounds.b - progress * height / 4);
		} else if (type == ES_TRANSITION_SLIDE_DOWN_OVER) {
			return ES_RECT_4(bounds.l, bounds.r, 
					bounds.t + progress * height / 4, bounds.b + progress * height / 4); 
		} else if (type == ES_TRANSITION_SLIDE_UP_UNDER) {
			return ES_RECT_4(bounds.l, bounds.r, 
					bounds.t - progress * height / 2, bounds.b - progress * height / 2);
		} else if (type == ES_TRANSITION_SLIDE_DOWN_UNDER) {
			return ES_RECT_4(bounds.l, bounds.r, 
					bounds.t + progress * height / 2, bounds.b + progress * height / 2); 
		}
	} else {
		if (type == ES_TRANSITION_SLIDE_UP) {
			return ES_RECT_4(bounds.l, bounds.r, 
					bounds.t + (1 - progress) * height / 2, bounds.b + (1 - progress) * height / 2); 
		} else if (type == ES_TRANSITION_SLIDE_DOWN) {
			return ES_RECT_4(bounds.l, bounds.r, 
					bounds.t - (1 - progress) * height / 2, bounds.b - (1 - progress) * height / 2); 
		} else if (type == ES_TRANSITION_COVER_UP) {
			return ES_RECT_4(bounds.l, bounds.r, 
					bounds.t + (1 - progress) * height, bounds.b + (1 - progress) * height); 
		} else if (type == ES_TRANSITION_COVER_DOWN) {
			return ES_RECT_4(bounds.l, bounds.r, 
					bounds.t - (1 - progress) * height, bounds.b - (1 - progress) * height); 
		} else if (type == ES_TRANSITION_SQUISH_UP || type == ES_TRANSITION_REVEAL_UP) {
			return ES_RECT_4(bounds.l, bounds.r, 
					bounds.t + (1 - progress) * height, bounds.b); 
		} else if (type == ES_TRANSITION_SQUISH_DOWN || type == ES_TRANSITION_REVEAL_DOWN) {
			return ES_RECT_4(bounds.l, bounds.r, 
					bounds.t, bounds.t + progress * height); 
		} else if (type == ES_TRANSITION_ZOOM_OUT) {
			return ES_RECT_4(bounds.l - 20 * (1 - progress), bounds.r + 20 * (1 - progress), 
					bounds.t - 20 * (1 - progress) * ratio, bounds.b + 20 * (1 - progress) * ratio);
		} else if (type == ES_TRANSITION_ZOOM_IN) {
			return ES_RECT_4(bounds.l + 20 * (1 - progress), bounds.r - 20 * (1 - progress) + 0.5, 
					bounds.t + 20 * (1 - progress) * ratio, bounds.b - 20 * (1 - progress) * ratio + 0.5);
		} else if (type == ES_TRANSITION_ZOOM_OUT_LIGHT) {
			return ES_RECT_4(bounds.l - 5 * (1 - progress), bounds.r + 5 * (1 - progress), 
					bounds.t - 5 * (1 - progress) * ratio, bounds.b + 5 * (1 - progress) * ratio);
		} else if (type == ES_TRANSITION_ZOOM_IN_LIGHT) {
			return ES_RECT_4(bounds.l + 5 * (1 - progress), bounds.r - 5 * (1 - progress) + 0.5, 
					bounds.t + 5 * (1 - progress) * ratio, bounds.b - 5 * (1 - progress) * ratio + 0.5);
		} else if (type == ES_TRANSITION_FADE_IN || type == ES_TRANSITION_FADE_OUT) {
			return bounds;
		} else if (type == ES_TRANSITION_SLIDE_UP_OVER) {
			return ES_RECT_4(bounds.l, bounds.r, 
					bounds.t + (1 - progress) * height / 2, bounds.b + (1 - progress) * height / 2); 
		} else if (type == ES_TRANSITION_SLIDE_DOWN_OVER) {
			return ES_RECT_4(bounds.l, bounds.r, 
					bounds.t - (1 - progress) * height / 2, bounds.b - (1 - progress) * height / 2); 
		} else if (type == ES_TRANSITION_SLIDE_UP_UNDER) {
			return ES_RECT_4(bounds.l, bounds.r, 
					bounds.t + (1 - progress) * height / 4, bounds.b + (1 - progress) * height / 4); 
		} else if (type == ES_TRANSITION_SLIDE_DOWN_UNDER) {
			return ES_RECT_4(bounds.l, bounds.r, 
					bounds.t - (1 - progress) * height / 4, bounds.b - (1 - progress) * height / 4); 
		}
	}

	EsAssert(false); // Unknown transition type.
	return {};
}

void UIDrawTransitionEffect(EsPainter *painter, EsPaintTarget *sourceSurface, EsRectangle bounds, EsTransitionType type, double progress, bool to) {
	EsRectangle destinationRegion = UIGetTransitionEffectRectangle(bounds, type, progress, to);
	EsRectangle sourceRegion = ES_RECT_4(0, bounds.r - bounds.l, 0, bounds.b - bounds.t);
	uint16_t alpha = (to ? progress : (1 - progress)) * 255;
	EsDrawPaintTarget(painter, sourceSurface, destinationRegion, sourceRegion, alpha);
}

void EsElementStartTransition(EsElement *element, EsTransitionType transitionType, uint32_t flags, float timeMultiplier) {
	uint32_t durationMs = timeMultiplier * GetConstantNumber("transitionTime");

	if (!durationMs) {
		return;
	}

	if (element->previousTransitionFrame) {
		EsPaintTargetDestroy(element->previousTransitionFrame);
		element->previousTransitionFrame = nullptr;
	}

	if (~flags & ES_ELEMENT_TRANSITION_ENTRANCE) {
		EsRectangle paintOutsets = element->currentStyle->paintOutsets;
		int width = element->width + paintOutsets.l + paintOutsets.r;
		int height = element->height + paintOutsets.t + paintOutsets.b;

		element->previousTransitionFrame = EsPaintTargetCreate(width, height, true);

		if (element->previousTransitionFrame) {
			EsPainter painter = {};
			painter.clip = ES_RECT_4(0, width, 0, height);
			painter.offsetX = paintOutsets.l;
			painter.offsetY = paintOutsets.t;
			painter.target = element->previousTransitionFrame;
			element->InternalPaint(&painter, PAINT_NO_TRANSITION | PAINT_NO_OFFSET);
		}
	}

	element->transitionTimeMs = 0;
	element->transitionDurationMs = durationMs;
	element->transitionType = transitionType;
	element->StartAnimating();
}

// --------------------------------- Painting.

EsRectangle EsPainterBoundsClient(EsPainter *painter) {
	return ES_RECT_4(painter->offsetX, painter->offsetX + painter->width, painter->offsetY, painter->offsetY + painter->height);
}

EsRectangle EsPainterBoundsInset(EsPainter *painter) {
	UIStyle *style = (UIStyle *) painter->style;
	return ES_RECT_4(painter->offsetX + style->insets.l, painter->offsetX + painter->width - style->insets.r, 
			painter->offsetY + style->insets.t, painter->offsetY + painter->height - style->insets.b);
}

void EsElement::Repaint(bool all, EsRectangle region) {
	// TODO Optimisation: don't paint if overlapped by an opaque child or sibling.

	if (all) {
		region.l = -currentStyle->paintOutsets.l, region.r =  width + currentStyle->paintOutsets.r;
		region.t = -currentStyle->paintOutsets.t, region.b = height + currentStyle->paintOutsets.b;
	} else {
		region = Translate(region, -internalOffsetLeft, -internalOffsetTop);
	}

	if (parent) {
		EsRectangle parentBounds = parent->GetWindowBounds(false);

		region = Translate(region, offsetX + parentBounds.l, offsetY + parentBounds.t);

		if (parent->currentStyle->metrics->clipEnabled) {
			Rectangle16 clipInsets = parent->currentStyle->metrics->clipInsets;
			region = EsRectangleIntersection(region, EsRectangleAddBorder(parentBounds, RECT16_TO_RECT(clipInsets)));
		}

		UIWindowNeedsUpdate(window);
	}

	if (THEME_RECT_VALID(region)) {
		window->updateRegion = EsRectangleBounding(window->updateRegion, region);
	}
}

void EsElement::InternalPaint(EsPainter *painter, int paintFlags) {
	if (width <= 0 || height <= 0 || (flags & ES_ELEMENT_HIDDEN)) {
		return;
	}

	state |= UI_STATE_ENTERED;

	int pOffsetX = painter->offsetX;
	int pOffsetY = painter->offsetY;

	if (~paintFlags & PAINT_NO_OFFSET) {
		pOffsetX += offsetX;
		pOffsetY += offsetY;
	}

	// Is it possible for this element to paint within the clip?

	{
		EsRectangle area; 
		area.l = pOffsetX - currentStyle->paintOutsets.l;
		area.r = pOffsetX + width + currentStyle->paintOutsets.r;
		area.t = pOffsetY - currentStyle->paintOutsets.t;
		area.b = pOffsetY + height + currentStyle->paintOutsets.b;

		if (!THEME_RECT_VALID(EsRectangleIntersection(area, painter->clip))) {
			return;
		}
	}

	if (state & UI_STATE_INSPECTING) EsPerformanceTimerPush();
	double timeChildPaint = 0;

	// Get the interpolated style.

	EsPainter oldPainter = *painter;

	UIStyle *style = ThemeAnimationComplete(&animation) ? currentStyle : ThemeStyleInterpolate(currentStyle, &animation);
	EsDefer(if (style != currentStyle) EsHeapFree(style));
	painter->style = style;

	painter->offsetX = pOffsetX, painter->offsetY = pOffsetY;
	painter->width = width, painter->height = height;

	// Get the child type of the element.

	int childType = 0;

	if (parent && parent->GetChildCount()) {
		if (parent->GetChildCount() == 1 && parent->GetChild(0) == this) {
			childType = THEME_CHILD_TYPE_ONLY;
		} else if (parent->GetChild(0) == this) {
			childType = (parent->flags & ES_ELEMENT_LAYOUT_HINT_REVERSE) ? THEME_CHILD_TYPE_LAST : THEME_CHILD_TYPE_FIRST;
		} else if (parent->GetChild(parent->GetChildCount() - 1) == this) {
			childType = (parent->flags & ES_ELEMENT_LAYOUT_HINT_REVERSE) ? THEME_CHILD_TYPE_FIRST : THEME_CHILD_TYPE_LAST;
		} else {
			childType = THEME_CHILD_TYPE_NONE;
		}

		if (parent->flags & ES_ELEMENT_LAYOUT_HINT_HORIZONTAL) {
			childType |= THEME_CHILD_TYPE_HORIZONTAL;
		}
	}

	if (paintFlags & PAINT_SHADOW) {
		style->PaintLayers(painter, ES_RECT_2S(painter->width, painter->height), childType, THEME_LAYER_MODE_SHADOW);
	} else if (paintFlags & PAINT_OVERLAY) {
		style->PaintLayers(painter, ES_RECT_2S(painter->width, painter->height), childType, THEME_LAYER_MODE_OVERLAY);

		// Paint layout bounds, if active.

		if (window->visualizeLayoutBounds) {
			EsDrawRectangle(painter, EsPainterBoundsClient(painter), 0, 0x7FFF0000, ES_RECT_1(1));
			EsDrawRectangle(painter, EsPainterBoundsInset(painter), 0, 0x7F0000FF, ES_RECT_1(1));
		}
	} else if (transitionTimeMs < transitionDurationMs && (~paintFlags & PAINT_NO_TRANSITION)) {
		double progress = SmoothAnimationTime((double) transitionTimeMs / (double) transitionDurationMs);
		EsRectangle bounds = EsPainterBoundsClient(painter);
		EsPaintTarget target;

		EsRectangle paintOutsets = currentStyle->paintOutsets;
		int targetWidth = width + paintOutsets.l + paintOutsets.r;
		int targetHeight = height + paintOutsets.t + paintOutsets.b;
		bounds.l -= paintOutsets.l, bounds.r += paintOutsets.r;
		bounds.t -= paintOutsets.t, bounds.b += paintOutsets.b;

		if (EsPaintTargetTake(&target, targetWidth, targetHeight)) {
			if (previousTransitionFrame) {
				UIDrawTransitionEffect(painter, previousTransitionFrame, bounds, 
						(EsTransitionType) transitionType, progress, false);
			}

			EsPainter p = {};
			p.clip = ES_RECT_4(0, targetWidth, 0, targetHeight);
			p.offsetX = paintOutsets.l;
			p.offsetY = paintOutsets.t;
			p.target = &target;
			InternalPaint(&p, PAINT_NO_TRANSITION | PAINT_NO_OFFSET);

			UIDrawTransitionEffect(painter, &target, bounds, (EsTransitionType) transitionType, progress, true);

			EsPaintTargetReturn(&target);
		} else {
			goto paintBackground;
		}
	} else {
		paintBackground:;

		// Paint the background.

		EsMessage m;
		m.type = ES_MSG_PAINT_BACKGROUND;
		m.painter = painter;

		if (!EsMessageSend(this, &m)) {
			// TODO Optimisation: don't paint if overlapped by an opaque child.
			style->PaintLayers(painter, ES_RECT_2S(painter->width, painter->height), childType, THEME_LAYER_MODE_BACKGROUND);
		}
		
		// Apply the clipping insets.

		EsRectangle oldClip = painter->clip;

		if (currentStyle->metrics->clipEnabled && (~flags & ES_ELEMENT_NO_CLIP)) {
			Rectangle16 insets = currentStyle->metrics->clipInsets;
			EsRectangle content = ES_RECT_4(painter->offsetX + insets.l, painter->offsetX + width - insets.r, 
					painter->offsetY + insets.t, painter->offsetY + height - insets.b);
			painter->clip = EsRectangleIntersection(content, painter->clip);
		}

		if (THEME_RECT_VALID(painter->clip)) {
			// Paint the content.

			painter->width -= internalOffsetLeft + internalOffsetRight;
			painter->height -= internalOffsetTop + internalOffsetBottom;
			painter->offsetX += internalOffsetLeft, painter->offsetY += internalOffsetTop;

			m.type = ES_MSG_PAINT;
			m.painter = painter;
			EsMessageSend(this, &m);

			painter->width += internalOffsetLeft + internalOffsetRight;
			painter->height += internalOffsetTop + internalOffsetBottom;
			painter->offsetX -= internalOffsetLeft, painter->offsetY -= internalOffsetTop;

			// Paint the children.
			// TODO Optimisation: don't paint children overlapped by an opaque sibling.

			if (state & UI_STATE_INSPECTING) EsPerformanceTimerPush();

			m.type = ES_MSG_PAINT_CHILDREN;
			m.painter = painter;

			if (!EsMessageSend(this, &m)) {
				bool isZStack = state & UI_STATE_Z_STACK;

				EsMessage zOrder;
				zOrder.type = ES_MSG_BEFORE_Z_ORDER;
				zOrder.beforeZOrder.start = 0;
				zOrder.beforeZOrder.nonClient = zOrder.beforeZOrder.end = children.Length();
				zOrder.beforeZOrder.clip = Translate(painter->clip, -painter->offsetX, -painter->offsetY);
				EsMessageSend(this, &zOrder);

				if (isZStack) {
					// Elements cast shadows on each other.

					for (uintptr_t i = zOrder.beforeZOrder.start; i < zOrder.beforeZOrder.end; i++) {
						EsElement *child = GetChildByZ(i);
						if (!child) continue;
						child->InternalPaint(painter, PAINT_SHADOW);
						child->InternalPaint(painter, ES_FLAGS_DEFAULT);
						child->InternalPaint(painter, PAINT_OVERLAY);
					}

					for (uintptr_t i = zOrder.beforeZOrder.nonClient; i < children.Length(); i++) {
						EsElement *child = GetChildByZ(i);
						if (!child) continue;
						child->InternalPaint(painter, PAINT_SHADOW);
						child->InternalPaint(painter, ES_FLAGS_DEFAULT);
						child->InternalPaint(painter, PAINT_OVERLAY);
					}
				} else {
					// Elements cast shadows on the container.

					for (uintptr_t i = zOrder.beforeZOrder.start; i < zOrder.beforeZOrder.end; i++) {
						EsElement *child = GetChildByZ(i);
						if (child) child->InternalPaint(painter, PAINT_SHADOW);
					}

					for (uintptr_t i = zOrder.beforeZOrder.nonClient; i < children.Length(); i++) {
						EsElement *child = GetChildByZ(i);
						if (child) child->InternalPaint(painter, PAINT_SHADOW);
					}

					for (uintptr_t i = zOrder.beforeZOrder.start; i < zOrder.beforeZOrder.end; i++) {
						EsElement *child = GetChildByZ(i);
						if (child) child->InternalPaint(painter, ES_FLAGS_DEFAULT);
					}

					for (uintptr_t i = zOrder.beforeZOrder.nonClient; i < children.Length(); i++) {
						EsElement *child = GetChildByZ(i);
						if (child) child->InternalPaint(painter, ES_FLAGS_DEFAULT);
					}

					for (uintptr_t i = zOrder.beforeZOrder.start; i < zOrder.beforeZOrder.end; i++) {
						EsElement *child = GetChildByZ(i);
						if (child) child->InternalPaint(painter, PAINT_OVERLAY);
					}

					for (uintptr_t i = zOrder.beforeZOrder.nonClient; i < children.Length(); i++) {
						EsElement *child = GetChildByZ(i);
						if (child) child->InternalPaint(painter, PAINT_OVERLAY);
					}
				}

				zOrder.type = ES_MSG_AFTER_Z_ORDER;
				EsMessageSend(this, &zOrder);
			}

			if (state & UI_STATE_INSPECTING) timeChildPaint = EsPerformanceTimerPop() * 1000;
		}

		// Let the inspector draw some decorations over the element.

		painter->clip = oldClip;
		InspectorNotifyElementPainted(this, painter);
	}

	if (state & UI_STATE_INSPECTING) {
		double timeTotalPaint = EsPerformanceTimerPop() * 1000;
		InspectorNotifyElementEvent(this, "paint", "Paint in %Fms (%Fms with children).", timeTotalPaint - timeChildPaint, timeTotalPaint);
	}

	*painter = oldPainter;

	if (window->visualizePaintSteps && ES_RECT_VALID(window->updateRegionInProgress) && painter->target->forWindowManager) {
		EsSyscall(ES_SYSCALL_WINDOW_SET_BITS, window->handle, (uintptr_t) &window->updateRegionInProgress, 
				(uintptr_t) painter->target->bits, WINDOW_SET_BITS_NORMAL);
	}
}

// --------------------------------- Animations.

bool EsElement::StartAnimating() {
	if ((state & UI_STATE_ANIMATING) || (state & UI_STATE_DESTROYING)) return false;
	gui.animatingElements.Add(this);
	gui.animationSleep = false;
	state |= UI_STATE_ANIMATING;
	lastTimeStamp = 0;
	return true;
}

void ProcessAnimations() {
	uint64_t timeStamp = EsTimeStamp(); // TODO Use global time instead.
	int64_t waitMs = -1;

	for (uintptr_t i = 0; i < gui.animatingElements.Length(); i++) {
		EsElement *element = gui.animatingElements[i];
		// EsPrint("Animating %x...\n", element);
		EsAssert(element->state & UI_STATE_ANIMATING); // Element was not animating but was in the animating elements list.

		if (element->lastTimeStamp == 0) {
			element->lastTimeStamp = timeStamp;
		}

		EsMessage m = {};
		m.type = ES_MSG_ANIMATE;
		int64_t deltaUs = (timeStamp - element->lastTimeStamp) / api.systemConstants[ES_SYSTEM_CONSTANT_TIME_STAMP_UNITS_PER_MICROSECOND];
		m.animate.deltaMs = deltaUs / 1000;
		m.animate.complete = true;

		if (!m.animate.deltaMs) {
			waitMs = 0;
			continue;
		}

		if (ThemeAnimationStep(&element->animation, m.animate.deltaMs)) {
			element->Repaint(true, ES_RECT_1(0));
		}

		element->transitionTimeMs += m.animate.deltaMs;
		bool transitionComplete = element->transitionTimeMs >= element->transitionDurationMs;

		if (!transitionComplete) {
			element->Repaint(true, ES_RECT_1(0));
		}

		bool backgroundAnimationComplete = ThemeAnimationComplete(&element->animation);

		EsMessageSend(element, &m);

		if (m.animate.complete && backgroundAnimationComplete && transitionComplete) {
			gui.animatingElements.DeleteSwap(i);
			element->state &= ~UI_STATE_ANIMATING;
			i--;

			if (element->state & UI_STATE_EXITING) {
				EsElement *ancestor = element;

				while (ancestor) {
					ancestor->state |= UI_STATE_DESTROYING_CHILD;
					ancestor = ancestor->parent;
				}

				element->state &= ~UI_STATE_EXITING;
			}
		} else if (m.animate.waitMs < waitMs || waitMs == -1) {
			waitMs = m.animate.waitMs;
		}

		element->lastTimeStamp += m.animate.deltaMs * 1000 * api.systemConstants[ES_SYSTEM_CONSTANT_TIME_STAMP_UNITS_PER_MICROSECOND];
		UIWindowNeedsUpdate(element->window);
	}

	if (waitMs > 0) {
		gui.animationSleep = true;

		EsTimerCancel(gui.animationSleepTimer);
		gui.animationSleepTimer = EsTimerSet(waitMs, [] (EsGeneric) { gui.animationSleep = false; }, nullptr);
	}
}

// --------------------------------- Style state management.
// TODO Move into theme.cpp?

bool EsElement::RefreshStyleState() {
	uint16_t styleStateFlags = customStyleState;

	if (flags & ES_ELEMENT_DISABLED) {
		styleStateFlags |= THEME_PRIMARY_STATE_DISABLED;
	} else {
		if (((state & UI_STATE_LEFT_PRESSED) && ((state & UI_STATE_HOVERED) || gui.draggingStarted || (state & UI_STATE_STRONG_PRESSED))) 
				|| (state & UI_STATE_MENU_SOURCE)) {
			styleStateFlags |= THEME_PRIMARY_STATE_PRESSED;
		} else if (((state & UI_STATE_HOVERED) && !window->pressed) || (window && window->pressed == this)) {
			styleStateFlags |= THEME_PRIMARY_STATE_HOVERED;
		} else {
			styleStateFlags |= THEME_PRIMARY_STATE_IDLE;
		}

		if (state & UI_STATE_FOCUSED) {
			styleStateFlags |= THEME_STATE_FOCUSED;
		}
	}

	if (state & UI_STATE_EXITING) {
		styleStateFlags |= THEME_STATE_AFTER_EXIT;
	}

	bool observedBitsChanged = false;

	if (!currentStyle || currentStyle->IsStateChangeObserved(styleStateFlags, previousStyleState)) {
		observedBitsChanged = true;
	}

	previousStyleState = styleStateFlags;

	return observedBitsChanged;
}

void EsElement::RefreshStyle(UIStyleKey *_oldStyleKey, bool alreadyRefreshStyleState, bool force) {
	// Compute state flags.

	if (!alreadyRefreshStyleState) {
		RefreshStyleState();
	}

	uint16_t styleStateFlags = previousStyleState;

	// Initialise the style.

	UIStyleKey oldStyleKey = _oldStyleKey ? *_oldStyleKey : currentStyleKey;
	currentStyleKey.stateFlags = styleStateFlags;

	if (!force && 0 == EsMemoryCompare(&currentStyleKey, &oldStyleKey, sizeof(UIStyleKey)) && currentStyle) {
		return;
	}

	if (~state & UI_STATE_ENTERED) {
		if (currentStyle) currentStyle->CloseReference();
		oldStyleKey = currentStyleKey;
		oldStyleKey.stateFlags |= THEME_STATE_BEFORE_ENTER;
		currentStyle = GetStyle(oldStyleKey, false);
	}

	UIStyle *oldStyle = currentStyle;
	currentStyle = GetStyle(currentStyleKey, false); // TODO Forcing new styles if force flag set.

	// Respond to modifications.

	bool repaint = false, animate = false;

	if (force) {
		repaint = true;
	}

	if (oldStyle) {
		if (oldStyle->style == currentStyle->style) {
			ThemeAnimationBuild(&animation, oldStyle, oldStyleKey.stateFlags, currentStyleKey.stateFlags);
			animate = !ThemeAnimationComplete(&animation);
		} else {
			ThemeAnimationDestroy(&animation);
		}

		repaint = true;
	} else {
		repaint = animate = true;
	}

	if (repaint) {
		if (animate) StartAnimating();
		Repaint(true, ES_RECT_1(0));
	}

	// Delete the old style if necessary.

	if (oldStyle) {
		oldStyle->CloseReference();
	}
}

void EsElement::SetStyle(const EsStyle *part, bool refreshIfChanged) {
	UIStyleKey oldStyleKey = currentStyleKey;
	currentStyleKey.part = (uintptr_t) part;

	if (currentStyleKey.part != oldStyleKey.part && refreshIfChanged) {
		RefreshStyle(&oldStyleKey);
	}
}

// --------------------------------- Layouting.

EsRectangle LayoutCell(EsElement *element, int width, int height) {
	uint64_t layout = element->flags;

	int maximumWidth  = element->currentStyle->metrics->maximumWidth  ?: ES_PANEL_BAND_SIZE_DEFAULT;
	int minimumWidth  = element->currentStyle->metrics->minimumWidth  ?: ES_PANEL_BAND_SIZE_DEFAULT;
	int maximumHeight = element->currentStyle->metrics->maximumHeight ?: ES_PANEL_BAND_SIZE_DEFAULT;
	int minimumHeight = element->currentStyle->metrics->minimumHeight ?: ES_PANEL_BAND_SIZE_DEFAULT;

	if (layout & ES_CELL_H_EXPAND) maximumWidth = INT_MAX;
	if (layout & ES_CELL_H_SHRINK) minimumWidth = 0;
	if (layout & ES_CELL_V_EXPAND) maximumHeight = INT_MAX;
	if (layout & ES_CELL_V_SHRINK) minimumHeight = 0;

	if (maximumWidth == ES_PANEL_BAND_SIZE_DEFAULT || minimumWidth == ES_PANEL_BAND_SIZE_DEFAULT) {
		int width = element->GetWidth(height);
		if (maximumWidth == ES_PANEL_BAND_SIZE_DEFAULT) maximumWidth = width;
		if (minimumWidth == ES_PANEL_BAND_SIZE_DEFAULT) minimumWidth = width;
	}

	int preferredWidth = ClampInteger(minimumWidth,  maximumWidth,  width);

	if (maximumHeight == ES_PANEL_BAND_SIZE_DEFAULT || minimumHeight == ES_PANEL_BAND_SIZE_DEFAULT) {
		int height = element->GetHeight(preferredWidth);
		if (maximumHeight == ES_PANEL_BAND_SIZE_DEFAULT) maximumHeight = height;
		if (minimumHeight == ES_PANEL_BAND_SIZE_DEFAULT) minimumHeight = height;
	}

	int preferredHeight = ClampInteger(minimumHeight, maximumHeight, height);

	EsRectangle bounds = ES_RECT_4(0, width, 0, height);

	if ((layout & (ES_CELL_H_LEFT | ES_CELL_H_RIGHT)) == ES_CELL_H_LEFT) {
		bounds.r = bounds.l + preferredWidth;
	} else if ((layout & (ES_CELL_H_LEFT | ES_CELL_H_RIGHT)) == ES_CELL_H_RIGHT) {
		bounds.l = bounds.r - preferredWidth;
	} else {
		bounds.l = bounds.l + width / 2 - preferredWidth / 2;
		bounds.r = bounds.l + preferredWidth;
	}

	if ((layout & (ES_CELL_V_TOP | ES_CELL_V_BOTTOM)) == ES_CELL_V_TOP) {
		bounds.b = bounds.t + preferredHeight;
	} else if ((layout & (ES_CELL_V_TOP | ES_CELL_V_BOTTOM)) == ES_CELL_V_BOTTOM) {
		bounds.t = bounds.b - preferredHeight;
	} else {
		bounds.t = bounds.t + height / 2 - preferredHeight / 2;
		bounds.b = bounds.t + preferredHeight;
	}

	return bounds;
}

void EsElementMove(EsElement *element, int x, int y, int width, int height, bool applyCellLayout) {
	EsMessageMutexCheck();

	if (applyCellLayout) {
		EsRectangle bounds = LayoutCell(element, width, height);
		width = Width(bounds), height = Height(bounds);
		x += bounds.l, y += bounds.t;
	}

	element->InternalMove(width, height, x, y);
}

void PanelMoveChild(EsElement *element, int width, int height, int offsetX, int offsetY) {
	EsPanel *panel = (EsPanel *) element->parent;

	{
		EsRectangle bounds = LayoutCell(element, width, height);
		width = Width(bounds), height = Height(bounds);
		offsetX += bounds.l, offsetY += bounds.t;
	}

	float progress = panel->transitionLengthMs ? SmoothAnimationTime((float) panel->transitionTimeMs / panel->transitionLengthMs) : 1;

	// TODO Make this faster than O(n^2).

	for (uintptr_t i = 0; i < panel->movementItems.Length(); i++) {
		PanelMovementItem *item = &panel->movementItems[i];

		if (item->element != element) {
			continue;
		}

		if (item->wasHidden) {
			break;
		} else {
			int oldWidth = Width(item->oldBounds);
			int oldHeight = Height(item->oldBounds);
			int oldOffsetX = item->oldBounds.l;
			int oldOffsetY = item->oldBounds.t;

			element->InternalMove(LinearInterpolate(oldWidth, width, progress),
					LinearInterpolate(oldHeight, height, progress),
					LinearInterpolate(oldOffsetX, offsetX, progress),
					LinearInterpolate(oldOffsetY, offsetY, progress));
		}

		return;
	}

	element->InternalMove(width, height, offsetX, offsetY);
}

void LayoutTable(EsPanel *panel, EsMessage *message) {
	bool debug = false;

	bool has[2] = {};
	int in[2] = {};
	int out[2] = {};

	if (message->type == ES_MSG_GET_WIDTH) {
		in[1] = message->measure.height;
		has[1] = has[1];
	} else if (message->type == ES_MSG_GET_HEIGHT) {
		in[0] = message->measure.width;
		has[0] = in[0];
	} else {
		EsRectangle bounds = panel->GetBounds();
		in[0] = bounds.r, in[1] = bounds.b;
		has[0] = has[1] = true;
	}

	// if (debug) EsPrint("LayoutTable, %z, %d/%d\n", isMeasure ? "measure" : "layout", in[0], in[1]);

	size_t childCount = panel->GetChildCount();

	uint8_t *memoryBase = (uint8_t *) EsHeapAllocate(sizeof(int) * childCount * 2 + sizeof(EsPanelBand) * (panel->bandCount[0] + panel->bandCount[1]), true), *memory = memoryBase;

	int *calculatedSize[2];
	calculatedSize[0] = (int *) memory; memory += sizeof(int) * childCount;
	calculatedSize[1] = (int *) memory; memory += sizeof(int) * childCount;
	EsPanelBand *calculatedProperties[2];
	calculatedProperties[0] = (EsPanelBand *) memory; memory += sizeof(EsPanelBand) * panel->bandCount[0];
	calculatedProperties[1] = (EsPanelBand *) memory; memory += sizeof(EsPanelBand) * panel->bandCount[1];

	for (int axis = 0; axis < 2; axis++) {
		if (panel->bands[axis]) {
			EsMemoryCopy(calculatedProperties[axis], panel->bands[axis], sizeof(EsPanelBand) * panel->bandCount[axis]);
		} else {
			for (uintptr_t i = 0; i < panel->bandCount[axis]; i++) {
				calculatedProperties[axis][i].preferredSize 
					= calculatedProperties[axis][i].maximumSize 
					= calculatedProperties[axis][i].minimumSize 
					= ES_PANEL_BAND_SIZE_DEFAULT;
			}
		}
	}

	EsRectangle insets = panel->GetInsets();

#define TABLE_AXIS_HORIZONTAL (0)
#define TABLE_AXIS_VERTICAL (1)

	for (int _axis = 0; _axis < 2; _axis++) {
		int axis = (~panel->flags & ES_PANEL_HORIZONTAL) ? (1 - _axis) : _axis;
		int gapSize = _axis ? panel->GetGapMinor() : panel->GetGapMajor();
		int insetStart = axis ? insets.t : insets.l;
		int insetEnd = axis ? insets.b : insets.r;

		if (debug) EsPrint("\tAxis %d\n", axis);

		for (uintptr_t i = 0; i < childCount; i++) {
			EsElement *child = panel->GetChild(i);
			if (child->flags & (ES_ELEMENT_HIDDEN | ES_ELEMENT_NON_CLIENT)) continue;

			// Step 1: Find the preferred size of the children for this axis.

			int size;

			if ((child->flags & (axis ? ES_CELL_V_PUSH : ES_CELL_H_PUSH)) && has[axis]) {
				size = 0;
			} else {
				int alternate = _axis ? calculatedSize[1 - axis][i] : 0;
				size = axis == TABLE_AXIS_HORIZONTAL ? child->GetWidth(alternate) : child->GetHeight(alternate);
			}

			if (debug) EsPrint("\tChild %d (%z) in cells %d->%d has size %d\n", i, child->cName, child->tableCell.from[axis], child->tableCell.to[axis], size);

			// Step 2: Find the preferred size of each band on this axis.

			int bandSpan = child->tableCell.to[axis] - child->tableCell.from[axis] + 1;
			int totalGapSize = (bandSpan - 1) * gapSize;

			int preferredSizePerBand = (size - totalGapSize) / bandSpan;
			int maximumSizeValue = axis ? child->currentStyle->metrics->maximumHeight : child->currentStyle->metrics->maximumWidth;
			int minimumSizeValue = axis ? child->currentStyle->metrics->minimumHeight : child->currentStyle->metrics->minimumWidth;
			int maximumSizePerBand = maximumSizeValue ? (((int) maximumSizeValue - totalGapSize) / bandSpan) : ES_PANEL_BAND_SIZE_DEFAULT;
			int minimumSizePerBand = maximumSizeValue ? (((int) minimumSizeValue - totalGapSize) / bandSpan) : ES_PANEL_BAND_SIZE_DEFAULT;

			for (int j = child->tableCell.from[axis]; j <= child->tableCell.to[axis]; j++) {
				EsAssert(j >= 0 && j < panel->bandCount[axis]); // Invalid element cell.

				EsPanelBand *band = calculatedProperties[axis] + j;

				if (child->flags & (axis ? ES_CELL_V_PUSH : ES_CELL_H_PUSH)) {
					if (!band->push) {
						band->push = 1;
					}
				}

				if (!panel->bands[axis] || panel->bands[axis][j].preferredSize == ES_PANEL_BAND_SIZE_DEFAULT) {
					if (band->preferredSize != ES_PANEL_BAND_SIZE_DEFAULT) {
						if (band->preferredSize < preferredSizePerBand) {
							band->preferredSize = preferredSizePerBand;
						}
					} else {
						band->preferredSize = preferredSizePerBand;
					}
				}

				if (!panel->bands[axis] || panel->bands[axis][j].maximumSize == ES_PANEL_BAND_SIZE_DEFAULT) {
					if (maximumSizePerBand != ES_PANEL_BAND_SIZE_DEFAULT) {
						if (band->maximumSize != ES_PANEL_BAND_SIZE_DEFAULT) {
							if (band->maximumSize > maximumSizePerBand) {
								band->maximumSize = maximumSizePerBand;
							}
						} else {
							band->maximumSize = maximumSizePerBand;
						}
					}
				}

				if (!panel->bands[axis] || panel->bands[axis][j].minimumSize == ES_PANEL_BAND_SIZE_DEFAULT) {
					if (minimumSizePerBand != ES_PANEL_BAND_SIZE_DEFAULT) {
						if (band->minimumSize != ES_PANEL_BAND_SIZE_DEFAULT) {
							if (band->minimumSize < minimumSizePerBand) {
								band->minimumSize = minimumSizePerBand;
							}
						} else {
							band->minimumSize = minimumSizePerBand;
						}
					}
				}
			}
		}

		// Step 3: Work out the size of each band.

		if (has[axis]) {
			int contentSpace = in[axis] - insetStart - insetEnd - (panel->bandCount[axis] - 1) * gapSize;

			for (int i = 0; i < panel->bandCount[axis]; i++) {
				EsPanelBand *band = calculatedProperties[axis] + i;
				if (band->minimumSize   == ES_PANEL_BAND_SIZE_DEFAULT) band->minimumSize   = 0;
				if (band->preferredSize == ES_PANEL_BAND_SIZE_DEFAULT) band->preferredSize = 0;
				if (band->maximumSize   == ES_PANEL_BAND_SIZE_DEFAULT) band->maximumSize   = INT_MAX;
			}

			int usedSpace = 0;
			
			for (int i = 0; i < panel->bandCount[axis]; i++) {
				usedSpace += calculatedProperties[axis][i].preferredSize;
			}
			
			bool shrink = usedSpace > contentSpace;
			int remainingDifference = AbsoluteInteger(usedSpace - contentSpace);

			while (remainingDifference > 0) {
				int availableWeight = 0;
				
				for (int i = 0; i < panel->bandCount[axis]; i++) {
					EsPanelBand *band = calculatedProperties[axis] + i;
					availableWeight += shrink ? band->pull : band->push;
				}
				
				if (!availableWeight) {
					break; // There are no more flexible bands.
				}
				
				int perWeight = remainingDifference / availableWeight, perWeightExtra = remainingDifference % availableWeight;
				bool stable = true;
				
				for (int i = 0; i < panel->bandCount[axis]; i++) {
					EsPanelBand *band = calculatedProperties[axis] + i;
					int available = shrink ? (band->preferredSize - band->minimumSize) : (band->maximumSize - band->preferredSize);
					int weight = shrink ? band->pull : band->push;
					int change = weight * perWeight;
					int extra = MinimumInteger(perWeightExtra, weight);
					change += extra, perWeightExtra -= extra;
					
					if (change > available) {
						band->preferredSize = shrink ? band->minimumSize : band->maximumSize;
						band->pull = band->push = 0;
						remainingDifference -= available;
						stable = false;
					}
				}
				
				if (stable) {
					perWeightExtra = remainingDifference % availableWeight;
				
					for (int i = 0; i < panel->bandCount[axis]; i++) {
						EsPanelBand *band = calculatedProperties[axis] + i;
						int weight = shrink ? band->pull : band->push;
						int change = weight * perWeight;
						int extra = MinimumInteger(perWeightExtra, weight);
						change += extra, perWeightExtra -= extra;
						band->preferredSize += (shrink ? -1 : 1) * change;
						if (band->preferredSize < band->minimumSize) band->preferredSize = band->minimumSize;
						if (band->preferredSize > band->maximumSize) band->preferredSize = band->maximumSize;
					}
					
					break; // We've found a working configuration.
				}
			}
		}

		// Step 4: Work out the final size of each child.

		for (uintptr_t i = 0; i < childCount; i++) {
			EsElement *child = panel->GetChild(i);
			if (child->flags & (ES_ELEMENT_HIDDEN | ES_ELEMENT_NON_CLIENT)) continue;

			int size = (child->tableCell.to[axis] - child->tableCell.from[axis]) * gapSize;

			for (int j = child->tableCell.from[axis]; j <= child->tableCell.to[axis]; j++) {
				EsPanelBand *band = calculatedProperties[axis] + j;
				size += band->preferredSize;
			}

			calculatedSize[axis][i] = size;
		}

		// Step 5: Calculate the position of the bands.

		int position = insetStart;

		for (int i = 0; i < panel->bandCount[axis]; i++) {
			if (i) position += gapSize;
			EsPanelBand *band = calculatedProperties[axis] + i;
			int size = band->preferredSize;
			band->maximumSize = position; // Aliasing maximumSize with position.
			position += size;
		}

		out[axis] = position + insetEnd;
	}

	// Step 6: Move the children to their new location.

	if (message->type == ES_MSG_GET_WIDTH) {
		message->measure.width = out[0];
	} else if (message->type == ES_MSG_GET_HEIGHT) {
		message->measure.height = out[1];
	} else {
		for (uintptr_t i = 0; i < childCount; i++) {
			EsElement *element = panel->GetChild(i);

			if (element->flags & ES_ELEMENT_NON_CLIENT) {
				continue;
			} else if (element->flags & ES_ELEMENT_HIDDEN) {
				element->InternalMove(0, 0, -1, -1);
				continue;
			}

			int position[2], size[2];

			for (int axis = 0; axis < 2; axis++) {
				position[axis] = calculatedProperties[axis][element->tableCell.from[axis]].maximumSize;
				size[axis] = calculatedSize[axis][i];
			}

			PanelMoveChild(element, size[0], size[1], position[0] - panel->scroll.position[0], position[1] - panel->scroll.position[1]);
		}
	}

	if (debug) {
		EsPrint("\t%d/%d\n", out[0], out[1]);
	}

	EsHeapFree(memoryBase);
}

int LayoutStackDeterminePerPush(EsPanel *panel, int available, int secondary) {
	size_t childCount = panel->GetChildCount();
	int fill = 0, count = 0, perPush = 0;

	for (uintptr_t i = 0; i < childCount; i++) {
		EsElement *child = panel->GetChild(i);
		if (child->flags & (ES_ELEMENT_HIDDEN | ES_ELEMENT_NON_CLIENT)) continue;

		count++;

		if (panel->flags & ES_PANEL_HORIZONTAL) {
			if (child->flags & ES_CELL_H_PUSH) {
				fill++;
			} else if (available > 0) {
				available -= child->GetWidth(secondary);
			}
		} else {
			if (child->flags & ES_CELL_V_PUSH) {
				fill++;
			} else if (available > 0) {
				available -= child->GetHeight(secondary);
			}
		}
	}

	if (count) {
		available -= (count - 1) * panel->GetGapMajor();
	}

	if (available > 0 && fill) {
		perPush = available / fill;
	}

	return perPush;
}

void LayoutStackSecondary(EsPanel *panel, EsMessage *message) {
	bool horizontal = panel->flags & ES_PANEL_HORIZONTAL;
	size_t childCount = panel->GetChildCount();
	EsRectangle insets = panel->GetInsets();
	int size = 0;

	int primary = horizontal ? message->measure.width : message->measure.height;
	int perPush = 0;

	if (panel->state & UI_STATE_INSPECTING) {
		InspectorNotifyElementEvent(panel, "layout", "Measuring stack on secondary axis with %d children, insets %R; provided primary size is %d.\n", 
				panel, childCount, insets, primary);
	}

	if (primary) {
		if (horizontal) primary -= insets.l + insets.r;
		else primary -= insets.t + insets.b;
		perPush = LayoutStackDeterminePerPush(panel, primary, 0);
	}

	for (uintptr_t i = 0; i < childCount; i++) {
		EsElement *child = panel->GetChild(i);
		if (child->flags & (ES_ELEMENT_HIDDEN | ES_ELEMENT_NON_CLIENT)) continue;

		if (horizontal) {
			int height = child->GetHeight((child->flags & ES_CELL_H_PUSH) ? perPush : 0);
			if (height > size) size = height;
		} else {
			int width = child->GetWidth((child->flags & ES_CELL_V_PUSH) ? perPush : 0);
			if (width > size) size = width;
		}
	}

	if (horizontal) message->measure.height = size + insets.t + insets.b;
	else message->measure.width = size + insets.l + insets.r;
}

void LayoutStackPrimary(EsPanel *panel, EsMessage *message) {
	bool horizontal = panel->flags & ES_PANEL_HORIZONTAL;
	bool reverse = panel->flags & ES_PANEL_REVERSE;
	EsRectangle bounds = panel->GetBounds();
	size_t childCount = panel->GetChildCount();

	EsRectangle insets = panel->GetInsets();
	int gap = panel->GetGapMajor();

	if (message->type != ES_MSG_LAYOUT && (panel->state & UI_STATE_INSPECTING)) {
		InspectorNotifyElementEvent(panel, "layout", "Measuring stack on primary axis with %d children, gap %d, insets %R.\n", childCount, gap, insets);
	}

	if (message->type == ES_MSG_LAYOUT && (panel->state & UI_STATE_INSPECTING)) {
		InspectorNotifyElementEvent(panel, "layout", "LayoutStack into %R with %d children, gap %d, insets %R.\n", bounds, childCount, gap, insets);
	}

	int hBase = message->type == ES_MSG_GET_HEIGHT ? message->measure.width : Width(bounds);
	int vBase = message->type == ES_MSG_GET_WIDTH ? message->measure.height : Height(bounds);

	int hSpace = hBase ? (hBase - insets.l - insets.r) : 0;
	int vSpace = vBase ? (vBase - insets.t - insets.b) : 0;
	int available = horizontal ? hSpace : vSpace;
	int perPush = LayoutStackDeterminePerPush(panel, available, horizontal ? vSpace : hSpace);

	int position = horizontal ? (reverse ? insets.r : insets.l) : (reverse ? insets.b : insets.t);
	bool anyNonHiddenChildren = false;

	for (uintptr_t i = 0; i < childCount; i++) {
		EsElement *child = panel->GetChild(i);
		if (child->flags & (ES_ELEMENT_HIDDEN | ES_ELEMENT_NON_CLIENT)) continue;
		EsRectangle relative;
		anyNonHiddenChildren = true;

		if (horizontal) {
			int width = (child->flags & ES_CELL_H_PUSH) ? perPush : child->GetWidth(vSpace);

			if (reverse) {
				relative = ES_RECT_4(bounds.r - position - width, bounds.r - position, insets.t, bounds.b - insets.b);
			} else {
				relative = ES_RECT_4(position, position + width, insets.t, bounds.b - insets.b);
			}

			position += width + gap;
		} else {
			int height = (child->flags & ES_CELL_V_PUSH) ? perPush : child->GetHeight(hSpace);

			if (reverse) {
				relative = ES_RECT_4(insets.l, bounds.r - insets.r, bounds.b - position - height, bounds.b - position);
			} else {
				relative = ES_RECT_4(insets.l, bounds.r - insets.r, position, position + height);
			}

			position += height + gap;
		}

		if (message->type == ES_MSG_LAYOUT) {
			if (panel->state & UI_STATE_INSPECTING) {
				InspectorNotifyElementEvent(panel, "layout", "\tMove child %d into %R.\n", i, relative);
			}

			EsRectangle childBounds = Translate(relative, -panel->scroll.position[0], -panel->scroll.position[1]);
			PanelMoveChild(child, Width(childBounds), Height(childBounds), childBounds.l, childBounds.t);
		}
	}

	if (anyNonHiddenChildren) position -= gap;

	if (message->type == ES_MSG_GET_WIDTH) {
		message->measure.width = position + (reverse ? insets.l : insets.r);
	} else if (message->type == ES_MSG_GET_HEIGHT) {
		message->measure.height = position + (reverse ? insets.t : insets.b);
	}
}

void LayoutStack(EsPanel *panel, EsMessage *message) {
	bool horizontal = panel->flags & ES_PANEL_HORIZONTAL;

	if (message->type == ES_MSG_LAYOUT 
			|| (message->type == ES_MSG_GET_WIDTH && horizontal) 
			|| (message->type == ES_MSG_GET_HEIGHT && !horizontal)) {
		LayoutStackPrimary(panel, message);
	} else {
		LayoutStackSecondary(panel, message);
	}
}

int EsElement::GetWidth(int height) {
	if (currentStyle->preferredWidth) return currentStyle->preferredWidth;
	if (!height) height = currentStyle->preferredHeight;
	else if (currentStyle->preferredHeight && currentStyle->preferredHeight > height && (~flags & (ES_CELL_V_SHRINK))) height = currentStyle->preferredHeight;
	else if (currentStyle->preferredHeight && currentStyle->preferredHeight < height && (~flags & (ES_CELL_V_EXPAND))) height = currentStyle->preferredHeight;
	else if (currentStyle->metrics->minimumHeight && currentStyle->metrics->minimumHeight > height) height = currentStyle->metrics->minimumHeight;
	else if (currentStyle->metrics->maximumHeight && currentStyle->metrics->maximumHeight < height) height = currentStyle->metrics->maximumHeight;
	if (height) height -= internalOffsetTop + internalOffsetBottom;
	EsMessage m = { ES_MSG_GET_WIDTH };
	m.measure.height = height;
	EsMessageSend(this, &m);
	int width = m.measure.width + internalOffsetLeft + internalOffsetRight;
	if (currentStyle->metrics->minimumWidth && currentStyle->metrics->minimumWidth > width) width = currentStyle->metrics->minimumWidth;
	if (currentStyle->metrics->maximumWidth && currentStyle->metrics->maximumWidth < width) width = currentStyle->metrics->maximumWidth;
	return width;
}

int EsElement::GetHeight(int width) {
	if (currentStyle->preferredHeight) return currentStyle->preferredHeight;
	if (!width) width = currentStyle->preferredWidth;
	else if (currentStyle->preferredWidth && currentStyle->preferredWidth > width && (~flags & (ES_CELL_H_SHRINK))) width = currentStyle->preferredWidth;
	else if (currentStyle->preferredWidth && currentStyle->preferredWidth < width && (~flags & (ES_CELL_H_EXPAND))) width = currentStyle->preferredWidth;
	else if (currentStyle->metrics->minimumWidth && currentStyle->metrics->minimumWidth > width) width = currentStyle->metrics->minimumWidth;
	else if (currentStyle->metrics->maximumWidth && currentStyle->metrics->maximumWidth < width) width = currentStyle->metrics->maximumWidth;
	if (width) width -= internalOffsetLeft + internalOffsetRight;
	EsMessage m = { ES_MSG_GET_HEIGHT };
	m.measure.width = width;
	EsMessageSend(this, &m);
	int height = m.measure.height + internalOffsetTop + internalOffsetBottom;
	if (currentStyle->metrics->minimumHeight && currentStyle->metrics->minimumHeight > height) height = currentStyle->metrics->minimumHeight;
	if (currentStyle->metrics->maximumHeight && currentStyle->metrics->maximumHeight < height) height = currentStyle->metrics->maximumHeight;
	return height;
}

void EsElement::InternalMove(int _width, int _height, int _offsetX, int _offsetY) {
	if (state & UI_STATE_EXITING) {
		return;
	}

#ifdef TRACE_LAYOUT
	if (parent) {
		EsElement *parent = this->parent->parent;
		while (parent) parent = parent->parent, EsPrint("\t");
		EsPrint("(move) %z\n", debugName);
	}
#endif

#ifdef TRACE_LAYOUT
	if (parent) {
		EsElement *parent = this->parent->parent;
		while (parent) parent = parent->parent, EsPrint("\t");
		EsPrint("(move) %z :: in %d, %d; %d, %d :: ", debugName, _offsetX, _offsetY, _width, _height);
	}
#endif

	// Add the internal offset.

	if (parent) {
		_offsetX += parent->internalOffsetLeft;
		_offsetY += parent->internalOffsetTop;
	}

	// What has changed?

	bool hasPositionChanged = _offsetX != offsetX || _offsetY != offsetY;
	bool hasSizeChanged = _width != width || _height != height;
	bool relayoutRequested = state & UI_STATE_RELAYOUT;
	bool relayoutChild = state & UI_STATE_RELAYOUT_CHILD;
	int oldOffsetX = offsetX, oldOffsetY = offsetY;

#ifdef TRACE_LAYOUT
	if (parent) {
		EsPrint("align %d, %d; %d, %d ::%z%z%z%z\n", _offsetX, _offsetY, _width, _height,
				hasPositionChanged ? " pos" : "", hasSizeChanged ? " size" : "", 
				relayoutRequested ? " rel" : "", relayoutChild ? " child" : "");
	}
#endif

	// Update the variables.

	offsetX = _offsetX;
	offsetY = _offsetY;
	width = _width;
	height = _height;
	state &= ~(UI_STATE_RELAYOUT | UI_STATE_RELAYOUT_CHILD);

	if (!relayoutRequested && !hasSizeChanged) {
		// If our size hasn't changed and a relayout wasn't requested, then we don't need to do any layouting.

		if (hasPositionChanged) {
			// Clear the old position.

			if (parent) {
				EsRectangle paintOutsets = currentStyle->paintOutsets;
				EsRectangle rectangle = ES_RECT_4(oldOffsetX - paintOutsets.l, oldOffsetX + width + paintOutsets.r,
						oldOffsetY - paintOutsets.t, oldOffsetY + height + paintOutsets.b);
				parent->Repaint(false, rectangle);
			}

			// Repaint if we've moved.

			Repaint(true);
		}

		if (relayoutChild) {
			for (uintptr_t i = 0; i < children.Length(); i++) {
				if (children[i]->state & (UI_STATE_RELAYOUT | UI_STATE_RELAYOUT_CHILD)) {
					children[i]->InternalMove(children[i]->width, children[i]->height, children[i]->offsetX, children[i]->offsetY);
				}
			}
		}
	} else {
		// Tell the element to layout its contents.

		if (state & UI_STATE_INSPECTING) EsPerformanceTimerPush();
		EsMessage m = { ES_MSG_LAYOUT };
		m.layout.sizeChanged = hasSizeChanged;
		EsMessageSend(this, &m);
		if (state & UI_STATE_INSPECTING) InspectorNotifyElementEvent(this, "layout", "Layout in %Fms.\n", EsPerformanceTimerPop() * 1000);

		// Repaint.

		Repaint(true);
	}

	if (window != this) {
		window->processCheckVisible = true;
	}

	if (hasPositionChanged || hasSizeChanged) {
		InspectorNotifyElementMoved(this, ES_RECT_4(offsetX, offsetX + width, offsetY, offsetY + height));
	}
}

EsRectangle EsElementGetPreferredSize(EsElement *element) {
	EsMessageMutexCheck();

	return ES_RECT_4(0, element->currentStyle->preferredWidth, 0, element->currentStyle->preferredHeight);
}

void EsElementRelayout(EsElement *element) {
	if (element->state & UI_STATE_DESTROYING) return;
	element->state |= UI_STATE_RELAYOUT;
	UIWindowNeedsUpdate(element->window);

	while (element) {
		element->state |= UI_STATE_RELAYOUT_CHILD;
		element = element->parent;
	}
}

void EsElementUpdateContentSize(EsElement *element, uint32_t flags) {
	if (element->state & UI_STATE_DESTROYING) return;
	if (!flags) flags = ES_ELEMENT_UPDATE_CONTENT_WIDTH | ES_ELEMENT_UPDATE_CONTENT_HEIGHT;

	while (element && flags) {
		element->state &= ~UI_STATE_USE_MEASUREMENT_CACHE;
		EsElementRelayout(element);

		if (element->currentStyle->preferredWidth || ((element->flags & ES_CELL_H_FILL) == ES_CELL_H_FILL)) {
			flags &= ~ES_ELEMENT_UPDATE_CONTENT_WIDTH;
		}

		if (element->currentStyle->preferredHeight || ((element->flags & ES_CELL_V_FILL) == ES_CELL_V_FILL)) {
			flags &= ~ES_ELEMENT_UPDATE_CONTENT_HEIGHT;
		}

		element = element->parent;
	}
}

// --------------------------------- Scrollbars.

// #define ENABLE_SMOOTH_SCROLLING

struct EsScrollbar : EsElement {
	EsButton *up, *down;
	EsElement *thumb;
	double position, autoScrollSpeed, smoothScrollTarget;
	int viewportSize, contentSize, thumbSize, oldThumbPosition, thumbPosition, originalThumbPosition, oldPosition;
	bool horizontal;
};

void ScrollbarLayout(EsScrollbar *scrollbar) {
	if (scrollbar->viewportSize >= scrollbar->contentSize || scrollbar->viewportSize <= 0 || scrollbar->contentSize <= 0) {
		EsElementSetDisabled(scrollbar, true);
	} else {
		EsElementSetDisabled(scrollbar, false);
		EsRectangle bounds = scrollbar->GetBounds();

		if (scrollbar->horizontal) {
			scrollbar->thumbSize = scrollbar->viewportSize * (bounds.r - scrollbar->height * 2) / scrollbar->contentSize;

			if (scrollbar->thumbSize < scrollbar->thumb->currentStyle->preferredWidth) {
				scrollbar->thumbSize = scrollbar->thumb->currentStyle->preferredWidth;
			}

			if (scrollbar->thumbSize > Width(bounds) - scrollbar->height * 2) {
				scrollbar->thumbSize = Width(bounds) - scrollbar->height * 2;
			}

			scrollbar->thumbPosition = LinearMap(0, scrollbar->contentSize - scrollbar->viewportSize, 
					scrollbar->height, bounds.r - scrollbar->thumbSize - scrollbar->height, scrollbar->smoothScrollTarget);

			EsElementMove(scrollbar->up, 0, 0, (int) scrollbar->thumbPosition + scrollbar->thumbSize / 2, scrollbar->thumb->currentStyle->preferredHeight);
			EsElementMove(scrollbar->thumb, (int) scrollbar->thumbPosition, 0, scrollbar->thumbSize, scrollbar->thumb->currentStyle->preferredHeight);
			EsElementMove(scrollbar->down, (int) scrollbar->thumbPosition + scrollbar->thumbSize / 2, 0,
					bounds.r - scrollbar->thumbSize / 2 - (int) scrollbar->thumbPosition, 
					scrollbar->thumb->currentStyle->preferredHeight);
		} else {
			scrollbar->thumbSize = scrollbar->viewportSize * (bounds.b - scrollbar->width * 2) / scrollbar->contentSize;

			if (scrollbar->thumbSize < scrollbar->thumb->currentStyle->preferredHeight) {
				scrollbar->thumbSize = scrollbar->thumb->currentStyle->preferredHeight;
			}

			if (scrollbar->thumbSize > Height(bounds) - scrollbar->width * 2) {
				scrollbar->thumbSize = Height(bounds) - scrollbar->width * 2;
			}

			scrollbar->thumbPosition = LinearMap(0, scrollbar->contentSize - scrollbar->viewportSize, 
					scrollbar->width, bounds.b - scrollbar->thumbSize - scrollbar->width, scrollbar->smoothScrollTarget);

			EsElementMove(scrollbar->up, 0, 0, scrollbar->thumb->currentStyle->preferredWidth, (int) scrollbar->thumbPosition + scrollbar->thumbSize / 2);
			EsElementMove(scrollbar->thumb, 0, (int) scrollbar->thumbPosition, scrollbar->thumb->currentStyle->preferredWidth, scrollbar->thumbSize);
			EsElementMove(scrollbar->down, 0, (int) scrollbar->thumbPosition + scrollbar->thumbSize / 2,
					scrollbar->thumb->currentStyle->preferredWidth, 
					bounds.b - scrollbar->thumbSize / 2 - (int) scrollbar->thumbPosition);
		}
	}
}

void ScrollbarSetMeasurements(EsScrollbar *scrollbar, int viewportSize, int contentSize) {
	EsMessageMutexCheck();

	if (scrollbar->viewportSize == viewportSize && scrollbar->contentSize == contentSize) {
		return;
	}

	scrollbar->viewportSize = viewportSize;
	scrollbar->contentSize = contentSize;

	ScrollbarLayout(scrollbar);
}

void ScrollbarSetPosition(EsScrollbar *scrollbar, double position, bool sendMovedMessage, bool smoothScroll) {
	EsMessageMutexCheck();

	if (position > scrollbar->contentSize - scrollbar->viewportSize) position = scrollbar->contentSize - scrollbar->viewportSize;
	if (position < 0) position = 0;

	scrollbar->smoothScrollTarget = position;

	int previous = scrollbar->position;

#ifdef ENABLE_SMOOTH_SCROLLING
	if (smoothScroll && OSCRTabsf(position - scrollbar->position) > 10) {
		scrollbar->StartAnimating();
	} else {
		scrollbar->position = position;
	}
#else
	(void) smoothScroll;
	scrollbar->position = position;
#endif

	EsRectangle bounds = scrollbar->GetBounds();

	scrollbar->thumbPosition = LinearMap(0, scrollbar->contentSize - scrollbar->viewportSize, 
			0, (scrollbar->horizontal ? bounds.r : bounds.b) - scrollbar->thumbSize, position);

	if (sendMovedMessage && scrollbar->oldPosition != (int) scrollbar->position) {
		EsMessage m = { ES_MSG_SCROLLBAR_MOVED };
		m.scrollbarMoved.scroll = (int) position;
		m.scrollbarMoved.previous = previous;
		EsMessageSend(scrollbar, &m);
	}

	if (scrollbar->thumbPosition != scrollbar->oldThumbPosition) {
		ScrollbarLayout(scrollbar);
	}

	scrollbar->oldThumbPosition = scrollbar->thumbPosition;
	scrollbar->oldPosition = scrollbar->position;
}

int ProcessScrollbarButtonMessage(EsElement *element, EsMessage *message) {
	EsScrollbar *scrollbar = (EsScrollbar *) element->parent;

	if (message->type == ES_MSG_MOUSE_LEFT_DOWN) {
		element->state |= UI_STATE_STRONG_PRESSED;

#define UI_SCROLLBAR_AUTO_SPEED (10.0)
		scrollbar->autoScrollSpeed = scrollbar->viewportSize / UI_SCROLLBAR_AUTO_SPEED / (100);

		if (scrollbar->up == element) {
			scrollbar->autoScrollSpeed *= -1;
		}

		element->StartAnimating();
	} else if (message->type == ES_MSG_MOUSE_LEFT_UP) {
		element->state &= ~UI_STATE_STRONG_PRESSED;
		scrollbar->autoScrollSpeed = 0;
	} else if (message->type == ES_MSG_ANIMATE) {
		if (scrollbar->autoScrollSpeed) {
			ScrollbarSetPosition(scrollbar, scrollbar->autoScrollSpeed * message->animate.deltaMs + scrollbar->position, true, false);
			message->animate.waitMs = 0;
			message->animate.complete = false;
		} else {
			message->animate.complete = true;
		}
	} else {
		return 0;
	}

	return ES_HANDLED;
}

EsScrollbar *ScrollbarCreate(EsElement *parent, uint64_t flags) {
	EsScrollbar *scrollbar = (EsScrollbar *) EsHeapAllocate(sizeof(EsScrollbar), true);
	scrollbar->thumb = (EsElement *) EsHeapAllocate(sizeof(EsElement), true);

	if (flags & ES_SCROLLBAR_HORIZONTAL) {
		scrollbar->horizontal = true;
	}

	scrollbar->Initialise(parent, flags, [] (EsElement *element, EsMessage *message) {
		EsScrollbar *scrollbar = (EsScrollbar *) element;

		if (message->type == ES_MSG_LAYOUT) {
			ScrollbarLayout(scrollbar);
		} else if (message->type == ES_MSG_ANIMATE) {
			if (scrollbar->position != scrollbar->smoothScrollTarget) {
				double factor = EsCRTexp2f(-5.0f / message->animate.deltaMs);
				// EsPrint("%dmcs -> %F\n", message->animate.deltaUs, factor);
				scrollbar->position += (scrollbar->smoothScrollTarget - scrollbar->position) * factor;
				ScrollbarSetPosition(scrollbar, scrollbar->smoothScrollTarget, true, true);
				bool done = scrollbar->position == scrollbar->smoothScrollTarget;
				message->animate.waitMs = 0, message->animate.complete = done;
			} else message->animate.complete = true;
		} else {
			return 0;
		}

		return ES_HANDLED;
	}, nullptr);

	scrollbar->cName = "scrollbar";

	scrollbar->up = EsButtonCreate(scrollbar, ES_CELL_FILL);
	scrollbar->up->messageUser = ProcessScrollbarButtonMessage;
	scrollbar->down = EsButtonCreate(scrollbar, ES_CELL_FILL);
	scrollbar->down->messageUser = ProcessScrollbarButtonMessage;

	scrollbar->thumb->Initialise(scrollbar, ES_CELL_FILL, [] (EsElement *element, EsMessage *message) {
		EsScrollbar *scrollbar = (EsScrollbar *) element->parent;
		EsRectangle bounds = scrollbar->GetBounds();

		if (message->type == ES_MSG_MOUSE_LEFT_DRAG) {
			if (scrollbar->horizontal) {
				float p = LinearMap(scrollbar->height, bounds.r - scrollbar->thumbSize - scrollbar->height, 0, scrollbar->contentSize - scrollbar->viewportSize,
						message->mouseDragged.newPositionX - message->mouseDragged.originalPositionX + scrollbar->originalThumbPosition);
				ScrollbarSetPosition(scrollbar, p, true, true);
			} else {
				float p = LinearMap(scrollbar->width, bounds.b - scrollbar->thumbSize - scrollbar->width, 0, scrollbar->contentSize - scrollbar->viewportSize,
						message->mouseDragged.newPositionY - message->mouseDragged.originalPositionY + scrollbar->originalThumbPosition);
				ScrollbarSetPosition(scrollbar, p, true, true);
			}
		} else if (message->type == ES_MSG_MOUSE_LEFT_DOWN) {
			scrollbar->originalThumbPosition = scrollbar->thumbPosition;
		} else {
			return 0;
		}

		return ES_HANDLED;
	}, nullptr);

	scrollbar->thumb->cName = "scrollbar thumb";

	if (scrollbar->horizontal) {
		scrollbar->up->SetStyle(ES_STYLE_PUSH_BUTTON_SCROLLBAR_LEFT);
		scrollbar->down->SetStyle(ES_STYLE_PUSH_BUTTON_SCROLLBAR_RIGHT);
		scrollbar->thumb->SetStyle(ES_STYLE_SCROLLBAR_THUMB_HORIZONTAL);
		scrollbar->SetStyle(ES_STYLE_SCROLLBAR_BAR_HORIZONTAL);
	} else {
		scrollbar->up->SetStyle(ES_STYLE_PUSH_BUTTON_SCROLLBAR_UP);
		scrollbar->down->SetStyle(ES_STYLE_PUSH_BUTTON_SCROLLBAR_DOWN);
		scrollbar->thumb->SetStyle(ES_STYLE_SCROLLBAR_THUMB_VERTICAL);
		scrollbar->SetStyle(ES_STYLE_SCROLLBAR_BAR_VERTICAL);
	}

	scrollbar->up->flags &= ~ES_ELEMENT_FOCUSABLE;
	scrollbar->down->flags &= ~ES_ELEMENT_FOCUSABLE;

	return scrollbar;
}

void ScrollPane::Setup(EsElement *_parent, uint8_t _xMode, uint8_t _yMode, uint16_t _flags) {
	parent = _parent;
	mode[0] = _xMode;
	mode[1] = _yMode;
	flags = _flags;

	if (mode[0] == SCROLL_MODE_NONE) flags &= ~SCROLL_X_DRAG;
	if (mode[1] == SCROLL_MODE_NONE) flags &= ~SCROLL_Y_DRAG;

	for (int axis = 0; axis < 2; axis++) {
		if (mode[axis] == SCROLL_MODE_FIXED || mode[axis] == SCROLL_MODE_AUTO) {
			uint64_t flags = ES_CELL_FILL | ES_ELEMENT_NON_CLIENT | (axis ? ES_SCROLLBAR_VERTICAL : ES_SCROLLBAR_HORIZONTAL);
			if (!bar[axis]) bar[axis] = ScrollbarCreate(parent, flags);
			bar[axis]->userData = this;

			bar[axis]->messageUser = [] (EsElement *element, EsMessage *message) {
				ScrollPane *pane = (ScrollPane *) element->userData.p;

				if (message->type == ES_MSG_SCROLLBAR_MOVED) {
					int axis = (element->flags & ES_SCROLLBAR_HORIZONTAL) ? 0 : 1;
					EsMessage m = *message;
					m.type = axis ? ES_MSG_SCROLL_Y : ES_MSG_SCROLL_X;
					pane->position[axis] = m.scrollbarMoved.scroll;
					EsMessageSend(pane->parent, &m);
				}

				return 0;
			};
		} else if (bar[axis]) {
			EsElementDestroy(bar[axis]);
			bar[axis] = nullptr;
		}
	}

	if (bar[0] && bar[1]) {
		if (!pad) pad = EsCustomElementCreate(parent, ES_CELL_FILL | ES_ELEMENT_NON_CLIENT, ES_STYLE_SCROLLBAR_PAD);
		pad->cName = "scrollbar pad";
	} else if (pad) {
		EsElementDestroy(pad);
		pad = nullptr;
	}
}

void ScrollPane::ReceivedMessage(EsMessage *message) {
	if (message->type == ES_MSG_LAYOUT) {
		Refresh();
	} else if (message->type == ES_MSG_MOUSE_LEFT_DRAG || message->type == ES_MSG_MOUSE_RIGHT_DRAG || message->type == ES_MSG_MOUSE_MIDDLE_DRAG) {
		if (flags & (SCROLL_X_DRAG | SCROLL_Y_DRAG)) {
			parent->StartAnimating();
			dragScrolling = true;
		}
	} else if (message->type == ES_MSG_MOUSE_LEFT_UP || message->type == ES_MSG_MOUSE_RIGHT_UP || message->type == ES_MSG_MOUSE_MIDDLE_UP) {
		dragScrolling = false;
	} else if (message->type == ES_MSG_ANIMATE) {
		if (dragScrolling) {
			EsPoint point = EsMouseGetPosition(parent); 
			EsRectangle bounds = parent->GetBounds();
			double distanceX = point.x < bounds.l ? point.x - bounds.l : point.x >= bounds.r ? point.x - bounds.r + 1 : 0;
			double distanceY = point.y < bounds.t ? point.y - bounds.t : point.y >= bounds.b ? point.y - bounds.b + 1 : 0;
			double deltaX = message->animate.deltaMs * distanceX / 300.0;
			double deltaY = message->animate.deltaMs * distanceY / 300.0;
			if (deltaX && (flags & SCROLL_X_DRAG)) SetX(position[0] + deltaX, true);
			if (deltaY && (flags & SCROLL_Y_DRAG)) SetY(position[1] + deltaY, true);
			message->animate.complete = false;
		}
	} else if (message->type == ES_MSG_GET_HEIGHT) {
		if (message->measure.width && (mode[0] == SCROLL_MODE_AUTO) && (mode[1] != SCROLL_MODE_AUTO)) {
			// To accurately measure the height of the element for this width,
			// we need to determine whether the horizontal scrollbar will be present.
			// TODO This assumes that the element will be send a LAYOUT message after measurements are complete,
			// 	in order for the scrollbars to be updated. But I think this will always happen..?
			EsMessage m = {};
			m.type = ES_MSG_GET_WIDTH;
			EsMessageSend(parent, &m);
			parent->internalOffsetBottom = (m.measure.width + fixedViewport[0] > message->measure.width) ? bar[0]->currentStyle->preferredHeight : 0;
		}
	} else if (message->type == ES_MSG_GET_WIDTH) {
		if (message->measure.width && (mode[1] == SCROLL_MODE_AUTO) && (mode[0] != SCROLL_MODE_AUTO)) {
			// As above.
			EsMessage m = {};
			m.type = ES_MSG_GET_HEIGHT;
			EsMessageSend(parent, &m);
			parent->internalOffsetRight = (m.measure.height + fixedViewport[1] > message->measure.height) ? bar[1]->currentStyle->preferredWidth : 0;
		}
	}
}

void ScrollPane::SetPosition(int axis, double newScroll, bool sendMovedMessage) {
	if (mode[axis] == SCROLL_MODE_NONE) return;
	if (newScroll < 0) newScroll = 0;
	else if (newScroll > limit[axis]) newScroll = limit[axis];
	if (newScroll == position[axis]) return;
	double previous = position[axis];
	position[axis] = newScroll;
	if (bar[axis]) ScrollbarSetPosition(bar[axis], position[axis], false, false);

	if (sendMovedMessage) {
		EsMessage m = {};
		m.type = axis ? ES_MSG_SCROLL_Y : ES_MSG_SCROLL_X;
		m.scrollbarMoved.scroll = position[axis];
		m.scrollbarMoved.previous = previous;
		EsMessageSend(parent, &m);
	}
}

bool ScrollPane::RefreshLimit(int axis, int64_t *contentSize) {
	if (mode[axis] != SCROLL_MODE_NONE) {
		uint8_t *internalOffset = axis ? &parent->internalOffsetRight : &parent->internalOffsetBottom;
		EsRectangle bounds = parent->GetBounds();

		EsMessage m = {};
		m.type = axis ? ES_MSG_GET_HEIGHT : ES_MSG_GET_WIDTH;
		if (axis) m.measure.width = bounds.r;
		else m.measure.height = bounds.b;
		EsMessageSend(parent, &m);

		*contentSize = axis ? m.measure.height : m.measure.width;
		limit[axis] = *contentSize - (axis ? bounds.b : bounds.r);
		if (limit[axis] < 0) limit[axis] = 0;

		if (parent->state & UI_STATE_INSPECTING) {
			InspectorNotifyElementEvent(parent, "scroll", "New %c limit: %d. (Measured content %d with other axis %d.)\n", 
					axis + 'X', limit[axis], *contentSize, axis ? bounds.r : bounds.b);
		}

		if (mode[axis] == SCROLL_MODE_AUTO && limit[axis] > 0 && !(*internalOffset)) {
			*internalOffset = axis ? bar[axis]->currentStyle->preferredWidth : bar[axis]->currentStyle->preferredHeight;
			return true;
		}
	}

	return false;
}

void ScrollPane::Refresh() {
	if (parent->state & UI_STATE_INSPECTING) {
		InspectorNotifyElementEvent(parent, "scroll", "Refreshing scroll pane...\n");
	}

	parent->internalOffsetRight = mode[1] == SCROLL_MODE_FIXED ? bar[1]->currentStyle->preferredWidth : 0;
	parent->internalOffsetBottom = mode[0] == SCROLL_MODE_FIXED ? bar[0]->currentStyle->preferredHeight : 0;

	int64_t contentWidth = 0, contentHeight = 0;

	bool recalculateLimits1 = RefreshLimit(0, &contentWidth);
	bool recalculateLimits2 = RefreshLimit(1, &contentHeight);

	if (recalculateLimits1 || recalculateLimits2) {
		RefreshLimit(0, &contentWidth);
		RefreshLimit(1, &contentHeight);
	}

	EsRectangle bounds = parent->GetBounds();

	if (bar[0]) ScrollbarSetMeasurements(bar[0], bounds.r - fixedViewport[0], contentWidth  - fixedViewport[0]);
	if (bar[1]) ScrollbarSetMeasurements(bar[1], bounds.b - fixedViewport[1], contentHeight - fixedViewport[1]);

	SetPosition(0, position[0], true);
	SetPosition(1, position[1], true);

	if (bar[0]) {
		bar[0]->InternalMove(parent->width - parent->internalOffsetRight, bar[0]->currentStyle->preferredHeight, 
				0, parent->height - parent->internalOffsetBottom);
		autoScrollbars[0] = ~bar[0]->flags & ES_ELEMENT_DISABLED;
	}

	if (bar[1]) {
		bar[1]->InternalMove(bar[1]->currentStyle->preferredWidth, parent->height - parent->internalOffsetBottom, 
				parent->width - parent->internalOffsetRight, 0);
		autoScrollbars[1] = ~bar[1]->flags & ES_ELEMENT_DISABLED;
	}

	if (pad) {
		pad->InternalMove(parent->internalOffsetRight, parent->internalOffsetBottom, 
				parent->width - parent->internalOffsetRight, parent->height - parent->internalOffsetBottom);
	}
}

// --------------------------------- Panels.

void PanelSwitcherTransitionComplete(EsPanel *panel) {
	if (panel->switchedFrom) {
		if (panel->destroyPreviousAfterTransitionCompletes) {
			panel->switchedFrom->Destroy();
		} else {
			EsElementSetHidden(panel->switchedFrom, true);
		}

		panel->switchedFrom = nullptr;
	}

	panel->transitionType = ES_TRANSITION_NONE;
}

void PanelTableSetChildCell(EsPanel *panel, EsElement *child) {
	uintptr_t index = panel->tableIndex++;
	TableCell cell = {};

	if (panel->flags & ES_PANEL_HORIZONTAL) {
		cell.from[0] = cell.to[0] = index % panel->bandCount[0];
		cell.from[1] = cell.to[1] = index / panel->bandCount[0];

		if (panel->bandCount[1] <= cell.from[1] && !panel->bands[1]) {
			panel->bandCount[1] = cell.from[1] + 1;
		}
	} else {
		cell.from[0] = cell.to[0] = index / panel->bandCount[1];
		cell.from[1] = cell.to[1] = index % panel->bandCount[1];

		if (panel->bandCount[0] <= cell.from[0] && !panel->bands[0]) {
			panel->bandCount[0] = cell.from[0] + 1;
		}
	}

	child->tableCell = cell;
}

int ProcessPanelMessage(EsElement *element, EsMessage *message) {
	EsPanel *panel = (EsPanel *) element;
	EsRectangle bounds = panel->GetBounds();

	panel->scroll.ReceivedMessage(message);

	if (message->type == ES_MSG_LAYOUT) {
		if (panel->flags & ES_PANEL_TABLE) {
			LayoutTable(panel, message);
		} else if (panel->flags & ES_PANEL_SWITCHER) {
			EsRectangle insets = panel->GetInsets();

			if (panel->switchedFrom) {
				EsElementMove(panel->switchedFrom, bounds.l + insets.l, bounds.t + insets.t,
						bounds.r - bounds.l - insets.r - insets.l, bounds.b - bounds.t - insets.b - insets.t);
			}

			if (panel->switchedTo) {
				EsElementMove(panel->switchedTo,  bounds.l + insets.l, bounds.t + insets.t,
						bounds.r - bounds.l - insets.r - insets.l, bounds.b - bounds.t - insets.b - insets.t);
			}
		} else if (panel->flags & ES_PANEL_Z_STACK) {
			EsRectangle insets = panel->GetInsets();

			for (uintptr_t i = 0; i < element->GetChildCount(); i++) {
				EsElement *child = element->GetChild(i);
				if (child->flags & (ES_ELEMENT_HIDDEN | ES_ELEMENT_NON_CLIENT)) continue;
				PanelMoveChild(child, bounds.r - bounds.l - insets.r - insets.l, 
						bounds.b - bounds.t - insets.b - insets.t, 
						bounds.l + insets.l, bounds.t + insets.t);
			}
		} else {
			LayoutStack(panel, message);
		}
	} else if (message->type == ES_MSG_PAINT_CHILDREN) {
		if ((panel->flags & ES_PANEL_SWITCHER) && panel->transitionType != ES_TRANSITION_NONE) {
			double progress = SmoothAnimationTimeSharp((double) panel->transitionTimeMs / (double) panel->transitionLengthMs);
			EsRectangle bounds = EsPainterBoundsClient(message->painter);
			int width = Width(bounds), height = Height(bounds);
			EsPaintTarget target;

			if (EsPaintTargetTake(&target, width, height)) {
				EsPainter painter = { .clip = ES_RECT_4(0, width, 0, height), .width = width, .height = height, .target = &target };

				// TODO 'Clip'-style transitions. ES_TRANSITION_REVEAL_UP/ES_TRANSITION_REVEAL_DOWN.

				if (panel->switchedFrom) {
					panel->switchedFrom->InternalPaint(&painter, PAINT_SHADOW);
					panel->switchedFrom->InternalPaint(&painter, ES_FLAGS_DEFAULT);
					panel->switchedFrom->InternalPaint(&painter, PAINT_OVERLAY);
					UIDrawTransitionEffect(message->painter, &target, bounds, (EsTransitionType) panel->transitionType, progress, false);
					EsPaintTargetClear(&target); 
				}

				if (panel->switchedTo) {
					panel->switchedTo->InternalPaint(&painter, PAINT_SHADOW);
					panel->switchedTo->InternalPaint(&painter, ES_FLAGS_DEFAULT);
					panel->switchedTo->InternalPaint(&painter, PAINT_OVERLAY);
					UIDrawTransitionEffect(message->painter, &target, bounds, (EsTransitionType) panel->transitionType, progress, true);
				}

				EsPaintTargetReturn(&target);
			} else {
				// Not enough memory to get a paint target.
				return 0;
			}
		} else {
			return 0;
		}
	} else if (message->type == ES_MSG_GET_WIDTH) {
		if (!panel->measurementCache.Get(message, &panel->state)) {
			if (panel->flags & ES_PANEL_TABLE) {
				LayoutTable(panel, message);
			} else if (panel->flags & (ES_PANEL_Z_STACK | ES_PANEL_SWITCHER)) {
				int maximum = 0;

				for (uintptr_t i = 0; i < element->GetChildCount(); i++) {
					EsElement *child = element->GetChild(i);
					if (child->flags & (ES_ELEMENT_HIDDEN | ES_ELEMENT_NON_CLIENT)) continue;
					int size = child->GetWidth(message->measure.height);
					if (size > maximum) maximum = size;
				}

				message->measure.width = maximum + panel->GetInsetWidth();
			} else {
				LayoutStack(panel, message);
			}

			panel->measurementCache.Store(message);
		}
	} else if (message->type == ES_MSG_GET_HEIGHT) {
		if (!panel->measurementCache.Get(message, &panel->state)) {
			if (panel->flags & ES_PANEL_TABLE) {
				LayoutTable(panel, message);
			} else if (panel->flags & (ES_PANEL_Z_STACK | ES_PANEL_SWITCHER)) {
				int maximum = 0;

				for (uintptr_t i = 0; i < element->GetChildCount(); i++) {
					EsElement *child = element->GetChild(i);
					if (child->flags & (ES_ELEMENT_HIDDEN | ES_ELEMENT_NON_CLIENT)) continue;
					int size = child->GetHeight(message->measure.width);
					if (size > maximum) maximum = size;
				}

				message->measure.height = maximum + panel->GetInsetHeight();
			} else {
				LayoutStack(panel, message);
			}
			panel->measurementCache.Store(message);
		}
	} else if (message->type == ES_MSG_ENSURE_VISIBLE) {
		EsElement *child = message->child, *e = child;
		int offsetX = panel->scroll.position[0], offsetY = panel->scroll.position[1];
		while (e != element) offsetX += e->offsetX, offsetY += e->offsetY, e = e->parent;
		EsRectangle bounds = panel->GetBounds();
		panel->scroll.SetX(offsetX + child->width / 2 - bounds.r / 2);
		panel->scroll.SetY(offsetY + child->height / 2 - bounds.b / 2);
	} else if (message->type == ES_MSG_PRE_ADD_CHILD) {
		if (!panel->addingSeparator && panel->separatorStylePart && panel->GetChildCount()) {
			panel->addingSeparator = true;
			EsCustomElementCreate(panel, panel->separatorFlags, panel->separatorStylePart)->cName = "panel separator";
			panel->addingSeparator = false;
		}
	} else if (message->type == ES_MSG_ADD_CHILD) {
		if (panel->flags & ES_PANEL_TABLE) {
			if (!panel->bandCount[0] && !panel->bandCount[1]) {
				// The application has not yet set the number of columns/rows, 
				// so we can't perform automatical element placement.
				// The application will need to call EsPanelTableSetChildCells.
			} else {
				PanelTableSetChildCell(panel, (EsElement *) message->child);
			}
		} else if (panel->flags & ES_PANEL_SWITCHER) {
			EsElement *child = (EsElement *) message->child;
			child->state |= UI_STATE_BLOCK_INTERACTION;
		}
	} else if (message->type == ES_MSG_SCROLL_X || message->type == ES_MSG_SCROLL_Y) {
		int delta = message->scrollbarMoved.scroll - message->scrollbarMoved.previous;
		int deltaX = message->type == ES_MSG_SCROLL_X ? delta : 0; 
		int deltaY = message->type == ES_MSG_SCROLL_Y ? delta : 0; 

		for (uintptr_t i = 0; i < panel->GetChildCount(); i++) {
			EsElement *child = panel->GetChild(i);
			if (child->flags & (ES_ELEMENT_HIDDEN | ES_ELEMENT_NON_CLIENT)) continue;
			child->InternalMove(child->width, child->height, child->offsetX - deltaX, child->offsetY - deltaY);
		}
	} else if (message->type == ES_MSG_ANIMATE) {
		panel->transitionTimeMs += message->animate.deltaMs;
		message->animate.complete = panel->transitionTimeMs >= panel->transitionLengthMs;

		if (panel->flags & ES_PANEL_SWITCHER) {
			panel->Repaint(true);

			if (message->animate.complete) {
				PanelSwitcherTransitionComplete(panel);
			}
		} else if (panel->movementItems.Length()) {
			EsElementRelayout(panel);

			if (message->animate.complete) {
				panel->movementItems.Free();
			}
		}
	} else if (message->type == ES_MSG_DESTROY_CONTENTS) {
		if ((panel->flags & ES_PANEL_TABLE)) {
			panel->tableIndex = 0;
			panel->bandCount[(panel->flags & ES_PANEL_HORIZONTAL) ? 1 : 0] = 0;
		}
	} else if (message->type == ES_MSG_DESTROY) {
		if ((panel->flags & ES_PANEL_TABLE)) {
			EsHeapFree(panel->bands[0]);
			EsHeapFree(panel->bands[1]);
		}
	} else if (message->type == ES_MSG_KEY_DOWN) {
		if (!(panel->flags & (ES_PANEL_TABLE | ES_PANEL_SWITCHER))
				&& panel->window->focused && panel->window->focused->parent == panel
				&& (panel->flags & ES_PANEL_HORIZONTAL)) {
			bool reverse = panel->flags & ES_PANEL_REVERSE,
			     left = message->keyboard.scancode == ES_SCANCODE_LEFT_ARROW,
			     right = message->keyboard.scancode == ES_SCANCODE_RIGHT_ARROW;

			if ((left && !reverse) || (right && reverse)) {
				EsElement *focus = nullptr;

				for (uintptr_t i = 0; i < panel->GetChildCount(); i++) {
					EsElement *child = panel->GetChild(i);

					if (child->flags & (ES_ELEMENT_HIDDEN | ES_ELEMENT_NON_CLIENT)) {
						continue;
					}

					if (child == panel->window->focused) {
						break;
					} else if (child->IsFocusable()) {
						focus = child;
					}
				}

				if (focus) {
					EsElementFocus(focus);
				}
			} else if ((left && reverse) || (right && !reverse)) {
				EsElement *focus = nullptr;

				for (uintptr_t i = panel->GetChildCount(); i > 0; i--) {
					EsElement *child = panel->GetChild(i - 1);

					if (child->flags & (ES_ELEMENT_HIDDEN | ES_ELEMENT_NON_CLIENT)) {
						continue;
					}

					if (child == panel->window->focused) {
						break;
					} else if (child->IsFocusable()) {
						focus = child;
					}
				}

				if (focus) {
					EsElementFocus(focus);
				}
			} else {
				return 0;
			}
		} else {
			return 0;
		}
	} else if (message->type == ES_MSG_GET_INSPECTOR_INFORMATION) {
		EsBuffer *buffer = message->getContent.buffer;

		if (panel->flags & ES_PANEL_Z_STACK) {
			EsBufferFormat(buffer, "z-stack");
		} else if (panel->flags & ES_PANEL_SWITCHER) {
			EsBufferFormat(buffer, "switcher");
		} else if (panel->flags & ES_PANEL_TABLE) {
			EsBufferFormat(buffer, "table");
		} else {
			EsBufferFormat(buffer, "%z%z stack", 
					(panel->flags & ES_PANEL_REVERSE) ? "reverse " : "",
					(panel->flags & ES_PANEL_HORIZONTAL) ? "horizontal" : "vertical");
		}
	} else if (message->type == ES_MSG_BEFORE_Z_ORDER) {
		bool isStack = !(panel->flags & (ES_PANEL_TABLE | ES_PANEL_SWITCHER | ES_PANEL_Z_STACK));

		if (isStack && panel->children.Length() > 100) {
			// Count the number of client children.

			size_t childCount = panel->children.Length();

			while (childCount) {
				if (panel->children[childCount - 1]->flags & ES_ELEMENT_NON_CLIENT) {
					childCount--;
				} else {
					break;
				}
			}

			if (childCount < 100) {
				return 0;
			}

			message->beforeZOrder.nonClient = childCount;

			// Binary search for an early visible child.

			bool found = false;
			uintptr_t position = 0;

			if (panel->flags & ES_PANEL_HORIZONTAL) {
				ES_MACRO_SEARCH(childCount, result = message->beforeZOrder.clip.l - panel->children[index]->offsetX;, position, found);
			} else {
				ES_MACRO_SEARCH(childCount, result = message->beforeZOrder.clip.t - panel->children[index]->offsetY;, position, found);
			}

			if (!found) {
				position = 0;
			}

			// Search back until we find the first.
			// Assumption: children with paint outsets do not extend beyond the next child.

			while (position) {
				if (position < childCount) {
					EsElement *child = panel->children[position];

					if (panel->flags & ES_PANEL_HORIZONTAL) {
						if (child->offsetX + child->width + child->currentStyle->paintOutsets.r < message->beforeZOrder.clip.l) {
							break;
						}
					} else {
						if (child->offsetY + child->height + child->currentStyle->paintOutsets.b < message->beforeZOrder.clip.t) {
							break;
						}
					}
				}

				position--;
			}

			message->beforeZOrder.start = position;

			// Search forward until we find the last visible child.

			while (position < childCount) {
				EsElement *child = panel->children[position];

				if (panel->flags & ES_PANEL_HORIZONTAL) {
					if (child->offsetX - child->currentStyle->paintOutsets.l > message->beforeZOrder.clip.r) {
						break;
					}
				} else {
					if (child->offsetY - child->currentStyle->paintOutsets.t > message->beforeZOrder.clip.b) {
						break;
					}
				}

				position++;
			}

			message->beforeZOrder.end = position;
		}
	} else {
		return 0;
	}

	return ES_HANDLED;
}

EsPanel *EsPanelCreate(EsElement *parent, uint64_t flags, const EsStyle *style) {
	EsPanel *panel = (EsPanel *) EsHeapAllocate(sizeof(EsPanel), true);

	panel->Initialise(parent, flags, ProcessPanelMessage, style);
	panel->cName = "panel";

	if (flags & ES_PANEL_Z_STACK)    panel->state |= UI_STATE_Z_STACK;
	if (flags & ES_PANEL_HORIZONTAL) panel->flags |= ES_ELEMENT_LAYOUT_HINT_HORIZONTAL;
	if (flags & ES_PANEL_REVERSE)    panel->flags |= ES_ELEMENT_LAYOUT_HINT_REVERSE;

	panel->scroll.Setup(panel, 
			((flags & ES_PANEL_H_SCROLL_FIXED) ? SCROLL_MODE_FIXED : (flags & ES_PANEL_H_SCROLL_AUTO) ? SCROLL_MODE_AUTO : SCROLL_MODE_NONE),
			((flags & ES_PANEL_V_SCROLL_FIXED) ? SCROLL_MODE_FIXED : (flags & ES_PANEL_V_SCROLL_AUTO) ? SCROLL_MODE_AUTO : SCROLL_MODE_NONE),
			ES_FLAGS_DEFAULT);

	return panel;
}

struct EsSpacer : EsElement {
	int width, height;
};

int ProcessSpacerMessage(EsElement *element, EsMessage *message) {
	EsSpacer *spacer = (EsSpacer *) element;

	if (message->type == ES_MSG_GET_WIDTH) {
		message->measure.width = spacer->width * spacer->currentStyle->scale;
	} else if (message->type == ES_MSG_GET_HEIGHT) {
		message->measure.height = spacer->height * spacer->currentStyle->scale;
	}

	return 0;
}

EsElement *EsSpacerCreate(EsElement *panel, uint64_t flags, const EsStyle *style, int width, int height) {
	EsSpacer *spacer = (EsSpacer *) EsHeapAllocate(sizeof(EsSpacer), true);
	spacer->Initialise(panel, flags, ProcessSpacerMessage, style);
	spacer->cName = "spacer";
	spacer->width = width == -1 ? 4 : width;
	spacer->height = height == -1 ? 4 : height;
	return spacer;
}

EsElement *EsCustomElementCreate(EsElement *parent, uint64_t flags, const EsStyle *style) {
	EsElement *element = (EsElement *) EsHeapAllocate(sizeof(EsElement), true);
	element->Initialise(parent, flags, nullptr, style);
	element->cName = "custom element";
	return element;
}

void EsElementSetCellRange(EsElement *element, int xFrom, int yFrom, int xTo, int yTo) {
	EsMessageMutexCheck();

	if (xFrom == -1) xFrom = element->tableCell.from[0];
	if (yFrom == -1) yFrom = element->tableCell.from[1];
	if (xTo == -1) xTo = xFrom;
	if (yTo == -1) yTo = yFrom;

	EsPanel *panel = (EsPanel *) element->parent;
	EsAssert(panel->messageClass == ProcessPanelMessage && panel->flags & ES_PANEL_TABLE); // Invalid parent for SetCellRange.

	TableCell cell = {};
	cell.from[0] = xFrom, cell.from[1] = yFrom;
	cell.to[0] = xTo, cell.to[1] = yTo;
	element->tableCell = cell;
}

void EsPanelSetBands(EsPanel *panel, size_t columnCount, size_t rowCount, EsPanelBand *columns, EsPanelBand *rows) {
	EsMessageMutexCheck();
	EsAssert(panel->flags & ES_PANEL_TABLE); // Cannot set the bands layout for a non-table panel.
	EsHeapFree(panel->bands[0]);
	EsHeapFree(panel->bands[1]);
	panel->bands[0] = nullptr;
	panel->bands[1] = nullptr;
	panel->bandCount[0] = columnCount;
	panel->bandCount[1] = rowCount;
	panel->bands[0] = columns ? (EsPanelBand *) EsHeapAllocate(columnCount * sizeof(EsPanelBand), false) : nullptr; 
	panel->bands[1] = rows ? (EsPanelBand *) EsHeapAllocate(rowCount * sizeof(EsPanelBand), false) : nullptr;
	if (columns) EsMemoryCopy(panel->bands[0], columns, columnCount * sizeof(EsPanelBand));
	if (rows) EsMemoryCopy(panel->bands[1], rows, rowCount * sizeof(EsPanelBand));
}

void EsPanelSetBandsAll(EsPanel *panel, EsPanelBand *column, EsPanelBand *row) {
	EsMessageMutexCheck();
	EsAssert(panel->flags & ES_PANEL_TABLE); // Cannot set the bands layout for a non-table panel.

	EsPanelBand *templates[2] = { column, row };

	for (uintptr_t axis = 0; axis < 2; axis++) {
		if (!templates[axis]) continue;

		if (!panel->bands[axis]) {
			panel->bands[axis] = (EsPanelBand *) EsHeapAllocate(panel->bandCount[axis] * sizeof(EsPanelBand), false);
		}

		for (uintptr_t i = 0; i < panel->bandCount[axis]; i++) {
			panel->bands[axis][i] = *templates[axis];
		}
	}
}

void EsPanelTableSetChildCells(EsPanel *panel) {
	// The number of columns/rows should have been set by the time this function is called.
	EsAssert(panel->bandCount[0] || panel->bandCount[1]);

	panel->tableIndex = 0;
	panel->bandCount[(panel->flags & ES_PANEL_HORIZONTAL) ? 1 : 0] = 0;

	for (uintptr_t i = 0; i < panel->GetChildCount(); i++) {
		EsElement *child = panel->GetChild(i);
		if (child->flags & ES_ELEMENT_NON_CLIENT) continue;
		PanelTableSetChildCell(panel, child);
	}
}

void EsPanelSwitchTo(EsPanel *panel, EsElement *targetChild, EsTransitionType transitionType, uint32_t flags, float timeMultiplier) {
	EsMessageMutexCheck();
	EsAssert(targetChild->parent == panel);
	EsAssert(panel->flags & ES_PANEL_SWITCHER); // Cannot switch element for a non-switcher panel.
	uint32_t timeMs = timeMultiplier * GetConstantNumber("transitionTime");

	if (targetChild == panel->switchedTo) {
		return;
	}

	if (panel->switchedFrom) {
		// We're interrupting the previous transition.
		PanelSwitcherTransitionComplete(panel);
	}

	panel->transitionType = transitionType;
	panel->transitionTimeMs = 0;
	panel->transitionLengthMs = timeMs; 
	panel->switchedFrom = panel->switchedTo;
	panel->switchedTo = targetChild;
	panel->destroyPreviousAfterTransitionCompletes = flags & ES_PANEL_SWITCHER_DESTROY_PREVIOUS_AFTER_TRANSITION;

	if (panel->switchedTo) {
		EsElementSetHidden(panel->switchedTo, false);
		panel->switchedTo->state &= ~UI_STATE_BLOCK_INTERACTION;
		panel->switchedTo->BringToFront();
	}

	if (panel->switchedFrom) {
		panel->switchedFrom->state |= UI_STATE_BLOCK_INTERACTION;
		UIMaybeRemoveFocusedElement(panel->window);
	}

	if (transitionType == ES_TRANSITION_NONE || panel->switchedFrom == panel->switchedTo || !panel->transitionLengthMs) {
		PanelSwitcherTransitionComplete(panel);
	} else {
		panel->StartAnimating();
	}

	EsElementRelayout(panel);
}

void EsPanelStartMovementAnimation(EsPanel *panel, float timeMultiplier) {
	// TODO Custom smoothing functions.

	uint32_t timeMs = timeMultiplier * GetConstantNumber("transitionTime");
	if (!timeMs) return;
	EsMessageMutexCheck();
	EsAssert(~panel->flags & ES_PANEL_SWITCHER); // Use EsPanelSwitchTo!
	panel->transitionTimeMs = 0;
	panel->transitionLengthMs = timeMs; 
	panel->StartAnimating();
	panel->movementItems.Free();

	for (uintptr_t i = 0; i < panel->GetChildCount(); i++) {
		EsElement *element = panel->GetChild(i);

		if (element->flags & ES_ELEMENT_NON_CLIENT) {
			continue;
		}

		PanelMovementItem item = {};
		item.element = element;
		item.oldBounds = ES_RECT_4(element->offsetX, element->offsetX + element->width, 
				element->offsetY, element->offsetY + element->height);
		item.wasHidden = element->flags & ES_ELEMENT_HIDDEN;
		panel->movementItems.Add(item);
	}

	EsElementRelayout(panel);
}

// --------------------------------- Canvas panes.

struct EsCanvasPane : EsElement {
	double panX, panY, zoom;
	bool zoomFit, contentsChanged, center;
	int previousWidth, previousHeight;
};

EsElement *CanvasPaneGetCanvas(EsElement *element) {
	for (uintptr_t i = 0; i < element->GetChildCount(); i++) {
		if (~element->GetChild(i)->flags & ES_ELEMENT_NON_CLIENT) {
			return element->GetChild(i);
		}
	}

	return nullptr;
}

int ProcessCanvasPaneMessage(EsElement *element, EsMessage *message) {
	EsCanvasPane *pane = (EsCanvasPane *) element;

	if (message->type == ES_MSG_LAYOUT) {
		EsElement *canvas = CanvasPaneGetCanvas(element);
		if (!canvas) return 0;
		
		EsRectangle bounds = element->GetBounds();
		EsRectangle insets = element->currentStyle->insets;
		bounds.l += insets.l, bounds.r -= insets.r;
		bounds.t += insets.t, bounds.b -= insets.b;

		pane->panX -= (Width(bounds) - pane->previousWidth) / 2 / pane->zoom;
		pane->panY -= (Height(bounds) - pane->previousHeight) / 2 / pane->zoom;
		pane->previousWidth = Width(bounds), pane->previousHeight = Height(bounds);

		int width = canvas->GetWidth(0), height = canvas->GetHeight(0);

		double minimumZoomX = 1, minimumZoomY = 1;
		if (width > Width(bounds)) minimumZoomX = (double) Width(bounds) / width;
		if (height > Height(bounds)) minimumZoomY = (double) Height(bounds) / height;
		double minimumZoom = minimumZoomX < minimumZoomY ? minimumZoomX : minimumZoomY;

		if (pane->zoom < minimumZoom || pane->contentsChanged || pane->zoomFit) {
			pane->zoom = minimumZoom;
			pane->zoomFit = true;
		}

		pane->contentsChanged = false;

		if (pane->panX < 0) pane->panX = 0;
		if (pane->panX > width - Width(bounds) / pane->zoom) pane->panX = width - Width(bounds) / pane->zoom;
		if (pane->panY < 0) pane->panY = 0;
		if (pane->panY > height - Height(bounds) / pane->zoom) pane->panY = height - Height(bounds) / pane->zoom;

		if (width * pane->zoom <= Width(bounds) || pane->center) {
			pane->panX = width / 2 - Width(bounds) / pane->zoom / 2;
		}

		if (height * pane->zoom <= Height(bounds) || pane->center) {
			pane->panY = height / 2 - Height(bounds) / pane->zoom / 2;
		}

		pane->center = false;

		int x = (int) (0.5f + LinearMap(pane->panX, pane->panX + Width(bounds) / pane->zoom, 0, Width(bounds), 0));
		int y = (int) (0.5f + LinearMap(pane->panY, pane->panY + Height(bounds) / pane->zoom, 0, Height(bounds), 0));	
		canvas->InternalMove(width, height, x + bounds.l, y + bounds.t);
	} else if (message->type == ES_MSG_PAINT) {
		EsElement *canvas = CanvasPaneGetCanvas(element);
		if (!canvas) return 0;

		UIStyle *style = GetStyle(MakeStyleKey(ES_STYLE_CANVAS_SHADOW, 0), true);
		EsRectangle shadow1 = ES_RECT_4PD(canvas->offsetX + style->preferredWidth, canvas->offsetY + canvas->height, 
				canvas->width, style->preferredHeight);
		EsRectangle shadow2 = ES_RECT_4PD(canvas->offsetX + canvas->width, canvas->offsetY + style->preferredHeight, 
				style->preferredWidth, canvas->height - style->preferredHeight);
		style->PaintLayers(message->painter, shadow1, THEME_CHILD_TYPE_ONLY, ES_FLAGS_DEFAULT);
		style->PaintLayers(message->painter, shadow2, THEME_CHILD_TYPE_ONLY, ES_FLAGS_DEFAULT);
	}

	return 0;
}

EsCanvasPane *EsCanvasPaneCreate(EsElement *parent, uint64_t flags, const EsStyle *style) {
	EsCanvasPane *pane = (EsCanvasPane *) EsHeapAllocate(sizeof(EsCanvasPane), true);
	pane->Initialise(parent, flags, ProcessCanvasPaneMessage, style);
	pane->cName = "canvas pane";
	pane->zoom = 1.0;
	return pane;
}

// --------------------------------- Text displays and textboxes.

#define TEXT_ELEMENTS
#include "text.cpp"
#undef TEXT_ELEMENTS

// --------------------------------- Announcements.

// TODO Different colored messages for info/warning/error.
// TODO Different hold times.

int AnnouncementMessage(EsElement *element, EsMessage *message) {
	EsWindow *window = (EsWindow *) element;

	if (message->type == ES_MSG_ANIMATE) {
		window->announcementTimeMs += message->animate.deltaMs;

		double progress = window->announcementTimeMs / GetConstantNumber("announcementDuration");

		if (progress > 1) {
			EsElementDestroy(window);
			return 0;
		}

		progress = 2 * progress - 1;
		progress = (1 + progress * progress * progress * progress * progress) * 0.5;

		double inOnly = 2 * (progress < 0.5 ? progress : 0.5);
		double inOut = progress < 0.5 ? progress * 2 : (2 - progress * 2);

		EsRectangle bounds = EsWindowGetBounds(window);
		int32_t height = Height(bounds);
		bounds.t = window->announcementBase.y - inOnly * GetConstantNumber("announcementMovement");
		bounds.b = bounds.t + height;

		EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, 0xFF * inOut, 0, ES_WINDOW_PROPERTY_ALPHA);
		EsSyscall(ES_SYSCALL_WINDOW_MOVE, window->handle, (uintptr_t) &bounds, 0, 
				ES_WINDOW_MOVE_ADJUST_TO_FIT_SCREEN | ES_WINDOW_MOVE_ALWAYS_ON_TOP | ES_WINDOW_MOVE_UPDATE_SCREEN);

		message->animate.complete = false;
	}

	return 0;
}

void EsAnnouncementShow(EsWindow *parent, uint64_t flags, int32_t x, int32_t y, const char *text, ptrdiff_t textBytes) {
	(void) flags;

	EsWindow *window = EsWindowCreate(nullptr, ES_WINDOW_TIP);
	window->messageUser = AnnouncementMessage;

	EsTextDisplay *display = EsTextDisplayCreate(window, ES_CELL_FILL, ES_STYLE_ANNOUNCEMENT, text, textBytes);

	int32_t width = display->GetWidth(0);
	int32_t height = display->GetHeight(width);

	EsRectangle parentBounds = {};
       	if (parent) parentBounds = EsWindowGetBounds(parent);
	EsRectangle bounds = ES_RECT_4PD(x - width / 2 + parentBounds.l, y - height + parentBounds.t, width, height);
	EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, 0x00, 0, ES_WINDOW_PROPERTY_ALPHA);
	EsSyscall(ES_SYSCALL_WINDOW_MOVE, window->handle, (uintptr_t) &bounds, 0, ES_WINDOW_MOVE_ADJUST_TO_FIT_SCREEN | ES_WINDOW_MOVE_ALWAYS_ON_TOP);
	window->announcementBase.y = EsWindowGetBounds(window).t;
	window->StartAnimating();
}

// --------------------------------- Buttons.

int ProcessButtonMessage(EsElement *element, EsMessage *message) {
	EsButton *button = (EsButton *) element;

	if (message->type == ES_MSG_PAINT) {
		EsDrawContent(message->painter, element, 
			ES_RECT_2S(message->painter->width, message->painter->height), 
			button->label, button->labelBytes, button->iconID, 
			(button->flags & ES_BUTTON_DROPDOWN) ? ES_DRAW_CONTENT_MARKER_DOWN_ARROW : ES_FLAGS_DEFAULT);
	} else if (message->type == ES_MSG_GET_WIDTH) {
		if (!button->measurementCache.Get(message, &button->state)) {
			int stringWidth = button->currentStyle->MeasureTextWidth(button->label, button->labelBytes);
			int iconWidth = button->iconID ? button->currentStyle->metrics->iconSize : 0;
			int contentWidth = stringWidth + iconWidth + ((stringWidth && iconWidth) ? button->currentStyle->gapMinor : 0)
				+ button->currentStyle->insets.l + button->currentStyle->insets.r;

			if (button->flags & ES_BUTTON_DROPDOWN) {
				int64_t width = 0;
				GetPreferredSizeFromStylePart(ES_STYLE_MARKER_DOWN_ARROW, &width, nullptr);
				contentWidth += width + button->currentStyle->gapMinor;
			}

			int minimumReportedWidth = GetConstantNumber("pushButtonMinimumReportedWidth");
			if (button->flags & ES_BUTTON_MENU_ITEM) minimumReportedWidth = GetConstantNumber("menuItemMinimumReportedWidth");
			if (!stringWidth || (button->flags & ES_BUTTON_COMPACT)) minimumReportedWidth = 0;

			message->measure.width = minimumReportedWidth > contentWidth ? minimumReportedWidth : contentWidth;

			button->measurementCache.Store(message);
		}
	} else if (message->type == ES_MSG_DESTROY) {
		EsHeapFree(button->label);

		if (button->command) {
			Array<EsElement *> elements = { button->command->elements };
			elements.FindAndDeleteSwap(button, true);
			button->command->elements = elements.array;
		}
	} else if (message->type == ES_MSG_MOUSE_LEFT_DOWN) {
	} else if (message->type == ES_MSG_MOUSE_LEFT_CLICK) {
		if (button->flags & ES_BUTTON_CHECKBOX) {
			button->customStyleState &= ~THEME_STATE_INDETERMINATE;
			button->customStyleState ^= THEME_STATE_CHECKED;
		} else if (button->flags & ES_BUTTON_RADIOBOX) {
			button->customStyleState |= THEME_STATE_CHECKED;

			EsMessage m = { ES_MSG_RADIO_GROUP_UPDATED };

			for (uintptr_t i = 0; i < button->parent->GetChildCount(); i++) {
				if (button->parent->GetChild(i) != button) {
					EsMessageSend(button->parent->GetChild(i), &m);
				}
			}
		}

		if (button->checkBuddy) {
			EsElementSetDisabled(button->checkBuddy, !(button->customStyleState & (THEME_STATE_CHECKED | THEME_STATE_INDETERMINATE)));
		}

		if (button->onCommand) {
			button->onCommand(button->instance, button, button->command);
		}

		if (button->flags & ES_BUTTON_MENU_ITEM) {
			button->window->Destroy();
		} else {
			button->MaybeRefreshStyle();
		}
	} else if (message->type == ES_MSG_GET_ACCESS_KEY_HINT_BOUNDS && (button->flags & (ES_BUTTON_RADIOBOX | ES_BUTTON_CHECKBOX))) {
		EsRectangle bounds = element->GetWindowBounds();
		int width = Width(*message->accessKeyHintBounds), height = Height(*message->accessKeyHintBounds);
		int x = bounds.l - 3 * width / 4, y = (bounds.t + bounds.b) / 2 - height / 4;
		*message->accessKeyHintBounds = ES_RECT_4(x - width / 2, x + width / 2, y - height / 4, y + 3 * height / 4);
	} else if (message->type == ES_MSG_RADIO_GROUP_UPDATED && (button->flags & ES_BUTTON_RADIOBOX)) {
		EsButtonSetCheck(button, ES_CHECK_UNCHECKED);
	} else if (message->type == ES_MSG_FOCUSED_START) {
		if (button->window->defaultEnterButton && (button->flags & ES_BUTTON_PUSH)) {
			button->window->enterButton->customStyleState &= ~THEME_STATE_DEFAULT_BUTTON;
			button->window->enterButton->MaybeRefreshStyle();
			button->customStyleState |= THEME_STATE_DEFAULT_BUTTON;
			button->window->enterButton = button;
		}
	} else if (message->type == ES_MSG_FOCUSED_END) {
		if (button->window->enterButton == button) {
			button->customStyleState &= ~THEME_STATE_DEFAULT_BUTTON;
			button->window->enterButton = button->window->defaultEnterButton;
			button->window->enterButton->customStyleState |= THEME_STATE_DEFAULT_BUTTON;
			button->window->enterButton->MaybeRefreshStyle();
		}
	} else if (message->type == ES_MSG_GET_INSPECTOR_INFORMATION) {
		EsBufferFormat(message->getContent.buffer, "'%s'", button->labelBytes, button->label);
	} else {
		return 0;
	}

	return ES_HANDLED;
}

EsButton *EsButtonCreate(EsElement *parent, uint64_t flags, const EsStyle *style, const char *label, ptrdiff_t labelBytes) {
	EsButton *button = (EsButton *) EsHeapAllocate(sizeof(EsButton), true);

	if (!style) {
		if (flags & ES_BUTTON_MENU_ITEM) {
			if (flags & ES_MENU_ITEM_HEADER) {
				style = ES_STYLE_MENU_ITEM_HEADER;
			} else {
				style = ES_STYLE_MENU_ITEM_NORMAL;
			}
		} else if (flags & ES_BUTTON_TOOLBAR) {
			style = ES_STYLE_PUSH_BUTTON_TOOLBAR;
		} else if (flags & ES_BUTTON_CHECKBOX) {
			style = ES_STYLE_CHECKBOX_NORMAL;
		} else if (flags & ES_BUTTON_RADIOBOX) {
			style = ES_STYLE_CHECKBOX_RADIOBOX;
		} else if (flags & ES_BUTTON_DEFAULT) {
			style = ES_STYLE_PUSH_BUTTON_NORMAL;
		} else {
			style = ES_STYLE_PUSH_BUTTON_NORMAL;
		}

		style = UIGetDefaultStyleVariant(style, parent);
	}

	if (style == ES_STYLE_PUSH_BUTTON_NORMAL || style == ES_STYLE_PUSH_BUTTON_DANGEROUS) {
		flags |= ES_BUTTON_PUSH;
	} else if (style == ES_STYLE_PUSH_BUTTON_TOOLBAR || style == ES_STYLE_PUSH_BUTTON_TOOLBAR_MEDIUM 
			|| style == ES_STYLE_PUSH_BUTTON_TOOLBAR_BIG || style == ES_STYLE_PUSH_BUTTON_STATUS_BAR) {
		flags |= ES_BUTTON_COMPACT | ES_BUTTON_NOT_FOCUSABLE;
	} else if (style == ES_STYLE_CHECKBOX_NORMAL || style == ES_STYLE_CHECKBOX_RADIOBOX) {
		flags |= ES_BUTTON_COMPACT;
	}

	if (~flags & ES_BUTTON_NOT_FOCUSABLE) {
		flags |= ES_ELEMENT_FOCUSABLE;
	}

	button->Initialise(parent, flags, ProcessButtonMessage, style);
	button->cName = "button";

	if (flags & ES_BUTTON_DEFAULT) {
		button->window->defaultEnterButton = button;
		button->window->enterButton = button;
		button->customStyleState |= THEME_STATE_DEFAULT_BUTTON;
	} else if (flags & ES_BUTTON_CANCEL) {
		button->window->escapeButton = button;
	}

	if (labelBytes == -1) labelBytes = EsCStringLength(label);
	HeapDuplicate((void **) &button->label, label, labelBytes);
	button->labelBytes = labelBytes;

	if ((flags & ES_BUTTON_MENU_ITEM) && (flags & ES_MENU_ITEM_HEADER)) {
		EsElementSetDisabled(button, true);
	}

	EsButtonSetCheck(button, (EsCheckState) (flags & 3), false);

	return button;
}

void EsButtonSetIcon(EsButton *button, uint32_t iconID) {
	EsMessageMutexCheck();

	button->iconID = iconID;
	button->Repaint(true);
}

void EsButtonOnCommand(EsButton *button, EsCommandCallback onCommand, EsCommand *command) {
	EsMessageMutexCheck();

	button->onCommand = onCommand;
	button->command = command;
}

void EsButtonSetCheckBuddy(EsButton *button, EsElement *checkBuddy) {
	EsMessageMutexCheck();

	button->checkBuddy = checkBuddy;
	EsElementSetDisabled(button->checkBuddy, !(button->customStyleState & (THEME_STATE_CHECKED | THEME_STATE_INDETERMINATE)));
}

EsElement *EsButtonGetCheckBuddy(EsButton *button) {
	EsMessageMutexCheck();

	return button->checkBuddy;
}

EsCheckState EsButtonGetCheck(EsButton *button) {
	EsMessageMutexCheck();

	if (button->customStyleState & THEME_STATE_CHECKED)       return ES_CHECK_CHECKED;
	if (button->customStyleState & THEME_STATE_INDETERMINATE) return ES_CHECK_INDETERMINATE;
	return ES_CHECK_UNCHECKED;
}

void EsButtonSetCheck(EsButton *button, EsCheckState checkState, bool sendUpdatedMessage) {
	if (checkState == EsButtonGetCheck(button)) {
		return;
	}

	button->customStyleState &= ~(THEME_STATE_CHECKED | THEME_STATE_INDETERMINATE);

	if (checkState == ES_CHECK_CHECKED)       button->customStyleState |= THEME_STATE_CHECKED;
	if (checkState == ES_CHECK_INDETERMINATE) button->customStyleState |= THEME_STATE_INDETERMINATE;

	if (sendUpdatedMessage) {
		EsMessage m = { ES_MSG_CHECK_UPDATED };
		m.checkState = checkState;
		EsMessageSend(button, &m);

		if (button->onCommand) {
			button->onCommand(button->instance, button, button->command);
		}
	}

	if (button->checkBuddy) {
		EsElementSetDisabled(button->checkBuddy, !(button->customStyleState & (THEME_STATE_CHECKED | THEME_STATE_INDETERMINATE)));
	}

	button->MaybeRefreshStyle();
}

void EsMenuAddItem(EsMenu *menu, uint64_t flags, const char *label, ptrdiff_t labelBytes, EsMenuCallback callback, EsGeneric context) {
	EsButton *button = (EsButton *) EsButtonCreate(menu, 
		ES_BUTTON_NOT_FOCUSABLE | ES_BUTTON_MENU_ITEM | ES_CELL_H_FILL | flags, 0,
		label, labelBytes != -1 ? labelBytes : EsCStringLength(label));
	button->userData = (void *) callback;

	button->messageUser = [] (EsElement *element, EsMessage *message) {
		if (message->type == ES_MSG_MOUSE_LEFT_CLICK) {
			EsMenuCallback callback = (EsMenuCallback) element->userData.p;
			if (callback) callback((EsMenu *) element->window, ((EsButton *) element)->menuItemContext);
		}

		return 0;
	};

	button->menuItemContext = context;

	if (flags & ES_MENU_ITEM_CHECKED) {
		EsButtonSetCheck(button, ES_CHECK_CHECKED);
	}
}

void EsMenuAddCommand(EsMenu *menu, uint64_t flags, const char *label, ptrdiff_t labelBytes, EsCommand *command) {
	EsButton *button = (EsButton *) EsButtonCreate(menu, 
			ES_BUTTON_NOT_FOCUSABLE | ES_BUTTON_MENU_ITEM | ES_CELL_H_FILL | flags, 
			0, label, labelBytes);
	EsCommandAddButton(command, button);
}

// --------------------------------- Color wells and pickers.

struct EsColorWell : EsElement {
	uint32_t color;
	struct ColorPicker *picker;
	bool indeterminate;
};

int ProcessColorWellMessage(EsElement *element, EsMessage *message);

struct ColorPicker {
	uint32_t color;
	float hue, saturation, value, opacity;
	float dragStartHue, dragStartSaturation;
	int dragComponent;
	bool indeterminateBeforeEyedrop;
	bool modified;

	ColorPickerHost host;
	EsPanel *panel;
	EsElement *circle, *slider, *circlePoint, *sliderPoint, *opacitySlider, *opacitySliderPoint;
	EsTextbox *textbox;

	uint32_t GetColorForHost() {
		return color | (host.hasOpacity ? ((uint32_t) (255.0f * opacity) << 24) : 0);
	}

	void Sync(EsElement *excluding) {
		if (excluding != textbox && textbox) {
			char string[16];
			size_t length;

			if (host.indeterminate && *host.indeterminate) {
				string[0] = '#';
				length = 1;
			} else {
				const char *hexChars = "0123456789ABCDEF";

				if (host.hasOpacity) {
					uint8_t alpha = (uint8_t) (opacity * 0xFF);
					length = EsStringFormat(string, sizeof(string), "#%c%c%c%c%c%c%c%c", 
							hexChars[(alpha >> 4) & 0xF], hexChars[(alpha >> 0) & 0xF], hexChars[(color >> 20) & 0xF], hexChars[(color >> 16) & 0xF], 
							hexChars[(color >> 12) & 0xF], hexChars[(color >> 8) & 0xF], hexChars[(color >> 4) & 0xF], hexChars[(color >> 0) & 0xF]);
				} else {
					length = EsStringFormat(string, sizeof(string), "#%c%c%c%c%c%c", 
							hexChars[(color >> 20) & 0xF], hexChars[(color >> 16) & 0xF], hexChars[(color >> 12) & 0xF], 
							hexChars[(color >> 8) & 0xF], hexChars[(color >> 4) & 0xF], hexChars[(color >> 0) & 0xF]);
				}
			}

			EsTextboxSelectAll(textbox);
			EsTextboxInsert(textbox, string, length, false);
		}

		if (excluding != circle) circle->Repaint(true);
		if (excluding != slider) slider->Repaint(true);
		if (excluding != opacitySlider && opacitySlider) opacitySlider->Repaint(true);

		if (excluding != circlePoint) {
			if (host.indeterminate && *host.indeterminate) {
				EsElementSetHidden(circlePoint, true);
			} else {
				EsElementSetHidden(circlePoint, false);
				float x = saturation * EsCRTcosf((hue - 3) * 1.047197551) * 0.5f + 0.5f;
				float y = saturation * EsCRTsinf((hue - 3) * 1.047197551) * 0.5f + 0.5f;
				EsRectangle pointSize = EsElementGetPreferredSize(circlePoint), circleSize = EsElementGetPreferredSize(circle);
				int x2 = x * circleSize.r - pointSize.r / 2, y2 = y * circleSize.b - pointSize.b / 2;
				EsElementMove(circlePoint, x2, y2, pointSize.r, pointSize.b, true);
				circlePoint->Repaint(true);
			}
		}

		if (excluding != sliderPoint) {
			if (host.indeterminate && *host.indeterminate) {
				EsElementSetHidden(sliderPoint, true);
			} else {
				EsElementSetHidden(sliderPoint, false);
				float x = 0.5f, y = 1.0f - EsCRTpowf(value, 1.333f);
				EsRectangle pointSize = EsElementGetPreferredSize(sliderPoint), sliderSize = EsElementGetPreferredSize(slider);
				int x2 = x * sliderSize.r - pointSize.r / 2, y2 = y * sliderSize.b - pointSize.b / 2;
				EsElementMove(sliderPoint, x2, y2, pointSize.r, pointSize.b, true);
				sliderPoint->Repaint(true);
			}
		}

		if (excluding != opacitySliderPoint && opacitySliderPoint) {
			if (host.indeterminate && *host.indeterminate) {
				EsElementSetHidden(opacitySliderPoint, true);
			} else {
				EsElementSetHidden(opacitySliderPoint, false);
				float x = 0.5f, y = 1.0f - opacity;
				EsRectangle pointSize = EsElementGetPreferredSize(opacitySliderPoint), sliderSize = EsElementGetPreferredSize(opacitySlider);
				int x2 = x * sliderSize.r - pointSize.r / 2, y2 = y * sliderSize.b - pointSize.b / 2;
				EsElementMove(opacitySliderPoint, x2, y2, pointSize.r, pointSize.b, true);
				opacitySliderPoint->Repaint(true);
			}
		}

		if (excluding != host.well && host.well) {
			if (host.well->messageClass == ProcessColorWellMessage) {
				((EsColorWell *) host.well)->color = GetColorForHost();
				host.well->Repaint(true);
			}

			EsMessage m = { ES_MSG_COLOR_CHANGED };
			m.colorChanged.newColor = GetColorForHost();
			m.colorChanged.pickerClosed = false;
			EsMessageSend(host.well, &m);

			modified = true;
		}
	}

	void PositionOnCircleToColor(int _x, int _y) {
		EsRectangle size = EsElementGetInsetSize(circle);
		float x = (float) _x / (float) (size.r  - 1) * 2.0f - 1.0f;
		float y = (float) _y / (float) (size.b - 1) * 2.0f - 1.0f;
		float newSaturation = EsCRTsqrtf(x * x + y * y), newHue = EsCRTatan2f(y, x) * 0.954929659f + 3;
		if (!EsKeyboardIsAltHeld() && newSaturation < 0.1f) newSaturation = 0;
		if (newSaturation > 1) newSaturation = 1;
		if (newHue >= 6) newHue -= 6;
		if (newHue < 0 || newHue >= 6) newHue = 0;

		if (EsKeyboardIsShiftHeld()) {
			float deltaHue = dragStartHue - newHue;
			float deltaSaturation = EsCRTfabs(dragStartSaturation - newSaturation);

			if (-3 < deltaHue && deltaHue < 3) deltaHue = EsCRTfabs(deltaHue);
			if (deltaHue < -3) deltaHue += 6;
			if (deltaHue >  3) deltaHue = -deltaHue + 6;
			deltaHue /= 2;

			if (deltaHue < deltaSaturation) {
				newHue = dragStartHue;
			} else {
				newSaturation = dragStartSaturation;
			}
		}

		uint32_t newColor = EsColorConvertToRGB(newHue, newSaturation, value);
		hue = newHue, color = newColor, saturation = newSaturation;
		if (host.indeterminate) *host.indeterminate = false;
		Sync(circle);
	}

	void PositionOnSliderToColor(int _x, int _y) {
		(void) _x;
		EsRectangle size = EsElementGetInsetSize(slider);
		float y = 1 - (float) _y / (float) (size.b - 1);
		if (y < 0) y = 0;
		y = EsCRTsqrtf(y) * EsCRTsqrtf(EsCRTsqrtf(y));
		if (y > 1) y = 1;
		if (y < 0) y = 0;
		uint32_t newColor = EsColorConvertToRGB(hue, saturation, y);
		color = newColor;
		value = y;
		if (host.indeterminate) *host.indeterminate = false;
		Sync(slider);
	}

	void PositionOnOpacitySliderToColor(int _x, int _y) {
		(void) _x;
		EsRectangle size = EsElementGetInsetSize(opacitySlider);
		float y = 1 - (float) _y / (float) (size.b - 1);
		if (y > 1) y = 1;
		if (y < 0) y = 0;
		opacity = y;
		if (host.indeterminate) *host.indeterminate = false;
		Sync(opacitySlider);
	}
};

int ProcessColorChosenPointMessage(EsElement *element, EsMessage *message) {
	ColorPicker *picker = (ColorPicker *) element->userData.p;

	if (message->type == ES_MSG_PAINT) {
		EsRectangle bounds = EsPainterBoundsInset(message->painter);
		EsStyledBox box = {};
		box.bounds = bounds;
		box.clip = message->painter->clip;
		box.borderColor = 0xFFFFFFFF;
		box.backgroundColor = picker->color | 0xFF000000;
		box.backgroundColor2 = picker->color | ((uint32_t) (255.0f * picker->opacity) << 24);
		box.borders = ES_RECT_1(2);
		box.cornerRadiusTopLeft = box.cornerRadiusTopRight = box.cornerRadiusBottomLeft = box.cornerRadiusBottomRight = Width(box.bounds) / 2;

		if (picker->opacity < 1 && picker->host.hasOpacity) {
			box.fragmentShader = [] (int x, int y, EsStyledBox *box) -> uint32_t {
				// TODO Move the alpha background as the chosen point moves.
				return EsColorBlend(((((x - 2) >> 3) ^ ((y + 5) >> 3)) & 1) ? 0xFFFFFFFF : 0xFFC0C0C0, 
						box->backgroundColor2, false);
			};
		}

		DrawStyledBox(message->painter, box);
	}

	return 0;
}

void ColorPickerCreate(EsElement *parent, ColorPickerHost host, uint32_t initialColor, bool showTextbox) {
	ColorPicker *picker = (ColorPicker *) EsHeapAllocate(sizeof(ColorPicker), true);
	picker->host = host;
	picker->color = initialColor & 0xFFFFFF;
       	picker->opacity = (float) (initialColor >> 24) / 255.0f;
	if (host.well && host.well->messageClass == ProcessColorWellMessage) ((EsColorWell *) host.well)->picker = picker;
	EsColorConvertToHSV(picker->color, &picker->hue, &picker->saturation, &picker->value);
	
	picker->panel = EsPanelCreate(parent, ES_PANEL_HORIZONTAL | ES_PANEL_TABLE, ES_STYLE_COLOR_PICKER_MAIN_PANEL);

	picker->panel->userData = picker;

	picker->panel->messageUser = [] (EsElement *element, EsMessage *message) {
		ColorPicker *picker = (ColorPicker *) element->userData.p;

		if (message->type == ES_MSG_DESTROY) {
			if (picker->host.well && picker->modified) {
				EsMessage m = { ES_MSG_COLOR_CHANGED };
				m.colorChanged.newColor = picker->GetColorForHost();
				m.colorChanged.pickerClosed = true;
				EsMessageSend(picker->host.well, &m);

				if (picker->host.well->messageClass == ProcessColorWellMessage) {
					((EsColorWell *) picker->host.well)->picker = nullptr;
				}
			}

			EsHeapFree(picker); 
		}

		return 0;
	};

	bool hasOpacity = picker->host.hasOpacity;

	EsPanelSetBands(picker->panel, hasOpacity ? 3 : 2, showTextbox ? 2 : 1);

	picker->circle = EsCustomElementCreate(picker->panel, ES_ELEMENT_FOCUSABLE | ES_ELEMENT_NOT_TAB_TRAVERSABLE, ES_STYLE_COLOR_CIRCLE);

	picker->circle->cName = "hue-saturation wheel";
	picker->circle->userData = picker;

	picker->circle->messageUser = [] (EsElement *element, EsMessage *message) {
		ColorPicker *picker = (ColorPicker *) element->userData.p;

		if (message->type == ES_MSG_PAINT) {
			// EsPerformanceTimerPush();

			EsPainter *painter = message->painter;
			EsRectangle bounds = EsPainterBoundsInset(painter);
			EsRectangle clip = painter->clip;
			EsRectangleClip(clip, bounds, &clip);
			uint32_t stride = painter->target->stride;
			uint32_t *bitmap = (uint32_t *) painter->target->bits;
			float epsilon = 1.0f / (bounds.b - bounds.t - 1);

			for (int j = clip.t; j < clip.b; j++) {
				for (int i = clip.l; i < clip.r; i++) {
					float x = (float) (i - bounds.l) / (float) (bounds.r - bounds.l - 1) * 2.0f - 1.0f;
					float y = (float) (j - bounds.t)  / (float) (bounds.b - bounds.t - 1) * 2.0f - 1.0f;
					float radius = EsCRTsqrtf(x * x + y * y), hue = EsCRTatan2f(y, x) * 0.954929659f + 3;
					if (hue >= 6) hue -= 6;

					if (radius > 1.0f + epsilon) {
						// Outside the circle.
					} else if (radius > 1.0f - epsilon) {
						// On the edge.
						uint32_t over = EsColorConvertToRGB(hue, 1, picker->value);
						// float opacity = (1.0f - ((radius - (1.0f - epsilon)) / epsilon * 0.5f));
						float opacity = 0.5f - (radius - 1.0f) / epsilon * 0.5f;
						uint32_t alpha = (((uint32_t) (255.0f * opacity)) & 0xFF) << 24;
						uint32_t *under = &bitmap[i + j * (stride >> 2)];
						*under = EsColorBlend(*under, over | alpha, true);
					} else {
						// Inside the circle.
						uint32_t over = EsColorConvertToRGB(hue, radius, picker->value);
						bitmap[i + j * (stride >> 2)] = over | 0xFF000000;
					}
				}
			}

			// EsPrint("Rendered color circle in %*Fms.\n", 3, 1000 * EsPerformanceTimerPop());
		} else if (message->type == ES_MSG_HIT_TEST) {
			EsRectangle size = EsElementGetInsetSize(element);
			float x = (float) message->hitTest.x / (float) (size.r  - 1) * 2.0f - 1.0f;
			float y = (float) message->hitTest.y / (float) (size.b - 1) * 2.0f - 1.0f;
			message->hitTest.inside = x * x + y * y <= 1;
		} else if (message->type == ES_MSG_MOUSE_LEFT_DRAG) {
			picker->PositionOnCircleToColor(message->mouseDragged.newPositionX, message->mouseDragged.newPositionY);
		} else if (message->type == ES_MSG_KEY_DOWN) {
			if ((message->keyboard.scancode == ES_SCANCODE_LEFT_SHIFT || message->keyboard.scancode == ES_SCANCODE_RIGHT_SHIFT) && !message->keyboard.repeat) {
				picker->dragStartHue = picker->hue;
				picker->dragStartSaturation = picker->saturation;
			} else {
				return 0;
			}
		} else if (message->type == ES_MSG_MOUSE_LEFT_DOWN) {
			picker->PositionOnCircleToColor(message->mouseDown.positionX, message->mouseDown.positionY);
			picker->dragStartHue = picker->hue;
			picker->dragStartSaturation = picker->saturation;
		} else {
			return 0;
		}

		return ES_HANDLED;
	};

	picker->circlePoint = EsCustomElementCreate(picker->circle, ES_ELEMENT_NO_HOVER, ES_STYLE_COLOR_CHOSEN_POINT);
	picker->circlePoint->messageUser = ProcessColorChosenPointMessage;
	picker->circlePoint->cName = "selected hue-saturation";
	picker->circlePoint->userData = picker;

	picker->slider = EsCustomElementCreate(picker->panel, ES_ELEMENT_FOCUSABLE | ES_ELEMENT_NOT_TAB_TRAVERSABLE, ES_STYLE_COLOR_SLIDER);
	picker->slider->cName = "value slider";
	picker->slider->userData = picker;

	picker->slider->messageUser = [] (EsElement *element, EsMessage *message) {
		ColorPicker *picker = (ColorPicker *) element->userData.p;

		if (message->type == ES_MSG_PAINT) {
			// EsPerformanceTimerPush();

			EsPainter *painter = message->painter;
			EsRectangle bounds = EsPainterBoundsInset(painter);
			EsRectangle clip = painter->clip;
			EsRectangleClip(clip, bounds, &clip);
			uint32_t stride = painter->target->stride;
			uint32_t *bitmap = (uint32_t *) painter->target->bits;

			float valueIncrement = -1.0f / (bounds.b - bounds.t - 1), value = 1.0f;

			for (int j = clip.t; j < clip.b; j++, value += valueIncrement) {
				// float valueSqrt = EsCRTsqrtf(value);
				// uint32_t color = EsColorConvertToRGB(picker->hue, picker->saturation, valueSqrt * EsCRTsqrtf(valueSqrt));

				for (int i = clip.l; i < clip.r; i++) {
					float i2 = (float) (i - ((bounds.l + bounds.r) >> 1)) / (float) (bounds.r - bounds.l);
					uint32_t color = EsColorConvertToRGB(picker->hue, picker->saturation, EsCRTpowf(value, 0.75f + i2 * i2 * 0.3f));
					bitmap[i + j * (stride >> 2)] = 0xFF000000 | color;
				}
			}

			// EsPrint("Rendered color slider in %*Fms.\n", 3, 1000 * EsPerformanceTimerPop());
		} else if (message->type == ES_MSG_MOUSE_LEFT_DRAG) {
			picker->PositionOnSliderToColor(message->mouseDragged.newPositionX, message->mouseDragged.newPositionY);
		} else if (message->type == ES_MSG_MOUSE_LEFT_DOWN) {
			picker->PositionOnSliderToColor(message->mouseDown.positionX, message->mouseDown.positionY);
		} else {
			return 0;
		}

		return ES_HANDLED;
	};

	picker->sliderPoint = EsCustomElementCreate(picker->slider, ES_ELEMENT_NO_HOVER, ES_STYLE_COLOR_CHOSEN_POINT);
	picker->sliderPoint->messageUser = ProcessColorChosenPointMessage;
	picker->sliderPoint->cName = "selected value";
	picker->sliderPoint->userData = picker;

	if (hasOpacity) {
		picker->opacitySlider = EsCustomElementCreate(picker->panel, ES_ELEMENT_FOCUSABLE | ES_ELEMENT_NOT_TAB_TRAVERSABLE, ES_STYLE_COLOR_SLIDER);
		picker->opacitySlider->cName = "opacity slider";
		picker->opacitySlider->userData = picker;

		picker->opacitySlider->messageUser = [] (EsElement *element, EsMessage *message) {
			ColorPicker *picker = (ColorPicker *) element->userData.p;

			if (message->type == ES_MSG_PAINT) {
				// EsPerformanceTimerPush();

				EsPainter *painter = message->painter;
				EsRectangle bounds = EsPainterBoundsInset(painter);
				EsRectangle clip = painter->clip;
				EsRectangleClip(clip, bounds, &clip);
				uint32_t stride = painter->target->stride;
				uint32_t *bitmap = (uint32_t *) painter->target->bits;

				float opacityIncrement = -1.0f / (bounds.b - bounds.t - 1), opacity = 1.0f;

				for (int j = clip.t; j < clip.b; j++, opacity += opacityIncrement) {
					uint32_t alpha = (uint32_t) (opacity * 255.0f) << 24;

					for (int i = clip.l; i < clip.r; i++) {
						bitmap[i + j * (stride >> 2)] 
							= EsColorBlend(((((i - bounds.l + 1) >> 3) ^ ((j - bounds.t + 2) >> 3)) & 1) ? 0xFFFFFFFF : 0xFFC0C0C0, 
									alpha | (picker->color & 0xFFFFFF), false);
					}
				}

				// EsPrint("Rendered opacity slider in %*Fms.\n", 3, 1000 * EsPerformanceTimerPop());
			} else if (message->type == ES_MSG_MOUSE_LEFT_DRAG) {
				picker->PositionOnOpacitySliderToColor(message->mouseDragged.newPositionX, message->mouseDragged.newPositionY);
			} else if (message->type == ES_MSG_MOUSE_LEFT_DOWN) {
				picker->PositionOnOpacitySliderToColor(message->mouseDown.positionX, message->mouseDown.positionY);
			} else {
				return 0;
			}

			return ES_HANDLED;
		};

		picker->opacitySliderPoint = EsCustomElementCreate(picker->opacitySlider, ES_ELEMENT_NO_HOVER, ES_STYLE_COLOR_CHOSEN_POINT);
		picker->opacitySliderPoint->messageUser = ProcessColorChosenPointMessage;
		picker->opacitySliderPoint->cName = "selected opacity";
		picker->opacitySliderPoint->userData = picker;
	}

	if (showTextbox) {
		picker->textbox = EsTextboxCreate(picker->panel, ES_TEXTBOX_EDIT_BASED | ES_CELL_EXPAND | ES_TEXTBOX_NO_SMART_CONTEXT_MENUS, ES_STYLE_COLOR_HEX_TEXTBOX);
		picker->textbox->userData = picker;

		picker->textbox->messageUser = [] (EsElement *element, EsMessage *message) {
			ColorPicker *picker = (ColorPicker *) element->userData.p;

			if (message->type == ES_MSG_TEXTBOX_UPDATED) {
				size_t bytes;
				char *string = EsTextboxGetContents(picker->textbox, &bytes);
				uint32_t color = EsColorParse(string, bytes);
				picker->opacity = (float) (color >> 24) / 255.0f;
				color &= 0xFFFFFF;
				EsColorConvertToHSV(color, &picker->hue, &picker->saturation, &picker->value);
				picker->color = color;
				if (picker->host.indeterminate) *picker->host.indeterminate = false;
				picker->Sync(picker->textbox);
				EsHeapFree(string);
			} else if (message->type == ES_MSG_TEXTBOX_EDIT_START) {
				EsTextboxSetSelection((EsTextbox *) element, 0, 1, 0, -1);
			} else if (message->type == ES_MSG_TEXTBOX_EDIT_END) {
				picker->Sync(nullptr);
			} else if (message->type == ES_MSG_TEXTBOX_NUMBER_DRAG_DELTA) {
				int componentCount = picker->host.hasOpacity ? 4 : 3;
				int componentIndex = (message->numberDragDelta.hoverCharacter - 1) / 2;
				if (componentIndex < 0) componentIndex = 0;
				if (componentIndex >= componentCount) componentIndex = componentCount - 1;
				componentIndex = componentCount - componentIndex - 1;
				picker->dragComponent = componentIndex;

				if (componentIndex == 3) {
					picker->opacity += message->numberDragDelta.delta / 255.0f;
					if (picker->opacity < 0) picker->opacity = 0;
					if (picker->opacity > 1) picker->opacity = 1;
				} else {
					int32_t componentValue = (picker->color >> (componentIndex << 3)) & 0xFF;
					componentValue += message->numberDragDelta.delta;
					if (componentValue < 0) componentValue = 0;
					if (componentValue > 255) componentValue = 255;
					picker->color &= ~(0xFF << (componentIndex << 3));
					picker->color |= (uint32_t) componentValue << (componentIndex << 3);
					EsColorConvertToHSV(picker->color, &picker->hue, &picker->saturation, &picker->value);
				}

				picker->Sync(nullptr);
			} else {
				return 0;
			}

			return ES_HANDLED;
		};

		EsTextboxUseNumberOverlay(picker->textbox, false);

		EsButton *eyedropperButton = EsButtonCreate(picker->panel, ES_CELL_EXPAND, 0);
		eyedropperButton->userData = picker;

		eyedropperButton->messageUser = [] (EsElement *element, EsMessage *message) {
			ColorPicker *picker = (ColorPicker *) element->userData.p;

			if (message->type == ES_MSG_MOUSE_LEFT_CLICK) {
				picker->indeterminateBeforeEyedrop = picker->host.indeterminate && *picker->host.indeterminate;
				EsSyscall(ES_SYSCALL_EYEDROP_START, (uintptr_t) element, picker->circle->window->handle, picker->color, 0);
			} else if (message->type == ES_MSG_EYEDROP_REPORT) {
				if (message->eyedrop.cancelled && picker->indeterminateBeforeEyedrop) {
					if (picker->host.well && picker->host.well->messageClass == ProcessColorWellMessage) {
						EsColorWellSetIndeterminate((EsColorWell *) picker->host.well);
					}
				} else {
					picker->color = message->eyedrop.color;
					EsColorConvertToHSV(picker->color, &picker->hue, &picker->saturation, &picker->value);
					if (picker->host.indeterminate) *picker->host.indeterminate = false;
					picker->Sync(nullptr);
				}
			} else {
				return 0;
			}

			return ES_HANDLED;
		};

		EsButtonSetIcon(eyedropperButton, ES_ICON_COLOR_SELECT_SYMBOLIC);

		if (hasOpacity) {
			EsElementSetCellRange(eyedropperButton, 1, 1, 2, 1);
		}
	}

	picker->Sync(picker->host.well);
	if (picker->textbox) EsElementFocus(picker->textbox);
}

uint32_t EsColorWellGetRGB(EsColorWell *well) {
	EsMessageMutexCheck();

	return well->color & ((well->flags & ES_COLOR_WELL_HAS_OPACITY) ? 0xFFFFFFFF : 0x00FFFFFF);
}

void EsColorWellSetRGB(EsColorWell *well, uint32_t color, bool sendChangedMessage) {
	EsMessageMutexCheck();

	well->color = color;
	well->indeterminate = false;
	well->Repaint(true);

	if (sendChangedMessage) {
		EsMessage m = { ES_MSG_COLOR_CHANGED };
		m.colorChanged.newColor = color;
		m.colorChanged.pickerClosed = true;
		EsMessageSend(well, &m);
	}

	if (well->picker) {
		well->picker->color = color & 0xFFFFFF;
		well->picker->opacity = (color >> 24) / 255.0f;
		EsColorConvertToHSV(well->picker->color, &well->picker->hue, &well->picker->saturation, &well->picker->value);
		well->picker->Sync(well);
	}
}

void EsColorWellSetIndeterminate(EsColorWell *well) {
	EsMessageMutexCheck();

	well->color = 0xFFFFFFFF;
	well->indeterminate = true;
	well->Repaint(true);

	if (well->picker) {
		well->picker->color = 0xFFFFFF;
		well->picker->opacity = 1.0f;
		EsColorConvertToHSV(well->picker->color, &well->picker->hue, &well->picker->saturation, &well->picker->value);
		well->picker->Sync(well);
	}
}

int ProcessColorWellMessage(EsElement *element, EsMessage *message) {
	EsColorWell *well = (EsColorWell *) element;

	if (message->type == ES_MSG_PAINT) {
		EsRectangle bounds = EsPainterBoundsInset(message->painter);
		EsStyledBox box = {};
		box.bounds = bounds;
		box.clip = message->painter->clip;
		box.borders = ES_RECT_1(1);

		if (well->indeterminate) {
			box.backgroundColor = 0;
			box.borderColor = 0x40000000;
		} else {
			box.backgroundColor = well->color;
			if (~well->flags & ES_COLOR_WELL_HAS_OPACITY) box.backgroundColor |= 0xFF000000;
			box.borderColor = EsColorBlend(well->color | 0xFF000000, 0x40000000, false);

			if ((well->flags & ES_COLOR_WELL_HAS_OPACITY) && ((well->color & 0xFF000000) != 0xFF000000)) {
				box.fragmentShader = [] (int x, int y, EsStyledBox *box) -> uint32_t {
					return EsColorBlend(((((x - box->bounds.l - 4) >> 3) ^ ((y - box->bounds.t + 2) >> 3)) & 1) 
							? 0xFFFFFFFF : 0xFFC0C0C0, box->backgroundColor, false);
				};
			}
		}

		DrawStyledBox(message->painter, box);
	} else if (message->type == ES_MSG_MOUSE_LEFT_CLICK) {
		EsMenu *menu = EsMenuCreate(well, ES_FLAGS_DEFAULT);
		ColorPickerHost host = { well, &well->indeterminate, (well->flags & ES_COLOR_WELL_HAS_OPACITY) ? true : false };
		ColorPickerCreate((EsElement *) menu, host, well->color, true);
		EsMenuShow(menu);
	} else {
		return 0;
	}

	return ES_HANDLED;
}

EsColorWell *EsColorWellCreate(EsElement *parent, uint64_t flags, uint32_t initialColor) {
	EsColorWell *well = (EsColorWell *) EsHeapAllocate(sizeof(EsColorWell), true);
	well->color = initialColor;
	well->Initialise(parent, flags | ES_ELEMENT_FOCUSABLE, ProcessColorWellMessage, ES_STYLE_PUSH_BUTTON_NORMAL_COLOR_WELL);
	well->cName = "color well";
	return well;
}

// --------------------------------- Splitters.

// TODO With dockable UI, show split bars at the start and end of the splitter as drop targets.
// 	The root splitter will also need two split bars at the start and end of the other axis.
// 	Split bars should also be enlarged when actings as drop targets.
// 	When dropping on an existing non-splitter panel, you can either form a tab group,
// 	or create a new split on the other axis, on one of the two sides.

struct EsSplitter : EsElement {
	bool horizontal;
	bool addingSplitBar;
	int previousSize;
	Array<int64_t> resizeStartSizes;
	bool calculatedInitialSize;
};

struct SplitBar : EsElement {
	int position, dragStartPosition;

	void Move(int newPosition, bool fromKeyboard) {
		EsSplitter *splitter = (EsSplitter *) parent;
		EsElement *panelBefore = nullptr, *panelAfter = nullptr;
		int barBefore = 0, barAfter;
		if (splitter->horizontal) barAfter = EsRectangleAddBorder(splitter->GetBounds(), splitter->currentStyle->borders).r  - currentStyle->preferredWidth;
		else                      barAfter = EsRectangleAddBorder(splitter->GetBounds(), splitter->currentStyle->borders).b - currentStyle->preferredHeight;
		int preferredSize = splitter->horizontal ? currentStyle->preferredWidth : currentStyle->preferredHeight;
		splitter->resizeStartSizes.Free();

		for (uintptr_t i = 0; i < splitter->GetChildCount(); i++) {
			if (splitter->GetChild(i) == this) {
				EsAssert(i & 1); // Expected split bars between each EsSplitter child.
				panelBefore = splitter->GetChild(i - 1);
				panelAfter = splitter->GetChild(i + 1);

				if (i != 1) {
					barBefore = ((SplitBar *) splitter->GetChild(i - 2))->position + preferredSize;
				}

				if (i != splitter->GetChildCount() - 2) {
					barAfter = ((SplitBar *) splitter->GetChild(i + 2))->position - preferredSize;
				}

				break;
			}
		}

		EsAssert(panelBefore && panelAfter); // Could not find split bar in parent.

		barBefore -= splitter->horizontal ? currentStyle->borders.l  : currentStyle->borders.t;
		barAfter  += splitter->horizontal ? currentStyle->borders.r : currentStyle->borders.b;

		int minimumPosition, maximumPosition, minimumPosition1, maximumPosition1, minimumPosition2, maximumPosition2;

		if (splitter->horizontal) {
			minimumPosition1 = barBefore + panelBefore->currentStyle->metrics->minimumWidth;
			maximumPosition1 = barAfter  - panelAfter ->currentStyle->metrics->minimumWidth;
			minimumPosition2 = barAfter  - panelAfter ->currentStyle->metrics->maximumWidth;
			maximumPosition2 = barBefore + panelBefore->currentStyle->metrics->maximumWidth;
			if (!panelAfter ->currentStyle->metrics->maximumWidth) minimumPosition2 = INT_MIN;
			if (!panelBefore->currentStyle->metrics->maximumWidth) maximumPosition2 = INT_MAX;
		} else {
			minimumPosition1 = barBefore + panelBefore->currentStyle->metrics->minimumHeight;
			maximumPosition1 = barAfter  - panelAfter ->currentStyle->metrics->minimumHeight;
			minimumPosition2 = barAfter  - panelAfter ->currentStyle->metrics->maximumHeight;
			maximumPosition2 = barBefore + panelBefore->currentStyle->metrics->maximumHeight;
			if (!panelAfter ->currentStyle->metrics->maximumHeight) minimumPosition2 = INT_MIN;
			if (!panelBefore->currentStyle->metrics->maximumHeight) maximumPosition2 = INT_MAX;
		}

		minimumPosition = minimumPosition1 > minimumPosition2 ? minimumPosition1 : minimumPosition2;
		maximumPosition = maximumPosition1 < maximumPosition2 ? maximumPosition1 : maximumPosition2;

		if (minimumPosition < maximumPosition) {
			int oldPosition = position;

			if (newPosition < minimumPosition) {
				if (newPosition > minimumPosition2 
						&& (fromKeyboard || newPosition < (barBefore + minimumPosition1) / 2) 
						&& (!fromKeyboard || newPosition < position) 
						&& panelBefore->flags & ES_CELL_COLLAPSABLE) {
					position = barBefore > minimumPosition2 ? barBefore : minimumPosition2;
				} else {
					position = minimumPosition;
				}
			} else if (newPosition > maximumPosition) {
				if (newPosition < maximumPosition2 
						&& (fromKeyboard || newPosition > (barAfter + maximumPosition1) / 2)
						&& (!fromKeyboard || newPosition > position)
						&& panelAfter->flags & ES_CELL_COLLAPSABLE) {
					position = barAfter < maximumPosition2 ? barAfter : maximumPosition2;
				} else {
					position = maximumPosition;
				}
			} else {
				position = newPosition;
			}

			if (oldPosition != position) {
				EsElementRelayout(splitter);
			}
		}
	}
};

int ProcessSplitBarMessage(EsElement *element, EsMessage *message) {
	SplitBar *bar = (SplitBar *) element;
	EsSplitter *splitter = (EsSplitter *) bar->parent;

	if (message->type == ES_MSG_MOUSE_LEFT_DOWN) {
		bar->dragStartPosition = bar->position;

		if (!bar->window->focused || bar->window->focused->messageClass != ProcessSplitBarMessage) {
			// Don't take focus.
			return ES_REJECTED;
		}
	} else if (message->type == ES_MSG_MOUSE_LEFT_DRAG) {
		if (splitter->horizontal) {
			bar->Move(message->mouseDragged.newPositionX - message->mouseDragged.originalPositionX + bar->dragStartPosition, false);
		} else {
			bar->Move(message->mouseDragged.newPositionY - message->mouseDragged.originalPositionY + bar->dragStartPosition, false);
		}
	} else if (message->type == ES_MSG_KEY_TYPED) {
		if (message->keyboard.scancode == (splitter->horizontal ? ES_SCANCODE_LEFT_ARROW : ES_SCANCODE_UP_ARROW)) {
			bar->Move(bar->position - GetConstantNumber("splitBarKeyboardMovementAmount"), true);
		} else if (message->keyboard.scancode == (splitter->horizontal ? ES_SCANCODE_RIGHT_ARROW : ES_SCANCODE_DOWN_ARROW)) {
			bar->Move(bar->position + GetConstantNumber("splitBarKeyboardMovementAmount"), true);
		} else {
			return 0;
		}
	} else if (message->type == ES_MSG_GET_ACCESS_KEY_HINT_BOUNDS) {
		AccessKeysCenterHint(element, message);
	} else {
		return 0;
	}

	return ES_HANDLED;
}

int ProcessSplitterMessage(EsElement *element, EsMessage *message) {
	EsSplitter *splitter = (EsSplitter *) element;

	if (message->type == ES_MSG_LAYOUT && splitter->GetChildCount()) {
		EsRectangle client = splitter->GetBounds();
		EsRectangle bounds = EsRectangleAddBorder(client, splitter->currentStyle->insets);

		size_t childCount = splitter->GetChildCount();
		EsAssert(childCount & 1); // Expected split bars between each EsSplitter child.
		uint64_t pushFlag = splitter->horizontal ? ES_CELL_H_PUSH : ES_CELL_V_PUSH;

		if (!splitter->calculatedInitialSize) {
			for (uintptr_t i = 0; i < childCount; i += 2) {
				EsElement *child = splitter->GetChild(i);

				if (~child->flags & pushFlag) {
					int width = child->GetWidth(bounds.b - bounds.t);
					int height = child->GetHeight(width);
					splitter->resizeStartSizes.Add(splitter->horizontal ? width : height);
				} else {
					splitter->resizeStartSizes.Add(0);
				}
			}
		}

		int64_t newSize = splitter->horizontal ? (bounds.r - bounds.l) : (bounds.b - bounds.t);

		if (newSize != splitter->previousSize && childCount > 1) {
			// Step 1: Make a list of current sizes.

			int64_t barSize = splitter->horizontal ? splitter->GetChild(1)->currentStyle->preferredWidth : splitter->GetChild(1)->currentStyle->preferredHeight;
			int64_t previousPosition = 0;

			if (!splitter->resizeStartSizes.Length()) {
				for (uintptr_t i = 1; i < childCount; i += 2) {
					int64_t position = ((SplitBar *) splitter->GetChild(i))->position;
					splitter->resizeStartSizes.Add(position - previousPosition);
					previousPosition = position + barSize;
				}

				splitter->resizeStartSizes.Add(splitter->previousSize - previousPosition);
			}

			Array<int64_t> currentSizes = {};

			for (uintptr_t i = 0; i < splitter->resizeStartSizes.Length(); i++) {
				currentSizes.Add(splitter->resizeStartSizes[i]);
			}

			// Step 2: Calculate the fixed size, and total weight.

			int64_t fixedSize = 0, totalWeight = 0;

			for (uintptr_t i = 0; i < childCount; i += 2) {
				EsElement *child = splitter->GetChild(i);

				if (~child->flags & pushFlag) {
					fixedSize += currentSizes[i >> 1];
				} else {
					if (currentSizes[i >> 1] < 1) currentSizes[i >> 1] = 1;
					totalWeight += currentSizes[i >> 1];
				}
			}

			EsAssert(totalWeight); // Splitter must have at least one child with a PUSH flag for its orientation.

			// Step 3: Calculate the new weighted sizes.

			int64_t availableSpace = newSize - fixedSize - barSize * (childCount >> 1);

			if (availableSpace >= 0) {
				for (uintptr_t i = 0; i < childCount; i += 2) {
					EsElement *child = splitter->GetChild(i);

					if (child->flags & pushFlag) {
						currentSizes[i >> 1] = availableSpace * currentSizes[i >> 1] / totalWeight;
					}
				}
			} else {
				availableSpace += fixedSize;
				if (availableSpace < 0) availableSpace = 0;

				for (uintptr_t i = 0; i < childCount; i += 2) {
					EsElement *child = splitter->GetChild(i);

					if (child->flags & pushFlag) {
						currentSizes[i >> 1] = 0;
					} else {
						currentSizes[i >> 1] = availableSpace * currentSizes[i >> 1] / fixedSize;
					}
				}
			}

			// Step 4: Update the positions.

			previousPosition = 0;

			for (uintptr_t i = 1; i < childCount; i += 2) {
				SplitBar *bar = (SplitBar *) splitter->GetChild(i);
				bar->position = previousPosition + currentSizes[i >> 1];
				previousPosition = bar->position + barSize - (splitter->horizontal ? bar->currentStyle->borders.l : bar->currentStyle->borders.t);

				if (bar->position == 0) {
					bar->position -= splitter->horizontal ? bar->currentStyle->borders.l  : bar->currentStyle->borders.t;
				} else if (bar->position == newSize - barSize) {
					bar->position += splitter->horizontal ? bar->currentStyle->borders.r : bar->currentStyle->borders.b;
				}
			}

			currentSizes.Free();
		}

		splitter->calculatedInitialSize = true;
		splitter->previousSize = newSize;

		int position = splitter->horizontal ? bounds.l : bounds.t;

		for (uintptr_t i = 0; i < childCount; i++) {
			EsElement *child = splitter->GetChild(i);

			if (i & 1) {
				if (splitter->horizontal) {
					int size = child->currentStyle->preferredWidth;
					EsElementMove(child, position, client.t, size, client.b - client.t);
					position += size;
				} else {
					int size = child->currentStyle->preferredHeight;
					EsElementMove(child, client.l, position, client.r - client.l, size);
					position += size;
				}
			} else if (i == childCount - 1) {
				if (splitter->horizontal) {
					EsElementMove(child, position, bounds.t, bounds.r - position, bounds.b - bounds.t);
				} else {
					EsElementMove(child, bounds.l, position, bounds.r - bounds.l, bounds.b - position);
				}
			} else {
				SplitBar *bar = (SplitBar *) splitter->GetChild(i + 1);
				int size = bar->position - position;

				if (splitter->horizontal) {
					EsElementMove(child, position, bounds.t, size, bounds.b - bounds.t);
				} else {
					EsElementMove(child, bounds.l, position, bounds.r - bounds.l, size);
				}

				position += size;
			}
		}
	} else if ((message->type == ES_MSG_GET_WIDTH && splitter->horizontal)
			|| (message->type == ES_MSG_GET_HEIGHT && !splitter->horizontal)) {
		int size = 0;

		for (uintptr_t i = 0; i < splitter->GetChildCount(); i++) {
			EsElement *child = splitter->GetChild(i);
			size += splitter->horizontal ? child->GetWidth(message->measure.height) : child->GetHeight(message->measure.width);
		}

		if (splitter->horizontal) {
			message->measure.width = size;
		} else {
			message->measure.height = size;
		}
	} else if (message->type == ES_MSG_PRE_ADD_CHILD && !splitter->addingSplitBar && splitter->GetChildCount()) {
		splitter->addingSplitBar = true;
		SplitBar *bar = (SplitBar *) EsHeapAllocate(sizeof(SplitBar), true);

		bar->Initialise(splitter, ES_ELEMENT_FOCUSABLE | ES_ELEMENT_NOT_TAB_TRAVERSABLE | ES_CELL_EXPAND, 
				ProcessSplitBarMessage, splitter->horizontal ? ES_STYLE_SPLIT_BAR_VERTICAL : ES_STYLE_SPLIT_BAR_HORIZONTAL);

		bar->cName = "split bar";
		bar->accessKey = 'Q';

		splitter->addingSplitBar = false;
	} else if (message->type == ES_MSG_REMOVE_CHILD && ((EsElement *) message->child)->messageClass != ProcessSplitBarMessage) {
		for (uintptr_t i = 0; i < splitter->GetChildCount(); i++) {
			if (splitter->GetChild(i) == message->child) {
				// Remove the corresponding split bar.

				if (i) {
					splitter->GetChild(i - 1)->Destroy();
				} else {
					splitter->GetChild(i + 1)->Destroy();
				}

				break;
			}
		}
	} else if (message->type == ES_MSG_DESTROY) {
		splitter->resizeStartSizes.Free();
	}

	return 0;
}

EsSplitter *EsSplitterCreate(EsElement *parent, uint64_t flags, const EsStyle *style) {
	EsSplitter *splitter = (EsSplitter *) EsHeapAllocate(sizeof(EsSplitter), true);
	splitter->horizontal = flags & ES_SPLITTER_HORIZONTAL;
	splitter->Initialise(parent, flags | ES_ELEMENT_NO_CLIP, ProcessSplitterMessage, 
			style ?: ES_STYLE_PANEL_WINDOW_BACKGROUND);
	splitter->cName = "splitter";
	return splitter;
}

// --------------------------------- Image displays.

// TODO
// 	asynchronous/synchronous load/decode from file/memory
// 	unloading image when not visible
// 	aspect ratio; sizing
// 	upscale/downscale quality
// 	subregion, transformations
// 	transparency, IsRegionCompletelyOpaque, proper blending mode with fragmentShader in DrawStyledBox
// 	image sets, DPI; SVG scaling
// 	embedding in TextDisplay
// 	merge with IconDisplay
// 	decode in separate process for security?
// 	clipboard
// 	zoom/pan

struct EsImageDisplay : EsElement {
	void *source;
	size_t sourceBytes;

	uint32_t *bits;
	size_t width, height, stride;
};

int ProcessImageDisplayMessage(EsElement *element, EsMessage *message) {
	EsImageDisplay *display = (EsImageDisplay *) element;

	if (message->type == ES_MSG_PAINT && (display->bits || display->source)) {
		if (!display->bits && display->source) {
			uint32_t width, height;
			uint8_t *bits = EsImageLoad((uint8_t *) display->source, display->sourceBytes, &width, &height, 4);

			if (bits) {
				display->bits = (uint32_t *) bits;
				display->width = width;
				display->height = height;
				display->stride = width * 4;
			}

			if (~display->flags & UI_STATE_CHECK_VISIBLE) {
				display->state |= UI_STATE_CHECK_VISIBLE;
				display->window->checkVisible.Add(display);
			}
		}

		EsPaintTarget source = {};
		source.bits = display->bits;
		source.width = display->width;
		source.height = display->height;
		source.stride = display->stride;
		EsDrawPaintTarget(message->painter, &source, 
				EsPainterBoundsInset(message->painter), 
				ES_RECT_4(0, display->width, 0, display->height), 0xFF);
	} else if (message->type == ES_MSG_GET_WIDTH) {
		message->measure.width = display->width;
	} else if (message->type == ES_MSG_GET_HEIGHT) {
		message->measure.height = display->height;
	} else if (message->type == ES_MSG_DESTROY) {
		EsHeapFree(display->bits);
		EsHeapFree(display->source);
	} else if (message->type == ES_MSG_NOT_VISIBLE) {
		EsHeapFree(display->bits);
		display->bits = nullptr;
	}

	return 0;
}

EsImageDisplay *EsImageDisplayCreate(EsElement *parent, uint64_t flags, const EsStyle *style) {
	EsImageDisplay *display = (EsImageDisplay *) EsHeapAllocate(sizeof(EsImageDisplay), true);
	display->Initialise(parent, flags, ProcessImageDisplayMessage, style);
	display->cName = "image";
	return display;
}

void EsImageDisplayLoadBits(EsImageDisplay *display, const uint32_t *bits, size_t width, size_t height, size_t stride) {
	EsHeapFree(display->bits);
	display->bits = (uint32_t *) EsHeapAllocate(stride * height, false);
	display->width = width;
	display->height = height;
	display->stride = stride;
	EsMemoryCopy(display->bits, bits, stride * height);
}

void EsImageDisplayLoadFromMemory(EsImageDisplay *display, const void *buffer, size_t bufferBytes) {
	if (display->flags & ES_IMAGE_DISPLAY_DECODE_WHEN_NEEDED) {
		EsHeapFree(display->source);
		EsHeapFree(display->bits);
		display->bits = nullptr;
		display->source = EsHeapAllocate(bufferBytes, false);
		if (!display->source) return;
		EsMemoryCopy(display->source, buffer, bufferBytes);
		display->sourceBytes = bufferBytes;

		if (~display->flags & ES_IMAGE_DISPLAY_MANUAL_SIZE) {
			// TODO Make a version of EsImageLoad that doesn't load the full image, but only gets the size.
			uint32_t width, height;
			uint8_t *bits = EsImageLoad((uint8_t *) buffer, bufferBytes, &width, &height, 4);
			EsHeapFree(bits);
			display->width = width;
			display->height = height;
		}
	} else {
		uint32_t width, height;
		uint8_t *bits = EsImageLoad((uint8_t *) buffer, bufferBytes, &width, &height, 4);
		if (!bits) return;
		EsHeapFree(display->bits);
		display->bits = (uint32_t *) bits;
		display->width = width;
		display->height = height;
		display->stride = width * 4;
	}
}

struct EsIconDisplay : EsElement {
	uint32_t iconID;
};

int ProcessIconDisplayMessage(EsElement *element, EsMessage *message) {
	EsIconDisplay *display = (EsIconDisplay *) element;

	if (message->type == ES_MSG_PAINT) {
		EsDrawContent(message->painter, element, ES_RECT_2S(message->painter->width, message->painter->height), "", 0, display->iconID);
	}

	return 0;
}

EsIconDisplay *EsIconDisplayCreate(EsElement *parent, uint64_t flags, const EsStyle *style, uint32_t iconID) {
	EsIconDisplay *display = (EsIconDisplay *) EsHeapAllocate(sizeof(EsIconDisplay), true);
	display->Initialise(parent, flags, ProcessIconDisplayMessage, style ?: ES_STYLE_ICON_DISPLAY);
	display->cName = "icon";
	display->iconID = iconID;
	return display;
}

// --------------------------------- Sliders.

struct EsSlider : EsElement {
	EsElement *point;
	double value;
	uint32_t steps;
	int32_t dragOffset;
	bool inDrag, endDrag;
};

int ProcessSliderPointMessage(EsElement *element, EsMessage *message) {
	EsSlider *slider = (EsSlider *) EsElementGetLayoutParent(element);

	if (message->type == ES_MSG_MOUSE_LEFT_DRAG) {
		double range = slider->width - slider->point->currentStyle->preferredWidth;
		slider->inDrag = true;
		EsSliderSetValue(slider, (message->mouseDragged.newPositionX + element->offsetX - slider->dragOffset) / range);
	} else if (message->type == ES_MSG_MOUSE_LEFT_UP && slider->inDrag) {
		slider->inDrag = false;
		slider->endDrag = true; // Force sending the update message.
		EsSliderSetValue(slider, slider->value);
		slider->endDrag = false;
	} else if (message->type == ES_MSG_MOUSE_LEFT_DOWN) {
		slider->dragOffset = message->mouseDown.positionX;
		EsElementFocus(slider);
	} else {
		return 0;
	}

	return ES_HANDLED;
}

int ProcessSliderMessage(EsElement *element, EsMessage *message) {
	EsSlider *slider = (EsSlider *) element;

	if (message->type == ES_MSG_LAYOUT) {
		int pointWidth = slider->point->currentStyle->preferredWidth;
		int pointHeight = slider->point->currentStyle->preferredHeight;
		slider->point->InternalMove(pointWidth, pointHeight, (slider->width - pointWidth) * slider->value, (slider->height - pointHeight) / 2);
	} else if (message->type == ES_MSG_FOCUSED_START) {
		slider->point->customStyleState |= THEME_STATE_FOCUSED_ITEM;
		slider->point->MaybeRefreshStyle();
	} else if (message->type == ES_MSG_FOCUSED_END) {
		slider->point->customStyleState &= ~THEME_STATE_FOCUSED_ITEM;
		slider->point->MaybeRefreshStyle();
	} else if (message->type == ES_MSG_KEY_TYPED && message->keyboard.scancode == ES_SCANCODE_LEFT_ARROW) {
		EsSliderSetValue(slider, slider->value - (slider->steps ? 1.0 / slider->steps : 0.02));
	} else if (message->type == ES_MSG_KEY_TYPED && message->keyboard.scancode == ES_SCANCODE_RIGHT_ARROW) {
		EsSliderSetValue(slider, slider->value + (slider->steps ? 1.0 / slider->steps : 0.02));
	} else if (message->type == ES_MSG_KEY_TYPED && message->keyboard.scancode == ES_SCANCODE_HOME) {
		EsSliderSetValue(slider, 0.0);
	} else if (message->type == ES_MSG_KEY_TYPED && message->keyboard.scancode == ES_SCANCODE_END) {
		EsSliderSetValue(slider, 1.0);
	} else if (message->type == ES_MSG_PAINT) {
		// TODO Draw ticks.
	} else {
		return 0;
	}

	return ES_HANDLED;
}

double EsSliderGetValue(EsSlider *slider) {
	return slider->value;
}

void EsSliderSetValue(EsSlider *slider, double newValue, bool sendUpdatedMessage) {
	newValue = ClampDouble(0.0, 1.0, newValue);

	if (slider->steps) {
		newValue = EsCRTfloor((slider->steps - 1) * newValue + 0.5) / (slider->steps - 1);
	}

	double previous = slider->value;
	if (previous == newValue && !slider->endDrag) return;
	slider->value = newValue;
	EsElementRelayout(slider);

	if (sendUpdatedMessage) {
		EsMessage m = { ES_MSG_SLIDER_MOVED, .sliderMoved = { .value = newValue, .previous = previous, .inDrag = slider->inDrag } };
		EsMessageSend(slider, &m);
	}
}

EsSlider *EsSliderCreate(EsElement *parent, uint64_t flags, const EsStyle *style, double value, uint32_t steps) {
	EsSlider *slider = (EsSlider *) EsHeapAllocate(sizeof(EsSlider), true);
	slider->Initialise(parent, flags | ES_ELEMENT_FOCUSABLE, ProcessSliderMessage, style ?: ES_STYLE_SLIDER_TRACK);
	slider->cName = "slider";
	slider->point = EsCustomElementCreate(slider, ES_FLAGS_DEFAULT, ES_STYLE_SLIDER_POINT);
	slider->point->messageUser = ProcessSliderPointMessage;
	slider->steps = steps;
	EsSliderSetValue(slider, value, false);
	return slider;
}

// --------------------------------- Message loop and core UI infrastructure.

void EsElement::PrintTree(int depth) {
	const char *tabs = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t";
	EsPrint("%s%z c:%d, %z\n", depth, tabs, cName, children.Length(), state & UI_STATE_DESTROYING ? "DESTROYING" : "");

	for (uintptr_t i = 0; i < children.Length(); i++) {
		children[i]->PrintTree(depth + 1);
	}
}

void EsElement::Destroy(bool manual) {
	if (state & UI_STATE_DESTROYING) {
		return;
	}

	if (manual) {
#ifndef DISABLE_ALL_ANIMATIONS
		if (currentStyle->metrics->exitDuration) {
			if (previousTransitionFrame) {
				EsPaintTargetDestroy(previousTransitionFrame);
			}

			previousTransitionFrame = EsPaintTargetCreate(width, height, true);

			if (previousTransitionFrame) {
				// TODO Doesn't support shadows.
				EsPainter painter = {};
				painter.clip = ES_RECT_4(0, width, 0, height);
				painter.target = previousTransitionFrame;
				InternalPaint(&painter, PAINT_NO_TRANSITION | PAINT_NO_OFFSET);
				state |= UI_STATE_EXITING;
				RefreshStyle();
			}
		}
#endif

		EsElement *ancestor = parent;

		while (ancestor && (~ancestor->state & UI_STATE_DESTROYING_CHILD)) {
			ancestor->state |= UI_STATE_DESTROYING_CHILD;
			ancestor = ancestor->parent;
		}
	}

	state |= UI_STATE_DESTROYING | UI_STATE_DESTROYING_CHILD | UI_STATE_BLOCK_INTERACTION;

	if (parent) {
		EsMessage m = { ES_MSG_REMOVE_CHILD };
		m.child = this;
		EsMessageSend(parent, &m);
	}

	if (window->hovered == this) {
		window->hovered = window;
		window->state |= UI_STATE_HOVERED;
		EsMessage m = {};
		m.type = ES_MSG_HOVERED_START;
		EsMessageSend(window, &m);
	}

	EsMessage m = { ES_MSG_DESTROY };
	if (messageUser) messageUser(this, &m);
	messageUser = nullptr;
	if (messageClass) messageClass(this, &m);
	messageClass = nullptr;

	if (window->inactiveFocus	== this) window->inactiveFocus = nullptr;
	if (window->pressed 		== this) window->pressed = nullptr;
	if (window->focused 		== this) window->focused = nullptr;
	if (window->defaultEnterButton 	== this) window->defaultEnterButton = nullptr;		
	if (window->enterButton 	== this) window->enterButton = window->defaultEnterButton;
	if (window->escapeButton 	== this) window->escapeButton = nullptr;
	if (window->ensureVisible 	== this) window->ensureVisible = nullptr;
	if (window->dragged 		== this) window->dragged = nullptr;
	if (gui.clickChainElement       == this) gui.clickChainElement = nullptr;

	if (parent) EsElementUpdateContentSize(parent);

	UIWindowNeedsUpdate(window);
}

bool EsElement::InternalDestroy() {
	if (~state & UI_STATE_DESTROYING_CHILD) {
		return false;
	}

	for (uintptr_t i = 0; i < children.Length(); i++) {
		if (state & UI_STATE_DESTROYING) {
			children[i]->Destroy(false);
		}

		if (children[i]->InternalDestroy() && (~state & UI_STATE_DESTROYING)) {
			children.Delete(i);
			i--;
		}
	}

	state &= ~UI_STATE_DESTROYING_CHILD;

	if (~state & UI_STATE_DESTROYING) {
		return false;
	}

	children.Free();

	if (state & UI_STATE_EXITING) {
		return false;
	}

	InspectorNotifyElementDestroyed(this);

	if (state & UI_STATE_ANIMATING) {
		for (uintptr_t i = 0; i < gui.animatingElements.Length(); i++) {
			if (gui.animatingElements[i] == this) {
				gui.animatingElements.DeleteSwap(i);
				break;
			}
		}

		state &= ~UI_STATE_ANIMATING;
	}

	if (state & UI_STATE_CHECK_VISIBLE) {
		window->checkVisible.FindAndDeleteSwap(this, true);
	}

	if (flags & ES_ELEMENT_FREE_USER_DATA) {
		EsHeapFree(userData.p);
	}

	if (currentStyle) currentStyle->CloseReference();
	if (previousTransitionFrame) EsPaintTargetDestroy(previousTransitionFrame);
	ThemeAnimationDestroy(&animation);
	if (window == this) UIWindowDestroy(window); // Windows are deallocated after receiving ES_MSG_WINDOW_DESTROYED.
	else EsHeapFree(this);

	return true;
}

EsElement *EsWindowGetToolbar(EsWindow *window, bool createNew) {
	if (createNew || !window->toolbar) {
		bool first = !window->toolbar;
		window->toolbar = EsPanelCreate(window->toolbarSwitcher, ES_PANEL_HORIZONTAL | ES_CELL_FILL, ES_STYLE_PANEL_TOOLBAR);
		window->toolbar->cName = "toolbar";
		EsAssert(window->toolbar->messageClass == ProcessPanelMessage);

		window->toolbar->messageClass = [] (EsElement *element, EsMessage *message) {
			if (message->type == ES_MSG_GET_CHILD_STYLE_VARIANT) {
				if (message->childStyleVariant == ES_STYLE_TEXT_LABEL) {
					message->childStyleVariant = ES_STYLE_TEXT_TOOLBAR;
					return ES_HANDLED;
				} else if (message->childStyleVariant == ES_STYLE_PUSH_BUTTON_NORMAL
						|| message->childStyleVariant == ES_STYLE_CHECKBOX_NORMAL
						|| message->childStyleVariant == ES_STYLE_CHECKBOX_RADIOBOX) {
					message->childStyleVariant = ES_STYLE_PUSH_BUTTON_TOOLBAR;
					return ES_HANDLED;
				}
			}

			return ProcessPanelMessage(element, message);
		};

		if (first) EsPanelSwitchTo(window->toolbarSwitcher, window->toolbar, ES_TRANSITION_NONE);
	}

	return window->toolbar;
}

void EsWindowSwitchToolbar(EsWindow *window, EsElement *toolbar, EsTransitionType transitionType) {
	EsPanelSwitchTo(window->toolbarSwitcher, toolbar, transitionType);
}

EsRectangle EsWindowGetBounds(EsWindow *window) {
	EsRectangle bounds;
	EsSyscall(ES_SYSCALL_WINDOW_GET_BOUNDS, window->handle, (uintptr_t) &bounds, 0, 0);
	return bounds;
}

EsRectangle EsElementGetInsetSize(EsElement *element) {
	EsMessageMutexCheck();

	EsRectangle insets = element->currentStyle->insets;
	return ES_RECT_4(0, element->width - insets.l - insets.r, 
			0, element->height - insets.t - insets.b);
}

void EsWindowSetIcon(EsWindow *window, uint32_t iconID) {
	EsMessageMutexCheck();

	char buffer[5];
	buffer[0] = DESKTOP_MSG_SET_ICON;
	EsMemoryCopy(buffer + 1, &iconID, sizeof(uint32_t));
	MessageDesktop(buffer, sizeof(buffer), window->handle);
}

void EsWindowSetTitle(EsWindow *window, const char *title, ptrdiff_t titleBytes) {
	EsMessageMutexCheck();

	APIInstance *instance = window->instance ? ((APIInstance *) window->instance->_private) : nullptr;
	const char *applicationName = instance ? instance->applicationName : nullptr;
	size_t applicationNameBytes = instance ? instance->applicationNameBytes : 0;

	if (!applicationNameBytes && !titleBytes) {
		return;
	}

	if (titleBytes == -1) {
		titleBytes = EsCStringLength(title);
	}

	if (titleBytes) {
		applicationNameBytes = 0;
	}

	char buffer[4096];
	size_t bytes = EsStringFormat(buffer, 4096, "%c%s%s", DESKTOP_MSG_SET_TITLE, titleBytes, title, applicationNameBytes, applicationName);
	MessageDesktop(buffer, bytes, window->handle);
}

void EsMouseSetPosition(EsWindow *relativeWindow, int x, int y) {
	if (relativeWindow) {
		EsRectangle bounds = EsWindowGetBounds(relativeWindow);
		x += bounds.l;
		y += bounds.t;
	}

	EsSyscall(ES_SYSCALL_CURSOR_POSITION_SET, x, y, 0, 0);
}

EsPoint EsMouseGetPosition(EsElement *relativeElement) {
	if (relativeElement) {
		EsPoint position = relativeElement->window->mousePosition;
		EsRectangle bounds = relativeElement->GetWindowBounds();
		position.x -= bounds.l;
		position.y -= bounds.t;
		return position;
	} else {
		EsPoint position;
		EsSyscall(ES_SYSCALL_CURSOR_POSITION_GET, (uintptr_t) &position, 0, 0, 0);
		return position;
	}
}

const EsStyle *UIGetDefaultStyleVariant(const EsStyle *style, EsElement *parent) {
	EsMessage m = { .type = ES_MSG_GET_CHILD_STYLE_VARIANT, .childStyleVariant = style };
	EsElement *ancestor = parent;

	while (ancestor) {
		if (ES_HANDLED == EsMessageSend(ancestor, &m)) {
			break; 
		}

		ancestor = ancestor->parent;
	}

	return m.childStyleVariant;
}

EsElement *UIFindHoverElementRecursively2(EsElement *element, int offsetX, int offsetY, EsPoint position, uintptr_t i) {
	EsElement *child = element->GetChildByZ(i - 1);

	if (!child) return nullptr;
	if (child->flags & ES_ELEMENT_HIDDEN) return nullptr;
	if (child->state & UI_STATE_DESTROYING) return nullptr;
	if (child->state & UI_STATE_BLOCK_INTERACTION) return nullptr;

	if (!EsRectangleContains(ES_RECT_4(offsetX + child->offsetX, offsetX + child->offsetX + child->width, 
					offsetY + child->offsetY, offsetY + child->offsetY + child->height), 
				position.x, position.y)) {
		return nullptr;
	}

	EsMessage m = { ES_MSG_HIT_TEST };
	m.hitTest.x = position.x - offsetX - child->offsetX - child->internalOffsetLeft;
	m.hitTest.y = position.y - offsetY - child->offsetY - child->internalOffsetTop;
	m.hitTest.inside = true;
	int response = EsMessageSend(child, &m);

	if ((response != ES_HANDLED || m.hitTest.inside) && response != ES_REJECTED) {
		return UIFindHoverElementRecursively(child, offsetX, offsetY, position);
	}

	return nullptr;
}

EsElement *UIFindHoverElementRecursively(EsElement *element, int offsetX, int offsetY, EsPoint position) {
	offsetX += element->offsetX;
	offsetY += element->offsetY;

	EsMessage zOrder = { ES_MSG_BEFORE_Z_ORDER };
	zOrder.beforeZOrder.nonClient = zOrder.beforeZOrder.end = element->children.Length();
	zOrder.beforeZOrder.clip = Translate(ES_RECT_4(0, element->width, 0, element->height), offsetX, offsetY);
	EsMessageSend(element, &zOrder);

	EsElement *result = nullptr;

	if (~element->flags & ES_ELEMENT_NO_HOVER_DESCENDENTS) {
		for (uintptr_t i = element->children.Length(); !result && i > zOrder.beforeZOrder.nonClient; i--) {
			result = UIFindHoverElementRecursively2(element, offsetX, offsetY, position, i);
		}

		for (uintptr_t i = zOrder.beforeZOrder.end; !result && i > zOrder.beforeZOrder.start; i--) {
			result = UIFindHoverElementRecursively2(element, offsetX, offsetY, position, i);
		}
	}

	zOrder.type = ES_MSG_AFTER_Z_ORDER;
	EsMessageSend(element, &zOrder);

	return result ? result : (element->flags & ES_ELEMENT_NO_HOVER) ? nullptr : element;
}

void UIFindHoverElement(EsWindow *window) {
	// TS("Finding element under cursor\n");

	EsPoint position = EsMouseGetPosition(window);

	EsElement *element;

	if (position.x < 0 || position.y < 0 || position.x >= window->width || position.y >= window->height || !window->hovering) {
		element = window;
	} else {
		element = UIFindHoverElementRecursively(window, 0, 0, position);
	}

	if (element->state & UI_STATE_HOVERED) {
		EsAssert(window->hovered == element); // Window's hovered element mismatched element state flags.
	} else {
		EsMessage m = {};
		m.type = ES_MSG_HOVERED_END;
		window->hovered->state &= ~UI_STATE_HOVERED;
		EsMessageSend(window->hovered, &m);
		element->state |= UI_STATE_HOVERED;
		m.type = ES_MSG_HOVERED_START;
		EsMessageSend((window->hovered = element), &m);
	}
}

void UIRemoveFocusFromElement(EsElement *oldFocus) {
	if (!oldFocus) return;
	EsMessage m = {};

	if (~oldFocus->state & UI_STATE_LOST_STRONG_FOCUS) {
		m.type = ES_MSG_STRONG_FOCUS_END;
		oldFocus->state |= UI_STATE_LOST_STRONG_FOCUS;
		EsMessageSend(oldFocus, &m);
	}

	m.type = ES_MSG_FOCUSED_END;
	oldFocus->state &= ~(UI_STATE_FOCUSED | UI_STATE_LOST_STRONG_FOCUS);
	EsMessageSend(oldFocus, &m);
}

void UIMaybeRemoveFocusedElement(EsWindow *window) {
	if (window->focused && !window->focused->IsFocusable()) {
		EsElement *oldFocus = window->focused;
		window->focused = nullptr;
		UIRemoveFocusFromElement(oldFocus);
	}
}

bool EsElementIsFocused(EsElement *element) {
	return element->window->focused == element;
}

void EsElementFocus(EsElement *element, uint32_t flags) {
	EsMessageMutexCheck();

	EsWindow *window = element->window;
	EsMessage m;

	// If this element is not focusable or if the window doesn't allow focused elements, ignore the request.

	if (!element->IsFocusable() || (window->windowStyle == ES_WINDOW_CONTAINER)) return;

	// If the element is already focused, then don't resend it any messages.
	
	if (window->focused == element) return;

	// Tell the previously focused element it's no longer focused.

	EsElement *oldFocus = window->focused;
	window->focused = element;
	UIRemoveFocusFromElement(oldFocus);

	// Tell any parents of the previously focused element that aren't parents of the newly focused element that they no longer has focus-within,
	// and the parents of the newly focused element that they have focus-within.
	
	EsElement *parent = element->parent;

	while (parent) {
		parent->state |= UI_STATE_TEMP;
		parent = parent->parent;
	}

	if (oldFocus) {
		parent = oldFocus->parent;

		while (parent) {
			if (~parent->state & UI_STATE_TEMP) {
				parent->state &= ~UI_STATE_FOCUS_WITHIN;
				m.type = ES_MSG_FOCUS_WITHIN_END;
				EsMessageSend(parent, &m);
			}

			parent = parent->parent;
		}
	}

	parent = element->parent;
	window->focused = element;

	while (parent) {
		if (~parent->state & UI_STATE_FOCUS_WITHIN) {
			parent->state |= UI_STATE_FOCUS_WITHIN;
			m.type = ES_MSG_FOCUS_WITHIN_START;
			EsMessageSend(parent, &m);

			EsAssert(window->focused == element); // Cannot change window focus from FOCUS_WITHIN_START message.
		}

		parent->state &= ~UI_STATE_TEMP;
		parent = parent->parent;
	}

	// Tell the newly focused element it's focused.

	m.type = ES_MSG_FOCUSED_START;
	m.focus.flags = flags;
	window->focused->state |= UI_STATE_FOCUSED;
	EsMessageSend(element, &m);
	EsAssert(window->focused == element); // Cannot change window focus from FOCUSED_START message.

	// Ensure the element is visible.

	if ((flags & ES_ELEMENT_FOCUS_ENSURE_VISIBLE) && element) {
		window->ensureVisible = element;
	}
}

EsRectangle EsElementGetWindowBounds(EsElement *element, bool client) {
	EsElement *e = element;
	int x = 0, y = 0;

	while (element) {
		x += element->offsetX, y += element->offsetY;
		element = element->parent;
	}

	int cw = e->width  - (client ? (e->internalOffsetLeft + e->internalOffsetRight) : 0);
	int ch = e->height - (client ? (e->internalOffsetTop + e->internalOffsetBottom) : 0);

	return ES_RECT_4(x, x + cw, y, y + ch);
}

EsRectangle EsElementGetScreenBounds(EsElement *element, bool client) {
	EsRectangle elementBoundsInWindow = EsElementGetWindowBounds(element, client);
	EsRectangle windowBoundsInScreen = EsWindowGetBounds(element->window);
	return Translate(elementBoundsInWindow, windowBoundsInScreen.l, windowBoundsInScreen.t);
}

void EsElementInsertAfter(EsElement *element) {
	EsAssert(!gui.insertAfter);
	gui.insertAfter = element;
}

void EsElement::Initialise(EsElement *_parent, uint64_t _flags, EsUICallback _classCallback, const EsStyle *style) {
	EsMessageMutexCheck();

	// EsPrint("New element '%z' %x with parent %x.\n", _debugName, this, _parent);

	messageClass = _classCallback;
	flags = _flags;

	if (gui.insertAfter) {
		if (_parent) {
			EsAssert(_parent == gui.insertAfter || _parent == gui.insertAfter->parent);
		} else {
			_parent = gui.insertAfter->parent;
		}
	}

	if (_parent) {
		if (!_parent->parent && (~_flags & ES_ELEMENT_NON_CLIENT)) {
			_parent = WindowGetMainPanel((EsWindow *) _parent);
		}

		parent = _parent;
		window = parent->window;
		instance = window->instance;

		if (parent->flags & ES_ELEMENT_DISABLED) flags |= ES_ELEMENT_DISABLED;

		if (~flags & ES_ELEMENT_NON_CLIENT) {
			EsMessage m = {};
			m.type = ES_MSG_PRE_ADD_CHILD;
			m.child = this;
			EsMessageSend(parent, &m);
		}

		if (gui.insertAfter == _parent) {
			parent->children.Insert(this, 0);
			gui.insertAfter = nullptr;
		} else if (gui.insertAfter) {
			uintptr_t i = parent->children.Find(gui.insertAfter, true);
			parent->children.Insert(this, i + 1);
			gui.insertAfter = nullptr;
		} else if (flags & ES_ELEMENT_NON_CLIENT) {
			parent->children.Add(this);
		} else {
			for (uintptr_t i = parent->children.Length();; i--) {
				if (i == 0 || (~parent->children[i - 1]->flags & ES_ELEMENT_NON_CLIENT)) {
					parent->children.Insert(this, i);
					break;
				}
			}
		}

		if (~flags & ES_ELEMENT_NON_CLIENT) {
			EsMessage m = {};
			m.type = ES_MSG_ADD_CHILD;
			m.child = this;
			EsMessageSend(parent, &m);
		}

		EsElementUpdateContentSize(parent);
	}

	SetStyle(style, false);
	RefreshStyle();
	InspectorNotifyElementCreated(this);
}

EsRectangle EsElementGetInsets(EsElement *element) {
	EsMessageMutexCheck();
	return element->currentStyle->insets;
}

EsThemeMetrics EsElementGetMetrics(EsElement *element) {
	EsMessageMutexCheck();
	EsThemeMetrics m = {};
	ThemeMetrics *metrics = element->currentStyle->metrics;
#define RECTANGLE_8_TO_ES_RECTANGLE(x) { (int32_t) (x).l, (int32_t) (x).r, (int32_t) (x).t, (int32_t) (x).b }
	m.insets = RECTANGLE_8_TO_ES_RECTANGLE(metrics->insets);
	m.clipInsets = RECTANGLE_8_TO_ES_RECTANGLE(metrics->clipInsets);
	m.globalOffset = RECTANGLE_8_TO_ES_RECTANGLE(metrics->globalOffset);
	m.clipEnabled = metrics->clipEnabled;
	m.cursor = metrics->cursor;
	m.entranceTransition = metrics->entranceTransition;
	m.exitTransition = metrics->exitTransition;
	m.entranceDuration = metrics->entranceDuration;
	m.exitDuration = metrics->exitDuration;
	m.preferredWidth = metrics->preferredWidth;
	m.preferredHeight = metrics->preferredHeight;
	m.minimumWidth = metrics->minimumWidth;
	m.minimumHeight = metrics->minimumHeight;
	m.maximumWidth = metrics->maximumWidth;
	m.maximumHeight = metrics->maximumHeight;
	m.gapMajor = metrics->gapMajor;
	m.gapMinor = metrics->gapMinor;
	m.gapWrap = metrics->gapWrap;
	m.textColor = metrics->textColor;
	m.selectedBackground = metrics->selectedBackground;
	m.selectedText = metrics->selectedText;
	m.iconColor = metrics->iconColor;
	m.textAlign = metrics->textAlign;
	m.textSize = metrics->textSize;
	m.fontFamily = metrics->fontFamily;
	m.fontWeight = metrics->fontWeight;
	m.iconSize = metrics->iconSize;
	m.isItalic = metrics->isItalic;
	m.ellipsis = metrics->ellipsis;
	return m;
}

void EsElementSetCallback(EsElement *element, EsUICallback callback) {
	EsMessageMutexCheck();
	element->messageUser = callback;
}

void EsElementSetHidden(EsElement *element, bool hidden) {
	EsMessageMutexCheck();

	bool old = element->flags & ES_ELEMENT_HIDDEN;
	if (old == hidden) return;

	if (hidden) {
		element->flags |= ES_ELEMENT_HIDDEN;
	} else {
		element->flags &= ~ES_ELEMENT_HIDDEN;
	}

	EsElementUpdateContentSize(element->parent);
	UIMaybeRemoveFocusedElement(element->window);
}

bool EsElementIsHidden(EsElement *element) {
	EsMessageMutexCheck();
	return element->flags & ES_ELEMENT_HIDDEN;
}

void EsElementSetDisabled(EsElement *element, bool disabled) {
	EsMessageMutexCheck();

	for (uintptr_t i = 0; i < element->GetChildCount(); i++) {
		if (element->GetChild(i)->flags & ES_ELEMENT_NON_CLIENT) continue;
		EsElementSetDisabled(element->GetChild(i), disabled);
	}

	if ((element->flags & ES_ELEMENT_DISABLED) && disabled) return;
	if ((~element->flags & ES_ELEMENT_DISABLED) && !disabled) return;

	if (disabled) element->flags |= ES_ELEMENT_DISABLED;
	else element->flags &= ~ES_ELEMENT_DISABLED;

	element->MaybeRefreshStyle();

	if (element->window->focused == element) {
		element->window->focused = nullptr;
	}
}

void EsElementDestroy(EsElement *element) {
	EsMessageMutexCheck();
	element->Destroy();
}

void EsElementDestroyContents(EsElement *element) {
	EsMessageMutexCheck();

	for (uintptr_t i = 0; i < element->GetChildCount(); i++) {
		if (element->GetChild(i)->flags & ES_ELEMENT_NON_CLIENT) continue;
		element->GetChild(i)->Destroy();
	}

	EsMessage m = {};
	m.type = ES_MSG_DESTROY_CONTENTS;
	EsMessageSend(element, &m);
}

EsElement *UITabTraversalGetChild(EsElement *element, uintptr_t index) {
	if (element->flags & ES_ELEMENT_LAYOUT_HINT_REVERSE) {
		return element->children[element->children.Length() - index - 1];
	} else {
		return element->children[index];
	}
}

EsElement *UITabTraversalDo(EsElement *element, bool shift) {
	if (shift) {
		if (element->parent && UITabTraversalGetChild(element->parent, 0) != element) {
			for (uintptr_t i = 1; i < element->parent->children.Length(); i++) {
				if (UITabTraversalGetChild(element->parent, i) == element) {
					element = UITabTraversalGetChild(element->parent, i - 1);

					while (element->children.Length()) {
						element = UITabTraversalGetChild(element, element->children.Length() - 1);
					}

					return element;
				}
			}
		} else if (element->parent) {
			return element->parent;
		} else {
			while (element->children.Length()) {
				element = UITabTraversalGetChild(element, element->children.Length() - 1);
			}

			return element;
		}
	} else {
		if (element->children.Length()) {
			return UITabTraversalGetChild(element, 0);
		} else while (element->parent) {
			EsElement *child = element;
			element = element->parent;

			for (uintptr_t i = 0; i < element->children.Length() - 1u; i++) {
				if (UITabTraversalGetChild(element, i) == child) {
					element = UITabTraversalGetChild(element, i + 1);
					return element;
				}
			}
		}
	}

	return element;
}

uint8_t EsKeyboardGetModifiers() { 
	return gui.leftModifiers | gui.rightModifiers; 
}

bool EsMouseIsLeftHeld()     { return gui.mouseButtonDown && gui.lastClickButton == ES_MSG_MOUSE_LEFT_DOWN; }
bool EsMouseIsRightHeld()    { return gui.mouseButtonDown && gui.lastClickButton == ES_MSG_MOUSE_RIGHT_DOWN; }
bool EsMouseIsMiddleHeld()   { return gui.mouseButtonDown && gui.lastClickButton == ES_MSG_MOUSE_MIDDLE_DOWN; }

void EsStyleRefreshAll(EsElement *element) {
	EsMessageMutexCheck();

	element->RefreshStyle(nullptr, false, true);

	for (uintptr_t i = 0; i < element->children.Length(); i++) {
		if (element->children[i]) {
			EsStyleRefreshAll(element->children[i]);
		}
	}
}

void EsUISetDPI(int dpiScale) {
	EsMessageMutexCheck();

	if (dpiScale < 50) {
		dpiScale = 50;
	}

	theming.scale = dpiScale / 100.0f;

	for (uintptr_t i = 0; i < gui.allWindows.Length(); i++) {
		EsStyleRefreshAll(gui.allWindows[i]);
	}
}

void EsElementGetSize(EsElement *element, int *width, int *height) {
	EsMessageMutexCheck();

	EsRectangle bounds = element->GetBounds();
	*width = bounds.r;
	*height = bounds.b;
}

void EsElementGetTextStyle(EsElement *element, EsTextStyle *style) {
	element->currentStyle->GetTextStyle(style);
}

void EsElementRepaint(EsElement *element, const EsRectangle *region) {
	EsMessageMutexCheck();
	if (region) element->Repaint(false, *region);
	else element->Repaint(true /* repaint all */);
}

void EsElementRepaintForScroll(EsElement *element, EsMessage *message) {
	// TODO Support custom borders sizes.

	EsRectangle borders = ES_RECT_4(element->internalOffsetLeft, element->internalOffsetRight, 
			element->internalOffsetTop, element->internalOffsetBottom);
	EsRectangle content = ES_RECT_4(borders.l, element->width - borders.r, borders.t, element->height - borders.b);
	EsRectangle repaint = content;
	int64_t delta = message->scrollbarMoved.scroll - message->scrollbarMoved.previous;

	if (message->type == ES_MSG_SCROLL_Y) {
		if (delta > 0) repaint.t = element->height - delta - borders.b;
		else repaint.b = borders.t - delta;
	} else if (message->type == ES_MSG_SCROLL_X) {
		if (delta > 0) repaint.l = element->width - delta - borders.r;
		else repaint.r = borders.l - delta;
	} else {
		EsAssert(false);
	}

	EsRectangle rectangle = element->GetWindowBounds(false);
	EsRectangle scrollBits = Translate(content, rectangle.l, rectangle.t);
	EsSyscall(ES_SYSCALL_WINDOW_SET_BITS, element->window->handle, (uintptr_t) &scrollBits, delta, 
			message->type == ES_MSG_SCROLL_Y ? WINDOW_SET_BITS_SCROLL_VERTICAL : WINDOW_SET_BITS_SCROLL_HORIZONTAL);

	repaint = EsRectangleIntersection(repaint, content);
	EsElementRepaint(element, &repaint);
	UIWindowPaintNow(element->window, nullptr, false);
}

bool EsElementStartAnimating(EsElement *element) {
	EsMessageMutexCheck();

	return element->StartAnimating();
}

EsElement *EsElementGetLayoutParent(EsElement *element) {
	EsMessageMutexCheck();

	return element->parent;
}

void EsElementDraw(EsElement *element, EsPainter *painter) {
	element->InternalPaint(painter, PAINT_SHADOW);
	element->InternalPaint(painter, ES_FLAGS_DEFAULT);
	element->InternalPaint(painter, PAINT_OVERLAY);
}

int UIMessageSendPropagateToAncestors(EsElement *element, EsMessage *message, EsElement **handler = nullptr) {
	while (element) {
		int response = EsMessageSend(element, message);

		if (response) {
			if (handler) *handler = element;
			return response;
		}

		element = element->parent;
	}

	if (handler) *handler = nullptr;
	return 0;
}

void UIMousePressReleased(EsWindow *window, EsMessage *message, bool sendClick) {
	if (window->pressed) {
		EsElement *pressed = window->pressed;
		window->pressed = nullptr;

		if (message) {
			EsRectangle bounds = pressed->GetWindowBounds();
			message->mouseDown.positionX -= bounds.l;
			message->mouseDown.positionY -= bounds.t;
			UIMessageSendPropagateToAncestors(pressed, message);
		} else {
			EsMessage m = {};
			m.type = (EsMessageType) (gui.lastClickButton + 1);
			UIMessageSendPropagateToAncestors(pressed, &m);
		}

		pressed->state &= ~UI_STATE_LEFT_PRESSED;
		EsMessage m = { ES_MSG_PRESSED_END };
		EsMessageSend(pressed, &m);

		if (message && (pressed->state & UI_STATE_HOVERED) && !gui.draggingStarted && sendClick) {
			if (message->type == ES_MSG_MOUSE_LEFT_UP) {
				m.type = ES_MSG_MOUSE_LEFT_CLICK;
				EsMessageSend(pressed, &m);
			} else if (message->type == ES_MSG_MOUSE_RIGHT_UP) {
				m.type = ES_MSG_MOUSE_RIGHT_CLICK;
				EsMessageSend(pressed, &m);
			} else if (message->type == ES_MSG_MOUSE_MIDDLE_UP) {
				m.type = ES_MSG_MOUSE_MIDDLE_CLICK;
				EsMessageSend(pressed, &m);
			}
		}

		if (window->hovered) window->hovered->MaybeRefreshStyle();
	}

	gui.draggingStarted = false;
}

void UIInitialiseKeyboardShortcutNamesTable() {
#define ADD_KEYBOARD_SHORTCUT_NAME(a, b) HashTablePutShort(&gui.keyboardShortcutNames, a, (void *) b)
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_A, "A");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_B, "B");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_C, "C");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_D, "D");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_E, "E");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_F, "F");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_G, "G");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_H, "H");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_I, "I");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_J, "J");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_K, "K");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_L, "L");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_M, "M");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_N, "N");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_O, "O");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_P, "P");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_Q, "Q");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_R, "R");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_S, "S");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_T, "T");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_U, "U");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_V, "V");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_W, "W");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_X, "X");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_Y, "Y");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_Z, "Z");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_0, "0");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_1, "1");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_2, "2");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_3, "3");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_4, "4");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_5, "5");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_6, "6");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_7, "7");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_8, "8");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_9, "9");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_BACKSPACE, "Backspace");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_ESCAPE, "Esc");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_INSERT, "Ins");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_HOME, "Home");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_PAGE_UP, "PgUp");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_DELETE, "Del");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_END, "End");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_PAGE_DOWN, "PgDn");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_UP_ARROW, "Up");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_LEFT_ARROW, "Left");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_DOWN_ARROW, "Down");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_RIGHT_ARROW, "Right");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_SPACE, "Space");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_TAB, "Tab");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_ENTER, "Enter");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_F1, "F1");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_F2, "F2");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_F3, "F3");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_F4, "F4");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_F5, "F5");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_F6, "F6");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_F7, "F7");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_F8, "F8");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_F9, "F9");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_F10, "F10");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_F11, "F11");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_F12, "F12");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_MM_NEXT, "Next Track");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_MM_PREVIOUS, "Previous Track");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_MM_STOP, "Stop Media");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_MM_PAUSE, "Play/Pause");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_MM_MUTE, "Mute");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_MM_QUIETER, "Quieter");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_MM_LOUDER, "Louder");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_MM_SELECT, "Open Media");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_WWW_SEARCH, "Search");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_WWW_HOME, "Homepage");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_WWW_BACK, "Back");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_WWW_FORWARD, "Forward");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_WWW_STOP, "Stop");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_WWW_REFRESH, "Refresh");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_WWW_STARRED, "Bookmark");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_SLASH, "/");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_PUNCTUATION_1, "\\");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_LEFT_BRACE, "(");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_RIGHT_BRACE, ")");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_EQUALS, "=");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_PUNCTUATION_5, "`");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_HYPHEN, "-");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_PUNCTUATION_3, ";");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_PUNCTUATION_4, "'");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_COMMA, ",");
	ADD_KEYBOARD_SHORTCUT_NAME(ES_SCANCODE_PERIOD, ".");
}

void AccessKeysCenterHint(EsElement *element, EsMessage *message) {
	EsRectangle bounds = element->GetWindowBounds();
	UIStyle *style = gui.accessKeys.hintStyle;
	int x = (bounds.l + bounds.r) / 2, y = (bounds.t + bounds.b) / 2 - style->preferredHeight / 4;
	*message->accessKeyHintBounds = ES_RECT_4(x - style->preferredWidth / 2, x + style->preferredWidth / 2, 
			y - style->preferredHeight / 4, y + 3 * style->preferredHeight / 4);
}

void AccessKeysGather(EsElement *element) {
	if (element->flags & ES_ELEMENT_BLOCK_FOCUS) return;
	if (element->state & UI_STATE_BLOCK_INTERACTION) return;
	if (element->flags & ES_ELEMENT_HIDDEN) return;

	for (uintptr_t i = 0; i < element->children.Length(); i++) {
		AccessKeysGather(element->children[i]);
	}

	if (!element->accessKey) return;
	if (element->state & UI_STATE_DESTROYING) return;
	if (element->flags & ES_ELEMENT_DISABLED) return;

	AccessKeyEntry entry = {};
	entry.character = element->accessKey;
	entry.number = gui.accessKeys.numbers[entry.character - 'A'];
	entry.element = element;

	if (entry.number >= 10) return;

	EsRectangle bounds = element->GetWindowBounds();
	UIStyle *style = gui.accessKeys.hintStyle;
	int x = (bounds.l + bounds.r) / 2, y = bounds.b;
	EsRectangle hintBounds = ES_RECT_4(x - style->preferredWidth / 2, x + style->preferredWidth / 2, 
			y - style->preferredHeight / 4, y + 3 * style->preferredHeight / 4);

	EsMessage m = {};
	m.type = ES_MSG_GET_ACCESS_KEY_HINT_BOUNDS;
	m.accessKeyHintBounds = &hintBounds;
	EsMessageSend(element, &m);

	if (hintBounds.r > (int32_t) gui.accessKeys.window->windowWidth) {
		hintBounds.l = gui.accessKeys.window->windowWidth - style->preferredWidth;
		hintBounds.r = hintBounds.l + style->preferredWidth;
	}

	if (hintBounds.l < 0) {
		hintBounds.l = 0;
		hintBounds.r = hintBounds.l + style->preferredWidth;
	}

	if (hintBounds.b > (int32_t) gui.accessKeys.window->windowHeight) {
		hintBounds.t = gui.accessKeys.window->windowHeight - style->preferredHeight;
		hintBounds.b = hintBounds.t + style->preferredHeight;
	}

	if (hintBounds.t < 0) {
		hintBounds.t = 0;
		hintBounds.b = hintBounds.t + style->preferredHeight;
	}

	entry.bounds = hintBounds;

	gui.accessKeys.entries.Add(entry);
	gui.accessKeys.numbers[entry.character - 'A']++;
}

void AccessKeyHintsShow(EsPainter *painter) {
	for (uintptr_t i = 0; i < gui.accessKeys.entries.Length(); i++) {
		AccessKeyEntry *entry = &gui.accessKeys.entries[i];
		UIStyle *style = gui.accessKeys.hintStyle;

		if (gui.accessKeys.typedCharacter && entry->character != gui.accessKeys.typedCharacter) {
			continue;
		}

		style->PaintLayers(painter, entry->bounds, 0, THEME_LAYER_MODE_BACKGROUND);
		char c = gui.accessKeys.typedCharacter ? entry->number + '0' : entry->character;
		style->PaintText(painter, nullptr, entry->bounds, &c, 1, 0, ES_FLAGS_DEFAULT);
	}
}

int AccessKeyLayerMessage(EsElement *element, EsMessage *message) {
	if (message->type == ES_MSG_PAINT && gui.accessKeyMode && gui.accessKeys.window == element->window) {
		AccessKeyHintsShow(message->painter);
	}

	return 0;
}

void AccessKeyModeEnter(EsWindow *window) {
	if (window->hasDialog || gui.menuMode || gui.accessKeyMode || window->windowStyle != ES_WINDOW_NORMAL) {
		return;
	}

	if (!gui.accessKeys.hintStyle) {
		gui.accessKeys.hintStyle = GetStyle(MakeStyleKey(ES_STYLE_ACCESS_KEY_HINT, 0), false);
	}

	gui.accessKeyMode = true;
	gui.accessKeys.window = window; 
	AccessKeysGather(window);

	for (uintptr_t i = 0; i < gui.accessKeys.entries.Length(); i++) {
		if (gui.accessKeys.numbers[gui.accessKeys.entries[i].character - 'A'] == 1) {
			gui.accessKeys.entries[i].number = -1;
		}
	}

	window->Repaint(true);
}

void AccessKeyModeExit() {
	if (!gui.accessKeyMode) {
		return;
	}

	gui.accessKeys.entries.Free();
	gui.accessKeys.window->Repaint(true);
	EsMemoryZero(gui.accessKeys.numbers, sizeof(gui.accessKeys.numbers));
	gui.accessKeys.typedCharacter = 0;
	gui.accessKeys.window = nullptr;
	gui.accessKeyMode = false;
}

void AccessKeyModeHandleKeyPress(EsMessage *message) {
	if (message->type == ES_MSG_KEY_UP) {
		return;
	}

	EsWindow *window = gui.accessKeys.window;

	int ic, isc;
	ConvertScancodeToCharacter(message->keyboard.scancode, &ic, &isc, false, false);
	ic = EsCRTtoupper(ic);

	bool keepAccessKeyModeActive = false;
	bool regatherKeys = false;

	if (ic >= 'A' && ic <= 'Z' && !gui.accessKeys.typedCharacter) {
		if (gui.accessKeys.numbers[ic - 'A'] > 1) {
			keepAccessKeyModeActive = true;
			gui.accessKeys.typedCharacter = ic;
		} else if (gui.accessKeys.numbers[ic - 'A'] == 1) {
			for (uintptr_t i = 0; i < gui.accessKeys.entries.Length(); i++) {
				AccessKeyEntry *entry = &gui.accessKeys.entries[i];

				if (entry->character == ic) {
					EsMessage m = { ES_MSG_MOUSE_LEFT_CLICK };
					EsMessageSend(entry->element, &m);
					EsElementFocus(entry->element, ES_ELEMENT_FOCUS_ENSURE_VISIBLE | ES_ELEMENT_FOCUS_FROM_KEYBOARD);

					keepAccessKeyModeActive = entry->element->flags & ES_ELEMENT_STICKY_ACCESS_KEY;
					regatherKeys = true;
				}
			}
		}
	} else if (ic >= '0' && ic <= '9' && gui.accessKeys.typedCharacter) {
		for (uintptr_t i = 0; i < gui.accessKeys.entries.Length(); i++) {
			AccessKeyEntry *entry = &gui.accessKeys.entries[i];

			if (entry->character == gui.accessKeys.typedCharacter && entry->number == ic - '0') {
				EsMessage m = { ES_MSG_MOUSE_LEFT_CLICK };
				EsMessageSend(entry->element, &m);
				EsElementFocus(entry->element, ES_ELEMENT_FOCUS_ENSURE_VISIBLE | ES_ELEMENT_FOCUS_FROM_KEYBOARD);

				keepAccessKeyModeActive = entry->element->flags & ES_ELEMENT_STICKY_ACCESS_KEY;
				regatherKeys = true;
			}
		}
	}

	if (!keepAccessKeyModeActive) {
		AccessKeyModeExit();
	} else if (regatherKeys) {
		AccessKeyModeExit();
		UIWindowLayoutNow(window, nullptr);
		AccessKeyModeEnter(window);
	} else {
		window->Repaint(true);
	}
}

void UIRefreshPrimaryClipboard(EsWindow *window) {
	if (window->focused) {
		EsMessage m = {};
		m.type = ES_MSG_PRIMARY_CLIPBOARD_UPDATED;
		EsMessageSend(window->focused, &m);
	}
}

void UIHandleKeyMessage(EsWindow *window, EsMessage *message) {
	if (message->type == ES_MSG_KEY_UP) {
		if (message->keyboard.scancode == ES_SCANCODE_LEFT_CTRL  ) gui.leftModifiers  &= ~ES_MODIFIER_CTRL;
		if (message->keyboard.scancode == ES_SCANCODE_LEFT_ALT   ) gui.leftModifiers  &= ~ES_MODIFIER_ALT;
		if (message->keyboard.scancode == ES_SCANCODE_LEFT_SHIFT ) gui.leftModifiers  &= ~ES_MODIFIER_SHIFT;
		if (message->keyboard.scancode == ES_SCANCODE_LEFT_FLAG  ) gui.leftModifiers  &= ~ES_MODIFIER_FLAG;
		if (message->keyboard.scancode == ES_SCANCODE_RIGHT_CTRL ) gui.rightModifiers &= ~ES_MODIFIER_CTRL;
		if (message->keyboard.scancode == ES_SCANCODE_RIGHT_ALT  ) gui.rightModifiers &= ~ES_MODIFIER_ALT;
		if (message->keyboard.scancode == ES_SCANCODE_RIGHT_SHIFT) gui.rightModifiers &= ~ES_MODIFIER_SHIFT;
		if (message->keyboard.scancode == ES_SCANCODE_RIGHT_FLAG ) gui.rightModifiers &= ~ES_MODIFIER_FLAG;

		if (message->keyboard.scancode == ES_SCANCODE_LEFT_ALT && gui.unhandledAltPress) {
			AccessKeyModeEnter(window);
		} else if (window->focused) {
			EsMessageSend(window->focused, message);
		} else if (window->windowStyle == ES_WINDOW_NORMAL) {
			uint8_t m = DESKTOP_MSG_UNHANDLED_KEY_EVENT;
			MessageDesktop(&m, 1, window->handle);
		}

		return;
	}

	if (window->targetMenu) {
		window = window->targetMenu;
	}

	gui.unhandledAltPress = false;

	if (message->keyboard.scancode == ES_SCANCODE_F2 && message->keyboard.modifiers == ES_MODIFIER_ALT) {
		EnterDebugger();
		EsPrint("[Alt-F2]\n");
	}

	if (message->keyboard.scancode == ES_SCANCODE_LEFT_CTRL  ) gui.leftModifiers  |= ES_MODIFIER_CTRL;
	if (message->keyboard.scancode == ES_SCANCODE_LEFT_ALT   ) gui.leftModifiers  |= ES_MODIFIER_ALT;
	if (message->keyboard.scancode == ES_SCANCODE_LEFT_SHIFT ) gui.leftModifiers  |= ES_MODIFIER_SHIFT;
	if (message->keyboard.scancode == ES_SCANCODE_LEFT_FLAG  ) gui.leftModifiers  |= ES_MODIFIER_FLAG;
	if (message->keyboard.scancode == ES_SCANCODE_RIGHT_CTRL ) gui.rightModifiers |= ES_MODIFIER_CTRL;
	if (message->keyboard.scancode == ES_SCANCODE_RIGHT_ALT  ) gui.rightModifiers |= ES_MODIFIER_ALT;
	if (message->keyboard.scancode == ES_SCANCODE_RIGHT_SHIFT) gui.rightModifiers |= ES_MODIFIER_SHIFT;
	if (message->keyboard.scancode == ES_SCANCODE_RIGHT_FLAG ) gui.rightModifiers |= ES_MODIFIER_FLAG;

	if (window->windowStyle == ES_WINDOW_MENU && message->keyboard.scancode == ES_SCANCODE_ESCAPE) {
		window->Destroy();
		return;
	}

	if (gui.menuMode) {
		// TODO Check the window is the one that enabled menu mode.
		// TODO Escape to close/exit menu mode.
		// TODO Left/right to navigate columns/cycle menubar and open/close submenus.
		// TODO Up/down to traverse menu.
		// TODO Enter to open submenu/invoke item.
		return;
	} else if (gui.accessKeyMode && gui.accessKeys.window == window) {
		AccessKeyModeHandleKeyPress(message);
		return;
	}

	if (window->pressed) {
		if (message->keyboard.scancode == ES_SCANCODE_ESCAPE) {
			UIMousePressReleased(window, nullptr, false);
			return;
		}
	}

	if (window->focused) {
		message->type = ES_MSG_KEY_TYPED;
		if (EsMessageSend(window->focused, message) != 0) return;

		EsElement *element = window->focused;
		message->type = ES_MSG_KEY_DOWN;

		if (UIMessageSendPropagateToAncestors(element, message)) {
			return;
		}
	}

	if (message->keyboard.scancode == ES_SCANCODE_TAB && (message->keyboard.modifiers & ~ES_MODIFIER_SHIFT) == 0) {
		EsElement *element = window->focused ?: window;
		EsElement *start = element;

		do element = UITabTraversalDo(element, message->keyboard.modifiers & ES_MODIFIER_SHIFT);
		while ((!element->IsFocusable() || (element->flags & ES_ELEMENT_NOT_TAB_TRAVERSABLE)) && element != start);

		EsElementFocus(element, ES_ELEMENT_FOCUS_ENSURE_VISIBLE | ES_ELEMENT_FOCUS_FROM_KEYBOARD);
		return;
	}

	if (window->focused) {
		if (message->keyboard.scancode == ES_SCANCODE_SPACE && message->keyboard.modifiers == 0) {
			EsMessage m = { ES_MSG_MOUSE_LEFT_CLICK };
			EsMessageSend(window->focused, &m);
			return;
		}
	} else {
		EsMessageSend(window, message);
	}

	// TODO Radio group navigation.

	if (window->enterButton && message->keyboard.scancode == ES_SCANCODE_ENTER && !message->keyboard.modifiers
			&& window->enterButton->onCommand && (~window->enterButton->flags & ES_ELEMENT_DISABLED)) {
		window->enterButton->onCommand(window->instance, window->enterButton, window->enterButton->command);
		return;
	} else if (window->escapeButton && message->keyboard.scancode == ES_SCANCODE_ESCAPE && !message->keyboard.modifiers
			&& window->escapeButton->onCommand && (~window->escapeButton->flags & ES_ELEMENT_DISABLED)) {
		window->escapeButton->onCommand(window->instance, window->escapeButton, window->escapeButton->command);
		return;
	}

	if (!window->hasDialog) {
		// TODO Sort out what commands can be used from within dialogs and menus.

		if (message->keyboard.scancode == ES_SCANCODE_LEFT_ALT) {
			gui.unhandledAltPress = true;
			return;
		}

		if (!gui.keyboardShortcutNames.itemCount) UIInitialiseKeyboardShortcutNamesTable();
		const char *shortcutName = (const char *) HashTableGetShort(&gui.keyboardShortcutNames, message->keyboard.scancode);

		if (shortcutName && window->instance && window->instance->_private) {
			APIInstance *instance = (APIInstance *) window->instance->_private;

			char keyboardShortcutString[128];
			size_t bytes = EsStringFormat(keyboardShortcutString, 128, "%z%z%z%z%c",
					(message->keyboard.modifiers & ES_MODIFIER_CTRL)  ? "Ctrl+"  : "", 
					(message->keyboard.modifiers & ES_MODIFIER_SHIFT) ? "Shift+" : "", 
					(message->keyboard.modifiers & ES_MODIFIER_ALT)   ? "Alt+"   : "", 
					shortcutName, 0) - 1;

			for (uintptr_t i = 0; i < instance->commands.Count(); i++) {
				EsCommand *command = instance->commands[i];
				if (!command->cKeyboardShortcut || command->disabled) continue;
				const char *position = EsCRTstrstr(command->cKeyboardShortcut, keyboardShortcutString);
				if (!position) continue;

				if ((position[bytes] == 0 || position[bytes] == '|') && (position == command->cKeyboardShortcut || position[-1] == '|')) {
					if (command->callback) {
						command->callback(window->instance, nullptr, command);
					}

					return;
				}
			}
		}
	}

	if (window->windowStyle == ES_WINDOW_NORMAL) {
		uint8_t m = DESKTOP_MSG_UNHANDLED_KEY_EVENT;
		MessageDesktop(&m, 1, window->handle);
		return;
	}
}

void UIWindowPaintNow(EsWindow *window, ProcessMessageTiming *timing, bool afterResize) {
	if (window->doNotPaint) {
		return;
	}

	// Calculate the regions to repaint, and then perform painting.

	if (timing) timing->startPaint = EsTimeStampMs();

	EsRectangle updateRegion = window->updateRegion;

	EsRectangle bounds = ES_RECT_4(0, window->windowWidth, 0, window->windowHeight);
	EsRectangleClip(updateRegion, bounds, &updateRegion);

	if (THEME_RECT_VALID(updateRegion)) {
		EsPainter painter = {};
		EsPaintTarget target = {};

		target.fullAlpha = window->windowStyle != ES_WINDOW_NORMAL;
		target.width = Width(updateRegion);
		target.height = Height(updateRegion);
		target.stride = target.width * 4;
		target.bits = EsHeapAllocate(target.stride * target.height, false);
		target.forWindowManager = true;

		if (!target.bits) {
			return; // Insufficient memory for painting.
		}

		EsMemoryFaultRange(target.bits, target.stride * target.height);
		painter.offsetX -= updateRegion.l;
		painter.offsetY -= updateRegion.t;
		painter.clip = ES_RECT_4(0, target.width, 0, target.height);
		painter.target = &target;

		window->updateRegionInProgress = updateRegion;
		window->InternalPaint(&painter, ES_FLAGS_DEFAULT);
		window->updateRegionInProgress = {};
		if (timing) timing->endPaint = EsTimeStampMs();

		if (window->visualizeRepaints) {
			EsDrawRectangle(&painter, painter.clip, 0, EsRandomU64(), ES_RECT_1(3));
		}

		// Update the screen.
		if (timing) timing->startUpdate = EsTimeStampMs();
		EsSyscall(ES_SYSCALL_WINDOW_SET_BITS, window->handle, (uintptr_t) &updateRegion, (uintptr_t) target.bits,
				afterResize ? WINDOW_SET_BITS_AFTER_RESIZE : WINDOW_SET_BITS_NORMAL);
		if (timing) timing->endUpdate = EsTimeStampMs();

		EsHeapFree(target.bits);
	}

	window->updateRegion = ES_RECT_4(window->windowWidth, 0, window->windowHeight, 0);
}

void UIWindowLayoutNow(EsWindow *window, ProcessMessageTiming *timing) {
	if (timing) timing->startLayout = EsTimeStampMs();

	window->InternalMove(window->width, window->height, 0, 0);

	if (window->ensureVisible) {
		EsElement *child = window->ensureVisible, *e = window->ensureVisible;

		while (e->parent) {
			EsMessage m = { ES_MSG_ENSURE_VISIBLE };
			m.child = child;
			e = e->parent;

			if (ES_HANDLED == EsMessageSend(e, &m)) {
				child = e;
			}
		}

		window->ensureVisible = nullptr;
	}

	if (window->processCheckVisible) {
		for (uintptr_t i = 0; i < window->checkVisible.Length(); i++) {
			EsElement *element = window->checkVisible[i];
			EsAssert(element->state & UI_STATE_CHECK_VISIBLE);
			EsRectangle bounds = element->GetWindowBounds();
			bool offScreen = bounds.r < 0 || bounds.b < 0 || bounds.l >= element->window->width || bounds.t >= element->window->height;
			if (!offScreen) continue;
			element->state &= ~UI_STATE_CHECK_VISIBLE;
			window->checkVisible.Delete(i);
			i--;
			EsMessage m = { ES_MSG_NOT_VISIBLE };
			EsMessageSend(element, &m);
		}

		window->processCheckVisible = false;
	}

	if (timing) timing->endLayout = EsTimeStampMs();
}

bool UISetCursor(EsWindow *window) {
	EsCursorStyle cursorStyle = ES_CURSOR_NORMAL;
	EsElement *element = window->pressed ?: window->hovered;

	if (element) {
		EsMessage m = { ES_MSG_GET_CURSOR };

		if (ES_HANDLED == EsMessageSend(element, &m)) {
			cursorStyle = m.cursorStyle;
		} else {
			cursorStyle = (EsCursorStyle) element->currentStyle->metrics->cursor;
		}
	}

	// TODO Make these configurable in the theme file!

	int x, y, ox, oy, w, h;

#define CURSOR(_constant, _x, _y, _ox, _oy, _w, _h) _constant: { x = _x; y = _y; ox = -_ox; oy = -_oy; w = _w; h = _h; } break

	switch (cursorStyle) {
		CURSOR(case ES_CURSOR_TEXT, 84, 60, 4, 10, 11, 22);
		CURSOR(case ES_CURSOR_RESIZE_VERTICAL, 44, 12, 4, 11, 11, 24);
		CURSOR(case ES_CURSOR_RESIZE_HORIZONTAL, 68, 15, 11, 4, 24, 11);
		CURSOR(case ES_CURSOR_RESIZE_DIAGONAL_1, 55, 35, 8, 8, 19, 19);
		CURSOR(case ES_CURSOR_RESIZE_DIAGONAL_2, 82, 35, 8, 8, 19, 19);
		CURSOR(case ES_CURSOR_SPLIT_VERTICAL, 37, 55, 4, 10, 12, 24);
		CURSOR(case ES_CURSOR_SPLIT_HORIZONTAL, 56, 60, 10, 4, 24, 12);
		CURSOR(case ES_CURSOR_HAND_HOVER, 131, 14, 8, 1, 21, 26);
		CURSOR(case ES_CURSOR_HAND_DRAG, 107, 18, 7, 1, 20, 22);
		CURSOR(case ES_CURSOR_HAND_POINT, 159, 14, 5, 1, 21, 26);
		CURSOR(case ES_CURSOR_SCROLL_UP_LEFT, 217, 89, 9, 9, 16, 16);
		CURSOR(case ES_CURSOR_SCROLL_UP, 193, 87, 5, 11, 13, 18);
		CURSOR(case ES_CURSOR_SCROLL_UP_RIGHT, 234, 89, 4, 9, 16, 16);
		CURSOR(case ES_CURSOR_SCROLL_LEFT, 175, 93, 11, 5, 18, 13);
		CURSOR(case ES_CURSOR_SCROLL_CENTER, 165, 51, 12, 12, 28, 28);
		CURSOR(case ES_CURSOR_SCROLL_RIGHT, 194, 106, 4, 4, 18, 13);
		CURSOR(case ES_CURSOR_SCROLL_DOWN_LEFT, 216, 106, 10, 4, 16, 16);
		CURSOR(case ES_CURSOR_SCROLL_DOWN, 182, 107, 4, 3, 12, 16);
		CURSOR(case ES_CURSOR_SCROLL_DOWN_RIGHT, 234, 106, 4, 4, 16, 16);
		CURSOR(case ES_CURSOR_SELECT_LINES, 7, 19, 10, 0, 16, 23);
		CURSOR(case ES_CURSOR_DROP_TEXT, 11, 48, 1, 1, 21, 28);
		CURSOR(case ES_CURSOR_CROSS_HAIR_PICK, 104, 56, 11, 11, 26, 26);
		CURSOR(case ES_CURSOR_CROSS_HAIR_RESIZE, 134, 53, 11, 11, 26, 26);
		CURSOR(case ES_CURSOR_MOVE_HOVER, 226, 13, 10, 10, 24, 32);
		CURSOR(case ES_CURSOR_MOVE_DRAG, 197, 13, 10, 10, 24, 24);
		CURSOR(case ES_CURSOR_ROTATE_HOVER, 228, 49, 9, 10, 24, 32);
		CURSOR(case ES_CURSOR_ROTATE_DRAG, 198, 49, 9, 10, 22, 22);
		CURSOR(case ES_CURSOR_BLANK, 5, 13, 0, 0, 1, 1);
		CURSOR(default, 23, 18, 1, 1, 15, 24);
	}

	bool _xor = cursorStyle == ES_CURSOR_TEXT;
	bool shadow = !_xor && api.global->showCursorShadow;

	return EsSyscall(ES_SYSCALL_WINDOW_SET_CURSOR, window->handle, 
			(uintptr_t) theming.cursors.bits + x * 4 + y * theming.cursors.stride, 
			((0xFF & ox) << 0) | ((0xFF & oy) << 8) | ((0xFF & w) << 16) | ((0xFF & h) << 24), 
			theming.cursors.stride | ((uint32_t) _xor << 31) | ((uint32_t) shadow << 30));
}

void UIProcessWindowManagerMessage(EsWindow *window, EsMessage *message, ProcessMessageTiming *timing) {
	// Check if the window has been destroyed.

	if (message->type == ES_MSG_WINDOW_DESTROYED) {
		if (window->destroyInstanceAfterClose) {
			EsMessage m = {};
			m.type = ES_MSG_INSTANCE_DESTROY;
			m.instanceDestroy.instance = window->instance;
			EsAssert(window->instance->window == window);
			window->instance->window = nullptr;
			window->instance = nullptr;
			EsMessagePost(nullptr, &m); 
		}

		EsAssert(window->handle == ES_INVALID_HANDLE);
		EsHeapFree(window);
		return;
	} else if (window->handle == ES_INVALID_HANDLE) {
		return;
	}

	// Begin update.

	window->willUpdate = true;

	// Make sure any elements marked to be destroyed in between updates are actually destroyed,
	// before we begin message processing.

	if (window->InternalDestroy()) {
		// The window has been destroyed.
		return;
	}

	ProcessAnimations();
	UIFindHoverElement(window);

	// Process input message.

	if (timing) timing->startLogic = EsTimeStampMs();

	if (api.global->swapLeftAndRightButtons) {
		if      (message->type == ES_MSG_MOUSE_LEFT_DOWN ) message->type = ES_MSG_MOUSE_RIGHT_DOWN;
		else if (message->type == ES_MSG_MOUSE_RIGHT_DOWN) message->type = ES_MSG_MOUSE_LEFT_DOWN;
		else if (message->type == ES_MSG_MOUSE_LEFT_UP   ) message->type = ES_MSG_MOUSE_RIGHT_UP;
		else if (message->type == ES_MSG_MOUSE_RIGHT_UP  ) message->type = ES_MSG_MOUSE_LEFT_UP;
	}

	if (message->type == ES_MSG_MOUSE_MOVED) {
		window->mousePosition.x = message->mouseMoved.newPositionX;
		window->mousePosition.y = message->mouseMoved.newPositionY;

		if ((!window->activated || window->targetMenu) && window->windowStyle == ES_WINDOW_NORMAL) {
			window->hovering = false;
			goto skipInputMessage;
		} else if (window->dragged) {
			if (gui.draggingStarted || DistanceSquared(message->mouseMoved.newPositionX - gui.lastClickX, 
						message->mouseMoved.newPositionY - gui.lastClickY) >= GetConstantNumber("dragThreshold")) {
				EsRectangle bounds = window->dragged->GetWindowBounds();
				message->type = gui.lastClickButton == ES_MSG_MOUSE_LEFT_DOWN ? ES_MSG_MOUSE_LEFT_DRAG
					: gui.lastClickButton == ES_MSG_MOUSE_RIGHT_DOWN ? ES_MSG_MOUSE_RIGHT_DRAG
					: gui.lastClickButton == ES_MSG_MOUSE_MIDDLE_DOWN ? ES_MSG_MOUSE_MIDDLE_DRAG : (EsMessageType) 0;
				EsAssert(message->type);
				message->mouseDragged.originalPositionX = gui.lastClickX;
				message->mouseDragged.originalPositionY = gui.lastClickY;
				message->mouseDragged.newPositionX -= bounds.l;
				message->mouseDragged.newPositionY -= bounds.t;
				message->mouseDragged.originalPositionX -= bounds.l;
				message->mouseDragged.originalPositionY -= bounds.t;

				if (ES_HANDLED == EsMessageSend(window->dragged, message)) {
					gui.draggingStarted = true;
				}
			}
		} else {
			EsRectangle bounds = window->hovered->GetWindowBounds();
			message->mouseMoved.newPositionX -= bounds.l;
			message->mouseMoved.newPositionY -= bounds.t;
			EsMessageSend(window->hovered, message);
		}

		window->hovering = true;
	} else if (message->type == ES_MSG_MOUSE_EXIT) {
		window->hovering = false;
	} else if (message->type == ES_MSG_MOUSE_LEFT_DOWN || message->type == ES_MSG_MOUSE_RIGHT_DOWN || message->type == ES_MSG_MOUSE_MIDDLE_DOWN) {
		window->mousePosition.x = message->mouseDown.positionX;
		window->mousePosition.y = message->mouseDown.positionY;

		AccessKeyModeExit();

		if (gui.mouseButtonDown || window->targetMenu) {
			goto skipInputMessage;
		}

		double timeStampMs = EsTimeStampMs();

		if (gui.clickChainStartMs + api.global->clickChainTimeoutMs < timeStampMs
				|| window->hovered != gui.clickChainElement) {
			// Start a new click chain.
			gui.clickChainStartMs = timeStampMs;
			gui.clickChainCount = 1;
			gui.clickChainElement = window->hovered;
		} else {
			gui.clickChainStartMs = timeStampMs;
			gui.clickChainCount++;
		}

		message->mouseDown.clickChainCount = gui.clickChainCount;

		gui.lastClickX = message->mouseDown.positionX;
		gui.lastClickY = message->mouseDown.positionY;
		gui.lastClickButton = message->type;
		gui.mouseButtonDown = true;

		if ((~window->hovered->flags & ES_ELEMENT_DISABLED) && (~window->hovered->state & UI_STATE_BLOCK_INTERACTION)) {
			// If the hovered element is destroyed in response to one of these messages, 
			// window->hovered will be set to nullptr, so save the element here.
			EsElement *element = window->hovered;

			if (message->type == ES_MSG_MOUSE_LEFT_DOWN) {
				element->state |= UI_STATE_LEFT_PRESSED;
			}

			window->pressed = element;
			EsMessage m = { ES_MSG_PRESSED_START };
			EsMessageSend(element, &m);

			EsRectangle bounds = element->GetWindowBounds();
			message->mouseDown.positionX -= bounds.l;
			message->mouseDown.positionY -= bounds.t;

			if (ES_REJECTED != UIMessageSendPropagateToAncestors(element, message, &window->dragged)) {
				if (window->dragged && (~window->dragged->flags & ES_ELEMENT_NO_FOCUS_ON_CLICK)) {
					EsElementFocus(window->dragged, false);
				}
			}
		}

		if (window->hovered != window->focused && window->focused && (~window->focused->state & UI_STATE_LOST_STRONG_FOCUS)) {
			EsMessage m = { ES_MSG_STRONG_FOCUS_END };
			window->focused->state |= UI_STATE_LOST_STRONG_FOCUS;
			EsMessageSend(window->focused, &m);
		}
	} else if (message->type == ES_MSG_MOUSE_LEFT_UP || message->type == ES_MSG_MOUSE_RIGHT_UP || message->type == ES_MSG_MOUSE_MIDDLE_UP) {
		AccessKeyModeExit();

		if (gui.mouseButtonDown && gui.lastClickButton == message->type - 1) {
			gui.mouseButtonDown = false;
			window->dragged = nullptr;
			UIMousePressReleased(window, message, true);
		}
	} else if (message->type == ES_MSG_KEY_UP || message->type == ES_MSG_KEY_DOWN) {
		UIHandleKeyMessage(window, message);

		// If this key was on the numpad, translate it to the normal key.

		int numpad = 0, nshift = 0;
		uint32_t scancode = message->keyboard.scancode;
		bool allowShift = false;

		if (scancode == ES_SCANCODE_NUM_DIVIDE  ) { numpad = ES_SCANCODE_SLASH; }
		if (scancode == ES_SCANCODE_NUM_MULTIPLY) { numpad = ES_SCANCODE_8; nshift = 1; }
		if (scancode == ES_SCANCODE_NUM_SUBTRACT) { numpad = ES_SCANCODE_HYPHEN; }
		if (scancode == ES_SCANCODE_NUM_ADD	) { numpad = ES_SCANCODE_EQUALS; nshift = 1; }
		if (scancode == ES_SCANCODE_NUM_ENTER	) { numpad = ES_SCANCODE_ENTER; }

		if (message->keyboard.numlock) {
			if (scancode == ES_SCANCODE_NUM_POINT) { numpad = ES_SCANCODE_PERIOD; }
			if (scancode == ES_SCANCODE_NUM_0    ) { numpad = ES_SCANCODE_0; }
			if (scancode == ES_SCANCODE_NUM_1    ) { numpad = ES_SCANCODE_1; }
			if (scancode == ES_SCANCODE_NUM_2    ) { numpad = ES_SCANCODE_2; }
			if (scancode == ES_SCANCODE_NUM_3    ) { numpad = ES_SCANCODE_3; }
			if (scancode == ES_SCANCODE_NUM_4    ) { numpad = ES_SCANCODE_4; }
			if (scancode == ES_SCANCODE_NUM_5    ) { numpad = ES_SCANCODE_5; }
			if (scancode == ES_SCANCODE_NUM_6    ) { numpad = ES_SCANCODE_6; }
			if (scancode == ES_SCANCODE_NUM_7    ) { numpad = ES_SCANCODE_7; }
			if (scancode == ES_SCANCODE_NUM_8    ) { numpad = ES_SCANCODE_8; }
			if (scancode == ES_SCANCODE_NUM_9    ) { numpad = ES_SCANCODE_9; }
		} else {
			if (scancode == ES_SCANCODE_NUM_POINT) { numpad = ES_SCANCODE_DELETE; allowShift = true; }
			if (scancode == ES_SCANCODE_NUM_0    ) { numpad = ES_SCANCODE_INSERT; allowShift = true; }
			if (scancode == ES_SCANCODE_NUM_1    ) { numpad = ES_SCANCODE_END; allowShift = true; }
			if (scancode == ES_SCANCODE_NUM_2    ) { numpad = ES_SCANCODE_DOWN_ARROW; allowShift = true; }
			if (scancode == ES_SCANCODE_NUM_3    ) { numpad = ES_SCANCODE_PAGE_DOWN; allowShift = true; }
			if (scancode == ES_SCANCODE_NUM_4    ) { numpad = ES_SCANCODE_LEFT_ARROW; allowShift = true; }
			if (scancode == ES_SCANCODE_NUM_6    ) { numpad = ES_SCANCODE_RIGHT_ARROW; allowShift = true; }
			if (scancode == ES_SCANCODE_NUM_7    ) { numpad = ES_SCANCODE_HOME; allowShift = true; }
			if (scancode == ES_SCANCODE_NUM_8    ) { numpad = ES_SCANCODE_UP_ARROW; allowShift = true; }
			if (scancode == ES_SCANCODE_NUM_9    ) { numpad = ES_SCANCODE_PAGE_UP; allowShift = true; }
		}

		if (numpad && ((~message->keyboard.modifiers & ES_MODIFIER_SHIFT) || allowShift)) {
			EsMessage m = *message;
			m.type = message->type;
			m.keyboard.modifiers = message->keyboard.modifiers | (nshift ? ES_MODIFIER_SHIFT : 0);
			m.keyboard.scancode = numpad;
			m.keyboard.numpad = true;
			UIHandleKeyMessage(window, &m);
		}
	} else if (message->type == ES_MSG_WINDOW_RESIZED) {
		AccessKeyModeExit();

		gui.leftModifiers = gui.rightModifiers = 0;
		gui.clickChainStartMs = 0;

		window->receivedFirstResize = true;

		if (!window->width || !window->height) {
			UIRefreshPrimaryClipboard(window); // Embedded window activated.
		}

		window->width = window->windowWidth = message->windowResized.content.r;
		window->height = window->windowHeight = message->windowResized.content.b;

		if (window->windowStyle == ES_WINDOW_CONTAINER) {
			EsRectangle opaqueBounds = ES_RECT_4(WINDOW_INSET, window->windowWidth - WINDOW_INSET, 
					WINDOW_INSET, window->windowHeight - WINDOW_INSET);
			EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, (uintptr_t) &opaqueBounds, 0, ES_WINDOW_PROPERTY_OPAQUE_BOUNDS);
		} else if (window->windowStyle == ES_WINDOW_INSPECTOR) {
			EsRectangle opaqueBounds = ES_RECT_4(0, window->windowWidth, 0, window->windowHeight);
			EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, (uintptr_t) &opaqueBounds, 0, ES_WINDOW_PROPERTY_OPAQUE_BOUNDS);
		}

		if (!message->windowResized.hidden) {
			EsElementRelayout(window);
			window->Repaint(true);
		}

		EsMessageSend(window, message);

		for (uintptr_t i = 0; i < window->sizeAlternatives.Length(); i++) {
			SizeAlternative *alternative = &window->sizeAlternatives[i];
			bool belowThreshold = window->width < alternative->widthThreshold * theming.scale 
				|| window->height < alternative->heightThreshold * theming.scale;
			EsElementSetHidden(alternative->small, !belowThreshold);
			EsElementSetHidden(alternative->big, belowThreshold);
		}
	} else if (message->type == ES_MSG_WINDOW_DEACTIVATED) {
		AccessKeyModeExit();

		if (window->windowStyle == ES_WINDOW_MENU) {
			window->Destroy();
		} else if (window->windowStyle == ES_WINDOW_CONTAINER) {
			// Redraw window borders.
			window->SetStyle(ES_STYLE_CONTAINER_WINDOW_INACTIVE);
			window->Repaint(true);
		}

		window->activated = false;
		window->hovering = false;

		if (window->focused) {
			window->inactiveFocus = window->focused;
			window->inactiveFocus->Repaint(true);
			UIRemoveFocusFromElement(window->focused);
			window->focused = nullptr;
		}

		EsMessageSend(window, message);
	} else if (message->type == ES_MSG_WINDOW_ACTIVATED) {
		AccessKeyModeExit();

		gui.leftModifiers = gui.rightModifiers = 0;
		gui.clickChainStartMs = 0;

		window->activated = true;
		EsMessage m = { ES_MSG_WINDOW_ACTIVATED };
		EsMessageSend(window, &m);

		if (window->windowStyle == ES_WINDOW_CONTAINER) {
			// Redraw window borders.
			window->SetStyle(ES_STYLE_CONTAINER_WINDOW_ACTIVE);
			window->Repaint(true);
		}

		if (!window->focused && window->inactiveFocus) {
			EsElementFocus(window->inactiveFocus, false);
			window->inactiveFocus->Repaint(true);
			window->inactiveFocus = nullptr;
		}

		UIRefreshPrimaryClipboard(window);
	}

	skipInputMessage:;

	if (timing) timing->endLogic = EsTimeStampMs();

	// Destroy, relayout, and repaint elements as necessary.

	if (window->InternalDestroy()) {
		// The window has been destroyed.
		return;
	}

	UIFindHoverElement(window);

	bool changedCursor = UISetCursor(window);

	if (window->receivedFirstResize || window->windowStyle != ES_WINDOW_NORMAL) {
		UIWindowLayoutNow(window, timing);
	}

	if (THEME_RECT_VALID(window->updateRegion) && window->width == (int) window->windowWidth && window->height == (int) window->windowHeight) {
		UIWindowPaintNow(window, timing, message->type == ES_MSG_WINDOW_RESIZED);
	} else if (changedCursor) {
		EsSyscall(ES_SYSCALL_SCREEN_FORCE_UPDATE, 0, 0, 0, 0);
	}

	if (window->instance) {
		APIInstance *instance = (APIInstance *) window->instance->_private;
		EsUndoEndGroup(instance->activeUndoManager);
	}

	// Free any unused styles.

	FreeUnusedStyles();

	// Finish update.

	window->willUpdate = false;
}

// --------------------------------- List view.

#include "list_view.cpp"

// --------------------------------- Inspector.

struct InspectorElementEntry {
	EsElement *element;
	EsRectangle takenBounds, givenBounds;
	int depth;
};

struct InspectorWindow : EsInstance {
	EsInstance *instance; // The instance being inspected.

	EsListView *elementList;
	Array<InspectorElementEntry> elements;
	InspectorElementEntry hoveredElement;
	char *cCategoryFilter;

	intptr_t selectedElement;
	EsButton *alignH[6];
	EsButton *alignV[6];
	EsButton *direction[4];
	EsTextbox *contentTextbox;
	EsButton *addChildButton;
	EsButton *addSiblingButton;
	EsButton *visualizeRepaints;
	EsButton *visualizeLayoutBounds;
	EsButton *visualizePaintSteps;
	EsListView *listEvents;
	EsTextbox *textboxCategoryFilter;
};

EsListViewColumn inspectorElementListColumns[] = {
	{ "Name", -1, 0, 300 },
	{ "Bounds", -1, 0, 200 },
	{ "Information", -1, 0, 200 },
};

int InspectorElementItemCallback(EsElement *element, EsMessage *message) {
	InspectorWindow *inspector = (InspectorWindow *) element->instance;

	if (message->type == ES_MSG_HOVERED_START) {
		InspectorElementEntry *entry = &inspector->elements[EsListViewGetIndexFromItem(element)];
		if (entry->element->parent) entry->element->parent->Repaint(true);
		else entry->element->Repaint(true);
		inspector->hoveredElement = *entry;
	} else if (message->type == ES_MSG_HOVERED_END || message->type == ES_MSG_DESTROY) {
		EsListViewIndex index = EsListViewGetIndexFromItem(element);
		InspectorElementEntry *entry = &inspector->elements[index];
		if (entry->element->parent) entry->element->parent->Repaint(true);
		else entry->element->Repaint(true);
		inspector->hoveredElement = {};
	}

	return 0;
}

void InspectorUpdateEditor(InspectorWindow *inspector) {
	EsElement *e = inspector->selectedElement == -1 ? nullptr : inspector->elements[inspector->selectedElement].element;

	bool isStack = e && e->messageClass == ProcessPanelMessage && !(e->flags & (ES_PANEL_Z_STACK | ES_PANEL_TABLE | ES_PANEL_SWITCHER));
	bool alignHLeft = e ? (e->flags & ES_CELL_H_LEFT) : false, alignHRight = e ? (e->flags & ES_CELL_H_RIGHT) : false;
	bool alignHExpand = e ? (e->flags & ES_CELL_H_EXPAND) : false, alignHShrink = e ? (e->flags & ES_CELL_H_SHRINK) : false; 
	bool alignHPush = e ? (e->flags & ES_CELL_H_PUSH) : false;
	bool alignVTop = e ? (e->flags & ES_CELL_V_TOP) : false, alignVBottom = e ? (e->flags & ES_CELL_V_BOTTOM) : false;
	bool alignVExpand = e ? (e->flags & ES_CELL_V_EXPAND) : false, alignVShrink = e ? (e->flags & ES_CELL_V_SHRINK) : false; 
	bool alignVPush = e ? (e->flags & ES_CELL_V_PUSH) : false;
	bool stackHorizontal = isStack && (e->flags & ES_PANEL_HORIZONTAL);
	bool stackReverse = isStack && (e->flags & ES_PANEL_REVERSE);

	EsButtonSetCheck(inspector->alignH[0], (EsCheckState) (e && alignHLeft && !alignHRight), false);
	EsButtonSetCheck(inspector->alignH[1], (EsCheckState) (e &&  alignHLeft == alignHRight), false);
	EsButtonSetCheck(inspector->alignH[2], (EsCheckState) (e && !alignHLeft && alignHRight), false);
	EsButtonSetCheck(inspector->alignH[3], (EsCheckState) (e && alignHExpand), false);
	EsButtonSetCheck(inspector->alignH[4], (EsCheckState) (e && alignHShrink), false);
	EsButtonSetCheck(inspector->alignH[5], (EsCheckState) (e && alignHPush), false);

	EsButtonSetCheck(inspector->alignV[0], (EsCheckState) (e && alignVTop && !alignVBottom), false);
	EsButtonSetCheck(inspector->alignV[1], (EsCheckState) (e &&  alignVTop == alignVBottom), false);
	EsButtonSetCheck(inspector->alignV[2], (EsCheckState) (e && !alignVTop && alignVBottom), false);
	EsButtonSetCheck(inspector->alignV[3], (EsCheckState) (e && alignVExpand), false);
	EsButtonSetCheck(inspector->alignV[4], (EsCheckState) (e && alignVShrink), false);
	EsButtonSetCheck(inspector->alignV[5], (EsCheckState) (e && alignVPush), false);

	EsButtonSetCheck(inspector->direction[0], (EsCheckState) (isStack &&  stackHorizontal &&  stackReverse), false);
	EsButtonSetCheck(inspector->direction[1], (EsCheckState) (isStack &&  stackHorizontal && !stackReverse), false);
	EsButtonSetCheck(inspector->direction[2], (EsCheckState) (isStack && !stackHorizontal &&  stackReverse), false);
	EsButtonSetCheck(inspector->direction[3], (EsCheckState) (isStack && !stackHorizontal && !stackReverse), false);

	EsElementSetDisabled(inspector->alignH[0], !e);
	EsElementSetDisabled(inspector->alignH[1], !e);
	EsElementSetDisabled(inspector->alignH[2], !e);
	EsElementSetDisabled(inspector->alignH[3], !e);
	EsElementSetDisabled(inspector->alignH[4], !e);
	EsElementSetDisabled(inspector->alignH[5], !e);
	EsElementSetDisabled(inspector->alignV[0], !e);
	EsElementSetDisabled(inspector->alignV[1], !e);
	EsElementSetDisabled(inspector->alignV[2], !e);
	EsElementSetDisabled(inspector->alignV[3], !e);
	EsElementSetDisabled(inspector->alignV[4], !e);
	EsElementSetDisabled(inspector->alignV[5], !e);
	EsElementSetDisabled(inspector->direction[0], !isStack);
	EsElementSetDisabled(inspector->direction[1], !isStack);
	EsElementSetDisabled(inspector->direction[2], !isStack);
	EsElementSetDisabled(inspector->direction[3], !isStack);
	EsElementSetDisabled(inspector->addChildButton, !isStack);
	EsElementSetDisabled(inspector->addSiblingButton, !e || !e->parent);

	EsElementSetDisabled(inspector->textboxCategoryFilter, !e);

	EsTextboxSelectAll(inspector->contentTextbox);
	EsTextboxInsert(inspector->contentTextbox, "", 0, false);

	if (e) {
#if 0
		for (uintptr_t i = 0; i < sizeof(builtinStyles) / sizeof(builtinStyles[0]); i++) {
			if (e->currentStyleKey.partHash == CalculateCRC64(EsLiteral(builtinStyles[i]))) {
				EsTextboxInsert(inspector->styleTextbox, builtinStyles[i], -1, false);
				break;
			}
		}
#endif

		if (e->messageClass == ProcessButtonMessage) {
			EsButton *button = (EsButton *) e;
			EsElementSetDisabled(inspector->contentTextbox, false);
			EsTextboxInsert(inspector->contentTextbox, button->label, button->labelBytes, false);
		} else if (e->messageClass == ProcessTextDisplayMessage) {
			EsTextDisplay *display = (EsTextDisplay *) e;
			EsElementSetDisabled(inspector->contentTextbox, false);
			EsTextboxInsert(inspector->contentTextbox, display->contents, display->textRuns[display->textRunCount].offset, false);
		} else {
			EsElementSetDisabled(inspector->contentTextbox, true);
		}
	} else {
		EsElementSetDisabled(inspector->contentTextbox, true);
	}
}

int InspectorElementListCallback(EsElement *element, EsMessage *message) {
	InspectorWindow *inspector = (InspectorWindow *) element->instance;

	if (message->type == ES_MSG_LIST_VIEW_GET_CONTENT) {
		int column = message->getContent.column, index = message->getContent.index;
		EsAssert(index >= 0 && index < (int) inspector->elements.Length());
		InspectorElementEntry *entry = &inspector->elements[index];

		if (column == 0) {
			EsBufferFormat(message->getContent.buffer, "%z", entry->element->cName);
		} else if (column == 1) {
			EsBufferFormat(message->getContent.buffer, "%R", entry->element->GetWindowBounds(false));
		} else if (column == 2) {
			EsMessage m = *message;
			m.type = ES_MSG_GET_INSPECTOR_INFORMATION;
			EsMessageSend(entry->element, &m);
		}

		return ES_HANDLED;
	} else if (message->type == ES_MSG_LIST_VIEW_GET_INDENT) {
		message->getIndent.indent = inspector->elements[message->getIndent.index].depth;
		return ES_HANDLED;
	} else if (message->type == ES_MSG_LIST_VIEW_CREATE_ITEM) {
		message->createItem.item->messageUser = InspectorElementItemCallback;
		return ES_HANDLED;
	} else if (message->type == ES_MSG_LIST_VIEW_SELECT) {
		if (inspector->selectedElement != -1) {
			inspector->elements[inspector->selectedElement].element->state &= ~UI_STATE_INSPECTING;
		}
		
		inspector->selectedElement = message->selectItem.isSelected ? message->selectItem.index : -1;

		if (inspector->selectedElement != -1) {
			EsElement *e = inspector->elements[inspector->selectedElement].element;
			e->state |= UI_STATE_INSPECTING;
			InspectorNotifyElementEvent(e, nullptr, "Viewing events from '%z'.\n", e->cName);
		}

		InspectorUpdateEditor(inspector);
		return ES_HANDLED;
	} else if (message->type == ES_MSG_LIST_VIEW_IS_SELECTED) {
		message->selectItem.isSelected = message->selectItem.index == inspector->selectedElement;
		return ES_HANDLED;
	}

	return 0;
}

int InspectorContentTextboxCallback(EsElement *element, EsMessage *message) {
	InspectorWindow *inspector = (InspectorWindow *) element->instance;

	if (message->type == ES_MSG_TEXTBOX_EDIT_END) {
		size_t newContentBytes;
		char *newContent = EsTextboxGetContents(inspector->contentTextbox, &newContentBytes);
		EsElement *e = inspector->elements[inspector->selectedElement].element;

		if (e->messageClass == ProcessButtonMessage) {
			EsButton *button = (EsButton *) e;
			HeapDuplicate((void **) &button->label, newContent, newContentBytes);
			button->labelBytes = newContentBytes;
		} else if (e->messageClass == ProcessTextDisplayMessage) {
			EsTextDisplay *display = (EsTextDisplay *) e;
			EsTextDisplaySetContents(display, newContent, newContentBytes);
		} else {
			EsAssert(false);
		}

		EsElementUpdateContentSize(e);
		if (e->parent) EsElementUpdateContentSize(e->parent);
		EsHeapFree(newContent);
		return ES_HANDLED;
	}

	return 0;
}

int InspectorTextboxCategoryFilterCallback(EsElement *element, EsMessage *message) {
	InspectorWindow *inspector = (InspectorWindow *) element->instance;
	
	if (message->type == ES_MSG_TEXTBOX_UPDATED) {
		EsHeapFree(inspector->cCategoryFilter);
		inspector->cCategoryFilter = EsTextboxGetContents((EsTextbox *) element);
	}

	return 0;
}

InspectorWindow *InspectorGet(EsElement *element) {
	if (!element->window || !element->instance) return nullptr;
	APIInstance *instance = (APIInstance *) element->instance->_private;
	InspectorWindow *inspector = instance->attachedInspector;
	if (!inspector || inspector->instance->window != element->window) return nullptr;
	return inspector;
}

void InspectorNotifyElementEvent(EsElement *element, const char *cCategory, const char *cFormat, ...) {
	if (~element->state & UI_STATE_INSPECTING) return;
	InspectorWindow *inspector = InspectorGet(element);
	if (!inspector) return;
	if (inspector->cCategoryFilter && inspector->cCategoryFilter[0] && cCategory && EsCRTstrcmp(cCategory, inspector->cCategoryFilter)) return;
	va_list arguments;
	va_start(arguments, cFormat);
	char _buffer[256];
	EsBuffer buffer = { .out = (uint8_t *) _buffer, .bytes = sizeof(_buffer) };
	if (cCategory) EsBufferFormat(&buffer, "%z: ", cCategory);
	EsBufferFormatV(&buffer, cFormat, arguments); 
	va_end(arguments);
	EsListViewFixedItemInsert(inspector->listEvents, _buffer, buffer.position);
	EsListViewScrollToEnd(inspector->listEvents);
}

void InspectorNotifyElementContentChanged(EsElement *element) {
	InspectorWindow *inspector = InspectorGet(element);
	if (!inspector) return;

	for (uintptr_t i = 0; i < inspector->elements.Length(); i++) {
		if (inspector->elements[i].element == element) {
			EsListViewInvalidateContent(inspector->elementList, 0, i);
			return;
		}
	}

	EsAssert(false);
}

void InspectorNotifyElementMoved(EsElement *element, EsRectangle takenBounds) {
	InspectorWindow *inspector = InspectorGet(element);
	if (!inspector) return;

	for (uintptr_t i = 0; i < inspector->elements.Length(); i++) {
		if (inspector->elements[i].element == element) {
			inspector->elements[i].takenBounds = takenBounds;
			inspector->elements[i].givenBounds = takenBounds; // TODO.
			EsListViewInvalidateContent(inspector->elementList, 0, i);
			return;
		}
	}

	EsAssert(false);
}

void InspectorNotifyElementDestroyed(EsElement *element) {
	InspectorWindow *inspector = InspectorGet(element);
	if (!inspector) return;

	for (uintptr_t i = 0; i < inspector->elements.Length(); i++) {
		if (inspector->elements[i].element == element) {
			if (inspector->selectedElement == (intptr_t) i) {
				inspector->selectedElement = -1;
				InspectorUpdateEditor(inspector);
			} else if (inspector->selectedElement > (intptr_t) i) {
				inspector->selectedElement--;
			}

			EsListViewRemove(inspector->elementList, 0, i, 1);
			inspector->elements.Delete(i);
			return;
		}
	}

	EsAssert(false);
}

void InspectorNotifyElementCreated(EsElement *element) {
	InspectorWindow *inspector = InspectorGet(element);
	if (!inspector) return;

	ptrdiff_t indexInParent = -1;

	for (uintptr_t i = 0; i < element->parent->children.Length(); i++) {
		if (element->parent->children[i] == element) {
			indexInParent = i;
			break;
		}
	}

	EsAssert(indexInParent != -1);

	ptrdiff_t insertAfterIndex = -1;

	for (uintptr_t i = 0; i < inspector->elements.Length(); i++) {
		if (indexInParent == 0) {
			if (inspector->elements[i].element == element->parent) {
				insertAfterIndex = i;
				break;
			}
		} else {
			if (inspector->elements[i].element == element->parent->children[indexInParent - 1]) {
				insertAfterIndex = i;
				int baseDepth = inspector->elements[i++].depth;

				for (; i < inspector->elements.Length(); i++) {
					if (inspector->elements[i].depth > baseDepth) {
						insertAfterIndex++;
					} else {
						break;
					}
				}

				break;
			}
		}
	}

	EsAssert(insertAfterIndex != -1);

	int depth = 0;
	EsElement *ancestor = element->parent;

	while (ancestor) {
		depth++;
		ancestor = ancestor->parent;
	}

	if (inspector->selectedElement > insertAfterIndex) {
		inspector->selectedElement++;
	}

	InspectorElementEntry entry;
	entry.element = element;
	entry.depth = depth;
	inspector->elements.Insert(entry, insertAfterIndex + 1);
	EsListViewInsert(inspector->elementList, 0, insertAfterIndex + 1, 1);
}

void InspectorFindElementsRecursively(InspectorWindow *inspector, EsElement *element, int depth) {
	InspectorElementEntry entry = {};
	entry.element = element;
	entry.depth = depth;
	inspector->elements.Add(entry);

	for (uintptr_t i = 0; i < element->children.Length(); i++) {
		InspectorFindElementsRecursively(inspector, element->children[i], depth + 1);
	}
}

void InspectorRefreshElementList(InspectorWindow *inspector) {
	EsListViewRemoveAll(inspector->elementList, 0);
	inspector->elements.Free();
	InspectorFindElementsRecursively(inspector, inspector->instance->window, 0);
	EsListViewInsert(inspector->elementList, 0, 0, inspector->elements.Length());
}

void InspectorNotifyElementPainted(EsElement *element, EsPainter *painter) {
	InspectorWindow *inspector = InspectorGet(element);
	if (!inspector) return;

	InspectorElementEntry *entry = inspector->hoveredElement.element ? &inspector->hoveredElement : nullptr;
	if (!entry) return;

	EsRectangle bounds = ES_RECT_4(painter->offsetX, painter->offsetX + painter->width, 
			painter->offsetY, painter->offsetY + painter->height);

	if (entry->element == element) {
		EsDrawRectangle(painter, bounds, 0x607F7FFF, 0x60FFFF7F, element->currentStyle->insets);
	} else if (entry->element->parent == element) {
		if ((element->flags & ES_CELL_FILL) != ES_CELL_FILL) {
			EsRectangle rectangle = entry->givenBounds;
			rectangle.l += bounds.l, rectangle.r += bounds.l;
			rectangle.t += bounds.t, rectangle.b += bounds.t;
			// EsDrawBlock(painter, rectangle, 0x20FF7FFF);
		}
	}
}

#define INSPECTOR_ALIGN_COMMAND(name, clear, set, toggle) \
void name (EsInstance *instance, EsElement *, EsCommand *) { \
	InspectorWindow *inspector = (InspectorWindow *) instance; \
	EsElement *e = inspector->elements[inspector->selectedElement].element; \
	if (toggle) e->flags ^= set; \
	else { e->flags &= ~(clear); e->flags |= set; } \
	EsElementUpdateContentSize(e); \
	if (e->parent) EsElementUpdateContentSize(e->parent); \
	inspector->elementList->Repaint(true); \
	InspectorUpdateEditor(inspector); \
}

INSPECTOR_ALIGN_COMMAND(InspectorHAlignLeft, ES_CELL_H_LEFT | ES_CELL_H_RIGHT, ES_CELL_H_LEFT, false);
INSPECTOR_ALIGN_COMMAND(InspectorHAlignCenter, ES_CELL_H_LEFT | ES_CELL_H_RIGHT, ES_CELL_H_LEFT | ES_CELL_H_RIGHT, false);
INSPECTOR_ALIGN_COMMAND(InspectorHAlignRight, ES_CELL_H_LEFT | ES_CELL_H_RIGHT, ES_CELL_H_RIGHT, false);
INSPECTOR_ALIGN_COMMAND(InspectorHAlignExpand, 0, ES_CELL_H_EXPAND, true);
INSPECTOR_ALIGN_COMMAND(InspectorHAlignShrink, 0, ES_CELL_H_SHRINK, true);
INSPECTOR_ALIGN_COMMAND(InspectorHAlignPush, 0, ES_CELL_H_PUSH, true);
INSPECTOR_ALIGN_COMMAND(InspectorVAlignTop, ES_CELL_V_TOP | ES_CELL_V_BOTTOM, ES_CELL_V_TOP, false);
INSPECTOR_ALIGN_COMMAND(InspectorVAlignCenter, ES_CELL_V_TOP | ES_CELL_V_BOTTOM, ES_CELL_V_TOP | ES_CELL_V_BOTTOM, false);
INSPECTOR_ALIGN_COMMAND(InspectorVAlignBottom, ES_CELL_V_TOP | ES_CELL_V_BOTTOM, ES_CELL_V_BOTTOM, false);
INSPECTOR_ALIGN_COMMAND(InspectorVAlignExpand, 0, ES_CELL_V_EXPAND, true);
INSPECTOR_ALIGN_COMMAND(InspectorVAlignShrink, 0, ES_CELL_V_SHRINK, true);
INSPECTOR_ALIGN_COMMAND(InspectorVAlignPush, 0, ES_CELL_V_PUSH, true);
INSPECTOR_ALIGN_COMMAND(InspectorDirectionLeft, ES_PANEL_HORIZONTAL | ES_PANEL_REVERSE | ES_ELEMENT_LAYOUT_HINT_HORIZONTAL | ES_ELEMENT_LAYOUT_HINT_REVERSE, 
		ES_PANEL_HORIZONTAL | ES_PANEL_REVERSE | ES_ELEMENT_LAYOUT_HINT_HORIZONTAL | ES_ELEMENT_LAYOUT_HINT_REVERSE, false);
INSPECTOR_ALIGN_COMMAND(InspectorDirectionRight, ES_PANEL_HORIZONTAL | ES_PANEL_REVERSE | ES_ELEMENT_LAYOUT_HINT_HORIZONTAL | ES_ELEMENT_LAYOUT_HINT_REVERSE, 
		ES_PANEL_HORIZONTAL | ES_ELEMENT_LAYOUT_HINT_HORIZONTAL, false);
INSPECTOR_ALIGN_COMMAND(InspectorDirectionUp, ES_PANEL_HORIZONTAL | ES_PANEL_REVERSE | ES_ELEMENT_LAYOUT_HINT_HORIZONTAL | ES_ELEMENT_LAYOUT_HINT_REVERSE, 
		ES_PANEL_REVERSE | ES_ELEMENT_LAYOUT_HINT_REVERSE, false);
INSPECTOR_ALIGN_COMMAND(InspectorDirectionDown, ES_PANEL_HORIZONTAL | ES_PANEL_REVERSE | ES_ELEMENT_LAYOUT_HINT_HORIZONTAL | ES_ELEMENT_LAYOUT_HINT_REVERSE, 0, false);

void InspectorVisualizeRepaints(EsInstance *instance, EsElement *, EsCommand *) {
	InspectorWindow *inspector = (InspectorWindow *) instance;
	EsWindow *window = inspector->instance->window;
	window->visualizeRepaints = !window->visualizeRepaints;
	EsButtonSetCheck(inspector->visualizeRepaints, window->visualizeRepaints ? ES_CHECK_CHECKED : ES_CHECK_UNCHECKED, false);
}

void InspectorVisualizePaintSteps(EsInstance *instance, EsElement *, EsCommand *) {
	InspectorWindow *inspector = (InspectorWindow *) instance;
	EsWindow *window = inspector->instance->window;
	window->visualizePaintSteps = !window->visualizePaintSteps;
	EsButtonSetCheck(inspector->visualizePaintSteps, window->visualizePaintSteps ? ES_CHECK_CHECKED : ES_CHECK_UNCHECKED, false);
}

void InspectorVisualizeLayoutBounds(EsInstance *instance, EsElement *, EsCommand *) {
	InspectorWindow *inspector = (InspectorWindow *) instance;
	EsWindow *window = inspector->instance->window;
	window->visualizeLayoutBounds = !window->visualizeLayoutBounds;
	EsButtonSetCheck(inspector->visualizeLayoutBounds, window->visualizeLayoutBounds ? ES_CHECK_CHECKED : ES_CHECK_UNCHECKED, false);
	EsElementRepaint(window);
}

void InspectorAddElement2(EsMenu *menu, EsGeneric context) {
	InspectorWindow *inspector = (InspectorWindow *) menu->instance; 
	if (inspector->selectedElement == -1) return;
	EsElement *e = inspector->elements[inspector->selectedElement].element; 
	int asSibling = context.u & 0x80;
	context.u &= ~0x80;

	if (asSibling) {
		EsElementInsertAfter(e); 
		e = e->parent;
	}

	if (context.u == 1) {
		EsButtonCreate(e);
	} else if (context.u == 2) {
		EsPanelCreate(e);
	} else if (context.u == 3) {
		EsSpacerCreate(e);
	} else if (context.u == 4) {
		EsTextboxCreate(e);
	} else if (context.u == 5) {
		EsTextDisplayCreate(e);
	}
}

void InspectorAddElement(EsInstance *, EsElement *element, EsCommand *) {
	EsMenu *menu = EsMenuCreate(element, ES_FLAGS_DEFAULT);
	EsMenuAddItem(menu, 0, "Add button", -1, InspectorAddElement2, element->userData.u | 1);
	EsMenuAddItem(menu, 0, "Add panel", -1, InspectorAddElement2, element->userData.u | 2);
	EsMenuAddItem(menu, 0, "Add spacer", -1, InspectorAddElement2, element->userData.u | 3);
	EsMenuAddItem(menu, 0, "Add textbox", -1, InspectorAddElement2, element->userData.u | 4);
	EsMenuAddItem(menu, 0, "Add text display", -1, InspectorAddElement2, element->userData.u | 5);
	EsMenuShow(menu);
}

void InspectorSetup(EsWindow *window) {
	InspectorWindow *inspector = (InspectorWindow *) EsHeapAllocate(sizeof(InspectorWindow), true); // TODO Freeing this.
	inspector->window = window;
	InstanceSetup(inspector);
	inspector->instance = window->instance;
	((APIInstance *) inspector->_private)->internalOnly = true;
	window->instance = inspector;

	inspector->selectedElement = -1;

	EsPanel *panel = EsPanelCreate(window, ES_CELL_FILL, ES_STYLE_PANEL_FILLED);

	{
		EsPanel *toolbar = EsPanelCreate(panel, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL, ES_STYLE_PANEL_TOOLBAR);
		inspector->visualizeRepaints = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR, 0, "Visualize repaints");
		EsButtonOnCommand(inspector->visualizeRepaints, InspectorVisualizeRepaints);
		inspector->visualizeLayoutBounds = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR, 0, "Visualize layout bounds");
		EsButtonOnCommand(inspector->visualizeLayoutBounds, InspectorVisualizeLayoutBounds);
		inspector->visualizePaintSteps = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR, 0, "Visualize paint steps");
		EsButtonOnCommand(inspector->visualizePaintSteps, InspectorVisualizePaintSteps);
	}

	inspector->elementList = EsListViewCreate(panel, ES_CELL_FILL | ES_LIST_VIEW_COLUMNS | ES_LIST_VIEW_SINGLE_SELECT);
	inspector->elementList->messageUser = InspectorElementListCallback;
	EsListViewSetColumns(inspector->elementList, inspectorElementListColumns, sizeof(inspectorElementListColumns) / sizeof(EsListViewColumn));
	EsListViewInsertGroup(inspector->elementList, 0);

	{
		EsPanel *toolbar = EsPanelCreate(panel, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL, ES_STYLE_PANEL_TOOLBAR);
		EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, 5, 0);
		EsTextDisplayCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, "Horizontal:");
		EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, 5, 0);
		inspector->alignH[0] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED);
		EsButtonSetIcon(inspector->alignH[0], ES_ICON_ALIGN_HORIZONTAL_LEFT);
		EsButtonOnCommand(inspector->alignH[0], InspectorHAlignLeft);
		inspector->alignH[1] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED);
		EsButtonSetIcon(inspector->alignH[1], ES_ICON_ALIGN_HORIZONTAL_CENTER);
		EsButtonOnCommand(inspector->alignH[1], InspectorHAlignCenter);
		inspector->alignH[2] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED);
		EsButtonSetIcon(inspector->alignH[2], ES_ICON_ALIGN_HORIZONTAL_RIGHT);
		EsButtonOnCommand(inspector->alignH[2], InspectorHAlignRight);
		inspector->alignH[3] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED, 0, "Expand");
		EsButtonOnCommand(inspector->alignH[3], InspectorHAlignExpand);
		inspector->alignH[4] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED, 0, "Shrink");
		EsButtonOnCommand(inspector->alignH[4], InspectorHAlignShrink);
		inspector->alignH[5] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED, 0, "Push");
		EsButtonOnCommand(inspector->alignH[5], InspectorHAlignPush);
		EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, 5, 0);
		EsTextDisplayCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, "Vertical:");
		EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, 5, 0);
		inspector->alignV[0] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED);
		EsButtonSetIcon(inspector->alignV[0], ES_ICON_ALIGN_VERTICAL_TOP);
		EsButtonOnCommand(inspector->alignV[0], InspectorVAlignTop);
		inspector->alignV[1] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED);
		EsButtonSetIcon(inspector->alignV[1], ES_ICON_ALIGN_VERTICAL_CENTER);
		EsButtonOnCommand(inspector->alignV[1], InspectorVAlignCenter);
		inspector->alignV[2] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED);
		EsButtonSetIcon(inspector->alignV[2], ES_ICON_ALIGN_VERTICAL_BOTTOM);
		EsButtonOnCommand(inspector->alignV[2], InspectorVAlignBottom);
		inspector->alignV[3] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED, 0, "Expand");
		EsButtonOnCommand(inspector->alignV[3], InspectorVAlignExpand);
		inspector->alignV[4] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED, 0, "Shrink");
		EsButtonOnCommand(inspector->alignV[4], InspectorVAlignShrink);
		inspector->alignV[5] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED, 0, "Push");
		EsButtonOnCommand(inspector->alignV[5], InspectorVAlignPush);
	}

	{
		EsPanel *toolbar = EsPanelCreate(panel, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL, ES_STYLE_PANEL_TOOLBAR);
		EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, 5, 0);
		EsTextDisplayCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, "Stack:");
		EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, 5, 0);
		inspector->direction[0] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED);
		EsButtonSetIcon(inspector->direction[0], ES_ICON_GO_PREVIOUS);
		EsButtonOnCommand(inspector->direction[0], InspectorDirectionLeft);
		inspector->direction[1] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED);
		EsButtonSetIcon(inspector->direction[1], ES_ICON_GO_NEXT);
		EsButtonOnCommand(inspector->direction[1], InspectorDirectionRight);
		inspector->direction[2] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED);
		EsButtonSetIcon(inspector->direction[2], ES_ICON_GO_UP);
		EsButtonOnCommand(inspector->direction[2], InspectorDirectionUp);
		inspector->direction[3] = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_DISABLED);
		EsButtonSetIcon(inspector->direction[3], ES_ICON_GO_DOWN);
		EsButtonOnCommand(inspector->direction[3], InspectorDirectionDown);
		EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, 25, 0);
		inspector->addChildButton = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_BUTTON_DROPDOWN | ES_ELEMENT_DISABLED | ES_BUTTON_COMPACT, nullptr, "Add child... ");
		EsButtonOnCommand(inspector->addChildButton, InspectorAddElement);
		inspector->addSiblingButton = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_BUTTON_DROPDOWN | ES_ELEMENT_DISABLED | ES_BUTTON_COMPACT, nullptr, "Add sibling... ");
		inspector->addSiblingButton->userData.i = 0x80;
		EsButtonOnCommand(inspector->addSiblingButton, InspectorAddElement);
	}

	{
		EsPanel *toolbar = EsPanelCreate(panel, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL, ES_STYLE_PANEL_TOOLBAR);
		EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, 5, 0);
		EsTextDisplayCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, "Content:");
		inspector->contentTextbox = EsTextboxCreate(toolbar, ES_ELEMENT_DISABLED | ES_TEXTBOX_EDIT_BASED);
		inspector->contentTextbox->messageUser = InspectorContentTextboxCallback;
		EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, 25, 0);
		EsTextDisplayCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, "Event category filter:");
		inspector->textboxCategoryFilter = EsTextboxCreate(toolbar, ES_ELEMENT_DISABLED);
		inspector->textboxCategoryFilter->messageUser = InspectorTextboxCategoryFilterCallback;
	}

	{
		inspector->listEvents = EsListViewCreate(panel, ES_CELL_FILL | ES_LIST_VIEW_CHOICE_SELECT | ES_LIST_VIEW_FIXED_ITEMS, ES_STYLE_LIST_CHOICE_BORDERED);
	}

	InspectorRefreshElementList(inspector);

	APIInstance *instance = (APIInstance *) inspector->instance->_private;
	instance->attachedInspector = inspector;
}
