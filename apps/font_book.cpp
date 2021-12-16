// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

#define ES_INSTANCE_TYPE Instance
#include <essence.h>
#include <shared/array.cpp>
#include <shared/strings.cpp>

// TODO Previewing font files from the database.
// TODO Installing fonts.
// TODO Character map.
// TODO Searching/filtering fonts.
// TODO Single instance.

#define SETTINGS_FILE "|Settings:/Default.ini"

struct Instance : EsInstance {
	Array<EsFontInformation> fonts;

	EsPanel *switcher;

	EsListView *fontList;
	EsTextbox *fontSizeTextbox;
	EsTextbox *previewTextTextbox;

	EsPanel *fontPreview;
	uint16_t fontPreviewID;

	EsElement *fontListToolbar;
	EsElement *fontPreviewToolbar;

	int fontSize, fontVariant;
	char *previewText;
	size_t previewTextBytes;
};

EsStyle styleFontList = {
	.inherit = ES_STYLE_PANEL_FILLED,

	.metrics = {
		.mask = ES_THEME_METRICS_GAP_MINOR | ES_THEME_METRICS_GAP_WRAP | ES_THEME_METRICS_INSETS,
		.insets = ES_RECT_1(25),
		.gapMinor = 15,
		.gapWrap = 15,
	},
};

EsStyle styleFontItem = {
	.inherit = ES_STYLE_PANEL_SHEET,

	.metrics = {
		.mask = ES_THEME_METRICS_PREFERRED_WIDTH | ES_THEME_METRICS_PREFERRED_HEIGHT,
		.preferredWidth = 300,
		.preferredHeight = 250,
	},
};

EsStyle styleFontInformationPanel = {
	.metrics = {
		.mask = ES_THEME_METRICS_INSETS | ES_THEME_METRICS_GAP_MAJOR,
		.insets = ES_RECT_1(15),
		.gapMajor = 10,
	},
};

EsStyle styleFontName = {
	.inherit = ES_STYLE_TEXT_LABEL_SECONDARY,

	.metrics = {
		.mask = ES_THEME_METRICS_TEXT_SIZE,
		.textSize = 10,
	},
};

EsStyle styleFontInformationRow = {
	.metrics = {
		.mask = ES_THEME_METRICS_GAP_MAJOR,
		.gapMajor = 10,
	},
};

EsStyle styleFontPreviewPage = {
	.inherit = ES_STYLE_PANEL_DOCUMENT,

	.metrics = {
		.mask = ES_THEME_METRICS_INSETS,
		.insets = ES_RECT_1(20),
	},
};

inline int ClampInteger(int low, int high, int integer) {
	if (integer < low) return low;
	if (integer > high) return high;
	return integer;
}

bool LoadSettings(Instance *instance) {
	EsINIState state = {};
	char *file = (char *) EsFileReadAll(EsLiteral(SETTINGS_FILE), &state.bytes);

	if (!state.buffer) {
		return false;
	}

	while (EsINIParse(&state)) {
		if (state.value && 0 == EsStringCompareRaw(state.section, state.sectionBytes, EsLiteral("preview"))) {
			if (0 == EsStringCompareRaw(state.key, state.keyBytes, EsLiteral("size"))) {
				instance->fontSize = EsIntegerParse(state.value, state.valueBytes);
			} else if (0 == EsStringCompareRaw(state.key, state.keyBytes, EsLiteral("variant"))) {
				instance->fontVariant = EsIntegerParse(state.value, state.valueBytes);
			} else if (0 == EsStringCompareRaw(state.key, state.keyBytes, EsLiteral("text"))) {
				instance->previewTextBytes = state.valueBytes;
				instance->previewText = (char *) EsHeapAllocate(instance->previewTextBytes, false);
				EsMemoryCopy(instance->previewText, state.value, instance->previewTextBytes);
			}
		}
	}

	EsHeapFree(file);
	return true;
}

void SaveSettings(Instance *instance) {
	EsBuffer buffer = { .canGrow = true };
	EsBufferFormat(&buffer, "[preview]\nsize=%d\nvariant=%d\ntext=%s\n", 
			instance->fontSize, instance->fontVariant, instance->previewTextBytes, instance->previewText);
	EsFileWriteAll(EsLiteral(SETTINGS_FILE), buffer.out, buffer.position);
	EsHeapFree(buffer.out);
}

