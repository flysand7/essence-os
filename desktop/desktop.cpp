// TODO Tabs:
//	- New tab page - search; recent files.
// 	- Right click menu.
// 	- Duplicate tabs.

// TODO Graphical issues:
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

// TODO Restarting Desktop if it crashes.
// TODO Make sure applications can't delete |Fonts:.
// TODO Handle open document deletion.
// TODO Store an array of processes for each InstalledApplication.

#define MSG_SETUP_DESKTOP_UI ((EsMessageType) (ES_MSG_USER_START + 1))

#define APPLICATION_PERMISSION_ALL_FILES                 (1 << 0)
#define APPLICATION_PERMISSION_MANAGE_PROCESSES          (1 << 1)
#define APPLICATION_PERMISSION_POSIX_SUBSYSTEM           (1 << 2)
#define APPLICATION_PERMISSION_RUN_TEMPORARY_APPLICATION (1 << 3)
#define APPLICATION_PERMISSION_SHUTDOWN                  (1 << 4)
#define APPLICATION_PERMISSION_VIEW_FILE_TYPES           (1 << 5)
#define APPLICATION_PERMISSION_ALL_DEVICES               (1 << 6)
#define APPLICATION_PERMISSION_START_APPLICATION         (1 << 7)

#define APPLICATION_ID_DESKTOP_BLANK_TAB (-0x70000000)
#define APPLICATION_ID_DESKTOP_SETTINGS  (-0x70000001)
#define APPLICATION_ID_DESKTOP_CRASHED   (-0x70000002)

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
	// NOTE Don't forget to update WindowTabMoveToNewContainer when modifying this.
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
	uintptr_t referenceCount;
};

struct InstalledApplication {
	char *cName;
	char *cExecutable;
	char *settingsPath;
	size_t settingsPathBytes;
	void (*createInstance)(EsMessage *); // For applications provided by Desktop.
	int64_t id;
	uint32_t iconID;
	bool hidden, useSingleProcess, temporary;
	bool useSingleInstance;
	struct ApplicationInstance *singleInstance;
	uint64_t permissions;
	struct ApplicationProcess *singleProcess;
	EsFileOffset totalSize; // 0 if uncalculated.
};

struct CommonDesktopInstance : EsInstance {
	void (*destroy)(EsInstance *);
};

struct CrashedTabInstance : CommonDesktopInstance {
};

struct BlankTabInstance : CommonDesktopInstance {
};

struct ApplicationProcess {
	EsObjectID id;
	EsHandle handle;
	InstalledApplication *application;
	size_t instanceCount;
};

struct ApplicationInstance {
	// User interface.
	WindowTab *tab; // nullptr for notRespondingInstance and user tasks.
	EsObjectID embeddedWindowID;
	EsHandle embeddedWindowHandle;

	// Currently loaded application.
	InstalledApplication *application;
	EsObjectID documentID;
	ApplicationProcess *process;
	bool isUserTask;

	// Metadata.
	char title[128];
	size_t titleBytes;
	uint32_t iconID;
	double progress;
};

const EsStyle styleSmallParagraph = {
	.inherit = ES_STYLE_TEXT_PARAGRAPH,

	.metrics = {
		.mask = ES_THEME_METRICS_TEXT_SIZE,
		.textSize = 8,
	},
};

