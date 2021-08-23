// TODO Save system configuration file on closing the instance or going back to all settings.

struct SettingsInstance : EsInstance {
	EsPanel *switcher;
	EsPanel *mainPage;
	EsButton *undoButton;
	Array<struct SettingsControl *> controls;
};

struct SettingsPage {
	const char *string;
	ptrdiff_t stringBytes;
	uint32_t iconID;
	void (*create)(EsElement *parent, SettingsPage *page);
	char accessKey;
};

struct SettingsControl {
#define SETTINGS_CONTROL_CHECKBOX (1)
#define SETTINGS_CONTROL_NUMBER (2)
	uint8_t type;
	bool originalValueBool;
	bool *globalPointerBool;
	int32_t originalValueInt;
	int32_t *globalPointerInt;
	int32_t minimumValue, maximumValue;
	const char *suffix;
	size_t suffixBytes;
	const char *cConfigurationSection; 
	const char *cConfigurationKey; 
	EsElement *element;
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
		.gapMajor = 3,
		.gapMinor = 3,
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

void SettingsBackButton(EsInstance *_instance, EsElement *, EsCommand *) {
	SettingsInstance *instance = (SettingsInstance *) _instance;
	instance->undoButton = nullptr;
	instance->controls.Free();
	EsPanelSwitchTo(instance->switcher, instance->mainPage, ES_TRANSITION_ZOOM_OUT, ES_PANEL_SWITCHER_DESTROY_PREVIOUS_AFTER_TRANSITION, 1.0f);
}

void SettingsNumberBoxSetValue(EsElement *element, int32_t newValue) {
	EsTextbox *textbox = (EsTextbox *) element;
	SettingsInstance *instance = (SettingsInstance *) textbox->instance;
	SettingsControl *control = (SettingsControl *) textbox->userData.p;

	newValue = ClampInteger(control->minimumValue, control->maximumValue, newValue);
	char buffer[64];
	size_t bytes = EsStringFormat(buffer, sizeof(buffer), "%d%s", newValue, control->suffixBytes, control->suffix);
	EsTextboxSelectAll(textbox);
	EsTextboxInsert(textbox, buffer, bytes);

	EsMutexAcquire(&api.systemConfigurationMutex);
	EsSystemConfigurationGroup *group = SystemConfigurationGetGroup(control->cConfigurationSection, -1, true);
	EsSystemConfigurationItem *item = SystemConfigurationGetItem(group, control->cConfigurationKey, -1, true);
	int32_t oldValue = EsIntegerParse(item->value, item->valueBytes);
	EsHeapFree(item->value);
	item->value = (char *) EsHeapAllocate(65, true);
	item->valueBytes = EsStringFormat(item->value, 64, "%fd", ES_STRING_FORMAT_SIMPLE, newValue);
	EsMutexRelease(&api.systemConfigurationMutex);

	if (oldValue != newValue) {
		if (control->globalPointerInt) *control->globalPointerInt = newValue;
		EsElementSetDisabled(instance->undoButton, false);
	}
}

void SettingsUndoButton(EsInstance *_instance, EsElement *, EsCommand *) {
	SettingsInstance *instance = (SettingsInstance *) _instance;

	for (uintptr_t i = 0; i < instance->controls.Length(); i++) {
		SettingsControl *control = instance->controls[i];

		if (control->type == SETTINGS_CONTROL_CHECKBOX) {
			EsButton *button = (EsButton *) control->element;
			EsButtonSetCheck(button, control->originalValueBool ? ES_CHECK_CHECKED : ES_CHECK_UNCHECKED, true);
		} else if (control->type == SETTINGS_CONTROL_NUMBER) {
			SettingsNumberBoxSetValue(control->element, control->originalValueInt);
		}
	}

	EsElementSetDisabled(instance->undoButton, true);
}

void SettingsAddTitle(EsElement *container, SettingsPage *page) {
	EsPanel *row = EsPanelCreate(container, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL);
	EsIconDisplayCreate(row, ES_FLAGS_DEFAULT, ES_STYLE_ICON_DISPLAY, page->iconID);
	EsSpacerCreate(row, ES_FLAGS_DEFAULT, 0, 10, 0);
	EsTextDisplayCreate(row, ES_CELL_H_FILL, ES_STYLE_TEXT_HEADING2, page->string, page->stringBytes);
	EsSpacerCreate(container, ES_CELL_H_FILL, ES_STYLE_BUTTON_GROUP_SEPARATOR);
}

void SettingsAddUndoButton(EsElement *stack) {
	EsPanel *overlay = EsPanelCreate(stack, ES_CELL_H_RIGHT | ES_CELL_V_TOP, &styleSettingsOverlayPanel);
	EsButton *undoButton = EsButtonCreate(overlay, ES_BUTTON_TOOLBAR | ES_ELEMENT_STICKY_ACCESS_KEY, 0, INTERFACE_STRING(DesktopSettingsUndoButton));
	undoButton->accessKey = 'U';
	((SettingsInstance *) stack->instance)->undoButton = undoButton;
	EsButtonSetIcon(undoButton, ES_ICON_EDIT_UNDO_SYMBOLIC);
	EsButtonOnCommand(undoButton, SettingsUndoButton);
	EsElementSetDisabled(undoButton, true);
}

void SettingsPageUnimplemented(EsElement *element, SettingsPage *page) {
	EsPanel *content = EsPanelCreate(element, ES_CELL_FILL | ES_PANEL_V_SCROLL_AUTO, &styleNewTabContent);
	EsPanel *container = EsPanelCreate(content, ES_PANEL_VERTICAL | ES_CELL_H_SHRINK, &styleSettingsGroupContainer2);
	SettingsAddTitle(container, page);
	EsTextDisplayCreate(container, ES_CELL_H_CENTER, 0, "Work in progress" ELLIPSIS);
}

void SettingsCheckboxCommand(EsInstance *_instance, EsElement *element, EsCommand *) {
	EsButton *button = (EsButton *) element;
	SettingsInstance *instance = (SettingsInstance *) _instance;
	SettingsControl *control = (SettingsControl *) button->userData.p;
	bool newValue = EsButtonGetCheck(button) == ES_CHECK_CHECKED;

	EsMutexAcquire(&api.systemConfigurationMutex);
	EsSystemConfigurationGroup *group = SystemConfigurationGetGroup(control->cConfigurationSection, -1, true);
	EsSystemConfigurationItem *item = SystemConfigurationGetItem(group, control->cConfigurationKey, -1, true);
	bool oldValue = EsIntegerParse(item->value, item->valueBytes);
	EsHeapFree(item->value);
	item->value = (char *) EsHeapAllocate(2, true);
	*item->value = newValue ? '1' : '0';
	item->valueBytes = 1;
	EsMutexRelease(&api.systemConfigurationMutex);

	if (oldValue == newValue) return;
	if (control->globalPointerBool) *control->globalPointerBool = newValue;
	EsElementSetDisabled(instance->undoButton, false);
}

void SettingsAddCheckbox(EsElement *table, const char *string, ptrdiff_t stringBytes, char accessKey,
		const char *cConfigurationSection, const char *cConfigurationKey, bool *globalPointerBool) {
	SettingsInstance *instance = (SettingsInstance *) table->instance;

	SettingsControl *control = (SettingsControl *) EsHeapAllocate(sizeof(SettingsControl), true);
	control->type = SETTINGS_CONTROL_CHECKBOX;
	control->cConfigurationSection = cConfigurationSection;
	control->cConfigurationKey = cConfigurationKey;
	control->globalPointerBool = globalPointerBool;
	control->originalValueBool = EsSystemConfigurationReadInteger(control->cConfigurationSection, -1, control->cConfigurationKey, -1);

	EsButton *button = EsButtonCreate(table, ES_CELL_H_FILL | ES_BUTTON_CHECKBOX | ES_ELEMENT_FREE_USER_DATA | ES_ELEMENT_STICKY_ACCESS_KEY, 0, string, stringBytes);
	button->userData = control;
	button->accessKey = accessKey;
	if (control->originalValueBool) EsButtonSetCheck(button, ES_CHECK_CHECKED, false);
	EsButtonOnCommand(button, SettingsCheckboxCommand);

	control->element = button;
	instance->controls.Add(control);
}

int SettingsNumberBoxMessage(EsElement *element, EsMessage *message) {
	EsTextbox *textbox = (EsTextbox *) element;
	SettingsControl *control = (SettingsControl *) textbox->userData.p;

	if (message->type == ES_MSG_TEXTBOX_EDIT_END || message->type == ES_MSG_TEXTBOX_NUMBER_DRAG_END) {
		char *expression = EsTextboxGetContents(textbox);
		EsCalculationValue value = EsCalculateFromUserExpression(expression); 
		EsHeapFree(expression);

		if (!value.error) {
			SettingsNumberBoxSetValue(element, value.number + 0.5);
			return ES_HANDLED;
		} else {
			return ES_REJECTED;
		}
	} else if (message->type == ES_MSG_TEXTBOX_NUMBER_DRAG_DELTA) {
		int oldValue = EsTextboxGetContentsAsDouble(textbox); 
		int newValue = ClampInteger(control->minimumValue, control->maximumValue, oldValue + message->numberDragDelta.delta * (message->numberDragDelta.fast ? 10 : 1));
		char buffer[64];
		size_t bytes = EsStringFormat(buffer, sizeof(buffer), "%d%s", newValue, control->suffixBytes, control->suffix);
		EsTextboxSelectAll(textbox);
		EsTextboxInsert(textbox, buffer, bytes);
		return ES_HANDLED;
	}

	return 0;
}

void SettingsAddNumberBox(EsElement *table, const char *string, ptrdiff_t stringBytes, char accessKey, 
		const char *cConfigurationSection, const char *cConfigurationKey, int32_t *globalPointer,
		int32_t minimumValue, int32_t maximumValue, const char *suffix, ptrdiff_t suffixBytes) {
	if (suffixBytes == -1) {
		suffixBytes = EsCStringLength(suffix);
	}

	SettingsInstance *instance = (SettingsInstance *) table->instance;

	SettingsControl *control = (SettingsControl *) EsHeapAllocate(sizeof(SettingsControl), true);
	control->type = SETTINGS_CONTROL_NUMBER;
	control->cConfigurationSection = cConfigurationSection;
	control->cConfigurationKey = cConfigurationKey;
	control->globalPointerInt = globalPointer;
	control->originalValueInt = EsSystemConfigurationReadInteger(control->cConfigurationSection, -1, control->cConfigurationKey, -1);
	control->suffix = suffix;
	control->suffixBytes = suffixBytes;
	control->minimumValue = minimumValue;
	control->maximumValue = maximumValue;

	EsTextDisplayCreate(table, ES_CELL_H_RIGHT | ES_CELL_H_PUSH, 0, string, stringBytes); 
	EsTextbox *textbox = EsTextboxCreate(table, ES_CELL_H_LEFT | ES_CELL_H_PUSH | ES_TEXTBOX_EDIT_BASED | ES_ELEMENT_FREE_USER_DATA, &styleSettingsNumberTextbox);
	EsTextboxUseNumberOverlay(textbox, false);
	textbox->userData = control;
	textbox->accessKey = accessKey;
	textbox->messageUser = SettingsNumberBoxMessage;

	char buffer[64];
	size_t bytes = EsStringFormat(buffer, sizeof(buffer), "%d%s", control->originalValueInt, suffixBytes, suffix);
	EsTextboxInsert(textbox, buffer, bytes);

	control->element = textbox;
	instance->controls.Add(control);
}

int SettingsDoubleClickTestMessage(EsElement *element, EsMessage *message) {
	if (message->type == ES_MSG_MOUSE_LEFT_DOWN) {
		if (message->mouseDown.clickChainCount >= 2) {
			element->customStyleState ^= THEME_STATE_CHECKED;
			EsElementRepaint(element);
		}

		return ES_HANDLED;
	}

	return 0;
}

void SettingsPageMouse(EsElement *element, SettingsPage *page) {
	EsPanel *content = EsPanelCreate(element, ES_CELL_FILL | ES_PANEL_V_SCROLL_AUTO, &styleNewTabContent);
	EsPanel *container = EsPanelCreate(content, ES_PANEL_VERTICAL | ES_CELL_H_SHRINK, &styleSettingsGroupContainer2);
	SettingsAddTitle(container, page);

	EsPanel *table;
	EsTextbox *textbox;

	table = EsPanelCreate(container, ES_CELL_H_FILL | ES_PANEL_TABLE | ES_PANEL_HORIZONTAL, &styleSettingsTable);
	EsPanelSetBands(table, 2);

	EsTextDisplayCreate(table, ES_CELL_H_RIGHT | ES_CELL_H_PUSH, 0, INTERFACE_STRING(DesktopSettingsMouseSpeed)); // TODO.
	textbox = EsTextboxCreate(table, ES_CELL_H_LEFT | ES_CELL_H_PUSH | ES_TEXTBOX_EDIT_BASED, &styleSettingsNumberTextbox);
	textbox->accessKey = 'M';
	EsTextboxUseNumberOverlay(textbox, false);
	EsTextboxInsert(textbox, "50%");

	EsTextDisplayCreate(table, ES_CELL_H_RIGHT | ES_CELL_H_PUSH, 0, INTERFACE_STRING(DesktopSettingsMouseCursorTrails)); // TODO.
	textbox = EsTextboxCreate(table, ES_CELL_H_LEFT | ES_CELL_H_PUSH | ES_TEXTBOX_EDIT_BASED, &styleSettingsNumberTextbox);
	textbox->accessKey = 'T';
	EsTextboxUseNumberOverlay(textbox, false);
	EsTextboxInsert(textbox, "0");

	EsTextDisplayCreate(table, ES_CELL_H_RIGHT | ES_CELL_H_PUSH, 0, INTERFACE_STRING(DesktopSettingsMouseLinesPerScrollNotch)); // TODO.
	textbox = EsTextboxCreate(table, ES_CELL_H_LEFT | ES_CELL_H_PUSH | ES_TEXTBOX_EDIT_BASED, &styleSettingsNumberTextbox);
	textbox->accessKey = 'S';
	EsTextboxUseNumberOverlay(textbox, false);
	EsTextboxInsert(textbox, "3");

	table = EsPanelCreate(container, ES_CELL_H_FILL, &styleSettingsCheckboxGroup);
	SettingsAddCheckbox(table, INTERFACE_STRING(DesktopSettingsMouseSwapLeftAndRightButtons), 'B', 
			"general", "swap_left_and_right_buttons", &api.global->swapLeftAndRightButtons);
	SettingsAddCheckbox(table, INTERFACE_STRING(DesktopSettingsMouseShowShadow), 'W', 
			"general", "show_cursor_shadow", nullptr); // TODO.
	SettingsAddCheckbox(table, INTERFACE_STRING(DesktopSettingsMouseLocateCursorOnCtrl), 'L', 
			"general", "locate_cursor_on_ctrl", nullptr);

	EsSpacerCreate(container, ES_CELL_H_FILL, ES_STYLE_BUTTON_GROUP_SEPARATOR);

	table = EsPanelCreate(container, ES_CELL_H_FILL | ES_PANEL_TABLE | ES_PANEL_HORIZONTAL, &styleSettingsTable);
	EsPanelSetBands(table, 2);

	SettingsAddNumberBox(table, INTERFACE_STRING(DesktopSettingsMouseDoubleClickSpeed), 'D', 
			"general", "click_chain_timeout_ms", &api.global->clickChainTimeoutMs,
			100, 1500, INTERFACE_STRING(CommonUnitMilliseconds));

	EsPanel *testBox = EsPanelCreate(container, ES_CELL_H_FILL);
	EsTextDisplayCreate(testBox, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(DesktopSettingsMouseTestDoubleClickIntroduction));
	EsSpacerCreate(testBox, ES_FLAGS_DEFAULT, 0, 0, 5);
	EsCustomElementCreate(testBox, ES_FLAGS_DEFAULT, ES_STYLE_DOUBLE_CLICK_TEST)->messageUser = SettingsDoubleClickTestMessage;

	SettingsAddUndoButton(element);
}

void SettingsPageKeyboard(EsElement *element, SettingsPage *page) {
	EsPanel *content = EsPanelCreate(element, ES_CELL_FILL | ES_PANEL_V_SCROLL_AUTO, &styleNewTabContent);
	EsPanel *container = EsPanelCreate(content, ES_PANEL_VERTICAL | ES_CELL_H_SHRINK, &styleSettingsGroupContainer2);
	SettingsAddTitle(container, page);

	EsPanel *table = EsPanelCreate(container, ES_CELL_H_FILL | ES_PANEL_TABLE | ES_PANEL_HORIZONTAL, &styleSettingsTable);
	EsPanelSetBands(table, 2);

	EsTextbox *textbox;

	EsTextDisplayCreate(table, ES_CELL_H_RIGHT | ES_CELL_H_PUSH, 0, INTERFACE_STRING(DesktopSettingsKeyboardKeyRepeatDelay)); // TODO.
	textbox = EsTextboxCreate(table, ES_CELL_H_LEFT | ES_CELL_H_PUSH | ES_TEXTBOX_EDIT_BASED, &styleSettingsNumberTextbox);
	textbox->accessKey = 'D';
	EsTextboxUseNumberOverlay(textbox, false);
	EsTextboxInsert(textbox, "400 ms");

	EsTextDisplayCreate(table, ES_CELL_H_RIGHT | ES_CELL_H_PUSH, 0, INTERFACE_STRING(DesktopSettingsKeyboardKeyRepeatRate)); // TODO.
	textbox = EsTextboxCreate(table, ES_CELL_H_LEFT | ES_CELL_H_PUSH | ES_TEXTBOX_EDIT_BASED, &styleSettingsNumberTextbox);
	textbox->accessKey = 'R';
	EsTextboxUseNumberOverlay(textbox, false);
	EsTextboxInsert(textbox, "40 ms");

	EsTextDisplayCreate(table, ES_CELL_H_RIGHT | ES_CELL_H_PUSH, 0, INTERFACE_STRING(DesktopSettingsKeyboardCaretBlinkRate)); // TODO.
	textbox = EsTextboxCreate(table, ES_CELL_H_LEFT | ES_CELL_H_PUSH | ES_TEXTBOX_EDIT_BASED, &styleSettingsNumberTextbox);
	textbox->accessKey = 'B';
	EsTextboxUseNumberOverlay(textbox, false);
	EsTextboxInsert(textbox, "500 ms");

	EsPanel *testBox = EsPanelCreate(container, ES_CELL_H_FILL);
	EsTextDisplayCreate(testBox, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(DesktopSettingsKeyboardTestTextboxIntroduction));
	EsSpacerCreate(testBox, ES_FLAGS_DEFAULT, 0, 0, 5);
	EsTextboxCreate(testBox, ES_CELL_H_LEFT)->accessKey = 'T';

	SettingsAddUndoButton(element);
}

SettingsPage settingsPages[] = {
	{ INTERFACE_STRING(DesktopSettingsAccessibility), ES_ICON_PREFERENCES_DESKTOP_ACCESSIBILITY, SettingsPageUnimplemented, 'A' }, // TODO.
	{ INTERFACE_STRING(DesktopSettingsApplications), ES_ICON_APPLICATIONS_OTHER, SettingsPageUnimplemented, 'A' }, // TODO.
	{ INTERFACE_STRING(DesktopSettingsDateAndTime), ES_ICON_PREFERENCES_SYSTEM_TIME, SettingsPageUnimplemented, 'D' }, // TODO.
	{ INTERFACE_STRING(DesktopSettingsDevices), ES_ICON_COMPUTER_LAPTOP, SettingsPageUnimplemented, 'D' }, // TODO.
	{ INTERFACE_STRING(DesktopSettingsDisplay), ES_ICON_PREFERENCES_DESKTOP_DISPLAY, SettingsPageUnimplemented, 'D' }, // TODO.
	{ INTERFACE_STRING(DesktopSettingsKeyboard), ES_ICON_INPUT_KEYBOARD, SettingsPageKeyboard, 'K' },
	{ INTERFACE_STRING(DesktopSettingsLocalisation), ES_ICON_PREFERENCES_DESKTOP_LOCALE, SettingsPageUnimplemented, 'L' }, // TODO.
	{ INTERFACE_STRING(DesktopSettingsMouse), ES_ICON_INPUT_MOUSE, SettingsPageMouse, 'M' },
	{ INTERFACE_STRING(DesktopSettingsNetwork), ES_ICON_PREFERENCES_SYSTEM_NETWORK, SettingsPageUnimplemented, 'N' }, // TODO.
	{ INTERFACE_STRING(DesktopSettingsPower), ES_ICON_PREFERENCES_SYSTEM_POWER, SettingsPageUnimplemented, 'P' }, // TODO.
	{ INTERFACE_STRING(DesktopSettingsSound), ES_ICON_PREFERENCES_DESKTOP_SOUND, SettingsPageUnimplemented, 'S' }, // TODO.
	{ INTERFACE_STRING(DesktopSettingsTheme), ES_ICON_APPLICATIONS_INTERFACEDESIGN, SettingsPageUnimplemented, 'T' }, // TODO.
};

void SettingsButtonPressed(EsInstance *_instance, EsElement *element, EsCommand *) {
	SettingsInstance *instance = (SettingsInstance *) _instance;
	SettingsPage *page = (SettingsPage *) element->userData.p;

	EsPanel *stack = EsPanelCreate(instance->switcher, ES_CELL_FILL | ES_PANEL_Z_STACK);
	page->create(stack, page);

	{
		EsPanel *overlay = EsPanelCreate(stack, ES_CELL_H_LEFT | ES_CELL_V_TOP, &styleSettingsOverlayPanel);
		EsButton *backButton = EsButtonCreate(overlay, ES_CELL_H_LEFT | ES_BUTTON_TOOLBAR | ES_ELEMENT_STICKY_ACCESS_KEY, 0, INTERFACE_STRING(DesktopSettingsBackButton));
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
			EsButton *button = EsButtonCreate(container, ES_ELEMENT_NO_FOCUS_ON_CLICK | ES_CELL_H_FILL | ES_ELEMENT_STICKY_ACCESS_KEY, 
					&styleSettingsButton, settingsPages[i].string, settingsPages[i].stringBytes);
			button->userData = &settingsPages[i];
			button->accessKey = settingsPages[i].accessKey;
			EsButtonSetIcon(button, settingsPages[i].iconID);
			EsButtonOnCommand(button, SettingsButtonPressed);
		}
	}
}
