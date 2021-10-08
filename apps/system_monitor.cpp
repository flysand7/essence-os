#define ES_INSTANCE_TYPE Instance
#include <essence.h>
#include <shared/array.cpp>

// TODO Single instance.
// TODO Sorting lists.
// TODO Processes: handle/thread count; IO statistics; more memory information.

struct Instance : EsInstance {
	EsPanel *switcher;
	EsTextbox *textboxGeneralLog;
	EsListView *listViewProcesses;
	EsPanel *panelMemoryStatistics;
	int index;
	EsCommand commandTerminateProcess;
	Array<EsTextDisplay *> textDisplaysMemory;
};

#define REFRESH_INTERVAL (1000)

#define DISPLAY_PROCESSES (1)
#define DISPLAY_GENERAL_LOG (3)
#define DISPLAY_MEMORY (12)

EsListViewColumn listViewProcessesColumns[] = {
	{ EsLiteral("Name"), 0, 150 },
	{ EsLiteral("PID"), ES_LIST_VIEW_COLUMN_RIGHT_ALIGNED, 120 },
	{ EsLiteral("Memory"), ES_LIST_VIEW_COLUMN_RIGHT_ALIGNED, 120 },
	{ EsLiteral("CPU"), ES_LIST_VIEW_COLUMN_RIGHT_ALIGNED, 120 },
	{ EsLiteral("Handles"), ES_LIST_VIEW_COLUMN_RIGHT_ALIGNED, 120 },
	{ EsLiteral("Threads"), ES_LIST_VIEW_COLUMN_RIGHT_ALIGNED, 120 },
};

EsListViewColumn listViewContextSwitchesColumns[] = {
	{ EsLiteral("Time stamp (ms)"), ES_LIST_VIEW_COLUMN_RIGHT_ALIGNED, 150 },
	{ EsLiteral("CPU"), ES_LIST_VIEW_COLUMN_RIGHT_ALIGNED, 150 },
	{ EsLiteral("Process"), 0, 150 },
	{ EsLiteral("Thread"), 0, 150 },
	{ EsLiteral("Count"), 0, 150 },
};

const EsStyle styleMonospacedTextbox = {
	.inherit = ES_STYLE_TEXTBOX_NO_BORDER,

	.metrics = {
		.mask = ES_THEME_METRICS_FONT_FAMILY,
		.fontFamily = ES_FONT_MONOSPACED,
	},
};

const EsStyle stylePanelMemoryStatistics = {
	.inherit = ES_STYLE_PANEL_FILLED,

	.metrics = {
		.mask = ES_THEME_METRICS_GAP_ALL,
		.gapMajor = 5,
		.gapMinor = 5,
	},
};

const EsStyle stylePanelMemoryCommands = {
	.metrics = {
		.mask = ES_THEME_METRICS_INSETS | ES_THEME_METRICS_GAP_ALL,
		.insets = ES_RECT_4(0, 0, 0, 5),
		.gapMajor = 5,
		.gapMinor = 5,
	},
};

const char *pciClassCodeStrings[] = {
	"Unknown",
	"Mass storage controller",
	"Network controller",
	"Display controller",
	"Multimedia controller",
	"Memory controller",
	"Bridge controller",
	"Simple communication controller",
	"Base system peripheral",
	"Input device controller",
	"Docking station",
	"Processor",
	"Serial bus controller",
	"Wireless controller",
	"Intelligent controller",
	"Satellite communication controller",
	"Encryption controller",
	"Signal processing controller",
};

const char *pciSubclassCodeStrings1[] = {
	"SCSI bus controller",
	"IDE controller",
	"Floppy disk controller",
	"IPI bus controller",
	"RAID controller",
	"ATA controller",
	"Serial ATA",
	"Serial attached SCSI",
	"Non-volatile memory controller",
};

const char *pciSubclassCodeStrings12[] = {
	"FireWire (IEEE 1394) controller",
	"ACCESS bus",
	"SSA",
	"USB controller",
	"Fibre channel",
	"SMBus",
	"InfiniBand",
	"IPMI interface",
	"SERCOS interface (IEC 61491)",
	"CANbus",
};