const EsStyle styleNewTabContent = {
	.metrics = {
		.mask = ES_THEME_METRICS_INSETS | ES_THEME_METRICS_GAP_MAJOR,
		.insets = ES_RECT_4(50, 50, 50, 50),
		.gapMajor = 25,
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
	Array<ApplicationProcess *> allApplicationProcesses;
	Array<ContainerWindow *> allContainerWindows;

	InstalledApplication *fileManager;
	InstalledApplication *installer;

	EsObjectID currentDocumentID;
	HashStore<EsObjectID, OpenDocument> openDocuments;

	TaskBar taskBar;
	EsWindow *wallpaperWindow;
	EsButton *tasksButton;

	bool shutdownWindowOpen;
	bool setupDesktopUIComplete;
	uint8_t installationState;

	EsHandle nextClipboardFile;
	EsObjectID nextClipboardProcessID;
	EsHandle clipboardFile;
	ClipboardInformation clipboardInformation;

	bool configurationModified;

	Array<ApplicationInstance *> allOngoingUserTasks;
	double totalUserTaskProgress;

#define SHUTDOWN_TIMEOUT (3000) // Maximum time to wait for applications to exit.
	EsHandle shutdownReady; // Set when all applications have exited.
	bool inShutdown;

	bool inspectorOpen;
} desktop;

int TaskBarButtonMessage(EsElement *element, EsMessage *message);
ApplicationInstance *ApplicationInstanceCreate(int64_t id, _EsApplicationStartupInformation *startupInformation, ContainerWindow *container, bool hidden = false);
bool ApplicationInstanceStart(int64_t applicationID, _EsApplicationStartupInformation *startupInformation, ApplicationInstance *instance);
void ApplicationInstanceClose(ApplicationInstance *instance);
ApplicationInstance *ApplicationInstanceFindByWindowID(EsObjectID windowID, bool remove = false);
void EmbeddedWindowDestroyed(EsObjectID id);
void ConfigurationWriteToFile();
void OpenDocumentOpenReference(EsObjectID id);
void OpenDocumentCloseReference(EsObjectID id);
void WallpaperLoad(EsGeneric);
WindowTab *WindowTabCreate(ContainerWindow *container);
ContainerWindow *ContainerWindowCreate();
void ContainerWindowShow(ContainerWindow *, int32_t width, int32_t height);

#include "settings.cpp"

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

	int draggedIndex = list->targetWidth ? ((item->dragPosition + list->targetWidth / 2) / list->targetWidth) : 0;
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

		for (uintptr_t i = 0, x = list->style->insets.l; i < childCount; i++) {
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

	for (uintptr_t i = 0, x = list->style->insets.l; i < list->items.Length(); i++) {
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
	bounds.l += list->style->insets.l;
	bounds.r -= list->style->insets.r;
	bounds.r -= additionalRightMargin;

	size_t childCount = list->items.Length();

	if (!childCount) {
		return 0;
	}

	int totalWidth = 0;

	for (uintptr_t i = 0; i < childCount; i++) {
		ReorderItem *child = list->items[i];
		totalWidth += child->style->metrics->maximumWidth + list->style->metrics->gapMinor;
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
		int gap = list->style->metrics->gapMinor;

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
// Desktop Inspector:
//////////////////////////////////////////////////////

void DesktopInspectorThread(EsGeneric) {
	EsMessageMutexAcquire();
	EsWindow *window = EsWindowCreate(nullptr, ES_WINDOW_PLAIN);
	EsRectangle screen;
	EsSyscall(ES_SYSCALL_SCREEN_WORK_AREA_GET, 0, (uintptr_t) &screen, 0, 0);
	screen.l = screen.r - 400;
	EsSyscall(ES_SYSCALL_WINDOW_MOVE, window->handle, (uintptr_t) &screen, 0, ES_WINDOW_MOVE_ALWAYS_ON_TOP);
	EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, ES_FLAGS_DEFAULT, 0, ES_WINDOW_PROPERTY_SOLID);
	EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, 180, 0, ES_WINDOW_PROPERTY_ALPHA);
	EsPanel *panel = EsPanelCreate(window, ES_CELL_FILL, ES_STYLE_PANEL_FILLED);
	EsMessageMutexRelease();

	while (true) {
		EsMessageMutexAcquire();
		EsElementDestroyContents(panel);
		char buffer[256];
		size_t bytes;

		EsTextDisplayCreate(panel, ES_CELL_H_FILL, ES_STYLE_TEXT_HEADING1, "Desktop Inspector");
		EsSpacerCreate(panel, ES_CELL_H_FILL, 0, 0, 5);

		for (uintptr_t i = 0; i < desktop.allApplicationInstances.Length(); i++) {
			ApplicationInstance *instance = desktop.allApplicationInstances[i];
			bytes = EsStringFormat(buffer, sizeof(buffer), "inst: eid %d, title '%s', pid %d, docid %d, app '%z'%z", 
					instance->embeddedWindowID, instance->titleBytes, instance->title, instance->process->id, 
					instance->documentID, instance->application->cName, instance->isUserTask ? ", utask" : "");
			EsTextDisplayCreate(panel, ES_CELL_H_FILL, &styleSmallParagraph, buffer, bytes);
		}

		for (uintptr_t i = 0; i < desktop.allApplicationProcesses.Length(); i++) {
			ApplicationProcess *process = desktop.allApplicationProcesses[i];
			bytes = EsStringFormat(buffer, sizeof(buffer), "proc: pid %d, app '%z', instances %d", 
					process->id, process->application ? process->application->cName : "??", process->instanceCount);
			EsTextDisplayCreate(panel, ES_CELL_H_FILL, &styleSmallParagraph, buffer, bytes);
		}

		for (uintptr_t i = 0; i < desktop.installedApplications.Length(); i++) {
			InstalledApplication *application = desktop.installedApplications[i];
			bytes = EsStringFormat(buffer, sizeof(buffer), "app: '%z'%z%z%z, spid %d, seid %d", 
					application->cName, application->temporary ? ", temp" : "", application->useSingleProcess ? ", 1proc" : "",
					application->useSingleInstance ? ", 1inst" : "",  
					application->singleProcess ? application->singleProcess->id : 0,
					application->singleInstance ? application->singleInstance->embeddedWindowID : 0);
			EsTextDisplayCreate(panel, ES_CELL_H_FILL, &styleSmallParagraph, buffer, bytes);
		}

		for (uintptr_t i = 0; i < desktop.openDocuments.Count(); i++) {
			OpenDocument *document = &desktop.openDocuments[i];
			bytes = EsStringFormat(buffer, sizeof(buffer), "doc: '%s', id %d, refs %d", 
					document->pathBytes, document->path, document->id, document->referenceCount);
			EsTextDisplayCreate(panel, ES_CELL_H_FILL, &styleSmallParagraph, buffer, bytes);
		}

		EsMessageMutexRelease();
		EsSleep(500);
	}
}

//////////////////////////////////////////////////////
// Container windows:
//////////////////////////////////////////////////////

void WindowTabClose(WindowTab *tab) {
	if (tab->notRespondingInstance) {
		// The application is not responding, so force quit the process.
		EsProcessTerminate(tab->applicationInstance->process->handle, 1);
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

void WindowTabDestroy(WindowTab *tab) {
	ContainerWindow *container = tab->container;

	if (container->tabBand->items.Length() == 1) {
		EsElementDestroy(container->window);
		EsElementDestroy(container->taskBarButton);
		desktop.allContainerWindows.FindAndDeleteSwap(container, true);
	} else {
		if (container->active == tab) {
			container->active = nullptr;

			for (uintptr_t i = 0; i < container->tabBand->items.Length(); i++) {
				if (container->tabBand->items[i] != tab) continue;
				WindowTabActivate((WindowTab *) container->tabBand->items[i ? (i - 1) : 1]);
				break;
			}
		}

		EsElementDestroy(tab);
	}
}

WindowTab *WindowTabMoveToNewContainer(WindowTab *tab, ContainerWindow *container, int32_t width, int32_t height) {
	if (!container) {
		// Create a new container.
		container = ContainerWindowCreate();
		if (!container) return nullptr;
		ContainerWindowShow(container, width, height);
	}

	// Create the new tab.
	WindowTab *newTab = WindowTabCreate(container);
	if (!newTab) return nullptr;

	// Move ownership of the instance to the new tab.
	newTab->applicationInstance = tab->applicationInstance;
	newTab->notRespondingInstance = tab->notRespondingInstance;
	EsAssert(tab->applicationInstance->tab == tab);
	tab->applicationInstance->tab = newTab;
	tab->applicationInstance = nullptr;
	tab->notRespondingInstance = nullptr;

	// Destroy the old tab, and activate the new one.
	WindowTabDestroy(tab); // Deplaces the embedded window from the old container.
	WindowTabActivate(newTab);

	// If this is an existing container window, make sure it's activated.
	if (container) EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, newTab->window->handle, 0, 0, ES_WINDOW_PROPERTY_FOCUSED);

	return newTab;
}

int CursorLocatorMessage(EsElement *element, EsMessage *message) {
	EsWindow *window = element->window;

	if (message->type == ES_MSG_ANIMATE) {
		window->announcementTimeMs += message->animate.deltaMs;
		double progress = window->announcementTimeMs / GetConstantNumber("cursorLocatorDuration");

		if (progress > 1) {
			EsElementDestroy(window);
		} else {
			EsElementRelayout(element);
			message->animate.complete = false;
			return ES_HANDLED;
		}
	} else if (message->type == ES_MSG_LAYOUT) {
		EsElement *child = element->GetChild(0);
		double progress = 1.0 - window->announcementTimeMs / GetConstantNumber("cursorLocatorDuration");
		int width = progress * child->GetWidth(0), height = progress * child->GetHeight(0);
		child->InternalMove(width, height, (element->width - width) / 2, (element->height - height) / 2);
	}

	return 0;
}

int ProcessGlobalKeyboardShortcuts(EsElement *, EsMessage *message) {
	if (desktop.installationState) {
		// Do not process global keyboard shortcuts if the installer is running.
	} else if (message->type == ES_MSG_KEY_DOWN) {
		bool ctrlOnly = message->keyboard.modifiers == ES_MODIFIER_CTRL;
		int scancode = message->keyboard.scancode;

		if (ctrlOnly && scancode == ES_SCANCODE_N && !message->keyboard.repeat) {
			ApplicationInstanceCreate(APPLICATION_ID_DESKTOP_BLANK_TAB, nullptr, nullptr);
		} else if (message->keyboard.modifiers == (ES_MODIFIER_CTRL | ES_MODIFIER_FLAG) && scancode == ES_SCANCODE_D) {
			if (!desktop.inspectorOpen) {
				desktop.inspectorOpen = true;
				EsThreadCreate(DesktopInspectorThread, nullptr, 0);
			}
		} else {
			return 0;
		}

		return ES_HANDLED;
	} else if (message->type == ES_MSG_KEY_UP) {
		if (message->keyboard.scancode == ES_SCANCODE_LEFT_CTRL && message->keyboard.single) {
			if (EsSystemConfigurationReadInteger(EsLiteral("general"), EsLiteral("locate_cursor_on_ctrl"))) {
				EsPoint position = EsMouseGetPosition();
				EsWindow *window = EsWindowCreate(nullptr, ES_WINDOW_TIP);
				EsElement *wrapper = EsCustomElementCreate(window, ES_CELL_FILL, ES_STYLE_CLEAR_BACKGROUND);
				wrapper->messageUser = CursorLocatorMessage;
				window->announcementBase = position;
				EsElement *element = EsCustomElementCreate(wrapper, ES_CELL_FILL, ES_STYLE_CURSOR_LOCATOR);
				int width = element->GetWidth(0), height = element->GetHeight(0);
				EsRectangle bounds = ES_RECT_4PD(position.x - width / 2, position.y - height / 2, width, height);
				EsSyscall(ES_SYSCALL_WINDOW_MOVE, window->handle, (uintptr_t) &bounds, 0, ES_WINDOW_MOVE_ALWAYS_ON_TOP);
				wrapper->StartAnimating();
			}
		}
	}

	return 0;
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
		} else if (ctrlOnly && scancode == ES_SCANCODE_T && !message->keyboard.repeat) {
			ApplicationInstanceCreate(APPLICATION_ID_DESKTOP_BLANK_TAB, nullptr, container);
		} else if (ctrlOnly && scancode == ES_SCANCODE_W && !message->keyboard.repeat) {
			WindowTabClose(container->active);
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
			bool unhandled = true;

			for (uintptr_t i = 0; i < 9; i++) {
				if (ctrlOnly && scancode == (int) (ES_SCANCODE_1 + i) && container->tabBand->items.Length() > i) {
					WindowTabActivate((WindowTab *) container->tabBand->items[i]);
					unhandled = false;
					break;
				}
			}

			if (unhandled) {
				ProcessGlobalKeyboardShortcuts(element, message);
			}
		}
	} else if (message->type == ES_MSG_KEY_UP) {
		ProcessGlobalKeyboardShortcuts(element, message);
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
	} else if (message->type == ES_MSG_MOUSE_LEFT_DOWN) {
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
		int closeButtonWidth = tab->closeButton->style->preferredWidth;
		Rectangle16 insets = tab->style->metrics->insets;
		EsElementSetHidden(tab->closeButton, tab->style->gapWrap * 2 + closeButtonWidth >= tab->width);
		EsElementMove(tab->closeButton, tab->width - tab->style->gapWrap - closeButtonWidth, 
				insets.t, closeButtonWidth, tab->height - insets.t - insets.b);
	} else if (message->type == ES_MSG_PAINT) {
		EsDrawContent(message->painter, element, ES_RECT_2S(message->painter->width, message->painter->height), 
				instance->title, instance->titleBytes, instance->iconID);
	} else if (message->type == ES_MSG_ANIMATE) {
		message->animate.complete = ReorderItemAnimate(tab, message->animate.deltaMs, "windowTabEntranceDuration");
	} else if (message->type == ES_MSG_MOUSE_LEFT_DRAG) {
		EsElementSetDisabled(band->GetChild(0), true);

		if (band->items.Length() == 1) {
			// Get the window we're hovering the tab over.
			EsObjectID hoverWindowID;
			EsPoint mousePositionOnScreen = EsMouseGetPosition();
			EsSyscall(ES_SYSCALL_WINDOW_FIND_BY_POINT, (uintptr_t) &hoverWindowID, mousePositionOnScreen.x, mousePositionOnScreen.y, tab->window->id);
			EsWindow *hoverWindow = WindowFromWindowID(hoverWindowID);
			bool dragInto = false;

			if (hoverWindow && hoverWindow->windowStyle == ES_WINDOW_CONTAINER) {
				// Are we hovering over the tab band?
				ContainerWindow *hoverContainer = (ContainerWindow *) hoverWindow->userData.p;
				EsRectangle hoverTabBandBounds = hoverContainer->tabBand->GetScreenBounds();
				dragInto = EsRectangleContains(hoverTabBandBounds, mousePositionOnScreen.x, mousePositionOnScreen.y);
			}

			if (!dragInto) {
				// Move the current window.
				WindowChangeBounds(RESIZE_MOVE, mousePositionOnScreen.x, mousePositionOnScreen.y, &gui.lastClickX, &gui.lastClickY, band->window);
			} else {
				ContainerWindow *hoverContainer = (ContainerWindow *) hoverWindow->userData.p;
				int32_t dragOffset = mousePositionOnScreen.x - tab->GetScreenBounds().l;

				// Move the tab into the new container.
				EsSyscall(ES_SYSCALL_WINDOW_TRANSFER_PRESS, tab->window->handle, hoverWindow->handle, 0, 0);
				EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, tab->window->handle, 0, 0, ES_WINDOW_PROPERTY_EMBED);
				WindowTab *newTab = WindowTabMoveToNewContainer(tab, hoverContainer, 0, 0);

				// Setup the drag in the new container.
				// TODO Sometimes the tab ends up a few pixels off?
				newTab->window->pressed = newTab;
				newTab->window->dragged = newTab;
				newTab->dragOffset = dragOffset + 2 * hoverContainer->tabBand->style->insets.l;
				newTab->dragging = true;
			}
		} else {
			EsPoint mousePosition = EsMouseGetPosition(tab->window);
			int32_t dragOffThreshold = GetConstantNumber("tabDragOffThreshold");
			// int32_t previousTabOffsetX = tab->offsetX;
			int32_t previousTabOffsetX = tab->dragPosition;

			if (EsRectangleContains(EsRectangleAdd(band->GetWindowBounds(), ES_RECT_1I(-dragOffThreshold)), mousePosition.x, mousePosition.y)) {
				ReorderItemDragged(tab, message->mouseDragged.newPositionX);
			} else {
				// TODO Moving a tab directly from one container to another.

				// If we dragged the tab off the left or right side of the band, put it at the start of the new tab band.
				bool putAtStart = tab->dragPosition < band->style->insets.l 
					|| tab->dragPosition + tab->width > band->width - band->style->insets.r;
				int32_t putAtStartClickX = band->style->insets.l + tab->dragOffset;

				// End the drag on this container.
				EsMessage m = { .type = ES_MSG_MOUSE_LEFT_UP };
				UIMouseUp(band->window, &m, false);

				// Move the tab to a new container.
				WindowTab *newTab = WindowTabMoveToNewContainer(tab, nullptr, band->window->width, band->window->height);

				if (newTab) {
					// Transfer the drag to the new container.
					EsSyscall(ES_SYSCALL_WINDOW_TRANSFER_PRESS, band->window->handle, newTab->window->handle, 0, 0);
					ReorderItemDragged(newTab, 0);
					newTab->dragPosition = putAtStart ? band->style->insets.l : previousTabOffsetX;
					newTab->window->pressed = newTab;
					newTab->window->dragged = newTab;
					gui.lastClickX = putAtStart ? putAtStartClickX : mousePosition.x;
					gui.mouseButtonDown = true;
					gui.draggingStarted = true;

					// Update the bounds of the new container.
					EsPoint mousePositionOnScreen = EsMouseGetPosition();
					WindowChangeBounds(RESIZE_MOVE, mousePositionOnScreen.x, mousePositionOnScreen.y, &gui.lastClickX, &gui.lastClickY, newTab->window);
				}
			}
		}
	} else if (message->type == ES_MSG_MOUSE_LEFT_UP) {
		ReorderItemDragComplete(tab);
		EsElementSetDisabled(band->GetChild(0), false);
	} else if (message->type == ES_MSG_MOUSE_RIGHT_CLICK) {
		EsMenu *menu = EsMenuCreate(tab, ES_FLAGS_DEFAULT);
		uint64_t disableIfOnlyTab = tab->container->tabBand->items.Length() == 1 ? ES_ELEMENT_DISABLED : ES_FLAGS_DEFAULT;

		EsMenuAddItem(menu, ES_FLAGS_DEFAULT, INTERFACE_STRING(DesktopCloseTab), [] (EsMenu *, EsGeneric context) {
			WindowTabClose((WindowTab *) context.p);
		}, tab);

		EsMenuAddItem(menu, disableIfOnlyTab, INTERFACE_STRING(DesktopMoveTabToNewWindow), [] (EsMenu *, EsGeneric context) {
			WindowTabMoveToNewContainer((WindowTab *) context.p, nullptr, 0, 0);
		}, tab);

		EsMenuAddItem(menu, disableIfOnlyTab, INTERFACE_STRING(DesktopMoveTabToNewWindowSplitLeft), [] (EsMenu *, EsGeneric context) {
			WindowTab *oldTab = (WindowTab *) context.p;
			EsWindow *oldWindow = oldTab->window;
			WindowTab *newTab = WindowTabMoveToNewContainer(oldTab, nullptr, 0, 0);
			if (!newTab) return;
			if (oldWindow->isMaximised) WindowRestore(oldWindow, false);
			WindowSnap(oldWindow, false, false, SNAP_EDGE_RIGHT);
			WindowSnap(newTab->window, false, false, SNAP_EDGE_LEFT);
		}, tab);

		EsMenuAddItem(menu, disableIfOnlyTab, INTERFACE_STRING(DesktopMoveTabToNewWindowSplitRight), [] (EsMenu *, EsGeneric context) {
			WindowTab *oldTab = (WindowTab *) context.p;
			EsWindow *oldWindow = oldTab->window;
			WindowTab *newTab = WindowTabMoveToNewContainer(oldTab, nullptr, 0, 0);
			if (!newTab) return;
			if (oldWindow->isMaximised) WindowRestore(oldWindow, false /* we immediately move the window after this */);
			WindowSnap(oldWindow, false, false, SNAP_EDGE_LEFT);
			WindowSnap(newTab->window, false, false, SNAP_EDGE_RIGHT);
		}, tab);

		if (EsKeyboardIsShiftHeld()) {
			EsMenuAddSeparator(menu);

			EsMenuAddItem(menu, ES_FLAGS_DEFAULT, INTERFACE_STRING(DesktopInspectUI), [] (EsMenu *, EsGeneric context) {
				WindowTab *tab = (WindowTab *) context.p;
				ApplicationInstance *instance = tab->applicationInstance;
				EsMessage m = { ES_MSG_TAB_INSPECT_UI };
				m.tabOperation.id = instance->embeddedWindowID;
				EsMessagePostRemote(instance->process->handle, &m);
			}, tab);
		}

		EsMenuShow(menu);
	} else if (message->type == ES_MSG_MOUSE_MIDDLE_UP && ((element->state & UI_STATE_HOVERED) || (tab->closeButton->state & UI_STATE_HOVERED))) {
		if (EsButtonGetCheck(tab->closeButton) == ES_CHECK_CHECKED) {
			// The tab contains a modified document, so it will probably popup a dialog after it receives the close request.
			// Therefore, we should switch to that tab.
			tab->BringToFront();
			WindowTabActivate(tab);
		}

		WindowTabClose(tab);
	} else if (message->type == ES_MSG_REORDER_ITEM_TEST) {
	} else {
		return 0;
	}

	return ES_HANDLED;
}

