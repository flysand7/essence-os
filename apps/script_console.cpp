#define ES_INSTANCE_TYPE Instance
#include <essence.h>
#include <shared/array.cpp>

// TODO REPL variables should be dynamically typed.
// 	- When setting a variable, show the new value and give a link to scroll to the previous value (if it wasn't cleared).

struct Instance : EsInstance {
	EsCommand commandClearOutput;

	EsThreadInformation scriptThread;
	char *inputText;
	size_t inputBytes;

	EsPanel *root;
	EsPanel *inputRow;
	EsPanel *outputPanel;
	EsElement *logOutputGroup;
	uintptr_t logGroupNestLevel;
	EsSpacer *outputDecoration;
	EsTextbox *inputTextbox;

	char *outputLineBuffer;
	size_t outputLineBufferBytes;
	size_t outputLineBufferAllocated;
	bool anyOutput;
	bool gotREPLResult;

	Array<EsElement *> outputElements;
};

EsFileInformation globalCommandHistoryFile;

void AddPrompt(Instance *instance);
void AddOutput(Instance *instance, const char *text, size_t textBytes);
void AddLogOutput(Instance *instance, const char *text, size_t textBytes);
void AddLogOpenGroup(Instance *instance, const char *title, size_t titleBytes);
void AddLogClose(Instance *instance);

// --------------------------------- Script engine interface.

#define Assert EsAssert
#include <util/script.c>

void **fixedAllocationBlocks;
uint8_t *fixedAllocationCurrentBlock;
uintptr_t fixedAllocationCurrentPosition;
size_t fixedAllocationCurrentSize;

EsDirectoryChild *directoryIterationBuffer;
uintptr_t directoryIterationBufferPosition;
size_t directoryIterationBufferCount;

Instance *scriptInstance;

void *AllocateFixed(size_t bytes) {
	if (!bytes) {
		return nullptr;
	}

	bytes = (bytes + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1);

	if (bytes >= fixedAllocationCurrentSize || fixedAllocationCurrentPosition >= fixedAllocationCurrentSize - bytes) {
		fixedAllocationCurrentSize = bytes > 1048576 ? bytes : 1048576;
		fixedAllocationCurrentPosition = 0;
		fixedAllocationCurrentBlock = (uint8_t *) EsCRTcalloc(1, fixedAllocationCurrentSize + sizeof(void *));

		if (!fixedAllocationCurrentBlock) {
			// TODO.
			EsAssert(false);
		}

		*(void **) fixedAllocationCurrentBlock = fixedAllocationBlocks;
		fixedAllocationBlocks = (void **) fixedAllocationCurrentBlock;
		fixedAllocationCurrentBlock += sizeof(void *);
	}

	void *p = fixedAllocationCurrentBlock + fixedAllocationCurrentPosition;
	fixedAllocationCurrentPosition += bytes;
	return p;
}

void *AllocateResize(void *old, size_t bytes) {
	return EsCRTrealloc(old, bytes);
}

int MemoryCompare(const void *a, const void *b, size_t bytes) {
	return EsCRTmemcmp(a, b, bytes);
}

void MemoryCopy(void *a, const void *b, size_t bytes) {
	EsMemoryCopy(a, b, bytes);
}

size_t PrintIntegerToBuffer(char *buffer, size_t bufferBytes, int64_t i) {
	EsCRTsnprintf(buffer, bufferBytes, "%ld", i);
	return EsCRTstrlen(buffer);
}

size_t PrintFloatToBuffer(char *buffer, size_t bufferBytes, double f) {
	EsCRTsnprintf(buffer, bufferBytes, "%f", f);
	return EsCRTstrlen(buffer);
}

void PrintDebug(const char *format, ...) {
	va_list arguments;
	va_start(arguments, format);
	char buffer[256];
	EsCRTvsnprintf(buffer, sizeof(buffer), format, arguments);
	AddOutput(scriptInstance, buffer, EsCStringLength(buffer)); 
	va_end(arguments);
}

void PrintError(Tokenizer *tokenizer, const char *format, ...) {
	PrintDebug("Error on line %d of '%s':\n", (int) tokenizer->line, tokenizer->module->path);
	va_list arguments;
	va_start(arguments, format);
	char buffer[256];
	EsCRTvsnprintf(buffer, sizeof(buffer), format, arguments);
	AddOutput(scriptInstance, buffer, EsCStringLength(buffer)); 
	va_end(arguments);
	PrintLine(tokenizer->module, tokenizer->line);
}

void PrintError2(Tokenizer *tokenizer, Node *node, const char *format, ...) {
	PrintDebug("Error on line %d of '%s':\n", (int) node->token.line, tokenizer->module->path);
	va_list arguments;
	va_start(arguments, format);
	char buffer[256];
	EsCRTvsnprintf(buffer, sizeof(buffer), format, arguments);
	AddOutput(scriptInstance, buffer, EsCStringLength(buffer)); 
	va_end(arguments);
	PrintLine(tokenizer->module, node->token.line);
}

void PrintError3(const char *format, ...) {
	PrintDebug("General error:\n");
	va_list arguments;
	va_start(arguments, format);
	char buffer[256];
	EsCRTvsnprintf(buffer, sizeof(buffer), format, arguments);
	AddOutput(scriptInstance, buffer, EsCStringLength(buffer)); 
	va_end(arguments);
}

void PrintError4(ExecutionContext *context, uint32_t instructionPointer, const char *format, ...) {
	LineNumber lineNumber = { 0 };
	LineNumberLookup(context, instructionPointer, &lineNumber);
	PrintDebug("Runtime error on line %d of '%s':\n", (int) lineNumber.lineNumber, 
			lineNumber.importData ? lineNumber.importData->path : "??");
	va_list arguments;
	va_start(arguments, format);
	char buffer[256];
	EsCRTvsnprintf(buffer, sizeof(buffer), format, arguments);
	AddOutput(scriptInstance, buffer, EsCStringLength(buffer)); 
	PrintLine(lineNumber.importData, lineNumber.lineNumber);
	PrintDebug("Back trace:\n");
	PrintBackTrace(context, instructionPointer, context->c, "");
	va_end(arguments);
}

void *FileLoad(const char *path, size_t *length) {
	return EsFileReadAll(path, -1, length);
}

#define RETURN_ERROR(error) do { MakeError(context, returnValue, error); return EXTCALL_RETURN_ERR_ERROR; } while (0)