const char *pciProgIFStrings12_3[] = {
	"UHCI",
	"OHCI",
	"EHCI",
	"XHCI",
};

struct ProcessItem {
	EsSnapshotProcessesItem data;
	uintptr_t cpuUsage;
};

char generalLogBuffer[256 * 1024];
Array<ProcessItem> processes;
int64_t selectedPID = -2;

ProcessItem *FindProcessByPID(Array<ProcessItem> snapshot, int64_t pid) {
	for (uintptr_t i = 0; i < snapshot.Length(); i++) {
		if (pid == snapshot[i].data.pid) {
			return &snapshot[i];
		}
	}

	return nullptr;
}

void UpdateProcesses(Instance *instance) {
	Array<ProcessItem> previous = processes;
	processes = {};

	size_t bufferSize;
	EsHandle handle = EsTakeSystemSnapshot(ES_SYSTEM_SNAPSHOT_PROCESSES, &bufferSize); 
	EsSnapshotProcesses *snapshot = (EsSnapshotProcesses *) EsHeapAllocate(bufferSize, false);
	EsConstantBufferRead(handle, snapshot);
	EsHandleClose(handle);

	for (uintptr_t i = 0; i < snapshot->count; i++) {
		ProcessItem item = {};
		item.data = snapshot->processes[i];
		processes.Add(item);

		if (snapshot->processes[i].isKernel) {
			ProcessItem item = {};
			item.data.cpuTimeSlices = snapshot->processes[i].idleTimeSlices;
			item.data.pid = -1;
			const char *idle = "CPU idle";
			item.data.nameBytes = EsCStringLength(idle);
			EsMemoryCopy(item.data.name, idle, item.data.nameBytes);
			processes.Add(item);
		}
	}

	EsHeapFree(snapshot);

	for (uintptr_t i = 0; i < previous.Length(); i++) {
		if (!FindProcessByPID(processes, previous[i].data.pid)) {
			EsListViewRemove(instance->listViewProcesses, 0, i, 1);
			previous.Delete(i--);
		}
	}

	for (uintptr_t i = 0; i < processes.Length(); i++) {
		processes[i].cpuUsage = processes[i].data.cpuTimeSlices;
		ProcessItem *item = FindProcessByPID(previous, processes[i].data.pid);
		if (item) processes[i].cpuUsage -= item->data.cpuTimeSlices;
	}

	int64_t totalCPUTimeSlices = 0;

	for (uintptr_t i = 0; i < processes.Length(); i++) {
		totalCPUTimeSlices += processes[i].cpuUsage;
	}

	if (!totalCPUTimeSlices) {
		totalCPUTimeSlices = 1;
	}

	int64_t percentageSum = 0;

	for (uintptr_t i = 0; i < processes.Length(); i++) {
		processes[i].cpuUsage = processes[i].cpuUsage * 100 / totalCPUTimeSlices;
		percentageSum += processes[i].cpuUsage;
	}

	while (percentageSum < 100 && percentageSum) {
		for (uintptr_t i = 0; i < processes.Length(); i++) {
			if (processes[i].cpuUsage && percentageSum < 100) {
				processes[i].cpuUsage++, percentageSum++;
			}
		}
	}

	for (uintptr_t i = 0; i < processes.Length(); i++) {
		if (!FindProcessByPID(previous, processes[i].data.pid)) {
			EsListViewInsert(instance->listViewProcesses, 0, i, 1);
		}
	}

	EsListViewInvalidateAll(instance->listViewProcesses);
	EsCommandSetDisabled(&instance->commandTerminateProcess, selectedPID < 0 || !FindProcessByPID(processes, selectedPID));

	EsTimerSet(REFRESH_INTERVAL, [] (EsGeneric context) {
		Instance *instance = (Instance *) context.p;
		
		if (instance->index == DISPLAY_PROCESSES) {
			UpdateProcesses(instance);
		}
	}, instance); 

	previous.Free();
}

