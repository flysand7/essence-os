// TODO Tabs:
//	- Dragging out of the window.
//	- Dragging onto other windows.
//	- Keyboard shortcuts.
//	- New tab page - search; recent files.
// 	- Right click menu.
// 	- Duplicate tabs.
// 	- If a process exits, its tabs won't close because Desktop has a handle.
// 		- Also clear any OpenDocument currentWriter fields it owns.

// TODO Graphical issues:
// 	- New tab button isn't flush with right border when tab band full.
// 	- Cursor doesn't update after switching embed window owners.
// 	- Closing tabs isn't animating.
// 	- Inactivate windows don't dim outline around tabs.
// 	- Resizing windows doesn't redraw old shadow sometimes.

// TODO Task bar:
// 	- Right click menu.
// 	- Notification area.

// TODO Desktop experience:
// 	- Alt+tab.
// 	- Changing wallpaper.
//
// TODO Global shortcuts:
// 	- Restoring closed tabs.
// 	- Switch to window.
// 	- Print screen.

// TODO Only let File Manager read the file_type sections of the system configuration.
// TODO Restarting Desktop if it crashes.
// TODO Make sure applications can't delete |Fonts: and |Themes:.

#define MSG_SETUP_DESKTOP_UI ((EsMessageType) (ES_MSG_USER_START + 1))

#define APPLICATION_PERMISSION_ALL_FILES                 (1 << 0)
#define APPLICATION_PERMISSION_MANAGE_PROCESSES          (1 << 1)
#define APPLICATION_PERMISSION_POSIX_SUBSYSTEM           (1 << 2)
#define APPLICATION_PERMISSION_RUN_TEMPORARY_APPLICATION (1 << 3)
#define APPLICATION_PERMISSION_SHUTDOWN                  (1 << 4)

#define APPLICATION_ID_DESKTOP_BLANK_TAB (-1)
#define APPLICATION_ID_DESKTOP_SETTINGS  (-2)
#define APPLICATION_ID_DESKTOP_CRASHED   (-3)

#define CRASHED_TAB_FATAL_ERROR        (0)
#define CRASHED_TAB_PROGRAM_NOT_FOUND  (1)
#define CRASHED_TAB_INVALID_EXECUTABLE (2)
#define CRASHED_TAB_NOT_RESPONDING     (3)

#define INSTALLATION_STATE_NONE      (0)
#define INSTALLATION_STATE_INSTALLER (1)

struct ReorderItem : EsElement {
	double sizeProgress, sizeTarget;
	double offsetProgress, offsetTarget;
	bool dragging;
	int dragOffset, dragPosition;
};

struct ReorderList : EsElement {
	int targetWidth, extraWidth;
	Array<ReorderItem *> items; // By index.
};

struct WindowTab : ReorderItem {
	struct ContainerWindow *container;
	struct ApplicationInstance *applicationInstance;
	struct ApplicationInstance *notRespondingInstance;
	EsButton *closeButton;
};

struct WindowTabBand : ReorderList {
	struct ContainerWindow *container;
	bool preventNextTabSizeAnimation;
};

struct TaskBarButton : ReorderItem {
	ContainerWindow *containerWindow;
};

struct TaskList : ReorderList {
};

struct TaskBar : EsElement {
	double enterProgress;
	EsRectangle targetBounds;
	TaskList taskList;
};

struct ContainerWindow {
	WindowTabBand *tabBand;
	TaskBarButton *taskBarButton;
	EsWindow *window;
	WindowTab *active;
};

struct OpenDocument {
	char *path;
	size_t pathBytes;
	char *temporarySavePath;
	size_t temporarySavePathBytes;
	EsHandle readHandle;
	EsObjectID id;
	EsObjectID currentWriter;
};

struct InstalledApplication {
	char *cName;
	char *cExecutable;
	void (*createInstance)(EsMessage *); // For applications provided by Desktop.
	int64_t id;
	uint32_t iconID;
	bool hidden, useSingleProcess, temporary;
	uint64_t permissions;
	size_t openInstanceCount; // Only used if useSingleProcess is true.
	EsHandle singleProcessHandle; 
	bool notified; // Temporary flag.
};

struct CrashedTabInstance : EsInstance {
};

struct BlankTabInstance : EsInstance {
};

struct SettingsInstance : EsInstance {
};

struct ApplicationInstance {
	// User interface.
	WindowTab *tab; // nullptr for notRespondingInstance.
	EsObjectID embeddedWindowID;
	EsHandle embeddedWindowHandle;

	// Currently loaded application.
	InstalledApplication *application;
	EsObjectID documentID, processID;
	EsHandle processHandle;

	// Tab information.
	char title[128];
	size_t titleBytes;
	uint32_t iconID;
};

const EsStyle styleNewTabContent = {
	.metrics = {
		.mask = ES_THEME_METRICS_INSETS | ES_THEME_METRICS_GAP_MAJOR,
		.insets = ES_RECT_4(50, 50, 50, 50),
		.gapMajor = 30,
	},
};

const EsStyle styleButtonGroupContainer = {
	.inherit = ES_STYLE_BUTTON_GROUP_CONTAINER,

	.metrics = {
		.mask = ES_THEME_METRICS_PREFERRED_WIDTH,
		.preferredWidth = 400,
	},
};

struct {
	Array<InstalledApplication *> installedApplications;
	Array<ApplicationInstance *> allApplicationInstances;
	Array<ContainerWindow *> allContainerWindows;
	Array<EsMessageDevice> connectedDevices;

	InstalledApplication *fileManager;

	EsObjectID currentDocumentID;
	HashStore<EsObjectID, OpenDocument> openDocuments;

	TaskBar taskBar;
	EsWindow *wallpaperWindow;

	bool shutdownWindowOpen;
	bool setupDesktopUIComplete;
	int installationState;

	EsHandle nextClipboardFile;
	EsObjectID nextClipboardProcessID;
	EsHandle clipboardFile;
	ClipboardInformation clipboardInformation;
} desktop;

int TaskBarButtonMessage(EsElement *element, EsMessage *message);
ApplicationInstance *ApplicationInstanceCreate(int64_t id, EsApplicationStartupInformation *startupInformation, ContainerWindow *container, bool hidden = false);
void ApplicationInstanceStart(int64_t applicationID, EsApplicationStartupInformation *startupInformation, ApplicationInstance *instance);
void ApplicationInstanceClose(ApplicationInstance *instance);
ApplicationInstance *ApplicationInstanceFindByWindowID(EsObjectID windowID, bool remove = false);
void EmbeddedWindowDestroyed(EsObjectID id);

//////////////////////////////////////////////////////
// Reorder lists:
//////////////////////////////////////////////////////

bool ReorderItemAnimate(ReorderItem *item, uint64_t deltaMs, const char *entranceDuration) {
	item->sizeProgress += (item->sizeTarget - item->sizeProgress) 
		* (1 - EsCRTexp(deltaMs * -3.0f / GetConstantNumber(entranceDuration)));
	item->offsetProgress += (item->offsetTarget - item->offsetProgress) 
		* (1 - EsCRTexp(deltaMs * -3.0f / GetConstantNumber("taskBarButtonMoveDuration")));

	bool complete = true;

	if (EsCRTfabs(item->sizeTarget - item->sizeProgress) < 1.5) {
		item->sizeProgress = item->sizeTarget;
	} else {
		complete = false;
	}

	if (EsCRTfabs(item->offsetTarget - item->offsetProgress) < 1.5) {
		item->offsetProgress = item->offsetTarget;
	} else {
		complete = false;
	}

	EsElementRelayout(item->parent);
	return complete;
}

bool ReorderItemDragged(ReorderItem *item, int mouseX) {
	ReorderList *list = (ReorderList *) item->parent;
	size_t childCount = list->items.Length();

	if (!item->dragging) {
		item->dragOffset = gui.lastClickX - item->offsetX;
		item->BringToFront();
	}

	item->dragPosition = mouseX + item->GetWindowBounds().l - item->dragOffset;

	int draggedIndex = (item->dragPosition + list->targetWidth / 2) / list->targetWidth;
	if (draggedIndex < 0) draggedIndex = 0;
	if (draggedIndex >= (int) childCount) draggedIndex = childCount - 1;
	int currentIndex = -1;

	for (uintptr_t i = 0; i < childCount; i++) {
		if (list->items[i] == item) {
			currentIndex = i;
			break;
		}
	}

	EsAssert(currentIndex != -1);

	bool changed = false;

	if (draggedIndex != currentIndex) {
		list->items.Delete(currentIndex);
		list->items.Insert(item, draggedIndex);

		for (uintptr_t i = 0, x = list->currentStyle->insets.l; i < childCount; i++) {
			ReorderItem *child = (ReorderItem *) list->items[i];

			if ((int) i != draggedIndex) {
				int oldPosition = child->offsetX, newPosition = x + child->offsetProgress;

				if (child->offsetProgress != oldPosition - newPosition) {
					child->offsetProgress = oldPosition - newPosition;
					child->StartAnimating();
				}
			}

			x += child->sizeProgress;
		}

		changed = true;
	}

	item->dragging = true;
	EsElementRelayout(item->parent);

	return changed;
}

void ReorderItemDragComplete(ReorderItem *item) {
	if (!item->dragging) {
		return;
	}

	ReorderList *list = (ReorderList *) item->parent;

	for (uintptr_t i = 0, x = list->currentStyle->insets.l; i < list->items.Length(); i++) {
		if (list->items[i] == item) {
			int oldPosition = item->offsetX, newPosition = x + item->offsetProgress;

			if (item->offsetProgress != oldPosition - newPosition) {
				item->offsetProgress = oldPosition - newPosition;
				item->StartAnimating();
			}
		}

		x += item->sizeTarget;
	}

	item->dragging = false;
}

int ReorderListLayout(ReorderList *list, int additionalRightMargin, bool clampDraggedItem, bool preventTabSizeAnimation) {
	EsRectangle bounds = list->GetBounds();
	bounds.l += list->currentStyle->insets.l;
	bounds.r -= list->currentStyle->insets.r;
	bounds.r -= additionalRightMargin;

	size_t childCount = list->items.Length();

	if (!childCount) {
		return 0;
	}

	int totalWidth = 0;

	for (uintptr_t i = 0; i < childCount; i++) {
		ReorderItem *child = list->items[i];
		totalWidth += child->currentStyle->metrics->maximumWidth + list->currentStyle->metrics->gapMinor;
	}

	bool widthClamped = false;

	if (totalWidth > Width(bounds)) {
		totalWidth = Width(bounds);
		widthClamped = true;
	}

	int targetWidth = totalWidth / childCount;
	int extraWidth = totalWidth % childCount;

	list->targetWidth = targetWidth;
	list->extraWidth = extraWidth;

	for (uintptr_t i = 0; i < childCount; i++) {
		ReorderItem *child = list->items[i];

		int sizeTarget = targetWidth;
		if (extraWidth) sizeTarget++, extraWidth--;

		if (preventTabSizeAnimation) {
			child->sizeTarget = child->sizeProgress = sizeTarget;
		}

		if (child->sizeTarget != sizeTarget) {
			child->sizeTarget = sizeTarget;
			child->StartAnimating();
		}
	}

	int x = bounds.l;

	for (uintptr_t i = 0; i < childCount; i++) {
		ReorderItem *child = list->items[i];
		int width = (i == childCount - 1 && widthClamped) ? (totalWidth - x) : child->sizeProgress;
		int gap = list->currentStyle->metrics->gapMinor;

		if (child->dragging) {
			int p = child->dragPosition;

			if (clampDraggedItem) {
				if (p + width > bounds.r) p = bounds.r - width;
				if (p < bounds.l) p = bounds.l;
			}

			EsElementMove(child, p, 0, width - gap, Height(bounds));
		} else {
			EsElementMove(child, x + child->offsetProgress, 0, width - gap, Height(bounds));
		}

		x += width;
	}

	return x;
}

