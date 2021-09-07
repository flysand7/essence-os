#define ES_INSTANCE_TYPE Instance
#include <essence.h>

#include <shared/strings.cpp>

// TODO Document save/load model, then merge into API.
// TODO Replace toolbar, then merge into API.
// TODO Merge Format menu into API.
// TODO Word wrap (textbox feature).

// TODO Possible extension features:
// - Block selection
// - Folding
// - Tab settings and auto-indent
// - Macros
// - Status bar
// - Goto line
// - Find in files
// - Convert case
// - Sort lines
// - Trim trailing space
// - Indent/comment/join/split shortcuts

#define SETTINGS_FILE "|Settings:/Default.ini"

const EsInstanceClassEditorSettings editorSettings = {
	INTERFACE_STRING(TextEditorNewFileName),
	INTERFACE_STRING(TextEditorNewDocument),
	ES_ICON_TEXT,
};

const EsStyle styleFormatPopupColumn = {
	.metrics = {
		.mask = ES_THEME_METRICS_GAP_MAJOR,
		.gapMajor = 5,
	},
};

struct Instance : EsInstance {
	EsTextbox *textboxDocument,
		  *textboxSearch;

	EsElement *toolbarMain, *toolbarSearch;

	EsTextDisplay *displaySearch;

	EsButton *buttonFormat;

	EsCommand commandFindNext,
		  commandFindPrevious,
		  commandFind,
		  commandFormat;

	uint32_t syntaxHighlightingLanguage;
	int32_t textSize;
};

int32_t globalTextSize = 10;

void Find(Instance *instance, bool backwards) {
	EsWindowSwitchToolbar(instance->window, instance->toolbarSearch, ES_TRANSITION_SLIDE_UP);

	size_t needleBytes;
	char *needle = EsTextboxGetContents(instance->textboxSearch, &needleBytes);

	int32_t line0, byte0, line1, byte1;
	EsTextboxGetSelection(instance->textboxDocument, &line0, &byte0, &line1, &byte1);

	if (backwards) {
		if (line1 < line0) {
			line0 = line1;
			byte0 = byte1;
		} else if (line1 == line0 && byte1 < byte0) {
			byte0 = byte1;
		}
	} else {
		if (line1 > line0) {
			line0 = line1;
			byte0 = byte1;
		} else if (line1 == line0 && byte1 > byte0) {
			byte0 = byte1;
		}
	}

	bool found = EsTextboxFind(instance->textboxDocument, needle, needleBytes, &line0, &byte0, backwards ? ES_TEXTBOX_FIND_BACKWARDS : ES_FLAGS_DEFAULT);

	if (found) {
		EsTextDisplaySetContents(instance->displaySearch, "");
		EsTextboxSetSelection(instance->textboxDocument, line0, byte0, line0, byte0 + needleBytes);
		EsTextboxEnsureCaretVisible(instance->textboxDocument, true);
		EsElementFocus(instance->textboxDocument);
	} else if (!needleBytes) {
		EsTextDisplaySetContents(instance->displaySearch, INTERFACE_STRING(CommonSearchPrompt2));
		EsElementFocus(instance->textboxSearch);
	} else {
		EsTextDisplaySetContents(instance->displaySearch, INTERFACE_STRING(CommonSearchNoMatches));
		EsElementFocus(instance->textboxSearch);
	}

	EsHeapFree(needle);
}

void SetLanguage(Instance *instance, uint32_t newLanguage) {
	EsFont font = {};
	font.family = newLanguage ? ES_FONT_MONOSPACED : ES_FONT_SANS;
	font.weight = ES_FONT_REGULAR;
	EsTextboxSetFont(instance->textboxDocument, font);

	instance->syntaxHighlightingLanguage = newLanguage;
	EsTextboxSetupSyntaxHighlighting(instance->textboxDocument, newLanguage);
}