void MakeError(ExecutionContext *context, Value *returnValue, EsError error) {
	const char *text = nullptr;

	if (error == ES_ERROR_OPERATION_BLOCKED) text = "OPERATION_BLOCKED";
	if (error == ES_ERROR_ACCESS_NOT_WITHIN_FILE_BOUNDS) text = "ACCESS_NOT_WITHIN_FILE_BOUNDS";
	if (error == ES_ERROR_DIRECTORY_NOT_EMPTY) text = "DIRECTORY_NOT_EMPTY";
	if (error == ES_ERROR_NODE_DELETED) text = "NODE_DELETED";
	if (error == ES_ERROR_FILE_TOO_LARGE) text = "FILE_TOO_LARGE";
	if (error == ES_ERROR_DRIVE_FULL) text = "DRIVE_FULL";
	if (error == ES_ERROR_CORRUPT_DATA) text = "CORRUPT_DATA";
	if (error == ES_ERROR_INVALID_NAME) text = "INVALID_NAME";
	if (error == ES_ERROR_FILE_ON_READ_ONLY_VOLUME) text = "FILE_ON_READ_ONLY_VOLUME";
	if (error == ES_ERROR_PATH_NOT_WITHIN_MOUNTED_VOLUME) text = "PATH_NOT_WITHIN_MOUNTED_VOLUME";
	if (error == ES_ERROR_PATH_NOT_TRAVERSABLE) text = "PATH_NOT_TRAVERSABLE";
	if (error == ES_ERROR_DEVICE_REMOVED) text = "DEVICE_REMOVED";
	if (error == ES_ERROR_INCORRECT_NODE_TYPE) text = "INCORRECT_NODE_TYPE";
	if (error == ES_ERROR_FILE_DOES_NOT_EXIST) text = "FILE_DOES_NOT_EXIST";
	if (error == ES_ERROR_VOLUME_MISMATCH) text = "VOLUME_MISMATCH";
	if (error == ES_ERROR_TARGET_WITHIN_SOURCE) text = "TARGET_WITHIN_SOURCE";
	if (error == ES_ERROR_UNKNOWN) text = "UNKNOWN";
	if (error == ES_ERROR_ALREADY_EXISTS) text = "ALREADY_EXISTS";
	if (error == ES_ERROR_CANCELLED) text = "CANCELLED";
	if (error == ES_ERROR_INSUFFICIENT_RESOURCES) text = "INSUFFICIENT_RESOURCES";
	if (error == ES_ERROR_PERMISSION_NOT_GRANTED) text = "PERMISSION_NOT_GRANTED";
	if (error == ES_ERROR_UNSUPPORTED) text = "UNSUPPORTED";
	if (error == ES_ERROR_HARDWARE_FAILURE) text = "HARDWARE_FAILURE";

	if (text) {
		RETURN_STRING_COPY(text, EsCStringLength(text));
	} else {
		returnValue->i = 0;
	}
}

CoroutineState *ExternalCoroutineWaitAny(ExecutionContext *context) {
	(void) context;
	EsAssert(false); // TODO.
	return nullptr;
}

int ExternalLog(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	STACK_POP_STRING(entryText, entryBytes);
	AddLogOutput(scriptInstance, entryText, entryBytes);
	return EXTCALL_NO_RETURN;
}

int ExternalLogOpenGroup(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	STACK_POP_STRING(entryText, entryBytes);
	AddLogOpenGroup(scriptInstance, entryText, entryBytes);
	return EXTCALL_NO_RETURN;
}

int ExternalLogClose(ExecutionContext *context, Value *returnValue) {
	(void) context;
	(void) returnValue;
	AddLogClose(scriptInstance);
	return EXTCALL_NO_RETURN;
}

int ExternalTextFormat(ExecutionContext *context, Value *returnValue, const char *mode) {
	char *buffer = (char *) EsHeapAllocate(16, false);
	uintptr_t index = HeapAllocate(context);
	context->heap[index].type = T_STR;
	context->heap[index].bytes = EsStringFormat(buffer, 16, "%z", mode);
	context->heap[index].text = buffer;
	returnValue->i = index;
	return EXTCALL_RETURN_MANAGED;
}

int ExternalTextColorError    (ExecutionContext *context, Value *returnValue) { return ExternalTextFormat(context, returnValue, "\a#DB002A]"); }
int ExternalTextColorHighlight(ExecutionContext *context, Value *returnValue) { return ExternalTextFormat(context, returnValue, "\a#362CB6]"); }
int ExternalTextMonospaced    (ExecutionContext *context, Value *returnValue) { return ExternalTextFormat(context, returnValue,       "\am]"); }
int ExternalTextPlain         (ExecutionContext *context, Value *returnValue) { return ExternalTextFormat(context, returnValue,        "\a]"); }

int ExternalTextWeight(ExecutionContext *context, Value *returnValue) { 
	if (context->c->stackPointer < 1) return -1;
	int i = context->c->stack[--context->c->stackPointer].i / 100;
	if (i < 1) i = 1; else if (i > 9) i = 9;
	char b[5] = { '\a', 'w', (char) (i + '0'), ']', 0 };
	return ExternalTextFormat(context, returnValue, b); 
}

int ExternalSystemSleepMs(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	if (context->c->stackPointer < 1) return -1;
	int64_t ms = context->c->stack[--context->c->stackPointer].i;
	if (ms > 0) EsSleep(ms); 
	return EXTCALL_NO_RETURN;
}

int ExternalSystemGetHostName(ExecutionContext *context, Value *returnValue) {
	(void) context;
	RETURN_STRING_COPY("Essence", 7);
	return EXTCALL_RETURN_MANAGED;
}

int ExternalSystemRunningAsAdministrator(ExecutionContext *context, Value *returnValue) {
	(void) context;
	returnValue->i = 0;
	return EXTCALL_RETURN_UNMANAGED;
}

int ExternalSystemGetProcessorCount(ExecutionContext *context, Value *returnValue) {
	(void) context;
	returnValue->i = EsSystemGetOptimalWorkQueueThreadCount();
	return EXTCALL_RETURN_UNMANAGED;
}

int ExternalRandomInt(ExecutionContext *context, Value *returnValue) {
	if (context->c->stackPointer < 2) return -1;
	int64_t min = context->c->stack[context->c->stackPointer - 1].i;
	int64_t max = context->c->stack[context->c->stackPointer - 2].i;
	if (max < min) { PrintError4(context, 0, "RandomInt() called with maximum limit (%ld) less than the minimum limit (%ld).\n", max, min); return 0; }
	returnValue->i = EsRandomU64() % (max - min + 1) + min;
	context->c->stackPointer -= 2;
	return EXTCALL_RETURN_UNMANAGED;
}

int ExternalPathCreateDirectory(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING(entryText, entryBytes);
	EsError error = EsPathCreate(entryText, entryBytes, ES_NODE_DIRECTORY, false);
	if (error == ES_SUCCESS || error == ES_ERROR_ALREADY_EXISTS) return EXTCALL_RETURN_ERR_UNMANAGED;
	RETURN_ERROR(error);
}

int ExternalPathExists(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	STACK_POP_STRING(entryText, entryBytes);
	returnValue->i = EsPathExists(entryText, entryBytes) ? 1 : 0;
	return EXTCALL_RETURN_UNMANAGED;
}

int ExternalPathIsFile(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	STACK_POP_STRING(entryText, entryBytes);
	EsNodeType type;
	returnValue->i = EsPathExists(entryText, entryBytes, &type) ? 1 : 0;
	if (type != ES_NODE_FILE) returnValue->i = 0;
	return EXTCALL_RETURN_UNMANAGED;
}

int ExternalPathIsDirectory(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	STACK_POP_STRING(entryText, entryBytes);
	EsNodeType type;
	returnValue->i = EsPathExists(entryText, entryBytes, &type) ? 1 : 0;
	if (type != ES_NODE_DIRECTORY) returnValue->i = 0;
	return EXTCALL_RETURN_UNMANAGED;
}

