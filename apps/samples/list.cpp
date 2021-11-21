#include <essence.h>

#define COLUMN_NAME           (0)
#define COLUMN_AGE            (1)
#define COLUMN_FAVORITE_COLOR (2)

const EsListViewEnumString colorStrings[] = {
#define COLOR_RED (0)
	{ "\a#e00]Red", -1 },
#define COLOR_GREEN (1)
	{ "\a#080]Green", -1 },
#define COLOR_BLUE (2)
	{ "\a#00f]Blue", -1 },
};

void AddPerson(EsListView *list, const char *name, int age, int favoriteColor) {
	EsListViewIndex index = EsListViewFixedItemInsert(list, (void *) name /* data */);
	EsListViewFixedItemSetString (list, index, COLUMN_NAME,           name);
	EsListViewFixedItemSetInteger(list, index, COLUMN_AGE,            age);
	EsListViewFixedItemSetInteger(list, index, COLUMN_FAVORITE_COLOR, favoriteColor);
}

void _start() {
	_init();

	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_INSTANCE_CREATE) {
			EsInstance *instance = EsInstanceCreate(message, "List", -1);
			EsPanel *wrapper = EsPanelCreate(instance->window, ES_CELL_FILL, ES_STYLE_PANEL_WINDOW_DIVIDER);

			uint64_t flags;
			flags = ES_CELL_FILL | ES_LIST_VIEW_COLUMNS | ES_LIST_VIEW_FIXED_ITEMS | ES_LIST_VIEW_SINGLE_SELECT;
			EsListView *list = EsListViewCreate(wrapper, flags);
			flags = ES_LIST_VIEW_COLUMN_HAS_MENU;
			EsListViewRegisterColumn(list, COLUMN_NAME, "Name", -1, flags, 150);
			flags = ES_LIST_VIEW_COLUMN_HAS_MENU | ES_TEXT_H_RIGHT | ES_DRAW_CONTENT_TABULAR 
				| ES_LIST_VIEW_COLUMN_FIXED_DATA_INTEGERS | ES_LIST_VIEW_COLUMN_FIXED_SORT_SIZE;
			EsListViewRegisterColumn(list, COLUMN_AGE, "Age", -1, flags, 100);
			flags = ES_LIST_VIEW_COLUMN_HAS_MENU | ES_DRAW_CONTENT_RICH_TEXT 
				| ES_LIST_VIEW_COLUMN_FIXED_FORMAT_ENUM_STRING | ES_LIST_VIEW_COLUMN_FIXED_DATA_INTEGERS;
			EsListViewRegisterColumn(list, COLUMN_FAVORITE_COLOR, "Favorite color", -1, flags, 150);
			EsListViewFixedItemSetEnumStringsForColumn(list, COLUMN_FAVORITE_COLOR, colorStrings, sizeof(colorStrings) / sizeof(colorStrings[0]));
			EsListViewAddAllColumns(list);

			AddPerson(list, "Alice",   40, COLOR_RED);
			AddPerson(list, "Bob",     10, COLOR_GREEN);
			AddPerson(list, "Cameron", 30, COLOR_BLUE);
			AddPerson(list, "Daniel",  20, COLOR_RED);
		}
	}
}
