#define INSTALLER

#define ES_CRT_WITHOUT_PREFIX
#include <essence.h>

#include <shared/hash.cpp>
#include <shared/strings.cpp>
#include <ports/lzma/LzmaDec.c>

#include <shared/array.cpp>
#define IMPLEMENTATION
#include <shared/array.cpp>
#undef IMPLEMENTATION

#define Log(...)
#define exit(x) EsThreadTerminate(ES_CURRENT_THREAD)
#include <shared/esfs2.h>

// Assume an additional 64MB of storage capacity is needed on top of totalUncompressedBytes.
#define PARTITION_OVERHEAD (64 * 1024 * 1024)

struct InstallerMetadata {
	uint64_t totalUncompressedBytes;
};

const EsStyle styleRoot = {
	.metrics = {
		.mask = ES_THEME_METRICS_GAP_MAJOR | ES_THEME_METRICS_INSETS,
		.insets = ES_RECT_1(30),
		.gapMajor = 20,
	},
};

const EsStyle styleDrivesSelectHint = {
	.inherit = ES_STYLE_TEXT_PARAGRAPH_SECONDARY,

	.metrics = {
		.mask = ES_THEME_METRICS_TEXT_ALIGN | ES_THEME_METRICS_INSETS,
		.insets = ES_RECT_1(30),
		.textAlign = ES_TEXT_H_CENTER | ES_TEXT_V_CENTER | ES_TEXT_WRAP,
	},
};

const EsStyle styleButtonsRow = {
	.metrics = {
		.mask = ES_THEME_METRICS_GAP_MAJOR,
		.gapMajor = 10,
	},
};

InstallerMetadata *metadata;
Array<EsMessageDevice> connectedDrives;
EsListView *drivesList;
EsPanel *driveInformation;
EsObjectID selectedDriveID;
EsButton *installButton;
EsPanel *switcher;
EsPanel *panelInstallOptions;
EsPanel *panelLicenses;

/////////////////////////////////////////////

#define BUFFER_SIZE (1048576)
#define NAME_MAX (4096)

struct Extractor {
	EsFileInformation fileIn;
	CLzmaDec state;
	uint8_t inBuffer[BUFFER_SIZE], outBuffer[BUFFER_SIZE], copyBuffer[BUFFER_SIZE], pathBuffer[NAME_MAX];
	size_t inFileOffset, inBytes, inPosition;
	uintptr_t positionInBlock, blockSize;
};

void *DecompressAllocate(ISzAllocPtr, size_t size) { return EsHeapAllocate(size, false); }
void DecompressFree(ISzAllocPtr, void *address) { EsHeapFree(address); }
const ISzAlloc decompressAllocator = { DecompressAllocate, DecompressFree };

ptrdiff_t DecompressBlock(Extractor *e) {
	if (e->inBytes == e->inPosition) {
		e->inBytes = EsFileReadSync(e->fileIn.handle, e->inFileOffset, BUFFER_SIZE, e->inBuffer);
		if (!e->inBytes) return -1;
		e->inPosition = 0;
		e->inFileOffset += e->inBytes;
	}

	size_t inProcessed = e->inBytes - e->inPosition;
	size_t outProcessed = BUFFER_SIZE;
	ELzmaStatus status;
	LzmaDec_DecodeToBuf(&e->state, e->outBuffer, &outProcessed, e->inBuffer + e->inPosition, &inProcessed, LZMA_FINISH_ANY, &status);
	e->inPosition += inProcessed;
	return outProcessed;
}

bool Decompress(Extractor *e, void *_buffer, size_t bytes) {
	uint8_t *buffer = (uint8_t *) _buffer;

	while (bytes) {
		if (e->positionInBlock == e->blockSize) {
			ptrdiff_t processed = DecompressBlock(e);
			if (processed == -1) return false;
			e->blockSize = processed;
			e->positionInBlock = 0;
		}

		size_t copyBytes = bytes > e->blockSize - e->positionInBlock ? e->blockSize - e->positionInBlock : bytes;
		EsMemoryCopy(buffer, e->outBuffer + e->positionInBlock, copyBytes);
		e->positionInBlock += copyBytes, buffer += copyBytes, bytes -= copyBytes;
	}

	return true;
}