int ExternalPathIsLink(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	STACK_POP_STRING(entryText, entryBytes);
	returnValue->i = 0;
	return EXTCALL_RETURN_UNMANAGED;
}

int ExternalPathMove(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING_2(entryText, entryBytes, entry2Text, entry2Bytes);
	EsError error = EsPathMove(entryText, entryBytes, entry2Text, entry2Bytes);
	if (error != ES_SUCCESS) RETURN_ERROR(error);
	return EXTCALL_RETURN_ERR_UNMANAGED;
}

int ExternalPathDelete(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	STACK_POP_STRING(entryText, entryBytes);
	returnValue->i = EsPathDelete(entryText, entryBytes) == ES_SUCCESS;
	return EXTCALL_RETURN_UNMANAGED;
}

int ExternalFileReadAll(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING(entryText, entryBytes);
	returnValue->i = 0;
	size_t length = 0;
	EsError error;
	void *data = EsFileReadAll(entryText, entryBytes, &length, &error); // Free with EsHeapFree.
	if (!data) RETURN_ERROR(error);
	RETURN_STRING_NO_COPY((char *) data, length);
	return EXTCALL_RETURN_ERR_MANAGED;
}

int ExternalFileWriteAll(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING_2(entryText, entryBytes, entry2Text, entry2Bytes);
	EsError error = EsFileWriteAll(entryText, entryBytes, entry2Text, entry2Bytes);
	if (error != ES_SUCCESS) RETURN_ERROR(error);
	return EXTCALL_RETURN_ERR_UNMANAGED;
}

int ExternalFileAppend(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING_2(entryText, entryBytes, entry2Text, entry2Bytes);
	returnValue->i = 0;
	EsFileInformation information = EsFileOpen(entryText, entryBytes, ES_FILE_WRITE);

	if (information.error == ES_SUCCESS) {
		EsError error = EsFileWriteSync(information.handle, information.size, entry2Bytes, entry2Text);
		EsHandleClose(information.handle);

		if (ES_CHECK_ERROR(error)) {
			RETURN_ERROR(error);
		}
	} else {
		RETURN_ERROR(information.error);
	}

	return EXTCALL_RETURN_ERR_UNMANAGED;
}

int ExternalFileGetSize(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING(entryText, entryBytes);
	EsDirectoryChild information;
	bool exists = EsPathQueryInformation(entryText, entryBytes, &information);
	if (!exists) RETURN_ERROR(ES_ERROR_FILE_DOES_NOT_EXIST);
	if (information.type != ES_NODE_FILE) RETURN_ERROR(ES_ERROR_INCORRECT_NODE_TYPE);
	returnValue->i = information.fileSize;
	return EXTCALL_RETURN_ERR_UNMANAGED;
}

int ExternalFileCopy(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING_2(entryText, entryBytes, entry2Text, entry2Bytes);
	EsError error = EsFileCopy(entryText, entryBytes, entry2Text, entry2Bytes);
	if (error != ES_SUCCESS) RETURN_ERROR(error);
	return EXTCALL_RETURN_ERR_UNMANAGED;
}

int External_DirectoryInternalStartIteration(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING(entryText, entryBytes);
	EsHeapFree(directoryIterationBuffer);
	EsError error;
	directoryIterationBuffer = EsDirectoryEnumerateChildren(entryText, entryBytes, &directoryIterationBufferCount, &error);
	directoryIterationBufferPosition = 0;
	if (error == ES_SUCCESS) return EXTCALL_RETURN_ERR_UNMANAGED;
	RETURN_ERROR(error);
}

int External_DirectoryInternalEndIteration(ExecutionContext *context, Value *returnValue) {
	(void) context;
	(void) returnValue;
	EsHeapFree(directoryIterationBuffer);
	directoryIterationBuffer = nullptr;
	directoryIterationBufferPosition = 0;
	directoryIterationBufferCount = 0;
	return EXTCALL_NO_RETURN;
}

int External_DirectoryInternalNextIteration(ExecutionContext *context, Value *returnValue) {
	(void) context;

	if (directoryIterationBufferPosition == directoryIterationBufferCount) {
		returnValue->i = 0;
	} else { 
		RETURN_STRING_COPY(directoryIterationBuffer[directoryIterationBufferPosition].name, 
				directoryIterationBuffer[directoryIterationBufferPosition].nameBytes); 
		directoryIterationBufferPosition++;
	}

	return EXTCALL_RETURN_MANAGED;
}

int ExternalOpenDocumentEnumerate(ExecutionContext *context, Value *returnValue) {
	EsBuffer buffer = { .canGrow = true };
	_EsOpenDocumentEnumerate(&buffer);

	size_t count = 0;
	EsBufferReadInto(&buffer, &count, sizeof(size_t));

	uintptr_t index = HeapAllocate(context);
	context->heap[index].type = T_LIST;
	context->heap[index].internalValuesAreManaged = true;
	context->heap[index].length = count;
	context->heap[index].list = (Value *) EsHeapAllocate(sizeof(Value) * count, false);

	for (uintptr_t i = 0; i < count; i++) {
		size_t pathBytes = 0;
		EsBufferReadInto(&buffer, &pathBytes, sizeof(size_t));
		char *path = (char *) EsHeapAllocate(pathBytes, false);
		EsBufferReadInto(&buffer, path, pathBytes);
		context->heap[index].list[i].i = HeapAllocate(context);
		context->heap[context->heap[index].list[i].i].type = T_STR;
		context->heap[context->heap[index].list[i].i].bytes = pathBytes;
		context->heap[context->heap[index].list[i].i].text = path;
	}

	EsHeapFree(buffer.out);
	EsAssert(!buffer.error);
	returnValue->i = index;

	return EXTCALL_RETURN_MANAGED;
}

#define EXTERNAL_STUB(name) int name(ExecutionContext *, Value *) { EsPrint("Unimplemented " #name "\n"); EsAssert(false); return -1; }

// TODO What to do with these?
EXTERNAL_STUB(ExternalPathGetDefaultPrefix);
EXTERNAL_STUB(ExternalPathSetDefaultPrefixToScriptSourceDirectory);

// TODO These functions should be moved to Helpers.
EXTERNAL_STUB(ExternalPersistRead);
EXTERNAL_STUB(ExternalPersistWrite);

// TODO Functions only available in the POSIX subsystem:
EXTERNAL_STUB(ExternalConsoleGetLine);
EXTERNAL_STUB(ExternalSystemShellExecute);
EXTERNAL_STUB(ExternalSystemShellExecuteWithWorkingDirectory);
EXTERNAL_STUB(ExternalSystemShellEvaluate);
EXTERNAL_STUB(ExternalSystemShellEnableLogging);
EXTERNAL_STUB(ExternalSystemGetEnvironmentVariable);
EXTERNAL_STUB(ExternalSystemSetEnvironmentVariable);

void *LibraryLoad(const char *cName) {
	// TODO.
	(void) cName;
	AddLogOutput(scriptInstance, EsLiteral("(LibraryLoad has not been implemented yet)"));
	return nullptr;
}

