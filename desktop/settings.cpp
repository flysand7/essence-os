struct SettingsInstance : EsInstance {
	EsPanel *switcher;
	EsPanel *mainPage;
};

const EsStyle styleSettingsGroupContainer = {
	.inherit = ES_STYLE_BUTTON_GROUP_CONTAINER,

	.metrics = {
		.mask = ES_THEME_METRICS_PREFERRED_WIDTH | ES_THEME_METRICS_INSETS,
		.insets = ES_RECT_1(5),
		.preferredWidth = 400,
	},
};

const EsStyle styleSettingsGroupContainer2 = {
	.inherit = ES_STYLE_BUTTON_GROUP_CONTAINER,

	.metrics = {
		.mask = ES_THEME_METRICS_PREFERRED_WIDTH | ES_THEME_METRICS_INSETS | ES_THEME_METRICS_GAP_MAJOR,
		.insets = ES_RECT_1(15),
		.preferredWidth = 400,
		.gapMajor = 15,
	},
};

const EsStyle styleSettingsNumberTextbox = {
	.inherit = ES_STYLE_TEXTBOX_BORDERED_SINGLE,

	.metrics = {
		.mask = ES_THEME_METRICS_PREFERRED_WIDTH,
		.preferredWidth = 80,
	},
};

const EsStyle styleSettingsTable = {
	.metrics = {
		.mask = ES_THEME_METRICS_GAP_MAJOR | ES_THEME_METRICS_GAP_MINOR,
		.gapMajor = 5,
		.gapMinor = 5,
	},
};

const EsStyle styleSettingsCheckboxGroup = {
	.metrics = {
		.mask = ES_THEME_METRICS_GAP_MAJOR | ES_THEME_METRICS_GAP_MINOR,
		.gapMajor = 0,
		.gapMinor = 0,
	},
};

const EsStyle styleSettingsOverlayPanel = {
	.metrics = {
		.mask = ES_THEME_METRICS_INSETS,
		.insets = ES_RECT_1(10),
	},
};

const EsStyle styleSettingsButton = {
	.inherit = ES_STYLE_PUSH_BUTTON_TOOLBAR,

	.metrics = {
		.mask = ES_THEME_METRICS_PREFERRED_WIDTH | ES_THEME_METRICS_PREFERRED_HEIGHT | ES_THEME_METRICS_LAYOUT_VERTICAL
			| ES_THEME_METRICS_GAP_ALL | ES_THEME_METRICS_TEXT_ALIGN | ES_THEME_METRICS_INSETS | ES_THEME_METRICS_ICON_SIZE,
		.insets = ES_RECT_2(0, 10),
		.preferredWidth = 0,
		.preferredHeight = 75,
		.gapMajor = 3,
		.gapMinor = 3,
		.gapWrap = 3,
		.textAlign = ES_TEXT_H_CENTER,
		.iconSize = 32,
		.layoutVertical = true,
	},
};

struct SettingsPage {
	const char *string;
	ptrdiff_t stringBytes;
	uint32_t iconID;
	void (*create)(EsElement *parent, SettingsPage *page);
	char accessKey;
};

void SettingsBackButton(EsInstance *_instance, EsElement *, EsCommand *) {
	SettingsInstance *instance = (SettingsInstance *) _instance;
	EsPanelSwitchTo(instance->switcher, instance->mainPage, ES_TRANSITION_ZOOM_OUT, ES_PANEL_SWITCHER_DESTROY_PREVIOUS_AFTER_TRANSITION, 1.0f);
}

void SettingsPageAddTitle(EsElement *container, SettingsPage *page) {
	EsPanel *row = EsPanelCreate(container, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL);
	EsIconDisplayCreate(row, ES_FLAGS_DEFAULT, ES_STYLE_ICON_DISPLAY, page->iconID);
	EsSpacerCreate(row, ES_FLAGS_DEFAULT, 0, 10, 0);
	EsTextDisplayCreate(row, ES_CELL_H_FILL, ES_STYLE_TEXT_HEADING2, page->string, page->stringBytes);
}

void SettingsPageAddUndoButton(EsElement *stack) {
	EsPanel *overlay = EsPanelCreate(stack, ES_CELL_H_RIGHT | ES_CELL_V_TOP, &styleSettingsOverlayPanel);
	EsButton *undoButton = EsButtonCreate(overlay, ES_BUTTON_TOOLBAR, 0, INTERFACE_STRING(DesktopSettingsUndoButton));
	undoButton->accessKey = 'U';
	EsButtonSetIcon(undoButton, ES_ICON_EDIT_UNDO_SYMBOLIC);
	EsElementSetDisabled(undoButton, true);
}

