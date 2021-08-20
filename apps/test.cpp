#include <essence.h>
#include <shared/strings.cpp>
// #include <shared/stb_ds.h>

EsTextbox *textbox;

#if 0

char *master;
char **undo;

void OffsetToLineAndByte(uintptr_t offset, int32_t *_line, int32_t *_byte) {
	int32_t line = 0, byte = 0;

	for (uintptr_t i = 0; i < offset; i++) {
		if (master[i] == '\n') {
			line++;
			byte = 0;
		} else {
			byte++;
		}
	}

	*_line = line;
	*_byte = byte;
}

void Compare() {
	size_t bytes;
	char *contents = EsTextboxGetContents(textbox, &bytes);
	// EsPrint("\tContents: '%e'\n\tMaster:   '%e'\n", bytes, contents, arrlenu(master), master);
	EsAssert(bytes == arrlenu(master));
	EsAssert(0 == EsMemoryCompare(master, contents, bytes));
	EsHeapFree(contents);
}

void FakeUndoItem(const void *, EsUndoManager *manager, EsMessage *message) {
	if (message->type == ES_MSG_UNDO_INVOKE) {
		EsUndoPush(manager, FakeUndoItem, nullptr, 0); 
	}
}

void AddUndoItem() {
	char *copy = nullptr;
	arrsetlen(copy, arrlenu(master));
	EsMemoryCopy(copy, master, arrlenu(copy));
	arrput(undo, copy);
}

void Complete() {
	EsUndoPush(textbox->instance->undoManager, FakeUndoItem, nullptr, 0); 
	EsUndoEndGroup(textbox->instance->undoManager);
}

void Insert(uintptr_t offset, const char *string, size_t stringBytes) {
	if (!stringBytes) return;
	AddUndoItem();
	// EsPrint("Insert '%e' at %d.\n", stringBytes, string, offset);
	int32_t line, byte;
	OffsetToLineAndByte(offset, &line, &byte);
	EsTextboxSetSelection(textbox, line, byte, line, byte);
	EsTextboxInsert(textbox, string, stringBytes);
	arrinsn(master, offset, stringBytes);
	EsMemoryCopy(master + offset, string, stringBytes);
	Compare();
	Complete();
}

void Delete(uintptr_t from, uintptr_t to) {
	if (from == to) return;
	AddUndoItem();
	// EsPrint("Delete from %d to %d.\n", from, to);
	int32_t fromLine, fromByte, toLine, toByte;
	OffsetToLineAndByte(from, &fromLine, &fromByte);
	OffsetToLineAndByte(to, &toLine, &toByte);
	EsTextboxSetSelection(textbox, fromLine, fromByte, toLine, toByte);
	EsTextboxInsert(textbox, 0, 0);
	if (to > from) arrdeln(master, from, to - from);
	else arrdeln(master, to, from - to);
	Compare();
	Complete();
}

void Test() {
	EsRandomSeed(10); 
	EsTextboxSetUndoManager(textbox, textbox->instance->undoManager);

	while (true) {
		uint8_t action = EsRandomU8();

		if (action < 0x70) {
			size_t stringBytes = EsRandomU8() & 0x1F;
			char string[0x20];

			for (uintptr_t i = 0; i < stringBytes; i++) {
				string[i] = EsRandomU8() < 0x40 ? '\n' : ((EsRandomU8() % 26) + 'a');
			}

			Insert(EsRandomU64() % (arrlenu(master) + 1), string, stringBytes);
		} else if (action < 0xE0) {
			if (arrlenu(master)) {
				Delete(EsRandomU64() % arrlenu(master), EsRandomU64() % arrlenu(master));
			}
		} else {
			if (!EsUndoIsEmpty(textbox->instance->undoManager, false)) {
				// EsPrint("Undo.\n");
				EsUndoInvokeGroup(textbox->instance->undoManager, false);
				arrfree(master);
				master = arrlast(undo);
				arrpop(undo);
				Compare();
			}
		}
	}
}

#endif

const EsStyle stylePanel = {
	.inherit = ES_STYLE_PANEL_WINDOW_BACKGROUND,

	.metrics = {
		.mask = ES_THEME_METRICS_INSETS | ES_THEME_METRICS_GAP_MAJOR,
		.insets = ES_RECT_1(20),
		.gapMajor = 1,
	},
};

int TestCanvasMessage(EsElement *, EsMessage *message) {
	if (message->type == ES_MSG_PAINT) {
		size_t dataBytes;
		const void *data = EsEmbeddedFileGet("test", &dataBytes);
		if (data) EsDrawVectorFile(message->painter, EsPainterBoundsClient(message->painter), data, dataBytes);
	} else if (message->type == ES_MSG_GET_WIDTH) {
		message->measure.width = 256;
	} else if (message->type == ES_MSG_GET_HEIGHT) {
		message->measure.height = 256;
	}

	return 0;
}

