// TODO Don't use EsTextbox for the output..
// TODO Put the connection settings in a Panel.Popup.

#define ES_INSTANCE_TYPE Instance
#include <essence.h>

struct Instance : EsInstance {
	EsTextbox *textboxNick;
	EsTextbox *textboxAddress;
	EsTextbox *textboxPort;
	EsTextbox *textboxOutput;
	EsTextbox *textboxInput;
	EsButton *buttonConnect;

	EsThreadInformation networkingThread;

	EsMutex inputCommandMutex;
	char *inputCommand;
	size_t inputCommandBytes;
};

const EsStyle styleSmallTextbox = {
	.inherit = ES_STYLE_TEXTBOX_BORDERED_SINGLE,

	.metrics = {
		.mask = ES_THEME_METRICS_PREFERRED_WIDTH,
		.preferredWidth = 100,
	},
};

const EsStyle styleOutputTextbox = {
	.inherit = ES_STYLE_TEXTBOX_NO_BORDER,

	.metrics = {
		.mask = ES_THEME_METRICS_FONT_FAMILY,
		.fontFamily = ES_FONT_MONOSPACED,
	},
};

const EsStyle styleInputTextbox = {
	.inherit = ES_STYLE_TEXTBOX_BORDERED_SINGLE,

	.metrics = {
		.mask = ES_THEME_METRICS_FONT_FAMILY,
		.fontFamily = ES_FONT_MONOSPACED,
	},
};

int TextboxInputCallback(EsElement *element, EsMessage *message) {
	Instance *instance = element->instance;

	if (message->type == ES_MSG_KEY_DOWN) {
		if (message->keyboard.scancode == ES_SCANCODE_ENTER) {
			size_t inputCommandBytes = 0;
			char *inputCommand = EsTextboxGetContents(instance->textboxInput, &inputCommandBytes);

			if (inputCommandBytes) {
				EsMutexAcquire(&instance->inputCommandMutex);
				EsHeapFree(instance->inputCommand);
				instance->inputCommand = inputCommand;
				instance->inputCommandBytes = inputCommandBytes;
				EsMutexRelease(&instance->inputCommandMutex);
			} else {
				EsHeapFree(inputCommand);
			}

			EsTextboxClear(instance->textboxInput, false);
			return ES_HANDLED;
		}
	}

	return 0;
}