int FontPreviewMessage(EsElement *element, EsMessage *message) {
	if (message->type == ES_MSG_PAINT) {
		// TODO Cache the text plan?
		EsTextPlanProperties properties = {};
		properties.flags = ES_TEXT_PLAN_SINGLE_USE | ES_TEXT_PLAN_TRIM_SPACES | ES_TEXT_WRAP | ES_TEXT_ELLIPSIS | ES_TEXT_PLAN_NO_FONT_SUBSTITUTION;
		EsRectangle bounds = EsPainterBoundsInset(message->painter);
		EsTextRun runs[2] = {};
		EsElementGetTextStyle(element, &runs[0].style);
		runs[0].style.font.family = element->userData.u;
		runs[0].style.font.weight = element->instance->fontVariant % 10;
		runs[0].style.font.italic = element->instance->fontVariant / 10;
		runs[0].style.size = element->instance->fontSize;
		runs[1].offset = element->instance->previewTextBytes;
		EsTextPlan *plan = EsTextPlanCreate(element, &properties, bounds, element->instance->previewText, runs, 1);
		EsDrawText(message->painter, plan, bounds); 
	}

	return 0;
}

int FontListMessage(EsElement *element, EsMessage *message) {
	Instance *instance = element->instance;

	if (message->type == ES_MSG_LIST_VIEW_CREATE_ITEM) {
		EsPanel *panel = EsPanelCreate(message->createItem.item, ES_CELL_FILL, &styleFontInformationPanel);
		EsFontInformation *font = &instance->fonts[message->createItem.index];

		EsPanel *row = EsPanelCreate(panel, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL, &styleFontInformationRow);
		// EsIconDisplayCreate(row, ES_FLAGS_DEFAULT, ES_STYLE_ICON_DISPLAY_SMALL, ES_ICON_FONT_X_GENERIC);
		EsTextDisplayCreate(row, ES_FLAGS_DEFAULT, &styleFontName, font->name, font->nameBytes);
		EsSpacerCreate(row, ES_CELL_H_FILL, ES_STYLE_SEPARATOR_HORIZONTAL);

		EsElement *preview = EsCustomElementCreate(panel, ES_CELL_FILL, ES_STYLE_TEXT_PARAGRAPH);
		preview->userData = font->id;
		preview->messageUser = FontPreviewMessage;

		size_t variants = 0;
		
		for (uintptr_t i = 0; i < 16; i++) {
			if (font->availableWeightsNormal & (1 << i)) variants++;
			if (font->availableWeightsItalic & (1 << i)) variants++;
		}

		row = EsPanelCreate(panel, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL, &styleFontInformationRow);

		char description[256]; // TODO Localization.
		size_t descriptionBytes = EsStringFormat(description, sizeof(description), "%s " HYPHENATION_POINT " %d variant%z", 
				font->categoryBytes, font->category, variants, variants == 1 ? "" : "s"); 
		EsTextDisplayCreate(row, ES_CELL_H_FILL | ES_CELL_V_BOTTOM, ES_STYLE_TEXT_LABEL_SECONDARY, description, descriptionBytes);

#if 0
		EsButton *openButton = EsButtonCreate(row, ES_BUTTON_NOT_FOCUSABLE);
		EsButtonSetIcon(openButton, ES_ICON_GO_NEXT_SYMBOLIC);
#endif
	}

	return 0;
}

void FontSizeTextboxUpdate(Instance *instance) {
	if (instance->fontSizeTextbox && instance->fontList) {
		char result[64];
		size_t resultBytes = EsStringFormat(result, sizeof(result), "%d", instance->fontSize);
		EsTextboxSelectAll(instance->fontSizeTextbox);
		EsTextboxInsert(instance->fontSizeTextbox, result, resultBytes);
		EsListViewInvalidateAll(instance->fontList);
	}
}

int FontSizeTextboxMessage(EsElement *element, EsMessage *message) {
	EsTextbox *textbox = (EsTextbox *) element;
	Instance *instance = element->instance;

	if (message->type == ES_MSG_TEXTBOX_EDIT_END) {
		char *expression = EsTextboxGetContents(textbox);
		EsCalculationValue value = EsCalculateFromUserExpression(expression); 
		EsHeapFree(expression);

		if (value.error) {
			return ES_REJECTED;
		} else {
			instance->fontSize = ClampInteger(6, 300, value.number);
			FontSizeTextboxUpdate(instance);
		}
	} else if (message->type == ES_MSG_TEXTBOX_NUMBER_DRAG_DELTA) {
		instance->fontSize = ClampInteger(6, 300, instance->fontSize + message->numberDragDelta.delta * (message->numberDragDelta.fast ? 10 : 1));
		FontSizeTextboxUpdate(instance);
	} else {
		return 0;
	}

	return ES_HANDLED;
}