void FormatPopupCreate(Instance *instance) {
	EsMenu *menu = EsMenuCreate(instance->buttonFormat, ES_FLAGS_DEFAULT);
	EsPanel *panel = EsPanelCreate(menu, ES_PANEL_HORIZONTAL, ES_STYLE_PANEL_POPUP);
	
	{
		EsPanel *column = EsPanelCreate(panel, ES_FLAGS_DEFAULT, &styleFormatPopupColumn);
		EsTextDisplayCreate(column, ES_CELL_H_EXPAND, ES_STYLE_TEXT_LABEL, INTERFACE_STRING(CommonFormatSize));
		EsListView *list = EsListViewCreate(column, ES_LIST_VIEW_CHOICE_SELECT | ES_LIST_VIEW_FIXED_ITEMS, ES_STYLE_LIST_CHOICE_BORDERED);

		const int presetSizes[] = {
			8, 9, 10, 11, 12, 13,
			14, 16,
			18, 24, 30,
			36, 48, 60,
			72, 96, 120, 144,
		};

		size_t presetSizeCount = sizeof(presetSizes) / sizeof(presetSizes[0]);
		int currentSize = instance->textSize;
		char buffer[64];

		if (currentSize < presetSizes[0]) {
			// The current size is not in the list; add it.
			EsListViewFixedItemInsert(list, buffer, EsStringFormat(buffer, sizeof(buffer), "%d pt", currentSize), currentSize);
		}

		for (uintptr_t i = 0; i < presetSizeCount; i++) {
			EsListViewFixedItemInsert(list, buffer, EsStringFormat(buffer, sizeof(buffer), "%d pt", presetSizes[i]), presetSizes[i]);

			if (currentSize > presetSizes[i] && (i == presetSizeCount - 1 || (i != presetSizeCount - 1 && currentSize < presetSizes[i + 1]))) {
				// The current size is not in the list; add it.
				EsListViewFixedItemInsert(list, buffer, EsStringFormat(buffer, sizeof(buffer), "%d pt", currentSize), currentSize);
			}
		}

		EsListViewFixedItemSelect(list, currentSize);

		list->messageUser = [] (EsElement *element, EsMessage *message) {
			if (message->type == ES_MSG_LIST_VIEW_SELECT) {
				Instance *instance = element->instance;
				EsGeneric newSize;

				if (EsListViewFixedItemGetSelected(((EsListView *) element), &newSize)) {
					globalTextSize = instance->textSize = newSize.u;
					EsTextboxSetTextSize(instance->textboxDocument, newSize.u);
				}
			}

			return 0;
		};
	}

	{
		EsPanel *column = EsPanelCreate(panel, ES_FLAGS_DEFAULT, &styleFormatPopupColumn);
		EsTextDisplayCreate(column, ES_CELL_H_EXPAND, ES_STYLE_TEXT_LABEL, INTERFACE_STRING(CommonFormatLanguage));
		EsListView *list = EsListViewCreate(column, ES_LIST_VIEW_CHOICE_SELECT | ES_LIST_VIEW_FIXED_ITEMS, ES_STYLE_LIST_CHOICE_BORDERED);
		EsListViewFixedItemInsert(list, INTERFACE_STRING(CommonFormatPlainText), 0);
		EsListViewFixedItemInsert(list, "C/C++", -1, ES_SYNTAX_HIGHLIGHTING_LANGUAGE_C);
		EsListViewFixedItemInsert(list, "Ini file", -1, ES_SYNTAX_HIGHLIGHTING_LANGUAGE_INI);
		EsListViewFixedItemSelect(list, instance->syntaxHighlightingLanguage);

		list->messageUser = [] (EsElement *element, EsMessage *message) {
			if (message->type == ES_MSG_LIST_VIEW_SELECT) {
				Instance *instance = element->instance;
				EsGeneric newLanguage;

				if (EsListViewFixedItemGetSelected(((EsListView *) element), &newLanguage)) {
					SetLanguage(instance, newLanguage.u);
				}
			}

			return 0;
		};
	}

	EsMenuShow(menu);
}

