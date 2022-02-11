// Include the Essence system header.
#include <essence.h>

// Define the metrics for panelStack.
// These values are specified in pixels,
// but will be automatically scaled using the UI scaling factor.
const EsStyle stylePanelStack = {
	.metrics = {
		.mask = ES_THEME_METRICS_INSETS
			| ES_THEME_METRICS_GAP_MAJOR,
		.insets = ES_RECT_1(20), // Spacing around the contents.
		.gapMajor = 15,          // Spacing between items.
	},
};

// Global variables.
EsTextbox     *textboxRate;
EsTextbox     *textboxAmount;
EsTextDisplay *displayResult;

void ConvertCommand(EsInstance *, EsElement *, EsCommand *) {
	// Get the conversion rate and amount to convert from the textboxes.
	double rate   = EsTextboxGetContentsAsDouble(textboxRate); 
	double amount = EsTextboxGetContentsAsDouble(textboxAmount); 

	// Calculate the result, and format it as a string.
	char result[64];
	size_t resultBytes = EsStringFormat(result, sizeof(result), "Result: $%F", rate * amount); 

	// Replace the contents of the result textbox.
	EsTextDisplaySetContents(displayResult, result, resultBytes);
}

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
			EsInstance *instance = EsInstanceCreate(message, "Converter");

			// Create a layout panel to draw the window background.
			EsPanel *panelRoot = EsPanelCreate(
				instance->window,                  // Make the panel the root element of the window.
				ES_CELL_FILL                       // The panel should fill the window.
				| ES_PANEL_V_SCROLL_AUTO           // Automatically show a vertical scroll bar if needed.
				| ES_PANEL_H_SCROLL_AUTO,          // Automatically show a horizontal scroll bar if needed.
				ES_STYLE_PANEL_WINDOW_BACKGROUND); // Use the window background style.
			panelRoot->cName = "panelRoot";

			// Create a vertical stack to layout the contents of window.
			EsPanel *panelStack = EsPanelCreate(
				panelRoot,           // Add it to panelRoot.
				ES_CELL_H_CENTER     // Horizontally center it in panelRoot.
				| ES_PANEL_STACK     // Use the stack layout.
				| ES_PANEL_VERTICAL, // Layout child elements from top to bottom.
				EsStyleIntern(&stylePanelStack));

			// Add a second layout panel to panelStack to contain the elements of the form.
			EsPanel *panelForm = EsPanelCreate(
				panelStack,                 // Add it to panelStack.
				ES_PANEL_TABLE              // Use table layout.
				| ES_PANEL_HORIZONTAL,      // Left to right, then top to bottom.
				ES_STYLE_PANEL_FORM_TABLE); // Use the standard metrics for a form.

			// Set the number of columns for the panelForm's table layout.
			EsPanelSetBands(panelForm, 2); 

			// Add a text display and textbox for the conversion rate to panelForm.
			EsTextDisplayCreate(
				panelForm,             // Add it to panelForm. It will go in the first column.
				ES_CELL_H_RIGHT,       // Align it to the right of the column.
				ES_STYLE_TEXT_LABEL,   // Use the text label style.
				"Amount per dollar:"); // The contents of the text display.
			textboxRate = EsTextboxCreate(
				panelForm,       // Add it to panelForm. It will go in the second column.
				ES_CELL_H_LEFT); // Align it to the left of the column.

			// Set the keyboard focus on the rate textbox.
			EsElementFocus(textboxRate);

			// Add a text display and textbox for the conversion amount to panelForm.
			EsTextDisplayCreate(panelForm, ES_CELL_H_RIGHT, ES_STYLE_TEXT_LABEL, "Value to convert ($):");
			textboxAmount = EsTextboxCreate(panelForm, ES_CELL_H_LEFT);

			// We want to add a convert button in the second column of next row.
			// But the next element we create will go into the first column,
			// so we create a spacer element first.
			EsSpacerCreate(panelForm);

			// Create the convert button.
			EsButton *buttonConvert = EsButtonCreate(
				panelForm,           // Add it to the panelForm. It will go in the second column.
				ES_CELL_H_LEFT       // Align it to the left of the column.
				| ES_BUTTON_DEFAULT, // Set it as the default button. Pressing Enter will invoke it.
				0,                   // Automatically determine the style to use.
				"Convert");          // The button's label.

			// Set the command callback for the button.
			// This is called when the button is invoked. 
			// That might be from the user clicking it,
			// using keyboard input, or an automation API invoking it.
			EsButtonOnCommand(buttonConvert, ConvertCommand);

			// Add a horizontal line below panelForm.
			EsSpacerCreate(
				panelStack,                     // Add it to panelStack.
				ES_CELL_H_FILL,                 // Fill the horizontal width of panelStack.
	 			ES_STYLE_SEPARATOR_HORIZONTAL); // Use the horizontal separator style.
			
			// Add a text display for the conversion result to panelStack.
			displayResult = EsTextDisplayCreate(panelStack, ES_CELL_H_LEFT, ES_STYLE_TEXT_LABEL,
				"Press \u201CConvert\u201D to update the result.");

			// Keep receiving messages in a loop,
			// so the system can handle input messages for the window.
		}
	}
}