void UpdateDisplay(Instance *instance, int index) {
	instance->index = index;

	if (index != DISPLAY_PROCESSES) {
		EsCommandSetDisabled(&instance->commandTerminateProcess, true);
	}

	if (index == DISPLAY_PROCESSES) {
		UpdateProcesses(instance);
		EsPanelSwitchTo(instance->switcher, instance->listViewProcesses, ES_TRANSITION_NONE);
		EsElementFocus(instance->listViewProcesses);
	} else if (index == DISPLAY_GENERAL_LOG) {
		size_t bytes = EsSyscall(ES_SYSCALL_DEBUG_COMMAND, index, (uintptr_t) generalLogBuffer, sizeof(generalLogBuffer), 0);
		EsTextboxSelectAll(instance->textboxGeneralLog);
		EsTextboxInsert(instance->textboxGeneralLog, generalLogBuffer, bytes);
		EsTextboxEnsureCaretVisible(instance->textboxGeneralLog, false);
		EsPanelSwitchTo(instance->switcher, instance->textboxGeneralLog, ES_TRANSITION_NONE);
	} else if (index == DISPLAY_MEMORY) {
		EsMemoryStatistics statistics = {};
		EsSyscall(ES_SYSCALL_DEBUG_COMMAND, index, (uintptr_t) &statistics, 0, 0);

		EsPanelSwitchTo(instance->switcher, instance->panelMemoryStatistics, ES_TRANSITION_NONE);

		if (!instance->textDisplaysMemory.Length()) {
			EsPanel *panel = EsPanelCreate(instance->panelMemoryStatistics, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL, &stylePanelMemoryCommands);
			EsButton *button;

			button = EsButtonCreate(panel, ES_FLAGS_DEFAULT, 0, EsLiteral("Leak 1 MB"));
			EsButtonOnCommand(button, [] (Instance *, EsElement *, EsCommand *) { EsMemoryReserve(0x100000); });
			button = EsButtonCreate(panel, ES_FLAGS_DEFAULT, 0, EsLiteral("Leak 4 MB"));
			EsButtonOnCommand(button, [] (Instance *, EsElement *, EsCommand *) { EsMemoryReserve(0x400000); });
			button = EsButtonCreate(panel, ES_FLAGS_DEFAULT, 0, EsLiteral("Leak 16 MB"));
			EsButtonOnCommand(button, [] (Instance *, EsElement *, EsCommand *) { EsMemoryReserve(0x1000000); });
			button = EsButtonCreate(panel, ES_FLAGS_DEFAULT, 0, EsLiteral("Leak 64 MB"));
			EsButtonOnCommand(button, [] (Instance *, EsElement *, EsCommand *) { EsMemoryReserve(0x4000000); });
			button = EsButtonCreate(panel, ES_FLAGS_DEFAULT, 0, EsLiteral("Leak 256 MB"));
			EsButtonOnCommand(button, [] (Instance *, EsElement *, EsCommand *) { EsMemoryReserve(0x10000000); });

			EsSpacerCreate(instance->panelMemoryStatistics, ES_CELL_H_FILL);
		}

		char buffer[256];
		size_t bytes;
		uintptr_t index = 0;
#define ADD_MEMORY_STATISTIC_DISPLAY(label, ...) \
		bytes = EsStringFormat(buffer, sizeof(buffer), __VA_ARGS__); \
		if (instance->textDisplaysMemory.Length() == index) { \
			EsTextDisplayCreate(instance->panelMemoryStatistics, ES_CELL_H_PUSH | ES_CELL_H_RIGHT, 0, EsLiteral(label)); \
			instance->textDisplaysMemory.Add(EsTextDisplayCreate(instance->panelMemoryStatistics, ES_CELL_H_PUSH | ES_CELL_H_LEFT)); \
		} \
		EsTextDisplaySetContents(instance->textDisplaysMemory[index++], buffer, bytes)

		ADD_MEMORY_STATISTIC_DISPLAY("Fixed heap allocation count:", "%d", statistics.fixedHeapAllocationCount);
		ADD_MEMORY_STATISTIC_DISPLAY("Fixed heap graphics surfaces:", "%D (%d B)", 
				statistics.totalSurfaceBytes, statistics.totalSurfaceBytes);
		ADD_MEMORY_STATISTIC_DISPLAY("Fixed heap normal size:", "%D (%d B)", 
				statistics.fixedHeapTotalSize - statistics.totalSurfaceBytes, statistics.fixedHeapTotalSize - statistics.totalSurfaceBytes);
		ADD_MEMORY_STATISTIC_DISPLAY("Fixed heap total size:", "%D (%d B)", 
				statistics.fixedHeapTotalSize, statistics.fixedHeapTotalSize);
		ADD_MEMORY_STATISTIC_DISPLAY("Core heap allocation count:", "%d", statistics.coreHeapAllocationCount);
		ADD_MEMORY_STATISTIC_DISPLAY("Core heap total size:", "%D (%d B)", statistics.coreHeapTotalSize, statistics.coreHeapTotalSize);
		ADD_MEMORY_STATISTIC_DISPLAY("Cached boot FS nodes:", "%d", statistics.cachedNodes);
		ADD_MEMORY_STATISTIC_DISPLAY("Cached boot FS directory entries:", "%d", statistics.cachedDirectoryEntries);
		ADD_MEMORY_STATISTIC_DISPLAY("Maximum object cache size:", "%D (%d pages)", statistics.maximumObjectCachePages * ES_PAGE_SIZE, 
				statistics.maximumObjectCachePages);
		ADD_MEMORY_STATISTIC_DISPLAY("Approximate object cache size:", "%D (%d pages)", statistics.approximateObjectCacheSize, 
				statistics.approximateObjectCacheSize / ES_PAGE_SIZE);
		ADD_MEMORY_STATISTIC_DISPLAY("Commit (pageable):", "%D (%d pages)", statistics.commitPageable * ES_PAGE_SIZE, statistics.commitPageable);
		ADD_MEMORY_STATISTIC_DISPLAY("Commit (fixed):", "%D (%d pages)", statistics.commitFixed * ES_PAGE_SIZE, statistics.commitFixed);
		ADD_MEMORY_STATISTIC_DISPLAY("Commit (total):", "%D (%d pages)", (statistics.commitPageable + statistics.commitFixed) * ES_PAGE_SIZE, 
				statistics.commitPageable + statistics.commitFixed);
		ADD_MEMORY_STATISTIC_DISPLAY("Commit limit:", "%D (%d pages)", statistics.commitLimit * ES_PAGE_SIZE, statistics.commitLimit);
		ADD_MEMORY_STATISTIC_DISPLAY("Commit fixed limit:", "%D (%d pages)", statistics.commitFixedLimit * ES_PAGE_SIZE, statistics.commitFixedLimit);
		ADD_MEMORY_STATISTIC_DISPLAY("Commit remaining:", "%D (%d pages)", statistics.commitRemaining * ES_PAGE_SIZE, statistics.commitRemaining);

		EsTimerSet(REFRESH_INTERVAL, [] (EsGeneric context) {
			Instance *instance = (Instance *) context.p;

			if (instance->index == DISPLAY_MEMORY) {
				UpdateDisplay(instance, DISPLAY_MEMORY);
			}
		}, instance); 
	}
}