WindowTab *WindowTabCreate(ContainerWindow *container) {
	WindowTab *tab = (WindowTab *) EsHeapAllocate(sizeof(WindowTab), true);
	tab->container = container;
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

		int x = ReorderListLayout(band, band->GetChild(0)->style->preferredWidth + 10 * theming.scale, 
				true, band->preventNextTabSizeAnimation);
		band->GetChild(0)->InternalMove(band->GetChild(0)->style->preferredWidth, 
				band->GetChild(0)->style->preferredHeight, x + 10 * theming.scale, 4 * theming.scale);
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

		EsMenuAddItem(menu, ES_FLAGS_DEFAULT, 
				band->items.Length() > 1 ? interfaceString_DesktopCloseAllTabs : interfaceString_DesktopCloseWindow, -1, 
				[] (EsMenu *, EsGeneric context) {
			WindowTabBand *band = (WindowTabBand *) context.p;

			for (uintptr_t i = 0; i < band->items.Length(); i++) {
				WindowTabClose((WindowTab *) band->items[i]);
			}
		}, band);

		EsMenuAddSeparator(menu);

		EsMenuAddItem(menu, band->window->isMaximised ? ES_ELEMENT_DISABLED : ES_FLAGS_DEFAULT,
				INTERFACE_STRING(DesktopMaximiseWindow), [] (EsMenu *, EsGeneric context) {
			WindowSnap((EsWindow *) context.p, false, false, SNAP_EDGE_MAXIMIZE);
		}, band->window);

		EsMenuAddItem(menu, band->window->isMaximised ? ES_ELEMENT_DISABLED : ES_FLAGS_DEFAULT,
				INTERFACE_STRING(DesktopMinimiseWindow), [] (EsMenu *, EsGeneric context) {
			EsSyscall(ES_SYSCALL_WINDOW_MOVE, ((EsWindow *) context.p)->handle, 0, 0, ES_WINDOW_MOVE_HIDDEN);
		}, band->window);

		EsMenuAddItem(menu, !band->window->isMaximised ? ES_ELEMENT_DISABLED : ES_FLAGS_DEFAULT,
				INTERFACE_STRING(DesktopRestoreWindow), [] (EsMenu *, EsGeneric context) {
			WindowRestore((EsWindow *) context.p);
		}, band->window);

		EsMenuAddSeparator(menu);

		EsMenuAddItem(menu, band->window->isMaximised ? ES_ELEMENT_DISABLED : ES_FLAGS_DEFAULT, 
				INTERFACE_STRING(DesktopCenterWindow), [] (EsMenu *, EsGeneric context) {
			WindowTabBand *band = (WindowTabBand *) context.p;
			EsRectangle workArea;
			EsSyscall(ES_SYSCALL_SCREEN_WORK_AREA_GET, 0, (uintptr_t) &workArea, 0, 0);
			EsRectangle newBounds = EsRectangleCenter(workArea, EsWindowGetBounds(band->window));
			newBounds.t -= 8, newBounds.b -= 8; // Because of the shadow, it looks a little better to be slightly above center :)
			EsSyscall(ES_SYSCALL_WINDOW_MOVE, band->window->handle, (uintptr_t) &newBounds, 0, ES_FLAGS_DEFAULT);
		}, band);

		EsMenuAddItem(menu, ES_FLAGS_DEFAULT,
				INTERFACE_STRING(DesktopSnapWindowLeft), [] (EsMenu *, EsGeneric context) {
			EsWindow *window = (EsWindow *) context.p;
			if (window->isMaximised) WindowRestore(window, false /* we immediately move the window after this */);
			WindowSnap(window, false, false, SNAP_EDGE_LEFT);
		}, band->window);

		EsMenuAddItem(menu, ES_FLAGS_DEFAULT,
				INTERFACE_STRING(DesktopSnapWindowRight), [] (EsMenu *, EsGeneric context) {
			EsWindow *window = (EsWindow *) context.p;
			if (window->isMaximised) WindowRestore(window, false /* we immediately move the window after this */);
			WindowSnap(window, false, false, SNAP_EDGE_RIGHT);
		}, band->window);

		EsMenuShow(menu);
	} else {
		return ReorderListMessage(band, message);
	}

	return ES_HANDLED;
}

void ContainerWindowShow(ContainerWindow *container, int32_t width, int32_t height) {
	EsWindow *window = container->window;

	window->windowWidth = width ?: GetConstantNumber("windowDefaultWidth");
	window->windowHeight = height ?: GetConstantNumber("windowDefaultHeight");

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

	EsSyscall(ES_SYSCALL_WINDOW_MOVE, window->handle, (uintptr_t) &bounds, 0, ES_WINDOW_MOVE_DYNAMIC);
	EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, 0, 0, ES_WINDOW_PROPERTY_FOCUSED);
}