int PreviewTextTextboxMessage(EsElement *element, EsMessage *message) {
	if (message->type == ES_MSG_TEXTBOX_UPDATED) {
		EsHeapFree(element->instance->previewText);
		element->instance->previewText = EsTextboxGetContents((EsTextbox *) element, &element->instance->previewTextBytes);
		if (element->instance->fontList) EsListViewInvalidateAll(element->instance->fontList);
	} else {
		return 0;
	}

	return ES_HANDLED;
}

void VariantsPopupCreate(Instance *instance, EsElement *element, EsCommand *) {
	EsMenu *menu = EsMenuCreate(element, ES_FLAGS_DEFAULT);
	EsPanel *panel = EsPanelCreate(menu, ES_PANEL_HORIZONTAL, ES_STYLE_PANEL_POPUP);

	EsListView *list = EsListViewCreate(panel, ES_LIST_VIEW_CHOICE_SELECT | ES_LIST_VIEW_FIXED_ITEMS, ES_STYLE_LIST_CHOICE_BORDERED);
	EsListViewFixedItemSetString(list, EsListViewFixedItemInsert(list,  1), 0, INTERFACE_STRING(FontBookVariantNormal100));
	EsListViewFixedItemSetString(list, EsListViewFixedItemInsert(list,  2), 0, INTERFACE_STRING(FontBookVariantNormal200));
	EsListViewFixedItemSetString(list, EsListViewFixedItemInsert(list,  3), 0, INTERFACE_STRING(FontBookVariantNormal300));
	EsListViewFixedItemSetString(list, EsListViewFixedItemInsert(list,  4), 0, INTERFACE_STRING(FontBookVariantNormal400));
	EsListViewFixedItemSetString(list, EsListViewFixedItemInsert(list,  5), 0, INTERFACE_STRING(FontBookVariantNormal500));
	EsListViewFixedItemSetString(list, EsListViewFixedItemInsert(list,  6), 0, INTERFACE_STRING(FontBookVariantNormal600));
	EsListViewFixedItemSetString(list, EsListViewFixedItemInsert(list,  7), 0, INTERFACE_STRING(FontBookVariantNormal700));
	EsListViewFixedItemSetString(list, EsListViewFixedItemInsert(list,  8), 0, INTERFACE_STRING(FontBookVariantNormal800));
	EsListViewFixedItemSetString(list, EsListViewFixedItemInsert(list,  9), 0, INTERFACE_STRING(FontBookVariantNormal900));
	EsListViewFixedItemSetString(list, EsListViewFixedItemInsert(list, 11), 0, INTERFACE_STRING(FontBookVariantItalic100));
	EsListViewFixedItemSetString(list, EsListViewFixedItemInsert(list, 12), 0, INTERFACE_STRING(FontBookVariantItalic200));
	EsListViewFixedItemSetString(list, EsListViewFixedItemInsert(list, 13), 0, INTERFACE_STRING(FontBookVariantItalic300));
	EsListViewFixedItemSetString(list, EsListViewFixedItemInsert(list, 14), 0, INTERFACE_STRING(FontBookVariantItalic400));
	EsListViewFixedItemSetString(list, EsListViewFixedItemInsert(list, 15), 0, INTERFACE_STRING(FontBookVariantItalic500));
	EsListViewFixedItemSetString(list, EsListViewFixedItemInsert(list, 16), 0, INTERFACE_STRING(FontBookVariantItalic600));
	EsListViewFixedItemSetString(list, EsListViewFixedItemInsert(list, 17), 0, INTERFACE_STRING(FontBookVariantItalic700));
	EsListViewFixedItemSetString(list, EsListViewFixedItemInsert(list, 18), 0, INTERFACE_STRING(FontBookVariantItalic800));
	EsListViewFixedItemSetString(list, EsListViewFixedItemInsert(list, 19), 0, INTERFACE_STRING(FontBookVariantItalic900));
	EsListViewFixedItemSelect(list, instance->fontVariant);

	list->messageUser = [] (EsElement *element, EsMessage *message) {
		if (message->type == ES_MSG_LIST_VIEW_SELECT) {
			Instance *instance = element->instance;
			EsGeneric selected;

			if (EsListViewFixedItemGetSelected(((EsListView *) element), &selected)) {
				instance->fontVariant = selected.i;
				EsListViewInvalidateAll(element->instance->fontList);
			}
		}

		return 0;
	};

	EsMenuShow(menu);
}