#define GET_CONTENT(...) EsBufferFormat(message->getContent.buffer, __VA_ARGS__)

int ListViewProcessesCallback(EsElement *element, EsMessage *message) {
	if (message->type == ES_MSG_LIST_VIEW_GET_CONTENT) {
		int column = message->getContent.column, index = message->getContent.index;
		ProcessItem *item = &processes[index];
		if      (column == 0) GET_CONTENT("%s", item->data.nameBytes, item->data.name);
		else if (column == 1) { if (item->data.pid == -1) GET_CONTENT("n/a"); else GET_CONTENT("%d", item->data.pid); }
		else if (column == 2) GET_CONTENT("%D", item->data.memoryUsage);
		else if (column == 3) GET_CONTENT("%d%%", item->cpuUsage);
		else if (column == 4) GET_CONTENT("%d", item->data.handleCount);
		else if (column == 5) GET_CONTENT("%d", item->data.threadCount);
		else EsAssert(false);
	} else if (message->type == ES_MSG_LIST_VIEW_IS_SELECTED) {
		message->selectItem.isSelected = processes[message->selectItem.index].data.pid == selectedPID;
	} else if (message->type == ES_MSG_LIST_VIEW_SELECT && message->selectItem.isSelected) {
		selectedPID = processes[message->selectItem.index].data.pid;
		EsCommandSetDisabled(&element->instance->commandTerminateProcess, selectedPID < 0 || !FindProcessByPID(processes, selectedPID));
	} else {
		return 0;
	}

	return ES_HANDLED;
}