void *LibraryGetAddress(void *library, const char *cName) {
	// TODO.
	(void) library;
	(void) cName;
	return nullptr;
}

// --------------------------------- User interface.

#define COLOR_BACKGROUND (0xFFFDFDFD)
#define COLOR_ROW_ODD (0xFFF4F4F4)
#define COLOR_ROW_HEADER_LINE (0xFFCCCCCC)
#define COLOR_OUTPUT_DECORATION_IN_PROGRESS (0xFFFF7F00)
#define COLOR_OUTPUT_DECORATION_SUCCESS (0xFF3070FF)
#define COLOR_OUTPUT_DECORATION_FAILURE (0xFFFF3040)
#define COLOR_TEXT_MAIN (0xFF010102)
#define COLOR_TEXT_LIGHT (0xFF606062)
#define TEXT_SIZE_DEFAULT (13)
#define TEXT_SIZE_OUTPUT (12)
#define TEXT_SIZE_SMALL (11)

const EsStyle styleBackground = {
	.appearance = {
		.enabled = true,
		.backgroundColor = COLOR_BACKGROUND,
	},
};

const EsStyle styleRoot = {
	.metrics = {
		.mask = ES_THEME_METRICS_INSETS | ES_THEME_METRICS_GAP_ALL,
		.insets = ES_RECT_4(32, 32, 32, 32),
		.gapMajor = 4,
	},
};

const EsStyle styleInputRow = {
	.metrics = {
		.mask = ES_THEME_METRICS_GAP_ALL,
		.gapMajor = 8,
	},
};

const EsStyle stylePathDefaultPrefixDisplay = {
	.metrics = {
		.mask = ES_THEME_METRICS_FONT_FAMILY | ES_THEME_METRICS_FONT_WEIGHT
			| ES_THEME_METRICS_TEXT_SIZE | ES_THEME_METRICS_TEXT_ALIGN | ES_THEME_METRICS_TEXT_COLOR,
		.textColor = COLOR_TEXT_MAIN,
		.textAlign = ES_TEXT_H_LEFT | ES_TEXT_WRAP | ES_TEXT_V_TOP,
		.textSize = TEXT_SIZE_DEFAULT,
		.fontFamily = ES_FONT_SANS,
		.fontWeight = 6,
	},
};

const EsStyle styleOutputParagraph = {
	.metrics = {
		.mask = ES_THEME_METRICS_FONT_FAMILY | ES_THEME_METRICS_FONT_WEIGHT
			| ES_THEME_METRICS_TEXT_SIZE | ES_THEME_METRICS_TEXT_ALIGN | ES_THEME_METRICS_TEXT_COLOR,
		.textColor = COLOR_TEXT_MAIN,
		.textAlign = ES_TEXT_H_LEFT | ES_TEXT_WRAP | ES_TEXT_V_TOP,
		.textSize = TEXT_SIZE_OUTPUT,
		.fontFamily = ES_FONT_SANS,
		.fontWeight = 4,
	},
};

const EsStyle styleOutputData = {
	.metrics = {
		.mask = ES_THEME_METRICS_FONT_FAMILY | ES_THEME_METRICS_FONT_WEIGHT
			| ES_THEME_METRICS_TEXT_SIZE | ES_THEME_METRICS_TEXT_ALIGN | ES_THEME_METRICS_TEXT_COLOR,
		.textColor = COLOR_TEXT_MAIN,
		.textAlign = ES_TEXT_H_LEFT | ES_TEXT_ELLIPSIS | ES_TEXT_V_TOP,
		.textSize = TEXT_SIZE_OUTPUT,
		.fontFamily = ES_FONT_SANS,
		.fontWeight = 4,
	},
};

const EsStyle styleOutputParagraphStrong = {
	.metrics = {
		.mask = ES_THEME_METRICS_FONT_FAMILY | ES_THEME_METRICS_FONT_WEIGHT
			| ES_THEME_METRICS_TEXT_SIZE | ES_THEME_METRICS_TEXT_ALIGN | ES_THEME_METRICS_TEXT_COLOR,
		.textColor = COLOR_TEXT_MAIN,
		.textAlign = ES_TEXT_H_LEFT | ES_TEXT_WRAP | ES_TEXT_V_TOP,
		.textSize = TEXT_SIZE_OUTPUT,
		.fontFamily = ES_FONT_SANS,
		.fontWeight = 6,
	},
};

const EsStyle styleRowHeaderText = {
	.metrics = {
		.mask = ES_THEME_METRICS_FONT_FAMILY | ES_THEME_METRICS_FONT_WEIGHT | ES_THEME_METRICS_INSETS
			| ES_THEME_METRICS_TEXT_SIZE | ES_THEME_METRICS_TEXT_ALIGN | ES_THEME_METRICS_TEXT_COLOR,
		.insets = ES_RECT_4(0, 0, 0, 2),
		.textColor = COLOR_TEXT_MAIN,
		.textAlign = ES_TEXT_H_LEFT | ES_TEXT_WRAP | ES_TEXT_V_TOP,
		.textSize = TEXT_SIZE_SMALL,
		.fontFamily = ES_FONT_SANS,
		.fontWeight = 6,
	},
};

const EsStyle styleOutputParagraphItalic = {
	.metrics = {
		.mask = ES_THEME_METRICS_FONT_FAMILY | ES_THEME_METRICS_FONT_WEIGHT | ES_THEME_METRICS_IS_ITALIC
			| ES_THEME_METRICS_TEXT_SIZE | ES_THEME_METRICS_TEXT_ALIGN | ES_THEME_METRICS_TEXT_COLOR,
		.textColor = COLOR_TEXT_MAIN,
		.textAlign = ES_TEXT_H_LEFT | ES_TEXT_WRAP | ES_TEXT_V_TOP,
		.textSize = TEXT_SIZE_OUTPUT,
		.fontFamily = ES_FONT_SANS,
		.fontWeight = 4,
		.isItalic = true,
	},
};

const EsStyle stylePromptText = {
	.metrics = {
		.mask = ES_THEME_METRICS_FONT_FAMILY | ES_THEME_METRICS_FONT_WEIGHT
			| ES_THEME_METRICS_TEXT_SIZE | ES_THEME_METRICS_TEXT_ALIGN | ES_THEME_METRICS_TEXT_COLOR,
		.textColor = COLOR_TEXT_LIGHT,
		.textAlign = ES_TEXT_H_LEFT | ES_TEXT_V_CENTER,
		.textSize = TEXT_SIZE_DEFAULT,
		.fontFamily = ES_FONT_SANS,
		.fontWeight = 5,
	},
};

const EsStyle styleInputTextbox = {
	.inherit = ES_STYLE_TEXTBOX_TRANSPARENT,

	.metrics = {
		.mask = ES_THEME_METRICS_FONT_FAMILY | ES_THEME_METRICS_FONT_WEIGHT
			| ES_THEME_METRICS_TEXT_SIZE | ES_THEME_METRICS_TEXT_COLOR,
		.textColor = COLOR_TEXT_MAIN,
		.textSize = TEXT_SIZE_DEFAULT,
		.fontFamily = ES_FONT_SANS,
		.fontWeight = 4,
	},
};