void LoadFontsFromDatabase(Instance *instance) {
	EsFontDatabaseEnumerate([] (const EsFontInformation *information, EsGeneric context) { 
		((Instance *) context.p)->fonts.AddPointer(information); 
	}, instance);

	EsSort(instance->fonts.array, instance->fonts.Length(), sizeof(EsFontInformation), [] (const void *left, const void *right, EsGeneric) {
		EsFontInformation *fontLeft = (EsFontInformation *) left;
		EsFontInformation *fontRight = (EsFontInformation *) right;
		int x = EsStringCompare(fontLeft->name, fontLeft->nameBytes, fontRight->name, fontRight->nameBytes);
		if (x) return x;
		return EsStringCompareRaw(fontLeft->name, fontLeft->nameBytes, fontRight->name, fontRight->nameBytes);
	}, 0); 

	EsListViewInsert(instance->fontList, 0, 0, instance->fonts.Length());
}

void BackCommand(Instance *instance, EsElement *, EsCommand *) {
	EsPanelSwitchTo(instance->switcher, instance->fontList, ES_TRANSITION_NONE);
	EsWindowSwitchToolbar(instance->window, instance->fontListToolbar, ES_TRANSITION_SLIDE_DOWN);
}

int InstanceCallback(Instance *instance, EsMessage *message) {
	if (message->type == ES_MSG_INSTANCE_CLOSE) {
		instance->switcher = nullptr;
		instance->fontList = nullptr;
		instance->fontSizeTextbox = nullptr;
		instance->previewTextTextbox = nullptr;
		instance->fontPreview = nullptr;
		instance->fontListToolbar = nullptr;
		instance->fontPreviewToolbar = nullptr;
	} else if (message->type == ES_MSG_INSTANCE_DESTROY) {
		SaveSettings(instance);
		EsHeapFree(instance->previewText);
		instance->fonts.Free();
		// TODO Remove the font added to the font database.
	} else if (message->type == ES_MSG_INSTANCE_OPEN) {
		if (!message->instanceOpen.update) {
			EsFontInformation information = {};
			information.availableWeightsNormal = 1 << 4 /* regular */;
			instance->fontPreviewID = EsFontDatabaseInsertFile(&information, message->instanceOpen.file);
			// TODO Check that the font is valid.

			EsElementDestroyContents(instance->fontPreview);

			EsPanel *titleRow = EsPanelCreate(instance->fontPreview, ES_CELL_H_CENTER | ES_PANEL_HORIZONTAL, &styleFontInformationRow);
			EsIconDisplayCreate(titleRow, ES_FLAGS_DEFAULT, ES_STYLE_ICON_DISPLAY, ES_ICON_FONT_X_GENERIC);
			EsTextDisplayCreate(titleRow, ES_FLAGS_DEFAULT, ES_STYLE_TEXT_HEADING0, message->instanceOpen.name, message->instanceOpen.nameBytes);
			EsSpacerCreate(instance->fontPreview, ES_FLAGS_DEFAULT, 0, 0, 20);

			int sizes[] = { 12, 18, 24, 36, 48, 60, 72, 0 };

			for (uintptr_t i = 0; sizes[i]; i++) {
				EsPanel *row = EsPanelCreate(instance->fontPreview, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL, &styleFontInformationRow);
				char buffer[64];
				EsTextDisplayCreate(row, ES_FLAGS_DEFAULT, 0, buffer, EsStringFormat(buffer, sizeof(buffer), "%d", sizes[i]));
				EsTextDisplay *display = EsTextDisplayCreate(row, ES_TEXT_DISPLAY_NO_FONT_SUBSTITUTION);
				const char *string = interfaceString_FontBookPreviewTextLongDefault;
				EsTextRun runs[2] = {};
				EsElementGetTextStyle(display, &runs[0].style);
				runs[0].style.size = sizes[i];
				runs[0].style.font.family = instance->fontPreviewID;
				runs[1].offset = EsCStringLength(string);
				EsTextDisplaySetStyledContents(display, string, runs, 1);
			}

			EsPanelSwitchTo(instance->switcher, instance->fontPreview, ES_TRANSITION_NONE);
			EsWindowSwitchToolbar(instance->window, instance->fontPreviewToolbar, ES_TRANSITION_SLIDE_UP);
		}

		EsInstanceOpenComplete(instance, message->instanceOpen.file, true);
	} else {
		return 0;
	}

	return ES_HANDLED;
}