void NetworkingThread(EsGeneric argument) {
	Instance *instance = (Instance *) argument.p;

	char errorMessage[4096];
	size_t errorMessageBytes = 0;

	char message[4096];
	size_t messageBytes = 0;

	char nick[64];
	size_t nickBytes = 0;

	char *password = nullptr;
	size_t passwordBytes = 0;

	char *address = nullptr;
	size_t addressBytes = 0;

	char buffer[1024];
	uintptr_t bufferPosition = 0;
	
	EsConnection connection = {};
	connection.sendBufferBytes = 65536;
	connection.receiveBufferBytes = 65536;

	{
		EsMessageMutexAcquire();
		address = EsTextboxGetContents(instance->textboxAddress, &addressBytes);
		EsMessageMutexRelease();
		EsError error = EsAddressResolve(address, addressBytes, ES_FLAGS_DEFAULT, &connection.address);

		if (error != ES_SUCCESS) {
			errorMessageBytes = EsStringFormat(errorMessage, sizeof(errorMessage), 
					"The address name '%s' could not be found.\n", addressBytes, address);
			goto exit;
		}
	}

	{
		EsMessageMutexAcquire();
		size_t portBytes;
		char *port = EsTextboxGetContents(instance->textboxPort, &portBytes);
		EsMessageMutexRelease();
		connection.address.port = EsIntegerParse(port, portBytes); 
		EsHeapFree(port);
	}

	{
		EsMessageMutexAcquire();
		char *_nick = EsTextboxGetContents(instance->textboxNick, &nickBytes);
		EsMessageMutexRelease();
		if (nickBytes > sizeof(nick)) nickBytes = sizeof(nick);
		EsMemoryCopy(nick, _nick, nickBytes);
		EsHeapFree(_nick);

		for (uintptr_t i = 0; i < nickBytes; i++) {
			if (nick[i] == ':') {
				password = nick + i + 1;
				passwordBytes = nickBytes - i - 1;
				nickBytes = i;
			}
		}
	}

	{
		EsError error = EsConnectionOpen(&connection, ES_CONNECTION_OPEN_WAIT);

		if (error != ES_SUCCESS) {
			errorMessageBytes = EsStringFormat(errorMessage, sizeof(errorMessage), 
					"Could not open the connection (%d).", error);
			goto exit;
		}

		messageBytes = EsStringFormat(message, sizeof(message), "%z%s%zNICK %s\r\nUSER %s localhost %s :%s\r\n",
				password ? "PASS " : "", passwordBytes, password, password ? "\r\n" : "",
				nickBytes, nick, nickBytes, nick, addressBytes, address, nickBytes, nick);
		EsConnectionWriteSync(&connection, message, messageBytes);
	}

	while (true) {
		// TODO Ping the server every 2 minutes.
		// TODO If we've received no messages for 5 minutes, timeout.

		uintptr_t inputBytes = 0;

		while (true) {
			char *inputCommand = nullptr;
			size_t inputCommandBytes = 0;

			EsMutexAcquire(&instance->inputCommandMutex);
			inputCommand = instance->inputCommand;
			inputCommandBytes = instance->inputCommandBytes;
			instance->inputCommand = nullptr;
			instance->inputCommandBytes = 0;
			EsMutexRelease(&instance->inputCommandMutex);

			if (inputCommand) {
				messageBytes = EsStringFormat(message, sizeof(message), "%s\r\n", inputCommandBytes, inputCommand);
				EsConnectionWriteSync(&connection, message, messageBytes);
				EsHeapFree(inputCommand);
			}

			size_t bytesRead;
			EsError error = EsConnectionRead(&connection, buffer + bufferPosition, sizeof(buffer) - bufferPosition, &bytesRead);

			if (error != ES_SUCCESS) {
				errorMessageBytes = EsStringFormat(errorMessage, sizeof(errorMessage), "The connection was lost (%d).", error);
				goto exit;
			}

			bufferPosition += bytesRead;

			if (bufferPosition >= 2) {
				for (uintptr_t i = 0; i < bufferPosition - 1; i++) {
					if (buffer[i] == '\r' && buffer[i + 1] == '\n') {
						buffer[i] = 0;
						inputBytes = i + 2;
						goto gotMessage;
					}
				}
			}

			if (bufferPosition == sizeof(buffer)) {
				errorMessageBytes = EsStringFormat(errorMessage, sizeof(errorMessage), "The server sent an invalid message.");
				goto exit;
			}
		}

		gotMessage:;

		EsMessageMutexAcquire();
		EsTextboxInsert(instance->textboxOutput, buffer);
		EsTextboxInsert(instance->textboxOutput, "\n");
		EsMessageMutexRelease();

		{
			EsPrint("=================\n%z\n", buffer);

			const char *command = nullptr, *user = nullptr, *parameters = nullptr, *text = nullptr;
			char *position = buffer;

			if (*position == ':') {
				user = ++position;
				while (*position && *position != ' ') position++;
				if (*position) *position++ = 0;
			} else {
				user = address;
			}

			while (*position && *position == ' ') position++;
			command = position;
			while (*position && *position != ' ') position++;
			if (*position) *position++ = 0;
			while (*position && *position == ' ') position++;

			if (*position != ':') {
				parameters = position;
				while (*position && *position != ' ') position++;
				if (*position) *position++ = 0;
			}

			while (*position && *position == ' ') position++;
			if (*position == ':') text = position + 1;

			if (0 == EsCRTstrcmp(command, "PING")) {
				messageBytes = EsStringFormat(message, sizeof(message), "PONG :%z\r\n", text ?: parameters);
				EsConnectionWriteSync(&connection, message, messageBytes);
			}

			EsPrint("command: '%z'\nuser: '%z'\nparameters: '%z'\ntext: '%z'\n\n", command, user, parameters, text);
		}

		EsMemoryMove(buffer + inputBytes, buffer + bufferPosition, -inputBytes, false);
		bufferPosition -= inputBytes;
	}

	exit:;

	if (connection.handle) {
		EsConnectionClose(&connection);
	}

	EsMessageMutexAcquire();

	if (errorMessageBytes) {
		EsDialogShow(instance->window, EsLiteral("Connection failed"), errorMessage, errorMessageBytes, 
				ES_ICON_DIALOG_ERROR, ES_DIALOG_ALERT_OK_BUTTON); 
	}

	EsElementSetDisabled(instance->textboxAddress, false);
	EsElementSetDisabled(instance->textboxNick, false);
	EsElementSetDisabled(instance->textboxPort, false);
	EsElementSetDisabled(instance->textboxInput, true);
	EsElementSetDisabled(instance->buttonConnect, false);

	EsMessageMutexRelease();

	EsHeapFree(address);
}

