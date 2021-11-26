#include <essence.h>

#define COLUMN_NAME           (0)
#define COLUMN_AGE            (1)
#define COLUMN_FAVORITE_COLOR (2)

const EsListViewEnumString colorStrings[] = {
	// We are using enum strings for the favorite color.
	// "\a ... ]" is a bit of rich text markup.
	// "#" sets the color to the hex code.
#define COLOR_RED (0)
	{ "\a#e00]Red", -1 },
#define COLOR_GREEN (1)
	{ "\a#080]Green", -1 },
#define COLOR_BLUE (2)
	{ "\a#00f]Blue", -1 },
};

void AddPerson(EsListView *list, const char *name, int age, int favoriteColor) {
	// Add a new item to the list, at the end.
	// Use the name pointer as the unique identifier to the item.
	// This returns the internal index of the item.
	EsListViewIndex index = EsListViewFixedItemInsert(list, (void *) name);

	// Set the name, age and favorite color for the item at the returned index.
	EsListViewFixedItemSetString (list, index, COLUMN_NAME,           name);
	EsListViewFixedItemSetInteger(list, index, COLUMN_AGE,            age);
	EsListViewFixedItemSetInteger(list, index, COLUMN_FAVORITE_COLOR, favoriteColor);
}

void _start() {
	_init();

	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_INSTANCE_CREATE) {
			// Create the instance of the application.
			EsInstance *instance = EsInstanceCreate(message, "List", -1);

			// Create a wrapper panel at the root of the window with the style ES_STYLE_PANEL_WINDOW_DIVIDER.
			// This will draw the divider between the toolbar and window contents.
			EsPanel *wrapper = EsPanelCreate(instance->window, ES_CELL_FILL, ES_STYLE_PANEL_WINDOW_DIVIDER);

			// Create the list view.
			EsListView *list = EsListViewCreate(
					wrapper,                       // The wrapper panel is the parent.
					ES_CELL_FILL                   // Fill the wrapper panel, which in turn fills the window.
					| ES_LIST_VIEW_COLUMNS         // Display a column header.
					| ES_LIST_VIEW_FIXED_ITEMS     // Use fixed items mode. Otherwise we'd have to provide the item data via a callback.
					| ES_LIST_VIEW_SINGLE_SELECT); // Allow a single item to be selected at once (or no items).

			// Register the name column.
			EsListViewRegisterColumn(list, 
					COLUMN_NAME,                  // The column's ID.
					"Name", -1,                   // Its title string.
					ES_LIST_VIEW_COLUMN_HAS_MENU, // Allow the user to click on the column header to get the menu for sorting options.
					150);                         // Initial width in scaled pixels.

			// Register the age column.
			EsListViewRegisterColumn(list, 
					COLUMN_AGE,                               // The column's ID.
					"Age", -1,                                // Its title string.
					ES_LIST_VIEW_COLUMN_HAS_MENU              // Column header has a menu.
					| ES_TEXT_H_RIGHT                         // Align the text in the column to the right.
					| ES_DRAW_CONTENT_TABULAR                 // Use the tabular digits style, so that digits line up between rows.
					| ES_LIST_VIEW_COLUMN_FIXED_DATA_INTEGERS // We're storing integers in this column (the default is strings).
					| ES_LIST_VIEW_COLUMN_FIXED_SORT_SIZE,    // The items in the column can be sorted by their size.
					100);                                     // Initial width.

			// Register the favorite color columns.
			EsListViewRegisterColumn(list, 
					COLUMN_FAVORITE_COLOR,                         // The column's ID.
					"Favorite color", -1,                          // Its title string.
					ES_LIST_VIEW_COLUMN_HAS_MENU                   // Column header has a menu.
					| ES_DRAW_CONTENT_RICH_TEXT                    // Parse rich text markup in the strings. (See colorStrings above).
					| ES_LIST_VIEW_COLUMN_FIXED_FORMAT_ENUM_STRING // To display an item, lookup an enum string from the array.
					| ES_LIST_VIEW_COLUMN_FIXED_DATA_INTEGERS,     // The enum values are stored as integers.
					150);                                          // Initial widths.
			EsListViewFixedItemSetEnumStringsForColumn(list, 
					COLUMN_FAVORITE_COLOR,                           // Set the enum strings for the favorite color column.
					colorStrings,                                    // The strings to use for the enum items in this column.
					sizeof(colorStrings) / sizeof(colorStrings[0])); // The number of strings in the array.

			// Add all the registered columns to the column header.
			EsListViewAddAllColumns(list);

			// Populate the list with sample data.
			AddPerson(list, "Alice",   40, COLOR_RED);
			AddPerson(list, "Bob",     10, COLOR_GREEN);
			AddPerson(list, "Cameron", 30, COLOR_BLUE);
			AddPerson(list, "Daniel",  20, COLOR_RED);
		}
	}
}
