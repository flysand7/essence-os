// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

// TODO Searching for a specific option.
// TODO Option descriptions.

#include <stdio.h>

#define UI_LINUX
#define UI_IMPLEMENTATION
#include "luigi.h"

#define DEPENDENCIES_FILE ""
#include "build_common.h"

UIWindow *window;
UITable *optionTable;
UILabel *unsavedChangedLabel;

int OptionTableMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_TABLE_GET_ITEM) {
		UITableGetItem *m = (UITableGetItem *) dp;
		Option *option = options + m->index;

		if (m->column == 2) {
			return snprintf(m->buffer, m->bufferBytes, "%s", option->useDefaultState ? "" : "!");
		} else if (m->column == 1) {
			if (option->type == OPTION_TYPE_BOOL) {
				return snprintf(m->buffer, m->bufferBytes, "%s", option->state.b ? "Yes" : "No");
			} else if (option->type == OPTION_TYPE_STRING) {
				return snprintf(m->buffer, m->bufferBytes, "%s", option->state.s);
			} else {
				// TODO.
			}
		} else {
			return snprintf(m->buffer, m->bufferBytes, "%s", option->id);
		}
	} else if (message == UI_MSG_CLICKED || (message == UI_MSG_RIGHT_UP && element->window->hovered == element)) {
		bool reset = message == UI_MSG_RIGHT_UP;
		int index = UITableHitTest((UITable *) element, element->window->cursorX, element->window->cursorY); 

		if (index != -1) {
			Option *option = options + index;

			if (option->type == OPTION_TYPE_BOOL) {
				if (reset) {
					option->state = option->defaultState;
				} else {
					bool okay = true;

					if (option->warning && !option->state.b) {
						if (UIDialogShow(element->window, 0, "Warning:\n%s\n%f%b%b", option->warning, "Enable", "Cancel")[0] == 'C') {
							okay = false;
						}
					}

					if (okay) {
						option->state.b = !option->state.b;
						option->useDefaultState = false;
					}
				}
			} else if (option->type == OPTION_TYPE_STRING) {
				if (reset) {
					free(option->state.s);
					option->state.s = option->defaultState.s ? strdup(option->defaultState.s) : NULL;
				} else {
					UIDialogShow(element->window, 0, "New value:          \n%t\n%f%b", &option->state.s, "OK");
					option->useDefaultState = false;
				}
			} else {
				// TODO.
				option->useDefaultState = false;
			}

			if (reset) {
				option->useDefaultState = true;
			}

			UITableResizeColumns(optionTable);
			UIElementRefresh(element);
			UILabelSetContent(unsavedChangedLabel, "You have unsaved changes!", -1);
			UIElementRefresh(&unsavedChangedLabel->e);
		}
	}

	return 0;
}

void ActionDefaults(void *_unused) {
	LoadDefaultOptions();
	UIElementRefresh(&optionTable->e);
	UILabelSetContent(unsavedChangedLabel, "You have unsaved changes!", -1);
	UIElementRefresh(&unsavedChangedLabel->e);
}

void ActionSave(void *_unused) {
	FILE *f = fopen("bin/config.ini", "wb");

	for (uintptr_t i = 0; i < sizeof(options) / sizeof(options[0]); i++) {
		if (options[i].useDefaultState) {
			continue;
		}

		if (options[i].type == OPTION_TYPE_BOOL) {
			fprintf(f, "%s=%d\n", options[i].id, options[i].state.b);
		} else if (options[i].type == OPTION_TYPE_STRING) {
			fprintf(f, "%s=%s\n", options[i].id, options[i].state.s ?: "");
		} else {
			// TODO.
		}
	}

	fclose(f);

	UILabelSetContent(unsavedChangedLabel, 0, 0);
	UIElementRefresh(&unsavedChangedLabel->e);
}

int main(int argc, char **argv) {
	LoadOptions();

	UIInitialise();
	ui.theme = _uiThemeClassic;
	window = UIWindowCreate(0, 0, "Config Editor", 1024, 768);
	UIPanel *panel = UIPanelCreate(&window->e, UI_PANEL_EXPAND);

	UIPanel *toolbar = UIPanelCreate(&panel->e, UI_PANEL_SMALL_SPACING | UI_PANEL_GRAY | UI_PANEL_HORIZONTAL);
	UIButtonCreate(&toolbar->e, 0, "Save", -1)->invoke = ActionSave;
	UIWindowRegisterShortcut(window, (UIShortcut) { .code = UI_KEYCODE_LETTER('S'), .ctrl = true, .invoke = ActionSave });
	UIButtonCreate(&toolbar->e, 0, "Defaults", -1)->invoke = ActionDefaults;
	UISpacerCreate(&toolbar->e, 0, 10, 0);
	UILabelCreate(&toolbar->e, 0, "Left click an option to modify it. Right click to reset. (Changes are local.)", -1);

	optionTable = UITableCreate(&panel->e, UI_ELEMENT_V_FILL, "Option\tValue\tModified");
	optionTable->e.messageUser = OptionTableMessage;
	optionTable->itemCount = sizeof(options) / sizeof(options[0]);
	UITableResizeColumns(optionTable);

	unsavedChangedLabel = UILabelCreate(&UIPanelCreate(&panel->e, UI_PANEL_WHITE)->e, UI_ELEMENT_H_FILL, 0, 0);

	return UIMessageLoop();
}