void InitialiseInstance(EsInstance *instance) {
	// EsPanel *panel = EsPanelCreate(instance->window, ES_CELL_FILL, ES_STYLE_PANEL_WINDOW_DIVIDER);
	// textbox = EsTextboxCreate(panel, ES_CELL_FILL | ES_TEXTBOX_ALLOW_TABS | ES_TEXTBOX_MULTILINE, ES_STYLE_TEXTBOX_NO_BORDER);
	// Test();

	EsPanel *panel = EsPanelCreate(instance->window, ES_CELL_FILL, &stylePanel);
	EsButtonCreate(panel, ES_BUTTON_CHECKBOX, 0, "Checkbox");
	EsButtonOnCommand(EsButtonCreate(panel, ES_FLAGS_DEFAULT, 0, "Crash"), [] (EsInstance *, EsElement *, EsCommand *) { EsAssert(false); });
	EsButtonOnCommand(EsButtonCreate(panel, ES_FLAGS_DEFAULT, 0, "Hang"), [] (EsInstance *, EsElement *, EsCommand *) { while (true); });
	EsButtonOnCommand(EsButtonCreate(panel, ES_FLAGS_DEFAULT, 0, "Wait"), [] (EsInstance *, EsElement *, EsCommand *) { EsSleep(8000); });
	EsButtonOnCommand(EsButtonCreate(panel, ES_FLAGS_DEFAULT, 0, "Wait, then crash"), [] (EsInstance *, EsElement *, EsCommand *) { EsSleep(8000); EsAssert(false); });

	EsButtonOnCommand(EsButtonCreate(panel, ES_FLAGS_DEFAULT, 0, "Crash 2"), [] (EsInstance *, EsElement *, EsCommand *) { 
		EsSyscall(ES_SYSCALL_WAIT, 0x8000000000000000, 1, 0, 0); 
	});

	EsButtonOnCommand(EsButtonCreate(panel, ES_FLAGS_DEFAULT, 0, "Crash 3"), [] (EsInstance *, EsElement *, EsCommand *) { 
		EsSyscall(ES_SYSCALL_PRINT, 0, 16, 0, 0);
	});

	EsButtonOnCommand(EsButtonCreate(panel, ES_FLAGS_DEFAULT, 0, "Crash 4"), [] (EsInstance *, EsElement *, EsCommand *) { 
		EsSyscall(ES_SYSCALL_WAIT, 0x0000FFFFFFFFFFFF, 1, 0, 0); 
	});

	EsButtonOnCommand(EsButtonCreate(panel, ES_FLAGS_DEFAULT, 0, "Crash 5"), [] (EsInstance *, EsElement *, EsCommand *) { 
		EsSyscall(ES_SYSCALL_WAIT, 0x00000FFFFFFFFFFF, 1, 0, 0); 
	});

	EsButtonOnCommand(EsButtonCreate(panel, ES_FLAGS_DEFAULT, 0, "Announcement 1"), [] (EsInstance *, EsElement *element, EsCommand *) { 
		EsRectangle bounds = EsElementGetWindowBounds(element);
		EsAnnouncementShow(element->window, ES_FLAGS_DEFAULT, (bounds.l + bounds.r) / 2, (bounds.t + bounds.b) / 2, "Hello, world!", -1); 
	});

	EsButtonOnCommand(EsButtonCreate(panel, ES_FLAGS_DEFAULT, 0, "Announcement 2"), [] (EsInstance *, EsElement *element, EsCommand *) { 
		EsRectangle bounds = EsElementGetWindowBounds(element);
		EsAnnouncementShow(element->window, ES_FLAGS_DEFAULT, (bounds.l + bounds.r) / 2, (bounds.t + bounds.b) / 2, INTERFACE_STRING(DesktopApplicationStartupError)); 
	});

	EsButtonCreate(panel, ES_FLAGS_DEFAULT, 0, "Push");
	EsButtonCreate(panel, ES_FLAGS_DEFAULT, 0, "Push");
	EsTextboxUseNumberOverlay(EsTextboxCreate(panel, ES_TEXTBOX_EDIT_BASED), true);
	EsCustomElementCreate(panel)->messageUser = TestCanvasMessage;
}

void _start() {
	_init();

	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_INSTANCE_CREATE) {
			InitialiseInstance(EsInstanceCreate(message, "Test App"));
		}
	}
}