EsError Extract(const char *pathIn, size_t pathInBytes, const char *pathOut, size_t pathOutBytes) {
	Extractor *e = (Extractor *) EsHeapAllocate(sizeof(Extractor), true);
	if (!e) return ES_ERROR_INSUFFICIENT_RESOURCES;
	EsDefer(EsHeapFree(e));

	e->fileIn = EsFileOpen(pathIn, pathInBytes, ES_FILE_READ);
	if (e->fileIn.error != ES_SUCCESS) return e->fileIn.error;

	uint8_t header[LZMA_PROPS_SIZE + 8];
	EsFileReadSync(e->fileIn.handle, 0, sizeof(header), header);

	LzmaDec_Construct(&e->state);
	LzmaDec_Allocate(&e->state, header, LZMA_PROPS_SIZE, &decompressAllocator);
	LzmaDec_Init(&e->state);

	e->inFileOffset = sizeof(header);

	uint64_t crc64 = 0, actualCRC64 = 0;

	EsMemoryCopy(e->pathBuffer, pathOut, pathOutBytes);

	while (true) {
		uint64_t fileSize;
		if (!Decompress(e, &fileSize, sizeof(fileSize))) break;
		actualCRC64 = fileSize;
		uint16_t nameBytes;
		if (!Decompress(e, &nameBytes, sizeof(nameBytes))) break;
		if (nameBytes > NAME_MAX - pathOutBytes) break;
		if (!Decompress(e, e->pathBuffer + pathOutBytes, nameBytes)) break;

		EsFileInformation fileOut = EsFileOpen((const char *) e->pathBuffer, pathOutBytes + nameBytes, 
				ES_FILE_WRITE | ES_NODE_CREATE_DIRECTORIES | ES_NODE_FAIL_IF_FOUND);
		EsFileOffset fileOutPosition = 0;

		if (fileOut.error != ES_SUCCESS) {
			LzmaDec_Free(&e->state, &decompressAllocator);
			EsHandleClose(e->fileIn.handle);
			return fileOut.error;
		}

		while (fileOutPosition < fileSize) {
			size_t copyBytes = (fileSize - fileOutPosition) > BUFFER_SIZE ? BUFFER_SIZE : (fileSize - fileOutPosition);
			Decompress(e, e->copyBuffer, copyBytes);
			EsFileWriteSync(fileOut.handle, fileOutPosition, copyBytes, e->copyBuffer);
			fileOutPosition += copyBytes;
			crc64 = CalculateCRC64(e->copyBuffer, copyBytes, crc64);
		}

		EsHandleClose(fileOut.handle);
	}

	LzmaDec_Free(&e->state, &decompressAllocator);
	EsHandleClose(e->fileIn.handle);

	return crc64 == actualCRC64 ? ES_SUCCESS : ES_ERROR_CORRUPT_DATA;
}

/////////////////////////////////////////////

void ReadBlock(uint64_t, uint64_t, void *) {
	// TODO.
}

void WriteBlock(uint64_t, uint64_t, void *) {
	// TODO.
}

void WriteBytes(uint64_t, uint64_t, void *) {
	// TODO.
}

/////////////////////////////////////////////

EsBlockDeviceInformation ConnectedDriveGetInformation(EsHandle handle) {
	EsBlockDeviceInformation information;
	EsDeviceControl(handle, ES_DEVICE_CONTROL_BLOCK_GET_INFORMATION, 0, &information);

	for (uintptr_t i = 0; i < information.modelBytes; i++) {
		if (information.model[i] == 0) {
			information.modelBytes = i;
			break;
		}
	}

	for (uintptr_t i = information.modelBytes; i > 0; i--) {
		if (information.model[i - 1] == ' ') {
			information.modelBytes--;
		} else {
			break;
		}
	}

	return information;
}

void ConnectedDriveAdd(EsMessageDevice device) {
	if (device.type != ES_DEVICE_BLOCK) {
		return;
	}

	EsBlockDeviceInformation information = ConnectedDriveGetInformation(device.handle); 

	if (information.nestLevel || information.driveType == ES_DRIVE_TYPE_CDROM) {
		return;
	}

	// TODO EsObjectID might not necessarily fit in EsGeneric...
	EsListViewFixedItemInsert(drivesList, information.model, information.modelBytes, device.id);
	connectedDrives.Add(device);
}

void ConnectedDriveRemove(EsMessageDevice device) {
	if (device.id == selectedDriveID) {
		EsElementDestroyContents(driveInformation);
		EsTextDisplayCreate(driveInformation, ES_CELL_H_FILL | ES_CELL_V_FILL, &styleDrivesSelectHint, INTERFACE_STRING(InstallerDriveRemoved));
		selectedDriveID = 0;
		EsElementSetDisabled(installButton, true);
	}

	for (uintptr_t i = 0; i < connectedDrives.Length(); i++) {
		if (connectedDrives[i].id == device.id) {
			EsListViewFixedItemRemove(drivesList, device.id);
			connectedDrives.Delete(i);
			return;
		}
	}
}

