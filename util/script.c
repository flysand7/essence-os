// TODO Basic missing features:
// 	- Maps: T[int], T[str].
// 	- Control flow: break, continue.
// 	- Other operators: integer modulo.
// 	- Named optional arguments with default values.
// 	- Using declared types from imported modules.
// 	- struct inheritance.

// TODO Larger missing features:
// 	- Serialization.
// 	- Debugging.
// 	- Verbose mode, where every external call is logged, every variable modification is logged, every line is logged, etc? Saving output to file.
// 	- Saving and showing the stack trace of where T_ERR values were created in assertion failure messages.

// TODO Cleanup:
// 	- Cleanup the code in External- functions and ScriptExecuteFunction using macros for common stack and heap operations.
// 	- Cleanup the ImportData/ExecutionContext/FunctionBuilder structures and their relationships.
// 	- Cleanup the variables/stack arrays.
// 	- Cleanup the platform layer.

// TODO Safety:
// 	- Safety against extremely large scripts?
// 	- Loading untrusted bytecode files?

// TODO Miscellaneous:
// 	- Inlining small strings.
// 	- Exponent notation in numeric literals.
// 	- Block comments.
// 	- More escape sequences in string literals.
// 	- Setting the initial values of global variables.
// 	- Better handling of memory allocation failures.
// 	- Operation arguments are evaluated in the opposite order to functions?
// 	- Shrink lists during garbage collection.

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define FUNCTION_MAX_ARGUMENTS (20) // Also the maximum number of return values in a tuple.

#define EXTCALL_NO_RETURN            (1)
#define EXTCALL_RETURN_UNMANAGED     (2)
#define EXTCALL_RETURN_MANAGED       (3)
#define EXTCALL_START_COROUTINE      (4)
#define EXTCALL_RETURN_ERR_UNMANAGED (5)
#define EXTCALL_RETURN_ERR_MANAGED   (6)
#define EXTCALL_RETURN_ERR_ERROR     (7)

#define T_ERROR               (0)
#define T_EOF                 (1)
#define T_IDENTIFIER          (2)
#define T_STRING_LITERAL      (3)
#define T_NUMERIC_LITERAL     (4)

#define T_ADD                 (20)
#define T_MINUS               (21)
#define T_ASTERISK            (22)
#define T_SLASH               (23)
#define T_NEGATE              (24)
#define T_LEFT_ROUND          (25)
#define T_RIGHT_ROUND         (26)
#define T_LEFT_SQUARE         (27)
#define T_RIGHT_SQUARE        (28)
#define T_LEFT_FANCY          (29)
#define T_RIGHT_FANCY         (30)
#define T_COMMA               (31)
#define T_EQUALS              (32)
#define T_SEMICOLON           (33)
#define T_GREATER_THAN        (34)
#define T_LESS_THAN           (35)
#define T_GT_OR_EQUAL         (36)
#define T_LT_OR_EQUAL         (37)
#define T_DOUBLE_EQUALS       (38)
#define T_NOT_EQUALS          (39)
#define T_LOGICAL_AND         (40)
#define T_LOGICAL_OR          (41)
#define T_ADD_EQUALS          (42)
#define T_MINUS_EQUALS        (43)
#define T_ASTERISK_EQUALS     (44)
#define T_SLASH_EQUALS        (45)
#define T_DOT                 (46)
#define T_COLON               (47)
#define T_LOGICAL_NOT         (48)
#define T_BIT_SHIFT_LEFT      (49)
#define T_BIT_SHIFT_RIGHT     (50)
#define T_BITWISE_OR          (51)
#define T_BITWISE_AND         (52)
#define T_BITWISE_NOT         (53)
#define T_BITWISE_XOR         (54)

#define T_ROOT                (70)
#define T_FUNCBODY            (71)
#define T_ARGUMENTS           (72)
#define T_ARGUMENT            (73)
#define T_FUNCTION            (74)
#define T_BLOCK               (75)
#define T_VARIABLE            (76)
#define T_CALL                (77)
#define T_DECLARE             (78)
#define T_FUNCPTR             (79)
#define T_STR_INTERPOLATE     (80)
#define T_INDEX               (81)
#define T_LIST                (82)
#define T_IMPORT_PATH         (83)
#define T_LIST_LITERAL        (84)
#define T_REPL_RESULT         (85)
#define T_RETURN_TUPLE        (86)
#define T_DECLARE_GROUP       (87)
#define T_DECL_GROUP_AND_SET  (88)
#define T_SET_GROUP           (89)
#define T_FOR_EACH            (90)
#define T_ERR_CAST            (91)
#define T_IF_ERR              (92)
#define T_INTTYPE_CONSTANT    (93)
#define T_ANYTYPE_CAST        (94)
#define T_CAST_TYPE_WRAPPER   (95)
#define T_ZERO                (96)

#define T_EXIT_SCOPE          (100)
#define T_END_FUNCTION        (101)
#define T_POP                 (102)
#define T_BRANCH              (103)
#define T_CONCAT              (104)
#define T_INTERPOLATE_STR     (105)
#define T_INTERPOLATE_BOOL    (106)
#define T_INTERPOLATE_INT     (107)
#define T_INTERPOLATE_FLOAT   (108)
#define T_DUP                 (109)
#define T_SWAP                (110)
#define T_INTERPOLATE_ILIST   (111)
#define T_ROT3                (112)
#define T_LIBCALL             (113)
#define T_END_CALLBACK        (114)

#define T_FLOAT_ADD           (120)
#define T_FLOAT_MINUS         (121)
#define T_FLOAT_ASTERISK      (122)
#define T_FLOAT_SLASH         (123)
#define T_FLOAT_NEGATE        (124)
#define T_FLOAT_GREATER_THAN  (125)
#define T_FLOAT_LESS_THAN     (126)
#define T_FLOAT_GT_OR_EQUAL   (127)
#define T_FLOAT_LT_OR_EQUAL   (128)
#define T_FLOAT_DOUBLE_EQUALS (129)
#define T_FLOAT_NOT_EQUALS    (130)
#define T_STR_DOUBLE_EQUALS   (131)
#define T_STR_NOT_EQUALS      (132)
#define T_EQUALS_DOT          (133)
#define T_EQUALS_LIST         (134)
#define T_INDEX_LIST          (135)

#define T_OP_RESIZE           (140)
#define T_OP_ADD              (141)
#define T_OP_INSERT           (142)
#define T_OP_INSERT_MANY      (143)
#define T_OP_DELETE           (144)
#define T_OP_DELETE_MANY      (145)
#define T_OP_DELETE_LAST      (146)
#define T_OP_DELETE_ALL       (147)
#define T_OP_FIRST            (148)
#define T_OP_LAST             (149)
#define T_OP_LEN              (150)
#define T_OP_DISCARD          (151)
#define T_OP_ASSERT           (152)
#define T_OP_CURRY            (153)
#define T_OP_ASYNC            (154)
#define T_OP_FIND_AND_DELETE  (155)
#define T_OP_FIND             (156)
#define T_OP_FIND_AND_DEL_STR (157)
#define T_OP_FIND_STR         (158)
#define T_OP_ASSERT_ERR       (159)
#define T_OP_SUCCESS          (160)
#define T_OP_ERROR            (161)
#define T_OP_DEFAULT          (162)
#define T_OP_INT_TO_FLOAT     (163)
#define T_OP_FLOAT_TRUNCATE   (164)
#define T_OP_CAST             (165)

#define T_IF                  (170)
#define T_WHILE               (171)
#define T_FOR                 (172)
#define T_INT                 (173)
#define T_FLOAT               (174)
#define T_BOOL                (175)
#define T_VOID                (176)
#define T_RETURN              (177)
#define T_ELSE                (178)
#define T_EXTCALL             (179)
#define T_STR                 (180)
#define T_FUNCTYPE            (181)
#define T_NULL                (182)
#define T_FALSE               (183)
#define T_TRUE                (184)
#define T_ASSERT              (185)
#define T_PERSIST             (186)
#define T_STRUCT              (187)
#define T_NEW                 (188)
#define T_OPTION              (189)
#define T_IMPORT              (190)
#define T_INLINE              (191)
#define T_AWAIT               (192)
#define T_TUPLE               (193)
#define T_IN                  (194)
#define T_ERR                 (195)
#define T_SUCCESS             (196)
#define T_RETERR              (197)
#define T_INTTYPE             (198)
#define T_HANDLETYPE          (199)
#define T_ANYTYPE             (200)
#define T_LIBRARY             (201)

#define STACK_READ_STRING(textVariable, bytesVariable, stackIndex) \
	if (context->c->stackPointer < stackIndex) return -1; \
	if (!context->c->stackIsManaged[context->c->stackPointer - stackIndex]) return -1; \
	uint64_t _index ## stackIndex = context->c->stack[context->c->stackPointer - stackIndex].i; \
	if (context->heapEntriesAllocated <= _index ## stackIndex) return -1; \
	HeapEntry *_entry ## stackIndex = &context->heap[_index ## stackIndex]; \
	if (_entry ## stackIndex->type != T_EOF && _entry ## stackIndex->type != T_STR && _entry ## stackIndex->type != T_CONCAT) return -1; \
	const char *textVariable; \
	size_t bytesVariable; \
	ScriptHeapEntryToString(context, _entry ## stackIndex, &textVariable, &bytesVariable);
#define STACK_POP_STRING(textVariable, bytesVariable) \
	STACK_READ_STRING(textVariable, bytesVariable, 1); \
	context->c->stackPointer--;
#define STACK_POP_STRING_2(textVariable1, bytesVariable1, textVariable2, bytesVariable2) \
	STACK_READ_STRING(textVariable1, bytesVariable1, 1); \
	STACK_READ_STRING(textVariable2, bytesVariable2, 2); \
	context->c->stackPointer -= 2;
#define RETURN_STRING_COPY(_text, _bytes) \
	returnValue->i = HeapAllocate(context); \
	context->heap[returnValue->i].type = T_STR; \
	context->heap[returnValue->i].bytes = _bytes; \
	context->heap[returnValue->i].text = (char *) AllocateResize(NULL, context->heap[returnValue->i].bytes); \
	MemoryCopy(context->heap[returnValue->i].text, _text, context->heap[returnValue->i].bytes);
#define RETURN_STRING_NO_COPY(_text, _bytes) \
	returnValue->i = HeapAllocate(context); \
	context->heap[returnValue->i].type = T_STR; \
	context->heap[returnValue->i].bytes = _bytes; \
	context->heap[returnValue->i].text = _text;

typedef struct Token {
	struct ImportData *module;
	const char *text;
	size_t textBytes;
	uint32_t line;
	uint8_t type;
} Token;

typedef struct Tokenizer {
	struct ImportData *module;
	const char *input;
	size_t inputBytes;
	uintptr_t position;
	uintptr_t line;
	bool error, isBaseModule;
} Tokenizer;

typedef struct Scope {
	struct Node **entries;
	size_t entryCount;
	size_t variableEntryCount;
	size_t entriesAllocated;
	bool isRoot;
} Scope;

typedef struct Node {
	uint8_t type;
	bool referencesRootScope, isExternalCall, isPersistentVariable, isOptionVariable, cycleCheck, hasTypeInheritanceParent;
	uint8_t operationType;
	int32_t inlineImportVariableIndex;
	Token token;
	struct Node *firstChild;
	struct Node *sibling;
	struct Node *parent; // Set in ASTSetScopes.
	Scope *scope; // Set in ASTSetScopes.
	struct Node *expressionType; // Set in ASTSetTypes.
	struct ImportData *importData; // The module being imported by this node.
} Node;

typedef struct Value {
	union {
		int64_t i;
		double f;
	};
} Value;

typedef struct LineNumber {
	struct ImportData *importData;
	uint32_t instructionPointer;
	uint32_t lineNumber;
	Token *function;
} LineNumber;

typedef struct FunctionBuilder {
	uint8_t *data;
	size_t dataBytes;
	size_t dataAllocated;
	LineNumber *lineNumbers;
	size_t lineNumberCount;
	size_t lineNumbersAllocated;
	int32_t scopeIndex;
	bool isPersistentVariable, isDotAssignment, isListAssignment;
	uintptr_t globalVariableOffset;
	struct ImportData *importData; // Only valid during script loading.
	Node *replResultType;
} FunctionBuilder;

typedef struct BackTraceItem {
	uint32_t instructionPointer : 30,
		 popResult : 1,
		 assertResult : 1;
	int32_t variableBase : 30;
} BackTraceItem;

typedef struct HeapEntry {
	uint8_t type;
	bool gcMark;
	bool internalValuesAreManaged;
	uint32_t externalReferenceCount;

	union {
		struct { // T_STR
			// TODO Inlining small strings.
			size_t bytes;
			char *text;
		};

		struct { // T_STRUCT
			uint16_t fieldCount;
			Value *fields; // Managed bools placed before this.
		};

		struct { // T_LIST
			uint32_t length, allocated;
			Value *list;
		};

		struct { // Unused entry.
			uintptr_t nextUnusedEntry;
		};

		struct { // Various function pointers.
			int64_t lambdaID;
			Value curryValue;
		};

		struct { // T_CONCAT
			uint32_t concat1, concat2;
			size_t concatBytes;
		};

		struct { // T_ERR
			bool success;
			Value errorValue;
		};

		struct { // T_ANYTYPE
			Node *anyType;
			Value anyValue;
		};
	};
} HeapEntry;

typedef struct CoroutineState {
	Value *localVariables;
	bool *localVariableIsManaged;
	size_t localVariableCount;
	size_t localVariablesAllocated;
	Value stack[50]; // TODO Merge with variables?
	bool stackIsManaged[50];
	uintptr_t stackPointer;
	size_t stackEntriesAllocated;
	BackTraceItem backTrace[300];
	uintptr_t backTracePointer;
	uint64_t id;
	int64_t unblockedBy;
	struct CoroutineState *nextCoroutine;
	struct CoroutineState **previousCoroutineLink;
	struct CoroutineState *nextUnblockedCoroutine;
	struct CoroutineState **previousUnblockedCoroutineLink;
	struct CoroutineState *nextExternalCoroutine;
	struct CoroutineState **waiters;
	size_t waiterCount;
	size_t waitersAllocated;
	struct CoroutineState ***waitingOn;
	size_t waitingOnCount;
	bool awaiting, startedByAsync, externalCoroutine;
	uintptr_t instructionPointer;
	uintptr_t variableBase;
	Value externalCoroutineData;
	void *externalCoroutineData2;

	// Data stored for library calls:
	uintptr_t parameterCount;
	Value returnValue;
	int returnValueType;
} CoroutineState;

typedef struct ExecutionContext {
	Value *globalVariables;
	bool *globalVariableIsManaged;
	size_t globalVariableCount;

	HeapEntry *heap;
	uintptr_t heapFirstUnusedEntry;
	size_t heapEntriesAllocated;

	FunctionBuilder *functionData; // Cleanup the relations between ExecutionContext, FunctionBuilder, Tokenizer and ImportData.
	Node *rootNode; // Only valid during script loading.
	char *scriptPersistFile;
	struct ImportData *mainModule;

	CoroutineState *c; // Active coroutine.
	CoroutineState *allCoroutines;
	CoroutineState *unblockedCoroutines;
	uint64_t lastCoroutineID;
	uint32_t externalCoroutineCount;
} ExecutionContext;

typedef struct ExternalFunction {
	const char *cName;
	int (*callback)(ExecutionContext *context, Value *returnValue);
} ExternalFunction;

typedef struct ImportData {
	char *path;
	size_t pathBytes;
	void *fileData;
	size_t fileDataBytes;
	uintptr_t globalVariableOffset;
	struct ImportData *nextImport;
	struct ImportData *parentImport;
	Node *rootNode;
	void *library;
} ImportData;

Node globalExpressionTypeVoid = { .type = T_VOID };
Node globalExpressionTypeInt = { .type = T_INT };
Node globalExpressionTypeFloat = { .type = T_FLOAT };
Node globalExpressionTypeBool = { .type = T_BOOL };
Node globalExpressionTypeStr = { .type = T_STR };
Node globalExpressionTypeIntList = { .type = T_LIST, .firstChild = &globalExpressionTypeInt };
Node globalExpressionTypeErrVoid = { .type = T_ERR, .firstChild = &globalExpressionTypeVoid };

// Global variables:
const char *startFunction = "Start";
size_t startFunctionBytes = 5;
char **options;
bool *optionsMatched;
size_t optionCount;
int debugBytecodeLevel;
ImportData *importedModules;
ImportData **importedModulesLink = &importedModules;

// Forward declarations:
Node *ParseBlock(Tokenizer *tokenizer, bool replMode);
Node *ParseExpression(Tokenizer *tokenizer, bool allowAssignment, uint8_t precedence);
void ScriptPrintNode(Node *node, int indent);
bool ScriptLoad(Tokenizer tokenizer, ExecutionContext *context, ImportData *importData, bool replMode);
void ScriptFreeCoroutine(CoroutineState *c);
uintptr_t HeapAllocate(ExecutionContext *context);

// --------------------------------- Platform layer definitions.

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
#include <assert.h>
#define Assert assert
#endif

void *AllocateFixed(size_t bytes);
void *AllocateResize(void *old, size_t bytes);
int MemoryCompare(const void *a, const void *b, size_t bytes);
void MemoryCopy(void *a, const void *b, size_t bytes);
size_t PrintIntegerToBuffer(char *buffer, size_t bufferBytes, int64_t i); // TODO This shouldn't be in the platform layer.
size_t PrintFloatToBuffer(char *buffer, size_t bufferBytes, double f); // TODO This shouldn't be in the platform layer.
void PrintDebug(const char *format, ...);
void PrintError(Tokenizer *tokenizer, const char *format, ...);
void PrintError2(Tokenizer *tokenizer, Node *node, const char *format, ...);
void PrintError3(const char *format, ...);
void PrintError4(ExecutionContext *context, uint32_t instructionPointer, const char *format, ...);
void PrintBackTrace(ExecutionContext *context, uint32_t instructionPointer, CoroutineState *c, const char *prefix);
void *FileLoad(const char *path, size_t *length);
CoroutineState *ExternalCoroutineWaitAny(ExecutionContext *context);
void ExternalPassREPLResult(ExecutionContext *context, Value value);
void *LibraryLoad(const char *name);
void *LibraryGetAddress(void *library, const char *name);

// --------------------------------- Base module.

char baseModuleSource[] = {
	// TODO Temporary.
	"struct DirectoryChild { str name; bool isDirectory; int size; };"
	"DirectoryChild[] Dir() { str[] names = DirectoryEnumerate(\"0:\"):assert(); DirectoryChild[] result = new DirectoryChild[]; result:resize(names:len()); for int i = 0; i < names:len(); i += 1 { result[i] = new DirectoryChild; result[i].name = names[i]; result[i].isDirectory = PathIsDirectory(\"0:/\"+names[i]); result[i].size = FileGetSize(\"0:/\"+names[i]):default(-1); } return result;}"
	"str[] OpenDocumentEnumerate() #extcall;"

	// Logging:

	"void Log(str x) #extcall;"
	"void LogInfo(str x) { Log(TextColorHighlight() + x); }"
	"void LogError(str x) { Log(TextColorError() + \"Error: \" + x); }"

	"void LogOpenGroup(str title) #extcall;"
	// TODO LogOpenList, LogOpenTable, etc.
	"void LogClose() #extcall;"

	"str TextColorError() #extcall;"
	"str TextColorHighlight() #extcall;"
	"str TextWeight(int i) #extcall;"
	"str TextMonospaced() #extcall;"
	"str TextPlain() #extcall;"
	"str TextBold() { return TextWeight(700); }"

	// String operations:

	"str StringSlice(str x, int start, int end) #extcall;"
	"int CharacterToByte(str x) #extcall;"
	"bool StringContains(str haystack, str needle) {"
	"	for int i = 0; i <= haystack:len() - needle:len(); i += 1 {"
	"		bool match = true;"
	"		for int j = 0; j < needle:len(); j += 1 { if haystack[i + j] != needle[j] match = false; }"
	"		if match { return true; }"
	"	}"
	"	return false;"
	"}"
	"str StringTrim(str string) {"
	"	int start = 0;"
	"	int end = string:len();"
	"	while start != end && (string[start] == \" \" || string[start] == \"\\t\" || string[start] == \"\\r\" || string[start] == \"\\n\") { start += 1; }"
	"	while start != end && (string[end - 1] == \" \" || string[end - 1] == \"\\t\" || string[end - 1] == \"\\r\" || string[end - 1] == \"\\n\") { end -= 1; }"
	"	return StringSlice(string, start, end);"
	"}"
	"str[] StringSplitByCharacter(str string, str character, bool includeEmptyString) {"
	"	assert character:len() == 1;"
	"	str[] list = new str[];"
	"	int x = 0;"
	"	for int i = 0; i < string:len(); i += 1 {"
	"		if string[i] == character {"
	"			if (includeEmptyString || x != i) { list:add(StringSlice(string, x, i)); }"
	"			x = i + 1;"
	"		}"
	"	}"
	"	if (includeEmptyString || x != string:len()) { list:add(StringSlice(string, x, string:len())); }"
	"	return list;"
	"}"
	"str StringJoin(str[] strings, str k, bool trailing) {"
	"	str c = \"\";"
	"	for int i = 0; i < strings:len(); i += 1 {"
	"		if i < strings:len() - 1 || trailing { c = c + strings[i] + k; }"
	"		else { c = c + strings[i]; }"
	"	}"
	"	return c;"
	"}"
	"bool StringEndsWith(str s, str x) { return s:len() >= x:len() && StringSlice(s, s:len() - x:len(), s:len()) == x; }"
	"bool StringStartsWith(str s, str x) { return s:len() >= x:len() && StringSlice(s, 0, x:len()) == x; }"
	"bool CharacterIsAlnum(str c) {"
	"	int b = CharacterToByte(c);"
	"	return (b >= CharacterToByte(\"A\") && b <= CharacterToByte(\"Z\")) || (b >= CharacterToByte(\"a\") && b <= CharacterToByte(\"z\"))"
	"		|| (b >= CharacterToByte(\"0\") && b <= CharacterToByte(\"9\"));"
	"}"

	// Miscellaneous:

	"int SystemGetProcessorCount() #extcall;"
	"bool SystemRunningAsAdministrator() #extcall;"
	"str SystemGetHostName() #extcall;"
	"void SystemSleepMs(int ms) #extcall;"
	"int RandomInt(int min, int max) #extcall;"

	"str UUIDGenerate() {"
	"	str hexChars = \"0123456789abcdef\";"
	"	str result;"
	"	for int i = 0; i <  8; i += 1 { result += hexChars[RandomInt(0, 15)]; }"
	"	result += \"-\";"
	"	for int i = 0; i <  4; i += 1 { result += hexChars[RandomInt(0, 15)]; }"
	"	result += \"-4\";"
	"	for int i = 0; i <  3; i += 1 { result += hexChars[RandomInt(0, 15)]; }"
	"	result += \"-\" + hexChars[RandomInt(8, 11)];"
	"	for int i = 0; i <  3; i += 1 { result += hexChars[RandomInt(0, 15)]; }"
	"	result += \"-\";"
	"	for int i = 0; i < 12; i += 1 { result += hexChars[RandomInt(0, 15)]; }"
	"	return result;"
	"}"

	// File system access:

	"bool PathExists(str x) #extcall;"
	"bool PathIsFile(str source) #extcall;"
	"bool PathIsDirectory(str source) #extcall;"
	"bool PathIsLink(str source) #extcall;"
	"err[void] PathCreateDirectory(str x) #extcall;"
	"err[void] PathDelete(str x) #extcall;"
	"err[void] PathMove(str source, str destination) #extcall;"
	"str PathGetDefaultPrefix() #extcall;"
	"err[void] PathSetDefaultPrefixToScriptSourceDirectory() #extcall;"
	"err[str] FileReadAll(str path) #extcall;"
	"err[void] FileWriteAll(str path, str x) #extcall;"
	"err[void] FileAppend(str path, str x) #extcall;"
	"err[void] FileCopy(str source, str destination) #extcall;"
	"err[int] FileGetSize(str path) #extcall;"
					      
	"err[void] _DirectoryInternalStartIteration(str path) #extcall;"
	"str _DirectoryInternalNextIteration() #extcall;"
	"void _DirectoryInternalEndIteration() #extcall;"
	"err[void] _DirectoryInternalEnumerateChildren(str path, str prefix, str[] result, bool recurse) {"
	"	reterr _DirectoryInternalStartIteration(path);"
	"	str child = _DirectoryInternalNextIteration();"
	"	int start = result:len();"
	"	while child != \"\" { result:add(child); child = _DirectoryInternalNextIteration(); }"
	"	_DirectoryInternalEndIteration();"
	"	int end = result:len();"
	"	if recurse {"
	"		for int i = start; i < end; i += 1 {"
	"			str actual = path + \"/\" + result[i];"
	"			if PathIsDirectory(actual) {"
	"				_DirectoryInternalEnumerateChildren(actual, prefix + result[i] + \"/\", result, true);"
	"			}"
	"		}"
	"	}"
	"	for int i = start; i < end; i += 1 {"
	"		result[i] = prefix + result[i];"
	"	}"
	"	return #success;"
	"}"
	"err[str[]] DirectoryEnumerate(str path) {"
	"	str[] result = new str[];"
	"	err[void] e = _DirectoryInternalEnumerateChildren(path, \"\", result, false);"
	"	reterr e; return result;"
	"}"
	"err[str[]] DirectoryEnumerateRecursively(str path) {"
	"	str[] result = new str[];"
	"	err[void] e = _DirectoryInternalEnumerateChildren(PathTrimTrailingSlash(path), \"\", result, true);"
	"	reterr e; return result;"
	"}"

	"str PathTrimTrailingSlash(str x) { if x:len() > 0 && x[x:len() - 1] == \"/\" { return StringSlice(x, 0, x:len() - 1); } return x; }"

	"err[void] PathDeleteRecursively(str path) {"
	"	err[str[]] e = DirectoryEnumerateRecursively(path);"
	"	if str[] all in e {"
	"		for int i = 0; i < all:len(); i += 1 {"
	"			str p = path + \"/\" + all[i];"
	"			if PathIsFile(p) || PathIsLink(p) {"
	"				PathDelete(p);"
	"			}"
	"		}"
	"		for int i = all:len(); i > 0; i -= 1 {"
	"			str p = path + \"/\" + all[i - 1];"
	"			if PathIsDirectory(p) {"
	"				PathDelete(p);"
	"			}"
	"		}"
	"		return PathDelete(path);"
	"	} else {"
	"		return new err[void] e:error();"
	"	}"
	"}"
	"err[void] PathCopyRecursively(str source, str destination) {"
	"	err[str[]] e = DirectoryEnumerateRecursively(source);"
	"	if str[] all in e {"
	"		reterr PathCreateDirectory(destination);"
	"		for int i = 0; i < all:len(); i += 1 {"
	"			str sourceItem = source + \"/\" + all[i];"
	"			str destinationItem = destination + \"/\" + all[i];"
	"			if PathIsDirectory(sourceItem) {"
	"				PathCreateDirectory(destinationItem);"
	"			} else {"
	"				reterr FileCopy(sourceItem, destinationItem);"
	"			}"
	"		}"
	"		return #success;"
	"	} else {"
	"		return new err[void] e:error();"
	"	}"
	"}"
	"err[void] PathCopyInto(str sourceDirectory, str item, str destinationDirectory) {"
	"	str sourceItem = sourceDirectory + \"/\"+ item;"
	"	str destinationItem = destinationDirectory + \"/\"+ item;"
	"	PathCreateLeadingDirectories(PathGetLeadingDirectories(destinationItem));"
	"	if PathIsDirectory(sourceItem) {"
	"		return PathCreateDirectory(destinationItem);"
	"	} else {"
	"		return FileCopy(sourceItem, destinationItem);"
	"	}"
	"}"
	"err[void] PathCopyAllInto(str sourceDirectory, str[] items, str destinationDirectory) {"
	"	for int i = 0; i < items:len(); i += 1 {"
	"		reterr PathCopyInto(sourceDirectory, items[i], destinationDirectory);"
	"	}"
	"	return #success;"
	"}"
	"err[void] PathCopyFilteredInto(str sourceDirectory, str[] filter, int filterWildcard, str destinationDirectory) {"
	"	assert filter:len() > 0;"
	"	err[str[]] e;"
	"	if filterWildcard != -1 || filter:len() > 1 { e = DirectoryEnumerateRecursively(sourceDirectory); }"
	"	else { e = DirectoryEnumerate(sourceDirectory); }"
	"	if str[] items in e {"
	"		for int i = 0; i < items:len(); i += 1 {"
	"			if PathMatchesFilter(items[i], filter, filterWildcard) {"
	"				reterr PathCopyInto(sourceDirectory, items[i], destinationDirectory);"
	"			}"
	"		}"
	"		return #success;"
	"	} else {"
	"		return new err[void] e:error();"
	"	}"
	"}"

	"bool PathMatchesFilter(str path, str[] filterComponents, int _filterWildcard) {"
	"	str[] pathComponents = StringSplitByCharacter(path, \"/\", false);"
	"	int filterWildcard = _filterWildcard;"
	"	if filterWildcard == -1 { "
	"		filterWildcard = pathComponents:len(); "
	"	}"
	"	if filterComponents:len() > pathComponents:len() { "
	"		return false; "
	"	}"
	"	for int i = 0; i < filterComponents:len(); i += 1 {"
	"		str filterComponent = filterComponents[i];"
	"		str pathComponent;"
	"		int wildcard = filterComponent:len();"
	"		if i < filterWildcard { "
	"			pathComponent = pathComponents[i]; "
	"		} else { "
	"			pathComponent = pathComponents[pathComponents:len() - filterComponents:len() + i]; "
	"		}"
	"		for int j = 0; j < filterComponent:len(); j += 1 { "
	"			if filterComponent[j] == \"*\" { "
	"				wildcard = j; "
	"				if pathComponent:len() < filterComponent:len() - 1 {"
	"					return false;"
	"				}"
	"			} "
	"		}"
	"		for int j = 0; j < filterComponent:len(); j += 1 {"
	"			if j < wildcard {"
	"				if filterComponent[j] != pathComponent[j] { return false; }"
	"			} else if j > wildcard {"
	"				if filterComponent[j] != pathComponent[pathComponent:len() - filterComponent:len() + j] { return false; }"
	"			}"
	"		}"
	"	}"
	"	return true;"
	"}"
	"str PathGetLeadingDirectories(str path) {"
	"	for int i = path:len() - 1; i >= 0; i -= 1 {"
	"		if path[i] == \"/\" {"
	"			return StringSlice(path, 0, i);"
	"		}"
	"	}"
	"	return \"\";"
	"}"
	"err[void] PathCreateLeadingDirectories(str path) {"
	"	for int i = 0; i < path:len(); i += 1 {"
	"		if path[i] == \"/\" {"
	"			PathCreateDirectory(StringSlice(path, 0, i));"
	"		}"
	"	}"
	"	return PathCreateDirectory(path);"
	"}"

	// Persistent variables:

	"bool PersistRead(str path) #extcall;"

	// Command line:

	"str ConsoleGetLine() #extcall;"
	"err[str] SystemGetEnvironmentVariable(str name) #extcall;"
	"err[void] SystemSetEnvironmentVariable(str name, str value) #extcall;"
	"bool SystemShellExecute(str x) #extcall;" // Returns true on success.
	"bool SystemShellExecuteWithWorkingDirectory(str wd, str x) #extcall;" // Returns true on success.
	"str SystemShellEvaluate(str x) #extcall;" // Captures stdout.
	"void SystemShellEnableLogging(bool x) #extcall;"
};

// --------------------------------- External function calls.

int ExternalLog(ExecutionContext *context, Value *returnValue);
int ExternalLogOpenGroup(ExecutionContext *context, Value *returnValue);
int ExternalLogClose(ExecutionContext *context, Value *returnValue);
int ExternalTextColorError(ExecutionContext *context, Value *returnValue);
int ExternalTextColorHighlight(ExecutionContext *context, Value *returnValue);
int ExternalTextWeight(ExecutionContext *context, Value *returnValue);
int ExternalTextMonospaced(ExecutionContext *context, Value *returnValue);
int ExternalTextPlain(ExecutionContext *context, Value *returnValue);
int ExternalConsoleGetLine(ExecutionContext *context, Value *returnValue);
int ExternalStringSlice(ExecutionContext *context, Value *returnValue);
int ExternalCharacterToByte(ExecutionContext *context, Value *returnValue);
int ExternalSystemShellExecute(ExecutionContext *context, Value *returnValue);
int ExternalSystemShellExecuteWithWorkingDirectory(ExecutionContext *context, Value *returnValue);
int ExternalSystemShellEvaluate(ExecutionContext *context, Value *returnValue);
int ExternalSystemShellEnableLogging(ExecutionContext *context, Value *returnValue);
int ExternalSystemGetProcessorCount(ExecutionContext *context, Value *returnValue);
int ExternalSystemGetEnvironmentVariable(ExecutionContext *context, Value *returnValue);
int ExternalSystemSetEnvironmentVariable(ExecutionContext *context, Value *returnValue);
int ExternalSystemRunningAsAdministrator(ExecutionContext *context, Value *returnValue);
int ExternalSystemGetHostName(ExecutionContext *context, Value *returnValue);
int ExternalSystemSleepMs(ExecutionContext *context, Value *returnValue);
int ExternalPathCreateDirectory(ExecutionContext *context, Value *returnValue);
int ExternalPathDelete(ExecutionContext *context, Value *returnValue);
int ExternalPathExists(ExecutionContext *context, Value *returnValue);
int ExternalPathIsFile(ExecutionContext *context, Value *returnValue);
int ExternalPathIsDirectory(ExecutionContext *context, Value *returnValue);
int ExternalPathIsLink(ExecutionContext *context, Value *returnValue);
int ExternalPathMove(ExecutionContext *context, Value *returnValue);
int ExternalPathGetDefaultPrefix(ExecutionContext *context, Value *returnValue);
int ExternalPathSetDefaultPrefixToScriptSourceDirectory(ExecutionContext *context, Value *returnValue);
int ExternalFileReadAll(ExecutionContext *context, Value *returnValue);
int ExternalFileWriteAll(ExecutionContext *context, Value *returnValue);
int ExternalFileAppend(ExecutionContext *context, Value *returnValue);
int ExternalFileCopy(ExecutionContext *context, Value *returnValue);
int ExternalFileGetSize(ExecutionContext *context, Value *returnValue);
int ExternalPersistRead(ExecutionContext *context, Value *returnValue);
int ExternalPersistWrite(ExecutionContext *context, Value *returnValue);
int ExternalRandomInt(ExecutionContext *context, Value *returnValue); // TODO This shouldn't be in the platform layer.
int External_DirectoryInternalStartIteration(ExecutionContext *context, Value *returnValue);
int External_DirectoryInternalNextIteration(ExecutionContext *context, Value *returnValue);
int External_DirectoryInternalEndIteration(ExecutionContext *context, Value *returnValue);
int ExternalOpenDocumentEnumerate(ExecutionContext *context, Value *returnValue);

ExternalFunction externalFunctions[] = {
	{ .cName = "Log", .callback = ExternalLog },
	{ .cName = "LogOpenGroup", .callback = ExternalLogOpenGroup },
	{ .cName = "LogClose", .callback = ExternalLogClose },
	{ .cName = "TextColorError", .callback = ExternalTextColorError },
	{ .cName = "TextColorHighlight", .callback = ExternalTextColorHighlight },
	{ .cName = "TextWeight", .callback = ExternalTextWeight },
	{ .cName = "TextMonospaced", .callback = ExternalTextMonospaced },
	{ .cName = "TextPlain", .callback = ExternalTextPlain },
	{ .cName = "ConsoleGetLine", .callback = ExternalConsoleGetLine },
	{ .cName = "StringSlice", .callback = ExternalStringSlice },
	{ .cName = "CharacterToByte", .callback = ExternalCharacterToByte },
	{ .cName = "SystemShellExecute", .callback = ExternalSystemShellExecute },
	{ .cName = "SystemShellExecuteWithWorkingDirectory", .callback = ExternalSystemShellExecuteWithWorkingDirectory },
	{ .cName = "SystemShellEvaluate", .callback = ExternalSystemShellEvaluate },
	{ .cName = "SystemShellEnableLogging", .callback = ExternalSystemShellEnableLogging },
	{ .cName = "SystemGetProcessorCount", .callback = ExternalSystemGetProcessorCount },
	{ .cName = "SystemGetEnvironmentVariable", .callback = ExternalSystemGetEnvironmentVariable },
	{ .cName = "SystemSetEnvironmentVariable", .callback = ExternalSystemSetEnvironmentVariable },
	{ .cName = "SystemRunningAsAdministrator", .callback = ExternalSystemRunningAsAdministrator },
	{ .cName = "SystemGetHostName", .callback = ExternalSystemGetHostName },
	{ .cName = "SystemSleepMs", .callback = ExternalSystemSleepMs },
	{ .cName = "PathExists", .callback = ExternalPathExists },
	{ .cName = "PathIsFile", .callback = ExternalPathIsFile },
	{ .cName = "PathIsDirectory", .callback = ExternalPathIsDirectory },
	{ .cName = "PathIsLink", .callback = ExternalPathIsLink },
	{ .cName = "PathCreateDirectory", .callback = ExternalPathCreateDirectory },
	{ .cName = "PathDelete", .callback = ExternalPathDelete },
	{ .cName = "PathMove", .callback = ExternalPathMove },
	{ .cName = "PathGetDefaultPrefix", .callback = ExternalPathGetDefaultPrefix },
	{ .cName = "PathSetDefaultPrefixToScriptSourceDirectory", .callback = ExternalPathSetDefaultPrefixToScriptSourceDirectory },
	{ .cName = "FileReadAll", .callback = ExternalFileReadAll },
	{ .cName = "FileWriteAll", .callback = ExternalFileWriteAll },
	{ .cName = "FileAppend", .callback = ExternalFileAppend },
	{ .cName = "FileCopy", .callback = ExternalFileCopy },
	{ .cName = "FileGetSize", .callback = ExternalFileGetSize },
	{ .cName = "PersistRead", .callback = ExternalPersistRead },
	{ .cName = "PersistWrite", .callback = ExternalPersistWrite },
	{ .cName = "RandomInt", .callback = ExternalRandomInt },
	{ .cName = "_DirectoryInternalStartIteration", .callback = External_DirectoryInternalStartIteration },
	{ .cName = "_DirectoryInternalNextIteration", .callback = External_DirectoryInternalNextIteration },
	{ .cName = "_DirectoryInternalEndIteration", .callback = External_DirectoryInternalEndIteration },
	{ .cName = "OpenDocumentEnumerate", .callback = ExternalOpenDocumentEnumerate },
};

// --------------------------------- Tokenization and parsing.

uint8_t TokenLookupPrecedence(uint8_t t) {
	if (t == T_EQUALS)          return 10;
	if (t == T_ADD_EQUALS)      return 10;
	if (t == T_MINUS_EQUALS)    return 10;
	if (t == T_ASTERISK_EQUALS) return 10;
	if (t == T_SLASH_EQUALS)    return 10;
	if (t == T_COMMA)           return 12;
	if (t == T_LOGICAL_OR)      return 14;
	if (t == T_LOGICAL_AND)     return 15;
	if (t == T_BITWISE_OR)      return 16;
	if (t == T_BITWISE_XOR)     return 17;
	if (t == T_BITWISE_AND)     return 18;
	if (t == T_DOUBLE_EQUALS)   return 20;
	if (t == T_NOT_EQUALS)      return 20;
	if (t == T_GREATER_THAN)    return 25;
	if (t == T_LESS_THAN)       return 25;
	if (t == T_GT_OR_EQUAL)     return 25;
	if (t == T_LT_OR_EQUAL)     return 25;
	if (t == T_BIT_SHIFT_LEFT)  return 30;
	if (t == T_BIT_SHIFT_RIGHT) return 30;
	if (t == T_ADD)             return 50;
	if (t == T_MINUS)           return 50;
	if (t == T_ASTERISK)        return 60;
	if (t == T_SLASH)           return 60;
	if (t == T_LOGICAL_NOT)     return 70;
	if (t == T_BITWISE_NOT)     return 70;
	if (t == T_NEGATE)          return 70;
	if (t == T_NEW)             return 75;
	if (t == T_DOT)             return 80;
	if (t == T_COLON)           return 80;
	if (t == T_AWAIT)           return 90;
	if (t == T_LEFT_ROUND)      return 100;
	Assert(false);
	return 0;
}

Token TokenNext(Tokenizer *tokenizer) {
	// TODO Block comments.

	Token token = { 0 };
	token.type = T_ERROR;
	token.module = tokenizer->module;

	while (true) {
		if (tokenizer->position == tokenizer->inputBytes) {
			token.type = T_EOF;
			break;
		}

		uint8_t c = tokenizer->input[tokenizer->position];
		uint8_t c1 = tokenizer->position + 1 == tokenizer->inputBytes ? 0 : tokenizer->input[tokenizer->position + 1];
		token.text = &tokenizer->input[tokenizer->position];
		token.textBytes = 1;
		token.line = tokenizer->line;

		if (c == ' ' || c == '\t' || c == '\r') { 
			tokenizer->position++; 
			continue; 
		} else if (c == '\n') {
			tokenizer->position++; 
			tokenizer->line++;
			continue; 
		} else if (c == '/' && c1 == '/') {
			while (tokenizer->position != tokenizer->inputBytes && tokenizer->input[tokenizer->position] != '\n') {
				tokenizer->position++;
			}

			continue;
		}

		else if (c == '<' && c1 == '=' && (tokenizer->position += 2)) token.type = T_LT_OR_EQUAL;
		else if (c == '>' && c1 == '=' && (tokenizer->position += 2)) token.type = T_GT_OR_EQUAL;
		else if (c == '=' && c1 == '=' && (tokenizer->position += 2)) token.type = T_DOUBLE_EQUALS;
		else if (c == '!' && c1 == '=' && (tokenizer->position += 2)) token.type = T_NOT_EQUALS;
		else if (c == '&' && c1 == '&' && (tokenizer->position += 2)) token.type = T_LOGICAL_AND;
		else if (c == '|' && c1 == '|' && (tokenizer->position += 2)) token.type = T_LOGICAL_OR;
		else if (c == '+' && c1 == '=' && (tokenizer->position += 2)) token.type = T_ADD_EQUALS;
		else if (c == '-' && c1 == '=' && (tokenizer->position += 2)) token.type = T_MINUS_EQUALS;
		else if (c == '*' && c1 == '=' && (tokenizer->position += 2)) token.type = T_ASTERISK_EQUALS;
		else if (c == '/' && c1 == '=' && (tokenizer->position += 2)) token.type = T_SLASH_EQUALS;
		else if (c == '<' && c1 == '<' && (tokenizer->position += 2)) token.type = T_BIT_SHIFT_LEFT;
		else if (c == '>' && c1 == '>' && (tokenizer->position += 2)) token.type = T_BIT_SHIFT_RIGHT;
		else if (c == '~' && c1 == '|' && (tokenizer->position += 2)) token.type = T_BITWISE_XOR;
		else if (c == '+' && ++tokenizer->position) token.type = T_ADD;
		else if (c == '-' && ++tokenizer->position) token.type = T_MINUS;
		else if (c == '*' && ++tokenizer->position) token.type = T_ASTERISK;
		else if (c == '/' && ++tokenizer->position) token.type = T_SLASH;
		else if (c == '(' && ++tokenizer->position) token.type = T_LEFT_ROUND;
		else if (c == ')' && ++tokenizer->position) token.type = T_RIGHT_ROUND;
		else if (c == '[' && ++tokenizer->position) token.type = T_LEFT_SQUARE;
		else if (c == ']' && ++tokenizer->position) token.type = T_RIGHT_SQUARE;
		else if (c == '{' && ++tokenizer->position) token.type = T_LEFT_FANCY;
		else if (c == '}' && ++tokenizer->position) token.type = T_RIGHT_FANCY;
		else if (c == ',' && ++tokenizer->position) token.type = T_COMMA;
		else if (c == ';' && ++tokenizer->position) token.type = T_SEMICOLON;
		else if (c == '=' && ++tokenizer->position) token.type = T_EQUALS;
		else if (c == '<' && ++tokenizer->position) token.type = T_LESS_THAN;
		else if (c == '>' && ++tokenizer->position) token.type = T_GREATER_THAN;
		else if (c == '%' && ++tokenizer->position) token.type = T_STR_INTERPOLATE;
		else if (c == '.' && ++tokenizer->position) token.type = T_DOT;
		else if (c == ':' && ++tokenizer->position) token.type = T_COLON;
		else if (c == '!' && ++tokenizer->position) token.type = T_LOGICAL_NOT;
		else if (c == '|' && ++tokenizer->position) token.type = T_BITWISE_OR;
		else if (c == '&' && ++tokenizer->position) token.type = T_BITWISE_AND;
		else if (c == '~' && ++tokenizer->position) token.type = T_BITWISE_NOT;

		else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c == '_') || (c >= 0x80) || c == '#') {
			token.textBytes = 0;
			token.type = T_IDENTIFIER;
			token.text = tokenizer->input + tokenizer->position;

			while ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') 
					|| (c == '_') || (c >= 0x80) || (c == '#' && !token.textBytes)) {
				tokenizer->position++;
				token.textBytes++;
				if (tokenizer->position == tokenizer->inputBytes) break;
				c = tokenizer->input[tokenizer->position];
			}

#define KEYWORD(x) (token.textBytes == sizeof(x) - 1 && 0 == MemoryCompare(x, token.text, token.textBytes))
			if (false) {}
			else if KEYWORD("#extcall") token.type = T_EXTCALL;
			else if KEYWORD("#import") token.type = T_IMPORT;
			else if KEYWORD("#inline") token.type = T_INLINE;
			else if KEYWORD("#library") token.type = T_LIBRARY;
			else if KEYWORD("#option") token.type = T_OPTION;
			else if KEYWORD("#persist") token.type = T_PERSIST;
			else if KEYWORD("#success") token.type = T_SUCCESS;
			else if KEYWORD("anytype") token.type = T_ANYTYPE;
			else if KEYWORD("assert") token.type = T_ASSERT;
			else if KEYWORD("await") token.type = T_AWAIT;
			else if KEYWORD("bool") token.type = T_BOOL;
			else if KEYWORD("else") token.type = T_ELSE;
			else if KEYWORD("err") token.type = T_ERR;
			else if KEYWORD("false") token.type = T_FALSE;
			else if KEYWORD("float") token.type = T_FLOAT;
			else if KEYWORD("for") token.type = T_FOR;
			else if KEYWORD("functype") token.type = T_FUNCTYPE;
			else if KEYWORD("handletype") token.type = T_HANDLETYPE;
			else if KEYWORD("if") token.type = T_IF;
			else if KEYWORD("in") token.type = T_IN;
			else if KEYWORD("int") token.type = T_INT;
			else if KEYWORD("inttype") token.type = T_INTTYPE;
			else if KEYWORD("new") token.type = T_NEW;
			else if KEYWORD("null") token.type = T_NULL;
			else if KEYWORD("reterr") token.type = T_RETERR;
			else if KEYWORD("return") token.type = T_RETURN;
			else if KEYWORD("str") token.type = T_STR;
			else if KEYWORD("struct") token.type = T_STRUCT;
			else if KEYWORD("true") token.type = T_TRUE;
			else if KEYWORD("tuple") token.type = T_TUPLE;
			else if KEYWORD("void") token.type = T_VOID;
			else if KEYWORD("while") token.type = T_WHILE;

			else if (token.text[0] == '#') {
				PrintError(tokenizer, "Unrecognised #-token '%.*s'.\n", token.textBytes, token.text);
				tokenizer->error = true;
				token.type = T_ERROR;
				break;
			}
		} else if (c >= '0' && c <= '9') {
			// TODO Exponent notation.

			token.textBytes = 0;
			token.type = T_NUMERIC_LITERAL;
			token.text = tokenizer->input + tokenizer->position;

			while ((c >= '0' && c <= '9') || (c == '.')) {
				tokenizer->position++;
				token.textBytes++;
				if (tokenizer->position == tokenizer->inputBytes) break;
				c = tokenizer->input[tokenizer->position];
			}

			if (token.textBytes == 1 && token.text[0] == '0') {
				token.type = T_ZERO;
			}
		} else if (c == '"') {
			// TODO Escape sequence to insert an arbitrary codepoint.

			bool inInterpolation = false;
			intptr_t startPosition = ++tokenizer->position;
			intptr_t endPosition = -1;
			
			for (uintptr_t i = tokenizer->position; true; i++) {
				if (inInterpolation) {
					if (tokenizer->input[i] == '%') {
						inInterpolation = false;
					} else if (tokenizer->input[i] == '"') {
						PrintError(tokenizer, "Strings are not allowed within a string interpolation expression.\n");
						tokenizer->error = true;
						break;
					} else if (tokenizer->input[i] == '\n') {
						PrintError(tokenizer, "String interpolation expressions must stay on a single line.\n");
						tokenizer->error = true;
						break;
					} else {
						token.textBytes++;
					}
				} else if (i == tokenizer->inputBytes || tokenizer->input[i] == '\n') {
					PrintError(tokenizer, "String does not end before the end of the line.\n");
					tokenizer->error = true;
					break;
				} else if (tokenizer->input[i] == '"') {
					endPosition = i;
					break;
				} else if (tokenizer->input[i] == '%') {
					inInterpolation = true;
				} else if (tokenizer->input[i] == '\\') {
					if (i + 1 == tokenizer->inputBytes 
							|| (tokenizer->input[i + 1] != 'n' && tokenizer->input[i + 1] != 't' 
								&& tokenizer->input[i + 1] != 'r' 
								&& tokenizer->input[i + 1] != '%' 
								&& tokenizer->input[i + 1] != '"' && tokenizer->input[i + 1] != '\\')) {
						PrintError(tokenizer, "String contains unrecognized escape sequence '\\%c'. "
								"Possibilities are: '\\\\', '\\%%', '\\n', '\\r', '\\t' and '\\\"'\n", tokenizer->input[i + 1]);
						tokenizer->error = true;
						break;
					} else {
						i++;
						token.textBytes++;
					}
				} else {
					token.textBytes++;
				}
			}

			if (endPosition != -1) {
				token.text = tokenizer->input + startPosition;
				token.textBytes = endPosition - startPosition;
				token.type = T_STRING_LITERAL;
				tokenizer->position = endPosition + 1;
			}
		} else {
			PrintError(tokenizer, "Unexpected character '%c'.\n", c);
			tokenizer->error = true;
		}

		break;
	}

	return token;
}

Token TokenPeek(Tokenizer *tokenizer) {
	Tokenizer copy = *tokenizer;
	return TokenNext(&copy);
}

Node *ParseType(Tokenizer *tokenizer, bool maybe, bool allowVoid, bool allowTuple) {
	Node *node = (Node *) AllocateFixed(sizeof(Node));
	node->token = TokenNext(tokenizer);

	if (node->token.type == T_INT 
			|| node->token.type == T_FLOAT 
			|| node->token.type == T_STR 
			|| node->token.type == T_BOOL 
			|| node->token.type == T_VOID 
			|| node->token.type == T_TUPLE
			|| node->token.type == T_ERR
			|| node->token.type == T_ANYTYPE
			|| node->token.type == T_IDENTIFIER) {
		node->type = node->token.type;

		if (!allowVoid && node->type == T_VOID) {
			if (!maybe) {
				PrintError2(tokenizer, node, "The 'void' type is not allowed here.\n");
			}

			return NULL;
		}

		if (!allowTuple && node->type == T_TUPLE) {
			if (!maybe) {
				PrintError2(tokenizer, node, "The 'tuple' type is not allowed here.\n");
			}

			return NULL;
		}

		if (node->type == T_TUPLE) {
			Token token = TokenNext(tokenizer);

			if (token.type != T_LEFT_SQUARE) {
				if (!maybe) PrintError2(tokenizer, node, "Expected a '[' after 'tuple'.\n");
				return NULL;
			}

			Node **link = &node->firstChild;
			bool needComma = false;
			int count = 0;

			while (true) {
				token = TokenPeek(tokenizer);

				if (token.type == T_RIGHT_SQUARE) {
					TokenNext(tokenizer);
					break;
				} else if (needComma) {
					if (token.type == T_COMMA) {
						TokenNext(tokenizer);
					} else {
						if (!maybe) PrintError2(tokenizer, node, "Expected a ',' or ']' in the type list for 'tuple'.\n");
						return NULL;
					}
				}

				if (count == FUNCTION_MAX_ARGUMENTS) {
					if (!maybe) PrintError2(tokenizer, node, "Too many values in the tuple (maximum is %d).\n", FUNCTION_MAX_ARGUMENTS);
					return NULL;
				}

				Node *n = ParseType(tokenizer, maybe, false, false);
				if (!n) return NULL;
				*link = n;
				link = &n->sibling;
				needComma = true;
				count++;
			}

			if (!node->firstChild || !node->firstChild->sibling) {
				if (!maybe) PrintError2(tokenizer, node, "A tuple must have at least two types in its list.\n");
				return NULL;
			}
		} else if (node->type == T_ERR) {
			Token token = TokenNext(tokenizer);

			if (token.type != T_LEFT_SQUARE) {
				if (!maybe) PrintError2(tokenizer, node, "Expected a '[' after 'err'.\n");
				return NULL;
			}

			Node *n = ParseType(tokenizer, maybe, true /* allow void */, false);
			if (!n) return NULL;
			node->firstChild = n;

			token = TokenNext(tokenizer);

			if (token.type != T_RIGHT_SQUARE) {
				if (!maybe) PrintError2(tokenizer, node, "Expected a ']' after the type of 'err'.\n");
				return NULL;
			}

			if (n->type == T_ERR) {
				if (!maybe) PrintError2(tokenizer, node, "Error types cannot be nested.\n");
				return NULL;
			}
		}

		bool first = true;

		while (true) {
			Token token = TokenPeek(tokenizer);

			if (token.type == T_ERROR) {
				return NULL;
			} else if (token.type == T_LEFT_SQUARE) {
				Node *list = (Node *) AllocateFixed(sizeof(Node));
				list->type = T_LIST;
				list->token = TokenNext(tokenizer);

				if (first) {
					list->firstChild = node;
					first = false;
					node = list;
				} else {
					Node *end = node;
					while (end->firstChild->firstChild) end = end->firstChild;
					list->firstChild = end->firstChild;
					end->firstChild = list;
				}

				token = TokenNext(tokenizer);

				if (token.type == T_ERROR) {
					return NULL;
				} else if (token.type != T_RIGHT_SQUARE) {
					if (!maybe) {
						PrintError2(tokenizer, node, "Expected a ']' after the '[' in an list type.\n");
					}

					return NULL;
				}
			} else {
				break;
			}
		}

		return node;
	} else if (!maybe) {
		PrintError2(tokenizer, node, "Expected a type. This can be a builtin type or an identifier.\n");
		return NULL;
	} else {
		return NULL;
	}
}

Node *ParseCall(Tokenizer *tokenizer, Node *function) {
	Node *call = (Node *) AllocateFixed(sizeof(Node));
	call->token = TokenNext(tokenizer);
	call->type = T_CALL;
	call->firstChild = function;
	Node *arguments = (Node *) AllocateFixed(sizeof(Node));
	arguments->type = T_ARGUMENTS;
	function->sibling = arguments;
	Node **link = &arguments->firstChild;

	if (call->token.type != T_LEFT_ROUND) {
		PrintError2(tokenizer, call, "Expected a '(' to start the list of arguments.\n");
		return NULL;
	}

	while (true) {
		Token token = TokenPeek(tokenizer);

		if (token.type == T_RIGHT_ROUND) {
			TokenNext(tokenizer);
			break;
		} else if (token.type == T_ERROR) {
			return NULL;
		}

		if (arguments->firstChild) {
			Token comma = TokenNext(tokenizer);

			if (comma.type != T_COMMA) {
				Node n = { .token = comma };
				PrintError2(tokenizer, &n, "Expected a comma to separate function arguments.\n");
				return NULL;
			}
		}

		Node *argument = ParseExpression(tokenizer, false, 0);
		if (!argument) return NULL;
		*link = argument;
		link = &argument->sibling;
	}

	return call;
}

Node *ParseExpression(Tokenizer *tokenizer, bool allowAssignment, uint8_t precedence) {
	Node *node = (Node *) AllocateFixed(sizeof(Node));
	node->token = TokenNext(tokenizer);
	if (node->token.type == T_ERROR) return NULL;

	if (node->token.type == T_IDENTIFIER) {
		node->type = T_VARIABLE;
	} else if (node->token.type == T_STRING_LITERAL) {
		Node *string = node;
		string->type = string->token.type;

		const char *raw = string->token.text;
		size_t rawBytes = string->token.textBytes;

		// It's impossible for size of the string to increase.
		char *output = (char *) AllocateFixed(rawBytes);
		size_t outputPosition = 0;

		string->token.text = output;
		string->token.textBytes = 0;

		for (uintptr_t i = 0; i < rawBytes; i++) {
			char c = raw[i];

			if (c == '\\') {
				Assert(i != rawBytes - 1);
				c = raw[++i];
				Assert(outputPosition != rawBytes);
				if (c == '\\') c = '\\';
				else if (c == 'n') c = '\n';
				else if (c == 'r') c = '\r';
				else if (c == 't') c = '\t';
				else if (c == '%') c = '%';
				else if (c == '"') c = '"';
				else Assert(false);
				output[outputPosition++] = c;
				string->token.textBytes++;
			} else if (c == '%') {
				Node *stringInterpolate = (Node *) AllocateFixed(sizeof(Node));
				stringInterpolate->type = T_STR_INTERPOLATE;
				stringInterpolate->firstChild = node;
				Tokenizer t = *tokenizer;
				t.position = raw - t.input + i + 1;
				stringInterpolate->firstChild->sibling = ParseExpression(&t, false, 0);
				if (!stringInterpolate->firstChild->sibling) return NULL;
				i = t.position - (raw - t.input);
				string = (Node *) AllocateFixed(sizeof(Node));
				string->type = T_STRING_LITERAL;
				string->token.text = output + outputPosition;
				string->token.textBytes = 0;
				stringInterpolate->firstChild->sibling->sibling = string;
				node = stringInterpolate;
			} else {
				Assert(outputPosition != rawBytes);
				output[outputPosition++] = c;
				string->token.textBytes++;
			}
		}
	} else if (node->token.type == T_NUMERIC_LITERAL || node->token.type == T_SUCCESS || node->token.type == T_ZERO
			|| node->token.type == T_TRUE || node->token.type == T_FALSE || node->token.type == T_NULL) {
		node->type = node->token.type;
	} else if (node->token.type == T_LOGICAL_NOT || node->token.type == T_MINUS || node->token.type == T_BITWISE_NOT) {
		node->type = node->token.type == T_MINUS ? T_NEGATE : node->token.type;
		node->firstChild = ParseExpression(tokenizer, false, TokenLookupPrecedence(node->type));
	} else if (node->token.type == T_LEFT_ROUND) {
		node = ParseExpression(tokenizer, false, 0);
		if (!node) return NULL;

		Token token = TokenNext(tokenizer);

		if (token.type != T_RIGHT_ROUND) {
			Node n = { .token = token };
			PrintError2(tokenizer, &n, "Expected a matching closing bracket.\n");
			return NULL;
		}
	} else if (node->token.type == T_NEW) {
		node->type = T_NEW;
		node->firstChild = ParseType(tokenizer, false, false /* allow void */, false /* allow tuple */);
		node->expressionType = node->firstChild;
		if (!node->firstChild) return NULL;

		if (node->firstChild->type == T_ERR) {
			node->firstChild->sibling = ParseExpression(tokenizer, false, TokenLookupPrecedence(node->type));
			if (!node->firstChild->sibling) return NULL;
		}
	} else if (node->token.type == T_LEFT_SQUARE) {
		node->type = T_LIST_LITERAL;
		Node **link = &node->firstChild;
		bool needComma = false;

		while (true) {
			Token peek = TokenPeek(tokenizer);

			if (peek.type == T_ERROR) {
				return NULL;
			} else if (peek.type == T_RIGHT_SQUARE) {
				TokenNext(tokenizer);
				break;
			}

			if (needComma) {
				if (peek.type != T_COMMA) {
					PrintError(tokenizer, "Expected a comma between list literal items.\n");
					return NULL;
				} else {
					TokenNext(tokenizer);
				}
			}

			Node *item = ParseExpression(tokenizer, false, 0);
			if (!item) return NULL;
			*link = item;
			link = &item->sibling;
			needComma = true;
		}
	} else if (node->token.type == T_AWAIT) {
		node->type = T_AWAIT;
		node->firstChild = ParseExpression(tokenizer, false, TokenLookupPrecedence(node->token.type));
	} else {
		PrintError2(tokenizer, node, "Expected an expression. "
				"Expressions can start with a variable identifier, a string literal, a number, 'await', 'new', '-', '!', '[' or '('.\n");
		return NULL;
	}

	while (true) {
		Token token = TokenPeek(tokenizer);

		if (token.type == T_ERROR) {
			return NULL;
		} else if ((token.type == T_EQUALS || token.type == T_ADD_EQUALS || token.type == T_MINUS_EQUALS 
					|| token.type == T_ASTERISK_EQUALS || token.type == T_SLASH_EQUALS) && !allowAssignment) {
			PrintError2(tokenizer, node, "Variable assignment is not allowed within an expression.\n");
			return NULL;
		} else if (token.type == T_DOT && TokenLookupPrecedence(token.type) > precedence) {
			TokenNext(tokenizer);
			Node *operation = (Node *) AllocateFixed(sizeof(Node));
			operation->token = TokenNext(tokenizer);
			operation->type = T_DOT;
			operation->firstChild = node;
			node = operation;

			if (operation->token.type != T_IDENTIFIER) {
				PrintError2(tokenizer, node, "Expected an identifier for the struct field after '.'.\n");
				return NULL;
			}
		} else if (token.type == T_COLON && TokenLookupPrecedence(token.type) > precedence) {
			TokenNext(tokenizer);
			Token operationName = TokenNext(tokenizer);

			if (operationName.type != T_IDENTIFIER && operationName.type != T_ASSERT && operationName.type != T_FLOAT) {
				PrintError2(tokenizer, node, "Expected an identifier for the operation name after ':'.\n");
				return NULL;
			}

			if (operationName.textBytes == 4 && 0 == MemoryCompare(operationName.text, "cast", 4)) {
				Node *n = node;
				node = (Node *) AllocateFixed(sizeof(Node));
				node->token = TokenNext(tokenizer);

				if (node->token.type == T_ERROR) {
					return NULL;
				} else if (node->token.type != T_LEFT_ROUND) {
					PrintError2(tokenizer, node, "Expected a '(' before the type to cast to.\n");
					return NULL;
				}

				node->firstChild = n;
				Node *type = ParseType(tokenizer, false, false, false);
				if (!type) return NULL;
				node->firstChild->sibling = (Node *) AllocateFixed(sizeof(Node));
				node->firstChild->sibling->type = T_CAST_TYPE_WRAPPER;
				node->firstChild->sibling->firstChild = type;

				Token token = TokenNext(tokenizer);

				if (token.type == T_ERROR) {
					return NULL;
				} else if (token.type != T_RIGHT_ROUND) {
					PrintError2(tokenizer, node, "Expected a ')' after the type to cast to.\n");
					return NULL;
				}
			} else {
				node = ParseCall(tokenizer, node);
				if (!node) return NULL;
			}

			node->type = T_COLON;
			node->token = operationName;
		} else if ((token.type == T_EQUALS || token.type == T_ADD || token.type == T_MINUS || token.type == T_BITWISE_XOR
					|| token.type == T_ASTERISK       || token.type == T_SLASH
					|| token.type == T_GREATER_THAN   || token.type == T_LESS_THAN
					|| token.type == T_BIT_SHIFT_LEFT || token.type == T_BIT_SHIFT_RIGHT
					|| token.type == T_BITWISE_OR     || token.type == T_BITWISE_AND
					|| token.type == T_LT_OR_EQUAL    || token.type == T_GT_OR_EQUAL
					|| token.type == T_DOUBLE_EQUALS  || token.type == T_NOT_EQUALS
					|| token.type == T_LOGICAL_AND    || token.type == T_LOGICAL_OR) 
				&& TokenLookupPrecedence(token.type) > precedence) {
			Node *operation = (Node *) AllocateFixed(sizeof(Node));
			operation->token = TokenNext(tokenizer);
			operation->type = operation->token.type;
			operation->firstChild = node;
			node->sibling = ParseExpression(tokenizer, false, TokenLookupPrecedence(token.type));
			if (!node->sibling) return NULL;
			node = operation;
		} else if (token.type == T_ADD_EQUALS || token.type == T_MINUS_EQUALS
				|| token.type == T_ASTERISK_EQUALS || token.type == T_SLASH_EQUALS) {
			Node *operation = (Node *) AllocateFixed(sizeof(Node));
			operation->token = TokenNext(tokenizer);
			operation->type = operation->token.type - T_ADD_EQUALS + T_ADD;
			operation->firstChild = node;
			node->sibling = ParseExpression(tokenizer, false, TokenLookupPrecedence(token.type));
			if (!node->sibling) return NULL;
			Node *nodeCopy = (Node *) AllocateFixed(sizeof(Node));
			*nodeCopy = *node;
			node = operation;
			operation = (Node *) AllocateFixed(sizeof(Node));
			operation->token = token;
			operation->type = T_EQUALS;
			operation->firstChild = nodeCopy;
			operation->firstChild->sibling = node;
			node = operation;
		} else if (token.type == T_LEFT_ROUND && TokenLookupPrecedence(token.type) > precedence) {
			node = ParseCall(tokenizer, node);
			if (!node) return NULL;
		} else if (token.type == T_LEFT_SQUARE) {
			TokenNext(tokenizer);
			Node *index = (Node *) AllocateFixed(sizeof(Node));
			index->type = T_INDEX;
			index->token = token;
			index->firstChild = node;
			index->firstChild->sibling = ParseExpression(tokenizer, false, 0);
			if (!index->firstChild->sibling) return NULL;
			node = index;
			Token token = TokenNext(tokenizer);

			if (token.type != T_RIGHT_SQUARE) {
				Node n = { .token = token };
				PrintError2(tokenizer, &n, "Expected a matching closing bracket.\n");
				return NULL;
			}
		} else if (token.type == T_COMMA && TokenLookupPrecedence(token.type) >= precedence && allowAssignment) {
			Node *operation = (Node *) AllocateFixed(sizeof(Node));
			operation->token = TokenNext(tokenizer);
			operation->type = T_SET_GROUP;
			operation->firstChild = node;
			node->sibling = ParseExpression(tokenizer, true, TokenLookupPrecedence(token.type));
			if (!node->sibling) return NULL;
			node = operation;
		} else {
			break;
		}
	}

	return node;
}

Node *ParseIf(Tokenizer *tokenizer) {
	Node *node = (Node *) AllocateFixed(sizeof(Node));
	node->type = T_IF;
	node->token = TokenNext(tokenizer);
	Node **link = &node->firstChild;

	{
		Tokenizer copy = *tokenizer;
		Node *type = ParseType(tokenizer, true /* maybe */, false, false);

		if (tokenizer->error) {
			return NULL;
		} else if (type) {
			Token name = TokenNext(tokenizer);

			if (tokenizer->error) {
				return NULL;
			} else if (name.type == T_IDENTIFIER) {
				Token in = TokenNext(tokenizer);

				if (tokenizer->error) {
					return NULL;
				} else if (in.type == T_IN) {
					node->type = T_IF_ERR;

					Node *declaration = (Node *) AllocateFixed(sizeof(Node));
					declaration->type = T_DECLARE;
					declaration->token = name;
					declaration->firstChild = type;
					node->firstChild = declaration;
					link = &declaration->sibling;
				}
			}
		}

		if (node->type != T_IF_ERR) {
			*tokenizer = copy;
		}
	}

	*link = ParseExpression(tokenizer, false, 0);
	if (!(*link)) return NULL;
	Token token = TokenPeek(tokenizer);

	if (token.type == T_LEFT_FANCY) {
		TokenNext(tokenizer);
		(*link)->sibling = ParseBlock(tokenizer, false);
		if (!(*link)->sibling) return NULL;
	} else {
		Node *wrapper = (Node *) AllocateFixed(sizeof(Node));
		wrapper->type = T_BLOCK;
		wrapper->firstChild = ParseExpression(tokenizer, true, 0);
		if (!wrapper->firstChild) return NULL;
		(*link)->sibling = wrapper;

		Token semicolon = TokenNext(tokenizer);

		if (semicolon.type != T_SEMICOLON) {
			PrintError2(tokenizer, (*link)->sibling, "Expected a semicolon at the end of the expression.\n");
			return NULL;
		}
	}

	token = TokenPeek(tokenizer);

	if (token.type == T_ELSE) {
		TokenNext(tokenizer);
		token = TokenPeek(tokenizer);

		if (token.type == T_IF) {
			(*link)->sibling->sibling = ParseIf(tokenizer);
			if (!(*link)->sibling->sibling) return NULL;
		} else if (token.type == T_LEFT_FANCY) {
			TokenNext(tokenizer);
			(*link)->sibling->sibling = ParseBlock(tokenizer, false);
			if (!(*link)->sibling->sibling) return NULL;
		} else {
			(*link)->sibling->sibling = ParseExpression(tokenizer, true, 0);
			if (!(*link)->sibling->sibling) return NULL;

			Token semicolon = TokenNext(tokenizer);

			if (semicolon.type != T_SEMICOLON) {
				PrintError2(tokenizer, (*link)->sibling, "Expected a semicolon at the end of the expression.\n");
				return NULL;
			}
		}
	}

	// Make sure that the if-err variable has its own scope.
	Node *wrapper = (Node *) AllocateFixed(sizeof(Node));
	wrapper->type = T_BLOCK;
	wrapper->token = node->token;
	wrapper->firstChild = node;
	return wrapper;
}

Node *ParseVariableDeclarationOrExpression(Tokenizer *tokenizer, bool replMode, bool allowIn) {
	Tokenizer copy = *tokenizer;
	bool isVariableDeclaration = false;

	if (ParseType(tokenizer, true, false /* allow void */, false /* allow tuple */) && TokenNext(tokenizer).type == T_IDENTIFIER) {
		Token token = TokenNext(tokenizer);
		isVariableDeclaration = token.type == T_EQUALS || token.type == T_SEMICOLON || token.type == T_COMMA || token.type == T_IN;
	}

	if (tokenizer->error) {
		return NULL;
	}

	*tokenizer = copy;

	if (isVariableDeclaration) {
		// TODO Variable support in replMode.
		if (replMode) {
			PrintError(tokenizer, "Variables are not yet supported in the console.\n");
			return NULL;
		}

		Node *declaration = (Node *) AllocateFixed(sizeof(Node));
		declaration->type = T_DECLARE;
		declaration->firstChild = ParseType(tokenizer, false, false /* allow void */, false /* allow tuple */);
		Assert(declaration->firstChild);
		declaration->token = TokenNext(tokenizer);
		Assert(declaration->token.type == T_IDENTIFIER);

		Tokenizer copy = *tokenizer;
		Token token = TokenNext(tokenizer);
		Assert(token.type == T_EQUALS || token.type == T_SEMICOLON || token.type == T_COMMA || token.type == T_IN);

		if (token.type == T_IN && allowIn) {
			*tokenizer = copy;
		} else if (token.type == T_EQUALS) {
			declaration->firstChild->sibling = ParseExpression(tokenizer, false, 0);
			if (!declaration->firstChild->sibling) return NULL;
			Token semicolon = TokenNext(tokenizer);

			if (semicolon.type != T_SEMICOLON) {
				PrintError2(tokenizer, declaration, "Expected a semicolon at the end of the variable declaration.\n");
				return NULL;
			}
		} else if (token.type == T_COMMA) {
			Node *group = (Node *) AllocateFixed(sizeof(Node));
			group->type = T_DECLARE_GROUP;
			group->firstChild = declaration;
			group->token = declaration->token;
			Node **link = &declaration->sibling;
			declaration = group;

			while (true) {
				Node *declaration = (Node *) AllocateFixed(sizeof(Node));
				*link = declaration;
				link = &declaration->sibling;
				declaration->type = T_DECLARE;
				declaration->firstChild = ParseType(tokenizer, false, false /* allow void */, false /* allow tuple */);
				if (!declaration->firstChild) return NULL;
				declaration->token = TokenNext(tokenizer);

				if (declaration->token.type != T_IDENTIFIER) {
					PrintError2(tokenizer, declaration, "Expected an identifier for the variable name.\n");
					return NULL;
				}

				Token token = TokenNext(tokenizer);

				if (token.type == T_ERROR) {
					return NULL;
				} else if (token.type == T_COMMA) {
					// Keep going...
				} else if (token.type == T_SEMICOLON) {
					break;
				} else if (token.type == T_EQUALS) {
					group->type = T_DECL_GROUP_AND_SET;
					*link = ParseExpression(tokenizer, false, 0);
					if (!(*link)) return NULL;
					Token semicolon = TokenNext(tokenizer);

					if (semicolon.type != T_SEMICOLON) {
						PrintError2(tokenizer, declaration, "Expected a semicolon at the end of the variable declaration group.\n");
						return NULL;
					}

					break;
				} else {
					PrintError2(tokenizer, declaration, "Expected one of ',', '=' or ';' in the variable declaration group.\n");
					return NULL;
				}
			}
		}

		return declaration;
	} else {
		Node *expression = ParseExpression(tokenizer, true, 0);
		if (!expression) return NULL;

		if (replMode) {
			Token end = TokenPeek(tokenizer);

			if (end.type == T_ERROR) {
				return NULL;
			} else if (end.type == T_EOF) {
				Node *replResult = (Node *) AllocateFixed(sizeof(Node));
				replResult->type = T_REPL_RESULT;
				replResult->firstChild = expression;
				return replResult;
			}
		}

		Token semicolon = TokenNext(tokenizer);

		if (semicolon.type != T_SEMICOLON) {
			PrintError2(tokenizer, expression, "Expected a semicolon at the end of the expression.\n");
			return NULL;
		}

		return expression;
	}
}

Node *ParseBlock(Tokenizer *tokenizer, bool replMode) {
	Node *node = (Node *) AllocateFixed(sizeof(Node));
	Node **link = &node->firstChild;
	node->type = T_BLOCK;
	node->token.line = tokenizer->line;
	node->token.module = tokenizer->module;

	while (true) {
		Token token = TokenPeek(tokenizer);

		if (token.type == T_ERROR) {
			return NULL;
		} else if (token.type == (replMode ? T_EOF : T_RIGHT_FANCY)) {
			TokenNext(tokenizer);
			return node;
		} else if (token.type == T_IF) {
			Node *node = ParseIf(tokenizer);
			if (!node) return NULL;
			*link = node;
			link = &node->sibling;
		} else if (token.type == T_WHILE) {
			Node *node = (Node *) AllocateFixed(sizeof(Node));
			node->type = T_WHILE;
			node->token = TokenNext(tokenizer);
			node->firstChild = ParseExpression(tokenizer, false, 0);
			if (!node->firstChild) return NULL;
			Token token = TokenNext(tokenizer);

			if (token.type != T_LEFT_FANCY && token.type != T_SEMICOLON) {
				PrintError2(tokenizer, node, "Expected a block or semicolon after the while condition.\n");
				return NULL;
			}

			if (token.type == T_LEFT_FANCY) {
				node->firstChild->sibling = ParseBlock(tokenizer, false);
				if (!node->firstChild->sibling) return NULL;
			} else {
				node->firstChild->sibling = (Node *) AllocateFixed(sizeof(Node));
				node->firstChild->sibling->type = T_BLOCK;
			}

			*link = node;
			link = &node->sibling;
		} else if (token.type == T_FOR) {
			// TODO Optional components.

			Node *node = (Node *) AllocateFixed(sizeof(Node));
			node->type = T_FOR;
			node->token = TokenNext(tokenizer);
			node->firstChild = ParseVariableDeclarationOrExpression(tokenizer, false /* replMode */, true /* allowIn */);
			if (!node->firstChild) return NULL;
			Node **siblingLink = NULL;
			Token peek = TokenPeek(tokenizer);

			if (tokenizer->error) {
				return NULL;
			} else if (peek.type == T_IN) {
				node->type = T_FOR_EACH;
				TokenNext(tokenizer);
				node->firstChild->sibling = ParseExpression(tokenizer, false, 0);
				if (!node->firstChild->sibling) return NULL;
				siblingLink = &node->firstChild->sibling->sibling;
			} else {
				node->firstChild->sibling = ParseExpression(tokenizer, false, 0);
				if (!node->firstChild->sibling) return NULL;

				Token token = TokenNext(tokenizer);

				if (token.type != T_SEMICOLON) {
					PrintError2(tokenizer, node, "Expected a semicolon.\n");
					return NULL;
				}

				node->firstChild->sibling->sibling = ParseExpression(tokenizer, true, 0);
				if (!node->firstChild->sibling->sibling) return NULL;

				siblingLink = &node->firstChild->sibling->sibling->sibling;
			}

			token = TokenNext(tokenizer);

			if (token.type != T_LEFT_FANCY && token.type != T_SEMICOLON) {
				PrintError2(tokenizer, node, "Expected a block or semicolon to complete the for statement.\n");
				return NULL;
			}

			if (token.type == T_LEFT_FANCY) {
				*siblingLink = ParseBlock(tokenizer, false);
				if (!(*siblingLink)) return NULL;
			} else {
				*siblingLink = (Node *) AllocateFixed(sizeof(Node));
				(*siblingLink)->type = T_BLOCK;
			}

			// Make sure that the for variable has its own scope.
			Node *wrapper = (Node *) AllocateFixed(sizeof(Node));
			wrapper->type = T_BLOCK;
			wrapper->token = node->token;
			wrapper->firstChild = node;

			*link = wrapper;
			link = &wrapper->sibling;
		} else if (token.type == T_RETURN || token.type == T_ASSERT || token.type == T_RETERR) {
			if (replMode) {
				PrintError2(tokenizer, node, "%s statements cannot be used in the console.\n", 
						token.type == T_RETURN || token.type == T_RETERR ? "Return" : "Assert");
				return NULL;
			}

			Node *node = (Node *) AllocateFixed(sizeof(Node));
			node->type = token.type;
			node->token = TokenNext(tokenizer);

			if (token.type == T_ASSERT || token.type == T_RETERR || TokenPeek(tokenizer).type != T_SEMICOLON) {
				node->firstChild = ParseExpression(tokenizer, false, 0);
				if (!node->firstChild) return NULL;

				if (token.type == T_RETURN && TokenPeek(tokenizer).type == T_COMMA) {
					Node *n = node;
					node = (Node *) AllocateFixed(sizeof(Node));
					node->type = T_RETURN_TUPLE;
					node->firstChild = n->firstChild;
					Node **argumentLink = &n->firstChild->sibling;

					while (TokenPeek(tokenizer).type == T_COMMA) {
						TokenNext(tokenizer);
						n = ParseExpression(tokenizer, false, 0);
						if (!n) return NULL;
						*argumentLink = n;
						argumentLink = &n->sibling;
					}
				}
			}

			*link = node;
			link = &node->sibling;

			Token semicolon = TokenNext(tokenizer);

			if (semicolon.type != T_SEMICOLON) {
				PrintError2(tokenizer, node, "Expected a semicolon at the end of the statement.\n");
				return NULL;
			}
		} else if (token.type == T_LEFT_FANCY) {
			TokenNext(tokenizer);
			Node *block = ParseBlock(tokenizer, false);
			if (!block) return NULL;
			*link = block;
			link = &block->sibling;
		} else {
			Node *node = ParseVariableDeclarationOrExpression(tokenizer, replMode, false /* allowIn */);
			if (!node) return NULL;
			*link = node;
			link = &node->sibling;
		}
	}
}

Node *ParseGlobalVariableOrFunctionDefinition(Tokenizer *tokenizer, bool allowGlobalVariables, bool parseFunctionBody) {
	Node *type = ParseType(tokenizer, false, true /* allow void */, true /* allow tuple */);

	if (!type) {
		return NULL;
	}

	Node *node = (Node *) AllocateFixed(sizeof(Node));
	node->token = TokenNext(tokenizer);

	if (node->token.type != T_IDENTIFIER) {
		if (allowGlobalVariables) {
			PrintError2(tokenizer, node, "Expected an identifier for the name of the function or global variable.\n");
		} else {
			PrintError2(tokenizer, node, "Expected an identifier for the name of the function pointer type.\n");
		}

		return NULL;
	}

	Token bracket = TokenPeek(tokenizer);

	if (bracket.type == T_ERROR) {
		return NULL;
	} else if (bracket.type == T_LEFT_ROUND) {
		TokenNext(tokenizer);
		node->type = T_FUNCTION;
		Node *functionPointerType = (Node *) AllocateFixed(sizeof(Node));
		functionPointerType->type = T_FUNCPTR;
		node->firstChild = functionPointerType;
		Node *arguments = (Node *) AllocateFixed(sizeof(Node));
		arguments->type = T_ARGUMENTS;
		functionPointerType->firstChild = arguments;
		arguments->sibling = type;
		Node **link = &arguments->firstChild;
		int argumentCount = 0;

		while (true) {
			Token token = TokenPeek(tokenizer);

			if (token.type == T_RIGHT_ROUND) {
				TokenNext(tokenizer);
				break;
			}

			if (arguments->firstChild) {
				Token comma = TokenNext(tokenizer);

				if (comma.type != T_COMMA) {
					Node n = { .token = comma };
					PrintError2(tokenizer, &n, "Expected a comma to separate function arguments.\n");
					return NULL;
				}
			}

			Node *argument = (Node *) AllocateFixed(sizeof(Node));
			argument->type = T_ARGUMENT;
			argument->firstChild = ParseType(tokenizer, false, false /* allow void */, false /* allow tuple */);
			if (!argument->firstChild) return NULL;
			argument->token = TokenNext(tokenizer);
			*link = argument;
			link = &argument->sibling;
			argumentCount++;

			if (argumentCount > FUNCTION_MAX_ARGUMENTS) {
				PrintError2(tokenizer, argument, "Functions cannot have more than %d arguments.\n", FUNCTION_MAX_ARGUMENTS);
				return NULL;
			}

			if (argument->token.type != T_IDENTIFIER) {
				PrintError2(tokenizer, argument, "Expected an identifier for the name of the function argument.\n");
				return NULL;
			}
		}

		if (parseFunctionBody) {
			Token token = TokenNext(tokenizer);

			if (token.type == T_LEFT_FANCY) {
				Node *body = (Node *) AllocateFixed(sizeof(Node));
				body->type = T_FUNCBODY;
				functionPointerType->sibling = body;
				body->firstChild = ParseBlock(tokenizer, false);

				if (!body->firstChild) {
					return NULL;
				}
			} else if (token.type == T_EXTCALL) {
				Token semicolon = TokenNext(tokenizer);

				if (semicolon.type != T_SEMICOLON) {
					PrintError(tokenizer, "Expected a semicolon after 'extcall'.\n");
					return NULL;
				}

				node->isExternalCall = true;
			} else {
				Node n = { .token = token };
				PrintError2(tokenizer, &n, "Expected a '{' to start the function body.\n");
				return NULL;
			}
		}
	} else if (allowGlobalVariables) {
		Token semicolon = TokenNext(tokenizer);

		if (semicolon.type == T_PERSIST) {
			node->isPersistentVariable = true;
			semicolon = TokenNext(tokenizer);
		}

		if (semicolon.type == T_OPTION) {
			node->isOptionVariable = true;
			semicolon = TokenNext(tokenizer);
		}

		if (semicolon.type != T_SEMICOLON) {
			PrintError(tokenizer, "Expected a semicolon after the global variable definition.\n");
			return NULL;
		}

		node->type = T_DECLARE;
		node->firstChild = type;
	} else {
		PrintError(tokenizer, "Expected a '(' to start the argument list.\n");
		return NULL;
	}

	return node;
}

Node *ParseRootREPL(Tokenizer *tokenizer) {
	// TODO Importing modules.

	Node *root = (Node *) AllocateFixed(sizeof(Node));
	root->type = T_ROOT;
	Node **link = &root->firstChild;

	Node *node = (Node *) AllocateFixed(sizeof(Node));
	node->type = T_IMPORT;
	node->token.type = T_INLINE;
	node->token.text = "#inline";
	node->token.textBytes = 7;
	node->firstChild = (Node *) AllocateFixed(sizeof(Node));
	node->firstChild->type = T_IMPORT_PATH;
	node->firstChild->token.type = T_STRING_LITERAL;
	node->firstChild->token.text = "__base_module__";
	node->firstChild->token.textBytes = 15;
	*link = node;
	link = &node->sibling;

	Node *function = (Node *) AllocateFixed(sizeof(Node));
	*link = function;
	link = &function->sibling;
	function->type = T_FUNCTION;
	function->token.text = startFunction;
	function->token.textBytes = startFunctionBytes;
	Node *functionPointerType = (Node *) AllocateFixed(sizeof(Node));
	function->firstChild = functionPointerType;
	functionPointerType->type = T_FUNCPTR;
	Node *functionArguments = (Node *) AllocateFixed(sizeof(Node));
	functionPointerType->firstChild = functionArguments;
	functionArguments->type = T_ARGUMENTS;
	Node *functionReturnType = (Node *) AllocateFixed(sizeof(Node));
	functionArguments->sibling = functionReturnType;
	functionReturnType->type = T_VOID;
	Node *functionBody = (Node *) AllocateFixed(sizeof(Node));
	functionPointerType->sibling = functionBody;
	functionBody->type = T_FUNCBODY;
	functionBody->firstChild = ParseBlock(tokenizer, true);

	if (!functionBody->firstChild) {
		return NULL;
	}

	return root;
}

Node *ParseRoot(Tokenizer *tokenizer) {
	Node *root = (Node *) AllocateFixed(sizeof(Node));
	root->type = T_ROOT;
	Node **link = &root->firstChild;

	if (!tokenizer->isBaseModule) {
		Node *node = (Node *) AllocateFixed(sizeof(Node));
		node->type = T_IMPORT;
		node->token.type = T_INLINE;
		node->token.text = "#inline";
		node->token.textBytes = 7;
		node->firstChild = (Node *) AllocateFixed(sizeof(Node));
		node->firstChild->type = T_IMPORT_PATH;
		node->firstChild->token.type = T_STRING_LITERAL;
		node->firstChild->token.text = "__base_module__";
		node->firstChild->token.textBytes = 15;
		*link = node;
		link = &node->sibling;
	}

	while (true) {
		Token token = TokenPeek(tokenizer);

		if (token.type == T_ERROR) {
			return NULL;
		} else if (token.type == T_EOF) {
			// ScriptPrintNode(root, 0);
			return root;
		} else if (token.type == T_FUNCTYPE) {
			TokenNext(tokenizer);
			Node *node = ParseGlobalVariableOrFunctionDefinition(tokenizer, false, false);
			if (!node) return NULL;
			node->type = T_FUNCTYPE;
			*link = node;
			link = &node->sibling;

			Token semicolon = TokenNext(tokenizer);

			if (semicolon.type != T_SEMICOLON) {
				PrintError2(tokenizer, node->firstChild->sibling, "Expected a semicolon after the argument list.\n");
				return NULL;
			}
		} else if (token.type == T_HANDLETYPE) {
			TokenNext(tokenizer);
			Node *node = (Node *) AllocateFixed(sizeof(Node));
			node->type = T_HANDLETYPE;
			node->token = TokenNext(tokenizer);
			*link = node;
			link = &node->sibling;

			if (node->token.type != T_IDENTIFIER) {
				PrintError2(tokenizer, node, "Expected the name of the handletype.\n");
				return NULL;
			}

			Token semicolon = TokenNext(tokenizer);

			if (semicolon.type == T_COLON) {
				node->firstChild = ParseType(tokenizer, false, false, false);
				if (!node->firstChild) return NULL;
				semicolon = TokenNext(tokenizer);
				node->hasTypeInheritanceParent = true;
			}

			if (semicolon.type != T_SEMICOLON) {
				PrintError2(tokenizer, node, "Expected a semicolon after the handletype identifier.\n");
				return NULL;
			}
		} else if (token.type == T_INTTYPE) {
			TokenNext(tokenizer);
			Node *node = (Node *) AllocateFixed(sizeof(Node));
			node->type = T_INTTYPE;
			node->token = TokenNext(tokenizer);
			*link = node;
			link = &node->sibling;
			Node **contentLink = &node->firstChild;

			if (node->token.type != T_IDENTIFIER) {
				PrintError2(tokenizer, node, "Expected the name of the inttype.\n");
				return NULL;
			}

			Token token = TokenNext(tokenizer);

			if (token.type == T_ERROR) {
				return NULL;
			} else if (token.type == T_COLON) {
				Node *parent = ParseType(tokenizer, false, false, false);
				if (!parent) return NULL;
				*contentLink = parent;
				contentLink = &parent->sibling;
				token = TokenNext(tokenizer);
				node->hasTypeInheritanceParent = true;
			}

			if (token.type == T_ERROR) {
				return NULL;
			} else if (token.type != T_LEFT_FANCY) {
				PrintError2(tokenizer, node, "Expected a '{' for the inttype contents after the name.\n");
				return NULL;
			}

			while (true) {
				Token peek = TokenPeek(tokenizer);
				if (peek.type == T_ERROR) return NULL;
				if (peek.type == T_RIGHT_FANCY) break;
				Node *constant = (Node *) AllocateFixed(sizeof(Node));
				constant->token = TokenNext(tokenizer);
				constant->type = T_INTTYPE_CONSTANT;
				*contentLink = constant;
				contentLink = &constant->sibling;

				if (constant->token.type != T_IDENTIFIER) {
					PrintError2(tokenizer, constant, "Expected an identifier for the constant.\n");
					return NULL;
				}

				if (TokenNext(tokenizer).type != T_EQUALS) {
					PrintError2(tokenizer, constant, "Expected '=' between the constant name and the value.\n");
					return NULL;
				}

				constant->firstChild = ParseExpression(tokenizer, false, 0);

				if (TokenNext(tokenizer).type != T_SEMICOLON) {
					PrintError2(tokenizer, constant, "Expected a semicolon after the constant value.\n");
					return NULL;
				}
			}

			TokenNext(tokenizer);

			if (TokenNext(tokenizer).type != T_SEMICOLON) {
				PrintError2(tokenizer, node, "Expected a semicolon after the inttype body.\n");
				return NULL;
			}
		} else if (token.type == T_STRUCT) {
			TokenNext(tokenizer);
			Node *node = (Node *) AllocateFixed(sizeof(Node));
			node->type = T_STRUCT;
			node->token = TokenNext(tokenizer);
			*link = node;
			link = &node->sibling;

			if (node->token.type != T_IDENTIFIER) {
				PrintError2(tokenizer, node, "Expected the name of the struct.\n");
				return NULL;
			}

			if (TokenNext(tokenizer).type != T_LEFT_FANCY) {
				PrintError2(tokenizer, node, "Expected a '{' for the struct contents after the name.\n");
				return NULL;
			}

			Node **fieldLink = &node->firstChild;

			while (true) {
				Token peek = TokenPeek(tokenizer);
				if (peek.type == T_ERROR) return NULL;
				if (peek.type == T_RIGHT_FANCY) break;
				Node *type = ParseType(tokenizer, false, false /* allow void */, false /* allow tuple */);
				if (!type) return NULL;
				Node *field = (Node *) AllocateFixed(sizeof(Node));
				field->token = TokenNext(tokenizer);
				field->type = T_DECLARE;
				field->firstChild = type;
				*fieldLink = field;
				fieldLink = &field->sibling;

				if (field->token.type != T_IDENTIFIER) {
					PrintError2(tokenizer, field, "Expected an identifier for the field name.\n");
					return NULL;
				}

				if (TokenNext(tokenizer).type != T_SEMICOLON) {
					PrintError2(tokenizer, field, "Expected a semicolon after the field name.\n");
					return NULL;
				}
			}

			TokenNext(tokenizer);

			if (TokenNext(tokenizer).type != T_SEMICOLON) {
				PrintError2(tokenizer, node, "Expected a semicolon after the struct body.\n");
				return NULL;
			}
		} else if (token.type == T_IMPORT) {
			Node *node = (Node *) AllocateFixed(sizeof(Node));
			node->type = T_IMPORT;
			TokenNext(tokenizer);
			node->firstChild = ParseExpression(tokenizer, false, 0);
			if (!node->firstChild) return NULL;
			node->token = TokenNext(tokenizer);

			if (node->firstChild->type != T_STRING_LITERAL) {
				PrintError2(tokenizer, node, "The path to the script file to import must be a string literal.\n");
				return NULL;
			} else if (node->token.type != T_IDENTIFIER && node->token.type != T_INLINE) {
				PrintError2(tokenizer, node, "Expected an identifier for the name to import the module as.\n");
				return NULL;
			}

			node->firstChild->type = T_IMPORT_PATH;
			*link = node;
			link = &node->sibling;

			if (TokenNext(tokenizer).type != T_SEMICOLON) {
				PrintError2(tokenizer, node, "Expected a semicolon after the #import statement.\n");
				return NULL;
			}
		} else if (token.type == T_LIBRARY) {
			if (tokenizer->module->library) {
				PrintError(tokenizer, "The library has already been set for this module.\n");
				return NULL;
			} else {
				TokenNext(tokenizer);
				Token token = TokenNext(tokenizer);

				if (token.type != T_STRING_LITERAL) {
					if (token.type != T_ERROR) PrintError(tokenizer, "Expected a string literal for the library name.\n");
					return NULL;
				}

				char name[256];

				if (token.textBytes > sizeof(name) - 1) {
					PrintError(tokenizer, "The library name is too long.\n");
					return NULL;
				}

				MemoryCopy(name, token.text, token.textBytes);
				name[token.textBytes] = 0;
				tokenizer->module->library = LibraryLoad(name);

				if (!tokenizer->module->library) {
					return NULL;
				}
			}

			if (TokenNext(tokenizer).type != T_SEMICOLON) {
				PrintError(tokenizer, "Expected a semicolon after the #library statement.\n");
				return NULL;
			}
		} else {
			Node *node = ParseGlobalVariableOrFunctionDefinition(tokenizer, true, true);
			if (!node) return NULL;
			*link = node;
			link = &node->sibling;
		}
	}
}

// --------------------------------- Scope management.

bool ScopeIsVariableType(Node *node) {
	return node->type == T_DECLARE || node->type == T_FUNCTION || node->type == T_ARGUMENT;
}

intptr_t ScopeLookupIndex(Node *node, Scope *scope, bool maybe, bool real /* if false, the variable index is returned */) {
	uintptr_t j = 0;

	for (uintptr_t i = 0; i < scope->entryCount; i++) {
		if (scope->entries[i]->token.textBytes == node->token.textBytes
				&& 0 == MemoryCompare(scope->entries[i]->token.text, node->token.text, node->token.textBytes)) {
			if (!ScopeIsVariableType(scope->entries[i]) && !real) {
				break;
			}

			return j;
		}

		if (ScopeIsVariableType(scope->entries[i]) || real) {
			j++;
		}
	}

	if (!maybe) {
		Assert(false);
	}

	return -1;
}

Node *ScopeLookup(Tokenizer *tokenizer, Node *node, bool maybe) {
	Node *ancestor = node;
	Scope *scope = NULL;

	while (ancestor) {
		if (ancestor->scope != scope) {
			scope = ancestor->scope;

			for (uintptr_t i = 0; i < scope->entryCount; i++) {
				if (scope->entries[i]->token.textBytes == node->token.textBytes
						&& 0 == MemoryCompare(scope->entries[i]->token.text, node->token.text, node->token.textBytes)) {
					if (node->referencesRootScope && scope->entries[i]->parent->type != T_ROOT) {
						PrintError2(tokenizer, node, "The identifier '%.*s' is used before it is declared in this scope.\n", 
								node->token.textBytes, node->token.text);
						return NULL;
					}

					return scope->entries[i];
				}
			}
		}

		ancestor = ancestor->parent;
	}

	if (!maybe) {
		PrintError2(tokenizer, node, "Could not find identifier '%.*s' inside its scope.\n", node->token.textBytes, node->token.text);
	}

	return NULL;
}

bool ScopeCheckNotAlreadyUsed(Tokenizer *tokenizer, Node *node) {
	Node *ancestor = node;
	Scope *scope = NULL;

	while (ancestor) {
		if (ancestor->scope != scope) {
			scope = ancestor->scope;

			for (uintptr_t i = 0; i < scope->entryCount; i++) {
				if (scope->entries[i]->token.textBytes == node->token.textBytes
						&& 0 == MemoryCompare(scope->entries[i]->token.text, node->token.text, node->token.textBytes)
						&& (!scope->isRoot || node->scope == scope)) {
					PrintError2(tokenizer, node, "The identifier '%.*s' was already used in this scope.\n", 
							node->token.textBytes, node->token.text);

					if (scope->entries[i]->type == T_INLINE) {
						if (scope->entries[i]->importData->pathBytes == 15 
								&& 0 == MemoryCompare(scope->entries[i]->importData->path, "__base_module__", scope->entries[i]->importData->pathBytes)) {
							PrintDebug("It was declared in base library module.\n", 
									scope->entries[i]->importData->path);
						} else {
							PrintDebug("It was imported inline from the module '%s'.\n", 
									scope->entries[i]->importData->path);
						}
					}

					return false;
				}
			}
		}

		ancestor = ancestor->parent;
	}

	return true;
}

bool ScopeAddEntry(Tokenizer *tokenizer, Scope *scope, Node *node) {
	if (node->type == T_IMPORT && node->token.type == T_INLINE) {
		return true;
	}

	node->scope = scope;

	if (scope->entryCount == scope->entriesAllocated) {
		scope->entriesAllocated = scope->entriesAllocated ? scope->entriesAllocated * 2 : 4;
		scope->entries = (Node **) AllocateResize(scope->entries, sizeof(Node *) * scope->entriesAllocated);
	}

	if (!ScopeCheckNotAlreadyUsed(tokenizer, node)) {
		return false;
	}

	scope->entries[scope->entryCount++] = node;

	if (ScopeIsVariableType(node)) {
		scope->variableEntryCount++;
	}

	// Set this here before type checking occurs, 
	// so that all the declarations in the scope already have their expression type set.
	node->expressionType = node->firstChild;

	return true;
}

void ASTFreeScopes(Node *node) {
	if (node && node->scope) {
		node->scope->entries = (Node **) AllocateResize(node->scope->entries, 0);

		Node *child = node->firstChild;

		while (child) {
			ASTFreeScopes(child);
			child = child->sibling;
		}
	}
}

bool ASTSetScopes(Tokenizer *tokenizer, ExecutionContext *context, Node *node, Scope *scope) {
	Node *child = node->firstChild;

	while (child) {
		child->parent = node;
		child = child->sibling;
	}

	if (node->type == T_ROOT || node->type == T_BLOCK || node->type == T_FUNCBODY) {
		scope = node->scope = (Scope *) AllocateFixed(sizeof(Scope));
		scope->isRoot = node->type == T_ROOT;
	} else {
		node->scope = scope;
	}

	if (node->type == T_FUNCBODY) {
		Node *argument = node->parent->firstChild->firstChild->firstChild;

		while (argument) {
			if (!ScopeAddEntry(tokenizer, scope, argument)) {
				return false;
			}
			
			argument = argument->sibling;
		}
	}

	if (node->type == T_VARIABLE) {
		Node *referenced = ScopeLookup(tokenizer, node, true);

		if (!referenced || referenced->parent->type == T_ROOT) {
			// (If the lookup fails, then it must be a forward declaration, which is only allowed at the root scope level.)
			node->referencesRootScope = true;
		}
	}

	{
		child = node->firstChild;

		while (child) {
			if (!ASTSetScopes(tokenizer, context, child, scope)) {
				return false;
			}

			child = child->sibling;
		}
	}

	if ((node->type == T_DECLARE || node->type == T_FUNCTION || node->type == T_FUNCTYPE || node->type == T_INTTYPE || node->type == T_INTTYPE_CONSTANT
			|| node->type == T_STRUCT || node->type == T_IMPORT || node->type == T_HANDLETYPE) && node->parent->type != T_STRUCT) {
		if (!ScopeAddEntry(tokenizer, scope, node)) {
			return false;
		}
	}

	if (node->type == T_IMPORT) {
		ImportData *alreadyImportedModule = importedModules;

		while (alreadyImportedModule) {
			if (alreadyImportedModule->pathBytes == node->firstChild->token.textBytes
					&& 0 == MemoryCompare(alreadyImportedModule->path, node->firstChild->token.text, alreadyImportedModule->pathBytes)) {
				break;
			}

			alreadyImportedModule = alreadyImportedModule->nextImport;
		}

		if (alreadyImportedModule) {
#if 0
			PrintError2(tokenizer, node, "The script at path '%.*s' has already been imported as a module.\n",
					node->firstChild->token.textBytes, node->firstChild->token.text);
			return false;
#else
			node->importData = alreadyImportedModule;
#endif
		} else {
			char *path = (char *) AllocateFixed(node->firstChild->token.textBytes + 1);
			MemoryCopy(path, node->firstChild->token.text, node->firstChild->token.textBytes);
			path[node->firstChild->token.textBytes] = 0;

			Tokenizer t = { 0 };
			void *fileData;

			if (node->firstChild->token.textBytes == 15 
					&& 0 == MemoryCompare(node->firstChild->token.text, "__base_module__", node->firstChild->token.textBytes)) {
				fileData = baseModuleSource;
				t.inputBytes = sizeof(baseModuleSource) - 1;
				t.isBaseModule = true;
			} else {
				fileData = FileLoad(path, &t.inputBytes);
			}

			if (!fileData) {
				PrintError2(tokenizer, node, "The script at path '%.*s' could not be loaded.\n",
						node->firstChild->token.textBytes, node->firstChild->token.text);
				return false;
			}

			node->importData = (ImportData *) AllocateFixed(sizeof(ImportData));
			node->importData->fileDataBytes = t.inputBytes;
			node->importData->fileData = fileData;
			node->importData->path = path;
			node->importData->pathBytes = node->firstChild->token.textBytes;
			node->importData->parentImport = tokenizer->module;

			ImportData *parentImport = tokenizer->module;

			while (parentImport) {
				if (parentImport->pathBytes == node->firstChild->token.textBytes
						&& 0 == MemoryCompare(parentImport->path, node->firstChild->token.text, parentImport->pathBytes)) {
					PrintError3("There is a cyclic import dependency.\n");
					ImportData *data = node->importData;

					while (data->parentImport) {
						PrintDebug("- '%s' is imported by '%s'\n", data->path, data->parentImport->path);
						data = data->parentImport;
					}

					return false;
				}

				parentImport = parentImport->parentImport;
			}

			t.module = node->importData;
			t.input = (const char *) fileData;
			t.line = 1;

			if (!ScriptLoad(t, context, node->importData, false)) {
				return false;
			}
		}

		if (node->token.type == T_INLINE) {
			uintptr_t j = 0;

			for (uintptr_t i = 0; i < node->importData->rootNode->scope->entryCount; i++) {
				if (node->importData->rootNode->scope->entries[i]->type == T_INLINE) {
					continue;
				}

				Node *inlineNode = (Node *) AllocateFixed(sizeof(Node));
				inlineNode->token = node->importData->rootNode->scope->entries[i]->token;
				inlineNode->type = T_INLINE;
				inlineNode->parent = node->parent;
				inlineNode->firstChild = node->importData->rootNode->scope->entries[i]->expressionType;
				inlineNode->importData = node->importData;

				if (ScopeIsVariableType(node->importData->rootNode->scope->entries[i])) {
					inlineNode->inlineImportVariableIndex = j++;
				} else {
					inlineNode->inlineImportVariableIndex = -1;
				}

				if (!ScopeAddEntry(tokenizer, scope, inlineNode)) {
					return false;
				}
			}
		}
	}

	return true;
}

// --------------------------------- Type checking.

bool ASTMatching(Node *left, Node *right) {
	if (!left && !right) {
		return true;
	} else if (!left || !right) {
		return false;
	} else if (left->type == T_NULL && (right->type == T_STRUCT || right->type == T_LIST || right->type == T_FUNCTYPE)) {
		return true;
	} else if (right->type == T_NULL && (left->type == T_STRUCT || left->type == T_LIST || left->type == T_FUNCTYPE)) {
		return true;
	} else if (left->type == T_ZERO && (right->type == T_INT || right->type == T_INTTYPE || right->type == T_HANDLETYPE)) {
		return true;
	} else if (right->type == T_ZERO && (left->type == T_INT || left->type == T_INTTYPE || left->type == T_HANDLETYPE)) {
		return true;
	} else if (left->type != right->type) {
		return false;
	} else if ((left->type == T_IDENTIFIER || left->type == T_STRUCT || left->type == T_HANDLETYPE || left->type == T_INTTYPE) 
			&& (left->token.module != right->token.module || left->token.textBytes != right->token.textBytes 
				|| MemoryCompare(left->token.text, right->token.text, right->token.textBytes))) {
		return false;
	} else {
		Node *childLeft = left->firstChild;
		Node *childRight = right->firstChild;

		while (true) {
			if (!childLeft && !childRight) {
				return true;
			} else if (!childLeft || !childRight) {
				return false;
			} else if (!ASTMatching(childLeft, childRight)) {
				return false;
			} else {
				childLeft = childLeft->sibling;
				childRight = childRight->sibling;
			}
		}
	}
}

bool ASTIsIntType(Node *node) {
	return node && node->type == T_INTTYPE;
}

bool ASTIsManagedType(Node *node) {
	return node->type == T_STR || node->type == T_LIST || node->type == T_STRUCT 
		|| node->type == T_FUNCPTR || node->type == T_FUNCPTR || node->type == T_ERR || node->type == T_ANYTYPE;
}

int ASTGetTypePopCount(Node *node) {
	if (node->type == T_TUPLE) {
		int count = 0;
		Node *child = node->firstChild;
		while (child) { count++; child = child->sibling; }
		return count;
	} else {
		return 1;
	}
}

bool ASTLookupTypeIdentifiers(Tokenizer *tokenizer, Node *node) {
	Node *child = node->firstChild;

	while (child) {
		if (!ASTLookupTypeIdentifiers(tokenizer, child)) return false;
		child = child->sibling;
	}

	if (node->type == T_FUNCPTR) {
		Node *type = node->firstChild->sibling;

		if (type->type == T_IDENTIFIER) {
			Node *lookup = ScopeLookup(tokenizer, type, false);
			if (!lookup) return false;
			Node *copy = (Node *) AllocateFixed(sizeof(Node));
			*copy = *lookup;
			copy->sibling = NULL;
			node->firstChild->sibling = copy;
		}
	}

	if (node->type == T_DECLARE || node->type == T_ARGUMENT || node->type == T_NEW || node->type == T_LIST 
			|| node->type == T_CAST_TYPE_WRAPPER || node->hasTypeInheritanceParent) {
		Node *type = node->firstChild;

		if (node->hasTypeInheritanceParent && type && type->type != T_IDENTIFIER && type->type != node->type) {
			PrintError2(tokenizer, node, "Types can only inherit from similar types.\n");
			return false;
		}

		if (type && type->type == T_IDENTIFIER) {
			Node *lookup = ScopeLookup(tokenizer, type, false);

			if (!lookup) {
				return false;
			}

			if (node->hasTypeInheritanceParent) {
				if (lookup->type != node->type) {
					PrintError2(tokenizer, node, "Types can only inherit from similar types.\n");
					return false;
				}
			} else if (!node->expressionType) {
				node->expressionType = node->firstChild;
			}

			Node *previousSibling = node->expressionType->sibling;

			if (!lookup) {
				return false;
			} else if (lookup->type == T_FUNCTYPE) {
				MemoryCopy(node->expressionType, lookup->firstChild, sizeof(Node));
			} else if (lookup->type == T_STRUCT || lookup->type == T_INTTYPE || lookup->type == T_HANDLETYPE) {
				if (node->hasTypeInheritanceParent) {
					if (lookup->cycleCheck) {
						PrintError2(tokenizer, lookup, "Cyclic type inheritance.\n");
						return false;
					}

					Node *copy = (Node *) AllocateFixed(sizeof(Node));
					*copy = *lookup;
					copy->sibling = node->firstChild->sibling;
					node->firstChild = copy;
					node->cycleCheck = true;
					if (!ASTLookupTypeIdentifiers(tokenizer, copy)) return false;
					node->cycleCheck = false;
				} else {
					MemoryCopy(node->expressionType, lookup, sizeof(Node));
				}
			} else {
				PrintError2(tokenizer, lookup, "The identifier did not resolve to a type.\n");
				return false;
			}

			node->expressionType->sibling = previousSibling;
		}
	}

	return true;
}

bool ASTImplicitCastIsPossible(Node *targetType, Node *expressionType) {
	if (!expressionType || !targetType) {
		return false;
	} else if (targetType->type == T_ERR && ASTMatching(targetType->firstChild, expressionType)) {
		return true;
	} else if (targetType->type == T_HANDLETYPE && expressionType->type == T_HANDLETYPE) {
		// Only allow implicit casts to the inherited type.
		Node *inherit = expressionType->firstChild;
		return inherit && (ASTMatching(targetType, inherit) || ASTImplicitCastIsPossible(targetType, inherit));
	} else if (targetType->type == T_INTTYPE && expressionType->type == T_INTTYPE) {
		// Only allow implicit casts to a type that inherits from this.
		Node *inherit = targetType->firstChild;
		return inherit && inherit->type != T_INTTYPE_CONSTANT && (ASTMatching(inherit, expressionType) || ASTImplicitCastIsPossible(inherit, expressionType));
	} else if (targetType->type == T_ANYTYPE && expressionType->type != T_VOID) {
		return true;
	} else {
		return false;
	}
}

Node *ASTImplicitCastApply(Tokenizer *tokenizer, Node *parent, Node *target, Node *expression) {
	Node *cast = NULL;

	if (target->type == T_ERR && ASTMatching(target->firstChild, expression->expressionType)) {
		if (!expression->expressionType || ASTMatching(expression->expressionType, &globalExpressionTypeVoid)) {
			PrintError2(tokenizer, parent, "You cannot assign a value of 'void' to 'err[void]'.\nInstead, use the constant '#success'.\n");
			return NULL;
		}

		cast = (Node *) AllocateFixed(sizeof(Node));
		cast->type = T_ERR_CAST;
	} else if ((target->type == T_HANDLETYPE && expression->expressionType->type == T_HANDLETYPE)
			|| (target->type == T_INTTYPE && expression->expressionType->type == T_INTTYPE)) {
		return expression; // Nothing needs to be done to cast between handletypes or inttypes.
	} else if (target->type == T_ANYTYPE) {
		cast = (Node *) AllocateFixed(sizeof(Node));
		cast->type = T_ANYTYPE_CAST;
	} else {
		Assert(false);
		return NULL;
	}

	cast->scope = parent->scope;
	cast->parent = parent;
	cast->firstChild = expression;
	cast->expressionType = target;
	return cast;
}

Value ASTNumericLiteralToValue(Node *node) {
	// TODO Overflow checking.

	Value v = { 0 };

	if (node->expressionType == &globalExpressionTypeInt) {
		v.i = 0;

		for (uintptr_t i = 0; i < node->token.textBytes; i++) {
			v.i *= 10;
			v.i += node->token.text[i] - '0';
		}
	} else if (node->expressionType == &globalExpressionTypeFloat) {
		bool dot = false;
		v.f = 0;
		double m = 0.1;

		for (uintptr_t i = 0; i < node->token.textBytes; i++) {
			if (node->token.text[i] == '.') {
				dot = true;
			} else if (dot) {
				v.f += (node->token.text[i] - '0') * m;
				m /= 10;
			} else {
				v.f *= 10;
				v.f += node->token.text[i] - '0';
			}
		}
	} else {
		Assert(false);
	}

	return v;
}

int64_t ASTEvaluateIntConstant(Tokenizer *tokenizer, Node *node, bool *error) {
	Assert(ASTMatching(node->expressionType, &globalExpressionTypeInt));

	if (node->type == T_NUMERIC_LITERAL) {
		return ASTNumericLiteralToValue(node).i;
	} else if (node->type == T_ZERO) {
		return 0;
	} else if (node->type == T_ADD) {
		return ASTEvaluateIntConstant(tokenizer, node->firstChild, error) + ASTEvaluateIntConstant(tokenizer, node->firstChild->sibling, error);
	} else if (node->type == T_MINUS) {
		return ASTEvaluateIntConstant(tokenizer, node->firstChild, error) - ASTEvaluateIntConstant(tokenizer, node->firstChild->sibling, error);
	} else if (node->type == T_ASTERISK) {
		return ASTEvaluateIntConstant(tokenizer, node->firstChild, error) * ASTEvaluateIntConstant(tokenizer, node->firstChild->sibling, error);
	} else if (node->type == T_BITWISE_OR) {
		return ASTEvaluateIntConstant(tokenizer, node->firstChild, error) | ASTEvaluateIntConstant(tokenizer, node->firstChild->sibling, error);
	} else if (node->type == T_BITWISE_AND) {
		return ASTEvaluateIntConstant(tokenizer, node->firstChild, error) & ASTEvaluateIntConstant(tokenizer, node->firstChild->sibling, error);
	} else if (node->type == T_BITWISE_XOR) {
		return ASTEvaluateIntConstant(tokenizer, node->firstChild, error) ^ ASTEvaluateIntConstant(tokenizer, node->firstChild->sibling, error);
	} else if (node->type == T_SLASH) {
		int64_t divisor = ASTEvaluateIntConstant(tokenizer, node->firstChild->sibling, error);

		if (*error) {
			return 0;
		} else if (divisor == 0) {
			*error = true;
			PrintError2(tokenizer, node, "Integer division by zero.\n");
			return 0;
		} else {
			return ASTEvaluateIntConstant(tokenizer, node->firstChild, error) / divisor;
		}
	} else if (node->type == T_BIT_SHIFT_LEFT || node->type == T_BIT_SHIFT_RIGHT) {
		int64_t shift = ASTEvaluateIntConstant(tokenizer, node->firstChild->sibling, error);

		if (*error) {
			return 0;
		} else if (shift < 0 || shift > 63) {
			*error = true;
			PrintError2(tokenizer, node, "Shift %d is out of range (0..63).\n", shift);
			return 0;
		} else if (node->type == T_BIT_SHIFT_LEFT) {
			return ASTEvaluateIntConstant(tokenizer, node->firstChild, error) << shift;
		} else {
			return ASTEvaluateIntConstant(tokenizer, node->firstChild, error) >> shift;
		}
	} else if (node->type == T_NEGATE) {
		return -ASTEvaluateIntConstant(tokenizer, node->firstChild, error);
	} else if (node->type == T_BITWISE_NOT) {
		return ~ASTEvaluateIntConstant(tokenizer, node->firstChild, error);
	} else {
		Assert(false);
		return 0;
	}
}

bool ASTIsIntegerConstant(Node *node) {
	if (node->type != T_NUMERIC_LITERAL 
			&& node->type != T_ZERO
			&& node->type != T_ADD 
			&& node->type != T_MINUS 
			&& node->type != T_ASTERISK 
			&& node->type != T_SLASH 
			&& node->type != T_NEGATE
			&& node->type != T_BIT_SHIFT_LEFT
			&& node->type != T_BIT_SHIFT_RIGHT
			&& node->type != T_BITWISE_OR
			&& node->type != T_BITWISE_AND
			&& node->type != T_BITWISE_XOR
			&& node->type != T_BITWISE_NOT) {
		return false;
	}

	if (!ASTMatching(node->expressionType, &globalExpressionTypeInt)) {
		return false;
	}

	Node *child = node->firstChild;

	while (child) {
		if (!ASTIsIntegerConstant(child)) return false;
		child = child->sibling;
	}

	return true;
}

bool ASTSetTypes(Tokenizer *tokenizer, Node *node) {
	Node *child = node->firstChild;

	while (child) {
		if (child == node) Assert(false);
		if (!ASTSetTypes(tokenizer, child)) return false;
		child->parent = node;
		child = child->sibling;
	}

	if (node->type == T_ROOT || node->type == T_BLOCK
			|| node->type == T_INT || node->type == T_FLOAT || node->type == T_STR 
			|| node->type == T_LIST || node->type == T_TUPLE || node->type == T_ERR || node->type == T_ANYTYPE
			|| node->type == T_BOOL || node->type == T_VOID || node->type == T_IDENTIFIER
			|| node->type == T_ARGUMENTS || node->type == T_ARGUMENT
			|| node->type == T_STRUCT || node->type == T_FUNCTYPE || node->type == T_IMPORT 
			|| node->type == T_IMPORT_PATH || node->type == T_INTTYPE || node->type == T_HANDLETYPE
			|| node->type == T_FUNCPTR || node->type == T_FUNCBODY || node->type == T_FUNCTION
			|| node->type == T_REPL_RESULT || node->type == T_DECLARE_GROUP || node->type == T_CAST_TYPE_WRAPPER) {
	} else if (node->type == T_NUMERIC_LITERAL) {
		size_t dotCount = 0;

		for (uintptr_t i = 0; i < node->token.textBytes; i++) {
			if (node->token.text[i] == '.') {
				dotCount++;
			}
		}

		if (dotCount == 0) {
			node->expressionType = &globalExpressionTypeInt;
		} else if (dotCount == 1) {
			node->expressionType = &globalExpressionTypeFloat;
		} else {
			PrintError2(tokenizer, node, "Invalid number. There should either be one decimal place (for a float), or none (for an integer).\n");
			return false;
		}
	} else if (node->type == T_TRUE || node->type == T_FALSE) {
		node->expressionType = &globalExpressionTypeBool;
	} else if (node->type == T_SUCCESS) {
		node->expressionType = &globalExpressionTypeErrVoid;
	} else if (node->type == T_NULL || node->type == T_ZERO) {
		node->expressionType = node;
	} else if (node->type == T_STRING_LITERAL) {
		node->expressionType = &globalExpressionTypeStr;
	} else if (node->type == T_VARIABLE) {
		Node *lookup = ScopeLookup(tokenizer, node, false);
		if (!lookup) return false;
		node->expressionType = lookup->expressionType;
	} else if (node->type == T_STR_INTERPOLATE) {
		Node *left = node->firstChild;
		Node *expression = node->firstChild->sibling;
		Node *right = node->firstChild->sibling->sibling;
		Assert(left->type == T_STRING_LITERAL || left->type == T_STR_INTERPOLATE);
		Assert(right->type == T_STRING_LITERAL);
		Assert(left->expressionType->type == T_STR);
		Assert(right->expressionType->type == T_STR);
		node->expressionType = &globalExpressionTypeStr;

		// TODO Converting more types to strings.

		if (!ASTMatching(expression->expressionType, &globalExpressionTypeInt)
				&& !ASTMatching(expression->expressionType, &globalExpressionTypeIntList)
				&& !ASTMatching(expression->expressionType, &globalExpressionTypeStr)
				&& !ASTMatching(expression->expressionType, &globalExpressionTypeFloat)
				&& !ASTMatching(expression->expressionType, &globalExpressionTypeBool)
				&& !ASTIsIntType(expression->expressionType)) {
			PrintError2(tokenizer, expression, "The expression cannot be converted to a string.\n");
			return false;
		}
	} else if (node->type == T_ADD || node->type == T_MINUS || node->type == T_ASTERISK || node->type == T_SLASH
			|| node->type == T_GREATER_THAN || node->type == T_LESS_THAN || node->type == T_LT_OR_EQUAL || node->type == T_GT_OR_EQUAL
			|| node->type == T_DOUBLE_EQUALS || node->type == T_NOT_EQUALS || node->type == T_LOGICAL_AND || node->type == T_LOGICAL_OR
			|| node->type == T_BIT_SHIFT_LEFT || node->type == T_BIT_SHIFT_RIGHT 
			|| node->type == T_BITWISE_OR || node->type == T_BITWISE_AND || node->type == T_BITWISE_XOR) {
		Node *leftType = node->firstChild->expressionType;
		Node *rightType = node->firstChild->sibling->expressionType;
		bool useRightType = false;

		if (!ASTMatching(leftType, rightType)) {
			if (leftType && rightType && leftType->type == T_INTTYPE && rightType->type == T_INTTYPE
					&& (ASTImplicitCastIsPossible(leftType, rightType) || ASTImplicitCastIsPossible(rightType, leftType))) {
				// If both expressions are inttypes with one inheriting from the other, allow an implicit cast.
				// Nothing needs to be done to cast between inttypes.
				useRightType = ASTImplicitCastIsPossible(rightType, leftType);
			} else {
				PrintError2(tokenizer, node, "The expression on the left and right side of a binary operator must have the same type.\n");
				return false;
			}
		}

		if (node->type == T_ADD) {
			if (!ASTMatching(leftType, &globalExpressionTypeInt)
					&& !ASTIsIntType(leftType)
					&& !ASTMatching(leftType, &globalExpressionTypeFloat)
					&& !ASTMatching(leftType, &globalExpressionTypeStr)) {
				PrintError2(tokenizer, node, "The add operator expects integers, floats or strings.\n");
				return false;
			}
		} else if (node->type == T_LOGICAL_AND || node->type == T_LOGICAL_OR) {
			if (!ASTMatching(leftType, &globalExpressionTypeBool)) {
				PrintError2(tokenizer, node, "This operator expects boolean expressions.\n");
				return false;
			}
		} else if (node->type == T_DOUBLE_EQUALS || node->type == T_NOT_EQUALS) {
			if (!ASTMatching(leftType, &globalExpressionTypeInt)
					&& !ASTMatching(leftType, &globalExpressionTypeFloat)
					&& !ASTMatching(leftType, &globalExpressionTypeStr)
					&& !ASTMatching(leftType, &globalExpressionTypeBool)
					&& (!leftType || leftType->type != T_LIST)
					&& (!leftType || leftType->type != T_STRUCT)) {
				PrintError2(tokenizer, node, "These types cannot be compared.\n");
				return false;
			}
		} else {
			if (!ASTMatching(leftType, &globalExpressionTypeInt)
					&& !ASTIsIntType(leftType)
					&& !ASTMatching(leftType, &globalExpressionTypeFloat)) {
				PrintError2(tokenizer, node, "This operator expects either integers or floats.\n");
				return false;
			}
		}

		if (node->type == T_GREATER_THAN || node->type == T_LESS_THAN || node->type == T_LT_OR_EQUAL || node->type == T_GT_OR_EQUAL
				|| node->type == T_DOUBLE_EQUALS || node->type == T_NOT_EQUALS) {
			node->expressionType = &globalExpressionTypeBool;
		} else {
			node->expressionType = useRightType ? rightType : leftType;
		}
	} else if (node->type == T_DECLARE) {
		if (node->firstChild->sibling && ASTImplicitCastIsPossible(node->firstChild, node->firstChild->sibling->expressionType)) {
			Node *cast = ASTImplicitCastApply(tokenizer, node, node->firstChild, node->firstChild->sibling);
			if (!cast) return false;
			node->firstChild->sibling = cast;
		} else if (node->firstChild->sibling && !ASTMatching(node->firstChild, node->firstChild->sibling->expressionType)) {
			PrintError2(tokenizer, node, "The type of the variable being assigned does not match the expression.\n");
			return false;
		}
	} else if (node->type == T_DECL_GROUP_AND_SET) {
		Node *lastChild = node->firstChild;
		while (lastChild->sibling) lastChild = lastChild->sibling;
		Node *tuple = lastChild->expressionType;

		if (tuple->type != T_TUPLE) {
			PrintError2(tokenizer, node, "You can only use '=' in the declaration group with a function that returns a tuple.\n");
			return false;
		}

		Node *child = node->firstChild;
		Node *item = tuple->firstChild;
		int index = 1;

		while (child->sibling && item) {
			if (!ASTMatching(child->expressionType, item)) {
				PrintError2(tokenizer, node, "The type of value %d in the tuple does not match the declaration type.\n", index);
				return false;
			}

			child = child->sibling;
			item = item->sibling;
			index++;
		}

		if ((!item && child->sibling) || (item && !child->sibling)) {
			PrintError2(tokenizer, node, "The number of declarations in the group does not match the number of values in the tuple.\n");
			return false;
		}
	} else if (node->type == T_SET_GROUP) {
		if (node->parent->type == T_SET_GROUP) {
			// Reattach our children at the end of the parent's children.
			Assert(!node->sibling);
			Node *child = node->parent->firstChild;
			while (child->sibling != node) child = child->sibling;
			child->sibling = node->firstChild;
		} else if (node->parent->type == T_EQUALS) {
			Node *expressionType = (Node *) AllocateFixed(sizeof(Node));
			expressionType->type = T_TUPLE;
			node->expressionType = expressionType;

			Node *child = node->firstChild;
			Node **link = &expressionType->firstChild;

			while (child) {
				Node *copy = (Node *) AllocateFixed(sizeof(Node));
				*copy = *child->expressionType;
				*link = copy;
				link = &(*link)->sibling;
				child = child->sibling;
			}

			*link = NULL;
		} else {
			PrintError2(tokenizer, node, "A comma separated list of expressions can only be used to assign a tuple return value to variables.\n");
			return false;
		}
	} else if (node->type == T_EQUALS) {
		if (ASTImplicitCastIsPossible(node->firstChild->expressionType, node->firstChild->sibling->expressionType)) {
			Node *cast = ASTImplicitCastApply(tokenizer, node, node->firstChild->expressionType, node->firstChild->sibling);
			if (!cast) return false;
			node->firstChild->sibling = cast;
		} else if (!ASTMatching(node->firstChild->expressionType, node->firstChild->sibling->expressionType)) {
			PrintError2(tokenizer, node, "The type of the variable being assigned does not match the expression.\n");
			return false;
		}
	} else if (node->type == T_CALL) {
		Node *functionPointer = node->firstChild;
		Node *expressionType = functionPointer->expressionType;

		if (!expressionType || expressionType->type != T_FUNCPTR) {
			PrintError2(tokenizer, functionPointer, "The expression being called is not a function.\n");
			return false;
		}

		node->expressionType = expressionType->firstChild->sibling;
		Node *match = expressionType->firstChild->firstChild;
		Node **argumentLink = &node->firstChild->sibling->firstChild;
		Node *argument = *argumentLink;
		size_t index = 1;

		while (true) {
			if (!argument && !match) {
				break;
			} else if (!argument || !match) {
				PrintError2(tokenizer, node, "The function has a different number of arguments to this.\n");
				return false;
			} else {
				if (ASTImplicitCastIsPossible(match->firstChild, argument->expressionType)) {
					Node *cast = ASTImplicitCastApply(tokenizer, node, match->firstChild, argument);
					if (!cast) return false;
					*argumentLink = cast;
				} else if (!ASTMatching(argument->expressionType, match->firstChild)) {
					PrintError2(tokenizer, node, "The types for argument %d do not match.\n", index);
					return false;
				}

				match = match->sibling;
				argumentLink = &argument->sibling;
				argument = *argumentLink;
				index++;
			}
		}
	} else if (node->type == T_ASSERT) {
		if (!ASTMatching(node->firstChild->expressionType, &globalExpressionTypeBool)
				&& !(node->firstChild->expressionType && node->firstChild->expressionType->type == T_ERR)) {
			PrintError2(tokenizer, node, "The asserted expression must evaluate to a boolean or error value.\n");
			return false;
		}
	} else if (node->type == T_RETURN || node->type == T_RETERR) {
		Node *expressionType = node->firstChild ? node->firstChild->expressionType : &globalExpressionTypeVoid;

		Node *function = node->parent;

		while (function->type != T_FUNCTION) {
			function = function->parent;
		}

		Node *returnType = function->firstChild->firstChild->sibling;

		if (node->firstChild && ASTMatching(returnType, &globalExpressionTypeVoid)) {
			PrintError2(tokenizer, node, "The function does not return a value ('void'), but the return statement has a return value.\n");
			return false;
		}

		if (node->type == T_RETURN) {
			if (!ASTMatching(expressionType, returnType)) {
				if (ASTImplicitCastIsPossible(returnType, node->firstChild->expressionType)) {
					Node *cast = ASTImplicitCastApply(tokenizer, node, returnType, node->firstChild);
					if (!cast) return false;
					node->firstChild = cast;
				} else {
					PrintError2(tokenizer, node, "The type of the expression does not match the return type of the function.\n");
					return false;
				}
			}
		} else {
			if (!returnType || returnType->type != T_ERR) {
				PrintError2(tokenizer, node, "'reterr' can only be used in functions that return an error value.\n");
				return false;
			}

			if (!expressionType || expressionType->type != T_ERR) {
				PrintError2(tokenizer, node, "The expression must be an error value.\n");
				return false;
			}
		}
	} else if (node->type == T_RETURN_TUPLE) {
		Node *expressionType = (Node *) AllocateFixed(sizeof(Node));
		expressionType->type = T_TUPLE;

		Node *child = node->firstChild;
		Node **link = &expressionType->firstChild;

		while (child) {
			Node *copy = (Node *) AllocateFixed(sizeof(Node));
			*copy = *child->expressionType;
			*link = copy;
			link = &(*link)->sibling;
			child = child->sibling;
		}

		*link = NULL;

		Node *function = node->parent;

		while (function->type != T_FUNCTION) {
			function = function->parent;
		}

		Node *returnType = function->firstChild->firstChild->sibling;

		if (ASTMatching(returnType, &globalExpressionTypeVoid)) {
			PrintError2(tokenizer, node, "The function does not return a value ('void'), but the return statement has a return value.\n");
			return false;
		}

		if (!ASTMatching(expressionType, returnType)) {
			PrintError2(tokenizer, node, "The type of the expression does not match the declared return type of the function.\n");
			return false;
		}
	} else if (node->type == T_IF) {
		if (!ASTMatching(node->firstChild->expressionType, &globalExpressionTypeBool)
				&& !(node->firstChild->expressionType && node->firstChild->expressionType->type == T_ERR)) {
			PrintError2(tokenizer, node, "The expression used for the condition must evaluate to a boolean or error value.\n");
			return false;
		}
	} else if (node->type == T_WHILE) {
		if (!ASTMatching(node->firstChild->expressionType, &globalExpressionTypeBool)) {
			PrintError2(tokenizer, node, "The expression used for the condition must evaluate to a boolean.\n");
			return false;
		}
	} else if (node->type == T_IF_ERR) {
		if (!node->firstChild->sibling->expressionType || node->firstChild->sibling->expressionType->type != T_ERR) {
			PrintError2(tokenizer, node, "The expression used for the condition must evaluate to a error type.\n");
			return false;
		}

		if (!ASTMatching(node->firstChild->firstChild, node->firstChild->sibling->expressionType->firstChild)) {
			PrintError2(tokenizer, node, "The variable declaration type must match the error value.\n");
			return false;
		}
	} else if (node->type == T_FOR) {
		if (!ASTMatching(node->firstChild->sibling->expressionType, &globalExpressionTypeBool)) {
			PrintError2(tokenizer, node, "The expression used for the condition must evaluate to a boolean.\n");
			return false;
		}
	} else if (node->type == T_FOR_EACH) {
		Node *listType = node->firstChild->sibling->expressionType;
		bool isStr = listType && ASTMatching(listType, &globalExpressionTypeStr);

		if (!listType || (listType->type != T_LIST && !isStr)) {
			PrintError2(tokenizer, node, "The expression on the right of 'in' must be a list or string.\n");
			return false;
		}

		if (isStr) {
			if (!ASTMatching(node->firstChild->expressionType, &globalExpressionTypeStr)) {
				PrintError2(tokenizer, node, "The variable on the left of 'in' must be a 'str' when iterating over a string.\n");
				return false;
			}
		} else {
			if (!ASTMatching(node->firstChild->expressionType, listType->firstChild)) {
				PrintError2(tokenizer, node, "The variable on the left of 'in' must match the type of the items in the list on the right.\n");
				return false;
			}
		}
	} else if (node->type == T_INDEX) {
		if (!ASTMatching(node->firstChild->expressionType, &globalExpressionTypeStr)
				&& node->firstChild->expressionType->type != T_LIST) {
			PrintError2(tokenizer, node, "The expression being indexed must be a string or list.\n");
			return false;
		}

		if (!ASTMatching(node->firstChild->sibling->expressionType, &globalExpressionTypeInt)) {
			PrintError2(tokenizer, node, "The index must be a integer.\n");
			return false;
		}

		if (ASTMatching(node->firstChild->expressionType, &globalExpressionTypeStr)) {
			node->expressionType = &globalExpressionTypeStr;
		} else {
			node->expressionType = node->firstChild->expressionType->firstChild;
		}
	} else if (node->type == T_NEW) {
		if (node->firstChild->type != T_STRUCT && node->firstChild->type != T_LIST && node->firstChild->type != T_ERR) {
			PrintError2(tokenizer, node, "This type is not a struct, list or error. 'new' is used to create new instances of structs, lists and errors.\n");
			return false;
		}

		if (node->firstChild->type == T_ERR && !ASTMatching(node->firstChild->sibling->expressionType, &globalExpressionTypeStr)) {
			PrintError2(tokenizer, node, "The error message should be a string.\n");
			return false;
		}
	} else if (node->type == T_DOT) {
		bool isStruct = node->firstChild->expressionType->type == T_STRUCT;

		if (!isStruct && node->firstChild->expressionType->type != T_IMPORT_PATH) {
			PrintError2(tokenizer, node, "This expression is not a struct or an imported module. "
					"You cannot use the '.' operator on it.\n");
			return false;
		}

		if (isStruct) {
			Node *structure = node->firstChild->expressionType;
			Node *field = structure->firstChild;

			while (field) {
				if (field->token.textBytes == node->token.textBytes && 
						0 == MemoryCompare(field->token.text, node->token.text, node->token.textBytes)) {
					break;
				}

				field = field->sibling;
			}

			if (!field) {
				PrintError2(tokenizer, node, "The field '%.*s' is not in the struct '%.*s'.\n",
						node->token.textBytes, node->token.text, structure->token.textBytes, structure->token.text);
				return false;
			}

			node->expressionType = field->firstChild;
		} else {
			ImportData *importData = node->firstChild->expressionType->parent->importData;
			intptr_t index = ScopeLookupIndex(node, importData->rootNode->scope, true, true);

			if (index == -1) {
				PrintError2(tokenizer, node, "The variable or function '%.*s' is not in the imported module '%s'.\n",
						node->token.textBytes, node->token.text, importData->path);
				return false;
			}

			node->expressionType = importData->rootNode->scope->entries[index]->expressionType;
		}
	} else if (node->type == T_COLON) {
		Node *expressionType = node->firstChild->expressionType;

		if (!expressionType) {
			PrintError2(tokenizer, node, "This is not an expression.\n");
			return false;
		}

		bool isList = expressionType->type == T_LIST;
		bool isStr = expressionType->type == T_STR;
		bool isFuncPtr = expressionType->type == T_FUNCPTR;
		bool isErr = expressionType->type == T_ERR;
		bool isInt = expressionType->type == T_INT;
		bool isFloat = expressionType->type == T_FLOAT;
		bool isAnyType = expressionType->type == T_ANYTYPE;

		if (!isList && !isStr & !isFuncPtr && !isErr && !isInt && !isFloat && !isAnyType) {
			PrintError2(tokenizer, node, "This type does not have any ':' operations.\n");
			return false;
		}

		Token token = node->token;
		Node *arguments[2] = { 0 };
		bool returnsItem = false, returnsInt = false, returnsBool = false, returnsStr = false, returnsFloat = false, simple = true;
		uint8_t op;

		if (isList && KEYWORD("resize")) arguments[0] = &globalExpressionTypeInt, op = T_OP_RESIZE;
		else if (isList && KEYWORD("add")) arguments[0] = expressionType->firstChild, op = T_OP_ADD;
		else if (isList && KEYWORD("insert")) arguments[0] = expressionType->firstChild, arguments[1] = &globalExpressionTypeInt, op = T_OP_INSERT;
		else if (isList && KEYWORD("insert_many")) arguments[0] = &globalExpressionTypeInt, arguments[1] = &globalExpressionTypeInt, op = T_OP_INSERT_MANY;
		else if (isList && KEYWORD("delete")) arguments[0] = &globalExpressionTypeInt, op = T_OP_DELETE;
		else if (isList && KEYWORD("find_and_delete")) arguments[0] = expressionType->firstChild, op = T_OP_FIND_AND_DELETE, returnsBool = true;
		else if (isList && KEYWORD("find")) arguments[0] = expressionType->firstChild, op = T_OP_FIND, returnsInt = true;
		else if (isList && KEYWORD("delete_many")) arguments[0] = &globalExpressionTypeInt, arguments[1] = &globalExpressionTypeInt, op = T_OP_DELETE_MANY;
		else if (isList && KEYWORD("delete_last")) op = T_OP_DELETE_LAST;
		else if (isList && KEYWORD("delete_all")) op = T_OP_DELETE_ALL;
		else if (isList && KEYWORD("first")) returnsItem = true, op = T_OP_FIRST;
		else if (isList && KEYWORD("last")) returnsItem = true, op = T_OP_LAST;
		else if ((isList || isStr) && KEYWORD("len")) returnsInt = true, op = T_OP_LEN;
		else if (isInt && KEYWORD("float")) returnsFloat = true, op = T_OP_INT_TO_FLOAT;
		else if (isFloat && KEYWORD("truncate")) returnsInt = true, op = T_OP_FLOAT_TRUNCATE;
		else if (isErr && KEYWORD("success")) returnsBool = true, op = T_OP_SUCCESS;
		else if (isErr && KEYWORD("assert")) returnsItem = true, op = T_OP_ASSERT_ERR;
		else if (isErr && KEYWORD("error")) returnsStr = true, op = T_OP_ERROR;
		else if (isErr && KEYWORD("default")) arguments[0] = expressionType->firstChild, returnsItem = true, op = T_OP_DEFAULT; // TODO Warn if the expression has side effects.

		else if (isFuncPtr && KEYWORD("async")) {
			if (expressionType->firstChild->firstChild) {
				PrintError2(tokenizer, node, "The function pointer should take no arguments. Use ':curry(...)' to set them before ':async()'.\n");
				return false;
			} else if (!ASTMatching(expressionType->firstChild->sibling, &globalExpressionTypeVoid)) {
				PrintError2(tokenizer, node, "The function pointer should not return anything. Use ':discard()' or ':assert()' before ':async()'.\n");
				return false;
			}

			// TODO Allow currying here, for convenience.
			op = T_OP_ASYNC;
			returnsInt = true;
		}

		else if (isFuncPtr && (KEYWORD("assert") || KEYWORD("discard"))) {
			if (KEYWORD("assert")) {
				op = T_OP_ASSERT;

				if (!ASTMatching(expressionType->firstChild->sibling, &globalExpressionTypeBool)) {
					PrintError2(tokenizer, node, "The return value of the function must be a bool to assert it.\n");
					return false;
				}
			} else {
				op = T_OP_DISCARD;

				if (ASTMatching(expressionType->firstChild->sibling, &globalExpressionTypeVoid)) {
					PrintError2(tokenizer, node, "The return value cannot be discarded from a function that already returns void.\n");
					return false;
				}

				if (expressionType->firstChild->sibling && expressionType->firstChild->sibling->type == T_TUPLE) {
					// TODO Remove this restriction?
					PrintError2(tokenizer, node, "The discard operation cannot be used on a function returning a tuple.\n");
					return false;
				}
			}

			node->expressionType = (Node *) AllocateFixed(sizeof(Node));
			node->expressionType->type = T_FUNCPTR;
			node->expressionType->firstChild = (Node *) AllocateFixed(sizeof(Node));
			MemoryCopy(node->expressionType->firstChild, expressionType->firstChild, sizeof(Node));
			node->expressionType->firstChild->sibling = &globalExpressionTypeVoid;
			simple = false;
		}

		else if (isFuncPtr && KEYWORD("curry")) {
			if (!expressionType->firstChild->firstChild) {
				PrintError2(tokenizer, node, "The function pointer doesn't take any arguments.\n");
				return false;
			} else if (!ASTMatching(expressionType->firstChild->firstChild->firstChild, node->firstChild->sibling->firstChild->expressionType)) {
				PrintError2(tokenizer, node, "The curried argument does not match the type of the first argument.\n");
				return false;
			} else if (node->firstChild->sibling->firstChild->sibling) {
				// TODO Allow currying multiple arguments together.
				PrintError2(tokenizer, node, "You can only curry one argument at a time.\n");
				return false;
			}

			node->expressionType = (Node *) AllocateFixed(sizeof(Node));
			node->expressionType->type = T_FUNCPTR;
			node->expressionType->firstChild = (Node *) AllocateFixed(sizeof(Node));
			MemoryCopy(node->expressionType->firstChild, expressionType->firstChild, sizeof(Node));
			node->expressionType->firstChild->firstChild = node->expressionType->firstChild->firstChild->sibling;
			node->expressionType->firstChild->sibling = expressionType->firstChild->sibling;
			op = T_OP_CURRY;
			simple = false;
		}

		else if (isAnyType && KEYWORD("cast")) {
			Assert(node->firstChild->sibling->type == T_CAST_TYPE_WRAPPER);
			node->expressionType = node->firstChild->sibling->firstChild;
			op = T_OP_CAST;
			simple = false;
		}

		else {
			PrintError2(tokenizer, node, "This type does not have an operation called '%.*s'.\n", token.textBytes, token.text);
			return false;
		}

		if (op == T_OP_FIND_AND_DELETE && ASTMatching(arguments[0], &globalExpressionTypeFloat)) {
			PrintError2(tokenizer, node, "The 'find_and_delete' operation cannot be used with floats.\n");
			return false;
		} else if (op == T_OP_FIND && ASTMatching(arguments[0], &globalExpressionTypeFloat)) {
			PrintError2(tokenizer, node, "The 'find' operation cannot be used with floats.\n");
			return false;
		}

		if (op == T_OP_DEFAULT && ASTMatching(arguments[0], &globalExpressionTypeVoid)) {
			PrintError2(tokenizer, node, "The 'default' operation cannot be used with error values of type void.\n");
			return false;
		} else if (op == T_OP_ASSERT_ERR && ASTMatching(expressionType->firstChild, &globalExpressionTypeVoid)) {
			PrintError2(tokenizer, node, "The 'assert' operation cannot be used with error values of type void.\n");
			return false;
		}

		if (op == T_OP_FIND_AND_DELETE && ASTMatching(arguments[0], &globalExpressionTypeStr)) {
			op = T_OP_FIND_AND_DEL_STR;
		} else if (op == T_OP_FIND && ASTMatching(arguments[0], &globalExpressionTypeStr)) {
			op = T_OP_FIND_STR;
		}

		if (simple) {
			Node *argument1 = node->firstChild->sibling->firstChild;
			Node *argument2 = argument1 ? argument1->sibling : NULL;
			Node *argument3 = argument2 ? argument2->sibling : NULL;

			if (argument3 || (argument2 && !arguments[1]) || (argument1 && !arguments[0])
					|| (!argument2 && arguments[1]) || (!argument1 && arguments[0])) {
				PrintError2(tokenizer, node, "Incorrect number of arguments for the operation '%.*s'.\n", token.textBytes, token.text);
				return false;
			}

			if (argument1 && !ASTMatching(argument1->expressionType, arguments[0])) {
				PrintError2(tokenizer, node, "Incorrect first argument type for the operation '%.*s'.\n", token.textBytes, token.text);
				return false;
			}

			if (argument2 && !ASTMatching(argument2->expressionType, arguments[1])) {
				PrintError2(tokenizer, node, "Incorrect second argument type for the operation '%.*s'.\n", token.textBytes, token.text);
				return false;
			}

			node->expressionType = returnsItem ? expressionType->firstChild 
				: returnsInt ? &globalExpressionTypeInt 
				: returnsStr ? &globalExpressionTypeStr 
				: returnsFloat ? &globalExpressionTypeFloat 
				: returnsBool ? &globalExpressionTypeBool : NULL;
		}

		node->operationType = op;
	} else if (node->type == T_LOGICAL_NOT) {
		if (!ASTMatching(node->firstChild->expressionType, &globalExpressionTypeBool)) {
			PrintError2(tokenizer, node, "Expected a bool for the logical not '!' operator.\n");
			return false;
		}

		node->expressionType = &globalExpressionTypeBool;
	} else if (node->type == T_NEGATE || node->type == T_BITWISE_NOT) {
		if (!ASTMatching(node->firstChild->expressionType, &globalExpressionTypeInt)
				&& !ASTMatching(node->firstChild->expressionType, &globalExpressionTypeFloat)) {
			PrintError2(tokenizer, node, "Expected a int or float for the %s operator.\n", node->type == T_NEGATE ? "unary negate '-'" : "bitwise not '~'");
			return false;
		}

		node->expressionType = node->firstChild->expressionType;
	} else if (node->type == T_LIST_LITERAL) {
		if (!node->firstChild) {
			// TODO Support empty list literals?
			PrintError2(tokenizer, node, "Empty list literals are not allowed. Instead, put 'new T[]' where 'T' is the item type.\n");
			return false;
		}

		Node *item = node->firstChild;
		node->expressionType = (Node *) AllocateFixed(sizeof(Node));
		node->expressionType->type = T_LIST;
		Node *copy = (Node *) AllocateFixed(sizeof(Node));
		*copy = *item->expressionType;
		copy->sibling = NULL;
		node->expressionType->firstChild = copy;

		while (item) {
			if (!ASTMatching(node->expressionType->firstChild, item->expressionType)) {
				PrintError2(tokenizer, item, "The type of this item is different to the ones before it in the list.\n");
				return false;
			}

			item = item->sibling;
		}
	} else if (node->type == T_AWAIT) {
		if (!ASTMatching(node->firstChild->expressionType, &globalExpressionTypeIntList)) {
			PrintError2(tokenizer, node, "Expected a list of task IDs to wait on.\n");
			return false;
		}

		node->expressionType = &globalExpressionTypeInt;
	} else if (node->type == T_INTTYPE_CONSTANT) {
		if (!ASTIsIntegerConstant(node->firstChild)) {
			PrintError2(tokenizer, node, "The expression is not a constant integer.\n");
			return false;
		}

		node->expressionType = node->parent;
	} else {
		PrintDebug("ASTSetTypes %d\n", node->type);
		Assert(false);
	}

	return true;
}

bool ASTCheckForReturnStatements(Tokenizer *tokenizer, Node *node) {
	if (node->type == T_ROOT) {
		Node *child = node->firstChild;

		while (child) {
			if (!ASTCheckForReturnStatements(tokenizer, child)) return false;
			child->parent = node;
			child = child->sibling;
		}
	} else if (node->type == T_FUNCTION) {
		if (node->firstChild->sibling && node->firstChild->firstChild->sibling
				&& node->firstChild->firstChild->sibling->type != T_VOID) {
			Assert(node->firstChild->sibling->type == T_FUNCBODY);
			return ASTCheckForReturnStatements(tokenizer, node->firstChild->sibling);
		}
	} else if (node->type == T_BLOCK || node->type == T_FUNCBODY) {
		Node *lastStatement = node->firstChild;

		while (lastStatement && lastStatement->sibling) {
			lastStatement = lastStatement->sibling;
		}

		if (lastStatement && (lastStatement->type == T_RETURN || lastStatement->type == T_RETURN_TUPLE)) {
			return true;
		} else if (lastStatement && (lastStatement->type == T_IF || lastStatement->type == T_IF_ERR || lastStatement->type == T_BLOCK)) {
			return ASTCheckForReturnStatements(tokenizer, lastStatement);
		} else {
			PrintError2(tokenizer, node, "This block needs to end with a return statement.\n");
			return false;
		}
	} else if (node->type == T_IF) {
		if (!node->firstChild->sibling->sibling) {
			PrintError2(tokenizer, node, "This function returns a value, so this if statement needs an else block which ends with a return statement.\n");
			return false;
		}

		return ASTCheckForReturnStatements(tokenizer, node->firstChild->sibling)
			&& ASTCheckForReturnStatements(tokenizer, node->firstChild->sibling->sibling);
	} else if (node->type == T_IF_ERR) {
		if (!node->firstChild->sibling->sibling->sibling) {
			PrintError2(tokenizer, node, "This function returns a value, so this if statement needs an else block which ends with a return statement.\n");
			return false;
		}

		return ASTCheckForReturnStatements(tokenizer, node->firstChild->sibling->sibling)
			&& ASTCheckForReturnStatements(tokenizer, node->firstChild->sibling->sibling->sibling);
	}

	return true;
}

// --------------------------------- Code generation.

void FunctionBuilderAppend(FunctionBuilder *builder, const void *buffer, size_t bytes) {
	if (builder->dataBytes + bytes > builder->dataAllocated) {
		builder->dataAllocated = 2 * builder->dataAllocated + bytes;
		builder->data = (uint8_t *) AllocateResize(builder->data, builder->dataAllocated);
	}

	for (uintptr_t i = 0; i < bytes; i++) {
		builder->data[builder->dataBytes + i] = ((const uint8_t *) buffer)[i];
	}

	builder->dataBytes += bytes;
}

void FunctionBuilderAddLineNumber(FunctionBuilder *builder, Node *node) {
	if (builder->lineNumberCount == builder->lineNumbersAllocated) {
		builder->lineNumbersAllocated = 2 * builder->lineNumbersAllocated + 4;
		builder->lineNumbers = (LineNumber *) AllocateResize(builder->lineNumbers, builder->lineNumbersAllocated * sizeof(LineNumber));
	}

	Node *ancestor = node;

	while (ancestor) {
		if (ancestor->type == T_FUNCTION) {
			builder->lineNumbers[builder->lineNumberCount].function = &ancestor->token;
		}

		ancestor = ancestor->parent;
	}

	builder->lineNumbers[builder->lineNumberCount].importData = builder->importData;
	builder->lineNumbers[builder->lineNumberCount].instructionPointer = builder->dataBytes;
	builder->lineNumbers[builder->lineNumberCount].lineNumber = node->token.line;
	builder->lineNumberCount++;
}

bool FunctionBuilderVariable(Tokenizer *tokenizer, FunctionBuilder *builder, Node *node, bool forAssignment) {
	Node *ancestor = node;
	Scope *scope = NULL;
	int32_t index = -1;
	Scope *rootScope = NULL;
	uintptr_t globalVariableOffset = builder->globalVariableOffset;
	bool inlineImport = false;
	bool isIntConstant = false;
	Value intConstantValue;

	while (ancestor) {
		if (ancestor->scope != scope) {
			scope = ancestor->scope;

			if (scope->isRoot) {
				rootScope = scope;
			}

			if (index != -1) {
				index += scope->variableEntryCount;
			}

			uintptr_t j = 0;

			for (uintptr_t i = 0; i < scope->entryCount; i++) {
				if (scope->entries[i]->token.textBytes == node->token.textBytes
						&& 0 == MemoryCompare(scope->entries[i]->token.text, node->token.text, node->token.textBytes)
						&& index == -1) {
					index = j;
					builder->isPersistentVariable = scope->entries[i]->isPersistentVariable;

					if (scope->entries[i]->type == T_INLINE) {
						index = scope->entries[i]->inlineImportVariableIndex;
						Assert(index != -1);
						globalVariableOffset = scope->entries[i]->importData->globalVariableOffset;
						inlineImport = true;
					} else if (scope->entries[i]->type == T_INTTYPE_CONSTANT) {
						isIntConstant = true;
						bool error = false;
						intConstantValue.i = ASTEvaluateIntConstant(tokenizer, scope->entries[i]->firstChild, &error);
						if (error) return false;
					}

					if (scope->entries[i]->type != T_DECLARE && forAssignment) {
						PrintError2(tokenizer, node, "A value cannot be assigned to this. "
								"Try putting a variable name here.\n");
						return false;
					}
				}

				if (ScopeIsVariableType(scope->entries[i])) {
					j++;
				}
			}
		}

		ancestor = ancestor->parent;
	}

	FunctionBuilderAddLineNumber(builder, node);

	if (isIntConstant) {
		uint8_t b = T_NUMERIC_LITERAL;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		FunctionBuilderAppend(builder, &intConstantValue, sizeof(intConstantValue));
	} else {
		if (index >= (int32_t) rootScope->variableEntryCount && !inlineImport) {
			index = rootScope->variableEntryCount - index - 1;
		} else {
			index += globalVariableOffset;
		}

		if (forAssignment) {
			builder->scopeIndex = index;
			builder->isDotAssignment = false;
			builder->isListAssignment = false;
		} else {
			FunctionBuilderAppend(builder, &node->type, sizeof(node->type));
			FunctionBuilderAppend(builder, &index, sizeof(index));
		}
	}

	return true;
}

bool FunctionBuilderRecurse(Tokenizer *tokenizer, Node *node, FunctionBuilder *builder, bool forAssignment) {
	if (forAssignment) {
		if (node->type == T_VARIABLE || node->type == T_DOT || node->type == T_INDEX) {
			// Supported.
		} else {
			PrintError2(tokenizer, node, "A value cannot be assigned to this expression. Try putting a variable name here.\n");
			return false;
		}
	}

	if (node->type == T_FUNCBODY || node->type == T_BLOCK) {
		if (node->scope->variableEntryCount > 10000) {
			PrintError2(tokenizer, node, "There are too many variables in this scope (the maximum is 10000).\n");
			return false;
		}

		uint16_t entryCount = node->scope->variableEntryCount;
		FunctionBuilderAddLineNumber(builder, node);
		FunctionBuilderAppend(builder, &node->type, sizeof(node->type));
		FunctionBuilderAppend(builder, &entryCount, sizeof(entryCount));

		for (uintptr_t i = 0; i < node->scope->entryCount; i++) {
			Node *entry = node->scope->entries[i];

			if (ScopeIsVariableType(entry)) {
				bool isManaged = ASTIsManagedType(entry->expressionType);
				FunctionBuilderAppend(builder, &isManaged, sizeof(isManaged));
			}
		}
	} else if (node->type == T_EQUALS || node->type == T_DECLARE || node->type == T_DECL_GROUP_AND_SET) {
		if (node->firstChild->sibling) {
			Node *variables[FUNCTION_MAX_ARGUMENTS];
			uintptr_t variableCount = 0;
			bool setGroup = node->type == T_EQUALS && node->firstChild->type == T_SET_GROUP;
			Node *lastChild = setGroup ? node->firstChild->firstChild : node->firstChild;

			while (setGroup ? lastChild : lastChild->sibling) {
				Assert(variableCount != FUNCTION_MAX_ARGUMENTS);
				variables[variableCount++] = lastChild;
				lastChild = lastChild->sibling;
			}

			if (!FunctionBuilderRecurse(tokenizer, setGroup ? node->firstChild->sibling : lastChild, builder, false)) {
				return false;
			}

			while (variableCount) {
				Node *child = variables[--variableCount];
				builder->isPersistentVariable = false;

				if (node->type == T_DECLARE || node->type == T_DECL_GROUP_AND_SET) {
					if (!FunctionBuilderVariable(tokenizer, builder, node->type == T_DECL_GROUP_AND_SET ? child : node, true)) {
						return false;
					}
				} else if (!FunctionBuilderRecurse(tokenizer, child, builder, true)) {
					return false;
				}

				FunctionBuilderAddLineNumber(builder, node);
				uint8_t b = builder->isListAssignment ? T_EQUALS_LIST : builder->isDotAssignment ? T_EQUALS_DOT : T_EQUALS;
				FunctionBuilderAppend(builder, &b, sizeof(b));

				if (!builder->isListAssignment) {
					FunctionBuilderAppend(builder, &builder->scopeIndex, sizeof(builder->scopeIndex));
				}

				if (builder->isPersistentVariable) {
					b = T_PERSIST;
					FunctionBuilderAppend(builder, &b, sizeof(b));
				}

				child = child->sibling;
			}
		}

		return true;
	} else if (node->type == T_CALL) {
		Node *argument = node->firstChild->sibling->firstChild;
		Node *arguments[FUNCTION_MAX_ARGUMENTS];
		size_t argumentCount = 0;

		while (argument) {
			arguments[argumentCount++] = argument;
			argument = argument->sibling;
		}

		for (uintptr_t i = 0; i < argumentCount; i++) {
			if (!FunctionBuilderRecurse(tokenizer, arguments[argumentCount - i - 1], builder, false)) {
				return false;
			}
		}

		if (!FunctionBuilderRecurse(tokenizer, node->firstChild, builder, false)) return false;
		FunctionBuilderAddLineNumber(builder, node);
		FunctionBuilderAppend(builder, &node->type, sizeof(node->type));
		return true;
	} else if (node->type == T_WHILE) {
		int32_t start = builder->dataBytes;
		if (!FunctionBuilderRecurse(tokenizer, node->firstChild, builder, false)) return false;
		FunctionBuilderAddLineNumber(builder, node);
		uint8_t b = T_IF;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		uintptr_t writeOffset = builder->dataBytes;
		uint32_t zero = 0;
		FunctionBuilderAppend(builder, &zero, sizeof(zero));
		if (!FunctionBuilderRecurse(tokenizer, node->firstChild->sibling, builder, false)) return false;
		b = T_BRANCH;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		int32_t delta = start - builder->dataBytes;
		FunctionBuilderAppend(builder, &delta, sizeof(delta));
		delta = builder->dataBytes - writeOffset;
		MemoryCopy(builder->data + writeOffset, &delta, sizeof(delta));
		return true;
	} else if (node->type == T_FOR) {
		Node *declare = node->firstChild;
		Node *condition = node->firstChild->sibling;
		Node *increment = node->firstChild->sibling->sibling;
		Node *body = node->firstChild->sibling->sibling->sibling;

		if (declare->type != T_DECLARE && declare->type != T_EQUALS) {
			PrintError2(tokenizer, node, "The first section of a for statement must be a variable declaration or an assignment.\n");
			return false;
		}

		if (!FunctionBuilderRecurse(tokenizer, declare, builder, false)) return false;
		int32_t start = builder->dataBytes;
		if (!FunctionBuilderRecurse(tokenizer, condition, builder, false)) return false;
		FunctionBuilderAddLineNumber(builder, node);
		uint8_t b = T_IF;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		uintptr_t writeOffset = builder->dataBytes;
		uint32_t zero = 0;
		FunctionBuilderAppend(builder, &zero, sizeof(zero));
		if (!FunctionBuilderRecurse(tokenizer, body, builder, false)) return false;
		if (!FunctionBuilderRecurse(tokenizer, increment, builder, false)) return false;

		if (increment->expressionType && increment->expressionType->type != T_VOID) {
			if (increment->type == T_CALL) {
				uint8_t b = T_POP;

				for (int i = 0; i < ASTGetTypePopCount(increment->expressionType); i++) {
					FunctionBuilderAppend(builder, &b, sizeof(b));
				}
			} else {
				PrintError2(tokenizer, increment, "The result of the expression is unused.\n");
				return false;
			}
		}

		b = T_BRANCH;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		int32_t delta = start - builder->dataBytes;
		FunctionBuilderAppend(builder, &delta, sizeof(delta));
		delta = builder->dataBytes - writeOffset;
		MemoryCopy(builder->data + writeOffset, &delta, sizeof(delta));
		return true;
	} else if (node->type == T_FOR_EACH) {
		Node *declare = node->firstChild;
		Node *list = node->firstChild->sibling;
		Node *body = node->firstChild->sibling->sibling;
		bool isStr = ASTMatching(list->expressionType, &globalExpressionTypeStr);

		if (declare->type != T_DECLARE) {
			PrintError2(tokenizer, node, "The left of a for-in statement must be a variable declaration.\n");
			return false;
		}

		// Declare the iteration variable.
		if (!FunctionBuilderRecurse(tokenizer, declare, builder, false)) return false;

		// Push the starting index of 0.
		uint8_t b = T_ZERO;
		FunctionBuilderAppend(builder, &b, sizeof(b));

		// Push the list.
		if (!FunctionBuilderRecurse(tokenizer, list, builder, false)) return false;

		int32_t start = builder->dataBytes;

		// Check whether the index is less than the list length.
		// Stack: list, index, ...
		b = T_SWAP;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		// Stack: index, list, ...
		b = T_DUP;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		// Stack: index, index, list, ...
		b = T_ROT3;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		// Stack: list, index, index, ...
		b = T_DUP;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		// Stack: list, list, index, index, ...
		b = T_OP_LEN;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		// Stack: length, list, index, index, ...
		b = T_ROT3;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		// Stack: index, length, list, index, ...
		b = T_GREATER_THAN;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		// Stack: comparison, list, index, ...

		FunctionBuilderAddLineNumber(builder, node);
		b = T_IF;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		uintptr_t writeOffset = builder->dataBytes;
		uint32_t zero = 0;
		FunctionBuilderAppend(builder, &zero, sizeof(zero));

		// Get the value out of the list.
		// Stack: list, index, ...
		b = T_SWAP;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		// Stack: index, list, ...
		b = T_DUP;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		// Stack: index, index, list, ...
		b = T_ROT3;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		// Stack: list, index, index, ...
		b = T_DUP;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		// Stack: list, list, index, index, ...
		b = T_ROT3;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		// Stack: index, list, list, index, ...
		b = isStr ? T_INDEX : T_INDEX_LIST;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		// Stack: list, index, ...

		// Set the iteration variable.
		Node *variableNode = (Node *) AllocateFixed(sizeof(Node));
		variableNode->type = T_IDENTIFIER;
		variableNode->token = declare->token;
		variableNode->scope = node->scope;
		variableNode->parent = node;
		FunctionBuilderVariable(tokenizer, builder, variableNode, true);
		b = T_EQUALS;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		FunctionBuilderAppend(builder, &builder->scopeIndex, sizeof(builder->scopeIndex));

		// Output the body.
		if (!FunctionBuilderRecurse(tokenizer, body, builder, false)) return false;

		// Increment the index.
		// Stack: list, index, ...
		b = T_SWAP;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		// Stack: index, list, ...
		b = T_NUMERIC_LITERAL;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		Value v;
		v.i = 1;
		FunctionBuilderAppend(builder, &v, sizeof(v));
		// Stack: 1, index, list, ...
		b = T_ADD;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		// Stack: new index, list, ...
		b = T_SWAP;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		// Stack: list, new index, ...

		b = T_BRANCH;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		int32_t delta = start - builder->dataBytes;
		FunctionBuilderAppend(builder, &delta, sizeof(delta));
		delta = builder->dataBytes - writeOffset;
		MemoryCopy(builder->data + writeOffset, &delta, sizeof(delta));

		// Pop the list and index.
		b = T_POP;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		FunctionBuilderAppend(builder, &b, sizeof(b));

		return true;
	} else if (node->type == T_IF) {
		if (!FunctionBuilderRecurse(tokenizer, node->firstChild, builder, false)) return false;

		if (node->firstChild->expressionType->type == T_ERR) {
			uint8_t b = T_OP_SUCCESS;
			FunctionBuilderAppend(builder, &b, sizeof(b));
		}

		FunctionBuilderAddLineNumber(builder, node);
		FunctionBuilderAppend(builder, &node->type, sizeof(node->type));
		uintptr_t writeOffset = builder->dataBytes, writeOffsetElse = 0;
		uint32_t zero = 0;
		FunctionBuilderAppend(builder, &zero, sizeof(zero));
		if (!FunctionBuilderRecurse(tokenizer, node->firstChild->sibling, builder, false)) return false;

		if (node->firstChild->sibling->sibling) {
			uint8_t b = T_BRANCH;
			FunctionBuilderAppend(builder, &b, sizeof(b));
			writeOffsetElse = builder->dataBytes;
			FunctionBuilderAppend(builder, &zero, sizeof(zero));
		}

		int32_t delta = builder->dataBytes - writeOffset;
		MemoryCopy(builder->data + writeOffset, &delta, sizeof(delta));

		if (node->firstChild->sibling->sibling) {
			if (!FunctionBuilderRecurse(tokenizer, node->firstChild->sibling->sibling, builder, false)) return false;
			delta = builder->dataBytes - writeOffsetElse;
			MemoryCopy(builder->data + writeOffsetElse, &delta, sizeof(delta));
		}

		return true;
	} else if (node->type == T_IF_ERR) {
		uint8_t b;
		if (!FunctionBuilderRecurse(tokenizer, node->firstChild->sibling, builder, false)) return false;
		FunctionBuilderAddLineNumber(builder, node);

		b = T_DUP;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		b = T_OP_SUCCESS;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		b = T_IF;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		uintptr_t writeOffset = builder->dataBytes, writeOffsetElse = 0;
		uint32_t zero = 0;
		FunctionBuilderAppend(builder, &zero, sizeof(zero));
		b = T_OP_ASSERT_ERR;
		FunctionBuilderAppend(builder, &b, sizeof(b));

		Node *variableNode = (Node *) AllocateFixed(sizeof(Node));
		variableNode->type = T_IDENTIFIER;
		variableNode->token = node->firstChild->token;
		variableNode->scope = node->scope;
		variableNode->parent = node;
		FunctionBuilderVariable(tokenizer, builder, variableNode, true);
		b = T_EQUALS;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		FunctionBuilderAppend(builder, &builder->scopeIndex, sizeof(builder->scopeIndex));

		if (!FunctionBuilderRecurse(tokenizer, node->firstChild->sibling->sibling, builder, false)) return false;

		b = T_BRANCH;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		writeOffsetElse = builder->dataBytes;
		FunctionBuilderAppend(builder, &zero, sizeof(zero));

		int32_t delta = builder->dataBytes - writeOffset;
		MemoryCopy(builder->data + writeOffset, &delta, sizeof(delta));

		b = T_POP;
		FunctionBuilderAppend(builder, &b, sizeof(b));

		if (node->firstChild->sibling->sibling->sibling) {
			if (!FunctionBuilderRecurse(tokenizer, node->firstChild->sibling->sibling->sibling, builder, false)) return false;
		}

		delta = builder->dataBytes - writeOffsetElse;
		MemoryCopy(builder->data + writeOffsetElse, &delta, sizeof(delta));

		return true;
	} else if (node->type == T_LOGICAL_OR || node->type == T_LOGICAL_AND) {
		if (!FunctionBuilderRecurse(tokenizer, node->firstChild, builder, false)) return false;
		FunctionBuilderAppend(builder, &node->type, sizeof(node->type));
		uintptr_t writeOffset = builder->dataBytes;
		uint32_t zero = 0;
		FunctionBuilderAppend(builder, &zero, sizeof(zero));
		if (!FunctionBuilderRecurse(tokenizer, node->firstChild->sibling, builder, false)) return false;
		int32_t delta = builder->dataBytes - writeOffset;
		MemoryCopy(builder->data + writeOffset, &delta, sizeof(delta));
		return true;
	} else if (node->type == T_NEW) {
		if (node->firstChild->type == T_ERR) {
			FunctionBuilderRecurse(tokenizer, node->firstChild->sibling, builder, false);
		}

		FunctionBuilderAddLineNumber(builder, node);
		FunctionBuilderAppend(builder, &node->type, sizeof(node->type));
		int16_t fieldCount = 0;

		if (node->firstChild->type == T_LIST) {
			fieldCount = ASTIsManagedType(node->firstChild->firstChild) ? -2 : -1;
		} else if (node->firstChild->type == T_ERR) {
			fieldCount = ASTIsManagedType(node->firstChild->firstChild) ? -4 : -3;
		} else {
			Node *child = node->firstChild->firstChild;

			while (child) { 
				if (fieldCount == 1000) {
					PrintError2(tokenizer, child, "The struct exceeds the maximum number of fields (1000).\n");
					return false;
				}

				fieldCount++; 
				child = child->sibling; 
			}
		}

		FunctionBuilderAppend(builder, &fieldCount, sizeof(fieldCount));
		return true;
	} else if (node->type == T_COLON) {
		FunctionBuilderRecurse(tokenizer, node->firstChild, builder, false);

		if (node->operationType != T_OP_CAST) {
			Node *argument = node->firstChild->sibling->firstChild;

			while (argument) {
				FunctionBuilderRecurse(tokenizer, argument, builder, false);
				argument = argument->sibling;
			}
		}

		FunctionBuilderAddLineNumber(builder, node);
		FunctionBuilderAppend(builder, &node->operationType, sizeof(node->operationType));

		if (node->operationType == T_OP_CAST) {
			FunctionBuilderAppend(builder, &node->expressionType, sizeof(node->expressionType));
		}

		return true;
	} else if (node->type == T_LIST_LITERAL) {
		// Step 1: Create the list.
		uint8_t b = T_NEW;
		int16_t isManaged = ASTIsManagedType(node->expressionType->firstChild) ? -2 : -1;
		FunctionBuilderAddLineNumber(builder, node);
		FunctionBuilderAppend(builder, &b, sizeof(b));
		FunctionBuilderAppend(builder, &isManaged, sizeof(isManaged));

		// Step 2: Resize the list.
		uint32_t size = 0;
		Node *item = node->firstChild;
		while (item) { size++; item = item->sibling; }
		b = T_DUP;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		b = T_NUMERIC_LITERAL;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		Value v;
		v.i = size;
		FunctionBuilderAppend(builder, &v, sizeof(v));
		b = T_OP_RESIZE;
		FunctionBuilderAppend(builder, &b, sizeof(b));

		// Step 3: Populate the list.
		item = node->firstChild;
		uint32_t i = 0;

		while (item) {
			b = T_DUP;
			FunctionBuilderAppend(builder, &b, sizeof(b));
			FunctionBuilderRecurse(tokenizer, item, builder, false);
			b = T_SWAP;
			FunctionBuilderAppend(builder, &b, sizeof(b));
			b = T_NUMERIC_LITERAL;
			FunctionBuilderAppend(builder, &b, sizeof(b));
			Value v;
			v.i = i++;
			FunctionBuilderAppend(builder, &v, sizeof(v));
			b = T_EQUALS_LIST;
			FunctionBuilderAppend(builder, &b, sizeof(b));
			item = item->sibling;
		}

		return true;
	}

	Node *child = node->firstChild;

	while (child) {
		if (!FunctionBuilderRecurse(tokenizer, child, builder, false)) {
			return false;
		}

		if (node->type == T_BLOCK && child->expressionType && child->expressionType->type != T_VOID) {
			if (child->type == T_CALL || child->type == T_AWAIT 
					|| (child->type == T_COLON && child->operationType == T_OP_FIND_AND_DELETE)
					|| (child->type == T_COLON && child->operationType == T_OP_FIND_AND_DEL_STR)) {
				uint8_t b = T_POP;

				for (int i = 0; i < ASTGetTypePopCount(child->expressionType); i++) {
					FunctionBuilderAppend(builder, &b, sizeof(b));
				}
			} else if (child->type == T_DECLARE || child->type == T_EQUALS) {
			} else {
				PrintError2(tokenizer, child, "The result of the expression is unused.\n");
				return false;
			}
		}

		child = child->sibling;
	}

	if (node->type == T_FUNCBODY || node->type == T_BLOCK) {
		uint8_t b = T_EXIT_SCOPE;
		uint16_t entryCount = node->scope->variableEntryCount;
		FunctionBuilderAddLineNumber(builder, node);
		FunctionBuilderAppend(builder, &b, sizeof(b));
		FunctionBuilderAppend(builder, &entryCount, sizeof(entryCount));
		b = T_END_FUNCTION;
		if (node->type == T_FUNCBODY) FunctionBuilderAppend(builder, &b, sizeof(b));
	} else if (node->type == T_DECLARE_GROUP) {
	} else if (node->type == T_RETURN || node->type == T_RETURN_TUPLE) {
		uint8_t b = T_END_FUNCTION;
		FunctionBuilderAddLineNumber(builder, node);
		FunctionBuilderAppend(builder, &b, sizeof(b));
	} else if (node->type == T_RETERR) {
		uint8_t b;
		FunctionBuilderAddLineNumber(builder, node);
		b = T_DUP;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		b = T_OP_SUCCESS;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		b = T_LOGICAL_NOT;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		b = T_IF;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		int32_t delta = 9;
		FunctionBuilderAppend(builder, &delta, sizeof(delta));
		b = T_OP_ERROR;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		b = T_NEW;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		int16_t newType = -3;
		FunctionBuilderAppend(builder, &newType, sizeof(newType));
		b = T_END_FUNCTION;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		b = T_POP;
		FunctionBuilderAppend(builder, &b, sizeof(b));
	} else if (node->type == T_NULL || node->type == T_LOGICAL_NOT || node->type == T_AWAIT || node->type == T_ERR_CAST || node->type == T_ZERO) {
		FunctionBuilderAddLineNumber(builder, node);
		FunctionBuilderAppend(builder, &node->type, sizeof(node->type));
	} else if (node->type == T_ANYTYPE_CAST) {
		FunctionBuilderAddLineNumber(builder, node);
		FunctionBuilderAppend(builder, &node->type, sizeof(node->type));
		FunctionBuilderAppend(builder, &node->firstChild->expressionType, sizeof(node->firstChild->expressionType));
	} else if (node->type == T_ASSERT) {
		FunctionBuilderAddLineNumber(builder, node);

		if (node->firstChild->expressionType->type == T_ERR) {
			uint8_t b = T_OP_ASSERT_ERR;
			FunctionBuilderAppend(builder, &b, sizeof(b));
			b = T_POP;
			FunctionBuilderAppend(builder, &b, sizeof(b));
		} else {
			FunctionBuilderAppend(builder, &node->type, sizeof(node->type));
		}
	} else if (node->type == T_ADD || node->type == T_MINUS || node->type == T_ASTERISK || node->type == T_SLASH || node->type == T_NEGATE || node->type == T_BITWISE_NOT
			|| node->type == T_BIT_SHIFT_LEFT || node->type == T_BIT_SHIFT_RIGHT 
			|| node->type == T_BITWISE_OR || node->type == T_BITWISE_AND || node->type == T_BITWISE_XOR) {
		uint8_t b = node->expressionType->type == T_FLOAT ? node->type - T_ADD + T_FLOAT_ADD 
			: node->expressionType->type == T_STR ? T_CONCAT : node->type;
		FunctionBuilderAddLineNumber(builder, node);
		FunctionBuilderAppend(builder, &b, sizeof(b));
	} else if (node->type == T_STR_INTERPOLATE) {
		Node *type = node->firstChild->sibling->expressionType;
		uint8_t b = type->type == T_STR ? T_INTERPOLATE_STR
			: type->type == T_FLOAT ? T_INTERPOLATE_FLOAT
			: (type->type == T_INT || ASTIsIntType(type)) ? T_INTERPOLATE_INT
			: (type->type == T_LIST && type->firstChild->type == T_INT) ? T_INTERPOLATE_ILIST
			: type->type == T_BOOL ? T_INTERPOLATE_BOOL : T_ERROR;
		Assert(b != T_ERROR);
		FunctionBuilderAddLineNumber(builder, node);
		FunctionBuilderAppend(builder, &b, sizeof(b));
	} else if (node->type == T_LESS_THAN || node->type == T_GREATER_THAN || node->type == T_LT_OR_EQUAL || node->type == T_GT_OR_EQUAL
			|| node->type == T_DOUBLE_EQUALS || node->type == T_NOT_EQUALS) {
		uint8_t b = node->firstChild->expressionType->type == T_STR ? node->type - T_DOUBLE_EQUALS + T_STR_DOUBLE_EQUALS 
			: node->firstChild->expressionType->type == T_FLOAT ? node->type - T_GREATER_THAN + T_FLOAT_GREATER_THAN : node->type;
		FunctionBuilderAddLineNumber(builder, node);
		FunctionBuilderAppend(builder, &b, sizeof(b));
	} else if (node->type == T_VARIABLE) {
		if (!FunctionBuilderVariable(tokenizer, builder, node, forAssignment)) {
			return false;
		}
	} else if (node->type == T_INDEX) {
		if (forAssignment) {
			if (node->firstChild->expressionType->type == T_STR) {
				PrintError2(tokenizer, node->firstChild, "Strings cannot be modified.\n");
				return false;
			} else {
				builder->isListAssignment = true;
				builder->isDotAssignment = false;
			}
		} else {
			uint8_t b = node->firstChild->expressionType->type == T_STR ? T_INDEX : T_INDEX_LIST;
			FunctionBuilderAddLineNumber(builder, node);
			FunctionBuilderAppend(builder, &b, sizeof(b));
		}
	} else if (node->type == T_DOT) {
		bool isStruct = node->firstChild->expressionType->type == T_STRUCT;

		if (isStruct) {
			Node *field = node->firstChild->expressionType->firstChild;
			int16_t fieldIndex = 0;

			while (field) {
				if (field->token.textBytes == node->token.textBytes && 
						0 == MemoryCompare(field->token.text, node->token.text, node->token.textBytes)) {
					break;
				}

				field = field->sibling;
				fieldIndex++;
			}

			if (ASTIsManagedType(field->firstChild)) {
				fieldIndex = -1 - fieldIndex;
			}

			if (forAssignment) {
				builder->scopeIndex = fieldIndex;
				builder->isDotAssignment = true;
				builder->isListAssignment = false;
			} else {
				FunctionBuilderAddLineNumber(builder, node);
				FunctionBuilderAppend(builder, &node->type, sizeof(node->type));
				FunctionBuilderAppend(builder, &fieldIndex, sizeof(fieldIndex));
			}
		} else {
			if (forAssignment) {
				PrintError2(tokenizer, node, "You cannot directly modify a variable from an imported module.\n");
				return false;
			} else {
				Node *importStatement = node->firstChild->expressionType->parent;
				Assert(importStatement->type == T_IMPORT);
				uint32_t realIndex = ScopeLookupIndex(node, importStatement->importData->rootNode->scope, false, true);
				Node *declarationNode = importStatement->importData->rootNode->scope->entries[realIndex];

				FunctionBuilderAddLineNumber(builder, node);
				uint8_t b = T_POP;
				FunctionBuilderAppend(builder, &b, sizeof(b));

				if (declarationNode->type == T_INTTYPE_CONSTANT) {
					bool error = false;
					Value intConstantValue;
					intConstantValue.i = ASTEvaluateIntConstant(tokenizer, declarationNode->firstChild, &error);
					if (error) return false;
					uint8_t b = T_NUMERIC_LITERAL;
					FunctionBuilderAppend(builder, &b, sizeof(b));
					FunctionBuilderAppend(builder, &intConstantValue, sizeof(intConstantValue));
				} else {
					uint32_t index = ScopeLookupIndex(node, importStatement->importData->rootNode->scope, false, false);
					index += importStatement->importData->globalVariableOffset;
					b = T_VARIABLE;
					FunctionBuilderAppend(builder, &b, sizeof(b));
					FunctionBuilderAppend(builder, &index, sizeof(index));
				}
			}
		}
	} else if (node->type == T_NUMERIC_LITERAL) {
		FunctionBuilderAddLineNumber(builder, node);
		FunctionBuilderAppend(builder, &node->type, sizeof(node->type));
		Value v = ASTNumericLiteralToValue(node);
		FunctionBuilderAppend(builder, &v, sizeof(v));
	} else if (node->type == T_STRING_LITERAL) {
		FunctionBuilderAddLineNumber(builder, node);
		FunctionBuilderAppend(builder, &node->type, sizeof(node->type));
		uint32_t textBytes = node->token.textBytes;
		FunctionBuilderAppend(builder, &textBytes, sizeof(textBytes));
		FunctionBuilderAppend(builder, node->token.text, textBytes);
	} else if (node->type == T_TRUE || node->type == T_FALSE) {
		FunctionBuilderAddLineNumber(builder, node);
		uint8_t b = T_NUMERIC_LITERAL;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		Value v;
		v.i = node->type == T_TRUE ? 1 : 0;
		FunctionBuilderAppend(builder, &v, sizeof(v));
	} else if (node->type == T_SUCCESS) {
		uint8_t b = T_NUMERIC_LITERAL;
		FunctionBuilderAppend(builder, &b, sizeof(b));
		Value v;
		v.i = 0;
		FunctionBuilderAppend(builder, &v, sizeof(v));
		b = T_ERR_CAST;
		FunctionBuilderAppend(builder, &b, sizeof(b));
	} else if (node->type == T_REPL_RESULT) {
		if (!ASTMatching(node->firstChild->expressionType, &globalExpressionTypeVoid)) {
			FunctionBuilderAppend(builder, &node->type, sizeof(node->type));
		}

		if (builder->replResultType) {
			PrintError2(tokenizer, node, "Multiple REPL results are not allowed.\n");
			return false;
		}

		builder->replResultType = node->firstChild->expressionType;
	} else {
		PrintDebug("FunctionBuilderRecurse %d\n", node->type);
		Assert(false);
	}

	return true;
}

bool ASTGenerate(Tokenizer *tokenizer, Node *root, ExecutionContext *context) {
	Node *child = root->firstChild;

	context->functionData->globalVariableOffset = context->globalVariableCount;
	context->globalVariableCount += root->scope->variableEntryCount;
	context->globalVariables = (Value *) AllocateResize(context->globalVariables, sizeof(Value) * context->globalVariableCount);
	context->globalVariableIsManaged = (bool *) AllocateResize(context->globalVariableIsManaged, sizeof(Value) * context->globalVariableCount);

	for (uintptr_t i = 0; i < root->scope->variableEntryCount; i++) {
		context->globalVariables[context->functionData->globalVariableOffset + i].i = 0;
		context->globalVariableIsManaged[context->functionData->globalVariableOffset + i] = false;
	}

	while (child) {
		if (child->type == T_FUNCTION) {
			uintptr_t variableIndex = context->functionData->globalVariableOffset + ScopeLookupIndex(child, root->scope, false, false);
			uintptr_t heapIndex = HeapAllocate(context);
			context->globalVariableIsManaged[variableIndex] = true;
			context->heap[heapIndex].type = T_FUNCPTR;
			context->heap[heapIndex].lambdaID = context->functionData->dataBytes;
			context->globalVariables[variableIndex].i = heapIndex;

			if (child->isExternalCall && child->token.module->library) {
				char name[256];

				if (child->token.textBytes > sizeof(name) - 1) {
					PrintError2(tokenizer, child, "The function name is too long to be loaded from a library.\n");
					return NULL;
				}

				MemoryCopy(name, child->token.text, child->token.textBytes);
				name[child->token.textBytes] = 0;

				void *address = LibraryGetAddress(child->token.module->library, name);
				if (!address) return false;
				uint8_t b = T_LIBCALL;
				FunctionBuilderAppend(context->functionData, &b, sizeof(b));
				FunctionBuilderAppend(context->functionData, &address, sizeof(address));
			} else if (child->isExternalCall) {
				uint8_t b = T_EXTCALL;

				uint16_t index = 0xFFFF;
				
				for (uintptr_t i = 0; i < sizeof(externalFunctions) / sizeof(externalFunctions[0]); i++) {
					bool match = true;

					for (uintptr_t j = 0; j <= child->token.textBytes; j++) {
						if (externalFunctions[i].cName[j] != (j == child->token.textBytes ? 0 : child->token.text[j])) {
							match = false;
							break;
						}
					}

					if (match) {
						index = i;
						break;
					}
				}

				if (index == 0xFFFF) {
					PrintError2(tokenizer, child, "No such external function '%.*s'.\n", child->token.textBytes, child->token.text);
					return false;
				}

				FunctionBuilderAppend(context->functionData, &b, sizeof(b));
				FunctionBuilderAppend(context->functionData, &index, sizeof(index));
			} else {
				if (!FunctionBuilderRecurse(tokenizer, child->firstChild->sibling, context->functionData, false)) return false;
			}
		} else if (child->type == T_DECLARE) {
			if (child->isPersistentVariable && context->mainModule != tokenizer->module) {
				PrintError2(tokenizer, child, "Persistent variables are not allowed in imported modules.\n");
				return false;
			}

			context->globalVariables[context->functionData->globalVariableOffset + ScopeLookupIndex(child, root->scope, false, false)].i = 0;
			context->globalVariableIsManaged[context->functionData->globalVariableOffset + ScopeLookupIndex(child, root->scope, false, false)] = ASTIsManagedType(child->expressionType);
		}

		child = child->sibling;
	}

	return true;
}

// --------------------------------- Main script execution.

void HeapGarbageCollectMark(ExecutionContext *context, uintptr_t index) {
	start:;
	Assert(index < context->heapEntriesAllocated);
	if (context->heap[index].gcMark) return;
	context->heap[index].gcMark = true;

	if (context->heap[index].type == T_EOF || context->heap[index].type == T_STR || context->heap[index].type == T_FUNCPTR) {
		// Nothing else to mark.
	} else if (context->heap[index].type == T_STRUCT) {
		for (uintptr_t i = 0; i < context->heap[index].fieldCount; i++) {
			if (((uint8_t *) context->heap[index].fields)[-1 - i]) {
				HeapGarbageCollectMark(context, context->heap[index].fields[i].i);
			}
		}
	} else if (context->heap[index].type == T_LIST) {
		if (context->heap[index].internalValuesAreManaged) {
			for (uintptr_t i = 0; i < context->heap[index].length; i++) {
				HeapGarbageCollectMark(context, context->heap[index].list[i].i);
			}
		}
	} else if (context->heap[index].type == T_CONCAT) {
		uintptr_t index1 = context->heap[index].concat1;
		uintptr_t index2 = context->heap[index].concat2;

		if (context->heap[index1].type == T_CONCAT) {
			HeapGarbageCollectMark(context, index2);
			index = index1;
		} else {
			HeapGarbageCollectMark(context, index1);
			index = index2;
		}

		goto start;
	} else if (context->heap[index].type == T_OP_DISCARD || context->heap[index].type == T_OP_ASSERT) {
		HeapGarbageCollectMark(context, context->heap[index].lambdaID);
	} else if (context->heap[index].type == T_OP_CURRY) {
		HeapGarbageCollectMark(context, context->heap[index].lambdaID);

		if (context->heap[index].internalValuesAreManaged) {
			HeapGarbageCollectMark(context, context->heap[index].curryValue.i);
		}
	} else if (context->heap[index].type == T_ERR) {
		if (context->heap[index].internalValuesAreManaged) {
			HeapGarbageCollectMark(context, context->heap[index].errorValue.i);
		}
	} else if (context->heap[index].type == T_ANYTYPE) {
		if (context->heap[index].internalValuesAreManaged) {
			HeapGarbageCollectMark(context, context->heap[index].anyValue.i);
		}
	} else {
		Assert(false);
	}
}

void HeapFreeEntry(ExecutionContext *context, uintptr_t i) {
	if (context->heap[i].type == T_STR) {
		AllocateResize(context->heap[i].text, 0);
	} else if (context->heap[i].type == T_STRUCT) {
		AllocateResize((uint8_t *) context->heap[i].fields - context->heap[i].fieldCount, 0);
	} else if (context->heap[i].type == T_LIST) {
		AllocateResize(context->heap[i].list, 0);
	} else if (context->heap[i].type == T_OP_DISCARD || context->heap[i].type == T_OP_ASSERT 
			|| context->heap[i].type == T_FUNCPTR || context->heap[i].type == T_OP_CURRY
			|| context->heap[i].type == T_CONCAT || context->heap[i].type == T_ERR
			|| context->heap[i].type == T_ANYTYPE) {
	} else {
		Assert(false);
	}

	context->heap[i].type = T_ERROR;
}

uintptr_t HeapAllocate(ExecutionContext *context) {
	if (!context->heapFirstUnusedEntry) {
		// All heapEntriesAllocated entries are in use.

		for (uintptr_t i = 0; i < context->heapEntriesAllocated; i++) {
			context->heap[i].gcMark = false;
		}

		for (uintptr_t i = 0; i < context->globalVariableCount; i++) {
			if (context->globalVariableIsManaged[i]) {
				HeapGarbageCollectMark(context, context->globalVariables[i].i);
			}
		}

		CoroutineState *c = context->allCoroutines;

		while (c) {
			for (uintptr_t i = 0; i < c->localVariableCount; i++) {
				if (c->localVariableIsManaged[i]) {
					HeapGarbageCollectMark(context, c->localVariables[i].i);
				}
			}

			for (uintptr_t i = 0; i < c->stackPointer; i++) {
				if (c->stackIsManaged[i]) {
					HeapGarbageCollectMark(context, c->stack[i].i);
				}
			}

			c = c->nextCoroutine;
		}
		
		uintptr_t *link = &context->heapFirstUnusedEntry;
		uintptr_t reclaimed = 0;

		for (uintptr_t i = 1; i < context->heapEntriesAllocated; i++) {
			if (!context->heap[i].gcMark && !context->heap[i].externalReferenceCount) {
				// PrintDebug("\033[0;32mFreeing index %d...\033[0m\n", i);
				HeapFreeEntry(context, i);
				*link = i;
				link = &context->heap[i].nextUnusedEntry;
				reclaimed++;
			}
		}

		if (reclaimed <= context->heapEntriesAllocated / 5) {
			// PrintDebug("\033[0;32mFreed only %d/%d entries. Doubling heap size...\033[0m\n", reclaimed, context->heapEntriesAllocated);

			intptr_t linkIndex = link == &context->heapFirstUnusedEntry ? -1 
				: ((intptr_t) link - (intptr_t) context->heap) / (intptr_t) sizeof(HeapEntry);
			uintptr_t oldSize = context->heapEntriesAllocated;
			context->heapEntriesAllocated *= 2;
			context->heap = (HeapEntry *) AllocateResize(context->heap, context->heapEntriesAllocated * sizeof(HeapEntry));
			link = linkIndex == -1 ? &context->heapFirstUnusedEntry : &context->heap[linkIndex].nextUnusedEntry;

			for (uintptr_t i = oldSize; i < context->heapEntriesAllocated; i++) {
				context->heap[i].type = T_ERROR;
				*link = i;
				link = &context->heap[i].nextUnusedEntry;
			}
		} else {
			// PrintDebug("\033[0;32mFreed %d/%d entries.\033[0m\n", reclaimed, context->heapEntriesAllocated);
		}

		*link = 0;
	}

	uintptr_t index = context->heapFirstUnusedEntry;
	Assert(index);
	context->heapFirstUnusedEntry = context->heap[index].nextUnusedEntry;
	return index;
}

void ScriptPrintNode(Node *node, int indent) {
	for (int i = 0; i < indent; i++) {
		PrintDebug("\t");
	}

	PrintDebug("%d l%d '%.*s'\n", node->type, node->token.line, node->token.textBytes, node->token.text);

	Node *child = node->firstChild;

	while (child) {
		ScriptPrintNode(child, indent + 1);
		child = child->sibling;
	}
}

size_t ScriptHeapEntryGetStringBytes(HeapEntry *entry) {
	if (entry->type == T_STR) {
		return entry->bytes;
	} else if (entry->type == T_EOF) {
		return 0;
	} else if (entry->type == T_CONCAT) {
		return entry->concatBytes;
	} else {
		Assert(false);
		return 0;
	}
}

void ScriptHeapEntryConcatConvertToStringWrite(ExecutionContext *context, HeapEntry *entry, char *buffer) {
	while (true) {
		if (entry->type == T_STR) {
			MemoryCopy(buffer, entry->text, entry->bytes);
		} else if (entry->type == T_EOF) {
		} else if (entry->type == T_CONCAT) {
			HeapEntry *part1 = &context->heap[entry->concat1], *part2 = &context->heap[entry->concat2];
			size_t part1Bytes = ScriptHeapEntryGetStringBytes(part1);

			if (part1->type == T_CONCAT) {
				ScriptHeapEntryConcatConvertToStringWrite(context, part2, buffer + part1Bytes);
				Assert(part2->type != T_CONCAT);
				entry = part1;
				continue;
			} else if (part2->type == T_CONCAT) {
				ScriptHeapEntryConcatConvertToStringWrite(context, part1, buffer);
				Assert(part1->type != T_CONCAT);
				entry = part2;
				buffer += part1Bytes;
				continue;
			} else {
				ScriptHeapEntryConcatConvertToStringWrite(context, part1, buffer);
				ScriptHeapEntryConcatConvertToStringWrite(context, part2, buffer + part1Bytes);
			}
		} else {
			Assert(false);
		}

		break;
	}
}

void ScriptHeapEntryConcatConvertToString(ExecutionContext *context, HeapEntry *entry) {
	// TODO Efficient concatenation of many strings.
	// TODO Preventing stack overflow.
	Assert(entry->type == T_CONCAT);
	HeapEntry *part1 = &context->heap[entry->concat1], *part2 = &context->heap[entry->concat2];
	size_t part1Bytes = ScriptHeapEntryGetStringBytes(part1), part2Bytes = ScriptHeapEntryGetStringBytes(part2);
	Assert(entry->concatBytes == part1Bytes + part2Bytes);
	entry->type = T_STR;
	entry->bytes = part1Bytes + part2Bytes;
	entry->text = (char *) AllocateResize(NULL, entry->bytes);
	ScriptHeapEntryConcatConvertToStringWrite(context, part1, entry->text);
	ScriptHeapEntryConcatConvertToStringWrite(context, part2, entry->text + part1Bytes);
}

void ScriptHeapEntryToString(ExecutionContext *context, HeapEntry *entry, const char **text, size_t *bytes) {
	if (entry->type == T_STR) {
		*text = entry->text;
		*bytes = entry->bytes;
	} else if (entry->type == T_EOF) {
		*text = "";
		*bytes = 0;
	} else if (entry->type == T_CONCAT) {
		ScriptHeapEntryConcatConvertToString(context, entry);
		ScriptHeapEntryToString(context, entry, text, bytes);
	} else {
		Assert(false);
		*text = "";
		*bytes = 0;
	}
}

int ScriptExecuteFunction(uintptr_t instructionPointer, ExecutionContext *context) {
	// TODO Things to verify if loading untrusted scripts -- is this a feature we will need?
	// 	Checking we don't go off the end of the function body.
	// 	Checking that this is actually a valid function body pointer.
	// 	Checking various integer overflows.

	uintptr_t variableBase = context->c->localVariableCount - 1;
	uint8_t *functionData = context->functionData->data;

	while (true) {
		uint8_t command = functionData[instructionPointer++];

		if (debugBytecodeLevel >= 1) {
			PrintDebug("--> %d, %ld, %ld, %ld\n", command, instructionPointer - 1, context->c->id, context->c->stackPointer);
			if (debugBytecodeLevel >= 2) PrintBackTrace(context, instructionPointer - 1, context->c, "");
		}

		if (command == T_BLOCK || command == T_FUNCBODY) {
			uint16_t newVariableCount = functionData[instructionPointer + 0] + (functionData[instructionPointer + 1] << 8); 
			instructionPointer += 2;

			if (context->c->localVariableCount + newVariableCount > context->c->localVariablesAllocated) {
				// TODO Handling memory errors here.
				context->c->localVariablesAllocated = context->c->localVariableCount + newVariableCount;
				context->c->localVariables = (Value *) AllocateResize(context->c->localVariables, context->c->localVariablesAllocated * sizeof(Value)); 
				context->c->localVariableIsManaged = (bool *) AllocateResize(context->c->localVariableIsManaged, context->c->localVariablesAllocated * sizeof(bool)); 
			}

			MemoryCopy(context->c->localVariableIsManaged + context->c->localVariableCount, functionData + instructionPointer, newVariableCount);
			instructionPointer += newVariableCount;

			for (uintptr_t i = context->c->localVariableCount; i < context->c->localVariableCount + newVariableCount; i++) {
				if (command == T_FUNCBODY) {
					if (context->c->stackPointer < 1) return -1;
					context->c->localVariables[i] = context->c->stack[--context->c->stackPointer];
				} else {
					Value zero = { 0 };
					context->c->localVariables[i] = zero;
				}
			}

			context->c->localVariableCount += newVariableCount;
		} else if (command == T_EXIT_SCOPE) {
			uint16_t count = functionData[instructionPointer + 0] + (functionData[instructionPointer + 1] << 8); 
			instructionPointer += 2;
			if (context->c->localVariableCount < count) return -1;
			context->c->localVariableCount -= count;
		} else if (command == T_NUMERIC_LITERAL) {
			if (context->c->stackPointer == context->c->stackEntriesAllocated) {
				PrintError4(context, instructionPointer - 1, "Stack overflow.\n");
				return 0;
			}

			context->c->stackIsManaged[context->c->stackPointer] = false;
			MemoryCopy(&context->c->stack[context->c->stackPointer++], &functionData[instructionPointer], sizeof(Value));
			instructionPointer += sizeof(Value);
		} else if (command == T_NULL || command == T_ZERO) {
			if (context->c->stackPointer == context->c->stackEntriesAllocated) {
				PrintError4(context, instructionPointer - 1, "Stack overflow.\n");
				return 0;
			}

			context->c->stackIsManaged[context->c->stackPointer] = command == T_NULL;
			context->c->stack[context->c->stackPointer++].i = 0;
		} else if (command == T_STRING_LITERAL) {
			if (context->c->stackPointer == context->c->stackEntriesAllocated) {
				PrintError4(context, instructionPointer - 1, "Stack overflow.\n");
				return 0;
			}

			uint32_t textBytes;
			MemoryCopy(&textBytes, &functionData[instructionPointer], sizeof(textBytes));
			instructionPointer += sizeof(textBytes);

			// TODO Handle memory allocation failures here.
			uintptr_t index = HeapAllocate(context);
			context->heap[index].type = T_STR;
			context->heap[index].text = (char *) AllocateResize(NULL, textBytes);
			context->heap[index].bytes = textBytes;
			MemoryCopy(context->heap[index].text, &functionData[instructionPointer], textBytes);
			instructionPointer += textBytes;

			Value v;
			v.i = index;
			context->c->stackIsManaged[context->c->stackPointer] = true;
			context->c->stack[context->c->stackPointer++] = v;
		} else if (command == T_CONCAT) {
			if (context->c->stackPointer < 2) return -1;
			uint64_t index1 = context->c->stack[context->c->stackPointer - 2].i;
			uint64_t index2 = context->c->stack[context->c->stackPointer - 1].i;
			if (!context->c->stackIsManaged[context->c->stackPointer - 2]) return -1;
			if (!context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;
			if (context->heapEntriesAllocated <= index1) return -1;
			if (context->heapEntriesAllocated <= index2) return -1;
			Assert(index1 <= 0xFFFFFFFF && index2 <= 0xFFFFFFFF);
			size_t bytes1 = ScriptHeapEntryGetStringBytes(&context->heap[index1]);
			size_t bytes2 = ScriptHeapEntryGetStringBytes(&context->heap[index2]);
			uintptr_t index = HeapAllocate(context); // TODO Handle memory allocation failures here.
			context->heap[index].type = T_CONCAT;
			context->heap[index].concat1 = index1;
			context->heap[index].concat2 = index2;
			context->heap[index].concatBytes = bytes1 + bytes2;

			// At most one argument can be a T_CONCAT (ohterwise converting to a string could stack overflow).
			if (context->heap[index1].type == T_CONCAT && context->heap[index2].type == T_CONCAT) {
				ScriptHeapEntryConcatConvertToString(context, bytes1 < bytes2 ? &context->heap[index1] : &context->heap[index2]);
			}

			context->c->stack[context->c->stackPointer - 2].i = index;
			context->c->stackPointer--;
		} else if (command == T_INTERPOLATE_STR || command == T_INTERPOLATE_BOOL 
				|| command == T_INTERPOLATE_INT || command == T_INTERPOLATE_FLOAT
				|| command == T_INTERPOLATE_ILIST) {
			STACK_READ_STRING(text1, bytes1, 3);
			STACK_READ_STRING(text3, bytes3, 1);

			char *freeText = NULL;
			const char *text2 = "";
			size_t bytes2 = 0;
			char temp[30];

			if (command == T_INTERPOLATE_STR) {
				STACK_READ_STRING(entryText2, entryBytes2, 2);
				text2 = entryText2, bytes2 = entryBytes2;
			} else if (command == T_INTERPOLATE_BOOL) {
				text2 = context->c->stack[context->c->stackPointer - 2].i ? "true" : "false";
				bytes2 = context->c->stack[context->c->stackPointer - 2].i ? 4 : 5;
			} else if (command == T_INTERPOLATE_INT) {
				text2 = temp;
				bytes2 = PrintIntegerToBuffer(temp, sizeof(temp), context->c->stack[context->c->stackPointer - 2].i);
			} else if (command == T_INTERPOLATE_FLOAT) {
				text2 = temp;
				bytes2 = PrintFloatToBuffer(temp, sizeof(temp), context->c->stack[context->c->stackPointer - 2].f);
			} else if (command == T_INTERPOLATE_ILIST) {
				if (!context->c->stackIsManaged[context->c->stackPointer - 2]) return -1;
				uint64_t index2 = context->c->stack[context->c->stackPointer - 2].i;
				if (context->heapEntriesAllocated <= index2) return -1;
				HeapEntry *entry2 = &context->heap[index2];
				if (entry2->type != T_EOF && entry2->type != T_LIST) return -1;

				if (entry2->type == T_EOF) {
					text2 = "null";
					bytes2 = 4;
				} else if (entry2->length == 0) {
					text2 = "[]";
					bytes2 = 2;
				} else {
					if (entry2->internalValuesAreManaged) return -1;
					bytes2 = 4;

					for (uintptr_t i = 0; i < entry2->length; i++) {
						bytes2 += PrintIntegerToBuffer(temp, sizeof(temp), entry2->list[i].i) + 2;
					}

					freeText = (char *) AllocateResize(freeText, bytes2);
					text2 = freeText;
					bytes2 = 0;
					freeText[bytes2++] = '[';
					freeText[bytes2++] = ' ';

					for (uintptr_t i = 0; i < entry2->length; i++) {
						bytes2 += PrintIntegerToBuffer(freeText + bytes2, sizeof(temp) /* enough space */, entry2->list[i].i);
						freeText[bytes2++] = ',';
						freeText[bytes2++] = ' ';
					}

					bytes2 -= 2;
					freeText[bytes2++] = ' ';
					freeText[bytes2++] = ']';
				}
			}

			// TODO Handle memory allocation failures here.
			uintptr_t index = HeapAllocate(context);
			context->heap[index].type = T_STR;
			context->heap[index].bytes = bytes1 + bytes2 + bytes3;
			context->heap[index].text = (char *) AllocateResize(NULL, context->heap[index].bytes);
			if (bytes1) MemoryCopy(context->heap[index].text + 0,               text1, bytes1);
			if (bytes2) MemoryCopy(context->heap[index].text + bytes1,          text2, bytes2);
			if (bytes3) MemoryCopy(context->heap[index].text + bytes1 + bytes2, text3, bytes3);
			context->c->stack[context->c->stackPointer - 3].i = index;

			if (freeText) AllocateResize(freeText, 0);

			context->c->stackPointer -= 2;
		} else if (command == T_VARIABLE) {
			if (context->c->stackPointer == context->c->stackEntriesAllocated) {
				PrintDebug("Stack overflow.\n");
				return -1;
			}

			int32_t scopeIndex;
			MemoryCopy(&scopeIndex, &functionData[instructionPointer], sizeof(scopeIndex));
			instructionPointer += sizeof(scopeIndex);

			if (scopeIndex >= 0) {
				if ((uintptr_t) scopeIndex >= context->globalVariableCount) return -1;
				context->c->stackIsManaged[context->c->stackPointer] = context->globalVariableIsManaged[scopeIndex];
				context->c->stack[context->c->stackPointer++] = context->globalVariables[scopeIndex];
			} else {
				scopeIndex = variableBase - scopeIndex;
				if ((uintptr_t) scopeIndex >= context->c->localVariableCount) return -1;
				context->c->stackIsManaged[context->c->stackPointer] = context->c->localVariableIsManaged[scopeIndex];
				context->c->stack[context->c->stackPointer++] = context->c->localVariables[scopeIndex];
			}
		} else if (command == T_EQUALS) {
			if (!context->c->stackPointer) return -1;
			int32_t scopeIndex;
			MemoryCopy(&scopeIndex, &functionData[instructionPointer], sizeof(scopeIndex));
			instructionPointer += sizeof(scopeIndex);

			if (scopeIndex >= 0) {
				if ((uintptr_t) scopeIndex >= context->globalVariableCount) return -1;
				if (context->globalVariableIsManaged[scopeIndex] != context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;
				context->globalVariables[scopeIndex] = context->c->stack[--context->c->stackPointer];
			} else {
				scopeIndex = variableBase - scopeIndex;
				if ((uintptr_t) scopeIndex >= context->c->localVariableCount) return -1;
				if (context->c->localVariableIsManaged[scopeIndex] != context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;
				context->c->localVariables[scopeIndex] = context->c->stack[--context->c->stackPointer];
			}
		} else if (command == T_EQUALS_DOT) {
			if (context->c->stackPointer < 2) return -1;
			if (!context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;

			uint64_t index = context->c->stack[context->c->stackPointer - 1].i;

			if (!index) {
				PrintError4(context, instructionPointer - 1, "The struct is null.\n");
				return 0;
			}

			if (context->heapEntriesAllocated <= index) return -1;
			HeapEntry *entry = &context->heap[index];
			if (entry->type != T_STRUCT) return -1;

			int32_t fieldIndex;
			MemoryCopy(&fieldIndex, &functionData[instructionPointer], sizeof(fieldIndex));
			instructionPointer += sizeof(fieldIndex);
			bool isManaged = fieldIndex < 0;
			if (isManaged) fieldIndex = -fieldIndex - 1;
			if (fieldIndex < 0 || fieldIndex >= entry->fieldCount) return -1;

			entry->fields[fieldIndex] = context->c->stack[context->c->stackPointer - 2];
			if (isManaged != context->c->stackIsManaged[context->c->stackPointer - 2]) return -1;
			((uint8_t *) entry->fields - 1)[-fieldIndex] = isManaged;

			context->c->stackPointer -= 2;
		} else if (command == T_EQUALS_LIST) {
			if (context->c->stackPointer < 3) return -1;
			if (context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;
			if (!context->c->stackIsManaged[context->c->stackPointer - 2]) return -1;

			uint64_t index = context->c->stack[context->c->stackPointer - 2].i;

			if (!index) {
				PrintError4(context, instructionPointer - 1, "The list is null.\n");
				return 0;
			}

			if (context->heapEntriesAllocated <= index) return -1;
			HeapEntry *entry = &context->heap[index];
			if (entry->type != T_LIST) return -1;

			index = context->c->stack[context->c->stackPointer - 1].i;

			if (index >= entry->length) {
				PrintError4(context, instructionPointer - 1, "The index %ld is not valid for the list, which has length %d.\n", index, entry->length);
				return 0;
			}

			entry->list[index] = context->c->stack[context->c->stackPointer - 3];
			if (entry->internalValuesAreManaged != context->c->stackIsManaged[context->c->stackPointer - 3]) return -1;

			context->c->stackPointer -= 3;
		} else if (command == T_INDEX_LIST) {
			if (context->c->stackPointer < 2) return -1;
			if (context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;
			if (!context->c->stackIsManaged[context->c->stackPointer - 2]) return -1;

			uint64_t index = context->c->stack[context->c->stackPointer - 2].i;

			if (!index) {
				PrintError4(context, instructionPointer - 1, "The list is null.\n");
				return 0;
			}

			if (context->heapEntriesAllocated <= index) return -1;
			HeapEntry *entry = &context->heap[index];
			if (entry->type != T_LIST) return -1;

			index = context->c->stack[context->c->stackPointer - 1].i;

			if (index >= entry->length) {
				PrintError4(context, instructionPointer - 1, "The index %ld is not valid for the list, which has length %d.\n", index, entry->length);
				return 0;
			}

			context->c->stack[context->c->stackPointer - 2] = entry->list[index];
			context->c->stackIsManaged[context->c->stackPointer - 2] = entry->internalValuesAreManaged;
			context->c->stackPointer--;
		} else if (command == T_OP_FIRST || command == T_OP_LAST) {
			if (context->c->stackPointer < 1) return -1;
			if (!context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;

			uint64_t index = context->c->stack[context->c->stackPointer - 1].i;

			if (!index) {
				PrintError4(context, instructionPointer - 1, "The list is null.\n");
				return 0;
			}

			if (context->heapEntriesAllocated <= index) return -1;
			HeapEntry *entry = &context->heap[index];
			if (entry->type != T_LIST) return -1;

			if (!entry->length) {
				PrintError4(context, instructionPointer - 1, "The list is empty.\n");
				return 0;
			}

			context->c->stack[context->c->stackPointer - 1] = entry->list[command == T_OP_FIRST ? 0 : entry->length - 1];
			context->c->stackIsManaged[context->c->stackPointer - 1] = entry->internalValuesAreManaged;
		} else if (command == T_DOT) {
			if (context->c->stackPointer < 1) return -1;
			if (!context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;

			uint64_t index = context->c->stack[context->c->stackPointer - 1].i;

			if (!index) {
				PrintError4(context, instructionPointer - 1, "The struct is null.\n");
				return 0;
			}

			if (context->heapEntriesAllocated <= index) return -1;
			HeapEntry *entry = &context->heap[index];
			if (entry->type != T_STRUCT) return -1;

			int16_t fieldIndex;
			MemoryCopy(&fieldIndex, &functionData[instructionPointer], sizeof(fieldIndex));
			instructionPointer += sizeof(fieldIndex);
			bool isManaged = fieldIndex < 0;
			if (isManaged) fieldIndex = -fieldIndex - 1;
			if (fieldIndex < 0 || fieldIndex >= entry->fieldCount) return -1;

			// Only allow the isManaged bool to be incorrect if it's a null managed variable.
			if (isManaged != ((uint8_t *) entry->fields - 1)[-fieldIndex] && (entry->fields[fieldIndex].i || !isManaged)) return -1;

			context->c->stack[context->c->stackPointer - 1] = entry->fields[fieldIndex];
			context->c->stackIsManaged[context->c->stackPointer - 1] = isManaged;
		} else if (command == T_BIT_SHIFT_LEFT) {
			if (context->c->stackPointer < 2) return -1;
			context->c->stack[context->c->stackPointer - 2].i = context->c->stack[context->c->stackPointer - 2].i << context->c->stack[context->c->stackPointer - 1].i;
			context->c->stackPointer--;
		} else if (command == T_BIT_SHIFT_RIGHT) {
			if (context->c->stackPointer < 2) return -1;
			context->c->stack[context->c->stackPointer - 2].i = context->c->stack[context->c->stackPointer - 2].i >> context->c->stack[context->c->stackPointer - 1].i;
			context->c->stackPointer--;
		} else if (command == T_BITWISE_OR) {
			if (context->c->stackPointer < 2) return -1;
			context->c->stack[context->c->stackPointer - 2].i = context->c->stack[context->c->stackPointer - 2].i | context->c->stack[context->c->stackPointer - 1].i;
			context->c->stackPointer--;
		} else if (command == T_BITWISE_AND) {
			if (context->c->stackPointer < 2) return -1;
			context->c->stack[context->c->stackPointer - 2].i = context->c->stack[context->c->stackPointer - 2].i & context->c->stack[context->c->stackPointer - 1].i;
			context->c->stackPointer--;
		} else if (command == T_BITWISE_XOR) {
			if (context->c->stackPointer < 2) return -1;
			context->c->stack[context->c->stackPointer - 2].i = context->c->stack[context->c->stackPointer - 2].i ^ context->c->stack[context->c->stackPointer - 1].i;
			context->c->stackPointer--;
		} else if (command == T_ADD) {
			if (context->c->stackPointer < 2) return -1;
			context->c->stack[context->c->stackPointer - 2].i = context->c->stack[context->c->stackPointer - 2].i + context->c->stack[context->c->stackPointer - 1].i;
			context->c->stackPointer--;
		} else if (command == T_MINUS) {
			if (context->c->stackPointer < 2) return -1;
			context->c->stack[context->c->stackPointer - 2].i = context->c->stack[context->c->stackPointer - 2].i - context->c->stack[context->c->stackPointer - 1].i;
			context->c->stackPointer--;
		} else if (command == T_ASTERISK) {
			if (context->c->stackPointer < 2) return -1;
			context->c->stack[context->c->stackPointer - 2].i = context->c->stack[context->c->stackPointer - 2].i * context->c->stack[context->c->stackPointer - 1].i;
			context->c->stackPointer--;
		} else if (command == T_SLASH) {
			if (context->c->stackPointer < 2) return -1;

			if (0 == context->c->stack[context->c->stackPointer - 1].i) {
				PrintError4(context, instructionPointer - 1, "Attempted division by zero.\n");
				return 0;
			}

			context->c->stack[context->c->stackPointer - 2].i = context->c->stack[context->c->stackPointer - 2].f / context->c->stack[context->c->stackPointer - 1].f;
			context->c->stackPointer--;
		} else if (command == T_NEGATE) {
			if (context->c->stackPointer < 1) return -1;
			context->c->stack[context->c->stackPointer - 1].i = -context->c->stack[context->c->stackPointer - 1].i;
		} else if (command == T_BITWISE_NOT) {
			if (context->c->stackPointer < 1) return -1;
			context->c->stack[context->c->stackPointer - 1].i = ~context->c->stack[context->c->stackPointer - 1].i;
		} else if (command == T_FLOAT_ADD) {
			if (context->c->stackPointer < 2) return -1;
			context->c->stack[context->c->stackPointer - 2].f = context->c->stack[context->c->stackPointer - 2].f + context->c->stack[context->c->stackPointer - 1].f;
			context->c->stackPointer--;
		} else if (command == T_FLOAT_MINUS) {
			if (context->c->stackPointer < 2) return -1;
			context->c->stack[context->c->stackPointer - 2].f = context->c->stack[context->c->stackPointer - 2].f - context->c->stack[context->c->stackPointer - 1].f;
			context->c->stackPointer--;
		} else if (command == T_FLOAT_ASTERISK) {
			if (context->c->stackPointer < 2) return -1;
			context->c->stack[context->c->stackPointer - 2].f = context->c->stack[context->c->stackPointer - 2].f * context->c->stack[context->c->stackPointer - 1].f;
			context->c->stackPointer--;
		} else if (command == T_FLOAT_SLASH) {
			if (context->c->stackPointer < 2) return -1;
			context->c->stack[context->c->stackPointer - 2].f = context->c->stack[context->c->stackPointer - 2].f / context->c->stack[context->c->stackPointer - 1].f;
			context->c->stackPointer--;
		} else if (command == T_FLOAT_NEGATE) {
			if (context->c->stackPointer < 1) return -1;
			context->c->stack[context->c->stackPointer - 1].f = -context->c->stack[context->c->stackPointer - 1].f;
		} else if (command == T_LESS_THAN) {
			if (context->c->stackPointer < 2) return -1;
			context->c->stack[context->c->stackPointer - 2].i = context->c->stack[context->c->stackPointer - 2].i < context->c->stack[context->c->stackPointer - 1].i;
			context->c->stackPointer--;
		} else if (command == T_GREATER_THAN) {
			if (context->c->stackPointer < 2) return -1;
			context->c->stack[context->c->stackPointer - 2].i = context->c->stack[context->c->stackPointer - 2].i > context->c->stack[context->c->stackPointer - 1].i;
			context->c->stackPointer--;
		} else if (command == T_LT_OR_EQUAL) {
			if (context->c->stackPointer < 2) return -1;
			context->c->stack[context->c->stackPointer - 2].i = context->c->stack[context->c->stackPointer - 2].i <= context->c->stack[context->c->stackPointer - 1].i;
			context->c->stackPointer--;
		} else if (command == T_GT_OR_EQUAL) {
			if (context->c->stackPointer < 2) return -1;
			context->c->stack[context->c->stackPointer - 2].i = context->c->stack[context->c->stackPointer - 2].i >= context->c->stack[context->c->stackPointer - 1].i;
			context->c->stackPointer--;
		} else if (command == T_DOUBLE_EQUALS) {
			if (context->c->stackPointer < 2) return -1;
			context->c->stack[context->c->stackPointer - 2].i = context->c->stack[context->c->stackPointer - 2].i == context->c->stack[context->c->stackPointer - 1].i;
			context->c->stackPointer--;
		} else if (command == T_NOT_EQUALS) {
			if (context->c->stackPointer < 2) return -1;
			context->c->stack[context->c->stackPointer - 2].i = context->c->stack[context->c->stackPointer - 2].i != context->c->stack[context->c->stackPointer - 1].i;
			context->c->stackPointer--;
		} else if (command == T_LOGICAL_NOT) {
			if (context->c->stackPointer < 1) return -1;
			context->c->stack[context->c->stackPointer - 1].i = !context->c->stack[context->c->stackPointer - 1].i;
		} else if (command == T_FLOAT_LESS_THAN) {
			if (context->c->stackPointer < 2) return -1;
			context->c->stack[context->c->stackPointer - 2].i = context->c->stack[context->c->stackPointer - 2].f < context->c->stack[context->c->stackPointer - 1].f;
			context->c->stackPointer--;
		} else if (command == T_FLOAT_GREATER_THAN) {
			if (context->c->stackPointer < 2) return -1;
			context->c->stack[context->c->stackPointer - 2].i = context->c->stack[context->c->stackPointer - 2].f > context->c->stack[context->c->stackPointer - 1].f;
			context->c->stackPointer--;
		} else if (command == T_FLOAT_LT_OR_EQUAL) {
			if (context->c->stackPointer < 2) return -1;
			context->c->stack[context->c->stackPointer - 2].i = context->c->stack[context->c->stackPointer - 2].f <= context->c->stack[context->c->stackPointer - 1].f;
			context->c->stackPointer--;
		} else if (command == T_FLOAT_GT_OR_EQUAL) {
			if (context->c->stackPointer < 2) return -1;
			context->c->stack[context->c->stackPointer - 2].i = context->c->stack[context->c->stackPointer - 2].f >= context->c->stack[context->c->stackPointer - 1].f;
			context->c->stackPointer--;
		} else if (command == T_FLOAT_DOUBLE_EQUALS) {
			if (context->c->stackPointer < 2) return -1;
			context->c->stack[context->c->stackPointer - 2].i = context->c->stack[context->c->stackPointer - 2].f == context->c->stack[context->c->stackPointer - 1].f;
			context->c->stackPointer--;
		} else if (command == T_FLOAT_NOT_EQUALS) {
			if (context->c->stackPointer < 2) return -1;
			context->c->stack[context->c->stackPointer - 2].i = context->c->stack[context->c->stackPointer - 2].f != context->c->stack[context->c->stackPointer - 1].f;
			context->c->stackPointer--;
		} else if (command == T_STR_DOUBLE_EQUALS || command == T_STR_NOT_EQUALS) {
			STACK_READ_STRING(text1, bytes1, 2);
			STACK_READ_STRING(text2, bytes2, 1);
			bool equal = bytes1 == bytes2 && 0 == MemoryCompare(text1, text2, bytes1);
			context->c->stack[context->c->stackPointer - 2].i = command == T_STR_NOT_EQUALS ? !equal : equal;
			context->c->stackIsManaged[context->c->stackPointer - 2] = false;
			context->c->stackPointer--;
		} else if (command == T_OP_LEN) {
			if (context->c->stackPointer < 1) return -1;
			if (!context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;
			uint64_t index = context->c->stack[context->c->stackPointer - 1].i;
			if (context->heapEntriesAllocated <= index) return -1;
			HeapEntry *entry = &context->heap[index];

			if (entry->type == T_LIST) {
				context->c->stack[context->c->stackPointer - 1].i = entry->length;
			} else {
				STACK_READ_STRING(stringText, stringBytes, 1);
				context->c->stack[context->c->stackPointer - 1].i = stringBytes;
			}

			context->c->stackIsManaged[context->c->stackPointer - 1] = false;
		} else if (command == T_INDEX) {
			if (context->c->stackPointer < 2) return -1;
			STACK_READ_STRING(text, bytes, 2);
			if (context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;
			uintptr_t index = context->c->stack[context->c->stackPointer - 1].i;

			if (index >= bytes) {
				PrintError4(context, instructionPointer - 1, "Index %ld out of bounds in string '%.*s' of length %ld.\n", 
						index, bytes, text, bytes);
				return 0;
			}

			char c = text[index];
			index = HeapAllocate(context);
			context->heap[index].type = T_STR;
			context->heap[index].bytes = 1;
			context->heap[index].text = (char *) AllocateResize(NULL, 1); // TODO Handling allocation failure.
			context->heap[index].text[0] = c;
			context->c->stack[context->c->stackPointer - 2].i = index;
			context->c->stackIsManaged[context->c->stackPointer - 2] = true;
			context->c->stackPointer--;
		} else if (command == T_CALL) {
			callCommand:;
			if (context->c->stackPointer < 1) return -1;
			if (!context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;
			Value newBody = context->c->stack[--context->c->stackPointer];

			if (newBody.i == 0) {
				PrintError4(context, instructionPointer - 1, "Function pointer was null.\n");
				return 0;
			}

			bool popResult = false;
			bool assertResult = false;

			while (true) {
				uint64_t index = newBody.i;
				if (context->heapEntriesAllocated <= index) return -1;
				HeapEntry *entry = &context->heap[index];
				newBody.i = entry->lambdaID;

				if (entry->type == T_OP_DISCARD) {
					popResult = true;
				} else if (entry->type == T_OP_ASSERT) {
					assertResult = true;
				} else if (entry->type == T_OP_CURRY) {
					if (context->c->stackPointer == context->c->stackEntriesAllocated) {
						PrintError4(context, instructionPointer - 1, "Stack overflow.\n");
						return 0;
					}

					context->c->stack[context->c->stackPointer] = entry->curryValue;
					context->c->stackIsManaged[context->c->stackPointer] = entry->internalValuesAreManaged;
					context->c->stackPointer++;
				} else if (entry->type == T_FUNCPTR) {
					break;
				} else {
					return -1;
				}
			} 

			if (context->c->backTracePointer == sizeof(context->c->backTrace) / sizeof(context->c->backTrace[0])) {
				PrintError4(context, instructionPointer - 1, "Back trace overflow.\n");
				return 0;
			}
			
			BackTraceItem *link = &context->c->backTrace[context->c->backTracePointer];
			context->c->backTracePointer++;
			link->instructionPointer = instructionPointer;
			link->variableBase = variableBase;
			link->popResult = popResult;
			link->assertResult = assertResult;
			instructionPointer = newBody.i;
			variableBase = context->c->localVariableCount - 1;
		} else if (command == T_IF) {
			if (context->c->stackPointer < 1) return -1;
			Value condition = context->c->stack[--context->c->stackPointer];
			int32_t delta;
			MemoryCopy(&delta, &functionData[instructionPointer], sizeof(delta));
			instructionPointer += condition.i ? (int32_t) sizeof(delta) : delta; 
		} else if (command == T_LOGICAL_OR) {
			if (context->c->stackPointer < 1) return -1;
			Value condition = context->c->stack[context->c->stackPointer - 1];
			int32_t delta;
			MemoryCopy(&delta, &functionData[instructionPointer], sizeof(delta));
			instructionPointer += condition.i ? delta : (int32_t) sizeof(delta); 
			if (!condition.i) context->c->stackPointer--;
		} else if (command == T_LOGICAL_AND) {
			if (context->c->stackPointer < 1) return -1;
			Value condition = context->c->stack[context->c->stackPointer - 1];
			int32_t delta;
			MemoryCopy(&delta, &functionData[instructionPointer], sizeof(delta));
			instructionPointer += condition.i ? (int32_t) sizeof(delta) : delta; 
			if (condition.i) context->c->stackPointer--;
		} else if (command == T_BRANCH) {
			int32_t delta;
			MemoryCopy(&delta, &functionData[instructionPointer], sizeof(delta));
			instructionPointer += delta; 
		} else if (command == T_POP) {
			if (context->c->stackPointer < 1) return -1;
			context->c->stackPointer--;
		} else if (command == T_DUP) {
			if (context->c->stackPointer < 1) return -1;

			if (context->c->stackPointer == context->c->stackEntriesAllocated) {
				PrintError4(context, instructionPointer - 1, "Stack overflow.\n");
				return 0;
			}

			context->c->stack[context->c->stackPointer] = context->c->stack[context->c->stackPointer - 1];
			context->c->stackIsManaged[context->c->stackPointer] = context->c->stackIsManaged[context->c->stackPointer - 1];
			context->c->stackPointer++;
		} else if (command == T_SWAP) {
			if (context->c->stackPointer < 2) return -1;
			Value v1 = context->c->stack[context->c->stackPointer - 1];
			Value v2 = context->c->stack[context->c->stackPointer - 2];
			bool m1 = context->c->stackIsManaged[context->c->stackPointer - 1];
			bool m2 = context->c->stackIsManaged[context->c->stackPointer - 2];
			context->c->stack[context->c->stackPointer - 1] = v2;
			context->c->stack[context->c->stackPointer - 2] = v1;
			context->c->stackIsManaged[context->c->stackPointer - 1] = m2;
			context->c->stackIsManaged[context->c->stackPointer - 2] = m1;
		} else if (command == T_ROT3) {
			if (context->c->stackPointer < 3) return -1;
			Value v1 = context->c->stack[context->c->stackPointer - 1];
			Value v2 = context->c->stack[context->c->stackPointer - 2];
			Value v3 = context->c->stack[context->c->stackPointer - 3];
			bool m1 = context->c->stackIsManaged[context->c->stackPointer - 1];
			bool m2 = context->c->stackIsManaged[context->c->stackPointer - 2];
			bool m3 = context->c->stackIsManaged[context->c->stackPointer - 3];
			context->c->stack[context->c->stackPointer - 1] = v3;
			context->c->stack[context->c->stackPointer - 2] = v1;
			context->c->stack[context->c->stackPointer - 3] = v2;
			context->c->stackIsManaged[context->c->stackPointer - 1] = m3;
			context->c->stackIsManaged[context->c->stackPointer - 2] = m1;
			context->c->stackIsManaged[context->c->stackPointer - 3] = m2;
		} else if (command == T_ASSERT) {
			if (context->c->stackPointer < 1) return -1;
			Value condition = context->c->stack[--context->c->stackPointer];

			if (condition.i == 0) {
				PrintError4(context, instructionPointer - 1, "Assertion failed.\n");
				return 0;
			}
		} else if (command == T_ERR_CAST) {
			if (context->c->stackPointer < 1) return -1;

			// TODO Handle memory allocation failures here.
			uintptr_t index = HeapAllocate(context);
			context->heap[index].type = T_ERR;
			context->heap[index].success = true;
			context->heap[index].internalValuesAreManaged = context->c->stackIsManaged[context->c->stackPointer - 1];;
			context->heap[index].errorValue = context->c->stack[context->c->stackPointer - 1];

			Value v;
			v.i = index;
			context->c->stackIsManaged[context->c->stackPointer - 1] = true;
			context->c->stack[context->c->stackPointer - 1] = v;
		} else if (command == T_ANYTYPE_CAST) {
			if (context->c->stackPointer < 1) return -1;

			// TODO Handle memory allocation failures here.
			uintptr_t index = HeapAllocate(context);
			context->heap[index].type = T_ANYTYPE;
			MemoryCopy(&context->heap[index].anyType, &functionData[instructionPointer], sizeof(context->heap[index].anyType));
			context->heap[index].internalValuesAreManaged = ASTIsManagedType(context->heap[index].anyType);
			context->heap[index].anyValue = context->c->stack[context->c->stackPointer - 1];

			Value v;
			v.i = index;
			context->c->stackIsManaged[context->c->stackPointer - 1] = true;
			context->c->stack[context->c->stackPointer - 1] = v;

			instructionPointer += sizeof(context->heap[index].anyType);
		} else if (command == T_OP_CAST) {
			if (context->c->stackPointer < 1) return -1;
			if (!context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;
			uintptr_t index = context->c->stack[context->c->stackPointer - 1].i;

			if (index == 0) {
				PrintError4(context, instructionPointer - 1, "The object is null.\n");
				return 0;
			}

			if (context->heapEntriesAllocated <= index) return -1;
			HeapEntry *entry = &context->heap[index];
			if (entry->type != T_ANYTYPE) return -1;
			Node *expressionType;
			MemoryCopy(&expressionType, &functionData[instructionPointer], sizeof(expressionType));

			if (!ASTMatching(expressionType, entry->anyType)) {
				PrintError4(context, instructionPointer - 1, "Invalid cast.\n");
				return 0;
			}

			Assert(ASTIsManagedType(expressionType) == entry->internalValuesAreManaged);
			context->c->stackIsManaged[context->c->stackPointer - 1] = entry->internalValuesAreManaged;
			context->c->stack[context->c->stackPointer - 1] = entry->anyValue;
			instructionPointer += sizeof(expressionType);
		} else if (command == T_OP_SUCCESS) {
			if (context->c->stackPointer < 1) return -1;
			if (!context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;
			uintptr_t index = context->c->stack[context->c->stackPointer - 1].i;
			bool success = false;

			if (index) {
				if (context->heapEntriesAllocated <= index) return -1;
				HeapEntry *entry = &context->heap[index];
				if (entry->type != T_ERR) return -1;
				success = entry->success;
			}

			context->c->stack[context->c->stackPointer - 1].i = success;
			context->c->stackIsManaged[context->c->stackPointer - 1] = false;
		} else if (command == T_OP_ASSERT_ERR) {
			if (context->c->stackPointer < 1) return -1;
			if (!context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;
			uintptr_t index = context->c->stack[context->c->stackPointer - 1].i;

			if (index == 0) {
				PrintError4(context, instructionPointer - 1, "Assertion failed. Unknown error.\n");
				return 0;
			} else {
				if (context->heapEntriesAllocated <= index) return -1;
				HeapEntry *entry = &context->heap[index];
				if (entry->type != T_ERR) return -1;

				if (!entry->success) {
					if (!entry->internalValuesAreManaged) return -1;
					size_t textBytes;
					const char *text;
					ScriptHeapEntryToString(context, &context->heap[entry->errorValue.i], &text, &textBytes);
					PrintError4(context, instructionPointer - 1, "Assertion failed.\nThe error code is: '%.*s'.\n", textBytes, text);
					return 0;
				}

				context->c->stack[context->c->stackPointer - 1] = entry->errorValue;
				context->c->stackIsManaged[context->c->stackPointer - 1] = entry->internalValuesAreManaged;
			}
		} else if (command == T_OP_ERROR) {
			if (context->c->stackPointer < 1) return -1;
			if (!context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;
			uintptr_t index = context->c->stack[context->c->stackPointer - 1].i;

			if (index == 0) {
				index = HeapAllocate(context);
				context->heap[index].type = T_STR;
				context->heap[index].text = (char *) AllocateResize(NULL, 7);
				context->heap[index].bytes = 7;
				MemoryCopy(context->heap[index].text, "UNKNOWN", 7);
			} else {
				if (context->heapEntriesAllocated <= index) return -1;
				HeapEntry *entry = &context->heap[index];
				if (entry->type != T_ERR) return -1;
				if (!entry->success && !entry->internalValuesAreManaged) return -1;
				index = entry->success ? 0 : entry->errorValue.i;
			}

			context->c->stack[context->c->stackPointer - 1].i = index;
			context->c->stackIsManaged[context->c->stackPointer - 1] = true;
		} else if (command == T_OP_DEFAULT) {
			if (context->c->stackPointer < 2) return -1;
			if (!context->c->stackIsManaged[context->c->stackPointer - 2]) return -1;
			uintptr_t index = context->c->stack[context->c->stackPointer - 2].i;

			if (index) {
				if (context->heapEntriesAllocated <= index) return -1;
				HeapEntry *entry = &context->heap[index];
				if (entry->type != T_ERR) return -1;
				if (!entry->success && !entry->internalValuesAreManaged) return -1;

				if (entry->success) {
					context->c->stack[context->c->stackPointer - 1] = entry->errorValue;
					context->c->stackIsManaged[context->c->stackPointer - 1] = entry->internalValuesAreManaged;
				}
			}

			context->c->stack[context->c->stackPointer - 2] = context->c->stack[context->c->stackPointer - 1];
			context->c->stackIsManaged[context->c->stackPointer - 2] = context->c->stackIsManaged[context->c->stackPointer - 1];
			context->c->stackPointer--;
		} else if (command == T_OP_INT_TO_FLOAT) {
			if (context->c->stackPointer < 1) return -1;
			if (context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;
			context->c->stack[context->c->stackPointer - 1].f = context->c->stack[context->c->stackPointer - 1].i;
		} else if (command == T_OP_FLOAT_TRUNCATE) {
			if (context->c->stackPointer < 1) return -1;
			if (context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;
			context->c->stack[context->c->stackPointer - 1].i = context->c->stack[context->c->stackPointer - 1].f;
		} else if (command == T_PERSIST) {
			if (!ExternalPersistWrite(context, NULL)) {
				return 0;
			}
		} else if (command == T_NEW) {
			if (context->c->stackPointer == context->c->stackEntriesAllocated) {
				PrintError4(context, instructionPointer - 1, "Stack overflow.\n");
				return 0;
			}

			int16_t fieldCount = functionData[instructionPointer + 0] + (functionData[instructionPointer + 1] << 8); 
			instructionPointer += 2;
			uintptr_t index = HeapAllocate(context);
			context->heap[index].type = fieldCount >= 0 ? T_STRUCT : fieldCount >= -2 ? T_LIST : T_ERR;

			if (fieldCount >= 0) {
				context->heap[index].fields = (Value *) ((uint8_t *) AllocateResize(NULL, fieldCount * (1 + sizeof(Value))) + fieldCount);
				context->heap[index].fieldCount = fieldCount;

				for (intptr_t i = 0; i < fieldCount; i++) {
					context->heap[index].fields[i].i = 0;

					// Default all fields to being unmanaged.
					// The first type they are set this will be updated.
					((uint8_t *) context->heap[index].fields)[-1 - i] = false;
				}
			} else if (fieldCount >= -2) {
				context->heap[index].internalValuesAreManaged = fieldCount == -2;
				context->heap[index].length = context->heap[index].allocated = 0;
				context->heap[index].list = NULL;
			} else {
				context->heap[index].internalValuesAreManaged = true;
				context->heap[index].success = false;

				if (context->c->stackPointer < 1) return -1;
				if (!context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;
				context->heap[index].errorValue = context->c->stack[context->c->stackPointer - 1];
				context->c->stackPointer--;
			}

			Value v;
			v.i = index;
			context->c->stackIsManaged[context->c->stackPointer] = true;
			context->c->stack[context->c->stackPointer++] = v;
		} else if (command == T_OP_RESIZE) {
			if (context->c->stackPointer < 2) return -1;
			if (!context->c->stackIsManaged[context->c->stackPointer - 2]) return -1;

			uint64_t index = context->c->stack[context->c->stackPointer - 2].i;

			if (!index) {
				PrintError4(context, instructionPointer - 1, "The list is null.\n");
				return 0;
			}

			if (context->heapEntriesAllocated <= index) return -1;
			HeapEntry *entry = &context->heap[index];
			if (entry->type != T_LIST) return -1;

			int64_t newLength = context->c->stack[context->c->stackPointer - 1].i;

			if (newLength < 0 || newLength >= 1000000000) {
				PrintError4(context, instructionPointer - 1, "The new length of the list is out of the supported range (0..1000000000).\n");
				return 0;
			}

			uint32_t oldLength = context->heap[index].length;
			context->heap[index].length = newLength;
			context->heap[index].allocated = newLength;

			// TODO Handling out of memory errors.
			context->heap[index].list = (Value *) AllocateResize(context->heap[index].list, newLength * sizeof(Value));

			for (uintptr_t i = oldLength; i < (size_t) newLength; i++) {
				context->heap[index].list[i].i = 0;
			}

			context->c->stackPointer -= 2;
		} else if (command == T_OP_ADD) {
			if (context->c->stackPointer < 2) return -1;
			if (!context->c->stackIsManaged[context->c->stackPointer - 2]) return -1;

			uint64_t index = context->c->stack[context->c->stackPointer - 2].i;

			if (!index) {
				PrintError4(context, instructionPointer - 1, "The list is null.\n");
				return 0;
			}

			if (context->heapEntriesAllocated <= index) return -1;
			HeapEntry *entry = &context->heap[index];
			if (entry->type != T_LIST) return -1;

			int64_t newLength = entry->length + 1;

			if (newLength < 0 || newLength >= 1000000000) {
				PrintError4(context, instructionPointer - 1, "The new length of the list is out of the supported range (0..1000000000).\n");
				return 0;
			}

			uint32_t oldLength = context->heap[index].length;
			entry->length = newLength;

			if (entry->length > entry->allocated) {
				// TODO Handling out of memory errors.
				entry->allocated = entry->allocated ? entry->allocated * 2 : 4;
				entry->list = (Value *) AllocateResize(entry->list, entry->allocated * sizeof(Value));
				Assert(entry->length <= entry->allocated);
			}

			if (entry->internalValuesAreManaged != context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;
			entry->list[oldLength] = context->c->stack[context->c->stackPointer - 1];

			context->c->stackPointer -= 2;
		} else if (command == T_OP_INSERT) {
			if (context->c->stackPointer < 3) return -1;
			if (!context->c->stackIsManaged[context->c->stackPointer - 3]) return -1;

			uint64_t index = context->c->stack[context->c->stackPointer - 3].i;

			if (!index) {
				PrintError4(context, instructionPointer - 1, "The list is null.\n");
				return 0;
			}

			if (context->heapEntriesAllocated <= index) return -1;
			HeapEntry *entry = &context->heap[index];
			if (entry->type != T_LIST) return -1;

			int64_t newLength = entry->length + 1;

			if (newLength < 0 || newLength >= 1000000000) {
				PrintError4(context, instructionPointer - 1, "The new length of the list is out of the supported range (0..1000000000).\n");
				return 0;
			}

			uint32_t oldLength = context->heap[index].length;
			entry->length = newLength;

			if (entry->length > entry->allocated) {
				// TODO Handling out of memory errors.
				entry->allocated = entry->allocated ? entry->allocated * 2 : 4;
				entry->list = (Value *) AllocateResize(entry->list, entry->allocated * sizeof(Value));
				Assert(entry->length <= entry->allocated);
			}

			if (context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;
			int64_t insertIndex = context->c->stack[context->c->stackPointer - 1].i;

			if (insertIndex < 0 || insertIndex > oldLength) {
				PrintError4(context, instructionPointer - 1, "Cannot insert at index %ld. The list has length %ld.\n",
						insertIndex, oldLength);
				return 0;
			}

			for (int64_t i = oldLength - 1; i >= insertIndex; i--) {
				entry->list[i + 1] = entry->list[i];
			}

			if (entry->internalValuesAreManaged != context->c->stackIsManaged[context->c->stackPointer - 2]) return -1;
			entry->list[insertIndex] = context->c->stack[context->c->stackPointer - 2];

			context->c->stackPointer -= 3;
		} else if (command == T_OP_INSERT_MANY) {
			if (context->c->stackPointer < 3) return -1;
			if (!context->c->stackIsManaged[context->c->stackPointer - 3]) return -1;

			uint64_t index = context->c->stack[context->c->stackPointer - 3].i;

			if (!index) {
				PrintError4(context, instructionPointer - 1, "The list is null.\n");
				return 0;
			}

			if (context->heapEntriesAllocated <= index) return -1;
			HeapEntry *entry = &context->heap[index];
			if (entry->type != T_LIST) return -1;

			if (context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;
			int64_t insertCount = context->c->stack[context->c->stackPointer - 1].i;
			int64_t newLength = (int64_t) entry->length + insertCount;

			if (insertCount < 0) {
				PrintError4(context, instructionPointer - 1, "The number of items to insert is negative (%ld).\n", insertCount);
				return 0;
			} else if (newLength < 0 || newLength >= 1000000000) {
				PrintError4(context, instructionPointer - 1, "The new length of the list (%ld + %ld = %ld) "
						"is out of the supported range (0..1000000000).\n", (int64_t) entry->length, insertCount, newLength);
				return 0;
			}

			uint32_t oldLength = context->heap[index].length;
			entry->length = newLength;

			if (entry->length > entry->allocated) {
				// TODO Handling out of memory errors.
				entry->allocated = entry->allocated ? entry->allocated * 2 : 4;
				if (entry->length > entry->allocated) entry->allocated = entry->length + 5;
				entry->list = (Value *) AllocateResize(entry->list, entry->allocated * sizeof(Value));
				Assert(entry->length <= entry->allocated);
			}

			if (context->c->stackIsManaged[context->c->stackPointer - 2]) return -1;
			int64_t insertIndex = context->c->stack[context->c->stackPointer - 2].i;

			if (insertIndex < 0 || insertIndex > oldLength) {
				PrintError4(context, instructionPointer - 1, "Cannot insert at index %ld. The list has length %ld.\n",
						insertIndex, oldLength);
				return 0;
			}

			for (int64_t i = oldLength - 1; i >= insertIndex; i--) {
				entry->list[i + insertCount] = entry->list[i];
			}

			for (uintptr_t i = 0; i < (uintptr_t) insertCount; i++) {
				entry->list[i + insertIndex].i = 0;
			}

			context->c->stackPointer -= 3;
		} else if (command == T_OP_DELETE || command == T_OP_DELETE_MANY) {
			int stackIndexList = command == T_OP_DELETE ? 2 : 3;
			int stackIndexIndex = command == T_OP_DELETE ? 1 : 2;
			if (context->c->stackPointer < (uintptr_t) stackIndexList) return -1;
			if (!context->c->stackIsManaged[context->c->stackPointer - stackIndexList]) return -1;
			uint64_t index = context->c->stack[context->c->stackPointer - stackIndexList].i;

			if (!index) {
				PrintError4(context, instructionPointer - 1, "The list is null.\n");
				return 0;
			}

			if (context->heapEntriesAllocated <= index) return -1;
			HeapEntry *entry = &context->heap[index];
			if (entry->type != T_LIST) return -1;

			if (context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;
			int64_t deleteCount = command == T_OP_DELETE ? 1 : context->c->stack[context->c->stackPointer - 1].i;
			int64_t newLength = (int64_t) entry->length - deleteCount;

			if (deleteCount < 0) {
				PrintError4(context, instructionPointer - 1, "The number of items to delete is negative (%ld).\n", deleteCount);
				return 0;
			} else if (newLength < 0 || newLength >= 1000000000) {
				PrintError4(context, instructionPointer - 1, "The new length of the list (%ld - %ld = %ld) "
						"is out of the supported range (0..1000000000).\n", (int64_t) entry->length, deleteCount, newLength);
				return 0;
			}

			// TODO Maybe shrink the list storage, if it will save a lot of memory.

			if (context->c->stackIsManaged[context->c->stackPointer - stackIndexIndex]) return -1;
			int64_t deleteIndex = context->c->stack[context->c->stackPointer - stackIndexIndex].i;

			if (deleteIndex < 0 || deleteIndex > newLength) {
				PrintError4(context, instructionPointer - 1, "Cannot delete %ld items starting at index %ld. The list has length %ld.\n",
						deleteCount, deleteIndex, (int64_t) entry->length);
				return 0;
			}

			for (int64_t i = deleteIndex; i < newLength; i++) {
				entry->list[i] = entry->list[i + deleteCount];
			}

			entry->length = newLength;
			context->c->stackPointer -= command == T_OP_DELETE ? 2 : 3;
		} else if (command == T_OP_DELETE_ALL) {
			if (context->c->stackPointer < 1) return -1;
			if (!context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;

			uint64_t index = context->c->stack[context->c->stackPointer - 1].i;

			if (!index) {
				PrintError4(context, instructionPointer - 1, "The list is null.\n");
				return 0;
			}

			if (context->heapEntriesAllocated <= index) return -1;
			HeapEntry *entry = &context->heap[index];
			if (entry->type != T_LIST) return -1;

			context->heap[index].length = context->heap[index].allocated = 0;
			context->heap[index].list = (Value *) AllocateResize(context->heap[index].list, 0);
			context->c->stackPointer--;
		} else if (command == T_OP_FIND_AND_DELETE || command == T_OP_FIND
				|| command == T_OP_FIND_AND_DEL_STR || command == T_OP_FIND_STR) {
			if (context->c->stackPointer < 2) return -1;
			if (!context->c->stackIsManaged[context->c->stackPointer - 2]) return -1;

			uint64_t index = context->c->stack[context->c->stackPointer - 2].i;

			if (!index) {
				PrintError4(context, instructionPointer - 1, "The list is null.\n");
				return 0;
			}

			if (context->heapEntriesAllocated <= index) return -1;
			HeapEntry *entry = &context->heap[index];
			if (entry->type != T_LIST) return -1;
			if (entry->internalValuesAreManaged != context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;
			if ((command == T_OP_FIND_STR || command == T_OP_FIND_AND_DEL_STR) && !entry->internalValuesAreManaged) return -1;
			context->c->stack[context->c->stackPointer - 2].i = command == T_OP_FIND || command == T_OP_FIND_STR ? -1 : 0;

			for (uintptr_t i = 0; i < entry->length; i++) {
				if (command == T_OP_FIND_STR || command == T_OP_FIND_AND_DEL_STR) {
					const char *text1, *text2;
					size_t bytes1, bytes2;
					ScriptHeapEntryToString(context, &context->heap[entry->list[i].i], &text1, &bytes1);
					ScriptHeapEntryToString(context, &context->heap[context->c->stack[context->c->stackPointer - 1].i], &text2, &bytes2);
					bool equal = bytes1 == bytes2 && 0 == MemoryCompare(text1, text2, bytes1);
					if (!equal) continue;
				} else {
					bool equal = entry->list[i].i == context->c->stack[context->c->stackPointer - 1].i;
					if (!equal) continue;
				}
					
				if (command == T_OP_FIND || command == T_OP_FIND_STR) {
					context->c->stack[context->c->stackPointer - 2].i = i;
				} else {
					context->c->stack[context->c->stackPointer - 2].i = 1;
					entry->length--;

					for (uintptr_t j = i; j < entry->length; j++) {
						entry->list[j] = entry->list[j + 1];
					}
				}

				break;
			}

			context->c->stackIsManaged[context->c->stackPointer - 2] = false;
			context->c->stackPointer--;
		} else if (command == T_OP_DISCARD || command == T_OP_ASSERT) {
			if (context->c->stackPointer < 1) return -1;
			if (!context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;
			int64_t id = context->c->stack[context->c->stackPointer - 1].i;
			uintptr_t index = HeapAllocate(context);
			context->heap[index].type = command;
			context->heap[index].lambdaID = id;
			context->c->stackIsManaged[context->c->stackPointer - 1] = true;
			context->c->stack[context->c->stackPointer - 1].i = index;
		} else if (command == T_OP_CURRY) {
			if (context->c->stackPointer < 2) return -1;
			if (!context->c->stackIsManaged[context->c->stackPointer - 2]) return -1;
			bool valueIsManaged = context->c->stackIsManaged[context->c->stackPointer - 1];
			Value value = context->c->stack[context->c->stackPointer - 1];
			int64_t id = context->c->stack[context->c->stackPointer - 2].i;
			uintptr_t index = HeapAllocate(context);
			context->heap[index].type = command;
			context->heap[index].lambdaID = id;
			context->heap[index].curryValue = value;
			context->heap[index].internalValuesAreManaged = valueIsManaged;
			context->c->stackIsManaged[context->c->stackPointer - 2] = true;
			context->c->stack[context->c->stackPointer - 2].i = index;
			context->c->stackPointer--;
		} else if (command == T_OP_ASYNC) {
			if (context->c->stackPointer < 1) return -1;
			if (!context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;
			CoroutineState *c = (CoroutineState *) AllocateResize(NULL, sizeof(CoroutineState)); // TODO Handle allocation failure.
			CoroutineState empty = { 0 };
			*c = empty;
			c->id = ++context->lastCoroutineID;
			c->startedByAsync = true;
			c->stackEntriesAllocated = sizeof(context->c->stack) / sizeof(context->c->stack[0]);
			c->stackPointer = 2;
			c->stack[0].i = -1; // Indicates to T_AWAIT to remove the coroutine.
			c->stackIsManaged[0] = false;
			c->stack[1] = context->c->stack[context->c->stackPointer - 1];
			c->stackIsManaged[1] = true;
			c->nextCoroutine = context->allCoroutines;
			if (c->nextCoroutine) c->nextCoroutine->previousCoroutineLink = &c->nextCoroutine;
			c->previousCoroutineLink = &context->allCoroutines;
			context->allCoroutines = c;
			c->nextUnblockedCoroutine = context->unblockedCoroutines;
			if (c->nextUnblockedCoroutine) c->nextUnblockedCoroutine->previousUnblockedCoroutineLink = &c->nextUnblockedCoroutine;
			c->previousUnblockedCoroutineLink = &context->unblockedCoroutines;
			context->unblockedCoroutines = c;
			context->c->stackIsManaged[context->c->stackPointer - 1] = false;
			context->c->stack[context->c->stackPointer - 1].i = c->id;
		} else if (command == T_AWAIT) {
			awaitCommand:;
			if (context->c->stackPointer < 1 && !context->c->externalCoroutine) return -1;

			// PrintDebug("== AWAIT from %ld\n", context->c->id);
			Assert(!context->c->nextUnblockedCoroutine && !context->c->previousUnblockedCoroutineLink);
			bool unblockImmediately = false;

			if (context->c->externalCoroutine) {
				// PrintDebug("== external coroutine\n");
				context->c->unblockedBy = -1;
				context->c->awaiting = true;
				context->c->instructionPointer = instructionPointer;
				context->c->variableBase = variableBase;
				context->c->waitingOnCount = 0;
			} else if (context->c->stack[context->c->stackPointer - 1].i == -1) {
				if (context->c->stackPointer != 1) return -1;
				// The coroutine has finished. Remove it from the list of all coroutines.
				*context->c->previousCoroutineLink = context->c->nextCoroutine;
				if (context->c->nextCoroutine) context->c->nextCoroutine->previousCoroutineLink = context->c->previousCoroutineLink;

				// PrintDebug("== finished\n");

				for (uintptr_t i = 0; i < context->c->waiterCount; i++) {
					CoroutineState *c = context->c->waiters[i];
					if (!c) continue;
					Assert(!c->nextUnblockedCoroutine && !c->previousUnblockedCoroutineLink);
					c->unblockedBy = context->c->id;
					c->nextUnblockedCoroutine = context->unblockedCoroutines;
					if (c->nextUnblockedCoroutine) c->nextUnblockedCoroutine->previousUnblockedCoroutineLink = &c->nextUnblockedCoroutine;
					c->previousUnblockedCoroutineLink = &context->unblockedCoroutines;
					context->unblockedCoroutines = c;

					for (uintptr_t j = 0; j < c->waitingOnCount; j++) {
						Assert(*(c->waitingOn[j]) == c);
						*(c->waitingOn[j]) = NULL;
					}

					c->waitingOnCount = 0;
					// PrintDebug("== unblocked %ld\n", c->id);
				}

				ScriptFreeCoroutine(context->c);
			} else {
				if (!context->c->stackIsManaged[context->c->stackPointer - 1]) return -1;
				// The coroutine is waiting.
				context->c->unblockedBy = -1;
				context->c->awaiting = true;
				context->c->instructionPointer = instructionPointer;
				context->c->variableBase = variableBase;
				uint64_t index = context->c->stack[context->c->stackPointer - 1].i;
				if (context->heapEntriesAllocated <= index) return -1;
				HeapEntry *entry = &context->heap[index];
				if (entry->internalValuesAreManaged || entry->type != T_LIST) return -1;
				
				context->c->waitingOn = (CoroutineState ***) AllocateResize(context->c->waitingOn, sizeof(CoroutineState **) * entry->length);
				Assert(context->c->waitingOnCount == 0);
				CoroutineState *c = context->allCoroutines;

				while (c) {
					for (uintptr_t i = 0; i < entry->length; i++) {
						if (c->id == (uint64_t) entry->list[i].i) {
							if (c->waiterCount == c->waitersAllocated) {
								c->waitersAllocated = c->waitersAllocated ? c->waitersAllocated * 2 : 4;
								c->waiters = (CoroutineState **) AllocateResize(c->waiters, sizeof(CoroutineState *) * c->waitersAllocated);
							}

							c->waiters[c->waiterCount] = context->c;
							context->c->waitingOn[context->c->waitingOnCount++] = &c->waiters[c->waiterCount];
							c->waiterCount++;
						}
					}

					c = c->nextCoroutine;
				}

				// PrintDebug("== waiting on %d...\n", context->c->waitingOnCount);

				if (!context->c->waitingOnCount) {
					// PrintDebug("== immediately unblocking\n");
					context->c->unblockedBy = entry->length ? entry->list[0].i : -1;
					unblockImmediately = true;
				}
			}

			CoroutineState *next = unblockImmediately ? context->c : context->unblockedCoroutines;

			if (!next) {
				if (context->externalCoroutineCount) {
					// PrintDebug("== wait for an external coroutine\n");
					next = ExternalCoroutineWaitAny(context);
					Assert(next->externalCoroutine);
					unblockImmediately = true;
				} else {
					// TODO Earlier deadlock detection.
					PrintError4(context, instructionPointer - 1, "No tasks can run if this task (ID %ld) starts waiting.\n", context->c->id);
					PrintDebug("All tasks:\n");
					CoroutineState *c = context->allCoroutines;

					while (c) {
						PrintDebug("\t%ld blocks ", c->id);
						bool first = true;

						for (uintptr_t i = 0; i < c->waiterCount; i++) {
							if (!c->waiters[i]) continue;
							PrintDebug("%s%ld", first ? "" : ", ", c->waiters[i]->id);
							first = false;
						}

						PrintDebug("\n");
						PrintBackTrace(context, c->instructionPointer - 1, c, "\t");
						c = c->nextCoroutine;
					}

					return 0;
				}
			}

			if (!unblockImmediately) {
				Assert(next->previousUnblockedCoroutineLink);
				*next->previousUnblockedCoroutineLink = next->nextUnblockedCoroutine;
				if (next->nextUnblockedCoroutine) next->nextUnblockedCoroutine->previousUnblockedCoroutineLink = next->previousUnblockedCoroutineLink;
			}

			next->nextUnblockedCoroutine = NULL;
			next->previousUnblockedCoroutineLink = NULL;
			context->c = next;
			// PrintDebug("== switch to %ld\n", next->id);

			if (context->c->awaiting) {
				if (!context->c->externalCoroutine) {
					context->c->stackIsManaged[context->c->stackPointer - 1] = false;
					context->c->stack[context->c->stackPointer - 1].i = context->c->unblockedBy;
				}

				instructionPointer = context->c->instructionPointer;
				variableBase = context->c->variableBase;
				// PrintDebug("== unblocked by %ld\n", context->c->unblockedBy);
			} else {
				// PrintDebug("== just started\n");
				instructionPointer = 1; // There is a T_AWAIT command at address 1.
				goto callCommand;
			}
		} else if (command == T_REPL_RESULT) {
			if (context->c->stackPointer < 1) return -1;
			ExternalPassREPLResult(context, context->c->stack[--context->c->stackPointer]);
		} else if (command == T_END_FUNCTION || command == T_EXTCALL || command == T_LIBCALL) {
			if (command == T_EXTCALL) {
				uint16_t index = functionData[instructionPointer + 0] + (functionData[instructionPointer + 1] << 8); 
				instructionPointer += 2;

				if (index < sizeof(externalFunctions) / sizeof(externalFunctions[0])) {
					Value returnValue;
					int result = externalFunctions[index].callback(context, &returnValue);
					if (result <= 0) return result;

					if (result == EXTCALL_START_COROUTINE) {
						context->externalCoroutineCount++;
						context->c->externalCoroutine = true;
						instructionPointer -= 3;
						// PrintDebug("start external coroutine %ld\n", context->c->id);
						goto awaitCommand;
					} else if (context->c->externalCoroutine) {
						context->externalCoroutineCount--;
						context->c->externalCoroutine = false;
						// PrintDebug("end external coroutine %ld\n", context->c->id);
					}

					bool isErr = result == EXTCALL_RETURN_ERR_ERROR 
						|| result == EXTCALL_RETURN_ERR_MANAGED 
						|| result == EXTCALL_RETURN_ERR_UNMANAGED;

					if (result == EXTCALL_RETURN_UNMANAGED || result == EXTCALL_RETURN_MANAGED || isErr) {
						if (context->c->stackPointer == context->c->stackEntriesAllocated) {
							PrintDebug("Evaluation stack overflow.\n");
							return -1;
						}

						if (isErr) {
							if (result != EXTCALL_RETURN_ERR_ERROR || returnValue.i) {
								// Temporarily put the return value on the stack in case garbage collection occurs 
								// in the following HeapAllocate (i.e. before the return value has been wrapped).
								context->c->stackIsManaged[context->c->stackPointer] = result != EXTCALL_RETURN_ERR_UNMANAGED;
								context->c->stack[context->c->stackPointer++] = returnValue;
								
								// TODO Handle memory allocation failures here.
								uintptr_t index = HeapAllocate(context);
								context->heap[index].type = T_ERR;
								context->heap[index].success = result != EXTCALL_RETURN_ERR_ERROR;
								context->heap[index].internalValuesAreManaged = result != EXTCALL_RETURN_ERR_UNMANAGED;
								context->heap[index].errorValue = returnValue;
								returnValue.i = index;

								context->c->stackPointer--;
							} else {
								// Unknown error.
								returnValue.i = 0;
							}

							result = EXTCALL_RETURN_MANAGED;
						}

						context->c->stackIsManaged[context->c->stackPointer] = result == EXTCALL_RETURN_MANAGED;
						context->c->stack[context->c->stackPointer++] = returnValue;
					}
				} else {
					return -1;
				}
			} else if (command == T_LIBCALL) {
				context->c->parameterCount = 0;
				context->c->returnValueType = EXTCALL_NO_RETURN;

				void *address;
				MemoryCopy(&address, &functionData[instructionPointer], sizeof(address));
				instructionPointer += sizeof(address);

				if (!((bool (*)(void *)) address)(context)) {
					return 0;
				}

				context->c->stackPointer -= context->c->parameterCount;

				if (context->c->returnValueType != EXTCALL_NO_RETURN) {
					if (context->c->stackPointer == context->c->stackEntriesAllocated) {
						PrintDebug("Evaluation stack overflow.\n");
						return -1;
					}

					context->c->stackIsManaged[context->c->stackPointer] = context->c->returnValueType == EXTCALL_RETURN_MANAGED;
					context->c->stack[context->c->stackPointer++] = context->c->returnValue;
				}
			}

			context->c->localVariableCount = variableBase + 1;

			if (context->c->backTracePointer) {
				BackTraceItem *item = &context->c->backTrace[context->c->backTracePointer - 1];

				if (command == T_EXTCALL || command == T_LIBCALL) {
					context->c->backTracePointer--;
					instructionPointer = item->instructionPointer;
					variableBase = item->variableBase;
				}

				if (item->popResult) {
					if (context->c->stackPointer < 1) return -1;
					context->c->stackPointer--;
				} else if (item->assertResult) {
					if (context->c->stackPointer < 1) return -1;
					Value condition = context->c->stack[--context->c->stackPointer];

					if (condition.i == 0) {
						PrintError4(context, instructionPointer - 1, "Return value was false on an asserting function pointer.\n");
						return 0;
					}
				}

				if (command != T_EXTCALL && command != T_LIBCALL) {
					context->c->backTracePointer--;
					instructionPointer = item->instructionPointer;
					variableBase = item->variableBase;
				}
			} else {
				break;
			}
		} else if (command == T_END_CALLBACK) {
			break;
		} else {
			PrintDebug("Unknown command %d.\n", command);
			return -1;
		}
	}

	if (context->allCoroutines->nextCoroutine || context->allCoroutines->startedByAsync) {
		PrintError3("Script ended with unfinished tasks.\n");
		return false;
	}

	return true;
}

bool ScriptParseOptions(ExecutionContext *context) {
	for (uintptr_t i = 0; i < optionCount; i++) {
		uintptr_t equalsPosition = 0;
		uintptr_t optionLength = 0;

		for (uintptr_t j = 0; options[i][j]; j++) {
			if (options[i][j] == '=') {
				equalsPosition = j;
				break;
			}
		}

		for (uintptr_t j = 0; options[i][j]; j++) {
			optionLength++;
		}

		if (!equalsPosition) {
			PrintError3("Invalid script option passed on command line '%s'.\n", options[i]);
			return false;
		}

		uintptr_t index = 0;
		Node *node = NULL;

		for (uintptr_t j = 0; j < context->rootNode->scope->entryCount; j++) {
			if (context->rootNode->scope->entries[j]->token.textBytes == equalsPosition
					&& 0 == MemoryCompare(context->rootNode->scope->entries[j]->token.text, options[i], equalsPosition)
					&& context->rootNode->scope->entries[j]->type == T_DECLARE) {
				node = context->rootNode->scope->entries[j];
				break;
			}

			if (ScopeIsVariableType(context->rootNode->scope->entries[j])) {
				index++;
			}
		}

		if (!node) {
			continue;
		}

		index += context->functionData->globalVariableOffset;

		if (node->expressionType->type == T_STR) {
			uintptr_t heapIndex = HeapAllocate(context);
			context->heap[heapIndex].type = T_STR;
			context->heap[heapIndex].bytes = optionLength - equalsPosition - 1;
			context->heap[heapIndex].text = (char *) AllocateResize(NULL, context->heap[heapIndex].bytes);
			context->globalVariables[index].i = heapIndex;
			MemoryCopy(context->heap[heapIndex].text, options[i] + equalsPosition + 1, context->heap[heapIndex].bytes);
		} else if (node->expressionType->type == T_INT) {
			// TODO Overflow checking.

			Value v;
			v.i = 0;

			for (uintptr_t j = 0; options[i][j + equalsPosition + 1]; j++) {
				char c = options[i][j + equalsPosition + 1];

				if (c >= '0' && c <= '9') {
					v.i *= 10;
					v.i += c - '0';
				} else {
					PrintError3("Option '%s' should be an integer.\n", options[i]);
					return false;
				}
			}

			context->globalVariables[index] = v;
		} else if (node->expressionType->type == T_BOOL) {
			char c = options[i][equalsPosition + 1];
			bool truthy = c == 't' || c == 'y' || c == '1';
			bool falsey = c == 'f' || c == 'n' || c == '0';

			if (!truthy && !falsey) {
				PrintError3("#option variable '%.*s' should be a boolean value 'true' or 'false'.\n", node->token.textBytes, node->token.text);
				return false;
			}

			context->globalVariables[index].i = truthy ? 1 : 0;
		} else {
			PrintError3("#option variable '%.*s' is not of string, boolean or integer type.\n", node->token.textBytes, node->token.text);
			return false;
		}

		if (optionsMatched[i]) {
			PrintError3("Script option passed on command line '%s' matches multiple #option variables in different modules.\n", options[i]);
			return false;
		}

		optionsMatched[i] = true;
	}

	return true;
}

bool ScriptParameterCString(void *engine, char **output) {
	ExecutionContext *context = (ExecutionContext *) engine;
	context->c->parameterCount++;
	if (context->c->stackPointer < context->c->parameterCount) return false;
	if (!context->c->stackIsManaged[context->c->stackPointer - context->c->parameterCount]) return false;
	uint64_t index = context->c->stack[context->c->stackPointer - context->c->parameterCount].i;
	if (context->heapEntriesAllocated <= index) return false;
	const char *text;
	size_t bytes;
	HeapEntry *entry = &context->heap[index];
	ScriptHeapEntryToString(context, entry, &text, &bytes);
	*output = (char *) AllocateResize(NULL, bytes + 1);
	MemoryCopy(*output, text, bytes);
	(*output)[bytes] = 0;
	return true;
}

void ScriptHeapRefClose(void *engine, intptr_t index) {
	ExecutionContext *context = (ExecutionContext *) engine;
	Assert(index >= 0 || index < (intptr_t) context->heapEntriesAllocated);
	Assert(context->heap[index].externalReferenceCount);
	context->heap[index].externalReferenceCount--;
}

bool ScriptParameterHeapRef(void *engine, intptr_t *output) {
	ExecutionContext *context = (ExecutionContext *) engine;
	context->c->parameterCount++;
	if (context->c->stackPointer < context->c->parameterCount) return false;
	intptr_t index = context->c->stack[context->c->stackPointer - context->c->parameterCount].i;
	if (index < 0 || index >= (intptr_t) context->heapEntriesAllocated) return false;
	if (context->heap[index].externalReferenceCount == 0xFFFFFFFF) return false;
	context->heap[index].externalReferenceCount++;
	*output = index;
	return true;
}

bool ScriptParameterInt64(void *engine, int64_t *output) {
	ExecutionContext *context = (ExecutionContext *) engine;
	context->c->parameterCount++;
	if (context->c->stackPointer < context->c->parameterCount) return false;
	*output = context->c->stack[context->c->stackPointer - context->c->parameterCount].i;
	return true;
}

bool ScriptParameterDouble(void *engine, double *output) {
	ExecutionContext *context = (ExecutionContext *) engine;
	context->c->parameterCount++;
	if (context->c->stackPointer < context->c->parameterCount) return false;
	*output = context->c->stack[context->c->stackPointer - context->c->parameterCount].f;
	return true;
}

bool ScriptParameterBool  (void *engine,     bool *output) { int64_t i; if (!ScriptParameterInt64(engine, &i)) return false; *output = i; return true; }
bool ScriptParameterUint32(void *engine, uint32_t *output) { int64_t i; if (!ScriptParameterInt64(engine, &i)) return false; *output = i; return true; }
bool ScriptParameterUint64(void *engine, uint64_t *output) { int64_t i; if (!ScriptParameterInt64(engine, &i)) return false; *output = i; return true; }
bool ScriptParameterInt32 (void *engine,  int32_t *output) { int64_t i; if (!ScriptParameterInt64(engine, &i)) return false; *output = i; return true; }

void ScriptReturnInt(void *engine, int64_t input) {
	ExecutionContext *context = (ExecutionContext *) engine;
	Assert(context->c->returnValueType == EXTCALL_NO_RETURN);
	context->c->returnValueType = EXTCALL_RETURN_UNMANAGED;
	context->c->returnValue.i = input;
}

bool ScriptParameterPointer(void *engine, void **output) {
	int64_t i; 
	if (!ScriptParameterInt64(engine, &i)) return false;
	*output = (void *) (intptr_t) i; 
	return true;
}

void ScriptReturnPointer(void *engine, void *input) {
	ScriptReturnInt(engine, (int64_t) (intptr_t) input);
}

bool ScriptRunCallback(void *engine, intptr_t functionPointer, int64_t *parameters, bool *managedParameters, size_t parameterCount) {
	// TODO Do this in a separate coroutine?

	ExecutionContext *context = (ExecutionContext *) engine;

	for (intptr_t i = parameterCount - 1; i >= -1; i--) {
		if (context->c->stackPointer == context->c->stackEntriesAllocated) {
			PrintError4(context, 0, "Stack overflow.\n");
			return false;
		}

		if (i == -1) {
			context->c->stack[context->c->stackPointer].i = functionPointer;
			context->c->stackIsManaged[context->c->stackPointer] = true;
		} else {
			context->c->stack[context->c->stackPointer].i = parameters[i];
			context->c->stackIsManaged[context->c->stackPointer] = managedParameters[i];
		}

		context->c->stackPointer++;
	}

	int result = ScriptExecuteFunction(2, context);
	if (result == -1) PrintError3("The script was malformed.\n");
	return result > 0;
}

bool ScriptLoad(Tokenizer tokenizer, ExecutionContext *context, ImportData *importData, bool replMode) {
	Node *previousRootNode = context->rootNode;
	ImportData *previousImportData = context->functionData->importData;

	context->rootNode = replMode ? ParseRootREPL(&tokenizer) : ParseRoot(&tokenizer); 
	context->functionData->importData = tokenizer.module;

	uint8_t b = 0; // Make sure no function can start at 0.
	FunctionBuilderAppend(context->functionData, &b, sizeof(b));
	b = T_AWAIT; // Put a T_AWAIT command at address 1.
	FunctionBuilderAppend(context->functionData, &b, sizeof(b));
	b = T_CALL; // Put a T_CALL command at address 2 (for ScriptRunCallback).
	FunctionBuilderAppend(context->functionData, &b, sizeof(b));
	b = T_END_CALLBACK; // Put a T_END_CALLBACK command at address 3 (for ScriptRunCallback).
	FunctionBuilderAppend(context->functionData, &b, sizeof(b));

	bool success = context->rootNode 
		&& ASTSetScopes(&tokenizer, context, context->rootNode, NULL)
		&& ASTLookupTypeIdentifiers(&tokenizer, context->rootNode)
		&& ASTSetTypes(&tokenizer, context->rootNode)
		&& ASTCheckForReturnStatements(&tokenizer, context->rootNode)
		&& ASTGenerate(&tokenizer, context->rootNode, context)
		&& ScriptParseOptions(context);

	importData->globalVariableOffset = context->functionData->globalVariableOffset;
	importData->rootNode = context->rootNode;
	*importedModulesLink = importData;
	importedModulesLink = &importData->nextImport;

	context->rootNode = previousRootNode;
	context->functionData->importData = previousImportData;

	return success;
}

int ScriptExecute(ExecutionContext *context, ImportData *mainModule) {
	bool optionMatchingError = false;

	for (uintptr_t i = 0; i < optionCount; i++) {
		if (!optionsMatched[i]) {
			PrintError3("Script option passed on command line '%s' does not match any #option variable.\n", options[i]);
			optionMatchingError = true;
		}
	}

	if (optionMatchingError) {
		return 1;
	}

	Node n;
	n.token.textBytes = startFunctionBytes;
	n.token.text = startFunction;
	intptr_t startIndex = ScopeLookupIndex(&n, mainModule->rootNode->scope, true, false);

	if (startIndex == -1) {
		PrintError3("The script does not have a start function '%.*s'.\n", startFunctionBytes, startFunction);
		return 1;
	}

	Node mainFunctionArguments = { 0 };
	mainFunctionArguments.type = T_ARGUMENTS;
	Node mainFunctionReturn = { 0 };
	mainFunctionReturn.type = T_VOID;
	Node mainFunctionType = { 0 };
	mainFunctionType.type = T_FUNCPTR;
	mainFunctionType.firstChild = &mainFunctionArguments;
	mainFunctionArguments.sibling = &mainFunctionReturn;

	if (!ASTMatching(&mainFunctionType, mainModule->rootNode->scope->entries[ScopeLookupIndex(&n, mainModule->rootNode->scope, false, true)]->expressionType)) {
		PrintError3("The start function '%.*s' should take no arguments and return 'void'.\n", startFunctionBytes, startFunction);
		return 1;
	}

	ImportData *module = importedModules;

	while (module) {
		Node n;
		n.token.textBytes = 10;
		n.token.text = "Initialise";
		intptr_t index = ScopeLookupIndex(&n, module->rootNode->scope, true, false);

		if (index != -1) {
			if (!ASTMatching(&mainFunctionType, module->rootNode->scope->entries[ScopeLookupIndex(&n, module->rootNode->scope, false, true)]->expressionType)) {
				PrintError3("The 'Initialise' function in the module '%s' should take no arguments and return 'void'.\n", module->path);
				return 1;
			}

			int result = ScriptExecuteFunction(context->heap[context->globalVariables[index + module->globalVariableOffset].i].lambdaID, context);

			if (result == 0) {
				// A runtime error occurred.
				return 1;
			} else if (result == -1 || context->c->stackPointer != 0) {
				PrintError3("The script was malformed.\n");
				return 1;
			}
		}

		module = module->nextImport;
	}

	int result = ScriptExecuteFunction(context->heap[context->globalVariables[context->functionData->globalVariableOffset + startIndex].i].lambdaID, context);

	if (result == 0) {
		// A runtime error occurred.
		return 1;
	} else if (result == -1 || context->c->stackPointer != 0) {
		PrintError3("The script was malformed.\n");
		return 1;
	}

	return 0;
}

void ScriptFreeCoroutine(CoroutineState *c) {
	AllocateResize(c->waiters, 0);
	AllocateResize(c->waitingOn, 0);
	AllocateResize(c->localVariables, 0);
	AllocateResize(c->localVariableIsManaged, 0);
	AllocateResize(c, 0);
}

void ScriptFree(ExecutionContext *context) {
	ImportData *module = importedModules;

	while (module) {
		if (module->pathBytes != 15 || 0 != MemoryCompare(module->path, "__base_module__", module->pathBytes)) {
			AllocateResize(module->fileData, 0);
		}

		ASTFreeScopes(module->rootNode);
		module = module->nextImport;
	}

	for (uintptr_t i = 1; i < context->heapEntriesAllocated; i++) {
		if (context->heap[i].type != T_ERROR) {
			HeapFreeEntry(context, i);
		}
	}

	CoroutineState *coroutine = context->allCoroutines;

	while (coroutine) {
		CoroutineState *next = coroutine->nextCoroutine;
		ScriptFreeCoroutine(coroutine);
		coroutine = next;
	}

	AllocateResize(context->heap, 0);
	AllocateResize(context->globalVariables, 0);
	AllocateResize(context->globalVariableIsManaged, 0);
	AllocateResize(context->functionData->lineNumbers, 0);
	AllocateResize(context->functionData->data, 0);
	AllocateResize(context->scriptPersistFile, 0);
}

// --------------------------------- Helpers.

void LineNumberLookup(ExecutionContext *context, uint32_t instructionPointer, LineNumber *output) {
	for (uintptr_t i = 0; i < context->functionData->lineNumberCount; i++) {
		if (context->functionData->lineNumbers[i].instructionPointer == instructionPointer) {
			*output = context->functionData->lineNumbers[i];
			return;
		}
	}
}

void PrintBackTrace(ExecutionContext *context, uint32_t instructionPointer, CoroutineState *c, const char *prefix) {
	LineNumber lineNumber = { 0 };
	LineNumberLookup(context, instructionPointer, &lineNumber);

	if (lineNumber.importData) {
		PrintDebug("%s\t%s:%d %s %.*s\n", prefix, lineNumber.importData->path, lineNumber.lineNumber, lineNumber.function ? "in" : "",
				lineNumber.function ? (int) lineNumber.function->textBytes : 0, lineNumber.function ? lineNumber.function->text : "");
	}

	uintptr_t btp = c->backTracePointer;
	uintptr_t minimum = c->startedByAsync ? 1 : 0;

	while (btp > minimum) {
		BackTraceItem *link = &c->backTrace[--btp];
		LineNumberLookup(context, link->instructionPointer - 1, &lineNumber);
		PrintDebug("%s\t%s:%d %s %.*s\n", prefix, lineNumber.importData->path ? lineNumber.importData->path : "??", 
				lineNumber.lineNumber, lineNumber.function ? "in" : "",
				lineNumber.function ? (int) lineNumber.function->textBytes : 0, lineNumber.function ? lineNumber.function->text : "");
	}
}

void PrintLine(ImportData *importData, uintptr_t line) {
	if (!importData) {
		return;
	}
	
	uintptr_t position = 0;

	for (uintptr_t i = 1; i < line; i++) {
		while (position < importData->fileDataBytes) {
			if (((char *) importData->fileData)[position] == '\n') {
				position++;
				break;
			}

			position++;
		}
	}

	uintptr_t length = 0;

	for (uintptr_t i = position; i <= importData->fileDataBytes; i++) {
		if (i == importData->fileDataBytes || ((char *) importData->fileData)[i] == '\n') {
			length = i - position;
			break;
		}
	}

	PrintDebug(">> %.*s\n", (int) length, &((char *) importData->fileData)[position]);
}

int ScriptExecuteFromFile(char *scriptPath, size_t scriptPathBytes, char *fileData, size_t fileDataBytes, bool replMode) {
	Tokenizer tokenizer = { 0 };
	ImportData importData = { 0 };
	importData.path = scriptPath;
	importData.pathBytes = scriptPathBytes;
	importData.fileData = fileData;
	importData.fileDataBytes = fileDataBytes;
	tokenizer.module = &importData;
	tokenizer.line = 1;
	tokenizer.input = fileData;
	tokenizer.inputBytes = fileDataBytes;

	FunctionBuilder builder = { 0 };
	ExecutionContext context = { 0 };
	context.functionData = &builder;
	context.mainModule = &importData;

	context.heapEntriesAllocated = 2;
	context.heap = (HeapEntry *) AllocateResize(NULL, sizeof(HeapEntry) * context.heapEntriesAllocated);
	context.heap[0].type = T_EOF;
	context.heap[1].type = T_ERROR;
	context.heap[1].nextUnusedEntry = 0;
	context.heapFirstUnusedEntry = 1;
	context.c = (CoroutineState *) AllocateResize(0, sizeof(CoroutineState));
	CoroutineState empty = { 0 };
	*context.c = empty;
	context.c->stackEntriesAllocated = sizeof(context.c->stack) / sizeof(context.c->stack[0]);
	context.c->previousCoroutineLink = &context.allCoroutines;
	context.allCoroutines = context.c;

	int result = ScriptLoad(tokenizer, &context, &importData, replMode) ? ScriptExecute(&context, &importData) : 1;
	ScriptFree(&context);

	importedModules = NULL;
	importedModulesLink = &importedModules;

	return result;
}

int ExternalStringSlice(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	if (context->c->stackPointer < 3) return -1;
	STACK_POP_STRING(string, bytes);
	uint64_t start = context->c->stack[--context->c->stackPointer].i;
	if (context->c->stackIsManaged[context->c->stackPointer]) return -1;
	uint64_t end = context->c->stack[--context->c->stackPointer].i;
	if (context->c->stackIsManaged[context->c->stackPointer]) return -1;

	if (start > bytes || end > bytes || end < start) {
		PrintError4(context, 0, "The slice range (%ld..%ld) is invalid for the string of length %ld.\n",
				start, end, bytes);
		return 0;
	}

	RETURN_STRING_COPY(string + start, end - start);
	return EXTCALL_RETURN_MANAGED;
}

int ExternalCharacterToByte(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	STACK_POP_STRING(entryText, entryBytes);
	returnValue->i = entryBytes ? entryText[0] : -1;
	return EXTCALL_RETURN_UNMANAGED;
}

// --------------------------------- Platform layer.

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define getcwd _getcwd
#define popen _popen
#define pclose _pclose
#define setenv(x, y, z) !SetEnvironmentVariable(x, y)
#else
#include <dlfcn.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <pthread.h>
#include <semaphore.h>
#endif
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void **fixedAllocationBlocks;
uint8_t *fixedAllocationCurrentBlock;
uintptr_t fixedAllocationCurrentPosition;
size_t fixedAllocationCurrentSize;

sem_t externalCoroutineSemaphore;
pthread_mutex_t externalCoroutineMutex;
CoroutineState *externalCoroutineUnblockedList;

bool systemShellLoggingEnabled = true;
bool coloredOutput;

char *scriptSourceDirectory;

DIR *directoryIterator;

char *StringZeroTerminate(const char *text, size_t bytes) {
	char *buffer = malloc(bytes + 1);
	if (!buffer) return NULL;
	memcpy(buffer, text, bytes);
	buffer[bytes] = 0;
	return buffer;
}

#define RETURN_ERROR(error) do { MakeError(context, returnValue, error); return EXTCALL_RETURN_ERR_ERROR; } while (0)

void MakeError(ExecutionContext *context, Value *returnValue, int error) {
	const char *text = NULL;

	if (error == EAGAIN || error == EBUSY) text = "OPERATION_BLOCKED";
	if (error == ENOTEMPTY) text = "DIRECTORY_NOT_EMPTY";
	if (error == EFBIG) text = "FILE_TOO_LARGE";
	if (error == ENOSPC || error == EDQUOT) text = "DRIVE_FULL";
	if (error == EROFS) text = "FILE_ON_READ_ONLY_VOLUME";
	if (error == ENOTDIR || error == EISDIR) text = "INCORRECT_NODE_TYPE";
	if (error == ENOENT) text = "FILE_DOES_NOT_EXIST";
	if (error == EEXIST) text = "ALREADY_EXISTS";
	if (error == ENOMEM) text = "INSUFFICIENT_RESOURCES";
	if (error == EPERM || error == EACCES) text = "PERMISSION_NOT_GRANTED";
	if (error == EXDEV) text = "VOLUME_MISMATCH";
	if (error == EIO) text = "HARDWARE_FAILURE";

	if (text) {
		RETURN_STRING_COPY(text, strlen(text));
	} else {
		returnValue->i = 0;
	}
}

void ExternalCoroutineDone(CoroutineState *coroutine) {
#ifdef __linux__
	pthread_mutex_lock(&externalCoroutineMutex);
	coroutine->nextExternalCoroutine = externalCoroutineUnblockedList;
	externalCoroutineUnblockedList = coroutine;
	pthread_mutex_unlock(&externalCoroutineMutex);
	sem_post(&externalCoroutineSemaphore);
#else
	(void) coroutine;
#endif
}

void *SystemShellExecuteThread(void *_coroutine) {
	CoroutineState *coroutine = (CoroutineState *) _coroutine;
	coroutine->externalCoroutineData.i = system((char *) coroutine->externalCoroutineData2) == 0;
	free((char *) coroutine->externalCoroutineData2);
	ExternalCoroutineDone(coroutine);
	return NULL;
}

int ExternalSystemShellExecute(ExecutionContext *context, Value *returnValue) {
	if (context->c->externalCoroutine) {
		*returnValue = context->c->externalCoroutineData;
		return EXTCALL_RETURN_UNMANAGED;
	}

	STACK_POP_STRING(text, bytes);
	char *temporary = StringZeroTerminate(text, bytes);

	if (temporary) {
		if (systemShellLoggingEnabled) PrintDebug("\033[0;32m%s\033[0m\n", temporary);
		context->c->externalCoroutineData2 = temporary;
#ifdef __linux__
		pthread_t thread;
		pthread_create(&thread, NULL, SystemShellExecuteThread, context->c);
		pthread_detach(thread);
		return EXTCALL_START_COROUTINE;
#else
		SystemShellExecuteThread(context->c);
		*returnValue = context->c->externalCoroutineData;
		return EXTCALL_RETURN_UNMANAGED;
#endif
	} else {
		fprintf(stderr, "Error in ExternalSystemShellExecute: Out of memory.\n");
		returnValue->i = 0;
		return EXTCALL_RETURN_UNMANAGED;
	}
}

void *SystemShellExecuteWithWorkingDirectoryThread(void *_coroutine) {
	CoroutineState *coroutine = (CoroutineState *) _coroutine;
	int status;
	pid_t p = waitpid(coroutine->externalCoroutineData.i, &status, 0);

	if (p != coroutine->externalCoroutineData.i) {
		fprintf(stderr, "waitpid returned %d\n", p);
		perror("waitpid failed");
		coroutine->externalCoroutineData.i = 0;
	} else {
		coroutine->externalCoroutineData.i = WEXITSTATUS(status) == 0;
	}

	ExternalCoroutineDone(coroutine);
	return NULL;
}

int ExternalSystemShellExecuteWithWorkingDirectory(ExecutionContext *context, Value *returnValue) {
	if (context->c->externalCoroutine) {
		*returnValue = context->c->externalCoroutineData;
		return EXTCALL_RETURN_UNMANAGED;
	}

	STACK_POP_STRING_2(entryText, entryBytes, entry2Text, entry2Bytes);
	returnValue->i = 0;
	char *temporary = StringZeroTerminate(entryText, entryBytes);
	if (!temporary) return EXTCALL_RETURN_MANAGED;
	char *temporary2 = StringZeroTerminate(entry2Text, entry2Bytes);
	if (!temporary2) return EXTCALL_RETURN_MANAGED;

	if (systemShellLoggingEnabled) PrintDebug("\033[0;32m(%s) %s\033[0m\n", temporary, temporary2);
	
#ifdef __linux__
	pid_t pid = fork();

	if (pid == 0) {
		Assert(chdir(temporary) == 0);
		exit(system(temporary2));
	} else if (pid < 0) {
		PrintDebug("Unable to fork(), got pid = %d, errno = %d.\n", pid, errno);
		returnValue->i = 0;
	} 

	free(temporary);
	free(temporary2);

	if (pid > 0) {
		context->c->externalCoroutineData.i = pid;
		pthread_t thread;
		pthread_create(&thread, NULL, SystemShellExecuteWithWorkingDirectoryThread, context->c);
		pthread_detach(thread);
		return EXTCALL_START_COROUTINE;
	}
#else
	char *data = (char *) malloc(10000);

	if (!data || data != getcwd(data, 10000)) {
		PrintError4(context, 0, "Could not get the working directory.\n");
		free(data);
		return 0;
	}

	Assert(chdir(temporary) == 0);
	returnValue->i = system(temporary2) == 0;
	chdir(data);
	free(temporary);
	free(temporary2);
	free(data);
#endif

	return EXTCALL_RETURN_UNMANAGED;
}

int ExternalSystemShellEvaluate(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING(text, bytes);
	char *temporary = StringZeroTerminate(text, bytes);

	if (temporary) {
		FILE *f = popen(temporary, "r");
		
		if (f) {
			char *buffer = NULL;
			size_t position = 0;
			size_t bufferAllocated = 0;

			while (true) {
				if (position == bufferAllocated) {
					bufferAllocated = bufferAllocated ? bufferAllocated * 2 : 64;
					char *reallocated = (char *) realloc(buffer, bufferAllocated);
					if (!reallocated) break;
					buffer = reallocated;
				}

				intptr_t bytesRead = fread(buffer + position, 1, bufferAllocated - position, f);

				if (bytesRead <= 0) {
					break;
				}

				position += bytesRead;
			}

			buffer = (char *) realloc(buffer, position); // Shrink to match the size exactly.
			pclose(f);

			RETURN_STRING_NO_COPY(buffer, position);
		} else {
			returnValue->i = 0;
		}

		free(temporary);
	} else {
		fprintf(stderr, "Error in ExternalSystemShellEvaluate: Out of memory.\n");
		returnValue->i = 0;
	}

	return EXTCALL_RETURN_MANAGED;
}

int ExternalSystemShellEnableLogging(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	if (context->c->stackPointer < 1) return -1;
	systemShellLoggingEnabled = context->c->stack[--context->c->stackPointer].i;
	if (context->c->stackIsManaged[context->c->stackPointer]) return -1;
	return EXTCALL_NO_RETURN;
}

int ExternalLog(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	STACK_POP_STRING(entryText, entryBytes);
	fprintf(stderr, "%.*s", (int) entryBytes, (char *) entryText);
	fprintf(stderr, coloredOutput ? "\033[0;m\n" : "\n");
	return EXTCALL_NO_RETURN;
}

int ExternalLogOpenGroup(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	if (context->c->stackPointer < 1) return -1;
	context->c->stackPointer--;
	return EXTCALL_NO_RETURN;
}

int ExternalLogClose(ExecutionContext *context, Value *returnValue) {
	(void) context;
	(void) returnValue;
	return EXTCALL_NO_RETURN;
}

int ExternalTextFormat(ExecutionContext *context, Value *returnValue, const char *mode) {
	if (coloredOutput) {
		char *buffer = malloc(32);
		uintptr_t index = HeapAllocate(context);
		context->heap[index].type = T_STR;
		context->heap[index].bytes = sprintf(buffer, "\033[0;%sm", mode);
		context->heap[index].text = buffer;
		returnValue->i = index;
	} else {
		returnValue->i = 0;
	}

	return EXTCALL_RETURN_MANAGED;
}

int ExternalTextColorError    (ExecutionContext *context, Value *returnValue) { return ExternalTextFormat(context, returnValue, "31"); }
int ExternalTextColorHighlight(ExecutionContext *context, Value *returnValue) { return ExternalTextFormat(context, returnValue, "36"); }
int ExternalTextMonospaced    (ExecutionContext *context, Value *returnValue) { return ExternalTextFormat(context, returnValue,   ""); }
int ExternalTextPlain         (ExecutionContext *context, Value *returnValue) { return ExternalTextFormat(context, returnValue,   ""); }

int ExternalTextWeight(ExecutionContext *context, Value *returnValue) { 
	if (context->c->stackPointer < 1) return -1;
	return ExternalTextFormat(context, returnValue, context->c->stack[--context->c->stackPointer].i > 500 ? "1" : ""); 
}

int ExternalPathCreateDirectory(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING(entryText, entryBytes);
	char *temporary = StringZeroTerminate(entryText, entryBytes);
	if (!temporary) RETURN_ERROR(ENOMEM);
#ifdef _WIN32
#pragma message ("ExternalPathCreateDirectory unimplemented")
#else
	returnValue->i = 1;
	if (mkdir(temporary, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)) returnValue->i = errno == EEXIST;
#endif
	free(temporary);
	if (!returnValue->i) RETURN_ERROR(errno);
	return EXTCALL_RETURN_ERR_UNMANAGED;
}

int ExternalPathDelete(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	STACK_POP_STRING(entryText, entryBytes);
	returnValue->i = 0;
	char *temporary = StringZeroTerminate(entryText, entryBytes);
	if (!temporary) RETURN_ERROR(ENOMEM);
	struct stat s = { 0 };
	bool isDirectory = lstat(temporary, &s) == 0 && S_ISDIR(s.st_mode);
	returnValue->i = isDirectory ? (rmdir(temporary) == 0) : (unlink(temporary) == 0);
	free(temporary);
	if (!returnValue->i) RETURN_ERROR(errno);
	return EXTCALL_RETURN_ERR_UNMANAGED;
}

int ExternalPathExists(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING(entryText, entryBytes);
	returnValue->i = 0;
	if (entryBytes == 0) return EXTCALL_RETURN_UNMANAGED;
	char *temporary = StringZeroTerminate(entryText, entryBytes);
	if (!temporary) return EXTCALL_RETURN_UNMANAGED;
	struct stat s = { 0 };
	returnValue->i = stat(temporary, &s) == 0;
	free(temporary);
	return EXTCALL_RETURN_UNMANAGED;
}

int ExternalPathIsFile(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING(entryText, entryBytes);
	returnValue->i = 0;
	if (entryBytes == 0) return EXTCALL_RETURN_UNMANAGED;
	char *temporary = StringZeroTerminate(entryText, entryBytes);
	if (!temporary) return EXTCALL_RETURN_UNMANAGED;
	struct stat s = { 0 };
	returnValue->i = lstat(temporary, &s) == 0 && S_ISREG(s.st_mode);
	free(temporary);
	return EXTCALL_RETURN_UNMANAGED;
}

int ExternalPathIsDirectory(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING(entryText, entryBytes);
	returnValue->i = 0;
	if (entryBytes == 0) return EXTCALL_RETURN_UNMANAGED;
	char *temporary = StringZeroTerminate(entryText, entryBytes);
	if (!temporary) return EXTCALL_RETURN_UNMANAGED;
	struct stat s = { 0 };
	returnValue->i = lstat(temporary, &s) == 0 && S_ISDIR(s.st_mode);
	free(temporary);
	return EXTCALL_RETURN_UNMANAGED;
}

int ExternalPathIsLink(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING(entryText, entryBytes);
	returnValue->i = 0;
	if (entryBytes == 0) return EXTCALL_RETURN_UNMANAGED;
	char *temporary = StringZeroTerminate(entryText, entryBytes);
	if (!temporary) return EXTCALL_RETURN_UNMANAGED;
	struct stat s = { 0 };
	returnValue->i = lstat(temporary, &s) == 0 && S_ISLNK(s.st_mode);
	free(temporary);
	return EXTCALL_RETURN_UNMANAGED;
}

int ExternalPathMove(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING_2(entryText, entryBytes, entry2Text, entry2Bytes);
	returnValue->i = 0;
	char *temporary = StringZeroTerminate(entryText, entryBytes);
	char *temporary2 = StringZeroTerminate(entry2Text, entry2Bytes);
	bool success = temporary && temporary2 && rename(temporary, temporary2) == 0;
	free(temporary);
	free(temporary2);
	if (!temporary || !temporary2) RETURN_ERROR(ENOMEM);
	if (!success) RETURN_ERROR(errno);
	return EXTCALL_RETURN_ERR_UNMANAGED;
}

int ExternalFileCopy(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING_2(entryText, entryBytes, entry2Text, entry2Bytes);
	returnValue->i = 0;
	char *temporary = StringZeroTerminate(entryText, entryBytes);
	char *temporary2 = StringZeroTerminate(entry2Text, entry2Bytes);

	if (!temporary || !temporary2) {
		free(temporary);
		free(temporary2);
		RETURN_ERROR(ENOMEM);
	}

	if (0 == strcmp(temporary, temporary2)) {
		free(temporary);
		free(temporary2);
		PrintError4(context, 0, "FileCopy called with the source and destination paths identical. Attempting to copy a file to itself is undefined behaviour!\n");
		return 0;
	}

	FILE *f = fopen(temporary, "rb");
	off_t inputFileSize = 0;

	if (f) {
		fseek(f, 0, SEEK_END);
		inputFileSize = ftell(f);
		fseek(f, 0, SEEK_SET);
	}

	FILE *f2 = fopen(temporary2, "wb");
	free(temporary);
	free(temporary2);
	bool okay = true;
	bool modifiedDuringCopy = false;

	if (f && f2) {
		char buffer[4096];
		off_t totalBytesRead = 0;

		while (okay) {
			intptr_t bytesRead = fread(buffer, 1, sizeof(buffer), f);
			totalBytesRead += bytesRead;
			if (totalBytesRead > inputFileSize) { modifiedDuringCopy = true; break; }
			if (bytesRead < 0) okay = false;
			if (bytesRead <= 0) break;
			intptr_t bytesWritten = fwrite(buffer, 1, bytesRead, f2);
			if (bytesWritten != bytesRead) okay = false;
		}

		if (totalBytesRead != inputFileSize) {
			modifiedDuringCopy = true;
		}
	} else {
		okay = false;
	}
	
	if (f && fclose(f)) okay = false;
	if (f2 && fclose(f2)) okay = false;

	if (modifiedDuringCopy) {
		RETURN_ERROR(-1);
	} else if (okay) {
		returnValue->i = 0;
		return EXTCALL_RETURN_ERR_UNMANAGED;
	} else {
		RETURN_ERROR(errno);
	}
}

int ExternalSystemGetEnvironmentVariable(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING(entryText, entryBytes);
	returnValue->i = 0;
	char *temporary = StringZeroTerminate(entryText, entryBytes);
	if (!temporary) RETURN_ERROR(ENOMEM);
	char *data = getenv(temporary);
	free(temporary);
	if (!data) RETURN_ERROR(-1);
	RETURN_STRING_COPY(data, data ? strlen(data) : 0);
	return EXTCALL_RETURN_ERR_MANAGED;
}

int ExternalSystemSetEnvironmentVariable(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING_2(entryText, entryBytes, entry2Text, entry2Bytes);
	char *temporary = StringZeroTerminate(entryText, entryBytes);
	char *temporary2 = StringZeroTerminate(entry2Text, entry2Bytes);

	bool success = temporary && temporary2 && setenv(temporary, temporary2, true) == 0;
	free(temporary);
	free(temporary2);

	if (!success) {
		if (!temporary || !temporary2) RETURN_ERROR(ENOMEM);
		RETURN_ERROR(errno);
	}

	return EXTCALL_RETURN_ERR_UNMANAGED;
}

int External_DirectoryInternalStartIteration(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING(entryText, entryBytes);
	returnValue->i = 0;
	char *temporary = StringZeroTerminate(entryText, entryBytes);
	if (!temporary) RETURN_ERROR(ENOMEM);
	if (directoryIterator) RETURN_ERROR(-1);
	directoryIterator = opendir(temporary);
	free(temporary);
	if (!directoryIterator) RETURN_ERROR(errno);
	return EXTCALL_RETURN_ERR_UNMANAGED;
}

int External_DirectoryInternalEndIteration(ExecutionContext *context, Value *returnValue) {
	(void) context;
	(void) returnValue;
	if (!directoryIterator) return 0;
	closedir(directoryIterator);
	directoryIterator = NULL;
	return EXTCALL_NO_RETURN;
}

int External_DirectoryInternalNextIteration(ExecutionContext *context, Value *returnValue) {
	(void) context;
	if (!directoryIterator) return 0;
	struct dirent *entry = readdir(directoryIterator);
	while (entry && (0 == strcmp(entry->d_name, ".") || 0 == strcmp(entry->d_name, ".."))) entry = readdir(directoryIterator);
	if (!entry) { returnValue->i = 0; return EXTCALL_RETURN_MANAGED; }
	RETURN_STRING_COPY(entry->d_name, strlen(entry->d_name));
	return EXTCALL_RETURN_MANAGED;
}

int ExternalFileReadAll(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING(entryText, entryBytes);
	returnValue->i = 0;
	char *temporary = StringZeroTerminate(entryText, entryBytes);
	if (!temporary) RETURN_ERROR(ENOMEM);
	size_t length = 0;
	void *data = FileLoad(temporary, &length);
	free(temporary);
	if (!data) RETURN_ERROR(errno);
	RETURN_STRING_NO_COPY(data, length);
	return EXTCALL_RETURN_ERR_MANAGED;
}

int ExternalFileGetSize(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING(entryText, entryBytes);
	returnValue->i = 0;
	char *temporary = StringZeroTerminate(entryText, entryBytes);
	if (!temporary) RETURN_ERROR(ENOMEM);
	FILE *file = fopen(temporary, "rb");
	free(temporary);

	if (file) {
		fseek(file, 0, SEEK_END);
		returnValue->i = ftell(file);
		fclose(file);
		return EXTCALL_RETURN_ERR_UNMANAGED;
	} else {
		RETURN_ERROR(errno);
	}
}

int ExternalFileWriteAll(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING_2(entryText, entryBytes, entry2Text, entry2Bytes);
	returnValue->i = 0;
	char *temporary = StringZeroTerminate(entryText, entryBytes);
	if (!temporary) RETURN_ERROR(ENOMEM);
	FILE *f = fopen(temporary, "wb");
	free(temporary);

	if (f) {
		returnValue->i = entry2Bytes == fwrite(entry2Text, 1, entry2Bytes, f);
		if (fclose(f)) RETURN_ERROR(errno);
		if (!returnValue->i) RETURN_ERROR(errno);
		return EXTCALL_RETURN_ERR_UNMANAGED;
	} else {
		RETURN_ERROR(errno);
	}
}

int ExternalFileAppend(ExecutionContext *context, Value *returnValue) {
	STACK_POP_STRING_2(entryText, entryBytes, entry2Text, entry2Bytes);
	returnValue->i = 0;
	char *temporary = StringZeroTerminate(entryText, entryBytes);
	if (!temporary) RETURN_ERROR(ENOMEM);
	FILE *f = fopen(temporary, "ab");
	free(temporary);

	if (f) {
		returnValue->i = entry2Bytes == fwrite(entry2Text, 1, entry2Bytes, f);
		if (fclose(f)) RETURN_ERROR(errno);
		if (!returnValue->i) RETURN_ERROR(errno);
		return EXTCALL_RETURN_ERR_UNMANAGED;
	} else {
		RETURN_ERROR(errno);
	}
}

int ExternalPathGetDefaultPrefix(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	char *data = (char *) malloc(10000);

	if (!data || data != getcwd(data, 10000)) {
		PrintError4(context, 0, "Could not get the working directory.\n");
		free(data);
		return 0;
	}

	RETURN_STRING_NO_COPY(realloc(data, strlen(data) + 1), strlen(data));
	return EXTCALL_RETURN_MANAGED;
}

int ExternalPathSetDefaultPrefixToScriptSourceDirectory(ExecutionContext *context, Value *returnValue) {
	(void) context;
	returnValue->i = chdir(scriptSourceDirectory) == 0;
	if (!returnValue->i) RETURN_ERROR(errno);
	return EXTCALL_RETURN_ERR_UNMANAGED;
}

int ExternalPersistRead(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	STACK_POP_STRING(entryText, entryBytes);
	returnValue->i = 0;
	if (entryBytes == 0) return EXTCALL_RETURN_UNMANAGED;
	char *temporary = StringZeroTerminate(entryText, entryBytes);
	if (!temporary) return EXTCALL_RETURN_UNMANAGED;
	free(context->scriptPersistFile);
	context->scriptPersistFile = temporary;
	size_t length = 0;
	uint8_t *data = (uint8_t *) FileLoad(temporary, &length);
	returnValue->i = 1;

	for (uintptr_t i = 0; i < length; ) {
		uint32_t variableNameLength, variableDataLength;
		char variableName[256];
		if (length < sizeof(uint32_t) * 2) break;
		if (i > length - sizeof(uint32_t) * 2) break;
		memcpy(&variableNameLength, &data[i], sizeof(uint32_t)); i += sizeof(uint32_t);
		memcpy(&variableDataLength, &data[i], sizeof(uint32_t)); i += sizeof(uint32_t);
		if (variableNameLength > 256) break;
		if (length < variableNameLength + variableDataLength) break;
		if (i > length - variableNameLength - variableDataLength) break;
		memcpy(variableName, &data[i], variableNameLength); i += variableNameLength;
		uintptr_t k = context->mainModule->globalVariableOffset;
		Scope *scope = context->mainModule->rootNode->scope;

		for (uintptr_t j = 0; j < scope->entryCount; j++) {
			if (scope->entries[j]->token.textBytes == variableNameLength
					&& 0 == MemoryCompare(scope->entries[j]->token.text, variableName, variableNameLength)
					&& scope->entries[j]->type == T_DECLARE
					&& scope->entries[j]->isPersistentVariable) {
				if (scope->entries[j]->expressionType->type == T_STR) {
					// TODO Handling allocation failures.
					context->globalVariables[k].i = HeapAllocate(context);
					context->heap[context->globalVariables[k].i].type = T_STR;
					context->heap[context->globalVariables[k].i].bytes = variableDataLength;
					context->heap[context->globalVariables[k].i].text = AllocateResize(NULL, variableDataLength);
					memcpy(context->heap[context->globalVariables[k].i].text, &data[i], variableDataLength);
				} else if (scope->entries[j]->expressionType->type == T_INT) {
					if (variableDataLength == sizeof(int64_t)) memcpy(&context->globalVariables[k].i, &data[i], sizeof(int64_t));
				} else if (scope->entries[j]->expressionType->type == T_FLOAT) {
					if (variableDataLength == sizeof(double)) memcpy(&context->globalVariables[k].f, &data[i], sizeof(double));
				} else if (scope->entries[j]->expressionType->type == T_BOOL) {
					if (variableDataLength == 1) context->globalVariables[k].i = data[i] == 1;
				} else {
					// TODO What should happen here?
				}

				break;
			}

			if (ScopeIsVariableType(scope->entries[j])) {
				k++;
			}
		}

		i += variableDataLength;
	}

	free(data);
	return EXTCALL_RETURN_UNMANAGED;
}

int ExternalPersistWrite(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;

	if (!context->scriptPersistFile) {
		// TODO Report the file/line number.
		PrintError3("Attempted to modify a persistent variable before calling PersistRead.\n");
		return 0;
	}

	FILE *f = fopen(context->scriptPersistFile, "wb");

	if (!f) {
		PrintDebug("\033[0;32mWarning: Persistent variables could not written. The file could not be opened.\033[0m\n");
		return EXTCALL_NO_RETURN;
	}

	uintptr_t k = context->mainModule->globalVariableOffset;
	Scope *scope = context->mainModule->rootNode->scope;

	for (uintptr_t j = 0; j < scope->entryCount; j++) {
		if (scope->entries[j]->type == T_DECLARE && scope->entries[j]->isPersistentVariable) {
			uint32_t variableNameLength = scope->entries[j]->token.textBytes;
			fwrite(&variableNameLength, 1, sizeof(uint32_t), f);

			if (scope->entries[j]->expressionType->type == T_STR) {
				HeapEntry *entry = &context->heap[context->globalVariables[k].i];
				const char *text;
				size_t bytes;
				ScriptHeapEntryToString(context, entry, &text, &bytes);
				uint32_t variableDataLength = bytes;
				fwrite(&variableDataLength, 1, sizeof(uint32_t), f);
				fwrite(scope->entries[j]->token.text, 1, variableNameLength, f);
				if (bytes) fwrite(text, 1, variableDataLength, f);
			} else if (scope->entries[j]->expressionType->type == T_INT) {
				uint32_t variableDataLength = sizeof(int64_t);
				fwrite(&variableDataLength, 1, sizeof(uint32_t), f);
				fwrite(scope->entries[j]->token.text, 1, variableNameLength, f);
				fwrite(&context->globalVariables[k].i, 1, sizeof(int64_t), f);
			} else if (scope->entries[j]->expressionType->type == T_FLOAT) {
				uint32_t variableDataLength = sizeof(double);
				fwrite(&variableDataLength, 1, sizeof(uint32_t), f);
				fwrite(scope->entries[j]->token.text, 1, variableNameLength, f);
				fwrite(&context->globalVariables[k].f, 1, sizeof(double), f);
			} else if (scope->entries[j]->expressionType->type == T_BOOL) {
				uint32_t variableDataLength = 1;
				fwrite(&variableDataLength, 1, sizeof(uint32_t), f);
				fwrite(scope->entries[j]->token.text, 1, variableNameLength, f);
				uint8_t b = context->globalVariables[k].i == 1;
				fwrite(&b, 1, sizeof(uint8_t), f);
			} else {
				PrintDebug("\033[0;32mWarning: The persistent variable %.*s could not be written, because it had an unsupported type.\033[0m\n",
						scope->entries[j]->token.textBytes, scope->entries[j]->token.text);
			}
		}

		if (ScopeIsVariableType(scope->entries[j])) {
			k++;
		}
	}

	if (fclose(f)) {
		PrintDebug("\033[0;32mWarning: Persistent variables could not written. The file could not be closed.\033[0m\n");
		return EXTCALL_NO_RETURN;
	}

	return EXTCALL_NO_RETURN;
}

int ExternalConsoleGetLine(ExecutionContext *context, Value *returnValue) {
#ifdef _WIN32
#pragma message ("ExternalConsoleGetLine unimplemented")
	return -1;
#else
	char *line = NULL;
	size_t pos;
	size_t unused = getline(&line, &pos, stdin);
	(void) unused;
	uintptr_t index = HeapAllocate(context);
	context->heap[index].type = T_STR;
	context->heap[index].bytes = strlen(line) - 1;
	context->heap[index].text = line;
	returnValue->i = index;
	return EXTCALL_RETURN_MANAGED;
#endif
}

int ExternalSystemGetProcessorCount(ExecutionContext *context, Value *returnValue) {
	(void) context;
#ifdef _WIN32
#pragma message ("ExternalSystemGetProcessorCount unimplemented")
	returnValue->i = 1;
#else
	returnValue->i = sysconf(_SC_NPROCESSORS_CONF);
#endif
	if (returnValue->i < 1) returnValue->i = 1;
	if (returnValue->i > 10000) returnValue->i = 1; // Values this large are obviously wrong.
	return EXTCALL_RETURN_UNMANAGED;
}

int ExternalSystemRunningAsAdministrator(ExecutionContext *context, Value *returnValue) {
	(void) context;
#ifdef _WIN32
#pragma message ("ExternalSystemRunningAsAdministrator unimplemented")
	returnValue->i = 0;
#else
	returnValue->i = geteuid() == 0;
#endif
	return EXTCALL_RETURN_UNMANAGED;
}

int ExternalSystemGetHostName(ExecutionContext *context, Value *returnValue) {
	(void) context;
	const char *name;
#ifdef _WIN32
	name = "Windows";
#else
	struct utsname buffer;
	uname(&buffer);
	name = buffer.sysname;
#endif
	RETURN_STRING_COPY(name, strlen(name));
	return EXTCALL_RETURN_MANAGED;
}

int ExternalRandomInt(ExecutionContext *context, Value *returnValue) {
	if (context->c->stackPointer < 2) return -1;
	int64_t min = context->c->stack[context->c->stackPointer - 1].i;
	int64_t max = context->c->stack[context->c->stackPointer - 2].i;
	if (max < min) { PrintError4(context, 0, "RandomInt() called with maximum limit (%ld) less than the minimum limit (%ld).\n", max, min); return 0; }
	returnValue->i = rand() % (max - min + 1) + min;
	context->c->stackPointer -= 2;
	return EXTCALL_RETURN_UNMANAGED;
}

void *SystemSleepThread(void *_coroutine) {
	CoroutineState *coroutine = (CoroutineState *) _coroutine;
	struct timespec sleepTime;
	uint64_t x = 1000000 * coroutine->externalCoroutineData.i;
	sleepTime.tv_sec = x / 1000000000;
	sleepTime.tv_nsec = x % 1000000000;
	nanosleep(&sleepTime, NULL);
	ExternalCoroutineDone(coroutine);
	return NULL;
}

int ExternalSystemSleepMs(ExecutionContext *context, Value *returnValue) {
	(void) returnValue;
	if (context->c->externalCoroutine) return EXTCALL_NO_RETURN;
	if (context->c->stackPointer < 1) return -1;
	context->c->externalCoroutineData = context->c->stack[--context->c->stackPointer];
#ifdef __linux__
	pthread_t thread;
	pthread_create(&thread, NULL, SystemSleepThread, context->c);
	pthread_detach(thread);
	return EXTCALL_START_COROUTINE;
#else
	struct timespec sleepTime;
	uint64_t x = 1000000 * context->c->externalCoroutineData.i;
	sleepTime.tv_sec = x / 1000000000;
	sleepTime.tv_nsec = x % 1000000000;
	nanosleep(&sleepTime, NULL);
	return EXTCALL_NO_RETURN;
#endif
}

int ExternalOpenDocumentEnumerate(ExecutionContext *context, Value *returnValue) {
	(void) context;
	returnValue->i = 0;
	return EXTCALL_RETURN_MANAGED;
}

CoroutineState *ExternalCoroutineWaitAny(ExecutionContext *context) {
	(void) context;
	while (sem_wait(&externalCoroutineSemaphore) == -1);
	pthread_mutex_lock(&externalCoroutineMutex);
	CoroutineState *unblocked = externalCoroutineUnblockedList;
	Assert(unblocked);
	externalCoroutineUnblockedList = unblocked->nextExternalCoroutine;
	unblocked->nextExternalCoroutine = NULL;
	pthread_mutex_unlock(&externalCoroutineMutex);
	return unblocked;
}

void PrintREPLResult(ExecutionContext *context, Node *type, Value value) {
	if (type->type == T_INT) {
		printf("%ld", value.i);
	} else if (type->type == T_BOOL) {
		printf("%s", value.i ? "true" : "false");
	} else if (type->type == T_NULL) {
		printf("null");
	} else if (type->type == T_FLOAT) {
		printf("%f", value.f);
	} else if (type->type == T_STR) {
		Assert(context->heapEntriesAllocated > (uint64_t) value.i);
		HeapEntry *entry = &context->heap[value.i];
		const char *valueText;
		size_t valueBytes;
		ScriptHeapEntryToString(context, entry, &valueText, &valueBytes);
		printf("%.*s", (int) valueBytes, valueText);
	} else if (type->type == T_ERR) {
		if (value.i) {
			Assert(context->heapEntriesAllocated > (uint64_t) value.i);
			HeapEntry *entry = &context->heap[value.i];

			if (entry->success) {
				if (type->firstChild->type == T_VOID) {
					printf("Success.");
				} else {
					PrintREPLResult(context, type->firstChild, entry->errorValue);
				}
			} else {
				Assert(context->heapEntriesAllocated > (uint64_t) entry->errorValue.i);
				const char *valueText;
				size_t valueBytes;
				ScriptHeapEntryToString(context, &context->heap[entry->errorValue.i], &valueText, &valueBytes);
				printf("Error: %.*s.", (int) valueBytes, valueText);
			}
		} else {
			printf("Error: UNKNOWN.");
		}
	} else if (type->type == T_LIST) {
		Assert(context->heapEntriesAllocated > (uint64_t) value.i);
		HeapEntry *entry = &context->heap[value.i];
		Assert(entry->type == T_LIST);

		if (!entry->length) {
			printf("Empty list.\n");
		} else {
			printf("[ ");

			for (uintptr_t i = 0; i < entry->length; i++) {
				if (i) printf(", ");
				PrintREPLResult(context, type->firstChild, entry->list[i]);
			}

			printf(" ]");
		}
	} else if (type->type == T_STRUCT) {
		Assert(context->heapEntriesAllocated > (uint64_t) value.i);
		HeapEntry *entry = &context->heap[value.i];
		Assert(entry->type == T_STRUCT);
		uintptr_t i = 0;
		printf("{ ");

		Node *field = type->firstChild;

		while (field) {
			if (i) printf(", ");
			Assert(i != entry->fieldCount);
			PrintREPLResult(context, field->firstChild, entry->fields[i]);
			field = field->sibling;
			i++;
		}

		printf(" }");
	} else if (type->type == T_FUNCPTR) {
		printf("Function pointer.");
	} else {
		printf("The type of the result was not recognized.");
	}
}

void ExternalPassREPLResult(ExecutionContext *context, Value value) {
	PrintREPLResult(context, context->functionData->replResultType, value);
	printf("\n");
}

void *AllocateFixed(size_t bytes) {
	if (!bytes) {
		return NULL;
	}

	bytes = (bytes + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1);

	if (bytes >= fixedAllocationCurrentSize || fixedAllocationCurrentPosition >= fixedAllocationCurrentSize - bytes) {
#if 1
		fixedAllocationCurrentSize = bytes > 1048576 ? bytes : 1048576;
#else
		fixedAllocationCurrentSize = bytes;
#endif
		fixedAllocationCurrentPosition = 0;
		fixedAllocationCurrentBlock = calloc(1, fixedAllocationCurrentSize + sizeof(void *));

		if (!fixedAllocationCurrentBlock) {
			fprintf(stderr, "Internal error: not enough memory to run the script.\n");
			exit(1);
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
	if (bytes == 0) {
		free(old);
		return NULL;
	}

	void *p = realloc(old, bytes);

	if (!p && bytes) {
		fprintf(stderr, "Internal error: not enough memory to run the script.\n");
		exit(1);
		// TODO Better error handling.
	}

	return p;
}

int MemoryCompare(const void *a, const void *b, size_t bytes) {
	return memcmp(a, b, bytes);
}

void MemoryCopy(void *a, const void *b, size_t bytes) {
	memcpy(a, b, bytes);
}

size_t PrintIntegerToBuffer(char *buffer, size_t bufferBytes, int64_t i) {
	snprintf(buffer, bufferBytes, "%ld", i);
	return strlen(buffer);
}

size_t PrintFloatToBuffer(char *buffer, size_t bufferBytes, double f) {
	snprintf(buffer, bufferBytes, "%f", f);
	return strlen(buffer);
}

void PrintDebug(const char *format, ...) {
	va_list arguments;
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
}

void PrintError(Tokenizer *tokenizer, const char *format, ...) {
	fprintf(stderr, "\033[0;33mError on line %d of '%s':\033[0m\n", (int) tokenizer->line, tokenizer->module->path);
	va_list arguments;
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	PrintLine(tokenizer->module, tokenizer->line);
}

void PrintError2(Tokenizer *tokenizer, Node *node, const char *format, ...) {
	fprintf(stderr, "\033[0;33mError on line %d of '%s':\033[0m\n", (int) node->token.line, tokenizer->module->path);
	va_list arguments;
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	PrintLine(tokenizer->module, node->token.line);
}

void PrintError3(const char *format, ...) {
	fprintf(stderr, "\033[0;33mGeneral error:\033[0m\n");
	va_list arguments;
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
}

void PrintError4(ExecutionContext *context, uint32_t instructionPointer, const char *format, ...) {
	LineNumber lineNumber = { 0 };
	LineNumberLookup(context, instructionPointer, &lineNumber);
	fprintf(stderr, "\033[0;33mRuntime error on line %d of '%s'\033[0m:\n", lineNumber.lineNumber, 
			lineNumber.importData ? lineNumber.importData->path : "??");
	va_list arguments;
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	PrintLine(lineNumber.importData, lineNumber.lineNumber);
	fprintf(stderr, "Back trace:\n");
	PrintBackTrace(context, instructionPointer, context->c, "");
}

void *LibraryLoad(const char *name) {
	Assert(strlen(name) < 256);
	char name2[256 + 20];
	strcpy(name2, "l");
	strcat(name2, name);
	strcat(name2, ".so");

	void *library = dlopen(name2, RTLD_LAZY);

	if (!library) {
		PrintError3("The library \"%s\" could not be found or loaded.\n", name2);
	}

	return library;
}

void *LibraryGetAddress(void *library, const char *name) {
	Assert(strlen(name) < 256);
	Assert(library);
	char name2[256 + 20];
	strcpy(name2, "ScriptExt");
	strcat(name2, name);

	void *address = dlsym(library, name2);

	if (!address) {
		PrintError3("The library symbol \"%s\" could not be found.\n", name2);
	}

	return address;
}

void *FileLoad(const char *path, size_t *length) {
	FILE *file = fopen(path, "rb");
	if (!file) return NULL;
	fseek(file, 0, SEEK_END);
	size_t fileSize = ftell(file);
	fseek(file, 0, SEEK_SET);
	char *buffer = (char *) malloc(fileSize + 1);
	buffer[fileSize] = 0;
	size_t readBytes = fread(buffer, 1, fileSize, file);
	fclose(file);
	if (readBytes != fileSize) { free(buffer); return NULL; }
	if (length) *length = fileSize;
	return buffer;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <engine options...> <path to script> <script options...>\n", argv[0]);
		return 1;
	}

	coloredOutput = isatty(STDERR_FILENO);
	srand(time(NULL));
	sem_init(&externalCoroutineSemaphore, 0, 0);

	char *scriptPath = NULL;
	char *evaluateString = NULL;
	bool evaluateMode = false;

	for (int i = 1; i < argc; i++) {
		if (argv[i][0] != '-') {
			if (evaluateMode) {
				evaluateString = argv[i];
			} else {
				scriptPath = argv[i];
			}

			options = argv + i + 1;
			optionCount = argc - i - 1;
			optionsMatched = (bool *) calloc(argc, sizeof(bool));
			break;
		} else if (0 == memcmp(argv[i], "--start=", 8)) {
			startFunction = argv[i] + 8;
			startFunctionBytes = strlen(argv[i]) - 8;
		} else if (0 == memcmp(argv[i], "--debug-bytecode=", 17)) {
			debugBytecodeLevel = atoi(argv[i] + 17);
		} else if (0 == strcmp(argv[i], "--evaluate") || 0 == strcmp(argv[i], "-e")) {
			evaluateMode = true;
		} else {
			fprintf(stderr, "Unrecognised engine option: '%s'.\n", argv[i]);
			return 1;
		}
	}

	if (!scriptPath && !evaluateString) {
		fprintf(stderr, "Error: %s\n", evaluateMode ? "String to evaluate not specified." : "Path to script not specified.");
		return 1;
	}

	if (scriptPath) {
		scriptSourceDirectory = (char *) malloc(strlen(scriptPath) + 2);
		strcpy(scriptSourceDirectory, scriptPath);
		char *lastSlash = strrchr(scriptSourceDirectory, '/');
		if (lastSlash) *lastSlash = 0;
		else strcpy(scriptSourceDirectory, ".");
	} else {
		scriptSourceDirectory = (char *) malloc(2);
		scriptSourceDirectory[0] = '.';
		scriptSourceDirectory[1] = 0;
	}

	size_t dataBytes = evaluateMode ? strlen(evaluateString) : 0;
	void *data = evaluateMode ? malloc(strlen(evaluateString)) : FileLoad(scriptPath, &dataBytes);
	if (evaluateMode) memcpy(data, evaluateString, dataBytes);

	if (!data) {
		PrintDebug("Error: Could not load the input file '%s'.\n", scriptPath);
		return 1;
	}

	if (evaluateMode) scriptPath = "[input]";
	int result = ScriptExecuteFromFile(scriptPath, strlen(scriptPath), data, dataBytes, evaluateMode);

	while (fixedAllocationBlocks) {
		void *block = fixedAllocationBlocks;
		fixedAllocationBlocks = (void **) *fixedAllocationBlocks;
		free(block);
	}

	free(scriptSourceDirectory);
	free(optionsMatched);

	return result;
}

#endif