int ReorderListMessage(EsElement *_list, EsMessage *message) {
	ReorderList *list = (ReorderList *) _list;

	if (message->type == ES_MSG_LAYOUT) {
		ReorderListLayout(list, 0, false, false);
	} else if (message->type == ES_MSG_DESTROY) {
		list->items.Free();
	} else if (message->type == ES_MSG_ADD_CHILD) {
		EsMessage m = { ES_MSG_REORDER_ITEM_TEST };

		if (ES_HANDLED == EsMessageSend(message->child, &m)) {
			list->items.Add((ReorderItem *) message->child);
		}
	} else if (message->type == ES_MSG_REMOVE_CHILD) {
		EsMessage m = { ES_MSG_REORDER_ITEM_TEST };

		if (ES_HANDLED == EsMessageSend(message->child, &m)) {
			list->items.FindAndDelete((ReorderItem *) message->child, true);
		}
	}

	return 0;
}

//////////////////////////////////////////////////////
// Container windows:
//////////////////////////////////////////////////////

void WindowTabClose(WindowTab *tab) {
	if (tab->notRespondingInstance) {
		// The application is not responding, so force quit the process.
		EsProcessTerminate(tab->applicationInstance->processHandle, 1);
	} else {
		ApplicationInstanceClose(tab->applicationInstance);
	}
}

void WindowTabActivate(WindowTab *tab, bool force = false) {
	if (tab->container->active != tab || force) {
		tab->container->active = tab;
		EsElementRelayout(tab->container->tabBand);
		tab->container->taskBarButton->Repaint(true);
		EsHandle handle = tab->notRespondingInstance ? tab->notRespondingInstance->embeddedWindowHandle : tab->applicationInstance->embeddedWindowHandle;
		EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, tab->window->handle, handle, 0, ES_WINDOW_PROPERTY_EMBED);
	}
}

int ContainerWindowMessage(EsElement *element, EsMessage *message) {
	ContainerWindow *container = (ContainerWindow *) element->userData.p;

	if (message->type == ES_MSG_WINDOW_ACTIVATED) {
		container->taskBarButton->customStyleState |= THEME_STATE_SELECTED;
		container->taskBarButton->MaybeRefreshStyle();
	} else if (message->type == ES_MSG_WINDOW_DEACTIVATED) {
		container->taskBarButton->customStyleState &= ~THEME_STATE_SELECTED;
		container->taskBarButton->MaybeRefreshStyle();
	} else if (message->type == ES_MSG_KEY_DOWN) {
		bool ctrlOnly = message->keyboard.modifiers == ES_MODIFIER_CTRL;
		int scancode = message->keyboard.scancode;

		if (((message->keyboard.modifiers & ~ES_MODIFIER_SHIFT) == ES_MODIFIER_CTRL) && message->keyboard.scancode == ES_SCANCODE_TAB) {
			int tab = -1;

			for (uintptr_t i = 0; i < container->tabBand->items.Length(); i++) {
				if (container->tabBand->items[i] == container->active) {
					tab = i;
				}
			}

			EsAssert(tab != -1);
			tab += ((message->keyboard.modifiers & ES_MODIFIER_SHIFT) ? -1 : 1);
			if (tab == -1) tab = container->tabBand->items.Length() - 1;
			if (tab == (int) container->tabBand->items.Length()) tab = 0;
			WindowTabActivate((WindowTab *) container->tabBand->items[tab]);
		} else if (ctrlOnly && scancode == ES_SCANCODE_T) {
			ApplicationInstanceCreate(APPLICATION_ID_DESKTOP_BLANK_TAB, nullptr, container);
		} else if (ctrlOnly && scancode == ES_SCANCODE_W) {
			WindowTabClose(container->active);
		} else if (ctrlOnly && scancode == ES_SCANCODE_N) {
			ApplicationInstanceCreate(APPLICATION_ID_DESKTOP_BLANK_TAB, nullptr, nullptr);
		} else if (message->keyboard.modifiers == ES_MODIFIER_FLAG && scancode == ES_SCANCODE_UP_ARROW) {
			WindowSnap(container->window, false, false, SNAP_EDGE_MAXIMIZE);
		} else if (message->keyboard.modifiers == ES_MODIFIER_FLAG && scancode == ES_SCANCODE_DOWN_ARROW) {
			if (container->window->isMaximised) {
				WindowRestore(container->window);
			} else {
				EsSyscall(ES_SYSCALL_WINDOW_MOVE, container->window->handle, 0, 0, ES_WINDOW_MOVE_HIDDEN);
			}
		} else if (message->keyboard.modifiers == ES_MODIFIER_FLAG && scancode == ES_SCANCODE_LEFT_ARROW) {
			if (container->window->restoreOnNextMove) {
				WindowRestore(container->window);
			} else {
				WindowSnap(container->window, false, false, SNAP_EDGE_LEFT);
			}
		} else if (message->keyboard.modifiers == ES_MODIFIER_FLAG && scancode == ES_SCANCODE_RIGHT_ARROW) {
			if (container->window->restoreOnNextMove) {
				WindowRestore(container->window);
			} else {
				WindowSnap(container->window, false, false, SNAP_EDGE_RIGHT);
			}
		} else {
			for (uintptr_t i = 0; i < 9; i++) {
				if (ctrlOnly && scancode == (int) (ES_SCANCODE_1 + i) && container->tabBand->items.Length() > i) {
					WindowTabActivate((WindowTab *) container->tabBand->items[i]);
				}
			}
		}
	} else if (message->type == ES_MSG_WINDOW_RESIZED) {
		container->tabBand->preventNextTabSizeAnimation = true;
	}

	return 0;
}

int WindowTabMessage(EsElement *element, EsMessage *message) {
	WindowTab *tab = (WindowTab *) element;
	WindowTabBand *band = (WindowTabBand *) tab->parent;
	ApplicationInstance *instance = tab->applicationInstance;

	if (message->type == ES_MSG_DESTROY) {
		if (tab->notRespondingInstance) {
			ApplicationInstanceClose(tab->notRespondingInstance);
			tab->notRespondingInstance = nullptr;
		}
	} else if (message->type == ES_MSG_PRESSED_START) {
		tab->BringToFront();
		WindowTabActivate(tab);
	} else if (message->type == ES_MSG_HIT_TEST) {
		EsRectangle bounds = tab->GetBounds();

		if (message->hitTest.x <= 12) {
			message->hitTest.inside = (bounds.b - message->hitTest.y) * 14 < message->hitTest.x * bounds.b;
		} else if (message->hitTest.x > bounds.r - 12) {
			message->hitTest.inside = (bounds.b - message->hitTest.y) * 14 < (bounds.r - message->hitTest.x) * bounds.b;
		}
	} else if (message->type == ES_MSG_LAYOUT) {
		int closeButtonWidth = tab->closeButton->currentStyle->preferredWidth;
		Rectangle16 insets = tab->currentStyle->metrics->insets;
		EsElementSetHidden(tab->closeButton, tab->currentStyle->gapWrap * 2 + closeButtonWidth >= tab->width);
		EsElementMove(tab->closeButton, tab->width - tab->currentStyle->gapWrap - closeButtonWidth, 
				insets.t, closeButtonWidth, tab->height - insets.t - insets.b);
	} else if (message->type == ES_MSG_PAINT) {
		EsDrawContent(message->painter, element, ES_RECT_2S(message->painter->width, message->painter->height), 
				instance->title, instance->titleBytes, instance->iconID);
	} else if (message->type == ES_MSG_ANIMATE) {
		message->animate.complete = ReorderItemAnimate(tab, message->animate.deltaMs, "windowTabEntranceDuration");
	} else if (message->type == ES_MSG_MOUSE_LEFT_DOWN) {
	} else if (message->type == ES_MSG_MOUSE_LEFT_DRAG) {
		if (band->items.Length() == 1) {
			EsPoint screenPosition = EsMouseGetPosition();
			WindowChangeBounds(RESIZE_MOVE, screenPosition.x, screenPosition.y, &gui.lastClickX, &gui.lastClickY, band->window);
		} else {
			ReorderItemDragged(tab, message->mouseDragged.newPositionX);
		}

		EsElementSetDisabled(band->GetChild(0), true);
	} else if (message->type == ES_MSG_MOUSE_LEFT_UP) {
		ReorderItemDragComplete(tab);
		EsElementSetDisabled(band->GetChild(0), false);
	} else if (message->type == ES_MSG_MOUSE_RIGHT_CLICK) {
		EsMenu *menu = EsMenuCreate(tab, ES_FLAGS_DEFAULT);

		EsMenuAddItem(menu, ES_FLAGS_DEFAULT, INTERFACE_STRING(DesktopCloseTab), [] (EsMenu *, EsGeneric context) {
			WindowTabClose((WindowTab *) context.p);
		}, tab);

		if (EsKeyboardIsShiftHeld()) {
			EsMenuAddSeparator(menu);

			EsMenuAddItem(menu, ES_FLAGS_DEFAULT, INTERFACE_STRING(DesktopInspectUI), [] (EsMenu *, EsGeneric context) {
				WindowTab *tab = (WindowTab *) context.p;
				ApplicationInstance *instance = tab->applicationInstance;
				EsMessage m = { ES_MSG_TAB_INSPECT_UI };
				m.tabOperation.id = instance->embeddedWindowID;
				EsMessagePostRemote(instance->processHandle, &m);
			}, tab);
		}

		EsMenuShow(menu);
	} else if (message->type == ES_MSG_MOUSE_MIDDLE_UP && (element->state & UI_STATE_HOVERED)) {
		WindowTabClose(tab);
	} else if (message->type == ES_MSG_REORDER_ITEM_TEST) {
	} else {
		return 0;
	}

	return ES_HANDLED;
}

WindowTab *WindowTabCreate(ContainerWindow *container, ApplicationInstance *instance) {
	WindowTab *tab = (WindowTab *) EsHeapAllocate(sizeof(WindowTab), true);
	tab->container = container;
	tab->applicationInstance = instance;
	tab->Initialise(container->tabBand, ES_CELL_H_SHRINK | ES_CELL_V_BOTTOM, WindowTabMessage, nullptr);
	tab->cName = "window tab";

	tab->closeButton = EsButtonCreate(tab, ES_FLAGS_DEFAULT, ES_STYLE_WINDOW_TAB_CLOSE_BUTTON);
	tab->closeButton->userData = tab;

	EsButtonOnCommand(tab->closeButton, [] (EsInstance *, EsElement *element, EsCommand *) {
		WindowTabClose((WindowTab *) element->userData.p);
	});
	
	return tab;
}

int WindowTabBandMessage(EsElement *element, EsMessage *message) {
	WindowTabBand *band = (WindowTabBand *) element;

	if (message->type == ES_MSG_LAYOUT) {
		for (uint16_t i = 0; i < band->items.Length(); i++) {
			WindowTab *tab = (WindowTab *) band->items[i];
			tab->SetStyle(tab == tab->container->active ? ES_STYLE_WINDOW_TAB_ACTIVE : ES_STYLE_WINDOW_TAB_INACTIVE);

			if (tab == tab->container->active) {
				tab->BringToFront();
			}
		}

		int x = ReorderListLayout(band, band->GetChild(0)->currentStyle->preferredWidth + 10, true, band->preventNextTabSizeAnimation);
		band->GetChild(0)->InternalMove(band->GetChild(0)->currentStyle->preferredWidth, 
				band->GetChild(0)->currentStyle->preferredHeight, x + 10, 4);
		band->preventNextTabSizeAnimation = false;
	} else if (message->type == ES_MSG_MOUSE_LEFT_DOWN) {
	} else if (message->type == ES_MSG_MOUSE_LEFT_DRAG) {
		EsPoint screenPosition = EsMouseGetPosition();
		WindowChangeBounds(RESIZE_MOVE, screenPosition.x, screenPosition.y, &gui.lastClickX, &gui.lastClickY, band->window);
	} else if (message->type == ES_MSG_MOUSE_LEFT_UP) {
		if (band->window->restoreOnNextMove) {
			band->window->resetPositionOnNextMove = true;
		}
	} else if (message->type == ES_MSG_MOUSE_RIGHT_CLICK) {
		EsMenu *menu = EsMenuCreate(band, ES_MENU_AT_CURSOR);

		EsMenuAddItem(menu, band->window->isMaximised ? ES_ELEMENT_DISABLED : ES_FLAGS_DEFAULT, 
				INTERFACE_STRING(DesktopCenterWindow), [] (EsMenu *, EsGeneric context) {
			WindowTabBand *band = (WindowTabBand *) context.p;
			EsRectangle workArea;
			EsSyscall(ES_SYSCALL_SCREEN_WORK_AREA_GET, 0, (uintptr_t) &workArea, 0, 0);
			EsRectangle newBounds = EsRectangleCenter(workArea, EsWindowGetBounds(band->window));
			newBounds.t -= 8, newBounds.b -= 8; // Because of the shadow, it looks a little better to be slightly above center :)
			EsSyscall(ES_SYSCALL_WINDOW_MOVE, band->window->handle, (uintptr_t) &newBounds, 0, ES_FLAGS_DEFAULT);
		}, band);

		EsMenuShow(menu);
	} else {
		return ReorderListMessage(band, message);
	}

	return ES_HANDLED;
}