const EsStyle styleCommandLogText = {
	.metrics = {
		.mask = ES_THEME_METRICS_FONT_FAMILY | ES_THEME_METRICS_FONT_WEIGHT
			| ES_THEME_METRICS_TEXT_SIZE | ES_THEME_METRICS_TEXT_ALIGN | ES_THEME_METRICS_TEXT_COLOR,
		.textColor = COLOR_TEXT_MAIN,
		.textAlign = ES_TEXT_H_LEFT | ES_TEXT_ELLIPSIS | ES_TEXT_V_TOP,
		.textSize = TEXT_SIZE_DEFAULT,
		.fontFamily = ES_FONT_SANS,
		.fontWeight = 4,
	},
};

const EsStyle styleInterCommandSpacer = {
	.metrics = {
		.mask = ES_THEME_METRICS_PREFERRED_HEIGHT,
		.preferredHeight = 14,
	},
};

const EsStyle styleOutputPanelWrapper = {
	.metrics = {
		.mask = ES_THEME_METRICS_INSETS | ES_THEME_METRICS_GAP_MAJOR,
		.insets = ES_RECT_1(10),
		.gapMajor = 12,
	},
};

const EsStyle styleOutputPanel = {
	.metrics = {
		.mask = ES_THEME_METRICS_INSETS | ES_THEME_METRICS_GAP_MAJOR,
		.insets = ES_RECT_2(0, 4),
		.gapMajor = 0,
	},
};

const EsStyle styleLogOutputGroup = {
	.metrics = {
		.mask = ES_THEME_METRICS_INSETS | ES_THEME_METRICS_GAP_MAJOR,
		.insets = ES_RECT_4(26, 0, 4, 4),
		.gapMajor = 0,
	},
};

const EsStyle styleLogOutputGroupTitle = {
	.metrics = {
		.mask = ES_THEME_METRICS_FONT_FAMILY | ES_THEME_METRICS_FONT_WEIGHT | ES_THEME_METRICS_INSETS
			| ES_THEME_METRICS_TEXT_SIZE | ES_THEME_METRICS_TEXT_ALIGN | ES_THEME_METRICS_TEXT_COLOR,
		.insets = ES_RECT_4(6, 0, 0, 0),
		.textColor = COLOR_TEXT_MAIN,
		.textAlign = ES_TEXT_H_LEFT | ES_TEXT_V_CENTER,
		.textSize = TEXT_SIZE_SMALL,
		.fontFamily = ES_FONT_SANS,
		.fontWeight = 6,
	},
};

const EsStyle styleOutputDecorationInProgress = {
	.metrics = {
		.mask = ES_THEME_METRICS_PREFERRED_WIDTH,
		.preferredWidth = 6,
	},

	.appearance = {
		.enabled = true,
		.backgroundColor = COLOR_OUTPUT_DECORATION_IN_PROGRESS,
	},
};

const EsStyle styleOutputDecorationSuccess = {
	.metrics = {
		.mask = ES_THEME_METRICS_PREFERRED_WIDTH,
		.preferredWidth = 6,
	},

	.appearance = {
		.enabled = true,
		.backgroundColor = COLOR_OUTPUT_DECORATION_SUCCESS,
	},
};

const EsStyle styleOutputDecorationFailure = {
	.metrics = {
		.mask = ES_THEME_METRICS_PREFERRED_WIDTH,
		.preferredWidth = 6,
	},

	.appearance = {
		.enabled = true,
		.backgroundColor = COLOR_OUTPUT_DECORATION_FAILURE,
	},
};

const EsThemeAppearance styleAppearanceRowHeader = {
	.enabled = true,
	.backgroundColor = COLOR_BACKGROUND,
	.borderColor = COLOR_ROW_HEADER_LINE,
	.borderSize = ES_RECT_4(0, 0, 0, 1),
};

const EsThemeAppearance styleAppearanceRowEven = {
	.enabled = true,
	.backgroundColor = COLOR_BACKGROUND,
};

const EsThemeAppearance styleAppearanceRowOdd = {
	.enabled = true,
	.backgroundColor = COLOR_ROW_ODD,
};

const EsStyle styleTableCell = {
	.metrics = {
		.mask = ES_THEME_METRICS_INSETS,
		.insets = ES_RECT_1(3),
	},
};

const EsStyle styleListRowEven = {
	.metrics = {
		.mask = ES_THEME_METRICS_INSETS,
		.insets = ES_RECT_1(3),
	},

	.appearance = styleAppearanceRowEven,
};

const EsStyle styleListRowOdd = {
	.metrics = {
		.mask = ES_THEME_METRICS_INSETS,
		.insets = ES_RECT_1(3),
	},

	.appearance = styleAppearanceRowOdd,
};

const EsStyle styleTable = {
	.metrics = {
		.mask = ES_THEME_METRICS_CLIP_ENABLED | ES_THEME_METRICS_GAP_MAJOR,
		.clipEnabled = true,
		.gapMajor = 6,
	},
};

const EsStyle styleList = {
	.metrics = {
		.mask = ES_THEME_METRICS_CLIP_ENABLED,
		.clipEnabled = true,
	},
};

const EsStyle styleImageViewerPane = {
	.metrics = {
		.mask = ES_THEME_METRICS_CLIP_ENABLED | ES_THEME_METRICS_MAXIMUM_HEIGHT,
		.clipEnabled = true,
		.maximumHeight = 400,
	},
};