void ConnectedDriveSelect(uintptr_t index) {
	EsMessageDevice device = connectedDrives[index];
	if (selectedDriveID == device.id) return;
	selectedDriveID = device.id;
	EsElementSetDisabled(installButton, true);

	EsBlockDeviceInformation information = ConnectedDriveGetInformation(device.handle);
	EsElementDestroyContents(driveInformation);

	EsPanel *nameRow = EsPanelCreate(driveInformation, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL);
	EsIconDisplayCreate(nameRow, ES_FLAGS_DEFAULT, 0, EsIconIDFromDriveType(information.driveType));
	EsSpacerCreate(nameRow, ES_CELL_V_FILL, 0, 8, 0);
	EsTextDisplayCreate(nameRow, ES_CELL_H_FILL, ES_STYLE_TEXT_HEADING2, information.model, information.modelBytes);
	EsSpacerCreate(driveInformation, ES_CELL_H_FILL, 0, 0, 16);

	EsPanel *messageRow = EsPanelCreate(driveInformation, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL);
	EsIconDisplay *statusIcon = EsIconDisplayCreate(messageRow, ES_CELL_V_TOP, ES_STYLE_ICON_DISPLAY_SMALL, ES_ICON_DIALOG_INFORMATION);
	EsSpacerCreate(messageRow, ES_CELL_V_FILL, 0, 8, 0);

	// TODO Check there are no file systems already on the drive.

	bool showCapacity = true;

	if (information.readOnly) {
		EsIconDisplaySetIcon(statusIcon, ES_ICON_DIALOG_ERROR);
		EsTextDisplayCreate(messageRow, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(InstallerDriveReadOnly));
		showCapacity = false;
	} else if (information.sectorSize * information.sectorCount < metadata->totalUncompressedBytes + PARTITION_OVERHEAD) {
		EsIconDisplaySetIcon(statusIcon, ES_ICON_DIALOG_ERROR);
		EsTextDisplayCreate(messageRow, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(InstallerDriveNotEnoughSpace));
	} else {
		EsTextDisplayCreate(messageRow, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(InstallerDriveOkay));
		EsElementSetDisabled(installButton, false);
	}

	if (showCapacity) {
		// TODO Localization.
		char buffer[128];
		size_t bytes = EsStringFormat(buffer, sizeof(buffer), "Minimum space required: %D", metadata->totalUncompressedBytes + PARTITION_OVERHEAD);
		EsSpacerCreate(driveInformation, ES_CELL_H_FILL, 0, 0, 10);
		EsTextDisplayCreate(driveInformation, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, buffer, bytes);
		bytes = EsStringFormat(buffer, sizeof(buffer), "Drive capacity: %D", information.sectorSize * information.sectorCount);
		EsTextDisplayCreate(driveInformation, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, buffer, bytes);
	}
}

/////////////////////////////////////////////

int DrivesListMessage(EsElement *element, EsMessage *message) {
	if (message->type == ES_MSG_LIST_VIEW_SELECT) {
		EsGeneric deviceID;

		if (EsListViewFixedItemGetSelected(((EsListView *) element), &deviceID)) {
			for (uintptr_t i = 0; i < connectedDrives.Length(); i++) {
				if (connectedDrives[i].id == deviceID.u) {
					ConnectedDriveSelect(i);
					break;
				}
			}
		}
	}

	return 0;
}

void ButtonViewLicenses(EsInstance *, EsElement *, EsCommand *) {
	EsPanelSwitchTo(switcher, panelLicenses, ES_TRANSITION_FADE_IN);
}

void ButtonInstallOptions(EsInstance *, EsElement *, EsCommand *) {
	EsPanelSwitchTo(switcher, panelInstallOptions, ES_TRANSITION_FADE_IN);
}


void ButtonShutdown(EsInstance *, EsElement *, EsCommand *) {
	EsSystemShowShutdownDialog();
}

void ButtonInstall(EsInstance *, EsElement *, EsCommand *) {
	// TODO.
}

int SwitcherMessage(EsElement *, EsMessage *message) {
	if (message->type == ES_MSG_GET_WIDTH || message->type == ES_MSG_GET_HEIGHT) {
		return EsMessageSend(panelInstallOptions, message);
	}

	return 0;
}