ContainerWindow *ContainerWindowCreate() {
	ContainerWindow *container = (ContainerWindow *) EsHeapAllocate(sizeof(ContainerWindow), true);
	EsWindow *window = EsWindowCreate(nullptr, ES_WINDOW_CONTAINER);
	desktop.allContainerWindows.Add(container);

	window->messageUser = ContainerWindowMessage;
	window->userData = container;
	
	window->mainPanel = EsPanelCreate(window, ES_ELEMENT_NON_CLIENT | ES_CELL_FILL, ES_STYLE_PANEL_CONTAINER_WINDOW_ROOT);
	window->SetStyle(ES_STYLE_CONTAINER_WINDOW);

	EsMessage m = { .type = ES_MSG_UI_SCALE_CHANGED };
	EsMessageSend(window, &m);

	container->window = window;

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

void TaskBarTasksButtonUpdate() {
	if (desktop.allOngoingUserTasks.Length()) {
		if (EsElementIsHidden(desktop.tasksButton)) {
			EsPanelStartMovementAnimation((EsPanel *) EsElementGetLayoutParent(desktop.tasksButton), 1.5f /* duration scale */);
			EsElementStartTransition(desktop.tasksButton, ES_TRANSITION_FADE_IN, ES_FLAGS_DEFAULT, 1.5f);
			EsElementSetHidden(desktop.tasksButton, false);
		}

		// TODO Maybe grow button.
	} else {
		// TODO Maybe shrink or hide button.
		//	Currently, this always hides it, but I don't think this is a good behaviour.

		if (!EsElementIsHidden(desktop.tasksButton)) {
			EsPanelStartMovementAnimation((EsPanel *) EsElementGetLayoutParent(desktop.tasksButton), 1.5f /* duration scale */);
			EsElementStartTransition(desktop.tasksButton, ES_TRANSITION_FADE_OUT, ES_ELEMENT_TRANSITION_HIDE_AFTER_COMPLETE, 1.5f);
		}
	}

	EsElementRepaint(desktop.tasksButton);
}

int TaskBarTasksButtonMessage(EsElement *element, EsMessage *message) {
	if (message->type == ES_MSG_GET_WIDTH) {
		message->measure.width = GetConstantNumber("taskBarTasksButtonWidth");
	} else if (message->type == ES_MSG_PAINT) {
		char title[256];
		size_t titleBytes;

		if (desktop.allOngoingUserTasks.Length() > 1) {
			// TODO Localization.
			titleBytes = EsStringFormat(title, sizeof(title), "%d tasks" ELLIPSIS, desktop.allOngoingUserTasks.Length());
		} else if (desktop.allOngoingUserTasks.Length() == 1) {
			titleBytes = EsStringFormat(title, sizeof(title), "%s", desktop.allOngoingUserTasks.First()->titleBytes, desktop.allOngoingUserTasks.First()->title);
		} else {
			titleBytes = 0;
		}

		EsDrawContent(message->painter, element, ES_RECT_2S(message->painter->width, message->painter->height), title, titleBytes);
	} else if (message->type == ES_MSG_PAINT_ICON) {
		double progress = !desktop.allOngoingUserTasks.Length() ? 1.0 : desktop.totalUserTaskProgress / desktop.allOngoingUserTasks.Length();

		if (progress > 0.0 && progress <= 0.05) {
			progress = 0.05; // Really small angles look strange; avoid them.
		} else if (progress >= 0.9 && progress < 1.0) {
			progress = (progress - 0.9) * 0.5 + 0.9; // Approach 95% from 90% at half speed to account for bad progress calculations.
		}

		uint32_t color1 = GetConstantNumber("taskBarTasksButtonWheelColor1");
		uint32_t color2 = GetConstantNumber("taskBarTasksButtonWheelColor2");

		EsPainter *painter = message->painter;

		EsRectangle destination = EsPainterBoundsClient(painter);
		destination = EsRectangleFit(destination, ES_RECT_2S(1, 1), true); // Center with a 1:1 aspect ratio.

		RastSurface surface = {};
		surface.buffer = (uint32_t *) painter->target->bits;
		surface.stride = painter->target->stride;

		if (RastSurfaceInitialise(&surface, painter->target->width, painter->target->height, true)) {
			RastVertex center = { (destination.l + destination.r) * 0.5f, (destination.t + destination.b) * 0.5f };

			RastContourStyle style = {};
			style.internalWidth = 5.0f * theming.scale;
			style.capMode = RAST_LINE_CAP_FLAT;

			RastPaint paint = {};
			paint.type = RAST_PAINT_SOLID;

			{
				paint.solid.color = color1 & 0xFFFFFF;
				paint.solid.alpha = (color1 >> 24) / 255.0f;

				RastPath path = {};
				RastPathAppendArc(&path, center, Width(destination) * 0.45f, ES_PI * 2.0f, 0.0f);
				RastShape shape = RastShapeCreateContour(&path, style, true);
				RastSurfaceFill(surface, shape, paint, false);
				RastPathDestroy(&path);
			}

			{
				paint.solid.color = color2 & 0xFFFFFF;
				paint.solid.alpha = (color2 >> 24) / 255.0f;

				RastPath path = {};
				RastPathAppendArc(&path, center, Width(destination) * 0.45f, ES_PI * 1.5f + progress * ES_PI * 2.0f, ES_PI * 1.5f);
				RastShape shape = RastShapeCreateContour(&path, style, true);
				RastSurfaceFill(surface, shape, paint, false);
				RastPathDestroy(&path);
			}
		}

		RastSurfaceDestroy(&surface);
	} else {
		return 0;
	}

	return ES_HANDLED;
}

void Shutdown(uintptr_t action) {
	// TODO This doesn't wait for Desktop instances.

	if (desktop.inShutdown) {
		return;
	}

	if (!desktop.allApplicationProcesses.Length()) {
		// No applications are open, so we can shut down immediately.
		EsSyscall(ES_SYSCALL_SHUTDOWN, action, 0, 0, 0);
	}

	desktop.inShutdown = true;
	desktop.shutdownReady = EsEventCreate(true);

	for (uintptr_t i = 0; i < desktop.allApplicationInstances.Length(); i++) {
		// Tell all applications to close.
		ApplicationInstanceClose(desktop.allApplicationInstances[i]);
	}

	EsThreadCreate([] (EsGeneric action) {
		// Shut down either when all applications exit, or after the timeout.
		EsWait(&desktop.shutdownReady, 1, SHUTDOWN_TIMEOUT); 
		EsSyscall(ES_SYSCALL_SHUTDOWN, action.u, 0, 0, 0);
	}, nullptr, action);
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
	// EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, window->handle, BLEND_WINDOW_MATERIAL_LIGHT_BLUR, 0, ES_WINDOW_PROPERTY_MATERIAL);

	// Setup the UI.

	EsPanel *stack = EsPanelCreate(window, ES_CELL_FILL | ES_PANEL_Z_STACK);
	stack->cName = "window stack";
	EsPanelCreate(stack, ES_CELL_FILL, ES_STYLE_PANEL_SHUTDOWN_OVERLAY)->cName = "modal overlay";
	EsPanel *dialog = EsPanelCreate(stack, ES_PANEL_VERTICAL | ES_CELL_CENTER, ES_STYLE_DIALOG_SHADOW);
	dialog->cName = "dialog";
	EsPanel *heading = EsPanelCreate(dialog, ES_PANEL_HORIZONTAL | ES_CELL_H_FILL, ES_STYLE_DIALOG_HEADING);
	EsIconDisplayCreate(heading, ES_FLAGS_DEFAULT, {}, ES_ICON_SYSTEM_SHUTDOWN);
	EsTextDisplayCreate(heading, ES_CELL_H_FILL | ES_CELL_V_CENTER, ES_STYLE_TEXT_HEADING2, 
			INTERFACE_STRING(DesktopShutdownTitle))->cName = "dialog heading";
	EsTextDisplayCreate(EsPanelCreate(dialog, ES_PANEL_VERTICAL | ES_CELL_H_FILL, ES_STYLE_DIALOG_CONTENT), 
			ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(DesktopConfirmShutdown))->cName = "dialog contents";
	EsPanel *buttonArea = EsPanelCreate(dialog, ES_PANEL_HORIZONTAL | ES_PANEL_REVERSE | ES_CELL_H_FILL, ES_STYLE_DIALOG_BUTTON_AREA);
	EsButton *cancelButton = EsButtonCreate(buttonArea, ES_BUTTON_DEFAULT | ES_BUTTON_CANCEL, 0, INTERFACE_STRING(CommonCancel));
	EsButton *restartButton = EsButtonCreate(buttonArea, ES_FLAGS_DEFAULT, ES_STYLE_PUSH_BUTTON_DANGEROUS, INTERFACE_STRING(DesktopRestartAction));
	EsButton *shutdownButton = EsButtonCreate(buttonArea, ES_FLAGS_DEFAULT, ES_STYLE_PUSH_BUTTON_DANGEROUS, INTERFACE_STRING(DesktopShutdownAction));
	EsElementFocus(cancelButton);

	// Setup command callbacks when the buttons are pressed.

	EsButtonOnCommand(shutdownButton, [] (EsInstance *, EsElement *, EsCommand *) {
		Shutdown(SHUTDOWN_ACTION_POWER_OFF);
	});

	EsButtonOnCommand(restartButton, [] (EsInstance *, EsElement *, EsCommand *) {
		Shutdown(SHUTDOWN_ACTION_RESTART);
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
	for (uintptr_t i = 0; i < desktop.allApplicationInstances.Length(); i++) {
		ApplicationInstance *instance = desktop.allApplicationInstances[i];

		if (instance->tab && instance->tab->notRespondingInstance && instance->tab->notRespondingInstance->embeddedWindowID == element->window->id) {
			EsProcessTerminate(instance->process->handle, 1);
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

		EsButton *button = EsButtonCreate(buttonGroup, ES_CELL_H_FILL | ES_ELEMENT_NO_FOCUS_ON_CLICK, ES_STYLE_BUTTON_GROUP_ITEM, application->cName);
		button->userData = application;

		if (application->iconID) {
			EsButtonSetIcon(button, (EsStandardIcon) application->iconID);
		} else {
			EsButtonSetIcon(button, ES_ICON_APPLICATION_DEFAULT_ICON);

			// TODO Load the icon asynchronously.
			// TODO Load the correct icon size.
			// TODO Reload the icon if the UI scale factor changes.
			// TODO Cache the icon bits.
			// TODO Generic icon and thumbnail cache in the API, based off the one from File Manager?

			size_t fileBytes;
			void *file = EsFileMap(application->cExecutable, -1, &fileBytes, ES_MAP_OBJECT_READ_ONLY);
			EsBundle bundle = { .base = (const BundleHeader *) file, .bytes = (ptrdiff_t) fileBytes };

			if (file) {
				size_t icon32Bytes;
				const void *icon32 = EsBundleFind(&bundle, EsLiteral("$Icons/32"), &icon32Bytes);

				if (icon32) {
					uint32_t width, height;
					uint32_t *bits = (uint32_t *) EsImageLoad(icon32, icon32Bytes, &width, &height, 4);

					if (bits) {
						EsButtonSetIconFromBits(button, bits, width, height, width * 4);
						EsHeapFree(bits);
					}
				}

				EsObjectUnmap(file);
			}
		}

		EsButtonOnCommand(button, [] (EsInstance *, EsElement *element, EsCommand *) {
			ApplicationInstance *instance = ApplicationInstanceFindByWindowID(element->window->id);

			if (ApplicationInstanceStart(((InstalledApplication *) element->userData.p)->id, nullptr, instance)) {
				WindowTabActivate(instance->tab, true);
				EsInstanceDestroy(element->instance);
			}
		});
	}
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
	EsMessagePostRemote(instance->process->handle, &m);
}

void ApplicationInstanceCleanup(ApplicationInstance *instance) {
	if (instance->documentID) {
		OpenDocumentCloseReference(instance->documentID);
		instance->documentID = 0;
	}

	InstalledApplication *application = instance->application;

	if (application && application->singleInstance == instance) {
		application->singleInstance = nullptr;
	}

	if (instance->process) {
		EsAssert(instance->process->instanceCount);
		instance->process->instanceCount--;

		if (!instance->process->instanceCount) {
			EsMessage m = { ES_MSG_APPLICATION_EXIT };
			EsMessagePostRemote(instance->process->handle, &m);
		}
	}

	instance->process = nullptr;
	instance->application = nullptr;
}

void PathGetNameAndContainingFolder(const char *path, ptrdiff_t pathBytes,
				const char **name, ptrdiff_t *nameBytes,
				const char **containingFolder, ptrdiff_t *containingFolderBytes,
				EsVolumeInformation *volumeInformation /* needs to be allocated outside */) {
	if (pathBytes == -1) {
		pathBytes = EsCStringLength(path);
	}

	*name = path;
	*nameBytes = pathBytes;
	*containingFolderBytes = 0;

	const char *containingFolderEnd = nullptr;

	for (uintptr_t i = pathBytes; i > 0; i--) {
		if (path[i - 1] == '/') {
			if (!containingFolderEnd) {
				containingFolderEnd = path + i - 1;
				*name = path + i;
				*nameBytes = pathBytes - i;
			} else {
				*containingFolder = path + i;
				*containingFolderBytes = containingFolderEnd - *containingFolder;
			}
		}
	}

	// TODO Get the user name of the folder from File Manager?
	// 	Maybe more of File Manager's code needs to be shared with Desktop..?

	if (pathBytes && !(*containingFolderBytes)) {
		if (EsMountPointGetVolumeInformation(path, pathBytes, volumeInformation)) {
			*containingFolder = volumeInformation->label;
			*containingFolderBytes = volumeInformation->labelBytes;
		}
	}
}

void *OpenDocumentGetRenameMessageData(const char *path, size_t pathBytes, size_t *bytes) {
	EsVolumeInformation volumeInformation;
	const char *name, *containingFolder;
	ptrdiff_t nameBytes, containingFolderBytes;
	PathGetNameAndContainingFolder(path, pathBytes, &name, &nameBytes, &containingFolder, &containingFolderBytes, &volumeInformation);
	*bytes = sizeof(ptrdiff_t) * 2 + nameBytes + containingFolderBytes;
	uint8_t *data = (uint8_t *) EsHeapAllocate(*bytes, false);
	EsMemoryCopy(data, &nameBytes, sizeof(ptrdiff_t));
	EsMemoryCopy(data + sizeof(ptrdiff_t), &containingFolderBytes, sizeof(ptrdiff_t));
	EsMemoryCopy(data + sizeof(ptrdiff_t) * 2, name, nameBytes);
	EsMemoryCopy(data + sizeof(ptrdiff_t) * 2 + nameBytes, containingFolder, containingFolderBytes);
	return data;
}

bool ApplicationInstanceStart(int64_t applicationID, _EsApplicationStartupInformation *startupInformation, ApplicationInstance *instance) {
	if (desktop.inShutdown) {
		return false;
	}
	
	InstalledApplication *application = nullptr;

	for (uintptr_t i = 0; i < desktop.installedApplications.Length(); i++) {
		if (desktop.installedApplications[i]->id == applicationID) {
			application = desktop.installedApplications[i];
			break;
		}
	}

	if (application && application->useSingleInstance && application->singleInstance) {
		WindowTabActivate(application->singleInstance->tab);
		EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, application->singleInstance->tab->window->handle, 0, 0, ES_WINDOW_PROPERTY_FOCUSED);
		return false;
	}

	if (!application) {
		_EsApplicationStartupInformation s = {};
		s.data = CRASHED_TAB_PROGRAM_NOT_FOUND;
		return ApplicationInstanceStart(APPLICATION_ID_DESKTOP_CRASHED, &s, instance);
	}

	_EsApplicationStartupInformation _startupInformation = {};
	
	if (!startupInformation) {
		startupInformation = &_startupInformation;
	}

	if (instance->tab) {
		EsButtonSetCheck(instance->tab->closeButton, ES_CHECK_UNCHECKED, false);
	}

	if (instance->tab && instance->tab->notRespondingInstance) {
		ApplicationInstanceClose(instance->tab->notRespondingInstance);
		instance->tab->notRespondingInstance = nullptr;
	}

	ApplicationProcess *process = application->singleProcess;

	if (application->createInstance) {
		EsObjectID desktopProcessID = EsProcessGetID(ES_CURRENT_PROCESS);

		for (uintptr_t i = 0; i < desktop.allApplicationProcesses.Length(); i++) {
			if (desktop.allApplicationProcesses[i]->id == desktopProcessID) {
				process = desktop.allApplicationProcesses[i];
				break;
			}
		}

		if (!process) {
			process = (ApplicationProcess *) EsHeapAllocate(sizeof(ApplicationProcess), true);
			process->handle = EsSyscall(ES_SYSCALL_HANDLE_SHARE, ES_CURRENT_PROCESS, ES_CURRENT_PROCESS, 0, 0);
			process->id = desktopProcessID;
			desktop.allApplicationProcesses.Add(process);
		}
	} else if (!process) {
		EsProcessInformation information;
		EsProcessCreationArguments arguments = {};

		_EsNodeInformation executableNode;
		EsError error = NodeOpen(application->cExecutable, EsCStringLength(application->cExecutable), 
				ES_FILE_READ | ES_NODE_FAIL_IF_NOT_FOUND, &executableNode);

		if (ES_CHECK_ERROR(error)) {
			_EsApplicationStartupInformation s = {};
			s.data = CRASHED_TAB_INVALID_EXECUTABLE;
			return ApplicationInstanceStart(APPLICATION_ID_DESKTOP_CRASHED, &s, instance);
		}

		arguments.executable = executableNode.handle;
		arguments.permissions = ES_PERMISSION_WINDOW_MANAGER;

		Array<EsMountPoint> initialMountPoints = {};
		Array<EsMessageDevice> initialDevices = {};
		Array<EsHandle> handleDuplicateList = {};
		Array<uint32_t> handleModeDuplicateList = {};
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
			root.prefixBytes = EsStringFormat(root.prefix, sizeof(root.prefix), "|POSIX:");
			initialMountPoints.Add(root);

			handleDuplicateList.Add(root.base);
			handleModeDuplicateList.Add(0);
		}

		if (application->permissions & APPLICATION_PERMISSION_SHUTDOWN) {
			arguments.permissions |= ES_PERMISSION_SHUTDOWN;
		}

		if (application->permissions & APPLICATION_PERMISSION_ALL_FILES) {
			for (uintptr_t i = 0; i < api.mountPoints.Length(); i++) {
				initialMountPoints.Add(api.mountPoints[i]);
				handleDuplicateList.Add(api.mountPoints[i].base);
				handleModeDuplicateList.Add(0);
			}

			arguments.permissions |= ES_PERMISSION_GET_VOLUME_INFORMATION;
		} else {
			MountPoint fonts = *NodeFindMountPoint(EsLiteral("|Fonts:"));
			initialMountPoints.Add(fonts);
			handleDuplicateList.Add(fonts.base);
			handleModeDuplicateList.Add(2 /* prevent write */);
		}

		if (application->permissions & APPLICATION_PERMISSION_ALL_DEVICES) {
			for (uintptr_t i = 0; i < api.connectedDevices.Length(); i++) {
				initialDevices.Add(api.connectedDevices[i]);
				handleDuplicateList.Add(api.connectedDevices[i].handle);
				handleModeDuplicateList.Add(0);
			}
		}

		{
			error = NodeOpen(application->settingsPath, application->settingsPathBytes, 
					ES_NODE_DIRECTORY | ES_NODE_CREATE_DIRECTORIES | _ES_NODE_DIRECTORY_WRITE, &settingsNode);

			if (error == ES_SUCCESS) {
				EsMountPoint settings = {};
				settings.prefixBytes = EsStringFormat(settings.prefix, sizeof(settings.prefix), "|Settings:");
				settings.base = settingsNode.handle;
				initialMountPoints.Add(settings);

				handleDuplicateList.Add(settings.base);
				handleModeDuplicateList.Add(0);
			} else {
				settingsNode.handle = ES_INVALID_HANDLE;
			}
		}

		arguments.data.initialMountPoints = EsConstantBufferCreate(initialMountPoints.array, initialMountPoints.Length() * sizeof(EsMountPoint), ES_CURRENT_PROCESS);
		handleDuplicateList.Add(arguments.data.initialMountPoints);
		handleModeDuplicateList.Add(0);

		arguments.data.initialDevices = EsConstantBufferCreate(initialDevices.array, initialDevices.Length() * sizeof(EsMessageDevice), ES_CURRENT_PROCESS);
		handleDuplicateList.Add(arguments.data.initialDevices);
		handleModeDuplicateList.Add(0);

		arguments.handles = handleDuplicateList.array;
		arguments.handleModes = handleModeDuplicateList.array;
		arguments.handleCount = handleDuplicateList.Length();
		EsAssert(handleDuplicateList.Length() == handleModeDuplicateList.Length());

		error = EsProcessCreate(&arguments, &information); 
		EsHandleClose(arguments.executable);

		initialMountPoints.Free();
		initialDevices.Free();
		handleDuplicateList.Free();
		handleModeDuplicateList.Free();

		if (settingsNode.handle) {
			EsHandleClose(settingsNode.handle);
		}

		if (arguments.data.initialMountPoints) {
			EsHandleClose(arguments.data.initialMountPoints);
		}

		if (arguments.data.initialDevices) {
			EsHandleClose(arguments.data.initialDevices);
		}

		if (!ES_CHECK_ERROR(error)) {
			EsHandleClose(information.mainThread.handle);

			process = (ApplicationProcess *) EsHeapAllocate(sizeof(ApplicationProcess), true);
			process->handle = information.handle;
			process->id = information.pid;
			process->application = application;
			desktop.allApplicationProcesses.Add(process);
		} else {
			_EsApplicationStartupInformation s = {};
			s.data = CRASHED_TAB_INVALID_EXECUTABLE;
			return ApplicationInstanceStart(APPLICATION_ID_DESKTOP_CRASHED, &s, instance);
		}
	}

	if (application->useSingleProcess) {
		application->singleProcess = process;
	}

	// Increment the instance count before cleaning up the old process,
	// so that when going between 2 Desktop instances,
	// the Desktop process doesn't exit.
	process->instanceCount++;

	ApplicationInstanceCleanup(instance);
	instance->application = application;
	instance->process = process;

	if (startupInformation->documentID) {
		instance->documentID = startupInformation->documentID;
		OpenDocumentOpenReference(instance->documentID);
	}

	EsVolumeInformation volumeInformation;

	EsMessage m = { ES_MSG_INSTANCE_CREATE };

	if (~startupInformation->flags & ES_APPLICATION_STARTUP_MANUAL_PATH) {
		PathGetNameAndContainingFolder(startupInformation->filePath, startupInformation->filePathBytes,
				&startupInformation->filePath, &startupInformation->filePathBytes,
				&startupInformation->containingFolder, &startupInformation->containingFolderBytes,
				&volumeInformation);
	}

	// Share handles to the file and the startup information buffer.

	if (startupInformation->readHandle) {
		startupInformation->readHandle = EsSyscall(ES_SYSCALL_HANDLE_SHARE, startupInformation->readHandle, process->handle, 0, 0);
	}

	uint8_t *createInstanceDataBuffer = ApplicationStartupInformationToBuffer(startupInformation, &m.createInstance.dataBytes);
	m.createInstance.data = EsConstantBufferCreate(createInstanceDataBuffer, m.createInstance.dataBytes, process->handle);
	EsHeapFree(createInstanceDataBuffer);

	EsHandle handle = EsSyscall(ES_SYSCALL_WINDOW_CREATE, ES_WINDOW_NORMAL, 0, 0, 0);
	instance->embeddedWindowHandle = handle;
	instance->embeddedWindowID = EsSyscall(ES_SYSCALL_WINDOW_GET_ID, handle, 0, 0, 0);
	m.createInstance.window = EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, handle, process->handle, 0, ES_WINDOW_PROPERTY_EMBED_OWNER);

	if (application->createInstance) {
		application->createInstance(&m);
	} else {
		EsMessagePostRemote(process->handle, &m);
	}

	if (application->useSingleInstance) {
		application->singleInstance = instance;
	}

	return true;
}

ApplicationInstance *ApplicationInstanceCreate(int64_t id, _EsApplicationStartupInformation *startupInformation, ContainerWindow *container, bool hidden) {
	ApplicationInstance *instance = (ApplicationInstance *) EsHeapAllocate(sizeof(ApplicationInstance), true);
	WindowTab *tab = !hidden ? WindowTabCreate(container ?: ContainerWindowCreate()) : nullptr;
	if (tab) tab->applicationInstance = instance;
	instance->title[0] = ' ';
	instance->titleBytes = 1;
	instance->tab = tab;
	desktop.allApplicationInstances.Add(instance);

	if (ApplicationInstanceStart(id, startupInformation, instance)) {
		if (!hidden) {
			WindowTabActivate(tab);
			if (!container) ContainerWindowShow(tab->container, 0, 0);
		}

		return instance;
	} else {
		if (!hidden) WindowTabDestroy(tab); // TODO Test this.
		EsHeapFree(instance);
		return nullptr;
	}
}

void ApplicationTemporaryDestroy(InstalledApplication *application) {
	if (!application->temporary) return;

	for (uintptr_t i = 0; i < desktop.installedApplications.Length(); i++) {
		if (desktop.installedApplications[i] == application) {
			desktop.installedApplications.Delete(i);
			EsHeapFree(application->cName);
			EsHeapFree(application->cExecutable);
			EsHeapFree(application->settingsPath);
			EsHeapFree(application);
			// TODO Delete the settings folder.
			return;
		}
	}

	EsAssert(false);
}

ApplicationProcess *ApplicationProcessFindByPID(EsObjectID pid, bool removeIfFound = false) {
	for (uintptr_t i = 0; i < desktop.allApplicationProcesses.Length(); i++) {
		ApplicationProcess *process = desktop.allApplicationProcesses[i];

		if (process->id == pid) {
			if (removeIfFound) {
				desktop.allApplicationProcesses.DeleteSwap(i);
			}

			return process;
		}
	}

	return nullptr;
}

InstalledApplication *ApplicationFindByPID(EsObjectID pid) {
	ApplicationProcess *process = ApplicationProcessFindByPID(pid);
	return process ? process->application : nullptr;
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

	InstalledApplication *application = ApplicationFindByPID(message->crash.pid);

	if (application) {
		if (application->singleProcess && application->singleProcess->id == message->crash.pid) {
			application->singleProcess = nullptr;
		}

		application->singleInstance = nullptr;

		if (desktop.installationState == INSTALLATION_STATE_INSTALLER && desktop.installer == application) {
			// Restart the installer.
			ApplicationInstanceCreate(desktop.installer->id, nullptr, nullptr, true /* hidden */);
		}
	}

	for (uintptr_t i = 0; i < desktop.allApplicationInstances.Length(); i++) {
		ApplicationInstance *instance = desktop.allApplicationInstances[i];

		if (instance->process->id == message->crash.pid) {
			if (instance->tab) {
				ApplicationInstanceStart(APPLICATION_ID_DESKTOP_CRASHED, nullptr, instance);
				WindowTabActivate(instance->tab, true);
			}
		}
	}

	EsProcessTerminate(processHandle, 1); 
	EsHandleClose(processHandle);
}

void ApplicationProcessTerminated(EsObjectID pid) {
	for (uintptr_t i = 0; i < desktop.allApplicationInstances.Length(); i++) {
		ApplicationInstance *instance = desktop.allApplicationInstances[i];

		if (instance->process->id == pid) {
			EmbeddedWindowDestroyed(instance->embeddedWindowID);
			i--; // EmbeddedWindowDestroyed removes it from the array.
		}
	}

	ApplicationProcess *process = ApplicationProcessFindByPID(pid, true /* remove from array */);

	if (process) {
		InstalledApplication *application = process->application;

		if (application) {
			if (application->singleProcess && application->singleProcess->id == pid) {
				application->singleProcess = nullptr;
			}

			application->singleInstance = nullptr;
			ApplicationTemporaryDestroy(application);
		}

		EsAssert(!process->instanceCount);
		EsHandleClose(process->handle);
		EsHeapFree(process);
	}

	for (uintptr_t i = 0; i < desktop.openDocuments.Count(); i++) {
		OpenDocument *document = &desktop.openDocuments[i];

		if (document->currentWriter == pid) {
			document->currentWriter = 0;
		}
	}

	if (!desktop.allApplicationProcesses.Length() && desktop.inShutdown) {
		EsEventSet(desktop.shutdownReady);
	}
}

//////////////////////////////////////////////////////
// Document management:
//////////////////////////////////////////////////////

void OpenDocumentListUpdated() {
	if (desktop.fileManager && desktop.fileManager->singleProcess) {
		EsMessage m = { .type = ES_MSG_FILE_MANAGER_DOCUMENT_UPDATE };
		EsMessagePostRemote(desktop.fileManager->singleProcess->handle, &m);
	}
}

void OpenDocumentCloseReference(EsObjectID id) {
	OpenDocument *document = desktop.openDocuments.Get(&id);
	EsAssert(document->referenceCount && document->referenceCount < 0x10000000 /* sanity check */);
	document->referenceCount--;
	if (document->referenceCount) return;
	EsHeapFree(document->path);
	EsHeapFree(document->temporarySavePath);
	EsHandleClose(document->readHandle);
	desktop.openDocuments.Delete(&id);
	OpenDocumentListUpdated();
}

void OpenDocumentOpenReference(EsObjectID id) {
	OpenDocument *document = desktop.openDocuments.Get(&id);
	EsAssert(document->referenceCount && document->referenceCount < 0x10000000 /* sanity check */);
	document->referenceCount++;
}

void OpenDocumentWithApplication(EsApplicationStartupRequest *startupRequest) {
	bool foundDocument = false;

	_EsApplicationStartupInformation startupInformation = {};
	startupInformation.id = startupRequest->id;
	startupInformation.flags = startupRequest->flags;
	startupInformation.filePath = startupRequest->filePath;
	startupInformation.filePathBytes = startupRequest->filePathBytes;

	for (uintptr_t i = 0; i < desktop.openDocuments.Count(); i++) {
		OpenDocument *document = &desktop.openDocuments[i];

		if (document->pathBytes == (size_t) startupInformation.filePathBytes
				&& 0 == EsMemoryCompare(document->path, startupInformation.filePath, document->pathBytes)) {
			foundDocument = true;
			startupInformation.readHandle = document->readHandle;
			startupInformation.documentID = document->id;
			document->referenceCount++;
			break;
		}
	}

	if (!foundDocument) {
		EsFileInformation file = EsFileOpen(startupInformation.filePath, startupInformation.filePathBytes, 
				ES_FILE_READ_SHARED | ES_NODE_FAIL_IF_NOT_FOUND);

		if (file.error != ES_SUCCESS) {
			// TODO Report error?
			return;
		}

		OpenDocument document = {};
		document.path = (char *) EsHeapAllocate(startupInformation.filePathBytes, false);
		document.pathBytes = startupInformation.filePathBytes;
		document.readHandle = file.handle;
		document.id = ++desktop.currentDocumentID;
		document.referenceCount = 1;
		EsMemoryCopy(document.path, startupInformation.filePath, startupInformation.filePathBytes);
		*desktop.openDocuments.Put(&document.id) = document;

		startupInformation.readHandle = document.readHandle;
		startupInformation.documentID = document.id;

		OpenDocumentListUpdated();
	}

	ApplicationInstanceCreate(startupInformation.id, &startupInformation, nullptr);
	OpenDocumentCloseReference(startupInformation.documentID);
}

EsError TemporaryFileCreate(EsHandle *handle, char **path, size_t *pathBytes, uint32_t additionalFlags) {
	char temporaryFileName[32];

	for (uintptr_t i = 0; i < sizeof(temporaryFileName); i++) {
		temporaryFileName[i] = (EsRandomU8() % 26) + 'a';
	}

	size_t temporaryFolderBytes;
	char *temporaryFolder = EsSystemConfigurationReadString(EsLiteral("paths"), EsLiteral("temporary"), &temporaryFolderBytes);
	char *temporaryFilePath = (char *) EsHeapAllocate(temporaryFolderBytes + 1 + sizeof(temporaryFileName), false);
	size_t temporaryFilePathBytes = EsStringFormat(temporaryFilePath, ES_STRING_FORMAT_ENOUGH_SPACE, "%s/%s", 
			temporaryFolderBytes, temporaryFolder, sizeof(temporaryFileName), temporaryFileName);

	EsFileInformation file = EsFileOpen(temporaryFilePath, temporaryFilePathBytes, 
			ES_NODE_FAIL_IF_FOUND | ES_NODE_CREATE_DIRECTORIES | additionalFlags);

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

void ApplicationInstanceRequestSave(ApplicationInstance *instance, const char *newName, size_t newNameBytes, bool failIfAlreadyExists) {
	if (!instance->process) return;
	
	EsMessage m = {};
	m.type = ES_MSG_INSTANCE_SAVE_RESPONSE;
	m.tabOperation.id = instance->embeddedWindowID;

	if (!instance->documentID) {
		size_t folderBytes;
		char *folder = EsSystemConfigurationReadString(EsLiteral("paths"), EsLiteral("default_user_documents"), &folderBytes);
		char *name = (char *) EsHeapAllocate(folderBytes + newNameBytes + 32, false);
		EsMemoryCopy(name, folder, folderBytes);
		EsMemoryCopy(name + folderBytes, newName, newNameBytes);
		EsHeapFree(folder);
		size_t nameBytes = EsPathFindUniqueName(name, folderBytes + newNameBytes, folderBytes + newNameBytes + 32);

		if (!nameBytes || (failIfAlreadyExists && nameBytes != folderBytes + newNameBytes)) {
			EsHeapFree(name);
			m.tabOperation.error = nameBytes ? ES_ERROR_FILE_ALREADY_EXISTS : ES_ERROR_TOO_MANY_FILES_WITH_NAME;
			EsMessagePostRemote(instance->process->handle, &m);
			return;
		}

		EsFileInformation file = EsFileOpen(name, nameBytes, ES_FILE_READ_SHARED | ES_NODE_FAIL_IF_FOUND);

		if (file.error != ES_SUCCESS) {
			EsHeapFree(name);
			m.tabOperation.error = file.error;
			EsMessagePostRemote(instance->process->handle, &m);
			return;
		}

		OpenDocument document = {};
		document.path = name;
		document.pathBytes = nameBytes;
		document.readHandle = file.handle;
		document.id = ++desktop.currentDocumentID;
		document.referenceCount++;
		*desktop.openDocuments.Put(&document.id) = document;

		instance->documentID = document.id;

		{
			// Tell the instance the chosen name and new containing folder for the document.
			EsMessage m = { ES_MSG_INSTANCE_DOCUMENT_RENAMED };
			void *data = OpenDocumentGetRenameMessageData(name, nameBytes, &m.tabOperation.bytes);
			m.tabOperation.id = instance->embeddedWindowID;
			m.tabOperation.handle = EsConstantBufferCreate(data, m.tabOperation.bytes, instance->process->handle); 
			EsMessagePostRemote(instance->process->handle, &m);
			EsHeapFree(data);
		}

		OpenDocumentListUpdated();
	}

	OpenDocument *document = desktop.openDocuments.Get(&instance->documentID);

	if (!document) {
		return;
	}

	if (document->currentWriter) {
		m.tabOperation.error = ES_ERROR_FILE_CANNOT_GET_EXCLUSIVE_USE;
	} else {
		EsHeapFree(document->temporarySavePath);
		document->temporarySavePath = nullptr;

		EsHandle fileHandle;
		m.tabOperation.error = TemporaryFileCreate(&fileHandle, &document->temporarySavePath, &document->temporarySavePathBytes, ES_FILE_WRITE);

		if (m.tabOperation.error == ES_SUCCESS) {
			document->currentWriter = instance->embeddedWindowID;
			m.tabOperation.handle = EsSyscall(ES_SYSCALL_HANDLE_SHARE, fileHandle, instance->process->handle, 0, 0);
			EsHandleClose(fileHandle);
		}
	}

	EsMessagePostRemote(instance->process->handle, &m);
}

void InstanceAnnouncePathMoved(InstalledApplication *fromApplication, const char *oldPath, size_t oldPathBytes, const char *newPath, size_t newPathBytes) {
	// TODO Update the location of installed applications and other things in the configuration.
	// TODO Replace fromApplication with something better.

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

	size_t messageDataBytes;
	void *messageData = OpenDocumentGetRenameMessageData(newPath, newPathBytes, &messageDataBytes);

	for (uintptr_t i = 0; i < desktop.allApplicationInstances.Length(); i++) {
		ApplicationInstance *instance = desktop.allApplicationInstances[i];

		if (instance->documentID != documentID) continue;
		if (instance->application == fromApplication) continue;
		if (!instance->process) continue;

		EsMessage m = { ES_MSG_INSTANCE_DOCUMENT_RENAMED };
		m.tabOperation.id = instance->embeddedWindowID;
		m.tabOperation.handle = EsConstantBufferCreate(messageData, messageDataBytes, instance->process->handle); 
		m.tabOperation.bytes = messageDataBytes;
		EsMessagePostRemote(instance->process->handle, &m);
	}

	EsHeapFree(messageData);

	if (fromApplication != desktop.fileManager && desktop.fileManager && desktop.fileManager->singleProcess) {
		char *data = (char *) EsHeapAllocate(sizeof(size_t) * 2 + oldPathBytes + newPathBytes, false);
		EsMemoryCopy(data + 0, &oldPathBytes, sizeof(size_t));
		EsMemoryCopy(data + sizeof(size_t), &newPathBytes, sizeof(size_t));
		EsMemoryCopy(data + sizeof(size_t) * 2, oldPath, oldPathBytes);
		EsMemoryCopy(data + sizeof(size_t) * 2 + oldPathBytes, newPath, newPathBytes);
		EsMessage m = {};
		m.type = ES_MSG_FILE_MANAGER_PATH_MOVED;
		m.user.context2 = sizeof(size_t) * 2 + oldPathBytes + newPathBytes;
		m.user.context1 = EsConstantBufferCreate(data, m.user.context2.u, desktop.fileManager->singleProcess->handle); 
		EsMessagePostRemote(desktop.fileManager->singleProcess->handle, &m);
		EsHeapFree(data);
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
	EsPathMove(document->temporarySavePath, document->temporarySavePathBytes, document->path, document->pathBytes, ES_PATH_MOVE_ALLOW_COPY_AND_DELETE);

	// Re-open the read handle.

	EsFileInformation file = EsFileOpen(document->path, document->pathBytes, ES_FILE_READ_SHARED | ES_NODE_FAIL_IF_NOT_FOUND);

	if (file.error != ES_SUCCESS) {
		// TODO What now?
	} else {
		EsHandleClose(document->readHandle);
		document->readHandle = file.handle;
	}

	document->currentWriter = 0;

	if (desktop.fileManager && desktop.fileManager->singleProcess) {
		EsMessage m = {};
		m.type = ES_MSG_FILE_MANAGER_FILE_MODIFIED;
		m.user.context1 = EsConstantBufferCreate(document->path, document->pathBytes, desktop.fileManager->singleProcess->handle); 
		m.user.context2 = document->pathBytes;
		EsMessagePostRemote(desktop.fileManager->singleProcess->handle, &m);
	}

	for (uintptr_t i = 0; i < desktop.allApplicationInstances.Length(); i++) {
		ApplicationInstance *instance = desktop.allApplicationInstances[i];

		if (instance->documentID != document->id) continue;
		if (!instance->process) continue;

		EsMessage m = { ES_MSG_INSTANCE_DOCUMENT_UPDATED };
		m.tabOperation.isSource = instance == fromInstance;
		m.tabOperation.id = instance->embeddedWindowID;
		m.tabOperation.handle = EsSyscall(ES_SYSCALL_HANDLE_SHARE, document->readHandle, instance->process->handle, 0, 0);
		EsMessagePostRemote(instance->process->handle, &m);
	}
}

//////////////////////////////////////////////////////
// Configuration file management:
//////////////////////////////////////////////////////

void ConfigurationLoadApplications() {
	// Add applications provided by Desktop.

	{
		InstalledApplication *application = (InstalledApplication *) EsHeapAllocate(sizeof(InstalledApplication), true);
		application->cName = (char *) "(blank tab)";
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
		application->useSingleInstance = true;
		desktop.installedApplications.Add(application);
	}

	{
		InstalledApplication *application = (InstalledApplication *) EsHeapAllocate(sizeof(InstalledApplication), true);
		application->cName = (char *) "(crashed tab)";
		application->id = APPLICATION_ID_DESKTOP_CRASHED;
		application->hidden = true;
		application->createInstance = InstanceCrashedTabCreate;
		desktop.installedApplications.Add(application);
	}

	EsMutexAcquire(&api.systemConfigurationMutex);

	for (uintptr_t i = 0; i < api.systemConfigurationGroups.Length(); i++) {
		// Load information about installed applications.

		EsSystemConfigurationGroup *group = &api.systemConfigurationGroups[i];

		if (0 != EsStringCompareRaw(group->sectionClass, group->sectionClassBytes, EsLiteral("application"))) {
			continue;
		}

		InstalledApplication *application = (InstalledApplication *) EsHeapAllocate(sizeof(InstalledApplication), true);

		application->cName = EsSystemConfigurationGroupReadString(group, EsLiteral("name"));
		application->cExecutable = EsSystemConfigurationGroupReadString(group, EsLiteral("executable"));
		application->settingsPath = EsSystemConfigurationGroupReadString(group, EsLiteral("settings_path"), &application->settingsPathBytes);
		char *icon = EsSystemConfigurationGroupReadString(group, EsLiteral("icon"));
		application->iconID = EsIconIDFromString(icon);
		EsHeapFree(icon);
		application->useSingleProcess = EsSystemConfigurationGroupReadInteger(group, EsLiteral("use_single_process"), false);
		application->useSingleInstance = EsSystemConfigurationGroupReadInteger(group, EsLiteral("use_single_instance"), false);
		application->hidden = EsSystemConfigurationGroupReadInteger(group, EsLiteral("hidden"), false);
		application->id = EsIntegerParse(group->section, group->sectionBytes);

#define READ_PERMISSION(x, y) if (EsSystemConfigurationGroupReadInteger(group, EsLiteral(x), 0)) application->permissions |= y
		READ_PERMISSION("permission_all_files", APPLICATION_PERMISSION_ALL_FILES);
		READ_PERMISSION("permission_all_devices", APPLICATION_PERMISSION_ALL_DEVICES);
		READ_PERMISSION("permission_manage_processes", APPLICATION_PERMISSION_MANAGE_PROCESSES);
		READ_PERMISSION("permission_posix_subsystem", APPLICATION_PERMISSION_POSIX_SUBSYSTEM);
		READ_PERMISSION("permission_run_temporary_application", APPLICATION_PERMISSION_RUN_TEMPORARY_APPLICATION);
		READ_PERMISSION("permission_shutdown", APPLICATION_PERMISSION_SHUTDOWN);
		READ_PERMISSION("permission_view_file_types", APPLICATION_PERMISSION_VIEW_FILE_TYPES);
		READ_PERMISSION("permission_start_application", APPLICATION_PERMISSION_START_APPLICATION);

		desktop.installedApplications.Add(application);

		if (EsSystemConfigurationGroupReadInteger(group, EsLiteral("is_file_manager"))) {
			desktop.fileManager = application;
		} else if (EsSystemConfigurationGroupReadInteger(group, EsLiteral("is_installer"))) {
			desktop.installer = application;
		}

		if (EsSystemConfigurationGroupReadInteger(group, EsLiteral("background_service"))) {
			_EsApplicationStartupInformation startupInformation = {};
			startupInformation.flags = ES_APPLICATION_STARTUP_BACKGROUND_SERVICE;
			ApplicationInstanceCreate(application->id, &startupInformation, nullptr, true /* hidden */);
		}
	}

	EsMutexRelease(&api.systemConfigurationMutex);

	EsSort(desktop.installedApplications.array, desktop.installedApplications.Length(), 
			sizeof(InstalledApplication *), [] (const void *_left, const void *_right, EsGeneric) {
		InstalledApplication *left = *(InstalledApplication **) _left;
		InstalledApplication *right = *(InstalledApplication **) _right;
		return EsStringCompare(left->cName, EsCStringLength(left->cName), right->cName, EsCStringLength(right->cName));
	}, 0);
}

void ConfigurationWriteSectionsToBuffer(const char *sectionClass, const char *section, bool includeComments, EsBuffer *pipe) {
	char buffer[4096];
	EsMutexAcquire(&api.systemConfigurationMutex);

	for (uintptr_t i = 0; i < api.systemConfigurationGroups.Length(); i++) {
		EsSystemConfigurationGroup *group = &api.systemConfigurationGroups[i];

		if ((sectionClass && EsStringCompareRaw(group->sectionClass, group->sectionClassBytes, sectionClass, -1))
				|| (section && EsStringCompareRaw(group->section, group->sectionBytes, section, -1))) {
			continue;
		}

		EsINIState s = {};
		s.sectionClass = group->sectionClass, s.sectionClassBytes = group->sectionClassBytes;
		s.section = group->section, s.sectionBytes = group->sectionBytes;
		size_t bytes = EsINIFormat(&s, buffer, sizeof(buffer));
		EsBufferWrite(pipe, buffer, bytes);

		for (uintptr_t i = 0; i < group->itemCount; i++) {
			EsSystemConfigurationItem *item = group->items + i;

			if ((!item->keyBytes || item->key[0] == ';') && !includeComments) {
				continue;
			}

			s.key = item->key, s.keyBytes = item->keyBytes;
			s.value = item->value, s.valueBytes = item->valueBytes;
			size_t bytes = EsINIFormat(&s, buffer, sizeof(buffer));
			EsBufferWrite(pipe, buffer, bytes);
		}
	}

	EsMutexRelease(&api.systemConfigurationMutex);
}

void ConfigurationWriteToFile() {
	if (!desktop.configurationModified) {
		return;
	}

	EsBuffer buffer = { .canGrow = true };
	ConfigurationWriteSectionsToBuffer(nullptr, nullptr, true /* include comments */, &buffer);

	if (!buffer.error) {
		if (ES_SUCCESS == EsFileWriteAll(EsLiteral(K_SYSTEM_CONFIGURATION "_"), buffer.out, buffer.position)) {
			// TODO Atomic delete and move.
			if (ES_SUCCESS == EsPathDelete(EsLiteral(K_SYSTEM_CONFIGURATION))) {
				if (ES_SUCCESS == EsPathMove(EsLiteral(K_SYSTEM_CONFIGURATION "_"), EsLiteral(K_SYSTEM_CONFIGURATION))) {
					EsPrint("ConfigurationWriteToFile - New configuration successfully written.\n");
					desktop.configurationModified = false;
				} else {
					EsPrint("ConfigurationWriteToFile - Error while moving to final path.\n");
				}
			} else {
				EsPrint("ConfigurationWriteToFile - Error while deleting old file.\n");
			}
		} else {
			EsPrint("ConfigurationWriteToFile - Error while writing to file.\n");
		}
	} else {
		EsPrint("ConfigurationWriteToFile - Error while writing to buffer.\n");
	}

	EsHeapFree(buffer.out);
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
	if (!instance || !instance->process) return;

	WindowTab *tab = instance->tab;

	EsProcessState state;
	EsProcessGetState(instance->process->handle, &state);

	if (state.flags & ES_PROCESS_STATE_PINGED) {
		if (tab->notRespondingInstance) {
			// The tab is already not responding.
		} else {
			// The tab has just stopped not responding.
			_EsApplicationStartupInformation startupInformation = { .data = CRASHED_TAB_NOT_RESPONDING };
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
			EsMessagePostRemote(instance->process->handle, &m); 
		}
	}
}

void DesktopSetup() {
	// Get the installation state.

	if (!desktop.setupDesktopUIComplete) {
		desktop.installationState = EsSystemConfigurationReadInteger(EsLiteral("general"), EsLiteral("installation_state"));
	}

	// Load the theme bitmap.

	if (!desktop.setupDesktopUIComplete) {
		size_t cursorsBitmapBytes;
		const void *cursorsBitmap = EsBundleFind(&bundleDesktop, EsLiteral("Cursors.png"), &cursorsBitmapBytes);
		EsHandle handle = EsMemoryOpen(ES_THEME_CURSORS_WIDTH * ES_THEME_CURSORS_HEIGHT * 4, EsLiteral(ES_THEME_CURSORS_NAME), ES_FLAGS_DEFAULT); 
		void *destination = EsObjectMap(handle, 0, ES_THEME_CURSORS_WIDTH * ES_THEME_CURSORS_HEIGHT * 4, ES_MAP_OBJECT_READ_WRITE);
		LoadImage(cursorsBitmap, cursorsBitmapBytes, destination, ES_THEME_CURSORS_WIDTH, ES_THEME_CURSORS_HEIGHT, true);
		EsObjectUnmap(destination);
		EsHandleClose(handle);
	}

	// Create the wallpaper window.

	{
		if (!desktop.wallpaperWindow) desktop.wallpaperWindow = EsWindowCreate(nullptr, ES_WINDOW_PLAIN);
		EsRectangle screen;
		EsSyscall(ES_SYSCALL_SCREEN_BOUNDS_GET, 0, (uintptr_t) &screen, 0, 0);
		EsSyscall(ES_SYSCALL_WINDOW_MOVE, desktop.wallpaperWindow->handle, (uintptr_t) &screen, 0, ES_WINDOW_MOVE_AT_BOTTOM);
		EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, desktop.wallpaperWindow->handle, (uintptr_t) &screen, 0, ES_WINDOW_PROPERTY_OPAQUE_BOUNDS);
		EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, desktop.wallpaperWindow->handle, 
				ES_WINDOW_SOLID_TRUE | ES_WINDOW_SOLID_NO_BRING_TO_FRONT, 0, ES_WINDOW_PROPERTY_SOLID);
		desktop.wallpaperWindow->doNotPaint = true;
		desktop.wallpaperWindow->messageUser = ProcessGlobalKeyboardShortcuts;

		if ((int32_t) desktop.wallpaperWindow->windowWidth != Width(screen) || (int32_t) desktop.wallpaperWindow->windowHeight != Height(screen)) {
			desktop.wallpaperWindow->windowWidth = Width(screen);
			desktop.wallpaperWindow->windowHeight = Height(screen);
			EsThreadCreate(WallpaperLoad, nullptr, 0);
		}
	}

	if (desktop.installationState == INSTALLATION_STATE_NONE) {
		// Create the taskbar.

		EsWindow *window = desktop.setupDesktopUIComplete ? desktop.taskBar.window : EsWindowCreate(nullptr, ES_WINDOW_PLAIN);
		window->messageUser = TaskBarWindowMessage;
		window->appearActivated = true;
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

			desktop.tasksButton = EsButtonCreate(panel, ES_ELEMENT_HIDDEN, ES_STYLE_TASK_BAR_BUTTON);
			desktop.tasksButton->messageUser = TaskBarTasksButtonMessage;

			EsButton *shutdownButton = EsButtonCreate(panel, ES_FLAGS_DEFAULT, ES_STYLE_TASK_BAR_EXTRA);
			EsButtonSetIcon(shutdownButton, ES_ICON_SYSTEM_SHUTDOWN_SYMBOLIC);

			EsButtonOnCommand(shutdownButton, [] (EsInstance *, EsElement *, EsCommand *) {
				ShutdownModalCreate();
			});

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
	} else if (desktop.installationState == INSTALLATION_STATE_INSTALLER) {
		// Start the instller.

		if (!desktop.setupDesktopUIComplete) {
			ApplicationInstanceCreate(desktop.installer->id, nullptr, nullptr, true /* hidden */);
		}
	}

#ifdef CHECK_FOR_NOT_RESPONDING
	if (!desktop.setupDesktopUIComplete) {
		// Setup the timer callback to check if the foreground window is responding.
		EsTimerSet(2500, CheckForegroundWindowResponding, 0); 
	}
#endif

	desktop.setupDesktopUIComplete = true;
}

void DesktopSyscall(EsMessage *message, uint8_t *buffer, EsBuffer *pipe) {
	ApplicationInstance *instance = ApplicationInstanceFindByWindowID(message->desktop.windowID);

	if (buffer[0] == DESKTOP_MSG_START_APPLICATION) {
		InstalledApplication *application = ApplicationFindByPID(message->desktop.processID);

		if (application && (application->permissions & APPLICATION_PERMISSION_START_APPLICATION)) {
			// TODO Restricting what flags can be requested?
			EsBuffer b = { .in = buffer + 1, .bytes = message->desktop.bytes - 1 };
			EsApplicationStartupRequest request = {};
			EsBufferReadInto(&b, &request, sizeof(EsApplicationStartupRequest));
			request.filePath = (const char *) EsBufferRead(&b, request.filePathBytes);
			if (!b.error) OpenDocumentWithApplication(&request);
		}
	} else if (buffer[0] == DESKTOP_MSG_CREATE_CLIPBOARD_FILE && pipe) {
		EsHandle processHandle = EsProcessOpen(message->desktop.processID);

		if (processHandle) {
			EsHandle handle;
			char *path;
			size_t pathBytes;
			EsError error = TemporaryFileCreate(&handle, &path, &pathBytes, ES_FILE_WRITE);

			if (error == ES_SUCCESS) {
				if (desktop.nextClipboardFile) {
					EsHandleClose(desktop.nextClipboardFile);
				}

				desktop.nextClipboardFile = handle;
				desktop.nextClipboardProcessID = message->desktop.processID;

				handle = EsSyscall(ES_SYSCALL_HANDLE_SHARE, handle, processHandle, 0, 0);

				EsHeapFree(path);
			} else {
				handle = ES_INVALID_HANDLE;
			}

			EsBufferWrite(pipe, &handle, sizeof(handle));
			EsBufferWrite(pipe, &error, sizeof(error));

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

			if (foreground && foreground->process) {
				EsMessage m = { ES_MSG_PRIMARY_CLIPBOARD_UPDATED };
				m.tabOperation.id = foreground->embeddedWindowID;
				EsMessagePostRemote(foreground->process->handle, &m);
			}
		} else {
			EsHandleClose(desktop.nextClipboardFile);
		}

		desktop.nextClipboardFile = ES_INVALID_HANDLE;
		desktop.nextClipboardProcessID = 0;
	} else if (buffer[0] == DESKTOP_MSG_CLIPBOARD_GET && pipe) {
		EsHandle processHandle = EsProcessOpen(message->desktop.processID);

		if (processHandle) {
			EsHandle fileHandle = desktop.clipboardFile 
				? EsSyscall(ES_SYSCALL_HANDLE_SHARE, desktop.clipboardFile, processHandle, 1 /* ES_FILE_READ_SHARED */, 0) : ES_INVALID_HANDLE;
			EsBufferWrite(pipe, &desktop.clipboardInformation, sizeof(desktop.clipboardInformation));
			EsBufferWrite(pipe, &fileHandle, sizeof(fileHandle));
			EsHandleClose(processHandle);
		}
	} else if (buffer[0] == DESKTOP_MSG_SYSTEM_CONFIGURATION_GET && pipe) {
		InstalledApplication *application = ApplicationFindByPID(message->desktop.processID);

		ConfigurationWriteSectionsToBuffer("font", nullptr, false, pipe);
		ConfigurationWriteSectionsToBuffer(nullptr, "ui_fonts", false, pipe);

		if (application && (application->permissions & APPLICATION_PERMISSION_ALL_FILES)) {
			ConfigurationWriteSectionsToBuffer(nullptr, "paths", false, pipe);
		}
	} else if (buffer[0] == DESKTOP_MSG_REQUEST_SHUTDOWN) {
		InstalledApplication *application = ApplicationFindByPID(message->desktop.processID);

		if (application && (application->permissions & APPLICATION_PERMISSION_SHUTDOWN)) {
			ShutdownModalCreate();
		}
	} else if (buffer[0] == DESKTOP_MSG_FILE_TYPES_GET && pipe) {
		InstalledApplication *application = ApplicationFindByPID(message->desktop.processID);

		if (application && (application->permissions & APPLICATION_PERMISSION_VIEW_FILE_TYPES)) {
			ConfigurationWriteSectionsToBuffer("file_type", nullptr, false, pipe);
		}
	} else if (buffer[0] == DESKTOP_MSG_ANNOUNCE_PATH_MOVED && message->desktop.bytes > 1 + sizeof(uintptr_t) * 2) {
		InstalledApplication *application = ApplicationFindByPID(message->desktop.processID);

		if (application && (application->permissions & APPLICATION_PERMISSION_ALL_FILES)) {
			uintptr_t oldPathBytes, newPathBytes;
			EsMemoryCopy(&oldPathBytes, buffer + 1, sizeof(uintptr_t));
			EsMemoryCopy(&newPathBytes, buffer + 1 + sizeof(uintptr_t), sizeof(uintptr_t));

			if (oldPathBytes >= 0x4000 || newPathBytes >= 0x4000
					|| oldPathBytes + newPathBytes + sizeof(uintptr_t) * 2 + 1 != message->desktop.bytes) {
				return;
			}

			const char *oldPath = (const char *) buffer + 1 + sizeof(uintptr_t) * 2;
			const char *newPath = (const char *) buffer + 1 + sizeof(uintptr_t) * 2 + oldPathBytes;

			InstanceAnnouncePathMoved(application, oldPath, oldPathBytes, newPath, newPathBytes);
		}
	} else if (buffer[0] == DESKTOP_MSG_START_USER_TASK && pipe) {
		ApplicationProcess *process = ApplicationProcessFindByPID(message->desktop.processID);

		if (!process || !process->instanceCount) {
			return;
		}

		InstalledApplication *application = process->application;

		// HACK User tasks use an embedded window object for IPC.
		// 	This allows us to basically treat them like other instances.

		EsHandle processHandle = EsProcessOpen(message->desktop.processID);
		EsHandle windowHandle = EsSyscall(ES_SYSCALL_WINDOW_CREATE, ES_WINDOW_NORMAL, 0, 0, 0);
		ApplicationInstance *instance = (ApplicationInstance *) EsHeapAllocate(sizeof(ApplicationInstance), true);
		bool added = false;

		if (processHandle && windowHandle && instance) {
			added = desktop.allApplicationInstances.Add(instance);
		}

		if (!processHandle || !windowHandle || !instance || !added) {
			if (processHandle) EsHandleClose(processHandle);
			if (windowHandle) EsHandleClose(windowHandle);
			if (instance) EsHeapFree(instance);

			EsHandle invalid = ES_INVALID_HANDLE;
			EsBufferWrite(pipe, &invalid, sizeof(invalid));
			return;
		}

		instance->title[0] = ' ';
		instance->titleBytes = 1;
		instance->isUserTask = true;
		instance->embeddedWindowHandle = windowHandle;
		instance->embeddedWindowID = EsSyscall(ES_SYSCALL_WINDOW_GET_ID, windowHandle, 0, 0, 0);
		instance->process = process;
		instance->process->instanceCount++;
		instance->application = application;

		EsHandle targetWindowHandle = EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, windowHandle, processHandle, 0, ES_WINDOW_PROPERTY_EMBED_OWNER);
		EsBufferWrite(pipe, &targetWindowHandle, sizeof(targetWindowHandle));

		desktop.allOngoingUserTasks.Add(instance);
		TaskBarTasksButtonUpdate();
	} else if (buffer[0] == DESKTOP_MSG_QUERY_OPEN_DOCUMENT) {
		InstalledApplication *application = ApplicationFindByPID(message->desktop.processID);

		if (application && (application->permissions & APPLICATION_PERMISSION_ALL_FILES)) {
			EsObjectID id = 0;

			for (uintptr_t i = 0; i < desktop.openDocuments.Count(); i++) {
				OpenDocument *document = &desktop.openDocuments[i];

				if (0 == EsStringCompare(document->path, document->pathBytes, (char *) buffer + 1, message->desktop.bytes - 1)) {
					id = document->id;
					break;
				}
			}

			EsOpenDocumentInformation information;
			EsMemoryZero(&information, sizeof(information));

			if (id) {
				information.isOpen = true;

				for (uintptr_t i = 0; i < desktop.allApplicationInstances.Length(); i++) {
					ApplicationInstance *instance = desktop.allApplicationInstances[i];

					if (instance->documentID == id) {
						information.isModified = instance->tab && (instance->tab->closeButton->customStyleState & THEME_STATE_CHECKED);
						information.applicationNameBytes = MinimumInteger(EsCStringLength(instance->application->cName), 
								sizeof(information.applicationName));
						EsMemoryCopy(information.applicationName, instance->application->cName, information.applicationNameBytes);
						break;
					}
				}
			}

			EsBufferWrite(pipe, &information, sizeof(information));
		}
	} else if (buffer[0] == DESKTOP_MSG_LIST_OPEN_DOCUMENTS) {
		InstalledApplication *application = ApplicationFindByPID(message->desktop.processID);

		if (application && (application->permissions & APPLICATION_PERMISSION_ALL_FILES)) {
			size_t count = desktop.openDocuments.Count();
			EsBufferWrite(pipe, &count, sizeof(size_t));

			for (uintptr_t i = 0; i < count; i++) {
				OpenDocument *document = &desktop.openDocuments[i];
				EsBufferWrite(pipe, &document->pathBytes, sizeof(size_t));
				EsBufferWrite(pipe, document->path, document->pathBytes);
			}
		}
	} else if (!instance) {
		// -------------------------------------------------
		// | Messages below here require a valid instance. |
		// -------------------------------------------------
		EsPrint("DesktopSyscall - Received message %d without an instance.\n", buffer[0]);
	} else if (buffer[0] == DESKTOP_MSG_SET_TITLE || buffer[0] == DESKTOP_MSG_SET_ICON) {
		if (buffer[0] == DESKTOP_MSG_SET_TITLE) {
			instance->titleBytes = EsStringFormat(instance->title, sizeof(instance->title), "%s", 
					message->desktop.bytes - 1, buffer + 1);
		} else {
			if (message->desktop.bytes == 5) {
				EsMemoryCopy(&instance->iconID, buffer + 1, sizeof(uint32_t));
			}
		}

		if (instance->tab) {
			instance->tab->Repaint(true);

			if (instance->tab == instance->tab->container->active) {
				instance->tab->container->taskBarButton->Repaint(true);
			}
		}
	} else if (buffer[0] == DESKTOP_MSG_SET_MODIFIED && message->desktop.bytes == 2) {
		if (instance->tab) {
			EsButtonSetCheck(instance->tab->closeButton, buffer[1] ? ES_CHECK_CHECKED : ES_CHECK_UNCHECKED, false);
		}
	} else if (buffer[0] == DESKTOP_MSG_SET_PROGRESS && message->desktop.bytes == 1 + sizeof(double) && instance->isUserTask) {
		double progress;
		EsMemoryCopy(&progress, buffer + 1, sizeof(double));

		if (progress >= 0.0 && progress <= 1.0) {
			desktop.totalUserTaskProgress += progress - instance->progress;
			instance->progress = progress;
			EsElementRepaint(desktop.tasksButton);
		}
	} else if (buffer[0] == DESKTOP_MSG_REQUEST_SAVE) {
		ApplicationInstanceRequestSave(instance, (const char *) buffer + 1, message->desktop.bytes - 1, false);
	} else if (buffer[0] == DESKTOP_MSG_RENAME) {
		const char *newName = (const char *) buffer + 1;
		size_t newNameBytes = message->desktop.bytes - 1;
		OpenDocument *document = desktop.openDocuments.Get(&instance->documentID);

		if (!instance->documentID) {
			ApplicationInstanceRequestSave(instance, newName, newNameBytes, true);
		} else if (document) {
			size_t folderBytes = 0, oldPathBytes, newPathBytes;

			for (uintptr_t i = 0; i < document->pathBytes; i++) {
				if (document->path[i] == '/') {
					folderBytes = i;
				}
			}

			char *oldPath = EsStringAllocateAndFormat(&oldPathBytes, "%s", document->pathBytes, document->path);
			char *newPath = EsStringAllocateAndFormat(&newPathBytes, "%s/%s", folderBytes, document->path, newNameBytes, newName);

			if (oldPathBytes == newPathBytes && 0 == EsMemoryCompare(oldPath, newPath, oldPathBytes)) {
				// Same name.
			} else {
				EsMessage m = {};
				m.type = ES_MSG_INSTANCE_RENAME_RESPONSE;
				m.tabOperation.id = instance->embeddedWindowID;
				m.tabOperation.error = EsPathMove(oldPath, oldPathBytes, newPath, newPathBytes);
				EsMessagePostRemote(instance->process->handle, &m);

				if (m.tabOperation.error == ES_SUCCESS) {
					InstanceAnnouncePathMoved(nullptr, oldPath, oldPathBytes, newPath, newPathBytes);
				}
			}

			EsHeapFree(oldPath);
			EsHeapFree(newPath);
		}
	} else if (buffer[0] == DESKTOP_MSG_COMPLETE_SAVE) {
		ApplicationInstanceCompleteSave(instance);
	} else if (buffer[0] == DESKTOP_MSG_SHOW_IN_FILE_MANAGER) {
		// TODO Don't open a new instance if the folder is already open?
		OpenDocument *document = desktop.openDocuments.Get(&instance->documentID);

		if (document) {
			_EsApplicationStartupInformation startupInformation = {};
			startupInformation.flags = ES_APPLICATION_STARTUP_MANUAL_PATH;
			startupInformation.filePath = document->path;
			startupInformation.filePathBytes = document->pathBytes;
			ApplicationInstanceCreate(desktop.fileManager->id, &startupInformation, instance->tab->container);
		}
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
			EsHandle handle;
			EsError error = TemporaryFileCreate(&handle, &application->settingsPath, &application->settingsPathBytes, ES_NODE_DIRECTORY);
			if (error == ES_SUCCESS) EsHandleClose(handle);
			desktop.installedApplications.Add(application);
			ApplicationInstanceCreate(application->id, nullptr, nullptr);
		}
	} else {
		EsPrint("DesktopSyscall - Received unhandled message %d.\n", buffer[0]);
	}
}