void AddREPLResult(ExecutionContext *context, EsElement *parent, Node *type, Value value) {
	// TODO Truncating/scrolling/collapsing/saving/copying output.
	// TODO Pagination.
	// TODO Letting scripts register custom views for structs.

	size_t bytes;

	if (type->type == T_INT) {
		char *buffer = EsStringAllocateAndFormat(&bytes, "%d", value.i);
		EsTextDisplayCreate(parent, ES_CELL_H_FILL, EsStyleIntern(&styleOutputData), buffer, bytes);
		EsHeapFree(buffer);
	} else if (type->type == T_BOOL) {
		char *buffer = EsStringAllocateAndFormat(&bytes, "%z", value.i ? "true" : "false");
		EsTextDisplayCreate(parent, ES_CELL_H_FILL, EsStyleIntern(&styleOutputData), buffer, bytes);
		EsHeapFree(buffer);
	} else if (type->type == T_NULL) {
		char *buffer = EsStringAllocateAndFormat(&bytes, "%z", "null");
		EsTextDisplayCreate(parent, ES_CELL_H_FILL, EsStyleIntern(&styleOutputData), buffer, bytes);
		EsHeapFree(buffer);
	} else if (type->type == T_FLOAT) {
		char *buffer = EsStringAllocateAndFormat(&bytes, "%F", value.f);
		EsTextDisplayCreate(parent, ES_CELL_H_FILL, EsStyleIntern(&styleOutputData), buffer, bytes);
		EsHeapFree(buffer);
	} else if (type->type == T_STR) {
		EsAssert(context->heapEntriesAllocated > (uint64_t) value.i);
		HeapEntry *entry = &context->heap[value.i];
		const char *valueText;
		size_t valueBytes;
		ScriptHeapEntryToString(context, entry, &valueText, &valueBytes);

		uint64_t pngSignature = 0x0A1A0A0D474E5089;
		uint32_t jpgSignature = 0xE0FFD8FF;
		bool isPNG = valueBytes > sizeof(pngSignature) && 0 == EsMemoryCompare(&pngSignature, valueText, sizeof(pngSignature));
		bool isJPG = valueBytes > sizeof(jpgSignature) && 0 == EsMemoryCompare(&jpgSignature, valueText, sizeof(jpgSignature));

		if (isPNG || isJPG) {
			EsCanvasPane *canvasPane = EsCanvasPaneCreate(parent, ES_CELL_H_FILL, EsStyleIntern(&styleImageViewerPane));
			EsImageDisplay *display = EsImageDisplayCreate(canvasPane, ES_CELL_H_LEFT | ES_CELL_V_TOP);
			EsImageDisplayLoadFromMemory(display, valueText, valueBytes);
			char *buffer = EsStringAllocateAndFormat(&bytes, "%d by %d pixels \u2027 %z", 
					EsImageDisplayGetImageWidth(display),
					EsImageDisplayGetImageHeight(display),
					isPNG ? "PNG" : isJPG ? "JPEG" : "unknown format");
			EsTextDisplayCreate(parent, ES_CELL_H_FILL, EsStyleIntern(&styleOutputParagraphItalic), buffer, bytes);
			EsHeapFree(buffer);
		} else if (EsUTF8IsValid(valueText, valueBytes)) {
			char *buffer = EsStringAllocateAndFormat(&bytes, "\u201C%s\u201D", valueBytes, valueText);
			EsTextDisplayCreate(parent, ES_CELL_H_FILL, EsStyleIntern(&styleOutputData), buffer, bytes);
			EsHeapFree(buffer);
		} else {
			EsTextDisplayCreate(parent, ES_CELL_H_FILL, EsStyleIntern(&styleOutputParagraphItalic), EsLiteral("Binary data string.\n"));
		}
	} else if (type->type == T_ERR) {
		if (value.i) {
			EsAssert(context->heapEntriesAllocated > (uint64_t) value.i);
			HeapEntry *entry = &context->heap[value.i];

			if (entry->success) {
				if (type->firstChild->type == T_VOID) {
					EsTextDisplayCreate(parent, ES_CELL_H_FILL, EsStyleIntern(&styleOutputParagraphItalic), 
							EsLiteral("Success.\n"));
				} else {
					AddREPLResult(context, parent, type->firstChild, entry->errorValue);
				}
			} else {
				EsAssert(context->heapEntriesAllocated > (uint64_t) entry->errorValue.i);
				const char *valueText;
				size_t valueBytes;
				ScriptHeapEntryToString(context, &context->heap[entry->errorValue.i], &valueText, &valueBytes);
				char buffer[100];
				size_t bytes = EsStringFormat(buffer, sizeof(buffer), "Error: %s.\n", valueBytes, valueText);
				EsTextDisplayCreate(parent, ES_CELL_H_FILL, EsStyleIntern(&styleOutputParagraphItalic), buffer, bytes);
			}
		} else {
			EsTextDisplayCreate(parent, ES_CELL_H_FILL, EsStyleIntern(&styleOutputParagraphItalic), 
					EsLiteral("Error: UNKNOWN.\n"));
		}
	} else if (type->type == T_LIST && type->firstChild->type == T_STRUCT) {
		EsAssert(context->heapEntriesAllocated > (uint64_t) value.i);
		HeapEntry *listEntry = &context->heap[value.i];
		EsAssert(listEntry->type == T_LIST);
		if (!listEntry->length) goto normalList;
		EsAssert(context->heapEntriesAllocated > (uint64_t) listEntry->list[0].i);
		EsAssert(context->heap[listEntry->list[0].i].type == T_STRUCT);

		EsPanel *table = EsPanelCreate(parent, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL | ES_PANEL_TABLE | ES_PANEL_H_SCROLL_AUTO, EsStyleIntern(&styleTable));
		EsPanelSetBands(table, context->heap[listEntry->list[0].i].fieldCount);
		EsPanelTableAddBandDecorator(table, { .index = 0, .repeatEvery = 0, .axis = 1, .appearance = styleAppearanceRowHeader });
		EsPanelTableAddBandDecorator(table, { .index = 1, .repeatEvery = 2, .axis = 1, .appearance = styleAppearanceRowEven });
		EsPanelTableAddBandDecorator(table, { .index = 2, .repeatEvery = 2, .axis = 1, .appearance = styleAppearanceRowOdd });

		{
			Node *field = type->firstChild->firstChild;

			while (field) {
				EsTextDisplayCreate(table, ES_CELL_H_CENTER | ES_CELL_V_CENTER, 
						EsStyleIntern(&styleRowHeaderText), field->token.text, field->token.textBytes);
				field = field->sibling;
			}
		}

		for (uintptr_t i = 0; i < listEntry->length; i++) {
			EsAssert(context->heapEntriesAllocated > (uint64_t) value.i);
			HeapEntry *itemEntry = &context->heap[listEntry->list[i].i];
			EsAssert(itemEntry->type == T_STRUCT);
			EsAssert(itemEntry->fieldCount == context->heap[listEntry->list[0].i].fieldCount);
			Node *field = type->firstChild->firstChild;
			uintptr_t j = 0;

			while (field) {
				EsAssert(j != itemEntry->fieldCount);
				AddREPLResult(context, EsPanelCreate(table, ES_CELL_H_CENTER | ES_CELL_V_TOP, EsStyleIntern(&styleTableCell)), 
						field->firstChild, itemEntry->fields[j]);
				field = field->sibling;
				j++;
			}
		}
	} else if (type->type == T_LIST) {
		normalList:;
		EsPanel *panel = EsPanelCreate(parent, ES_CELL_H_FILL | ES_PANEL_VERTICAL | ES_PANEL_STACK, EsStyleIntern(&styleList));
		EsAssert(context->heapEntriesAllocated > (uint64_t) value.i);
		HeapEntry *entry = &context->heap[value.i];
		EsAssert(entry->type == T_LIST);

		if (!entry->length) {
			EsTextDisplayCreate(parent, ES_CELL_H_FILL, EsStyleIntern(&styleOutputParagraphItalic), 
					EsLiteral("Empty list.\n"));
		}

		for (uintptr_t i = 0; i < entry->length; i++) {
			EsPanel *item = EsPanelCreate(panel, ES_CELL_H_FILL, EsStyleIntern((i % 2) ? &styleListRowOdd : &styleListRowEven));
			AddREPLResult(context, item, type->firstChild, entry->list[i]);
		}
	} else if (type->type == T_STRUCT) {
		EsPanel *panel = EsPanelCreate(parent, ES_CELL_H_FILL | ES_PANEL_VERTICAL | ES_PANEL_STACK);
		EsAssert(context->heapEntriesAllocated > (uint64_t) value.i);
		HeapEntry *entry = &context->heap[value.i];
		EsAssert(entry->type == T_STRUCT);
		uintptr_t i = 0;

		Node *field = type->firstChild;

		while (field) {
			EsAssert(i != entry->fieldCount);
			EsPanel *item = EsPanelCreate(panel, ES_CELL_H_FILL, EsStyleIntern((i % 2) ? &styleListRowOdd : &styleListRowEven));
			AddREPLResult(context, item, field->firstChild, entry->fields[i]);
			field = field->sibling;
			i++;
		}
	} else if (type->type == T_FUNCPTR) {
		EsTextDisplayCreate(parent, ES_CELL_H_FILL, EsStyleIntern(&styleOutputParagraphItalic), 
				EsLiteral("Function pointer.\n"));
	} else {
		EsTextDisplayCreate(parent, ES_CELL_H_FILL, EsStyleIntern(&styleOutputParagraphItalic), 
				EsLiteral("The type of the result was not recognized.\n"));
	}
}

