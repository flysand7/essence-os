// Include the Essence system header.
#include <essence.h>

void _start() {
	// We're not using the C standard library, 
	// so we need to initialise global constructors manually.
	_init();

	while (true) {
		// Receive a message from the system.
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_INSTANCE_CREATE) {
			// The system wants us to create an instance of our application.
			// Call EsInstanceCreate with the message and application name.
			EsInstance *instance = EsInstanceCreate(message, "Hello", -1);

			// Create a text display with the "Hello, world!" message.
			EsTextDisplayCreate(
				instance->window,                 // Add the text display to the instance's window.
				ES_CELL_FILL,                     // The text display should fill the window.
				ES_STYLE_PANEL_WINDOW_BACKGROUND, // Use the window background style.
				"Hello, world!", -1);             // Pass -1 for a zero-terminated string.

			// Keep receiving messages in a loop,
			// so the system can handle input messages for the window.
		}
	}
}
