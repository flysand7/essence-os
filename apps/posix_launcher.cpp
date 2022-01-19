// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

#include <essence.h>
#include <shared/strings.cpp>

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>

#define MSG_RECEIVED_OUTPUT ((EsMessageType) (ES_MSG_USER_START + 1))

EsInstance *instance;
EsHandle commandEvent;
char outputBuffer[262144];
uintptr_t outputBufferPosition;
EsMutex mutex;
int stdinWritePipe;
EsTextbox *textboxOutput, *textboxInput;

const EsStyle styleOutputTextbox = {
	.inherit = ES_STYLE_TEXTBOX_NO_BORDER,

	.metrics = {
		.mask = ES_THEME_METRICS_FONT_FAMILY | ES_THEME_METRICS_TEXT_SIZE,
		.textSize = 12,
		.fontFamily = ES_FONT_MONOSPACED,
	},
};

const EsStyle styleInputTextbox = {
	.inherit = ES_STYLE_TEXTBOX_BORDERED_SINGLE,

	.metrics = {
		.mask = ES_THEME_METRICS_FONT_FAMILY | ES_THEME_METRICS_TEXT_SIZE,
		.textSize = 12,
		.fontFamily = ES_FONT_MONOSPACED,
	},
};

const EsStyle styleInputRow = {
	.inherit = ES_STYLE_PANEL_FORM_TABLE,

	.metrics = {
		.mask = ES_THEME_METRICS_INSETS,
		.insets = ES_RECT_1(7),
	},
};

void WriteToOutputTextbox(const char *string, ptrdiff_t stringBytes) {
	if (stringBytes == -1) {
		stringBytes = EsCRTstrlen(string);
	}

	bool done = false, postMessage = false;

	while (true) {
		EsMutexAcquire(&mutex);

		if (outputBufferPosition + stringBytes <= sizeof(outputBuffer)) {
			EsMemoryCopy(outputBuffer + outputBufferPosition, string, stringBytes);
			postMessage = outputBufferPosition == 0;
			outputBufferPosition += stringBytes;
			done = true;
		}

		EsMutexRelease(&mutex);

		if (!done) {
			// The main thread is busy. Wait a little bit before trying again.
			EsSleep(100);
		} else {
			break;
		}
	}

	if (postMessage) {
		EsMessage m = {};
		m.type = MSG_RECEIVED_OUTPUT;
		EsMessagePost(nullptr, &m); 
	}
}

void RunCommandThread() {
	WriteToOutputTextbox("Starting busybox shell...\n", -1);

	char *argv[3] = { (char *) "busybox", (char *) "sh", nullptr };
	char executable[4096];
	int status;
	int standardOutputPipe[2];
	int standardInputPipe[2];
	pid_t pid;

	char *envp[5] = { 
		(char *) "LANG=en_US.UTF-8", 
		(char *) "HOME=/", 
		(char *) "PATH=/Applications/POSIX/bin", 
		(char *) "TMPDIR=/Applications/POSIX/tmp",
		nullptr 
	};

	executable[EsStringFormat(executable, sizeof(executable) - 1, "/Applications/POSIX/bin/%z", argv[0])] = 0;

	pipe(standardOutputPipe);
	pipe(standardInputPipe);

	pid = vfork();

	if (pid == 0) {
		dup2(standardInputPipe[0], 0);
		dup2(standardOutputPipe[1], 1);
		dup2(standardOutputPipe[1], 2);
		close(standardInputPipe[0]);
		close(standardOutputPipe[1]);
		execve(executable, argv, envp);
		WriteToOutputTextbox("\nThe executable failed to load.\n", -1);
		_exit(-1);
	} else if (pid == -1) {
		WriteToOutputTextbox("\nUnable to vfork().\n", -1);
	}

	close(standardInputPipe[0]);
	close(standardOutputPipe[1]);

	EsMutexAcquire(&mutex);
	stdinWritePipe = standardInputPipe[1];
	EsMutexRelease(&mutex);

	while (true) {
		char buffer[1024];
		ssize_t bytesRead = read(standardOutputPipe[0], buffer, 1024);

		if (bytesRead <= 0) {
			break;
		} else {
			WriteToOutputTextbox(buffer, bytesRead);
		}
	}

	EsMutexAcquire(&mutex);
	stdinWritePipe = 0;
	EsMutexRelease(&mutex);

	close(standardInputPipe[1]);
	close(standardOutputPipe[0]);

	wait4(-1, &status, 0, NULL);
}