ContainerWindow *ContainerWindowCreate() {
	ContainerWindow *container = (ContainerWindow *) EsHeapAllocate(sizeof(ContainerWindow), true);
	container->window = EsWindowCreate(nullptr, ES_WINDOW_CONTAINER);
	desktop.allContainerWindows.Add(container);
	container->window->messageUser = ContainerWindowMessage;
	container->window->userData = container;

	container->tabBand = (WindowTabBand *) EsHeapAllocate(sizeof(WindowTabBand), true);
	container->tabBand->container = container;
	container->tabBand->Initialise(container->window, ES_CELL_FILL, WindowTabBandMessage, ES_STYLE_WINDOW_TAB_BAND);
	container->tabBand->cName = "window tab band";

	EsButton *newTabButton = EsButtonCreate(container->tabBand, ES_FLAGS_DEFAULT, ES_STYLE_WINDOW_TAB_BAND_NEW);
	
	EsButtonOnCommand(newTabButton, [] (EsInstance *, EsElement *element, EsCommand *) {
		ApplicationInstanceCreate(APPLICATION_ID_DESKTOP_BLANK_TAB, nullptr, (ContainerWindow *) element->window->userData.p);
	});

	container->taskBarButton = (TaskBarButton *) EsHeapAllocate(sizeof(TaskBarButton), true);
	container->taskBarButton->customStyleState = THEME_STATE_SELECTED;
	container->taskBarButton->containerWindow = container;
	container->taskBarButton->Initialise(&desktop.taskBar.taskList, ES_CELL_FILL, 
			TaskBarButtonMessage, ES_STYLE_TASK_BAR_BUTTON);
	container->taskBarButton->cName = "task bar button";

	return container;
}

//////////////////////////////////////////////////////
// Task bar and system modals:
//////////////////////////////////////////////////////

int TaskBarButtonMessage(EsElement *element, EsMessage *message) {
	TaskBarButton *button = (TaskBarButton *) element;

	if (message->type == ES_MSG_PAINT) {
		ContainerWindow *containerWindow = button->containerWindow;
		ApplicationInstance *instance = containerWindow->active->applicationInstance;
		EsDrawContent(message->painter, element, ES_RECT_2S(message->painter->width, message->painter->height), 
				instance->title, instance->titleBytes, instance->iconID);
	} else if (message->type == ES_MSG_MOUSE_LEFT_CLICK) {
		if (button->customStyleState & THEME_STATE_SELECTED) {
			EsSyscall(ES_SYSCALL_WINDOW_MOVE, button->containerWindow->window->handle, 0, 0, ES_WINDOW_MOVE_HIDDEN);
		} else {
			EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, button->containerWindow->window->handle, 0, 0, ES_WINDOW_PROPERTY_FOCUSED);
		}
	} else if (message->type == ES_MSG_ANIMATE) {
		message->animate.complete = ReorderItemAnimate(button, message->animate.deltaMs, "taskBarButtonEntranceDuration");
	} else if (message->type == ES_MSG_MOUSE_LEFT_DRAG) {
		ReorderItemDragged(button, message->mouseDragged.newPositionX);
	} else if (message->type == ES_MSG_MOUSE_LEFT_UP) {
		ReorderItemDragComplete(button);
	} else if (message->type == ES_MSG_MOUSE_LEFT_DOWN) {
	} else if (message->type == ES_MSG_REORDER_ITEM_TEST) {
	} else {
		return 0;
	}

	return ES_HANDLED;
}

int TaskBarWindowMessage(EsElement *, EsMessage *message) {
	if (message->type == ES_MSG_ANIMATE) {
		desktop.taskBar.enterProgress += (1 - desktop.taskBar.enterProgress) 
			* (1 - EsCRTexp(message->animate.deltaMs * -3.0 / GetConstantNumber("taskBarEntranceDuration")));

		if (EsCRTfabs(1 - desktop.taskBar.enterProgress) < 0.001) {
			desktop.taskBar.enterProgress = 1;
			message->animate.complete = true;
		} else {
			message->animate.complete = false;
		}

		EsRectangle bounds = desktop.taskBar.targetBounds;
		bounds = Translate(bounds, 0, Height(bounds) * (1 - desktop.taskBar.enterProgress));
		EsSyscall(ES_SYSCALL_WINDOW_MOVE, desktop.taskBar.window->handle, (uintptr_t) &bounds, 0, ES_WINDOW_MOVE_UPDATE_SCREEN);
	}

	return 0;
}

int TaskBarMessage(EsElement *element, EsMessage *message) {
	if (message->type == ES_MSG_LAYOUT) {
		EsRectangle bounds = element->GetBounds();
		element->GetChild(0)->InternalMove(Width(bounds), Height(bounds), 0, 0);
	}

	return 0;
}

void ShutdownModalCreate() {
	if (desktop.shutdownWindowOpen) {
		return;
	}

	desktop.shutdownWindowOpen = true;

	// Setup the window.

	EsWindow *window = EsWindowCreate(nullptr, ES_WINDOW_PLAIN);
	EsRectangle screen;
	EsSyscall(ES_SYSCALL_SCREEN_BOUNDS_GET, 0, (uintptr_t) &screen, 0, 0);
	EsSyscall(ES_SYSCALL_WINDOW_MOVE, window->handle, (uintptr_t) &screen, 0, ES_WINDOW_MOVE_ALWAYS_ON_TOP);
	EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, (uintptr_t) &screen, 0, ES_WINDOW_PROPERTY_BLUR_BOUNDS);
	EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, ES_WINDOW_SOLID_TRUE, 0, ES_WINDOW_PROPERTY_SOLID);
	EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, 0, 0, ES_WINDOW_PROPERTY_FOCUSED);
	EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, BLEND_WINDOW_MATERIAL_LIGHT_BLUR, 0, ES_WINDOW_PROPERTY_MATERIAL);

	// Setup the UI.

	EsPanel *stack = EsPanelCreate(window, ES_CELL_FILL | ES_PANEL_Z_STACK, ES_STYLE_PANEL_NORMAL_WINDOW_ROOT);
	stack->cName = "window stack";
	EsPanelCreate(stack, ES_CELL_FILL, ES_STYLE_PANEL_SHUTDOWN_OVERLAY)->cName = "modal overlay";
	EsPanel *dialog = EsPanelCreate(stack, ES_PANEL_VERTICAL | ES_CELL_CENTER, ES_STYLE_PANEL_DIALOG_ROOT);
	dialog->cName = "dialog";
	EsPanel *heading = EsPanelCreate(dialog, ES_PANEL_HORIZONTAL | ES_CELL_H_FILL, ES_STYLE_DIALOG_HEADING);
	EsIconDisplayCreate(heading, ES_FLAGS_DEFAULT, {}, ES_ICON_SYSTEM_SHUTDOWN);
	EsTextDisplayCreate(heading, ES_CELL_H_FILL | ES_CELL_V_CENTER, ES_STYLE_TEXT_HEADING2, 
			INTERFACE_STRING(DesktopShutdownTitle))->cName = "dialog heading";
	EsTextDisplayCreate(EsPanelCreate(dialog, ES_PANEL_VERTICAL | ES_CELL_H_FILL, ES_STYLE_DIALOG_CONTENT), 
			ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(DesktopConfirmShutdown))->cName = "dialog contents";
	EsPanel *buttonArea = EsPanelCreate(dialog, ES_PANEL_HORIZONTAL | ES_PANEL_REVERSE | ES_CELL_H_FILL, ES_STYLE_DIALOG_BUTTON_AREA);
	EsButton *cancelButton = EsButtonCreate(buttonArea, ES_BUTTON_DEFAULT, 0, INTERFACE_STRING(CommonCancel));
	EsButton *restartButton = EsButtonCreate(buttonArea, ES_FLAGS_DEFAULT, 0, INTERFACE_STRING(DesktopRestartAction));
	EsButton *shutdownButton = EsButtonCreate(buttonArea, ES_FLAGS_DEFAULT, ES_STYLE_PUSH_BUTTON_DANGEROUS, INTERFACE_STRING(DesktopShutdownAction));
	EsElementFocus(cancelButton);

	// Setup command callbacks when the buttons are pressed.

	EsButtonOnCommand(shutdownButton, [] (EsInstance *, EsElement *, EsCommand *) {
		EsSyscall(ES_SYSCALL_SHUTDOWN, SHUTDOWN_ACTION_POWER_OFF, 0, 0, 0);
	});

	EsButtonOnCommand(restartButton, [] (EsInstance *, EsElement *, EsCommand *) {
		EsSyscall(ES_SYSCALL_SHUTDOWN, SHUTDOWN_ACTION_RESTART, 0, 0, 0);
	});

	EsButtonOnCommand(cancelButton, [] (EsInstance *, EsElement *element, EsCommand *) {
		EsElementDestroy(element->window);
		desktop.shutdownWindowOpen = false;
	});
}

//////////////////////////////////////////////////////
// Built-in tabs:
//////////////////////////////////////////////////////

void InstanceForceQuit(EsInstance *, EsElement *element, EsCommand *) {
	EsObjectID windowID = EsSyscall(ES_SYSCALL_WINDOW_GET_ID, element->window->handle, 0, 0, 0);

	for (uintptr_t i = 0; i < desktop.allApplicationInstances.Length(); i++) {
		ApplicationInstance *instance = desktop.allApplicationInstances[i];

		if (instance->tab && instance->tab->notRespondingInstance && instance->tab->notRespondingInstance->embeddedWindowID == windowID) {
			EsProcessTerminate(instance->processHandle, 1);
			break;
		}
	}
}

void InstanceCrashedTabCreate(EsMessage *message) {
	CrashedTabInstance *instance = (CrashedTabInstance *) _EsInstanceCreate(sizeof(CrashedTabInstance), message, nullptr);
	int32_t reason = ((APIInstance *) instance->_private)->startupInformation->data;
	instance->window->toolbarFillMode = true;
	if (reason != CRASHED_TAB_NOT_RESPONDING) EsWindowSetIcon(instance->window, ES_ICON_DIALOG_ERROR);
	EsElement *toolbar = EsWindowGetToolbar(instance->window);
	EsPanel *panel = EsPanelCreate(toolbar, ES_CELL_V_CENTER | ES_CELL_V_PUSH | ES_CELL_H_SHRINK | ES_CELL_H_PUSH | ES_PANEL_VERTICAL, ES_STYLE_PANEL_CRASH_INFO);

	if (reason == CRASHED_TAB_FATAL_ERROR) {
		EsTextDisplayCreate(panel, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(DesktopCrashedApplication));
	} else if (reason == CRASHED_TAB_PROGRAM_NOT_FOUND) {
		EsTextDisplayCreate(panel, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(DesktopNoSuchApplication));
		EsWindowSetTitle(instance->window, INTERFACE_STRING(CommonErrorTitle));
	} else if (reason == CRASHED_TAB_INVALID_EXECUTABLE) {
		EsTextDisplayCreate(panel, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(DesktopApplicationStartupError));
		EsWindowSetTitle(instance->window, INTERFACE_STRING(CommonErrorTitle));
	} else if (reason == CRASHED_TAB_NOT_RESPONDING) {
		EsTextDisplayCreate(panel, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(DesktopNotResponding));
		EsButton *button = EsButtonCreate(panel, ES_CELL_H_RIGHT, ES_STYLE_PUSH_BUTTON_DANGEROUS, INTERFACE_STRING(DesktopForceQuit));
		EsButtonOnCommand(button, InstanceForceQuit);
	}
}