void _start() {
	_init();

	metadata = (InstallerMetadata *) EsFileReadAll(EsLiteral("0:/installer_metadata.dat"), nullptr);
	EsAssert(metadata);

	{
		EsWindow *window = EsWindowCreate(_EsInstanceCreate(sizeof(EsInstance), nullptr), ES_WINDOW_PLAIN);
		EsHandle handle = EsWindowGetHandle(window);
		window->instance->window = window;

		EsRectangle screen;
		EsSyscall(ES_SYSCALL_SCREEN_BOUNDS_GET, 0, (uintptr_t) &screen, 0, 0);
		EsSyscall(ES_SYSCALL_WINDOW_MOVE, handle, (uintptr_t) &screen, 0, 0);
		EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, handle, ES_WINDOW_SOLID_TRUE, 0, ES_WINDOW_PROPERTY_SOLID);
		EsSyscall(ES_SYSCALL_WINDOW_SET_PROPERTY, handle, 0, 0, ES_WINDOW_PROPERTY_FOCUSED);

		EsPanel *sheet = EsPanelCreate(window, ES_PANEL_VERTICAL | ES_CELL_PUSH | ES_CELL_CENTER, ES_STYLE_INSTALLER_ROOT);
		switcher = EsPanelCreate(sheet, ES_CELL_H_FILL | ES_PANEL_SWITCHER);
		switcher->messageUser = SwitcherMessage;

		{
			panelInstallOptions = EsPanelCreate(switcher, ES_CELL_H_FILL, &styleRoot);
			EsPanelSwitchTo(switcher, panelInstallOptions, ES_TRANSITION_NONE);
			EsTextDisplayCreate(panelInstallOptions, ES_CELL_H_FILL, ES_STYLE_TEXT_HEADING0, INTERFACE_STRING(InstallerTitle));

			EsPanel *drivesPanel = EsPanelCreate(panelInstallOptions, ES_CELL_H_FILL, ES_STYLE_PANEL_INSET);
			EsPanel *drivesSplit = EsPanelCreate(drivesPanel, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL);
			EsPanel *drivesLeft = EsPanelCreate(drivesSplit, ES_CELL_H_FILL);
			EsTextDisplayCreate(drivesLeft, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(InstallerDrivesList));
			EsSpacerCreate(drivesLeft, ES_CELL_H_FILL, 0, 0, 8);
			drivesList = EsListViewCreate(drivesLeft, ES_CELL_H_FILL | ES_LIST_VIEW_CHOICE_SELECT | ES_LIST_VIEW_FIXED_ITEMS, ES_STYLE_LIST_CHOICE_BORDERED);
			drivesList->messageUser = DrivesListMessage;
			EsElementFocus(drivesList);
			EsSpacerCreate(drivesSplit, ES_CELL_V_FILL, 0, 25, 0);
			driveInformation = EsPanelCreate(drivesSplit, ES_CELL_H_FILL | ES_CELL_V_FILL);
			EsTextDisplayCreate(driveInformation, ES_CELL_H_FILL | ES_CELL_V_FILL, &styleDrivesSelectHint, INTERFACE_STRING(InstallerDrivesSelectHint));

			EsPanel *buttonsRow = EsPanelCreate(panelInstallOptions, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL, &styleButtonsRow);
			EsButtonOnCommand(EsButtonCreate(buttonsRow, ES_FLAGS_DEFAULT, 0, INTERFACE_STRING(InstallerViewLicenses)), ButtonViewLicenses);
			EsButtonOnCommand(EsButtonCreate(buttonsRow, ES_FLAGS_DEFAULT, 0, INTERFACE_STRING(DesktopShutdownAction)), ButtonShutdown);
			EsSpacerCreate(buttonsRow, ES_CELL_H_FILL);
			installButton = EsButtonCreate(buttonsRow, ES_ELEMENT_DISABLED, 0, INTERFACE_STRING(InstallerInstall));
			EsButtonOnCommand(installButton, ButtonInstall);
		}

		{
			panelLicenses = EsPanelCreate(switcher, ES_CELL_FILL, &styleRoot);
			EsTextbox *textbox = EsTextboxCreate(panelLicenses, ES_CELL_FILL | ES_TEXTBOX_MULTILINE);
			EsElementSetDisabled(textbox);
			size_t bytes;
			char *data = (char *) EsFileReadAll(EsLiteral("0:/installer_licenses.txt"), &bytes);
			EsTextboxInsert(textbox, data, bytes);
			EsHeapFree(data);
			EsButtonOnCommand(EsButtonCreate(panelLicenses, ES_CELL_H_LEFT, 0, INTERFACE_STRING(InstallerGoBack)), ButtonInstallOptions);
		}

		// EsWindowCreate(instance, ES_WINDOW_INSPECTOR);
	}

	EsDeviceEnumerate([] (EsMessageDevice device, EsGeneric) {
		ConnectedDriveAdd(device);
	}, 0);

	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_DEVICE_CONNECTED) {
			ConnectedDriveAdd(message->device);
		} else if (message->type == ES_MSG_DEVICE_DISCONNECTED) {
			ConnectedDriveRemove(message->device);
		}
	}
}
