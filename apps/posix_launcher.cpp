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
volatile bool runningCommand;
int stdinWritePipe;
char *command;
EsTextbox *textboxOutput, *textboxInput;

const EsStyle styleMonospacedTextbox = {
	.inherit = ES_STYLE_TEXTBOX_NO_BORDER,

	.metrics = {
		.mask = ES_THEME_METRICS_FONT_FAMILY | ES_THEME_METRICS_TEXT_SIZE,
		.textSize = 12,
		.fontFamily = ES_FONT_MONOSPACED,
	},
};

char *ParseArgument(char **position) {
	char *start = *position;

	while (*start == ' ') {
		start++;
	}

	if (!(*start)) {
		return nullptr;
	}

	char *end = start;

	while ((*end != ' ' || (end != start && end[-1] == '\\')) && *end) {
		end++;
	}

	if (*end) {
		*end = 0;
		end++;
	}

	*position = end;
	return start;
}

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
	char *argv[64];
	int argc = 0;
	char executable[4096];
	int status;
	int standardOutputPipe[2];
	int standardInputPipe[2];
	pid_t pid;
	struct timespec startTime, endTime;

	char *envp[5] = { 
		(char *) "LANG=en_US.UTF-8", 
		(char *) "HOME=/", 
		(char *) "PATH=/Applications/POSIX/bin", 
		(char *) "TMPDIR=/Applications/POSIX/tmp",
		nullptr 
	};

	char *commandPosition = command;

	while (argc < 63) {
		argv[argc] = ParseArgument(&commandPosition);
		if (!argv[argc]) break;
		argc++;
	}

	if (!argc) {
		goto done;
	}

	argv[argc] = nullptr;

	if (0 == EsCRTstrcmp(argv[0], "run")) {
		if (argc != 2) {
			WriteToOutputTextbox("\nUsage: run <absolute path to esx>\n", -1);
		} else {
			EsApplicationRunTemporary(instance, argv[1], EsCStringLength(argv[1]));
		}

		WriteToOutputTextbox("\n----------------\n", -1);
		goto done;
	} else if (0 == EsCRTstrcmp(argv[0], "cd")) {
		if (argc != 2) {
			WriteToOutputTextbox("\nUsage: cd <path>\n", -1);
		} else {
			chdir(argv[1]);

			WriteToOutputTextbox("\nNew working directory:\n", -1);
			WriteToOutputTextbox(getcwd(nullptr, 0), -1);
			WriteToOutputTextbox("\n", -1);
		}

		WriteToOutputTextbox("\n----------------\n", -1);
		goto done;
	}

	if (argv[0][0] == '/') {
		executable[EsStringFormat(executable, sizeof(executable) - 1, "%z", argv[0])] = 0;
	} else {
		executable[EsStringFormat(executable, sizeof(executable) - 1, "/Applications/POSIX/bin/%z", argv[0])] = 0;
	}

	clock_gettime(CLOCK_MONOTONIC, &startTime);

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
		// TODO Report the error.
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

	clock_gettime(CLOCK_MONOTONIC, &endTime);

	{
		double startTimeS = startTime.tv_sec + startTime.tv_nsec / 1000000000.0;
		double endTimeS = endTime.tv_sec + endTime.tv_nsec / 1000000000.0;
		char buffer[256];
		size_t bytes = EsStringFormat(buffer, sizeof(buffer), "\nProcess exited with status %d.\nExecution time: %Fs.\n", status >> 8, endTimeS - startTimeS);
		WriteToOutputTextbox(buffer, bytes);
		WriteToOutputTextbox("\n----------------\n", -1);
	}

	done:;
	EsHeapFree(command);
	__sync_synchronize();
	runningCommand = false;
}

int ProcessTextboxInputMessage(EsElement *, EsMessage *message) {
	if (message->type == ES_MSG_KEY_DOWN) {
		if (message->keyboard.scancode == ES_SCANCODE_ENTER 
				&& !message->keyboard.modifiers 
				&& EsTextboxGetLineLength(textboxInput)) {
			char *data = EsTextboxGetContents(textboxInput);

			if (!runningCommand) {
				runningCommand = true;
				command = data;

				EsTextboxInsert(textboxOutput, "\n> ", -1, false);
				EsTextboxInsert(textboxOutput, command, -1, false);
				EsTextboxInsert(textboxOutput, "\n", -1, false);
				EsTextboxEnsureCaretVisible(textboxOutput, false);

				EsEventSet(commandEvent);
			} else {
				EsTextboxInsert(textboxOutput, data, -1, false);
				EsTextboxInsert(textboxOutput, "\n", -1, false);
				EsMutexAcquire(&mutex);

				if (stdinWritePipe) {
					write(stdinWritePipe, data, EsCStringLength(data));
					write(stdinWritePipe, "\n", 1);
				}

				EsMutexRelease(&mutex);
				EsHeapFree(data);
			}

			EsTextboxClear(textboxInput, false);
			return ES_HANDLED;
		}
	}

	return 0;
}

void MessageLoopThread(EsGeneric) {
	EsMessageMutexAcquire();

	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_INSTANCE_CREATE) {
			EsAssert(!instance);
			instance = EsInstanceCreate(message, "POSIX Launcher");
			EsWindow *window = instance->window;
			EsWindowSetIcon(window, ES_ICON_UTILITIES_TERMINAL);
			EsPanel *panel = EsPanelCreate(window, ES_PANEL_VERTICAL | ES_CELL_FILL, ES_STYLE_PANEL_WINDOW_BACKGROUND);
			textboxOutput = EsTextboxCreate(panel, ES_TEXTBOX_MULTILINE | ES_CELL_FILL, &styleMonospacedTextbox);
			EsSpacerCreate(panel, ES_CELL_H_FILL, ES_STYLE_SEPARATOR_HORIZONTAL);
			textboxInput = EsTextboxCreate(panel, ES_CELL_H_FILL, &styleMonospacedTextbox);
			textboxInput->messageUser = ProcessTextboxInputMessage;
			EsElementFocus(textboxInput);
		} else if (message->type == MSG_RECEIVED_OUTPUT) {
			EsMutexAcquire(&mutex);

			if (outputBufferPosition) {
				// EsPrint("Inserting %d bytes...\n", outputBufferPosition);
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

#if 0
	runningCommand = true;
	command = (char *) EsHeapAllocate(128, true);
	EsStringFormat(command, 128, "gcc es/util/build_core.c -g");
	EsEventSet(commandEvent);
#endif

	while (true) {
		EsWaitSingle(commandEvent);
		RunCommandThread();
	}

	return 0;
}