void ExternalPassREPLResult(ExecutionContext *context, Value value) {
	Instance *instance = scriptInstance;

	if (instance->gotREPLResult) return;
	if (instance->outputLineBufferBytes) AddOutput(instance, EsLiteral("\n"));

	instance->anyOutput = true;
	instance->gotREPLResult = true;

	EsMessageMutexAcquire();
	AddREPLResult(context, instance->outputPanel, context->functionData->replResultType, value);
	EsMessageMutexRelease();
}

void ScriptThread(EsGeneric _instance) {
	Instance *instance = (Instance *) _instance.p;
	scriptInstance = instance;
	int result = ScriptExecuteFromFile(EsLiteral("in"), instance->inputText, instance->inputBytes, true);
	instance->inputText = nullptr;
	
	if (instance->outputLineBufferBytes) {
		AddOutput(instance, EsLiteral("\n"));
	}

	while (fixedAllocationBlocks) {
		void *block = fixedAllocationBlocks;
		fixedAllocationBlocks = (void **) *fixedAllocationBlocks;
		EsHeapFree(block);
	}

	fixedAllocationCurrentBlock = nullptr;
	fixedAllocationCurrentPosition = 0;
	fixedAllocationCurrentSize = 0;

	EsMessageMutexAcquire();

	if (!instance->anyOutput) {
		EsElementDestroy(EsElementGetLayoutParent(instance->outputPanel));
		instance->outputPanel = nullptr;
	} else {
		instance->anyOutput = false;
	}

	instance->gotREPLResult = false;

	if (result == 0) {
		EsSpacerChangeStyle(instance->outputDecoration, EsStyleIntern(&styleOutputDecorationSuccess));
	} else {
		EsSpacerChangeStyle(instance->outputDecoration, EsStyleIntern(&styleOutputDecorationFailure));
	}

	instance->outputElements.Add(EsSpacerCreate(instance->root, ES_CELL_H_FILL, EsStyleIntern(&styleInterCommandSpacer)));
	AddPrompt(instance);

	EsMessageMutexRelease();
}

void EnterCommand(Instance *instance) {
	EsAssert(instance->inputTextbox);
	EsAssert(instance->inputRow);
	size_t dataBytes;
	char *data = EsTextboxGetContents(instance->inputTextbox, &dataBytes, ES_FLAGS_DEFAULT);
	EsElementDestroy(instance->inputRow);
	instance->inputTextbox = nullptr;
	instance->inputRow = nullptr;

	uint8_t newline = '\n';
	EsFileWriteSync(globalCommandHistoryFile.handle, EsFileGetSize(globalCommandHistoryFile.handle), dataBytes, data);
	EsFileWriteSync(globalCommandHistoryFile.handle, EsFileGetSize(globalCommandHistoryFile.handle), 1, &newline);
	EsFileControl(globalCommandHistoryFile.handle, ES_FILE_CONTROL_FLUSH); 

	EsPanel *commandLogRow = EsPanelCreate(instance->root, ES_CELL_H_FILL | ES_PANEL_STACK | ES_PANEL_HORIZONTAL, EsStyleIntern(&styleInputRow));
	EsTextDisplayCreate(commandLogRow, ES_FLAGS_DEFAULT, EsStyleIntern(&stylePromptText), "\u2661");
	EsTextDisplay *inputCommand = EsTextDisplayCreate(commandLogRow, ES_CELL_H_FILL, EsStyleIntern(&styleCommandLogText), data, dataBytes);
	EsTextDisplaySetupSyntaxHighlighting(inputCommand, ES_SYNTAX_HIGHLIGHTING_LANGUAGE_SCRIPT);
	instance->outputElements.Add(commandLogRow);

	EsAssert(!instance->outputPanel);
	EsAssert(!instance->outputDecoration);
	EsPanel *outputPanelWrapper = EsPanelCreate(instance->root, ES_CELL_H_FILL | ES_PANEL_STACK | ES_PANEL_HORIZONTAL, EsStyleIntern(&styleOutputPanelWrapper));
	instance->outputDecoration = EsSpacerCreate(outputPanelWrapper, ES_CELL_V_FILL, EsStyleIntern(&styleOutputDecorationInProgress));
	instance->outputPanel = EsPanelCreate(outputPanelWrapper, ES_CELL_H_FILL | ES_PANEL_STACK | ES_PANEL_VERTICAL, EsStyleIntern(&styleOutputPanel));
	instance->logOutputGroup = instance->outputPanel;

	instance->inputText = data;
	instance->inputBytes = dataBytes;
	EsAssert(ES_SUCCESS == EsThreadCreate(ScriptThread, &instance->scriptThread, instance)); 
	EsHandleClose(instance->scriptThread.handle);
}

int InputTextboxMessage(EsElement *element, EsMessage *message) {
	if (message->type == ES_MSG_KEY_TYPED) {
		if (message->keyboard.scancode == ES_SCANCODE_ENTER) {
			EnterCommand(element->instance);
			return ES_HANDLED;
		}
	}

	return 0;
}

void AddOutput(Instance *instance, const char *text, size_t textBytes) {
	instance->anyOutput = true;

	for (uintptr_t i = 0; i < textBytes; i++) {
		if (text[i] == '\n') {
			if (EsUTF8IsValid(instance->outputLineBuffer, instance->outputLineBufferBytes)) {
				EsMessageMutexAcquire();
				EsTextDisplayCreate(instance->outputPanel, ES_CELL_H_FILL | ES_TEXT_DISPLAY_RICH_TEXT, 
						EsStyleIntern(&styleOutputParagraph), 
						instance->outputLineBuffer, instance->outputLineBufferBytes);
				EsMessageMutexRelease();
			} else {
				EsMessageMutexAcquire();
				EsTextDisplayCreate(instance->outputPanel, ES_CELL_H_FILL, EsStyleIntern(&styleOutputParagraphItalic), 
						EsLiteral("Encoding error.\n"));
				EsMessageMutexRelease();
			}

			instance->outputLineBufferBytes = 0;
		} else {
			if (instance->outputLineBufferBytes == instance->outputLineBufferAllocated) {
				instance->outputLineBufferAllocated = (instance->outputLineBufferAllocated + 4) * 2;
				instance->outputLineBuffer = (char *) EsHeapReallocate(instance->outputLineBuffer, 
						instance->outputLineBufferAllocated, false);
			}

			instance->outputLineBuffer[instance->outputLineBufferBytes++] = text[i];
		}
	}
}

