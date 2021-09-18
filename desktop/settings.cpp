struct SettingsInstance : CommonDesktopInstance {
	EsPanel *switcher;
	EsPanel *mainPage;
	EsButton *undoButton, *backButton;
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
#define SETTINGS_CONTROL_SLIDER (3)
	uint8_t type;
	bool originalValueBool;
	int32_t originalValueInt;
	int32_t minimumValue, maximumValue;
	uint32_t steps;
	double dragSpeed, dragValue, discreteStep;
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

const uint32_t windowColors[] = {
	0xFF0032,
	0xFF6042,
	0xFF7F24,
	0xFFFF24,
	0x67EFC4,
	0x54ACE5,
	0x448CF5,
	0x044CF5,
	0xAC00FF,
	0xE8ECF9,
	0xB7BBC5, // TODO Make this look better when deactivated.
};

const EsStyle styleSettingsGroupContainer3 = {
	.inherit = ES_STYLE_BUTTON_GROUP_CONTAINER,

	.metrics = {
		.mask = ES_THEME_METRICS_PREFERRED_WIDTH | ES_THEME_METRICS_INSETS | ES_THEME_METRICS_GAP_MAJOR,
		.insets = ES_RECT_1(15),
		.preferredWidth = 600,
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
		.gapMajor = 7,
		.gapMinor = 7,
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

const EsStyle styleSliderRow = {
	.metrics = {
		.mask = ES_THEME_METRICS_GAP_MAJOR | ES_THEME_METRICS_INSETS,
		.insets = ES_RECT_4(0, 0, 3, 0),
		.gapMajor = 6,
	},
};

void SettingsUpdateGlobalAndWindowManager() {
	api.global->clickChainTimeoutMs = EsSystemConfigurationReadInteger(EsLiteral("general"), EsLiteral("click_chain_timeout_ms"));
	api.global->swapLeftAndRightButtons = EsSystemConfigurationReadInteger(EsLiteral("general"), EsLiteral("swap_left_and_right_buttons"));
	api.global->showCursorShadow = EsSystemConfigurationReadInteger(EsLiteral("general"), EsLiteral("show_cursor_shadow"));
	api.global->useSmartQuotes = EsSystemConfigurationReadInteger(EsLiteral("general"), EsLiteral("use_smart_quotes"));
	api.global->enableHoverState = EsSystemConfigurationReadInteger(EsLiteral("general"), EsLiteral("enable_hover_state"));

	{
		float newUIScale = EsSystemConfigurationReadInteger(EsLiteral("general"), EsLiteral("ui_scale")) * 0.01f;
		bool changed = api.global->uiScale != newUIScale && api.global->uiScale;
		api.global->uiScale = newUIScale;

		if (changed) {
			EsMessage m;
			EsMemoryZero(&m, sizeof(EsMessage));
			m.type = ES_MSG_UI_SCALE_CHANGED;

			for (uintptr_t i = 0; i < desktop.allApplicationProcesses.Length(); i++) {
				EsMessagePostRemote(desktop.allApplicationProcesses[i]->handle, &m);
			}
		}
	}

	uint32_t cursorProperties = 0;
	if (EsSystemConfigurationReadInteger(EsLiteral("general"), EsLiteral("use_cursor_acceleration"))) cursorProperties |= CURSOR_USE_ACCELERATION;
	if (EsSystemConfigurationReadInteger(EsLiteral("general"), EsLiteral("use_cursor_alt_slow")))     cursorProperties |= CURSOR_USE_ALT_SLOW;
	int32_t cursorSpeed = EsSystemConfigurationReadInteger(EsLiteral("general"), EsLiteral("cursor_speed"));
	double cursorSpeedFactor = EsCRTexp2(0.25 * cursorSpeed /* -20 to 20, 0 is default */) /* 1/32 to 32, 1 is default */;
	cursorProperties |= (uint32_t) (cursorSpeedFactor * 0x100) << 16; // Speed.
	cursorProperties |= (uint32_t) EsSystemConfigurationReadInteger(EsLiteral("general"), EsLiteral("cursor_trails")) << 13; // Trail count.
	EsSyscall(ES_SYSCALL_CURSOR_PROPERTIES_SET, cursorProperties, 0, 0, 0);
}

void SettingsBackButton(EsInstance *_instance, EsElement *, EsCommand *) {
	SettingsInstance *instance = (SettingsInstance *) _instance;

	EsElementSetDisabled(instance->undoButton, true);
	EsElementSetHidden(instance->undoButton, true);
	EsElementSetDisabled(instance->backButton, true);

	instance->controls.Free();

	EsPanelSwitchTo(instance->switcher, instance->mainPage, ES_TRANSITION_ZOOM_OUT, ES_PANEL_SWITCHER_DESTROY_PREVIOUS_AFTER_TRANSITION, 1.0f);
	ConfigurationWriteToFile();
}

void SettingsNumberBoxSetValue(EsElement *element, double newValueDouble) {
	EsTextbox *textbox = (EsTextbox *) element;
	SettingsInstance *instance = (SettingsInstance *) textbox->instance;
	SettingsControl *control = (SettingsControl *) textbox->userData.p;

	int32_t newValue = (int32_t) (newValueDouble / control->discreteStep + 0.5) * control->discreteStep;

	newValue = ClampInteger(control->minimumValue, control->maximumValue, newValue);
	char buffer[64];
	size_t bytes = EsStringFormat(buffer, sizeof(buffer), "%i%s", newValue, control->suffixBytes, control->suffix);
	EsTextboxSelectAll(textbox);
	EsTextboxInsert(textbox, buffer, bytes);

	EsMutexAcquire(&api.systemConfigurationMutex);

	EsSystemConfigurationGroup *group = SystemConfigurationGetGroup(control->cConfigurationSection, -1, true);
	int32_t oldValue = 0;

	if (group) {
		EsSystemConfigurationItem *item = SystemConfigurationGetItem(group, control->cConfigurationKey, -1, true);

		if (item) {
			oldValue = EsIntegerParse(item->value, item->valueBytes);
			EsHeapFree(item->value);
			item->value = (char *) EsHeapAllocate(65, true);
			item->valueBytes = EsStringFormat(item->value, 64, "%fd", ES_STRING_FORMAT_SIMPLE, newValue);
		}
	}

	EsMutexRelease(&api.systemConfigurationMutex);

	if (oldValue != newValue) {
		SettingsUpdateGlobalAndWindowManager();
		EsElementSetDisabled(instance->undoButton, false);
		desktop.configurationModified = true;
	}
}

void SettingsUndoButton(EsInstance *_instance, EsElement *, EsCommand *) {
	SettingsInstance *instance = (SettingsInstance *) _instance;

	for (uintptr_t i = 0; i < instance->controls.Length(); i++) {
		SettingsControl *control = instance->controls[i];

		if (control->type == SETTINGS_CONTROL_CHECKBOX) {
			EsButtonSetCheck((EsButton *) control->element, control->originalValueBool ? ES_CHECK_CHECKED : ES_CHECK_UNCHECKED, true);
		} else if (control->type == SETTINGS_CONTROL_NUMBER) {
			SettingsNumberBoxSetValue(control->element, control->originalValueInt);
		} else if (control->type == SETTINGS_CONTROL_SLIDER) {
			EsSliderSetValue((EsSlider *) control->element, LinearMap(control->minimumValue, control->maximumValue, 0, 1, control->originalValueInt), true);
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

void SettingsPageUnimplemented(EsElement *element, SettingsPage *page) {
	EsPanel *content = EsPanelCreate(element, ES_CELL_FILL | ES_PANEL_V_SCROLL_AUTO, &styleNewTabContent);
	EsPanel *container = EsPanelCreate(content, ES_PANEL_VERTICAL | ES_CELL_H_SHRINK, &styleSettingsGroupContainer2);
	SettingsAddTitle(container, page);

	EsPanel *warningRow = EsPanelCreate(container, ES_CELL_H_CENTER | ES_PANEL_HORIZONTAL, &styleSettingsTable);
	EsIconDisplayCreate(warningRow, ES_FLAGS_DEFAULT, 0, ES_ICON_DIALOG_WARNING);
	EsTextDisplayCreate(warningRow, ES_FLAGS_DEFAULT, 0, "Work in progress" ELLIPSIS);
}

void SettingsCheckboxCommand(EsInstance *_instance, EsElement *element, EsCommand *) {
	EsButton *button = (EsButton *) element;
	SettingsInstance *instance = (SettingsInstance *) _instance;
	SettingsControl *control = (SettingsControl *) button->userData.p;
	bool newValue = EsButtonGetCheck(button) == ES_CHECK_CHECKED;

	EsMutexAcquire(&api.systemConfigurationMutex);

	EsSystemConfigurationGroup *group = SystemConfigurationGetGroup(control->cConfigurationSection, -1, true);
	bool oldValue = false;

	if (group) {
		EsSystemConfigurationItem *item = SystemConfigurationGetItem(group, control->cConfigurationKey, -1, true);

		if (item) {
			oldValue = EsIntegerParse(item->value, item->valueBytes);
			EsHeapFree(item->value);
			item->value = (char *) EsHeapAllocate(2, true);
			*item->value = newValue ? '1' : '0';
			item->valueBytes = 1;
		}
	}

	EsMutexRelease(&api.systemConfigurationMutex);

	if (oldValue == newValue) return;
	SettingsUpdateGlobalAndWindowManager();
	EsElementSetDisabled(instance->undoButton, false);
	desktop.configurationModified = true;
}

void SettingsAddCheckbox(EsElement *table, const char *string, ptrdiff_t stringBytes, char accessKey,
		const char *cConfigurationSection, const char *cConfigurationKey) {
	SettingsInstance *instance = (SettingsInstance *) table->instance;

	SettingsControl *control = (SettingsControl *) EsHeapAllocate(sizeof(SettingsControl), true);
	control->type = SETTINGS_CONTROL_CHECKBOX;
	control->cConfigurationSection = cConfigurationSection;
	control->cConfigurationKey = cConfigurationKey;
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
		size_t bytes;
		char *expression = EsTextboxGetContents(textbox, &bytes);

		if (control->suffixBytes && bytes > control->suffixBytes && 0 == EsMemoryCompare(control->suffix, 
					expression + bytes - control->suffixBytes, control->suffixBytes)) {
			// Trim the suffix.
			expression[bytes - control->suffixBytes] = 0;
		}

		EsCalculationValue value = EsCalculateFromUserExpression(expression); 
		EsHeapFree(expression);

		if (!value.error) {
			SettingsNumberBoxSetValue(element, value.number);
			return ES_HANDLED;
		} else {
			return ES_REJECTED;
		}
	} else if (message->type == ES_MSG_TEXTBOX_NUMBER_DRAG_START) {
		control->dragValue = EsTextboxGetContentsAsDouble(textbox);
	} else if (message->type == ES_MSG_TEXTBOX_NUMBER_DRAG_DELTA) {
		double newValue = ClampDouble(control->minimumValue, control->maximumValue, 
				control->dragValue + message->numberDragDelta.delta * (message->numberDragDelta.fast ? 10.0 : 1.0) * control->dragSpeed);
		control->dragValue = newValue;
		char buffer[64];
		size_t bytes = EsStringFormat(buffer, sizeof(buffer), "%i%s", (int32_t) newValue, control->suffixBytes, control->suffix);
		EsTextboxSelectAll(textbox);
		EsTextboxInsert(textbox, buffer, bytes);
		return ES_HANDLED;
	}

	return 0;
}

void SettingsAddNumberBox(EsElement *table, const char *string, ptrdiff_t stringBytes, char accessKey, 
		const char *cConfigurationSection, const char *cConfigurationKey,
		int32_t minimumValue, int32_t maximumValue, const char *suffix, ptrdiff_t suffixBytes, double dragSpeed, double discreteStep) {
	if (suffixBytes == -1) {
		suffixBytes = EsCStringLength(suffix);
	}

	SettingsInstance *instance = (SettingsInstance *) table->instance;

	SettingsControl *control = (SettingsControl *) EsHeapAllocate(sizeof(SettingsControl), true);
	control->type = SETTINGS_CONTROL_NUMBER;
	control->cConfigurationSection = cConfigurationSection;
	control->cConfigurationKey = cConfigurationKey;
	control->originalValueInt = EsSystemConfigurationReadInteger(control->cConfigurationSection, -1, control->cConfigurationKey, -1);
	control->suffix = suffix;
	control->suffixBytes = suffixBytes;
	control->minimumValue = minimumValue;
	control->maximumValue = maximumValue;
	control->dragSpeed = dragSpeed;
	control->discreteStep = discreteStep;

	EsTextDisplayCreate(table, ES_CELL_H_RIGHT | ES_CELL_H_PUSH, 0, string, stringBytes); 
	EsTextbox *textbox = EsTextboxCreate(table, ES_CELL_H_LEFT | ES_CELL_H_PUSH | ES_TEXTBOX_EDIT_BASED | ES_ELEMENT_FREE_USER_DATA, &styleSettingsNumberTextbox);
	EsTextboxUseNumberOverlay(textbox, false);
	textbox->userData = control;
	textbox->accessKey = accessKey;
	textbox->messageUser = SettingsNumberBoxMessage;

	char buffer[64];
	size_t bytes = EsStringFormat(buffer, sizeof(buffer), "%i%s", control->originalValueInt, suffixBytes, suffix);
	EsTextboxInsert(textbox, buffer, bytes);

	control->element = textbox;
	instance->controls.Add(control);
}

int SettingsSliderMessage(EsElement *element, EsMessage *message) {
	EsSlider *slider = (EsSlider *) element;
	SettingsInstance *instance = (SettingsInstance *) slider->instance;
	SettingsControl *control = (SettingsControl *) slider->userData.p;

	if (message->type == ES_MSG_SLIDER_MOVED && !message->sliderMoved.inDrag) {
		EsMutexAcquire(&api.systemConfigurationMutex);

		EsSystemConfigurationGroup *group = SystemConfigurationGetGroup(control->cConfigurationSection, -1, true);
		int32_t oldValue = 0;
		int32_t newValue = LinearMap(0, 1, control->minimumValue, control->maximumValue, EsSliderGetValue(slider));

		if (group) {
			EsSystemConfigurationItem *item = SystemConfigurationGetItem(group, control->cConfigurationKey, -1, true);

			if (item) {
				oldValue = EsIntegerParse(item->value, item->valueBytes);
				EsHeapFree(item->value);
				item->value = (char *) EsHeapAllocate(65, true);
				item->valueBytes = EsStringFormat(item->value, 64, "%fd", ES_STRING_FORMAT_SIMPLE, newValue);
			}
		}

		EsMutexRelease(&api.systemConfigurationMutex);

		if (oldValue != newValue) {
			SettingsUpdateGlobalAndWindowManager();
			EsElementSetDisabled(instance->undoButton, false);
			desktop.configurationModified = true;
		}
	}

	return 0;
}

void SettingsAddSlider(EsElement *table, const char *string, ptrdiff_t stringBytes, char accessKey, 
		const char *cConfigurationSection, const char *cConfigurationKey,
		int32_t minimumValue, int32_t maximumValue, uint32_t steps,
		const char *lowString, ptrdiff_t lowStringBytes, const char *highString, ptrdiff_t highStringBytes) {
	SettingsInstance *instance = (SettingsInstance *) table->instance;

	SettingsControl *control = (SettingsControl *) EsHeapAllocate(sizeof(SettingsControl), true);
	control->type = SETTINGS_CONTROL_SLIDER;
	control->cConfigurationSection = cConfigurationSection;
	control->cConfigurationKey = cConfigurationKey;
	control->originalValueInt = EsSystemConfigurationReadInteger(control->cConfigurationSection, -1, control->cConfigurationKey, -1);
	control->minimumValue = minimumValue;
	control->maximumValue = maximumValue;
	control->steps = steps;

	EsPanel *stack = EsPanelCreate(table, ES_CELL_H_FILL);
	EsTextDisplayCreate(stack, ES_CELL_H_LEFT, 0, string, stringBytes); 
	EsPanel *row = EsPanelCreate(stack, ES_PANEL_HORIZONTAL | ES_CELL_H_FILL, &styleSliderRow);
	EsTextDisplayCreate(row, ES_CELL_H_PUSH | ES_CELL_H_RIGHT, 0, lowString, lowStringBytes); 
	EsSlider *slider = EsSliderCreate(row, ES_ELEMENT_FREE_USER_DATA, 0, 
			LinearMap(control->minimumValue, control->maximumValue, 0, 1, control->originalValueInt), steps);
	EsTextDisplayCreate(row, ES_CELL_H_PUSH | ES_CELL_H_LEFT, 0, highString, highStringBytes); 
	slider->userData = control;
	slider->accessKey = accessKey;
	slider->messageUser = SettingsSliderMessage;

	control->element = slider;
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
	EsElementSetHidden(((SettingsInstance *) element->instance)->undoButton, false);

	EsPanel *content = EsPanelCreate(element, ES_CELL_FILL | ES_PANEL_V_SCROLL_AUTO, &styleNewTabContent);
	EsPanel *container = EsPanelCreate(content, ES_PANEL_VERTICAL | ES_CELL_H_SHRINK, &styleSettingsGroupContainer2);
	SettingsAddTitle(container, page);

	EsPanel *table;

	SettingsAddSlider(container, INTERFACE_STRING(DesktopSettingsMouseSpeed), 'M', "general", "cursor_speed", -20, 20, 41, 
			INTERFACE_STRING(DesktopSettingsMouseSpeedSlow), INTERFACE_STRING(DesktopSettingsMouseSpeedFast));

	table = EsPanelCreate(container, ES_CELL_H_FILL, &styleSettingsCheckboxGroup);
	SettingsAddCheckbox(table, INTERFACE_STRING(DesktopSettingsMouseUseAcceleration), 'C', "general", "use_cursor_acceleration");
	SettingsAddCheckbox(table, INTERFACE_STRING(DesktopSettingsMouseSlowOnAlt), 'O', "general", "use_cursor_alt_slow");

	EsSpacerCreate(container, ES_CELL_H_FILL, ES_STYLE_BUTTON_GROUP_SEPARATOR);

	SettingsAddSlider(container, INTERFACE_STRING(DesktopSettingsMouseCursorTrails), 'T', "general", "cursor_trails", 0, 7, 8, 
			INTERFACE_STRING(DesktopSettingsMouseCursorTrailsNone), INTERFACE_STRING(DesktopSettingsMouseCursorTrailsMany));

	table = EsPanelCreate(container, ES_CELL_H_FILL | ES_PANEL_TABLE | ES_PANEL_HORIZONTAL, &styleSettingsTable);
	EsPanelSetBands(table, 2);

	SettingsAddNumberBox(table, INTERFACE_STRING(DesktopSettingsMouseLinesPerScrollNotch), 'S', "general", "scroll_lines_per_notch", 
			1, 100, nullptr, 0, 0.04, 1);

	table = EsPanelCreate(container, ES_CELL_H_FILL, &styleSettingsCheckboxGroup);
	SettingsAddCheckbox(table, INTERFACE_STRING(DesktopSettingsMouseSwapLeftAndRightButtons), 'B', "general", "swap_left_and_right_buttons");
	SettingsAddCheckbox(table, INTERFACE_STRING(DesktopSettingsMouseShowShadow), 'W', "general", "show_cursor_shadow");
	SettingsAddCheckbox(table, INTERFACE_STRING(DesktopSettingsMouseLocateCursorOnCtrl), 'L', "general", "locate_cursor_on_ctrl");

	EsSpacerCreate(container, ES_CELL_H_FILL, ES_STYLE_BUTTON_GROUP_SEPARATOR);

	table = EsPanelCreate(container, ES_CELL_H_FILL | ES_PANEL_TABLE | ES_PANEL_HORIZONTAL, &styleSettingsTable);
	EsPanelSetBands(table, 2);

	SettingsAddNumberBox(table, INTERFACE_STRING(DesktopSettingsMouseDoubleClickSpeed), 'D', "general", "click_chain_timeout_ms", 
			100, 1500, INTERFACE_STRING(CommonUnitMilliseconds), 1.0, 1);

	EsPanel *testBox = EsPanelCreate(container, ES_CELL_H_FILL);
	EsTextDisplayCreate(testBox, ES_CELL_H_FILL, ES_STYLE_TEXT_PARAGRAPH, INTERFACE_STRING(DesktopSettingsMouseTestDoubleClickIntroduction));
	EsSpacerCreate(testBox, ES_FLAGS_DEFAULT, 0, 0, 5);
	EsCustomElementCreate(testBox, ES_FLAGS_DEFAULT, ES_STYLE_DOUBLE_CLICK_TEST)->messageUser = SettingsDoubleClickTestMessage;
}

void SettingsPageKeyboard(EsElement *element, SettingsPage *page) {
	EsElementSetHidden(((SettingsInstance *) element->instance)->undoButton, false);

	EsPanel *content = EsPanelCreate(element, ES_CELL_FILL | ES_PANEL_V_SCROLL_AUTO, &styleNewTabContent);
	EsPanel *container = EsPanelCreate(content, ES_PANEL_VERTICAL | ES_CELL_H_SHRINK, &styleSettingsGroupContainer2);
	SettingsAddTitle(container, page);

	EsPanel *warningRow = EsPanelCreate(container, ES_CELL_H_CENTER | ES_PANEL_HORIZONTAL, &styleSettingsTable);
	EsIconDisplayCreate(warningRow, ES_FLAGS_DEFAULT, 0, ES_ICON_DIALOG_WARNING);
	EsTextDisplayCreate(warningRow, ES_FLAGS_DEFAULT, 0, "Work in progress" ELLIPSIS);

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

	table = EsPanelCreate(container, ES_CELL_H_FILL, &styleSettingsCheckboxGroup);
	SettingsAddCheckbox(table, INTERFACE_STRING(DesktopSettingsKeyboardUseSmartQuotes), 'Q', "general", "use_smart_quotes");
}

void SettingsPageDisplay(EsElement *element, SettingsPage *page) {
	// TODO.

	EsElementSetHidden(((SettingsInstance *) element->instance)->undoButton, false);

	EsPanel *content = EsPanelCreate(element, ES_CELL_FILL | ES_PANEL_V_SCROLL_AUTO, &styleNewTabContent);
	EsPanel *container = EsPanelCreate(content, ES_PANEL_VERTICAL | ES_CELL_H_SHRINK, &styleSettingsGroupContainer2);
	SettingsAddTitle(container, page);

	EsPanel *warningRow = EsPanelCreate(container, ES_CELL_H_CENTER | ES_PANEL_HORIZONTAL, &styleSettingsTable);
	EsIconDisplayCreate(warningRow, ES_FLAGS_DEFAULT, 0, ES_ICON_DIALOG_WARNING);
	EsTextDisplayCreate(warningRow, ES_FLAGS_DEFAULT, 0, "Work in progress" ELLIPSIS);

	EsPanel *table = EsPanelCreate(container, ES_CELL_H_FILL | ES_PANEL_TABLE | ES_PANEL_HORIZONTAL, &styleSettingsTable);
	EsPanelSetBands(table, 2);
	SettingsAddNumberBox(table, INTERFACE_STRING(DesktopSettingsDisplayUIScale), 'S', "general", "ui_scale", 
			100, 400, INTERFACE_STRING(CommonUnitPercent), 0.05, 5);
}

int SettingsColorButtonMessage(EsElement *element, EsMessage *message) {
	if (message->type == ES_MSG_PAINT_BACKGROUND) {
		// HACK This assumes a lot about the workings of the radiobox style...

		EsRectangle bounds = EsPainterBoundsClient(message->painter);
		bounds.r = bounds.l + 14 * EsElementGetScaleFactor(element);
		bounds = EsRectangleFit(bounds, ES_RECT_2S(1, 1), true);
		uint32_t cornerRadius = Width(bounds);
		uint32_t cornerRadii[4] = { cornerRadius, cornerRadius, cornerRadius, cornerRadius };
		int32_t borderSize = 1 * EsElementGetScaleFactor(element);

		uint32_t color = EsColorBlend(windowColors[element->userData.u] | 0xFF000000, 0x20FFFFFF, false);
		if (element->window->pressed == element) color = EsColorBlend(color, 0x40000000, false);
		EsDrawRoundedRectangle(message->painter, bounds, color, EsColorBlend(color, 0x40000000, false), ES_RECT_1(borderSize), cornerRadii);

		if (EsButtonGetCheck((EsButton *) element) == ES_CHECK_CHECKED) {
			int32_t inset = 4 * EsElementGetScaleFactor(element);
			uint32_t bulletColor = EsColorIsLight(color) ? 0xFF000000 : 0xFFFFFFFF;
			EsDrawRoundedRectangle(message->painter, EsRectangleAdd(bounds, ES_RECT_1I(inset)), bulletColor, 0, ES_RECT_1(0), cornerRadii);
		}

		return ES_HANDLED;
	}

	return 0;
}

void SettingsWindowColorUpdated() {
	uint8_t index = EsSystemConfigurationReadInteger(EsLiteral("general"), EsLiteral("window_color"));

	if (index > sizeof(windowColors) / sizeof(windowColors[0])) {
		index = 0;
	}

	EsColorConvertToHSV(windowColors[index], &theming.systemHue, &theming.systemSaturation, &theming.systemValue);

	if (theming.systemHue > 0.3f && theming.systemHue < 3.3f) {
		theming.systemHueShift = -1.5f;
	} else {
		theming.systemHueShift = 1.0f;
	}

	for (uintptr_t i = 0; i < gui.allWindows.Length(); i++) {
		if (gui.allWindows[i]->windowStyle == ES_WINDOW_CONTAINER) {
			gui.allWindows[i]->Repaint(true);
			UIWindowNeedsUpdate(gui.allWindows[i]);
		}
	}
}

void SettingsColorButtonCommand(EsInstance *, EsElement *element, EsCommand *) {
	if (EsButtonGetCheck((EsButton *) element) != ES_CHECK_CHECKED) {
		return;
	}

	EsMutexAcquire(&api.systemConfigurationMutex);
	EsSystemConfigurationGroup *group = SystemConfigurationGetGroup("general", -1, true);

	if (group) {
		EsSystemConfigurationItem *item = SystemConfigurationGetItem(group, "window_color", -1, true);

		if (item) {
			EsHeapFree(item->value);
			item->value = (char *) EsHeapAllocate(65, true);
			item->valueBytes = EsStringFormat(item->value, 64, "%fd", ES_STRING_FORMAT_SIMPLE, element->userData.u);
		}
	}

	EsMutexRelease(&api.systemConfigurationMutex);
	SettingsWindowColorUpdated();
	desktop.configurationModified = true;
}

void SettingsPageTheme(EsElement *element, SettingsPage *page) {
	// TODO Fonts, theme file, etc.

	EsPanel *content = EsPanelCreate(element, ES_CELL_FILL | ES_PANEL_V_SCROLL_AUTO, &styleNewTabContent);
	EsPanel *container = EsPanelCreate(content, ES_PANEL_VERTICAL | ES_CELL_H_SHRINK, &styleSettingsGroupContainer2);
	SettingsAddTitle(container, page);

	EsPanel *warningRow = EsPanelCreate(container, ES_CELL_H_CENTER | ES_PANEL_HORIZONTAL, &styleSettingsTable);
	EsIconDisplayCreate(warningRow, ES_FLAGS_DEFAULT, 0, ES_ICON_DIALOG_WARNING);
	EsTextDisplayCreate(warningRow, ES_FLAGS_DEFAULT, 0, "Work in progress" ELLIPSIS);

	EsPanel *table = EsPanelCreate(container, ES_CELL_H_CENTER | ES_PANEL_TABLE | ES_PANEL_HORIZONTAL, &styleSettingsTable);
	EsPanelSetBands(table, 2);
	EsTextDisplayCreate(table, ES_CELL_H_RIGHT, 0, INTERFACE_STRING(DesktopSettingsThemeWallpaper)); 

	EsTextbox *textbox = EsTextboxCreate(table, ES_CELL_H_LEFT | ES_CELL_H_PUSH | ES_TEXTBOX_EDIT_BASED | ES_ELEMENT_FREE_USER_DATA, ES_STYLE_TEXTBOX_BORDERED_SINGLE);
	size_t currentWallpaperBytes;
	char *currentWallpaper = EsSystemConfigurationReadString(EsLiteral("general"), EsLiteral("wallpaper"), &currentWallpaperBytes);
	EsTextboxInsert(textbox, currentWallpaper, currentWallpaperBytes);
	EsHeapFree(currentWallpaper);

	textbox->messageUser = [] (EsElement *element, EsMessage *message) {
		if (message->type == ES_MSG_TEXTBOX_EDIT_END) {
			EsMutexAcquire(&api.systemConfigurationMutex);

			EsSystemConfigurationGroup *group = SystemConfigurationGetGroup("general", -1, true);

			if (group) {
				EsSystemConfigurationItem *item = SystemConfigurationGetItem(group, "wallpaper", -1, true);

				if (item) {
					EsHeapFree(item->value);
					item->value = EsTextboxGetContents((EsTextbox *) element, &item->valueBytes);
					desktop.configurationModified = true;
					EsThreadCreate(WallpaperLoad, nullptr, 0);
				}
			}

			EsMutexRelease(&api.systemConfigurationMutex);
			return ES_HANDLED;
		}

		return 0;
	};

	EsTextDisplayCreate(table, ES_CELL_H_RIGHT, 0, INTERFACE_STRING(DesktopSettingsThemeWindowColor)); 
	EsPanel *panel = EsPanelCreate(table, ES_CELL_H_LEFT | ES_PANEL_HORIZONTAL | ES_PANEL_RADIO_GROUP);
	uint8_t windowColor = EsSystemConfigurationReadInteger(EsLiteral("general"), EsLiteral("window_color"));

	for (uintptr_t i = 0; i < sizeof(windowColors) / sizeof(windowColors[0]); i++) {
		EsButton *button = EsButtonCreate(panel, ES_CELL_H_LEFT | ES_BUTTON_RADIOBOX | ES_ELEMENT_NO_FOCUS_ON_CLICK);
		if (windowColor == i) EsButtonSetCheck(button, ES_CHECK_CHECKED);
		button->userData.u = i;
		button->messageUser = SettingsColorButtonMessage;
		EsButtonOnCommand(button, SettingsColorButtonCommand);
	}

	table = EsPanelCreate(container, ES_CELL_H_FILL, &styleSettingsCheckboxGroup);
	SettingsAddCheckbox(table, INTERFACE_STRING(DesktopSettingsThemeEnableHoverState), 'H', "general", "enable_hover_state");
}

SettingsPage settingsPages[] = {
	// { INTERFACE_STRING(DesktopSettingsAccessibility), ES_ICON_PREFERENCES_DESKTOP_ACCESSIBILITY, SettingsPageUnimplemented, 'A' }, // TODO.
	// { INTERFACE_STRING(DesktopSettingsDateAndTime), ES_ICON_PREFERENCES_SYSTEM_TIME, SettingsPageUnimplemented, 'C' }, // TODO.
	{ INTERFACE_STRING(DesktopSettingsDisplay), ES_ICON_PREFERENCES_DESKTOP_DISPLAY, SettingsPageDisplay, 'D' },
	{ INTERFACE_STRING(DesktopSettingsKeyboard), ES_ICON_INPUT_KEYBOARD, SettingsPageKeyboard, 'E' },
	// { INTERFACE_STRING(DesktopSettingsLocalisation), ES_ICON_PREFERENCES_DESKTOP_LOCALE, SettingsPageUnimplemented, 'F' }, // TODO.
	{ INTERFACE_STRING(DesktopSettingsMouse), ES_ICON_INPUT_MOUSE, SettingsPageMouse, 'G' },
	// { INTERFACE_STRING(DesktopSettingsNetwork), ES_ICON_PREFERENCES_SYSTEM_NETWORK, SettingsPageUnimplemented, 'H' }, // TODO.
	// { INTERFACE_STRING(DesktopSettingsPower), ES_ICON_PREFERENCES_SYSTEM_POWER, SettingsPageUnimplemented, 'J' }, // TODO.
	// { INTERFACE_STRING(DesktopSettingsSound), ES_ICON_PREFERENCES_DESKTOP_SOUND, SettingsPageUnimplemented, 'K' }, // TODO.
	{ INTERFACE_STRING(DesktopSettingsTheme), ES_ICON_APPLICATIONS_INTERFACEDESIGN, SettingsPageTheme, 'M' },
};

void SettingsButtonPressed(EsInstance *_instance, EsElement *element, EsCommand *) {
	SettingsInstance *instance = (SettingsInstance *) _instance;
	SettingsPage *page = (SettingsPage *) element->userData.p;

	EsPanel *stack = EsPanelCreate(instance->switcher, ES_CELL_FILL | ES_PANEL_Z_STACK);
	page->create(stack, page);

	EsPanelSwitchTo(instance->switcher, stack, ES_TRANSITION_ZOOM_IN, ES_FLAGS_DEFAULT, 1.0f);
	EsElementSetDisabled(instance->backButton, false);
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
		EsElement *toolbar = EsWindowGetToolbar(instance->window);

		EsButton *backButton = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_STICKY_ACCESS_KEY, 0, INTERFACE_STRING(DesktopSettingsBackButton));
		instance->backButton = backButton;
		backButton->accessKey = 'A';
		EsButtonSetIcon(backButton, ES_ICON_GO_HOME_SYMBOLIC);
		EsButtonOnCommand(backButton, SettingsBackButton);
		EsElementSetDisabled(backButton, true);

		EsSpacerCreate(toolbar, ES_CELL_FILL);

		EsButton *undoButton = EsButtonCreate(toolbar, ES_BUTTON_TOOLBAR | ES_ELEMENT_STICKY_ACCESS_KEY, 0, INTERFACE_STRING(DesktopSettingsUndoButton));
		instance->undoButton = undoButton;
		undoButton->accessKey = 'U';
		EsButtonSetIcon(undoButton, ES_ICON_EDIT_UNDO_SYMBOLIC);
		EsButtonOnCommand(undoButton, SettingsUndoButton);
		EsElementSetDisabled(undoButton, true);
		EsElementSetHidden(undoButton, true);
	}

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
			SettingsPage *page = &settingsPages[i];
			EsButton *button = EsButtonCreate(container, ES_ELEMENT_NO_FOCUS_ON_CLICK | ES_CELL_H_FILL | ES_ELEMENT_STICKY_ACCESS_KEY, 
					&styleSettingsButton, page->string, page->stringBytes);
			button->userData = page;
			button->accessKey = EsCRTisalpha(page->string[0]) ? page->string[0] : page->accessKey;
			EsButtonSetIcon(button, page->iconID);
			EsButtonOnCommand(button, SettingsButtonPressed);
		}
	}

	instance->destroy = [] (EsInstance *) {
		ConfigurationWriteToFile();
	};
}