int ProcessTextboxInputMessage(EsElement *, EsMessage *message) {
	if (message->type == ES_MSG_KEY_DOWN) {
		if (message->keyboard.scancode == ES_SCANCODE_ENTER 
				&& !message->keyboard.modifiers 
				&& EsTextboxGetLineLength(textboxInput)) {
			char *data = EsTextboxGetContents(textboxInput);
			EsTextboxInsert(textboxOutput, data, -1, false);
			EsTextboxInsert(textboxOutput, "\n", -1, false);
			EsMutexAcquire(&mutex);

			if (stdinWritePipe) {
				write(stdinWritePipe, data, EsCStringLength(data));
				write(stdinWritePipe, "\n", 1);
			}

			EsMutexRelease(&mutex);
			EsHeapFree(data);
			EsTextboxClear(textboxInput, false);
			return ES_HANDLED;
		}
	}

	return 0;
}

void MessageLoopThread(EsGeneric) {
	// Cannot access the C standard library on this thread!

	EsMessageMutexAcquire();

	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_INSTANCE_CREATE) {
			EsAssert(!instance);
			instance = EsInstanceCreate(message, "POSIX Launcher");
			EsWindow *window = instance->window;
			EsWindowSetIcon(window, ES_ICON_UTILITIES_TERMINAL);
			EsPanel *panel = EsPanelCreate(window, ES_PANEL_VERTICAL | ES_CELL_FILL, ES_STYLE_PANEL_WINDOW_BACKGROUND);
			textboxOutput = EsTextboxCreate(panel, ES_TEXTBOX_MULTILINE | ES_CELL_FILL, &styleOutputTextbox);
			EsSpacerCreate(panel, ES_CELL_H_FILL, ES_STYLE_SEPARATOR_HORIZONTAL);
			EsPanel *row = EsPanelCreate(panel, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL, &styleInputRow);
			EsTextDisplayCreate(row, ES_FLAGS_DEFAULT, ES_STYLE_TEXT_LABEL, EsLiteral("Input:"));
			textboxInput = EsTextboxCreate(row, ES_CELL_H_FILL, &styleInputTextbox);
			EsTextboxEnableSmartReplacement(textboxInput, false);
			EsTextboxSetReadOnly(textboxOutput, true);
			textboxInput->messageUser = ProcessTextboxInputMessage;
			EsElementFocus(textboxInput);
			EsEventSet(commandEvent); // Ready to receive output.
		} else if (message->type == MSG_RECEIVED_OUTPUT) {
			EsMutexAcquire(&mutex);

			if (outputBufferPosition) {
				EsTextboxMoveCaretRelative(textboxOutput, ES_TEXTBOX_MOVE_CARET_ALL);
				EsTextboxInsert(textboxOutput, outputBuffer, outputBufferPosition, false);
				EsTextboxEnsureCaretVisible(textboxOutput, false);
				outputBufferPosition = 0;
			}

			EsMutexRelease(&mutex);
		}
	}
}

int main(int argc, char **argv) {
	(void) argc;
	(void) argv;

	commandEvent = EsEventCreate(true);
	EsMessageMutexRelease();
	EsThreadCreate(MessageLoopThread, nullptr, 0);
	EsWaitSingle(commandEvent);
	RunCommandThread();

	return 0;
}