void AddLogOutput(Instance *instance, const char *text, size_t textBytes) {
	instance->anyOutput = true;

	if (EsUTF8IsValid(text, textBytes)) {
		EsMessageMutexAcquire();
		EsTextDisplayCreate(instance->logOutputGroup, ES_CELL_H_FILL | ES_TEXT_DISPLAY_RICH_TEXT, 
				EsStyleIntern(&styleOutputParagraph), text, textBytes);
		EsMessageMutexRelease();
	} else {
		EsMessageMutexAcquire();
		EsTextDisplayCreate(instance->logOutputGroup, ES_CELL_H_FILL, 
				EsStyleIntern(&styleOutputParagraphItalic), EsLiteral("Encoding error.\n"));
		EsMessageMutexRelease();
	}
}

void DisclosureButtonCommand(Instance *, EsElement *element, EsCommand *) {
	EsElement *panel = (EsElement *) element->userData.p;
	EsButtonSetIcon((EsButton *) element, EsElementIsHidden(panel) ? ES_ICON_PANE_HIDE_SYMBOLIC : ES_ICON_PANE_SHOW_SYMBOLIC);
	EsElementSetHidden(panel, !EsElementIsHidden(panel));
}

void AddLogOpenGroup(Instance *instance, const char *title, size_t titleBytes) {
	if (instance->logGroupNestLevel >= 1000000000) {
		return;
	}

	instance->anyOutput = true;
	instance->logGroupNestLevel++;

	if (instance->logGroupNestLevel < 20) {
		bool startCollapsed = instance->logGroupNestLevel > 2;
		EsMessageMutexAcquire();
		EsPanel *row = EsPanelCreate(instance->logOutputGroup, ES_CELL_H_FILL | ES_PANEL_HORIZONTAL);
		// TODO Better icons, or make a dedicated disclosure control in the API?
		EsButton *disclosureButton = EsButtonCreate(row, ES_FLAGS_DEFAULT, ES_STYLE_PUSH_BUTTON_STATUS_BAR);
		EsButtonSetIcon(disclosureButton, startCollapsed ? ES_ICON_PANE_SHOW_SYMBOLIC : ES_ICON_PANE_HIDE_SYMBOLIC);
		EsTextDisplayCreate(row, ES_CELL_H_FILL, EsStyleIntern(&styleLogOutputGroupTitle), title, titleBytes);
		instance->logOutputGroup = EsPanelCreate(instance->logOutputGroup, 
				ES_CELL_H_FILL | (startCollapsed ? ES_ELEMENT_HIDDEN : 0), EsStyleIntern(&styleLogOutputGroup));
		disclosureButton->userData = instance->logOutputGroup;
		EsButtonOnCommand(disclosureButton, DisclosureButtonCommand);
		EsMessageMutexRelease();
	}
}

void AddLogClose(Instance *instance) {
	if (instance->logGroupNestLevel) {
		if (instance->logGroupNestLevel < 20) {
			EsMessageMutexAcquire();
			instance->logOutputGroup = EsElementGetLayoutParent(instance->logOutputGroup);
			EsMessageMutexRelease();
		}

		instance->logGroupNestLevel--;
	}
}

void AddPrompt(Instance *instance) {
	if (instance->outputPanel) {
		instance->outputElements.Add(EsElementGetLayoutParent(instance->outputPanel));
	}

	EsAssert(!instance->inputTextbox);
	EsAssert(!instance->inputRow);
	instance->outputPanel = nullptr;
	instance->outputDecoration = nullptr;

	instance->inputRow = EsPanelCreate(instance->root, ES_CELL_H_FILL | ES_PANEL_STACK | ES_PANEL_HORIZONTAL, EsStyleIntern(&styleInputRow));
	EsTextDisplayCreate(instance->inputRow, ES_FLAGS_DEFAULT, EsStyleIntern(&stylePromptText), "\u2665");
	instance->inputTextbox = EsTextboxCreate(instance->inputRow, ES_CELL_H_FILL, EsStyleIntern(&styleInputTextbox)); // TODO Command history.
	EsTextboxSetupSyntaxHighlighting(instance->inputTextbox, ES_SYNTAX_HIGHLIGHTING_LANGUAGE_SCRIPT);
	EsTextboxEnableSmartReplacement(instance->inputTextbox, false);
	instance->inputTextbox->messageUser = InputTextboxMessage;
	EsElementFocus(instance->inputTextbox, ES_ELEMENT_FOCUS_ENSURE_VISIBLE);
}

void CommandClearOutput(Instance *instance, EsElement *, EsCommand *) {
	for (uintptr_t i = 0; i < instance->outputElements.Length(); i++) {
		EsElementDestroy(instance->outputElements[i]);
	}

	instance->outputElements.Free();
}

int InstanceCallback(Instance *instance, EsMessage *message) {
	if (message->type == ES_MSG_INSTANCE_DESTROY) {
		// TODO Stopping the script thread.
		// 	Probably the script thread will need to open a reference to the instance,
		// 	and check the instance is not closed every time it attempts to acquire the message mutex.
		EsHeapFree(instance->outputLineBuffer);
		instance->outputElements.Free();
	}

	return 0;
}

void _start() {
	_init();

	globalCommandHistoryFile = EsFileOpen(EsLiteral("|Settings:/Command History.txt"), ES_FILE_WRITE);

	// TODO Proper drive mounting.

	size_t deviceCount;
	EsMessageDevice *devices = EsDeviceEnumerate(&deviceCount);
	
	for (uintptr_t i = 0; i < deviceCount; i++) {
		if (devices[i].type == ES_DEVICE_FILE_SYSTEM && EsDeviceControl(devices[i].handle, ES_DEVICE_CONTROL_FS_IS_BOOT, 0, nullptr)) {
			EsMountPointAdd(EsLiteral("0:"), devices[i].handle);
		}
	}

	EsHeapFree(devices);

	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_INSTANCE_CREATE) {
			Instance *instance = EsInstanceCreate(message, "Script Console", -1);
			instance->callback = InstanceCallback;
			EsCommandRegister(&instance->commandClearOutput, instance, EsLiteral("Clear output"), CommandClearOutput, 
					1 /* stableID */, "Ctrl+Shift+L", true /* enabled */);
			EsWindowSetIcon(instance->window, ES_ICON_UTILITIES_TERMINAL);
			EsPanel *wrapper = EsPanelCreate(instance->window, ES_CELL_FILL, ES_STYLE_PANEL_WINDOW_DIVIDER);
			EsPanel *background = EsPanelCreate(wrapper, ES_CELL_FILL | ES_PANEL_V_SCROLL_AUTO, EsStyleIntern(&styleBackground));
			instance->root = EsPanelCreate(background, ES_CELL_H_FILL | ES_PANEL_STACK | ES_PANEL_VERTICAL, EsStyleIntern(&styleRoot));
			AddPrompt(instance);
			EsElement *toolbar = EsWindowGetToolbar(instance->window);
			EsCommandAddButton(&instance->commandClearOutput, 
					EsButtonCreate(toolbar, ES_FLAGS_DEFAULT, 0, EsLiteral("Clear output")));
		}
	}
}
