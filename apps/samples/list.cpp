#include <essence.h>

EsListViewColumn columns[] = {
	// Title                Flags                      Initial width
	{ "Name",           -1, ES_FLAGS_DEFAULT,          150 },
	{ "Age",            -1, ES_TEXT_H_RIGHT,           100 },
	{ "Favorite color", -1, ES_DRAW_CONTENT_RICH_TEXT, 150 },
};

void AddPerson(EsListView *list, const char *name, int age, const char *favoriteColor) {
	char ageString[16];
	EsStringFormat(ageString, sizeof(ageString), "%d%c", age, 0);

	EsListViewIndex index = EsListViewFixedItemInsert(list, name);
	EsListViewFixedItemAddString(list, index, ageString);
	EsListViewFixedItemAddString(list, index, favoriteColor);
}

void _start() {
	_init();

	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_INSTANCE_CREATE) {
			EsInstance *instance = EsInstanceCreate(message, "List", -1);
			EsPanel *wrapper = EsPanelCreate(instance->window, ES_CELL_FILL, ES_STYLE_PANEL_WINDOW_DIVIDER);
			EsListView *list = EsListViewCreate(wrapper, ES_CELL_FILL | ES_LIST_VIEW_COLUMNS | ES_LIST_VIEW_FIXED_ITEMS);
			EsListViewSetColumns(list, columns, sizeof(columns) / sizeof(columns[0]));
			AddPerson(list, "Alice",   20, "\a#e00]Red");
			AddPerson(list, "Bob",     30, "\a#080]Green");
			AddPerson(list, "Cameron", 40, "\a#00f]Blue");
		}
	}
}