void _start() {
	_init();

	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_INSTANCE_CREATE) {
			Instance *instance = EsInstanceCreate(message, INTERFACE_STRING(FontBookTitle)); 
			instance->callback = InstanceCallback;
			EsWindowSetIcon(instance->window, ES_ICON_APPLICATIONS_FONTS);
			EsPanel *rootPanel = EsPanelCreate(instance->window, ES_CELL_FILL, ES_STYLE_PANEL_WINDOW_DIVIDER);

			instance->switcher = EsPanelCreate(rootPanel, ES_CELL_FILL | ES_PANEL_SWITCHER);

			// Settings:

			if (!LoadSettings(instance)) {
				instance->fontSize = 24;
				instance->fontVariant = 4 /* regular */;
				instance->previewTextBytes = EsCStringLength(interfaceString_FontBookPreviewTextDefault);
				instance->previewText = (char *) EsHeapAllocate(instance->previewTextBytes, false);
				EsMemoryCopy(instance->previewText, interfaceString_FontBookPreviewTextDefault, instance->previewTextBytes);
			}

			// Font list page:

			uint64_t flags = ES_CELL_FILL | ES_LIST_VIEW_TILED | ES_LIST_VIEW_CENTER_TILES;
			EsListView *fontList = EsListViewCreate(instance->switcher, flags, &styleFontList, &styleFontItem);
			EsListViewSetMaximumItemsPerBand(fontList, 4);
			EsListViewInsertGroup(fontList, 0);
			instance->fontList = fontList;
			fontList->messageUser = FontListMessage;
			fontList->accessKey = 'F';
			LoadFontsFromDatabase(instance);

			EsPanelSwitchTo(instance->switcher, instance->fontList, ES_TRANSITION_NONE);

			// Font preview page:

			instance->fontPreview = EsPanelCreate(instance->switcher, ES_CELL_FILL | ES_PANEL_V_SCROLL_AUTO, &styleFontPreviewPage);

			// Font list toolbar:

			EsElement *toolbar = EsWindowGetToolbar(instance->window, true /* create new */);
			instance->fontListToolbar = toolbar;

			EsPanel *section = EsPanelCreate(toolbar, ES_PANEL_HORIZONTAL);
			EsTextDisplayCreate(section, ES_FLAGS_DEFAULT, 0, INTERFACE_STRING(FontBookTextSize));
			instance->fontSizeTextbox = EsTextboxCreate(section, ES_TEXTBOX_EDIT_BASED, ES_STYLE_TEXTBOX_BORDERED_SINGLE_COMPACT);
			instance->fontSizeTextbox->messageUser = FontSizeTextboxMessage;
			instance->fontSizeTextbox->accessKey = 'S';
			EsTextboxUseNumberOverlay(instance->fontSizeTextbox, false);
			FontSizeTextboxUpdate(instance);

			EsSpacerCreate(toolbar, ES_CELL_H_FILL);

			section = EsPanelCreate(toolbar, ES_PANEL_HORIZONTAL);
			EsTextDisplayCreate(section, ES_FLAGS_DEFAULT, 0, INTERFACE_STRING(FontBookPreviewText));
			instance->previewTextTextbox = EsTextboxCreate(section, ES_FLAGS_DEFAULT, ES_STYLE_TEXTBOX_BORDERED_SINGLE);
			instance->previewTextTextbox->messageUser = PreviewTextTextboxMessage;
			instance->previewTextTextbox->accessKey = 'P';
			EsTextboxInsert(instance->previewTextTextbox, instance->previewText, instance->previewTextBytes, false);

			EsSpacerCreate(toolbar, ES_CELL_H_FILL);

			EsButton *button = EsButtonCreate(toolbar, ES_BUTTON_DROPDOWN, {}, INTERFACE_STRING(FontBookVariants));
			button->accessKey = 'V';
			EsButtonOnCommand(button, VariantsPopupCreate);

			// Font preview toolbar:

			toolbar = EsWindowGetToolbar(instance->window, true /* create new */);
			instance->fontPreviewToolbar = toolbar;
			button = EsButtonCreate(toolbar, ES_FLAGS_DEFAULT, 0, INTERFACE_STRING(FontBookNavigationBack));
			button->accessKey = 'B';
			EsButtonSetIcon(button, ES_ICON_GO_PREVIOUS_SYMBOLIC);
			EsButtonOnCommand(button, BackCommand);
		}
	}
}