void ConnectCommand(Instance *instance, EsElement *, EsCommand *) {
	EsElementSetDisabled(instance->textboxAddress, true);
	EsElementSetDisabled(instance->textboxNick, true);
	EsElementSetDisabled(instance->textboxPort, true);
	EsElementSetDisabled(instance->textboxInput, false);
	EsElementSetDisabled(instance->buttonConnect, true);

	EsThreadCreate(NetworkingThread, &instance->networkingThread, instance);
}

void _start() {
	_init();

	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_INSTANCE_CREATE) {
			// Create an new instance.

			Instance *instance = EsInstanceCreate(message, "IRC Client");
			EsWindow *window = instance->window;
			EsWindowSetIcon(window, ES_ICON_INTERNET_CHAT);

			// Create the toolbar.

			EsElement *toolbar = EsWindowGetToolbar(window);
			EsPanel *section = EsPanelCreate(toolbar, ES_PANEL_HORIZONTAL);
			EsTextDisplayCreate(section, ES_FLAGS_DEFAULT, 0, EsLiteral("Nick:"));
			instance->textboxNick = EsTextboxCreate(section, ES_FLAGS_DEFAULT, &styleSmallTextbox);
			EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, 5, 0);
			section = EsPanelCreate(toolbar, ES_PANEL_HORIZONTAL);
			EsTextDisplayCreate(section, ES_FLAGS_DEFAULT, 0, EsLiteral("Address:"));
			instance->textboxAddress = EsTextboxCreate(section, ES_FLAGS_DEFAULT, &styleSmallTextbox);
			EsSpacerCreate(toolbar, ES_FLAGS_DEFAULT, nullptr, 5, 0);
			section = EsPanelCreate(toolbar, ES_PANEL_HORIZONTAL);
			EsTextDisplayCreate(section, ES_FLAGS_DEFAULT, 0, EsLiteral("Port:"));
			instance->textboxPort = EsTextboxCreate(section, ES_FLAGS_DEFAULT, &styleSmallTextbox);
			EsSpacerCreate(toolbar, ES_CELL_H_FILL);
			instance->buttonConnect = EsButtonCreate(toolbar, ES_FLAGS_DEFAULT, 0, EsLiteral("Connect"));
			EsButtonOnCommand(instance->buttonConnect, ConnectCommand);

			// Create the main area.

			EsPanel *panel = EsPanelCreate(window, ES_PANEL_VERTICAL | ES_CELL_FILL, ES_STYLE_PANEL_WINDOW_DIVIDER);
			instance->textboxOutput = EsTextboxCreate(panel, ES_CELL_FILL | ES_TEXTBOX_MULTILINE, &styleOutputTextbox);
			EsPanelCreate(panel, ES_CELL_H_FILL, ES_STYLE_SEPARATOR_HORIZONTAL);
			EsPanel *inputArea = EsPanelCreate(panel, ES_PANEL_HORIZONTAL | ES_CELL_H_FILL, ES_STYLE_PANEL_FILLED);
			instance->textboxInput = EsTextboxCreate(inputArea, ES_CELL_FILL, &styleInputTextbox);
			instance->textboxInput->messageUser = TextboxInputCallback;
			EsElementSetDisabled(instance->textboxInput);
		}
	}
}