void SettingsPageUnimplemented(EsElement *element, SettingsPage *page) {
	EsPanel *content = EsPanelCreate(element, ES_CELL_FILL | ES_PANEL_V_SCROLL_AUTO, &styleNewTabContent);
	EsPanel *container = EsPanelCreate(content, ES_PANEL_VERTICAL | ES_CELL_H_SHRINK, &styleSettingsGroupContainer2);
	SettingsPageAddTitle(container, page);
	EsTextDisplayCreate(container, ES_CELL_H_CENTER, 0, "Work in progress" ELLIPSIS);
}

void SettingsPageMouse(EsElement *element, SettingsPage *page) {
	// TODO Make this interactive.

	SettingsPageAddUndoButton(element);

	EsPanel *content = EsPanelCreate(element, ES_CELL_FILL | ES_PANEL_V_SCROLL_AUTO, &styleNewTabContent);
	EsPanel *container = EsPanelCreate(content, ES_PANEL_VERTICAL | ES_CELL_H_SHRINK, &styleSettingsGroupContainer2);
	SettingsPageAddTitle(container, page);

	EsPanel *table = EsPanelCreate(container, ES_CELL_H_FILL | ES_PANEL_TABLE | ES_PANEL_HORIZONTAL, &styleSettingsTable);
	EsPanelSetBands(table, 2);

	EsTextbox *textbox;

	EsTextDisplayCreate(table, ES_CELL_H_RIGHT | ES_CELL_H_PUSH, 0, INTERFACE_STRING(DesktopSettingsMouseDoubleClickSpeed));
	textbox = EsTextboxCreate(table, ES_CELL_H_LEFT | ES_CELL_H_PUSH | ES_TEXTBOX_EDIT_BASED, &styleSettingsNumberTextbox);
	textbox->accessKey = 'D';
	EsTextboxUseNumberOverlay(textbox, false);
	EsTextboxInsert(textbox, "500 ms");

	EsTextDisplayCreate(table, ES_CELL_H_RIGHT | ES_CELL_H_PUSH, 0, INTERFACE_STRING(DesktopSettingsMouseSpeed));
	textbox = EsTextboxCreate(table, ES_CELL_H_LEFT | ES_CELL_H_PUSH | ES_TEXTBOX_EDIT_BASED, &styleSettingsNumberTextbox);
	textbox->accessKey = 'M';
	EsTextboxUseNumberOverlay(textbox, false);
	EsTextboxInsert(textbox, "50%");

	EsTextDisplayCreate(table, ES_CELL_H_RIGHT | ES_CELL_H_PUSH, 0, INTERFACE_STRING(DesktopSettingsMouseCursorTrails));
	textbox = EsTextboxCreate(table, ES_CELL_H_LEFT | ES_CELL_H_PUSH | ES_TEXTBOX_EDIT_BASED, &styleSettingsNumberTextbox);
	textbox->accessKey = 'T';
	EsTextboxUseNumberOverlay(textbox, false);
	EsTextboxInsert(textbox, "0");

	EsTextDisplayCreate(table, ES_CELL_H_RIGHT | ES_CELL_H_PUSH, 0, INTERFACE_STRING(DesktopSettingsMouseLinesPerScrollNotch));
	textbox = EsTextboxCreate(table, ES_CELL_H_LEFT | ES_CELL_H_PUSH | ES_TEXTBOX_EDIT_BASED, &styleSettingsNumberTextbox);
	textbox->accessKey = 'S';
	EsTextboxUseNumberOverlay(textbox, false);
	EsTextboxInsert(textbox, "3");

	table = EsPanelCreate(container, ES_CELL_H_FILL | ES_PANEL_TABLE | ES_PANEL_HORIZONTAL, &styleSettingsCheckboxGroup);
	EsPanelSetBands(table, 1);
	EsButtonCreate(table, ES_CELL_H_FILL | ES_BUTTON_CHECKBOX, 0, INTERFACE_STRING(DesktopSettingsMouseSwapLeftAndRightButtons))->accessKey = 'B';
	EsButtonCreate(table, ES_CELL_H_FILL | ES_BUTTON_CHECKBOX, 0, INTERFACE_STRING(DesktopSettingsMouseShowShadow))->accessKey = 'W';
	EsButtonCreate(table, ES_CELL_H_FILL | ES_BUTTON_CHECKBOX, 0, INTERFACE_STRING(DesktopSettingsMouseLocateCursorOnCtrl))->accessKey = 'L';
}