void AddTab(EsElement *toolbar, uintptr_t index, const char *label, bool asDefault = false) {
	EsButton *button = EsButtonCreate(toolbar, ES_BUTTON_RADIOBOX, 0, label);
	button->userData.u = index;

	EsButtonOnCommand(button, [] (Instance *instance, EsElement *element, EsCommand *) {
		if (EsButtonGetCheck((EsButton *) element) == ES_CHECK_CHECKED) {
			UpdateDisplay(instance, element->userData.u);
		}
	});

	if (asDefault) EsButtonSetCheck(button, ES_CHECK_CHECKED);
}

void AddListView(EsListView **pointer, EsElement *switcher, EsUICallback callback, EsListViewColumn *columns, size_t columnsSize, uint64_t additionalFlags) {
	*pointer = EsListViewCreate(switcher, ES_CELL_FILL | ES_LIST_VIEW_COLUMNS | additionalFlags);
	(*pointer)->messageUser = callback;
	EsListViewSetColumns(*pointer, columns, columnsSize / sizeof(EsListViewColumn));
	EsListViewInsertGroup(*pointer, 0);
}

void TerminateProcess(Instance *instance, EsElement *, EsCommand *) {
	if (selectedPID == 0 /* Kernel */) {
		// Terminating the kernel process is a meaningless action; the closest equivalent is shutting down.
		EsSystemShowShutdownDialog();
		return;
	}

	EsHandle handle = EsProcessOpen(selectedPID); 

	if (handle) {
		EsProcessTerminate(handle, 1); 
	} else {
		EsRectangle bounds = EsElementGetWindowBounds(instance->listViewProcesses);
		EsAnnouncementShow(instance->window, ES_FLAGS_DEFAULT, (bounds.l + bounds.r) / 2, (bounds.t + bounds.b) / 2, EsLiteral("Could not terminate process"));
	}
}

void ProcessApplicationMessage(EsMessage *message) {
	if (message->type == ES_MSG_INSTANCE_CREATE) {
		Instance *instance = EsInstanceCreate(message, "System Monitor");

		EsCommandRegister(&instance->commandTerminateProcess, instance, EsLiteral("Terminate process"), TerminateProcess, 1, "Del", false);

		EsWindow *window = instance->window;
		EsWindowSetIcon(window, ES_ICON_UTILITIES_SYSTEM_MONITOR);
		EsPanel *switcher = EsPanelCreate(window, ES_CELL_FILL | ES_PANEL_SWITCHER, ES_STYLE_PANEL_WINDOW_DIVIDER);
		instance->switcher = switcher;

		instance->textboxGeneralLog = EsTextboxCreate(switcher, ES_TEXTBOX_MULTILINE | ES_CELL_FILL | ES_ELEMENT_DISABLED, &styleMonospacedTextbox);

		AddListView(&instance->listViewProcesses, switcher, ListViewProcessesCallback, 
				listViewProcessesColumns, sizeof(listViewProcessesColumns), ES_LIST_VIEW_SINGLE_SELECT);

		instance->panelMemoryStatistics = EsPanelCreate(switcher, 
				ES_CELL_FILL | ES_PANEL_TABLE | ES_PANEL_HORIZONTAL | ES_PANEL_V_SCROLL_AUTO, &stylePanelMemoryStatistics);
		EsPanelSetBands(instance->panelMemoryStatistics, 2 /* columns */);

		EsElement *toolbar = EsWindowGetToolbar(window);
		AddTab(toolbar, DISPLAY_PROCESSES, "Processes", true);
		AddTab(toolbar, DISPLAY_GENERAL_LOG, "System log");
		AddTab(toolbar, DISPLAY_MEMORY, "Memory");
	} else if (message->type == ES_MSG_INSTANCE_DESTROY) {
		processes.Free();
	}
}

void _start() {
	_init();

	while (true) {
		ProcessApplicationMessage(EsMessageReceive());
	}
}