void InstanceBlankTabCreate(EsMessage *message) {
	EsInstance *instance = _EsInstanceCreate(sizeof(BlankTabInstance), message, nullptr);
	EsWindowSetTitle(instance->window, INTERFACE_STRING(DesktopNewTabTitle));
	EsPanel *windowBackground = EsPanelCreate(instance->window, ES_CELL_FILL, ES_STYLE_PANEL_WINDOW_BACKGROUND);
	EsPanel *content = EsPanelCreate(windowBackground, ES_CELL_FILL | ES_PANEL_V_SCROLL_AUTO, &styleNewTabContent);
	EsPanel *buttonGroup;

	// Installed applications list.

	buttonGroup = EsPanelCreate(content, ES_PANEL_VERTICAL | ES_CELL_H_SHRINK, &styleButtonGroupContainer);
	buttonGroup->separatorStylePart = ES_STYLE_BUTTON_GROUP_SEPARATOR;
	buttonGroup->separatorFlags = ES_CELL_H_FILL;

	for (uintptr_t i = 0; i < desktop.installedApplications.Length(); i++) {
		InstalledApplication *application = desktop.installedApplications[i];
		if (application->hidden) continue;

		EsButton *button = EsButtonCreate(buttonGroup, ES_CELL_H_FILL | ES_BUTTON_NOT_FOCUSABLE, ES_STYLE_BUTTON_GROUP_ITEM, application->cName);
		EsButtonSetIcon(button, (EsStandardIcon) application->iconID ?: ES_ICON_APPLICATION_DEFAULT_ICON);
		button->userData = application;

		EsButtonOnCommand(button, [] (EsInstance *, EsElement *element, EsCommand *) {
			EsObjectID tabID = EsSyscall(ES_SYSCALL_WINDOW_GET_ID, element->window->handle, 0, 0, 0);
			ApplicationInstance *instance = ApplicationInstanceFindByWindowID(tabID);
			ApplicationInstanceStart(((InstalledApplication *) element->userData.p)->id, nullptr, instance);
			WindowTabActivate(instance->tab, true);
			EsInstanceDestroy(element->instance);
		});
	}
}

void InstanceSettingsCreate(EsMessage *message) {
	// TODO.

	EsInstance *instance = _EsInstanceCreate(sizeof(SettingsInstance), message, nullptr);
	EsWindowSetTitle(instance->window, INTERFACE_STRING(DesktopSettingsTitle));
	EsWindowSetIcon(instance->window, ES_ICON_PREFERENCES_DESKTOP);
	EsPanel *windowBackground = EsPanelCreate(instance->window, ES_CELL_FILL, ES_STYLE_PANEL_WINDOW_BACKGROUND);
	EsPanel *content = EsPanelCreate(windowBackground, ES_CELL_FILL | ES_PANEL_V_SCROLL_AUTO, &styleNewTabContent);
	EsPanel *buttonGroup;

	buttonGroup = EsPanelCreate(content, ES_PANEL_VERTICAL | ES_CELL_H_SHRINK, &styleButtonGroupContainer);
	buttonGroup->separatorStylePart = ES_STYLE_BUTTON_GROUP_SEPARATOR;
	buttonGroup->separatorFlags = ES_CELL_H_FILL;

	EsButton *button = EsButtonCreate(buttonGroup, ES_CELL_H_FILL | ES_BUTTON_NOT_FOCUSABLE, ES_STYLE_BUTTON_GROUP_ITEM, "Keyboard");
	EsButtonSetIcon(button, ES_ICON_PREFERENCES_DESKTOP_KEYBOARD);
}

//////////////////////////////////////////////////////
// Application management:
//////////////////////////////////////////////////////

ApplicationInstance *ApplicationInstanceFindByWindowID(EsObjectID windowID, bool remove) {
	for (uintptr_t i = 0; i < desktop.allApplicationInstances.Length(); i++) {
		ApplicationInstance *instance = desktop.allApplicationInstances[i];

		if (instance->embeddedWindowID == windowID) {
			if (remove) {
				desktop.allApplicationInstances.Delete(i);
			}

			return instance;
		}
	}

	return nullptr;
}

void ApplicationInstanceClose(ApplicationInstance *instance) {
	// TODO Force closing not responding instances.
	EsMessage m = { ES_MSG_TAB_CLOSE_REQUEST };
	m.tabOperation.id = instance->embeddedWindowID;
	EsMessagePostRemote(instance->processHandle, &m);
}

void ApplicationInstanceStart(int64_t applicationID, EsApplicationStartupInformation *startupInformation, ApplicationInstance *instance) {
	EsApplicationStartupInformation _startupInformation = {};
	
	if (!startupInformation) {
		startupInformation = &_startupInformation;
	}

	if (instance->tab && instance->tab->notRespondingInstance) {
		ApplicationInstanceClose(instance->tab->notRespondingInstance);
		instance->tab->notRespondingInstance = nullptr;
	}

	if (instance->processHandle) {
		EsHandleClose(instance->processHandle);
		instance->processID = 0;
		instance->processHandle = ES_INVALID_HANDLE;
	}

	InstalledApplication *application = nullptr;

	for (uintptr_t i = 0; i < desktop.installedApplications.Length(); i++) {
		if (desktop.installedApplications[i]->id == applicationID) {
			application = desktop.installedApplications[i];
		}
	}

	if (!application) {
		EsApplicationStartupInformation s = {};
		s.data = CRASHED_TAB_PROGRAM_NOT_FOUND;
		ApplicationInstanceStart(APPLICATION_ID_DESKTOP_CRASHED, &s, instance);
		return;
	}

	instance->application = application;

	if (application->useSingleProcess && application->singleProcessHandle) {
		EsProcessState state;
		EsProcessGetState(application->singleProcessHandle, &state); 

		if (state.flags & (ES_PROCESS_STATE_ALL_THREADS_TERMINATED | ES_PROCESS_STATE_TERMINATING | ES_PROCESS_STATE_CRASHED)) {
			EsHandleClose(application->singleProcessHandle);
			application->singleProcessHandle = ES_INVALID_HANDLE;
		}
	}

	EsHandle process = application->singleProcessHandle;

	if (application->createInstance) {
		process = ES_CURRENT_PROCESS;
	} else if (!application->useSingleProcess || process == ES_INVALID_HANDLE) {
		EsProcessInformation information;
		EsProcessCreationArguments arguments = {};

		_EsNodeInformation executableNode;
		EsError error = NodeOpen(application->cExecutable, EsCStringLength(application->cExecutable), 
				ES_FILE_READ | ES_NODE_FAIL_IF_NOT_FOUND, &executableNode);

		if (ES_CHECK_ERROR(error)) {
			EsApplicationStartupInformation s = {};
			s.data = CRASHED_TAB_INVALID_EXECUTABLE;
			ApplicationInstanceStart(APPLICATION_ID_DESKTOP_CRASHED, &s, instance);
			return;
		}

		arguments.executable = executableNode.handle;
		arguments.permissions = ES_PERMISSION_WINDOW_MANAGER;

		Array<EsMountPoint> initialMountPoints = {};
		_EsNodeInformation settingsNode = {};

		if (application->permissions & APPLICATION_PERMISSION_MANAGE_PROCESSES) {
			arguments.permissions |= ES_PERMISSION_TAKE_SYSTEM_SNAPSHOT;
			arguments.permissions |= ES_PERMISSION_PROCESS_CREATE;
			arguments.permissions |= ES_PERMISSION_PROCESS_OPEN;
		}

		if (application->permissions & APPLICATION_PERMISSION_POSIX_SUBSYSTEM) {
			arguments.permissions |= ES_PERMISSION_PROCESS_OPEN;
			arguments.permissions |= ES_PERMISSION_POSIX_SUBSYSTEM;

			MountPoint root = *NodeFindMountPoint(EsLiteral("0:"));
			root.write = true;
			root.prefixBytes = EsStringFormat(root.prefix, sizeof(root.prefix), "|POSIX:");
			initialMountPoints.Add(root);
		}

		if (application->permissions & APPLICATION_PERMISSION_ALL_FILES) {
			for (uintptr_t i = 0; i < api.mountPoints.Length(); i++) {
				initialMountPoints.Add(api.mountPoints[i]);
				initialMountPoints[i].write = true;
			}

			arguments.permissions |= ES_PERMISSION_GET_VOLUME_INFORMATION;
		} else {
			initialMountPoints.Add(*NodeFindMountPoint(EsLiteral("|Themes:")));
			initialMountPoints.Add(*NodeFindMountPoint(EsLiteral("|Fonts:")));
		}

		{
			size_t settingsPathBytes, settingsFolderBytes;
			char *settingsFolder = EsSystemConfigurationReadString(EsLiteral("general"), EsLiteral("settings_path"), &settingsFolderBytes);
			char *settingsPath = EsStringAllocateAndFormat(&settingsPathBytes, "%s/%z", settingsFolderBytes, settingsFolder, application->cName);
			error = NodeOpen(settingsPath, settingsPathBytes, ES_NODE_DIRECTORY | ES_NODE_CREATE_DIRECTORIES | _ES_NODE_DIRECTORY_WRITE, &settingsNode);
			EsHeapFree(settingsPath);
			EsHeapFree(settingsFolder);

			if (error == ES_SUCCESS) {
				EsMountPoint settings = {};
				settings.prefixBytes = EsStringFormat(settings.prefix, sizeof(settings.prefix), "|Settings:");
				settings.base = settingsNode.handle;
				settings.write = true;
				initialMountPoints.Add(settings);
			} else {
				settingsNode.handle = ES_INVALID_HANDLE;
			}
		}

		arguments.initialMountPoints = initialMountPoints.array;
		arguments.initialMountPointCount = initialMountPoints.Length();

		error = EsProcessCreate(&arguments, &information); 
		EsHandleClose(arguments.executable);

		initialMountPoints.Free();
		if (settingsNode.handle) EsHandleClose(settingsNode.handle);

		if (!ES_CHECK_ERROR(error)) {
			process = information.handle;
			EsHandleClose(information.mainThread.handle);
		} else {
			EsApplicationStartupInformation s = {};
			s.data = CRASHED_TAB_INVALID_EXECUTABLE;
			ApplicationInstanceStart(APPLICATION_ID_DESKTOP_CRASHED, &s, instance);
			return;
		}
	}

	if (application->useSingleProcess) {
		application->singleProcessHandle = process;
	}

	instance->processID = EsProcessGetID(process);
	instance->processHandle = EsSyscall(ES_SYSCALL_PROCESS_SHARE, process, ES_CURRENT_PROCESS, 0, 0);

	EsMessage m = { ES_MSG_INSTANCE_CREATE };

	if (~startupInformation->flags & ES_APPLICATION_STARTUP_MANUAL_PATH) {
		// Only tell the application the name of the file.

		for (uintptr_t i = 0; i < (size_t) startupInformation->filePathBytes; i++) {
			if (startupInformation->filePath[i] == '/') {
				startupInformation->filePath += i + 1;
				startupInformation->filePathBytes -= i + 1;
				i = 0;
			}
		}
	}

	// Share handles to the file and the startup information buffer.

	if (startupInformation->readHandle) {
		startupInformation->readHandle = EsSyscall(ES_SYSCALL_NODE_SHARE, startupInformation->readHandle, process, 0, 0);
	}

	if (!application->useSingleProcess && !application->createInstance) {
		startupInformation->flags |= ES_APPLICATION_STARTUP_SINGLE_INSTANCE_IN_PROCESS;
	}

	uint8_t *createInstanceDataBuffer = ApplicationStartupInformationToBuffer(startupInformation, &m.createInstance.dataBytes);
	m.createInstance.data = EsConstantBufferCreate(createInstanceDataBuffer, m.createInstance.dataBytes, process);
	EsHeapFree(createInstanceDataBuffer);

	EsHandle handle = EsSyscall(ES_SYSCALL_WINDOW_CREATE, ES_WINDOW_NORMAL, 0, 0, 0);
	instance->embeddedWindowHandle = handle;
	EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, handle, 0xFF000000 | GetConstantNumber("windowFillColor"), 0, ES_WINDOW_PROPERTY_RESIZE_CLEAR_COLOR);
	instance->embeddedWindowID = EsSyscall(ES_SYSCALL_WINDOW_GET_ID, handle, 0, 0, 0);
	m.createInstance.window = EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, handle, process, 0, ES_WINDOW_PROPERTY_EMBED_OWNER);

	if (application->createInstance) {
		application->createInstance(&m);
	} else {
		EsMessagePostRemote(process, &m);

		if (!application->useSingleProcess) {
			EsHandleClose(process);
		} else {
			application->openInstanceCount++;
		}
	}
}