void SettingsPageKeyboard(EsElement *element, SettingsPage *page) {
	// TODO Make this interactive.

	SettingsPageAddUndoButton(element);

	EsPanel *content = EsPanelCreate(element, ES_CELL_FILL | ES_PANEL_V_SCROLL_AUTO, &styleNewTabContent);
	EsPanel *container = EsPanelCreate(content, ES_PANEL_VERTICAL | ES_CELL_H_SHRINK, &styleSettingsGroupContainer2);
	SettingsPageAddTitle(container, page);

	EsPanel *table = EsPanelCreate(container, ES_CELL_H_FILL | ES_PANEL_TABLE | ES_PANEL_HORIZONTAL, &styleSettingsTable);
	EsPanelSetBands(table, 2);

	EsTextbox *textbox;

	EsTextDisplayCreate(table, ES_CELL_H_RIGHT | ES_CELL_H_PUSH, 0, INTERFACE_STRING(DesktopSettingsKeyboardKeyRepeatDelay));
	textbox = EsTextboxCreate(table, ES_CELL_H_LEFT | ES_CELL_H_PUSH | ES_TEXTBOX_EDIT_BASED, &styleSettingsNumberTextbox);
	textbox->accessKey = 'D';
	EsTextboxUseNumberOverlay(textbox, false);
	EsTextboxInsert(textbox, "400 ms");

	EsTextDisplayCreate(table, ES_CELL_H_RIGHT | ES_CELL_H_PUSH, 0, INTERFACE_STRING(DesktopSettingsKeyboardKeyRepeatRate));
	textbox = EsTextboxCreate(table, ES_CELL_H_LEFT | ES_CELL_H_PUSH | ES_TEXTBOX_EDIT_BASED, &styleSettingsNumberTextbox);
	textbox->accessKey = 'R';
	EsTextboxUseNumberOverlay(textbox, false);
	EsTextboxInsert(textbox, "40 ms");

	EsTextDisplayCreate(table, ES_CELL_H_RIGHT | ES_CELL_H_PUSH, 0, INTERFACE_STRING(DesktopSettingsKeyboardCaretBlinkRate));
	textbox = EsTextboxCreate(table, ES_CELL_H_LEFT | ES_CELL_H_PUSH | ES_TEXTBOX_EDIT_BASED, &styleSettingsNumberTextbox);
	textbox->accessKey = 'B';
	EsTextboxUseNumberOverlay(textbox, false);
	EsTextboxInsert(textbox, "500 ms");

	EsPanel *testBox = EsPanelCreate(container, ES_CELL_H_FILL);
	EsTextDisplayCreate(testBox, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(DesktopSettingsKeyboardTestTextboxIntroduction));
	EsSpacerCreate(testBox, ES_FLAGS_DEFAULT, 0, 0, 5);
	EsTextboxCreate(testBox, ES_CELL_H_LEFT)->accessKey = 'T';
}

SettingsPage settingsPages[] = {
	{ INTERFACE_STRING(DesktopSettingsAccessibility), ES_ICON_PREFERENCES_DESKTOP_ACCESSIBILITY, SettingsPageUnimplemented, 'A' },
	{ INTERFACE_STRING(DesktopSettingsApplications), ES_ICON_APPLICATIONS_OTHER, SettingsPageUnimplemented, 'A' },
	{ INTERFACE_STRING(DesktopSettingsDateAndTime), ES_ICON_PREFERENCES_SYSTEM_TIME, SettingsPageUnimplemented, 'D' },
	{ INTERFACE_STRING(DesktopSettingsDevices), ES_ICON_COMPUTER_LAPTOP, SettingsPageUnimplemented, 'D' },
	{ INTERFACE_STRING(DesktopSettingsDisplay), ES_ICON_PREFERENCES_DESKTOP_DISPLAY, SettingsPageUnimplemented, 'D' },
	{ INTERFACE_STRING(DesktopSettingsKeyboard), ES_ICON_INPUT_KEYBOARD, SettingsPageKeyboard, 'K' },
	{ INTERFACE_STRING(DesktopSettingsLocalisation), ES_ICON_PREFERENCES_DESKTOP_LOCALE, SettingsPageUnimplemented, 'L' },
	{ INTERFACE_STRING(DesktopSettingsMouse), ES_ICON_INPUT_MOUSE, SettingsPageMouse, 'M' },
	{ INTERFACE_STRING(DesktopSettingsNetwork), ES_ICON_PREFERENCES_SYSTEM_NETWORK, SettingsPageUnimplemented, 'N' },
	{ INTERFACE_STRING(DesktopSettingsPower), ES_ICON_PREFERENCES_SYSTEM_POWER, SettingsPageUnimplemented, 'P' },
	{ INTERFACE_STRING(DesktopSettingsSound), ES_ICON_PREFERENCES_DESKTOP_SOUND, SettingsPageUnimplemented, 'S' },
	{ INTERFACE_STRING(DesktopSettingsTheme), ES_ICON_APPLICATIONS_INTERFACEDESIGN, SettingsPageUnimplemented, 'T' },
};