void EmbeddedWindowDestroyed(EsObjectID id) {
	EsMenuCloseAll(); // The tab will be destroyed, but menus might be keeping pointers to it.
	ApplicationInstance *instance = ApplicationInstanceFindByWindowID(id, true /* remove if found */);
	if (!instance) return;

	EsHandleClose(instance->embeddedWindowHandle);

	ApplicationInstanceCleanup(instance);

	if (instance->tab) {
		WindowTabDestroy(instance->tab);
	} else if (instance->isUserTask) {
		desktop.totalUserTaskProgress -= instance->progress;
		EsElementRepaint(desktop.tasksButton);
		desktop.allOngoingUserTasks.FindAndDeleteSwap(instance, false /* ignore if not found */);
		TaskBarTasksButtonUpdate();
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
			EsBuffer pipe = { .canGrow = true };
			DesktopSyscall(message, buffer, &pipe);
			if (message->desktop.pipe) EsPipeWrite(message->desktop.pipe, pipe.out, pipe.position);
			EsHeapFree(pipe.out);
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

		for (uintptr_t i = 0; i < desktop.allApplicationProcesses.Length(); i++) {
			ApplicationProcess *process = desktop.allApplicationProcesses[i];

			if (process->application && (process->application->permissions & APPLICATION_PERMISSION_ALL_FILES)) {
				message->registerFileSystem.rootDirectory = EsSyscall(ES_SYSCALL_HANDLE_SHARE, rootDirectory, process->handle, 0, 0);
				EsMessagePostRemote(process->handle, message);
			}
		}
	} else if (message->type == ES_MSG_DEVICE_CONNECTED) {
		EsHandle handle = message->device.handle;

		for (uintptr_t i = 0; i < desktop.allApplicationProcesses.Length(); i++) {
			ApplicationProcess *process = desktop.allApplicationProcesses[i];

			if (process->application && (process->application->permissions & APPLICATION_PERMISSION_ALL_DEVICES)) {
				message->device.handle = EsSyscall(ES_SYSCALL_HANDLE_SHARE, handle, process->handle, 0, 0);
				EsMessagePostRemote(process->handle, message);
			}
		}

		if (message->device.type == ES_DEVICE_CLOCK) {
			EsDateComponents reading;
			uint64_t linear;

			if (ES_SUCCESS == EsDeviceControl(message->device.handle, ES_DEVICE_CONTROL_CLOCK_READ, &reading, &linear)) {
				// TODO Scheduler timer is not particularly accurate, so we should periodically resynchronize with the clock.
				api.global->schedulerTimeOffset = (linear ?: DateToLinear(&reading)) - api.global->schedulerTimeMs;
			}
		}
	} else if (message->type == ES_MSG_UNREGISTER_FILE_SYSTEM || message->type == ES_MSG_DEVICE_DISCONNECTED) {
		for (uintptr_t i = 0; i < desktop.allApplicationProcesses.Length(); i++) {
			ApplicationProcess *process = desktop.allApplicationProcesses[i];

			if (!process->application) {
				continue;
			}

			if (message->type == ES_MSG_UNREGISTER_FILE_SYSTEM) {
				if (~process->application->permissions & APPLICATION_PERMISSION_ALL_FILES) {
					continue;
				}
			} else if (message->type == ES_MSG_DEVICE_DISCONNECTED) {
				if (~process->application->permissions & APPLICATION_PERMISSION_ALL_DEVICES) {
					continue;
				}
			}

			EsMessagePostRemote(process->handle, message);
		}
	} else if (message->type == ES_MSG_SET_SCREEN_RESOLUTION) {
		if (desktop.setupDesktopUIComplete) {
			DesktopSetup(); // Refresh desktop UI.
		} else {
			// The screen resolution will be correctly queried in DesktopSetup.
		}
	} else if (message->type == ES_MSG_INSTANCE_DESTROY) {
		CommonDesktopInstance *instance = (CommonDesktopInstance *) message->instanceDestroy.instance;

		if (instance->destroy) {
			instance->destroy(instance);
		}
	} else if (message->type == ES_MSG_KEY_DOWN) {
		ProcessGlobalKeyboardShortcuts(nullptr, message);
	} else if (message->type == MSG_SETUP_DESKTOP_UI || message->type == ES_MSG_UI_SCALE_CHANGED) {
		DesktopSetup();
	}
}

void DesktopEntry() {
	ConfigurationLoadApplications();

	EsMessage m = { MSG_SETUP_DESKTOP_UI };
	EsMessagePost(nullptr, &m);

	while (true) {
		EsMessage *message = EsMessageReceive();
		DesktopMessage(message);
	}
}