ApplicationInstance *ApplicationInstanceCreate(int64_t id, EsApplicationStartupInformation *startupInformation, ContainerWindow *container, bool hidden) {
	ApplicationInstance *instance = (ApplicationInstance *) EsHeapAllocate(sizeof(ApplicationInstance), true);
	WindowTab *tab = !hidden ? WindowTabCreate(container ?: ContainerWindowCreate(), instance) : nullptr;
	instance->title[0] = ' ';
	instance->titleBytes = 1;
	instance->tab = tab;
	desktop.allApplicationInstances.Add(instance);
	ApplicationInstanceStart(id, startupInformation, instance);
	if (!hidden) WindowTabActivate(tab);
	return instance;
}

void ApplicationTemporaryDestroy(InstalledApplication *application) {
	if (!application->temporary) return;
	EsAssert(!application->singleProcessHandle);

	for (uintptr_t i = 0; i < desktop.installedApplications.Length(); i++) {
		if (desktop.installedApplications[i] == application) {
			desktop.installedApplications.Delete(i);
			EsHeapFree(application->cName);
			EsHeapFree(application->cExecutable);
			EsHeapFree(application);
			// TODO Delete the settings folder.
			return;
		}
	}

	EsAssert(false);
}

void ApplicationInstanceCrashed(EsMessage *message) {
	EsHandle processHandle = EsProcessOpen(message->crash.pid);
	EsAssert(processHandle); // Since the process is paused, it cannot be removed.

	EsProcessState state;
	EsProcessGetState(processHandle, &state); 
	const char *fatalErrorString = state.crashReason.errorCode >= ES_FATAL_ERROR_COUNT ? "[unknown]" 
		: EnumLookupNameFromValue(enumStrings_EsFatalError, state.crashReason.errorCode);
	const char *systemCallString = state.crashReason.duringSystemCall == -1 ? "[none]" 
		: EnumLookupNameFromValue(enumStrings_EsSyscallType, state.crashReason.duringSystemCall);
	EsPrint("Process %d has crashed with error %z, during system call %z.\n", state.id, fatalErrorString, systemCallString);

	for (uintptr_t i = 0; i < desktop.allApplicationInstances.Length(); i++) {
		ApplicationInstance *instance = desktop.allApplicationInstances[i];

		if (instance->processID == message->crash.pid) {
			ApplicationInstanceStart(APPLICATION_ID_DESKTOP_CRASHED, nullptr, instance);
			WindowTabActivate(instance->tab, true);
		}
	}

	for (uintptr_t i = 0; i < desktop.installedApplications.Length(); i++) {
		if (desktop.installedApplications[i]->useSingleProcess && desktop.installedApplications[i]->singleProcessHandle
				&& EsProcessGetID(desktop.installedApplications[i]->singleProcessHandle) == message->crash.pid) {
			EsHandleClose(desktop.installedApplications[i]->singleProcessHandle);
			desktop.installedApplications[i]->singleProcessHandle = ES_INVALID_HANDLE;
			desktop.installedApplications[i]->openInstanceCount = 0;
			ApplicationTemporaryDestroy(desktop.installedApplications[i]);
			break;
		}
	}

	EsProcessTerminate(processHandle, 1); 
	EsHandleClose(processHandle);
}

void ApplicationProcessTerminated(EsObjectID pid) {
	for (uintptr_t i = 0; i < desktop.allApplicationInstances.Length(); i++) {
		ApplicationInstance *instance = desktop.allApplicationInstances[i];

		if (instance->processID != pid) {
			continue;
		}

		EmbeddedWindowDestroyed(instance->embeddedWindowID);
	}

	for (uintptr_t i = 0; i < desktop.installedApplications.Length(); i++) {
		if (desktop.installedApplications[i]->useSingleProcess && desktop.installedApplications[i]->singleProcessHandle
				&& EsProcessGetID(desktop.installedApplications[i]->singleProcessHandle) == pid) {
			EsHandleClose(desktop.installedApplications[i]->singleProcessHandle);
			desktop.installedApplications[i]->singleProcessHandle = ES_INVALID_HANDLE;
			desktop.installedApplications[i]->openInstanceCount = 0;
			ApplicationTemporaryDestroy(desktop.installedApplications[i]);
			break;
		}
	}
}

//////////////////////////////////////////////////////
// Document management:
//////////////////////////////////////////////////////

void OpenDocumentWithApplication(EsApplicationStartupInformation *startupInformation) {
	bool foundDocument = false;
	EsObjectID documentID;

	for (uintptr_t i = 0; i < desktop.openDocuments.Count(); i++) {
		OpenDocument *document = &desktop.openDocuments[i];

		if (document->pathBytes == (size_t) startupInformation->filePathBytes
				&& 0 == EsMemoryCompare(document->path, startupInformation->filePath, document->pathBytes)) {
			foundDocument = true;
			startupInformation->readHandle = document->readHandle;
			documentID = document->id;
			break;
		}
	}

	if (!foundDocument) {
		EsFileInformation file = EsFileOpen(startupInformation->filePath, startupInformation->filePathBytes, 
				ES_FILE_READ_SHARED | ES_NODE_FAIL_IF_NOT_FOUND);

		if (file.error != ES_SUCCESS) {
			// TODO Report error?
			return;
		}

		OpenDocument document = {};
		document.path = (char *) EsHeapAllocate(startupInformation->filePathBytes, false);
		document.pathBytes = startupInformation->filePathBytes;
		document.readHandle = file.handle;
		document.id = ++desktop.currentDocumentID;
		documentID = document.id;
		EsMemoryCopy(document.path, startupInformation->filePath, startupInformation->filePathBytes);
		*desktop.openDocuments.Put(&document.id) = document;

		startupInformation->readHandle = document.readHandle;
	}

	ApplicationInstance *instance = ApplicationInstanceCreate(startupInformation->id, startupInformation, nullptr);

	if (instance) {
		instance->documentID = documentID;
	}
}

EsError TemporaryFileCreate(EsHandle *handle, char **path, size_t *pathBytes) {
	char temporaryFileName[32];

	for (uintptr_t i = 0; i < sizeof(temporaryFileName); i++) {
		temporaryFileName[i] = (EsRandomU8() % 26) + 'a';
	}

	size_t temporaryFolderBytes;
	char *temporaryFolder = EsSystemConfigurationReadString(EsLiteral("general"), EsLiteral("temporary_path"), &temporaryFolderBytes);
	char *temporaryFilePath = (char *) EsHeapAllocate(temporaryFolderBytes + 1 + sizeof(temporaryFileName), false);
	size_t temporaryFilePathBytes = EsStringFormat(temporaryFilePath, ES_STRING_FORMAT_ENOUGH_SPACE, "%s/%s", 
			temporaryFolderBytes, temporaryFolder, sizeof(temporaryFileName), temporaryFileName);

	EsFileInformation file = EsFileOpen(temporaryFilePath, temporaryFilePathBytes, 
			ES_FILE_WRITE_EXCLUSIVE | ES_NODE_FAIL_IF_FOUND | ES_NODE_CREATE_DIRECTORIES);

	EsHeapFree(temporaryFolder);

	if (file.error != ES_SUCCESS) {
		EsHeapFree(temporaryFilePath);
	} else {
		*path = temporaryFilePath;
		*pathBytes = temporaryFilePathBytes;
		*handle = file.handle;
	}

	return file.error;
}

void ApplicationInstanceRequestSave(ApplicationInstance *instance, const char *newName, size_t newNameBytes) {
	if (!instance->processHandle) return;
	
	EsMessage m = {};
	m.type = ES_MSG_INSTANCE_SAVE_RESPONSE;
	m.tabOperation.id = instance->embeddedWindowID;

	if (!instance->documentID) {
		size_t folderBytes;
		char *folder = EsSystemConfigurationReadString(EsLiteral("general"), EsLiteral("default_user_documents_path"), &folderBytes);
		char *name = (char *) EsHeapAllocate(folderBytes + newNameBytes + 32, false);
		EsMemoryCopy(name, folder, folderBytes);
		EsMemoryCopy(name + folderBytes, newName, newNameBytes);
		EsHeapFree(folder);
		size_t nameBytes = EsPathFindUniqueName(name, folderBytes + newNameBytes, folderBytes + newNameBytes + 32);

		if (!nameBytes) {
			EsHeapFree(name);
			m.tabOperation.error = ES_ERROR_FILE_ALREADY_EXISTS;
			EsMessagePostRemote(instance->processHandle, &m);
			return;
		}

		EsFileInformation file = EsFileOpen(name, nameBytes, ES_FILE_READ_SHARED | ES_NODE_FAIL_IF_FOUND);

		if (file.error != ES_SUCCESS) {
			EsHeapFree(name);
			m.tabOperation.error = file.error;
			EsMessagePostRemote(instance->processHandle, &m);
			return;
		}

		OpenDocument document = {};
		document.path = name;
		document.pathBytes = nameBytes;
		document.readHandle = file.handle;
		document.id = ++desktop.currentDocumentID;
		*desktop.openDocuments.Put(&document.id) = document;

		instance->documentID = document.id;

		{
			// Tell the instance the chosen name for the document.

			uintptr_t nameOffset = 0;

			for (uintptr_t i = 0; i < nameBytes; i++) {
				if (name[i] == '/') {
					nameOffset = i + 1;
				}
			}

			EsMessage m = { ES_MSG_INSTANCE_DOCUMENT_RENAMED };
			m.tabOperation.id = instance->embeddedWindowID;
			m.tabOperation.handle = EsConstantBufferCreate(name + nameOffset, nameBytes - nameOffset, instance->processHandle); 
			m.tabOperation.bytes = nameBytes - nameOffset;
			EsMessagePostRemote(instance->processHandle, &m);
		}
	}

	OpenDocument *document = desktop.openDocuments.Get(&instance->documentID);

	if (!document) {
		return;
	}

	if (document->currentWriter) {
		m.tabOperation.error = ES_ERROR_FILE_CANNOT_GET_EXCLUSIVE_USE;
	} else {
		EsHandle fileHandle;
		m.tabOperation.error = TemporaryFileCreate(&fileHandle, &document->temporarySavePath, &document->temporarySavePathBytes);

		if (m.tabOperation.error == ES_SUCCESS) {
			document->currentWriter = instance->embeddedWindowID;
			m.tabOperation.handle = EsSyscall(ES_SYSCALL_NODE_SHARE, fileHandle, instance->processHandle, 0, 0);
			EsHandleClose(fileHandle);
		}
	}

	EsMessagePostRemote(instance->processHandle, &m);
}