void ProcessApplicationMessage(EsMessage *message) {
	if (message->type == ES_MSG_INSTANCE_CREATE) {
		Instance *instance = EsInstanceCreate(message, INTERFACE_STRING(TextEditorTitle));
		EsInstanceSetClassEditor(instance, &editorSettings);

		EsWindow *window = instance->window;
		EsWindowSetIcon(window, ES_ICON_ACCESSORIES_TEXT_EDITOR);
		EsButton *button;

		// Commands:

		uint32_t stableID = 1;

		EsCommandRegister(&instance->commandFindNext, instance, [] (Instance *instance, EsElement *, EsCommand *) {
			Find(instance, false);
		}, stableID++, "F3"); 

		EsCommandRegister(&instance->commandFindPrevious, instance, [] (Instance *instance, EsElement *, EsCommand *) {
			Find(instance, true);
		}, stableID++, "Shift+F3"); 

		EsCommandRegister(&instance->commandFind, instance, [] (Instance *instance, EsElement *, EsCommand *) {
			EsWindowSwitchToolbar(instance->window, instance->toolbarSearch, ES_TRANSITION_ZOOM_OUT);
			EsElementFocus(instance->textboxSearch);
		}, stableID++, "Ctrl+F");

		EsCommandRegister(&instance->commandFormat, instance, [] (Instance *instance, EsElement *, EsCommand *) {
			FormatPopupCreate(instance);
		}, stableID++, "Ctrl+Alt+T"); 

		EsCommandSetDisabled(&instance->commandFindNext, false);
		EsCommandSetDisabled(&instance->commandFindPrevious, false);
		EsCommandSetDisabled(&instance->commandFind, false);
		EsCommandSetDisabled(&instance->commandFormat, false);

		// Content:

		EsPanel *panel = EsPanelCreate(window, ES_CELL_FILL, ES_STYLE_PANEL_WINDOW_DIVIDER);
		uint64_t documentFlags = ES_CELL_FILL | ES_TEXTBOX_MULTILINE | ES_TEXTBOX_ALLOW_TABS | ES_TEXTBOX_MARGIN;
		instance->textboxDocument = EsTextboxCreate(panel, documentFlags, ES_STYLE_TEXTBOX_NO_BORDER);
		instance->textboxDocument->cName = "document";
		instance->textSize = globalTextSize;
		EsTextboxSetTextSize(instance->textboxDocument, globalTextSize);
		EsTextboxSetUndoManager(instance->textboxDocument, instance->undoManager);
		EsElementFocus(instance->textboxDocument);

		// Main toolbar:

		EsElement *toolbarMain = instance->toolbarMain = EsWindowGetToolbar(window, true);

		EsToolbarAddFileMenu(toolbarMain);
		
		button = EsButtonCreate(toolbarMain, ES_FLAGS_DEFAULT, {}, INTERFACE_STRING(CommonSearchOpen));
		button->accessKey = 'S';
		EsButtonSetIcon(button, ES_ICON_EDIT_FIND_SYMBOLIC);

		EsButtonOnCommand(button, [] (Instance *instance, EsElement *, EsCommand *) {
			EsWindowSwitchToolbar(instance->window, instance->toolbarSearch, ES_TRANSITION_SLIDE_UP);
			EsElementFocus(instance->textboxSearch);
		});

		button = EsButtonCreate(toolbarMain, ES_BUTTON_DROPDOWN, {}, INTERFACE_STRING(CommonFormatPopup));
		button->accessKey = 'M';
		EsButtonSetIcon(button, ES_ICON_FORMAT_TEXT_LARGER_SYMBOLIC);
		EsCommandAddButton(&instance->commandFormat, button);
		instance->buttonFormat = button;

		EsWindowSwitchToolbar(window, toolbarMain, ES_TRANSITION_NONE);

		// Search toolbar:

		EsElement *toolbarSearch = instance->toolbarSearch = EsWindowGetToolbar(window, true);

		button = EsButtonCreate(toolbarSearch, ES_FLAGS_DEFAULT, 0);
		button->cName = "go back", button->accessKey = 'X';
		EsButtonSetIcon(button, ES_ICON_GO_FIRST_SYMBOLIC);

		EsButtonOnCommand(button, [] (Instance *instance, EsElement *, EsCommand *) {
			EsWindowSwitchToolbar(instance->window, instance->toolbarMain, ES_TRANSITION_SLIDE_DOWN);
		});

		EsPanel *section = EsPanelCreate(toolbarSearch, ES_PANEL_HORIZONTAL);
		EsTextDisplayCreate(section, ES_FLAGS_DEFAULT, 0, INTERFACE_STRING(CommonSearchPrompt));

		instance->textboxSearch = EsTextboxCreate(section, ES_FLAGS_DEFAULT, {});
		instance->textboxSearch->cName = "search textbox";
		instance->textboxSearch->accessKey = 'S';

		instance->textboxSearch->messageUser = [] (EsElement *element, EsMessage *message) {
			Instance *instance = element->instance;

			if (message->type == ES_MSG_KEY_DOWN && message->keyboard.scancode == ES_SCANCODE_ENTER) {
				EsCommand *command = (message->keyboard.modifiers & ES_MODIFIER_SHIFT) ? &instance->commandFindPrevious : &instance->commandFindNext;
				command->callback(instance, element, command);
				return ES_HANDLED;
			} else if (message->type == ES_MSG_KEY_DOWN && message->keyboard.scancode == ES_SCANCODE_ESCAPE) {
				EsWindowSwitchToolbar(instance->window, instance->toolbarMain, ES_TRANSITION_SLIDE_DOWN);
				EsElementFocus(instance->textboxDocument);
				return ES_HANDLED;
			} else if (message->type == ES_MSG_FOCUSED_START) {
				EsTextboxSelectAll(instance->textboxSearch);
			}

			return 0;
		};

		instance->displaySearch = EsTextDisplayCreate(toolbarSearch, ES_CELL_H_FILL, {}, "");

		button = EsButtonCreate(toolbarSearch, ES_FLAGS_DEFAULT, {}, INTERFACE_STRING(CommonSearchNext));
		button->accessKey = 'N';
		EsCommandAddButton(&instance->commandFindNext, button);
		button = EsButtonCreate(toolbarSearch, ES_FLAGS_DEFAULT, {}, INTERFACE_STRING(CommonSearchPrevious));
		button->accessKey = 'P';
		EsCommandAddButton(&instance->commandFindPrevious, button);
	} else if (message->type == ES_MSG_INSTANCE_OPEN) {
		Instance *instance = message->instanceOpen.instance;

		size_t fileSize;
		char *file = (char *) EsFileStoreReadAll(message->instanceOpen.file, &fileSize);

		if (!file) {
			EsInstanceOpenComplete(message, false);
		} else if (!EsUTF8IsValid(file, fileSize)) {
			EsInstanceOpenComplete(message, false);
		} else {
			EsTextboxSelectAll(instance->textboxDocument);
			EsTextboxInsert(instance->textboxDocument, file, fileSize);
			EsTextboxSetSelection(instance->textboxDocument, 0, 0, 0, 0);
			EsElementRelayout(instance->textboxDocument);

			if (EsStringEndsWith(message->instanceOpen.name, message->instanceOpen.nameBytes, EsLiteral(".c"), true)
					|| EsStringEndsWith(message->instanceOpen.name, message->instanceOpen.nameBytes, EsLiteral(".cpp"), true)
					|| EsStringEndsWith(message->instanceOpen.name, message->instanceOpen.nameBytes, EsLiteral(".h"), true)) {
				SetLanguage(instance, ES_SYNTAX_HIGHLIGHTING_LANGUAGE_C);
			} else if (EsStringEndsWith(message->instanceOpen.name, message->instanceOpen.nameBytes, EsLiteral(".ini"), true)) {
				SetLanguage(instance, ES_SYNTAX_HIGHLIGHTING_LANGUAGE_INI);
			} else {
				SetLanguage(instance, 0);
			}

			EsInstanceOpenComplete(message, true);
		}

		EsHeapFree(file);
	} else if (message->type == ES_MSG_INSTANCE_SAVE) {
		Instance *instance = message->instanceSave.instance;
		size_t byteCount;
		char *contents = EsTextboxGetContents(instance->textboxDocument, &byteCount);
		EsFileStoreWriteAll(message->instanceSave.file, contents, byteCount);
		EsHeapFree(contents);
		EsInstanceSaveComplete(message, true);
	} else if (message->type == ES_MSG_APPLICATION_EXIT) {
		EsBuffer buffer = {};
		buffer.canGrow = true;
		EsBufferFormat(&buffer, "[general]\ntext_size=%d\n", globalTextSize);
		EsFileWriteAll(EsLiteral(SETTINGS_FILE), buffer.out, buffer.position);
		EsHeapFree(buffer.out);
	}
}

void _start() {
	_init();

	EsINIState state = { (char *) EsFileReadAll(EsLiteral(SETTINGS_FILE), &state.bytes) };

	while (EsINIParse(&state)) {
		if (0 == EsStringCompareRaw(state.section, state.sectionBytes, EsLiteral("general"))) {
			if (0 == EsStringCompareRaw(state.key, state.keyBytes, EsLiteral("text_size"))) {
				globalTextSize = EsIntegerParse(state.value, state.valueBytes);
			}
		}
	}

	while (true) {
		ProcessApplicationMessage(EsMessageReceive());
	}
}