void SettingsButtonPressed(EsInstance *_instance, EsElement *element, EsCommand *) {
	SettingsInstance *instance = (SettingsInstance *) _instance;
	SettingsPage *page = (SettingsPage *) element->userData.p;

	EsPanel *stack = EsPanelCreate(instance->switcher, ES_CELL_FILL | ES_PANEL_Z_STACK);
	page->create(stack, page);

	{
		EsPanel *overlay = EsPanelCreate(stack, ES_CELL_H_LEFT | ES_CELL_V_TOP, &styleSettingsOverlayPanel);
		EsButton *backButton = EsButtonCreate(overlay, ES_CELL_H_LEFT | ES_BUTTON_TOOLBAR, 0, INTERFACE_STRING(DesktopSettingsBackButton));
		backButton->accessKey = 'A';
		EsButtonSetIcon(backButton, ES_ICON_GO_HOME_SYMBOLIC);
		EsButtonOnCommand(backButton, SettingsBackButton);
	}

	EsPanelSwitchTo(instance->switcher, stack, ES_TRANSITION_ZOOM_IN, ES_FLAGS_DEFAULT, 1.0f);
}

void InstanceSettingsCreate(EsMessage *message) {
	SettingsInstance *instance = (SettingsInstance *) _EsInstanceCreate(sizeof(SettingsInstance), message, nullptr);
	EsWindowSetTitle(instance->window, INTERFACE_STRING(DesktopSettingsTitle));
	EsWindowSetIcon(instance->window, ES_ICON_PREFERENCES_DESKTOP);
	EsPanel *windowBackground = EsPanelCreate(instance->window, ES_CELL_FILL, ES_STYLE_PANEL_WINDOW_BACKGROUND);
	instance->switcher = EsPanelCreate(windowBackground, ES_CELL_FILL | ES_PANEL_SWITCHER);
	EsPanel *content = EsPanelCreate(instance->switcher, ES_CELL_FILL | ES_PANEL_V_SCROLL_AUTO, &styleNewTabContent);
	instance->mainPage = content;
	EsPanelSwitchTo(instance->switcher, content, ES_TRANSITION_NONE);

	{
		EsPanel *row = EsPanelCreate(content, ES_CELL_H_CENTER | ES_PANEL_HORIZONTAL);
		EsIconDisplayCreate(row, ES_FLAGS_DEFAULT, ES_STYLE_ICON_DISPLAY, ES_ICON_PREFERENCES_DESKTOP);
		EsSpacerCreate(row, ES_FLAGS_DEFAULT, 0, 10, 0);
		EsTextDisplayCreate(row, ES_FLAGS_DEFAULT, ES_STYLE_TEXT_HEADING1, INTERFACE_STRING(DesktopSettingsTitle));
	}

	{
		EsPanel *container = EsPanelCreate(content, ES_CELL_H_SHRINK | ES_PANEL_TABLE | ES_PANEL_HORIZONTAL, &styleSettingsGroupContainer);
		EsPanelSetBands(container, 4);

		EsSort(settingsPages, sizeof(settingsPages) / sizeof(settingsPages[0]), sizeof(settingsPages[0]), [] (const void *_a, const void *_b, EsGeneric) {
			SettingsPage *a = (SettingsPage *) _a, *b = (SettingsPage *) _b;
			return EsStringCompare(a->string, a->stringBytes, b->string, b->stringBytes);
		}, nullptr);

		for (uintptr_t i = 0; i < sizeof(settingsPages) / sizeof(settingsPages[0]); i++) {
			EsButton *button = EsButtonCreate(container, ES_ELEMENT_NO_FOCUS_ON_CLICK | ES_CELL_H_FILL, 
					&styleSettingsButton, settingsPages[i].string, settingsPages[i].stringBytes);
			button->userData = &settingsPages[i];
			button->accessKey = settingsPages[i].accessKey;
			EsButtonSetIcon(button, settingsPages[i].iconID);
			EsButtonOnCommand(button, SettingsButtonPressed);
		}
	}
}