void InstanceAnnouncePathMoved(ApplicationInstance *fromInstance, const uint8_t *buffer, size_t embedWindowMessageBytes) {
	// TODO Update the location of installed applications and other things in the configuration.

	uintptr_t oldPathBytes, newPathBytes;
	EsMemoryCopy(&oldPathBytes, buffer + 1, sizeof(uintptr_t));
	EsMemoryCopy(&newPathBytes, buffer + 1 + sizeof(uintptr_t), sizeof(uintptr_t));

	if (oldPathBytes >= 0x4000 || newPathBytes >= 0x4000
			|| oldPathBytes + newPathBytes + sizeof(uintptr_t) * 2 + 1 != embedWindowMessageBytes) {
		return;
	}

	const char *oldPath = (const char *) buffer + 1 + sizeof(uintptr_t) * 2;
	const char *newPath = (const char *) buffer + 1 + sizeof(uintptr_t) * 2 + oldPathBytes;

	EsObjectID documentID = 0;

	for (uintptr_t i = 0; i < desktop.openDocuments.Count(); i++) {
		OpenDocument *document = &desktop.openDocuments[i];

		if (document->pathBytes >= oldPathBytes
				&& 0 == EsMemoryCompare(document->path, oldPath, oldPathBytes)
				&& (oldPathBytes == document->pathBytes || document->path[oldPathBytes] == '/')) {
			if (document->pathBytes == oldPathBytes) documentID = document->id;
			char *newDocumentPath = (char *) EsHeapAllocate(document->pathBytes - oldPathBytes + newPathBytes, false);
			EsMemoryCopy(newDocumentPath, newPath, newPathBytes);
			EsMemoryCopy(newDocumentPath + newPathBytes, document->path + oldPathBytes, document->pathBytes - oldPathBytes);
			document->pathBytes += newPathBytes - oldPathBytes;
			EsHeapFree(document->path);
			document->path = newDocumentPath;
		}
	}

	if (!documentID) {
		return;
	}

	uintptr_t newNameOffset = 0;

	for (uintptr_t i = 0; i < newPathBytes; i++) {
		if (newPath[i] == '/') {
			newNameOffset = i + 1;
		}
	}

	for (uintptr_t i = 0; i < desktop.allApplicationInstances.Length(); i++) {
		ApplicationInstance *instance = desktop.allApplicationInstances[i];

		if (instance->documentID != documentID) continue;
		if (instance->application == fromInstance->application) continue;
		if (!instance->processHandle) continue;

		EsMessage m = { ES_MSG_INSTANCE_DOCUMENT_RENAMED };
		m.tabOperation.id = instance->embeddedWindowID;
		m.tabOperation.handle = EsConstantBufferCreate(newPath + newNameOffset, newPathBytes - newNameOffset, instance->processHandle); 
		m.tabOperation.bytes = newPathBytes - newNameOffset;
		EsMessagePostRemote(instance->processHandle, &m);
	}
}

void ApplicationInstanceCompleteSave(ApplicationInstance *fromInstance) {
	OpenDocument *document = desktop.openDocuments.Get(&fromInstance->documentID);

	if (!document || fromInstance->embeddedWindowID != document->currentWriter) {
		return;
	}

	// Move the temporary file to its target destination.
	// TODO Handling errors.
	// TODO What should happen if the old file is deleted, but the new file isn't moved?

	EsPathDelete(document->path, document->pathBytes);
	EsPathMove(document->temporarySavePath, document->temporarySavePathBytes, document->path, document->pathBytes);

	// Re-open the read handle.

	EsFileInformation file = EsFileOpen(document->path, document->pathBytes, ES_FILE_READ_SHARED | ES_NODE_FAIL_IF_NOT_FOUND);

	if (file.error != ES_SUCCESS) {
		// TODO What now?
	} else {
		EsHandleClose(document->readHandle);
		document->readHandle = file.handle;
	}

	document->currentWriter = 0;

	if (desktop.fileManager->singleProcessHandle) {
		EsMessage m = {};
		m.type = ES_MSG_FILE_MANAGER_FILE_MODIFIED;
		m.user.context1 = EsConstantBufferCreate(document->path, document->pathBytes, desktop.fileManager->singleProcessHandle); 
		m.user.context2 = document->pathBytes;
		EsMessagePostRemote(desktop.fileManager->singleProcessHandle, &m);
	}

	for (uintptr_t i = 0; i < desktop.allApplicationInstances.Length(); i++) {
		ApplicationInstance *instance = desktop.allApplicationInstances[i];

		if (instance->documentID != document->id) continue;
		if (!instance->processHandle) continue;

		EsMessage m = { ES_MSG_INSTANCE_DOCUMENT_UPDATED };
		m.tabOperation.isSource = instance == fromInstance;
		m.tabOperation.id = instance->embeddedWindowID;
		m.tabOperation.handle = EsSyscall(ES_SYSCALL_NODE_SHARE, document->readHandle, instance->processHandle, 0, 0);
		EsMessagePostRemote(instance->processHandle, &m);
	}
}

//////////////////////////////////////////////////////
// Configuration file management:
//////////////////////////////////////////////////////

void ConfigurationLoad() {
	// Add applications provided by Desktop.

	{
		InstalledApplication *application = (InstalledApplication *) EsHeapAllocate(sizeof(InstalledApplication), true);
		application->id = APPLICATION_ID_DESKTOP_BLANK_TAB;
		application->hidden = true;
		application->createInstance = InstanceBlankTabCreate;
		desktop.installedApplications.Add(application);
	}

	{
		InstalledApplication *application = (InstalledApplication *) EsHeapAllocate(sizeof(InstalledApplication), true);
		application->cName = (char *) interfaceString_DesktopSettingsApplication;
		application->id = APPLICATION_ID_DESKTOP_SETTINGS;
		application->iconID = ES_ICON_PREFERENCES_DESKTOP;
		application->createInstance = InstanceSettingsCreate;
		desktop.installedApplications.Add(application);
	}

	{
		InstalledApplication *application = (InstalledApplication *) EsHeapAllocate(sizeof(InstalledApplication), true);
		application->id = APPLICATION_ID_DESKTOP_CRASHED;
		application->hidden = true;
		application->createInstance = InstanceCrashedTabCreate;
		desktop.installedApplications.Add(application);
	}

	for (uintptr_t i = 0; i < api.systemConfigurationGroups.Length(); i++) {
		// Load information about installed applications.

		EsSystemConfigurationGroup *group = &api.systemConfigurationGroups[i];

		if (0 != EsStringCompareRaw(group->sectionClass, group->sectionClassBytes, EsLiteral("application"))) {
			continue;
		}

		InstalledApplication *application = (InstalledApplication *) EsHeapAllocate(sizeof(InstalledApplication), true);

		application->cName = EsSystemConfigurationGroupReadString(group, EsLiteral("name"));
		size_t executableBytes = 0;
		char *icon = EsSystemConfigurationGroupReadString(group, EsLiteral("icon"));
		application->iconID = EsIconIDFromString(icon);
		EsHeapFree(icon);
		char *executable = EsSystemConfigurationGroupReadString(group, EsLiteral("executable"), &executableBytes);
		application->cExecutable = (char *) EsHeapAllocate(executableBytes + 1, false);
		EsMemoryCopy(application->cExecutable, executable, executableBytes);
		application->cExecutable[executableBytes] = 0;
		EsHeapFree(executable);
		application->useSingleProcess = EsSystemConfigurationGroupReadInteger(group, EsLiteral("use_single_process"), true);
		application->hidden = EsSystemConfigurationGroupReadInteger(group, EsLiteral("hidden"), false);
		application->id = EsIntegerParse(group->section, group->sectionBytes);

#define READ_PERMISSION(x, y) if (EsSystemConfigurationGroupReadInteger(group, EsLiteral(x), 0)) application->permissions |= y
		READ_PERMISSION("permission_all_files", APPLICATION_PERMISSION_ALL_FILES);
		READ_PERMISSION("permission_manage_processes", APPLICATION_PERMISSION_MANAGE_PROCESSES);
		READ_PERMISSION("permission_posix_subsystem", APPLICATION_PERMISSION_POSIX_SUBSYSTEM);
		READ_PERMISSION("permission_run_temporary_application", APPLICATION_PERMISSION_RUN_TEMPORARY_APPLICATION);
		READ_PERMISSION("permission_shutdown", APPLICATION_PERMISSION_SHUTDOWN);

		desktop.installedApplications.Add(application);

		if (EsSystemConfigurationGroupReadInteger(group, EsLiteral("is_file_manager"))) {
			desktop.fileManager = application;
		}
	}

	EsSort(desktop.installedApplications.array, desktop.installedApplications.Length(), 
			sizeof(InstalledApplication *), [] (const void *_left, const void *_right, EsGeneric) {
		InstalledApplication *left = *(InstalledApplication **) _left;
		InstalledApplication *right = *(InstalledApplication **) _right;
		return EsStringCompare(left->cName, EsCStringLength(left->cName), right->cName, EsCStringLength(right->cName));
	}, 0);

	// Set the system configuration for other applications to read.

	// TODO Enforce a limit of 4MB on the size of the system configuration.
	// TODO Alternatively, replace this with a growable EsBuffer.
	const size_t bufferSize = 4194304;
	char *buffer = (char *) EsHeapAllocate(bufferSize, false);
	size_t position = 0;

	for (uintptr_t i = 0; i < api.systemConfigurationGroups.Length(); i++) {
		EsSystemConfigurationGroup *group = &api.systemConfigurationGroups[i];

		if (EsStringCompareRaw(group->sectionClass, group->sectionClassBytes, EsLiteral("font"))
				&& EsStringCompareRaw(group->sectionClass, group->sectionClassBytes, EsLiteral("file_type"))
				&& EsStringCompareRaw(group->section, group->sectionBytes, EsLiteral("ui"))) {
			continue;
		}

		EsINIState s = {};
		s.sectionClass = group->sectionClass, s.sectionClassBytes = group->sectionClassBytes;
		s.section = group->section, s.sectionBytes = group->sectionBytes;
		position += EsINIFormat(&s, buffer + position, bufferSize - position);

		for (uintptr_t i = 0; i < group->itemCount; i++) {
			EsSystemConfigurationItem *item = group->items + i;

			if (!item->keyBytes || item->key[0] == ';') {
				continue;
			}

			s.key = item->key, s.keyBytes = item->keyBytes;
			s.value = item->value, s.valueBytes = item->valueBytes;
			position += EsINIFormat(&s, buffer + position, bufferSize - position);
		}
	}

	EsSyscall(ES_SYSCALL_SYSTEM_CONFIGURATION_WRITE, (uintptr_t) buffer, position, 0, 0);
	EsHeapFree(buffer);
}

//////////////////////////////////////////////////////
// Image utilities:
//////////////////////////////////////////////////////

void WallpaperLoad(EsGeneric) {
	size_t pathBytes;
	char *path = EsSystemConfigurationReadString(EsLiteral("general"), EsLiteral("wallpaper"), &pathBytes);

	if (path) {
		void *buffer = EsHeapAllocate(desktop.wallpaperWindow->windowWidth * desktop.wallpaperWindow->windowHeight * 4, false);
		LoadImage(path, pathBytes, buffer, desktop.wallpaperWindow->windowWidth, desktop.wallpaperWindow->windowHeight, false);
		EsHeapFree(path);

		EsRectangle region = ES_RECT_2S(desktop.wallpaperWindow->windowWidth, desktop.wallpaperWindow->windowHeight);
		EsSyscall(ES_SYSCALL_WINDOW_SET_BITS, desktop.wallpaperWindow->handle, (uintptr_t) &region, (uintptr_t) buffer, 0);
		EsSyscall(ES_SYSCALL_SCREEN_FORCE_UPDATE, true, 0, 0, 0);

		// Work out the most common hue on the wallpaper, and set the system hue.

		uint32_t hueBuckets[36] = {};
		uint32_t hueSelected = 0;

		for (uintptr_t i = 0; i < desktop.wallpaperWindow->windowWidth * desktop.wallpaperWindow->windowHeight; i += 7) {
			float h, s, v;
			EsColorConvertToHSV(((uint32_t *) buffer)[i], &h, &s, &v);
			uintptr_t bucket = (uintptr_t) (h * 6);
			hueBuckets[bucket] += 2;
			hueBuckets[bucket == 35 ? 0 : (bucket + 1)] += 1;
			hueBuckets[bucket == 0 ? 35 : (bucket - 1)] += 1;
		}

		for (uintptr_t i = 1; i < 36; i++) {
			if (hueBuckets[i] > hueBuckets[hueSelected]) {
				hueSelected = i;
			}
		}

		theming.systemHue = hueSelected / 6.0f;
		if (theming.systemHue < 0) theming.systemHue += 6.0f;

		EsHeapFree(buffer);

		// Tell all container windows to redraw with the new system hue.

		EsMessageMutexAcquire();

		for (uintptr_t i = 0; i < gui.allWindows.Length(); i++) {
			if (gui.allWindows[i]->windowStyle == ES_WINDOW_CONTAINER) {
				gui.allWindows[i]->Repaint(true);
				UIWindowNeedsUpdate(gui.allWindows[i]);
			}
		}

		EsMessageMutexRelease();
	}

	// TODO Fade wallpaper in.
}

//////////////////////////////////////////////////////
// General Desktop:
//////////////////////////////////////////////////////

ApplicationInstance *ApplicationInstanceFindForeground() {
	for (uintptr_t i = 0; i < desktop.allApplicationInstances.Length(); i++) {
		ApplicationInstance *instance = desktop.allApplicationInstances[i];
		WindowTab *tab = instance->tab;

		if (tab && (tab->container->taskBarButton->customStyleState & THEME_STATE_SELECTED) && tab->container->active == instance->tab) {
			return instance;
		}
	}

	return nullptr;
}

void CheckForegroundWindowResponding(EsGeneric) {
	ApplicationInstance *instance = ApplicationInstanceFindForeground();
	EsTimerSet(2500, CheckForegroundWindowResponding, 0); 
	if (!instance) return;

	WindowTab *tab = instance->tab;

	EsProcessState state;
	EsProcessGetState(instance->processHandle, &state);

	if (state.flags & ES_PROCESS_STATE_PINGED) {
		if (tab->notRespondingInstance) {
			// The tab is already not responding.
		} else {
			// The tab has just stopped not responding.
			EsApplicationStartupInformation startupInformation = { .data = CRASHED_TAB_NOT_RESPONDING };
			tab->notRespondingInstance = ApplicationInstanceCreate(APPLICATION_ID_DESKTOP_CRASHED, 
					&startupInformation, tab->container, true /* hidden */);
			WindowTabActivate(tab, true);
		}
	} else {
		if (tab->notRespondingInstance) {
			// The tab has started responding.
			ApplicationInstanceClose(tab->notRespondingInstance);
			tab->notRespondingInstance = nullptr;
			WindowTabActivate(tab, true);
		} else {
			// Check if the tab is responding.
			EsMessage m;
			EsMemoryZero(&m, sizeof(EsMessage));
			m.type = ES_MSG_PING;
			EsMessagePostRemote(instance->processHandle, &m); 
		}
	}
}

void DesktopSetup() {
	if (!desktop.setupDesktopUIComplete) {
		// Get the installation state.
		desktop.installationState = EsSystemConfigurationReadInteger(EsLiteral("general"), EsLiteral("installation_state"));
	}

	if (!desktop.setupDesktopUIComplete) {
		// Load the theme bitmap.

		EsHandle handle = EsMemoryOpen(ES_THEME_CURSORS_WIDTH * ES_THEME_CURSORS_HEIGHT * 4, EsLiteral(ES_THEME_CURSORS_NAME), ES_FLAGS_DEFAULT); 
		void *destination = EsObjectMap(handle, 0, ES_THEME_CURSORS_WIDTH * ES_THEME_CURSORS_HEIGHT * 4, ES_MAP_OBJECT_READ_WRITE);
		LoadImage(theming.system.in + theming.system.bytes - theming.header->bitmapBytes, theming.header->bitmapBytes, 
				destination, ES_THEME_CURSORS_WIDTH, ES_THEME_CURSORS_HEIGHT, true);
		EsObjectUnmap(destination);
		EsHandleClose(handle);
	}

	{
		// Create the wallpaper window.

		if (!desktop.wallpaperWindow) desktop.wallpaperWindow = EsWindowCreate(nullptr, ES_WINDOW_PLAIN);
		EsRectangle screen;
		EsSyscall(ES_SYSCALL_SCREEN_BOUNDS_GET, 0, (uintptr_t) &screen, 0, 0);
		EsSyscall(ES_SYSCALL_WINDOW_MOVE, desktop.wallpaperWindow->handle, (uintptr_t) &screen, 0, ES_WINDOW_MOVE_AT_BOTTOM);
		EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, desktop.wallpaperWindow->handle, (uintptr_t) &screen, 0, ES_WINDOW_PROPERTY_OPAQUE_BOUNDS);
		EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, desktop.wallpaperWindow->handle, 
				ES_WINDOW_SOLID_TRUE | ES_WINDOW_SOLID_NO_BRING_TO_FRONT, 0, ES_WINDOW_PROPERTY_SOLID);
		desktop.wallpaperWindow->windowWidth = Width(screen);
		desktop.wallpaperWindow->windowHeight = Height(screen);
		desktop.wallpaperWindow->doNotPaint = true;
		EsThreadCreate(WallpaperLoad, nullptr, 0);
	}

	if (desktop.installationState == INSTALLATION_STATE_NONE) {
		// Create the taskbar.

		EsWindow *window = desktop.setupDesktopUIComplete ? desktop.taskBar.window : EsWindowCreate(nullptr, ES_WINDOW_PLAIN);
		window->messageUser = TaskBarWindowMessage;
		window->StartAnimating();
		EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, ES_WINDOW_SOLID_TRUE | ES_WINDOW_SOLID_NO_ACTIVATE, 0, ES_WINDOW_PROPERTY_SOLID);
		EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, BLEND_WINDOW_MATERIAL_GLASS, 0, ES_WINDOW_PROPERTY_MATERIAL);

		if (!desktop.setupDesktopUIComplete) desktop.taskBar.Initialise(window, ES_CELL_FILL, TaskBarMessage, ES_STYLE_TASK_BAR_BAR);
		desktop.taskBar.cName = "task bar";
		EsThemeMetrics metrics = EsElementGetMetrics(&desktop.taskBar);
		window->userData = &desktop.taskBar;

		EsRectangle screen;
		EsSyscall(ES_SYSCALL_SCREEN_BOUNDS_GET, 0, (uintptr_t) &screen, 0, 0);

		EsRectangle bounds = screen;
		bounds.t = bounds.b - metrics.preferredHeight;
		desktop.taskBar.targetBounds = bounds;

		screen.b = bounds.t;
		EsSyscall(ES_SYSCALL_SCREEN_WORK_AREA_SET, 0, (uintptr_t) &screen, 0, 0);

		bounds.r -= bounds.l, bounds.b -= bounds.t;
		bounds.l = bounds.t = 0;
		EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, (uintptr_t) &bounds, 0, ES_WINDOW_PROPERTY_BLUR_BOUNDS);

		if (!desktop.setupDesktopUIComplete) {
			EsPanel *panel = EsPanelCreate(&desktop.taskBar, ES_PANEL_HORIZONTAL | ES_CELL_FILL, {});

			EsButton *newWindowButton = EsButtonCreate(panel, ES_FLAGS_DEFAULT, ES_STYLE_TASK_BAR_NEW_WINDOW);

			EsButtonOnCommand(newWindowButton, [] (EsInstance *, EsElement *, EsCommand *) {
				ApplicationInstanceCreate(APPLICATION_ID_DESKTOP_BLANK_TAB, nullptr, nullptr);
			});

			desktop.taskBar.taskList.Initialise(panel, ES_CELL_FILL, ReorderListMessage, nullptr);
			desktop.taskBar.taskList.cName = "task list";

			EsButton *shutdownButton = EsButtonCreate(panel, ES_FLAGS_DEFAULT, ES_STYLE_TASK_BAR_EXTRA);
			EsButtonSetIcon(shutdownButton, ES_ICON_SYSTEM_SHUTDOWN_SYMBOLIC);

			EsButtonOnCommand(shutdownButton, [] (EsInstance *, EsElement *, EsCommand *) {
				ShutdownModalCreate();
			});

		}
	}

	if (!desktop.setupDesktopUIComplete) {
		// Launch the first application.

		char *firstApplication = EsSystemConfigurationReadString(EsLiteral("general"), EsLiteral("first_application"));

		if (firstApplication && firstApplication[0]) {
			for (uintptr_t i = 0; i < desktop.installedApplications.Length(); i++) {
				if (desktop.installedApplications[i]->cName && 0 == EsCRTstrcmp(desktop.installedApplications[i]->cName, firstApplication)) {
					ApplicationInstanceCreate(desktop.installedApplications[i]->id, nullptr, nullptr);
				}
			}
		}

		EsHeapFree(firstApplication);
	}

#ifdef CHECK_FOR_NOT_RESPONDING
	if (!desktop.setupDesktopUIComplete) {
		// Setup the timer callback to check if the foreground window is responding.
		EsTimerSet(2500, CheckForegroundWindowResponding, 0); 
	}
#endif

	if (desktop.setupDesktopUIComplete) {
	} else if (desktop.installationState == INSTALLATION_STATE_NONE) {
#if 0
		// Play the startup sound.

		EsThreadCreate([] (EsGeneric) {
			size_t pathBytes;
			char *path = EsSystemConfigurationReadString(EsLiteral("general"), EsLiteral("startup_sound"), &pathBytes);

			if (path) {
				PlaySound(path, pathBytes);
				EsHeapFree(path);
			}
		}, nullptr, 0);
#endif
	} else if (desktop.installationState == INSTALLATION_STATE_INSTALLER) {
		// Start the installer.

		EsWindow *window = EsWindowCreate(nullptr, ES_WINDOW_PLAIN);

		EsRectangle screen;
		EsSyscall(ES_SYSCALL_SCREEN_BOUNDS_GET, 0, (uintptr_t) &screen, 0, 0);
		EsSyscall(ES_SYSCALL_WINDOW_MOVE, window->handle, (uintptr_t) &screen, 0, ES_WINDOW_MOVE_ALWAYS_ON_TOP);
		EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, ES_WINDOW_SOLID_TRUE, 0, ES_WINDOW_PROPERTY_SOLID);

		EsPanel *root = EsPanelCreate(window, ES_PANEL_VERTICAL | ES_CELL_PUSH | ES_CELL_CENTER, ES_STYLE_INSTALLER_ROOT);
		EsTextDisplayCreate(root, ES_CELL_H_FILL, ES_STYLE_TEXT_HEADING0, EsLiteral("Essence Installation"));

		// TODO.
	}

	desktop.setupDesktopUIComplete = true;
}

void DesktopMessage2(EsMessage *message, uint8_t *buffer) {
	ApplicationInstance *instance = ApplicationInstanceFindByWindowID(message->desktop.windowID);

	if (buffer[0] == DESKTOP_MSG_START_APPLICATION) {
		EsApplicationStartupInformation *information = ApplicationStartupInformationParse(buffer + 1, message->desktop.bytes - 1);
		if (information) OpenDocumentWithApplication(information);
	} else if (buffer[0] == DESKTOP_MSG_CREATE_CLIPBOARD_FILE && message->desktop.pipe) {
		EsHandle processHandle = EsProcessOpen(message->desktop.processID);

		if (processHandle) {
			EsHandle handle;
			char *path;
			size_t pathBytes;
			EsError error = TemporaryFileCreate(&handle, &path, &pathBytes);

			if (error == ES_SUCCESS) {
				if (desktop.nextClipboardFile) {
					EsHandleClose(desktop.nextClipboardFile);
				}

				desktop.nextClipboardFile = handle;
				desktop.nextClipboardProcessID = message->desktop.processID;

				handle = EsSyscall(ES_SYSCALL_NODE_SHARE, handle, processHandle, 0, 0);

				EsHeapFree(path);
			} else {
				handle = ES_INVALID_HANDLE;
			}

			EsPipeWrite(message->desktop.pipe, &handle, sizeof(handle));
			EsPipeWrite(message->desktop.pipe, &error, sizeof(error));

			EsHandleClose(processHandle);
		}
	} else if (buffer[0] == DESKTOP_MSG_CLIPBOARD_PUT && message->desktop.bytes == sizeof(ClipboardInformation)
			&& desktop.nextClipboardFile && desktop.nextClipboardProcessID == message->desktop.processID) {
		ClipboardInformation *information = (ClipboardInformation *) buffer;

		if (information->error == ES_SUCCESS) {
			if (desktop.clipboardFile) {
				EsFileDelete(desktop.clipboardFile);
				EsHandleClose(desktop.clipboardFile);
			}

			desktop.clipboardFile = desktop.nextClipboardFile;
			desktop.clipboardInformation = *information;

			ApplicationInstance *foreground = ApplicationInstanceFindForeground();

			if (foreground && foreground->processHandle) {
				EsMessage m = { ES_MSG_PRIMARY_CLIPBOARD_UPDATED };
				m.tabOperation.id = foreground->embeddedWindowID;
				EsMessagePostRemote(foreground->processHandle, &m);
			}
		} else {
			EsHandleClose(desktop.nextClipboardFile);
		}

		desktop.nextClipboardFile = ES_INVALID_HANDLE;
		desktop.nextClipboardProcessID = 0;
	} else if (buffer[0] == DESKTOP_MSG_CLIPBOARD_GET && message->desktop.pipe) {
		EsHandle processHandle = EsProcessOpen(message->desktop.processID);

		if (processHandle) {
			EsHandle fileHandle = desktop.clipboardFile 
				? EsSyscall(ES_SYSCALL_NODE_SHARE, desktop.clipboardFile, processHandle, 0, 1 /* ES_FILE_READ_SHARED */) : ES_INVALID_HANDLE;
			EsPipeWrite(message->desktop.pipe, &desktop.clipboardInformation, sizeof(desktop.clipboardInformation));
			EsPipeWrite(message->desktop.pipe, &fileHandle, sizeof(fileHandle));
			EsHandleClose(processHandle);
		}
	} else if (!instance) {
		// -------------------------------------------------
		// | Messages below here require a valid instance. |
		// -------------------------------------------------
	} else if (buffer[0] == DESKTOP_MSG_SET_TITLE || buffer[0] == DESKTOP_MSG_SET_ICON) {
		if (buffer[0] == DESKTOP_MSG_SET_TITLE) {
			instance->titleBytes = EsStringFormat(instance->title, sizeof(instance->title), "%s", 
					message->desktop.bytes - 1, buffer + 1);
		} else {
			if (message->desktop.bytes == 5) {
				EsMemoryCopy(&instance->iconID, buffer + 1, sizeof(uint32_t));
			}
		}

		instance->tab->Repaint(true);

		if (instance->tab == instance->tab->container->active) {
			instance->tab->container->taskBarButton->Repaint(true);
		}
	} else if (buffer[0] == DESKTOP_MSG_REQUEST_SAVE) {
		ApplicationInstanceRequestSave(instance, (const char *) buffer + 1, message->desktop.bytes - 1);
	} else if (buffer[0] == DESKTOP_MSG_COMPLETE_SAVE) {
		ApplicationInstanceCompleteSave(instance);
	} else if (buffer[0] == DESKTOP_MSG_SHOW_IN_FILE_MANAGER) {
		// TODO Don't open a new instance if the folder is already open?
		OpenDocument *document = desktop.openDocuments.Get(&instance->documentID);

		if (document) {
			EsApplicationStartupInformation startupInformation = {};
			startupInformation.flags = ES_APPLICATION_STARTUP_MANUAL_PATH;
			startupInformation.filePath = document->path;
			startupInformation.filePathBytes = document->pathBytes;
			ApplicationInstanceCreate(desktop.fileManager->id, &startupInformation, instance->tab->container);
		}
	} else if (buffer[0] == DESKTOP_MSG_ANNOUNCE_PATH_MOVED
			&& (instance->application->permissions & APPLICATION_PERMISSION_ALL_FILES)
			&& message->desktop.bytes > 1 + sizeof(uintptr_t) * 2) {
		InstanceAnnouncePathMoved(instance, buffer, message->desktop.bytes);
	} else if (buffer[0] == DESKTOP_MSG_RUN_TEMPORARY_APPLICATION) {
		if (instance->application && (instance->application->permissions & APPLICATION_PERMISSION_RUN_TEMPORARY_APPLICATION)) {
			InstalledApplication *application = (InstalledApplication *) EsHeapAllocate(sizeof(InstalledApplication), true);
			application->temporary = true;
			application->hidden = true;
			application->useSingleProcess = true;
			application->cExecutable = (char *) EsHeapAllocate(message->desktop.bytes, false);
			EsMemoryCopy(application->cExecutable, buffer + 1, message->desktop.bytes - 1);
			application->cExecutable[message->desktop.bytes - 1] = 0;
			static int64_t nextTemporaryID = -1;
			application->id = nextTemporaryID--;
			application->cName = (char *) EsHeapAllocate(32, false);
			for (int i = 1; i < 31; i++) application->cName[i] = (EsRandomU8() % 26) + 'a';
			application->cName[0] = '_', application->cName[31] = 0;
			desktop.installedApplications.Add(application);
			ApplicationInstanceCreate(application->id, nullptr, nullptr);
		}
	} else if (buffer[0] == DESKTOP_MSG_REQUEST_SHUTDOWN) {
		if (instance->application && (instance->application->permissions & APPLICATION_PERMISSION_SHUTDOWN)) {
			ShutdownModalCreate();
		}
	}
}

void EmbeddedWindowDestroyed(EsObjectID id) {
	// TODO Close open documents.

	EsMenuCloseAll();
	ApplicationInstance *instance = ApplicationInstanceFindByWindowID(id, true /* remove if found */);
	if (!instance) return;

	EsHandleClose(instance->embeddedWindowHandle);
	if (instance->processHandle) EsHandleClose(instance->processHandle);

	InstalledApplication *application = instance->application;

	if (application && application->singleProcessHandle) {
		EsAssert(application->openInstanceCount);
		application->openInstanceCount--;

		if (!application->openInstanceCount && application->useSingleProcess) {
			EsMessage m = { ES_MSG_APPLICATION_EXIT };
			EsMessagePostRemote(application->singleProcessHandle, &m);
			EsHandleClose(application->singleProcessHandle);
			application->singleProcessHandle = ES_INVALID_HANDLE;
			ApplicationTemporaryDestroy(application);
			application = nullptr;
		}
	}

	if (instance->tab) {
		ContainerWindow *container = instance->tab->container;

		if (container->tabBand->items.Length() == 1) {
			EsElementDestroy(container->window);
			EsElementDestroy(container->taskBarButton);
			desktop.allContainerWindows.FindAndDeleteSwap(container, true);
		} else {
			if (container->active == instance->tab) {
				container->active = nullptr;

				for (uintptr_t i = 0; i < container->tabBand->items.Length(); i++) {
					if (container->tabBand->items[i] != instance->tab) continue;
					WindowTabActivate((WindowTab *) container->tabBand->items[i ? (i - 1) : 1]);
					break;
				}
			}

			EsElementDestroy(instance->tab);
		}
	}

	EsHeapFree(instance);
}

void DesktopMessage(EsMessage *message) {
	if (message->type == ES_MSG_POWER_BUTTON_PRESSED) {
		ShutdownModalCreate();
	} else if (message->type == ES_MSG_EMBEDDED_WINDOW_DESTROYED) {
		EmbeddedWindowDestroyed(message->desktop.windowID);
	} else if (message->type == ES_MSG_DESKTOP) {
		uint8_t *buffer = (uint8_t *) EsHeapAllocate(message->desktop.bytes, false);

		if (buffer) {
			EsConstantBufferRead(message->desktop.buffer, buffer);
			DesktopMessage2(message, buffer);
			EsHeapFree(buffer);
		}

		EsHandleClose(message->desktop.buffer);
		if (message->desktop.pipe) EsHandleClose(message->desktop.pipe);
	} else if (message->type == ES_MSG_APPLICATION_CRASH) {
		ApplicationInstanceCrashed(message);
	} else if (message->type == ES_MSG_PROCESS_TERMINATED) {
		ApplicationProcessTerminated(message->crash.pid);
	} else if (message->type == ES_MSG_REGISTER_FILE_SYSTEM) {
		EsHandle rootDirectory = message->registerFileSystem.rootDirectory;

		for (uintptr_t i = 0; i < desktop.allApplicationInstances.Length(); i++) {
			ApplicationInstance *instance = desktop.allApplicationInstances[i];

			if (instance->application && (instance->application->permissions & APPLICATION_PERMISSION_ALL_FILES) 
					&& instance->processHandle && !instance->application->notified) {
				message->registerFileSystem.rootDirectory = EsSyscall(ES_SYSCALL_NODE_SHARE, rootDirectory, instance->processHandle, 0, 0);
				EsMessagePostRemote(instance->processHandle, message);
				if (instance->application->useSingleProcess) instance->application->notified = true;
			}
		}

		for (uintptr_t i = 0; i < desktop.installedApplications.Length(); i++) {
			desktop.installedApplications[i]->notified = false;
		}
	} else if (message->type == ES_MSG_UNREGISTER_FILE_SYSTEM) {
		for (uintptr_t i = 0; i < desktop.allApplicationInstances.Length(); i++) {
			ApplicationInstance *instance = desktop.allApplicationInstances[i];

			if (instance->application && (instance->application->permissions & APPLICATION_PERMISSION_ALL_FILES) 
					&& instance->processHandle && !instance->application->notified) {
				EsMessagePostRemote(instance->processHandle, message);
				if (instance->application->useSingleProcess) instance->application->notified = true;
			}
		}

		for (uintptr_t i = 0; i < desktop.installedApplications.Length(); i++) {
			desktop.installedApplications[i]->notified = false;
		}
	} else if (message->type == ES_MSG_DEVICE_CONNECTED) {
		desktop.connectedDevices.Add(message->device);
	} else if (message->type == ES_MSG_DEVICE_DISCONNECTED) {
		for (uintptr_t i = 0; i < desktop.connectedDevices.Length(); i++) {
			if (desktop.connectedDevices[i].id == message->device.id) {
				EsHandleClose(desktop.connectedDevices[i].handle);
				desktop.connectedDevices.Delete(i);
				break;
			}
		}
	} else if (message->type == ES_MSG_SET_SCREEN_RESOLUTION) {
		if (desktop.setupDesktopUIComplete) {
			DesktopSetup(); // Refresh desktop UI.
		} else {
			// The screen resolution will be correctly queried in DesktopSetup.
		}
	} else if (message->type == MSG_SETUP_DESKTOP_UI) {
		DesktopSetup();
	}
}

void DesktopEntry() {
	ConfigurationLoad();

	EsMessage m = { MSG_SETUP_DESKTOP_UI };
	EsMessagePost(nullptr, &m);

	while (true) {
		EsMessage *message = EsMessageReceive();
		DesktopMessage(message);
	}
}
