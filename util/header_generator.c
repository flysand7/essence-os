const char **apiTableEntries;

File output, outputAPIArray, outputSyscallArray, outputDependencies, outputEnumStringsArray;
char *buffer;
int position;

#define DEST_API_ARRAY "bin/generated_code/api_array.h"
#define DEST_SYSCALL_ARRAY "bin/generated_code/syscall_array.h"
#define DEST_ENUM_STRINGS_ARRAY "bin/generated_code/enum_strings_array.h"
#define DEST_DEPENDENCIES "bin/dependency_files/api_header.d"

typedef struct Token {
#define TOKEN_IDENTIFIER (1)
#define TOKEN_LEFT_BRACE (2) 
#define TOKEN_RIGHT_BRACE (3) 
#define TOKEN_EQUALS (4) 
#define TOKEN_ENUM (5) 
#define TOKEN_STRUCT (6) 
#define TOKEN_NUMBER (8) 
#define TOKEN_ASTERISK (9) 
#define TOKEN_COMMA (10) 
#define TOKEN_SEMICOLON (11) 
#define TOKEN_DEFINE (12) 
#define TOKEN_FUNCTION (13) 
#define TOKEN_FUNCTION_NOT_IN_KERNEL (15) 
#define TOKEN_EOF (16) 
#define TOKEN_ELLIPSIS (17)
#define TOKEN_UNION (18)
#define TOKEN_VOLATILE (19)
#define TOKEN_CONST (20)
#define TOKEN_LEFT_BRACKET (21) 
#define TOKEN_RIGHT_BRACKET (22) 
#define TOKEN_LEFT_PAREN (23) 
#define TOKEN_RIGHT_PAREN (24) 
#define TOKEN_INCLUDE (26)
#define TOKEN_API_TYPE (29)
#define TOKEN_FUNCTION_POINTER (30)
#define TOKEN_TYPE_NAME (31)
#define TOKEN_PRIVATE (32)
	int type, value;
	char *text;
} Token;

#define ENTRY_ROOT (0)
#define ENTRY_DEFINE (1)
#define ENTRY_ENUM (3)
#define ENTRY_STRUCT (4)
#define ENTRY_UNION (5)
#define ENTRY_FUNCTION (6)
#define ENTRY_VARIABLE (7)
#define ENTRY_API_TYPE (8)
#define ENTRY_TYPE_NAME (9)

typedef struct Entry {
	int type;

	char *name;
	struct Entry *children;

	bool isPrivate;

	union {
		struct {
			char *type, *arraySize, *initialValue;
			int pointer;
			bool isArray, isVolatile, isConst, isForwardDeclared;
		} variable;

		struct {
			char *value;
		} define;

		struct {
			bool inKernel, functionPointer;
			int apiArrayIndex;
		} function;

		struct {
			char *parent;
		} apiType;

		char *oldTypeName;
	};
} Entry;

char *TokenToString(Token token) {
	if (!token.value) return NULL;
	char *string = (char *) malloc(token.value + 1);
	memcpy(string, token.text, token.value);
	string[token.value] = 0;
	return string;
}

int currentLine = 1;

Token NextToken() {
	tryAgain:;

	char c = buffer[position++];

	if (c == '\t') goto tryAgain;
	if (c == '\n') { currentLine++; goto tryAgain; }
	if (c == ' ') goto tryAgain;

	if (c == '/' && buffer[position] == '/') { while (buffer[position++] != '\n'); goto tryAgain; } 

	Token token = {};

	if (c == 0) token.type = TOKEN_EOF;

	else if (c == '{') token.type = TOKEN_LEFT_BRACE;
	else if (c == '}') token.type = TOKEN_RIGHT_BRACE;
	else if (c == '[') token.type = TOKEN_LEFT_BRACKET;
	else if (c == ']') token.type = TOKEN_RIGHT_BRACKET;
	else if (c == '(') token.type = TOKEN_LEFT_PAREN;
	else if (c == ')') token.type = TOKEN_RIGHT_PAREN;
	else if (c == '=') token.type = TOKEN_EQUALS;
	else if (c == '*') token.type = TOKEN_ASTERISK;
	else if (c == ',') token.type = TOKEN_COMMA;
	else if (c == ';') token.type = TOKEN_SEMICOLON;

	else if (c == '.' && buffer[position] == '.' && buffer[position + 1] == '.') position += 2, token.type = TOKEN_ELLIPSIS;

	else if ((c >= '0' && c <= '9') || c == '-') {
		token.type = TOKEN_NUMBER;
		token.text = buffer + position - 1;

		do {
			token.value++;
			c = buffer[position++];
		} while ((c >= '0' && c <= '9'));

		position--;
	}

	else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
		token.type = TOKEN_IDENTIFIER;
		token.text = buffer + position - 1;

		do {
			token.value++;
			c = buffer[position++];
		} while ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (c >= '0' && c <= '9'));

		position--;

#define COMPARE_KEYWORD(x, y) if (strlen(x) == (size_t) token.value && 0 == memcmp(x, token.text, token.value)) token.type = y
		COMPARE_KEYWORD("define", TOKEN_DEFINE);
		COMPARE_KEYWORD("enum", TOKEN_ENUM);
		COMPARE_KEYWORD("struct", TOKEN_STRUCT);
		COMPARE_KEYWORD("function", TOKEN_FUNCTION);
		COMPARE_KEYWORD("function_not_in_kernel", TOKEN_FUNCTION_NOT_IN_KERNEL);
		COMPARE_KEYWORD("union", TOKEN_UNION);
		COMPARE_KEYWORD("volatile", TOKEN_VOLATILE);
		COMPARE_KEYWORD("const", TOKEN_CONST);
		COMPARE_KEYWORD("include", TOKEN_INCLUDE);
		COMPARE_KEYWORD("opaque_type", TOKEN_API_TYPE);
		COMPARE_KEYWORD("function_pointer", TOKEN_FUNCTION_POINTER);
		COMPARE_KEYWORD("type_name", TOKEN_TYPE_NAME);
		COMPARE_KEYWORD("private", TOKEN_PRIVATE);
	}

	else {
		Log("unrecognised token '%c', at '%.*s'\n", c, 10, buffer + position - 5);
		exit(1);
	}

	return token;
}

bool FoundEndOfLine(int length) {
	if (buffer[position + length] == '\n') return true;
	if (buffer[position + length] == '/' && buffer[position + length + 1] == '/') return true;
	return false;
}

Token ParseVariable(Token token, bool forFunction, Entry *_variable, Entry *parent) {
	int pointer = 0;
	bool array = false, isVolatile = false, isConst = false, isForwardDeclared = false;
	Token arraySize = {};

	Token type = token;

	if (type.type == TOKEN_VOLATILE) { isVolatile = true; type = NextToken(); }
	if (type.type == TOKEN_CONST) { isConst = true; type = NextToken(); }
	if (type.type == TOKEN_STRUCT) { isForwardDeclared = true; type = NextToken(); }

	commaRepeat:;
	pointer = 0;
	
	Token name = {};

	if (type.type != TOKEN_ELLIPSIS) {
		name = NextToken();

		while (name.type == TOKEN_ASTERISK) {
			name = NextToken();
			pointer++;
		}

		if (name.type == TOKEN_FUNCTION) name.type = TOKEN_IDENTIFIER;
	}

	Token semicolon = NextToken();

	if (semicolon.type == TOKEN_LEFT_BRACKET) {
		arraySize = NextToken();
		assert(arraySize.type == TOKEN_IDENTIFIER || arraySize.type == TOKEN_NUMBER || arraySize.type == TOKEN_RIGHT_BRACKET);
		array = true;

		if (arraySize.type != TOKEN_RIGHT_BRACKET) {
			assert(NextToken().type == TOKEN_RIGHT_BRACKET);
		}

		semicolon = NextToken();
	}

	if (type.type != TOKEN_ELLIPSIS) {
		assert(type.type == TOKEN_IDENTIFIER);
		assert(name.type == TOKEN_IDENTIFIER);
	}

	Entry entry = { .type = ENTRY_VARIABLE, .name = TokenToString(name), .variable = {
		.type = TokenToString(type), .arraySize = TokenToString(arraySize), .pointer = pointer, .isArray = array, 
		.isVolatile = isVolatile, .isConst = isConst, .isForwardDeclared = isForwardDeclared
	} };

	if (forFunction && semicolon.type == TOKEN_EQUALS) {
		entry.variable.initialValue = TokenToString(NextToken());
		semicolon = NextToken();
	}

	if (_variable) {
		*_variable = entry;
	}

	arrput(parent->children, entry);

	if (forFunction) {
		return semicolon;
	}

	if (semicolon.type == TOKEN_SEMICOLON) {
	} else if (semicolon.type == TOKEN_COMMA) {
		goto commaRepeat;
	} else {
		assert(false);
	}

	return semicolon;
}

Entry ParseRecord(bool isUnion) {
	Entry entry = { .type = isUnion ? ENTRY_UNION : ENTRY_STRUCT };
	Token token = NextToken();

	while (true) {
		if (token.type == TOKEN_RIGHT_BRACE) {
			break;
		} else if (token.type == TOKEN_UNION) {
			assert(NextToken().type == TOKEN_LEFT_BRACE);
			arrput(entry.children, ParseRecord(true));
			assert(NextToken().type == TOKEN_SEMICOLON);
		} else if (token.type == TOKEN_STRUCT) {
			assert(NextToken().type == TOKEN_LEFT_BRACE);
			Entry child = ParseRecord(false);
			Token name = NextToken();

			if (name.type == TOKEN_IDENTIFIER) {
				assert(NextToken().type == TOKEN_SEMICOLON);
				child.name = TokenToString(name);
			} else {
				assert(name.type == TOKEN_SEMICOLON);
			}

			arrput(entry.children, child);
		} else {
			ParseVariable(token, false, NULL, &entry);
		}

		token = NextToken();
	}

	return entry;
}

void ParseFile(Entry *root, const char *name) {
	if (outputDependencies.ready) {
		FilePrintFormat(outputDependencies, "%s\n", name);
	}

	char *oldBuffer = buffer;
	int oldPosition = position;
	buffer = (char *) LoadFile(name, NULL);
	assert(buffer);
	position = 0;

	Token token;
	bool isPrivate = false;

	while (true) {
		token = NextToken();

		if (token.type == TOKEN_DEFINE) {
			Token identifier = NextToken();
			size_t length = 0;
			while (!FoundEndOfLine(length)) length++;
			Entry entry = { .type = ENTRY_DEFINE, .name = TokenToString(identifier), .isPrivate = isPrivate };
			entry.define.value = TokenToString((Token) { .value = (int) length, .text = buffer + position });
			arrput(root->children, entry);
			position += length;
		} else if (token.type == TOKEN_INCLUDE) {
			size_t length = 0;
			while (!FoundEndOfLine(length)) length++;
			char a = buffer[position + length];
			int oldCurrentLine = currentLine;
			currentLine = 1;
			buffer[position + length] = 0;
			ParseFile(root, buffer + position + 1);
			currentLine = oldCurrentLine;
			buffer[position + length] = a;
			position += length;
		} else if (token.type == TOKEN_ENUM) {
			Token name = NextToken();
			assert(name.type == TOKEN_IDENTIFIER);
			assert(NextToken().type == TOKEN_LEFT_BRACE);

			Entry entry = { .type = ENTRY_ENUM, .name = TokenToString(name), .isPrivate = isPrivate };
			Token token = NextToken();

			while (true) {
				if (token.type == TOKEN_RIGHT_BRACE) {
					break;
				}

				Token entryName = token;
				token = NextToken();
				assert(entryName.type == TOKEN_IDENTIFIER);
				Entry define = { .type = ENTRY_DEFINE, .name = TokenToString(entryName) };

				if (token.type == TOKEN_EQUALS) {
					size_t length = 0;
					while (!FoundEndOfLine(length)) length++;
					define.define.value = TokenToString((Token) { .value = (int) length, .text = buffer + position });
					position += length;
					token = NextToken();
				}

				arrput(entry.children, define);
			}

			arrput(root->children, entry);
		} else if (token.type == TOKEN_STRUCT) {
			Token structName = NextToken();
			assert(structName.type == TOKEN_IDENTIFIER);
			assert(NextToken().type == TOKEN_LEFT_BRACE);
			Entry entry = ParseRecord(false);
			entry.isPrivate = isPrivate;
			entry.name = TokenToString(structName);
			arrput(root->children, entry);
		} else if (token.type == TOKEN_FUNCTION || token.type == TOKEN_FUNCTION_NOT_IN_KERNEL 
				|| token.type == TOKEN_FUNCTION_POINTER) {
			bool inKernel = token.type != TOKEN_FUNCTION_NOT_IN_KERNEL; 
			Entry objectFunctionType;
			bool firstVariable = true;
			Entry entry = { .type = ENTRY_FUNCTION, .isPrivate = isPrivate, .function = { .inKernel = inKernel, .apiArrayIndex = 0 } };

			if (token.type == TOKEN_FUNCTION_POINTER) {
				entry.function.functionPointer = true;
			}

			Token leftParen = ParseVariable(NextToken(), true, NULL, &entry);
			assert(leftParen.type == TOKEN_LEFT_PAREN);
			Token token = NextToken();

			while (true) {
				if (token.type == TOKEN_RIGHT_PAREN) break;
				if (token.type == TOKEN_COMMA) token = NextToken();
				token = ParseVariable(token, true, firstVariable ? &objectFunctionType : NULL, &entry);
				firstVariable = false;
			}

			arrput(root->children, entry);
		} else if (token.type == TOKEN_API_TYPE) {
			Token name = NextToken(), parent = NextToken();
			Entry entry = { .type = ENTRY_API_TYPE, .name = TokenToString(name), .apiType = { .parent = TokenToString(parent) } };
			arrput(root->children, entry);
		} else if (token.type == TOKEN_TYPE_NAME) {
			Token oldName = NextToken(), newName = NextToken();
			Entry entry = { .type = ENTRY_TYPE_NAME, .isPrivate = isPrivate, .name = TokenToString(newName), .oldTypeName = TokenToString(oldName) };
			arrput(root->children, entry);
		} else if (token.type == TOKEN_SEMICOLON) {
		} else if (token.type == TOKEN_PRIVATE) {
		} else if (token.type == TOKEN_EOF) {
			break;
		} else {
			Log("unexpected token '%.*s' at top level\n", token.value, token.text);
			exit(1);
		}

		isPrivate = token.type == TOKEN_PRIVATE;
	}

	free(buffer);
	buffer = oldBuffer;
	position = oldPosition;
}

bool OutputCVariable(Entry *variable, bool noInitialValue, const char *nameOverride, 
		bool forFunction, bool forFunctionPointer) {
	FilePrintFormat(output, "%s%s%s", variable->variable.isVolatile ? "volatile " : "", 
			variable->variable.isConst ? "const " : "",
			variable->variable.isForwardDeclared ? "struct " : "");

	if (variable->variable.type) {
		if (0 == strcmp(variable->variable.type, "STRING")) {
			FilePrintFormat(output, "const char *%s", nameOverride ?: variable->name);
			assert(!variable->variable.pointer && !variable->variable.isArray);
			char *initialValue = variable->variable.initialValue;
			bool isBlankString = initialValue && 0 == strcmp(initialValue, "BLANK_STRING");
			if (!isBlankString) assert(!initialValue || !initialValue[0]);
			if (!noInitialValue && isBlankString) FilePrintFormat(output, " = nullptr");

			if (!forFunction) {
				FilePrintFormat(output, "; ptrdiff_t %sBytes; ", nameOverride ?: variable->name);
			} else {
				FilePrintFormat(output, ", ptrdiff_t %sBytes", nameOverride ?: variable->name);
			}

			if (!noInitialValue && isBlankString) { FilePrintFormat(output, " = -1"); return true; }
		} else {
			FilePrintFormat(output, "%s %.*s%s%s%s", variable->variable.type, variable->variable.pointer, "********", forFunctionPointer ? "(*" : "", 
					nameOverride ?: variable->name, forFunctionPointer ? ")" : "");

			if (variable->variable.isArray) {
				FilePrintFormat(output, "[%s]", variable->variable.arraySize ?: "");
			}

			char *initialValue = variable->variable.initialValue;

			if (initialValue && !noInitialValue) {
				FilePrintFormat(output, " = %s", initialValue);
				return true;
			}
		}
	} else {
		FilePrintFormat(output, "...");
	}

	return false;
}

void OutputCRecord(Entry *record, int indent) {
	for (int i = 0; i < arrlen(record->children); i++) {
		Entry *entry = record->children + i;
		for (int i = 0; i < indent + 1; i++) FilePrintFormat(output, "\t");

		if (entry->type == ENTRY_VARIABLE) {
			OutputCVariable(entry, true, NULL, false, false);
			FilePrintFormat(output, ";\n");
		} else if (entry->type == ENTRY_UNION) {
			FilePrintFormat(output, "union {\n");
			OutputCRecord(entry, indent + 1);
			for (int i = 0; i < indent + 1; i++) FilePrintFormat(output, "\t");
			FilePrintFormat(output, "};\n\n");
		} else if (entry->type == ENTRY_STRUCT) {
			FilePrintFormat(output, "struct {\n");
			OutputCRecord(entry, indent + 1);
			for (int i = 0; i < indent + 1; i++) FilePrintFormat(output, "\t");
			FilePrintFormat(output, "} %s;\n\n", entry->name ?: "");
		}
	}
}

void OutputCFunction(Entry *entry) {
	if (entry->function.functionPointer) {
		FilePrintFormat(output, "typedef ");

		for (int i = 0; i < arrlen(entry->children); i++) {
			Entry *variable = entry->children + i;
			if (i >= 2) FilePrintFormat(output, ", ");
			OutputCVariable(variable, true, NULL, true, i == 0);
			if (i == 0) FilePrintFormat(output, "(");
		}

		FilePrintFormat(output, ");\n");

		return;
	}

	bool inKernel = entry->function.inKernel;
	if (!inKernel) FilePrintFormat(output, "#ifndef KERNEL\n");
	FilePrintFormat(output, "#ifdef ES_FORWARD\n#ifndef __cplusplus\nES_EXTERN_C ");

	// C code in API.

	for (int i = 0; i < arrlen(entry->children); i++) {
		Entry *variable = entry->children + i;
		if (i >= 2) FilePrintFormat(output, ", ");
		OutputCVariable(variable, true, NULL, true, false);
		if (i == 0) FilePrintFormat(output, "(");
	}

	FilePrintFormat(output, ");\n#else\nES_EXTERN_C ");

	// C++ code in API.

	bool anyDefaultArguments = false;

	for (int i = 0; i < arrlen(entry->children); i++) {
		Entry *variable = entry->children + i;
		if (i >= 2) FilePrintFormat(output, ", ");
		if (OutputCVariable(variable, false, NULL, true, false)) anyDefaultArguments = true;
		if (i == 0) FilePrintFormat(output, "(");
	}

	FilePrintFormat(output, ");\n#endif\n#else\ntypedef ");

	// Code in application.

	Entry *functionVariable = entry->children + 0;
	char *functionName = functionVariable->name;

	for (int i = 0; i < arrlen(entry->children); i++) {
		Entry *variable = entry->children + i;
		if (i >= 2) FilePrintFormat(output, ", ");

		if (i == 0) {
			char name[256];
			sprintf(name, "(*__typeof_%s)", functionName);
			OutputCVariable(variable, true, name, true, false);
			FilePrintFormat(output, "(");
		} else {
			OutputCVariable(variable, true, NULL, true, false);
		}
	}

	FilePrintFormat(output, ");\n#ifndef __cplusplus\n#define %s ((__typeof_%s) ES_API_BASE[%d])\n#else\n", 
			functionName, functionName, entry->function.apiArrayIndex);

	if (anyDefaultArguments) {
		FilePrintFormat(output, "__attribute__((always_inline)) inline \n");

		// C++ code in application with default arguments.

		for (int i = 0; i < arrlen(entry->children); i++) {
			Entry *variable = entry->children + i;
			if (i >= 2) FilePrintFormat(output, ", ");
			OutputCVariable(variable, false, NULL, true, false);
			if (i == 0) FilePrintFormat(output, "(");
		}

		FilePrintFormat(output, ") { \n\t%s((__typeof_%s) ES_API_BASE[%d])(", 
				(functionVariable->variable.pointer == 0 && 0 == strcmp(functionVariable->variable.type, "void")) ? "" : "return ", 
				functionName, entry->function.apiArrayIndex);

		for (int i = 1; i < arrlen(entry->children); i++) {
			Entry *variable = entry->children + i;
			if (i > 1) FilePrintFormat(output, ", ");
			FilePrintFormat(output, "%s", variable->name);

			if (0 == strcmp(variable->variable.type, "STRING")) {
				FilePrintFormat(output, ", %sBytes", variable->name);
			}
		}

		FilePrintFormat(output, "); }");
	} else {
		// C/C++ code in application without default arguments.

		FilePrintFormat(output, "#define %s ((__typeof_%s) ES_API_BASE[%d])", 
				functionName, functionName, entry->function.apiArrayIndex);
	}

	FilePrintFormat(output, "\n#endif\n#endif\n");
	if (!inKernel) FilePrintFormat(output, "#endif\n");
}

void OutputC(Entry *root) {
	{
		size_t bytes;
		char *buffer = LoadFile("desktop/prefix.h", &bytes);
		if (outputDependencies.ready) FilePrintFormat(outputDependencies, "%s\n", "desktop/prefix.h");
		FilePrintFormat(output, "%.*s\n", (int) bytes, buffer);
		free(buffer);
	}

	for (int i = 0; i < arrlen(root->children); i++) {
		Entry *entry = root->children + i;

		if (entry->type == ENTRY_STRUCT) {
			FilePrintFormat(output, "struct %s;\n", entry->name);
		}
	}

	for (int i = 0; i < arrlen(root->children); i++) {
		Entry *entry = root->children + i;

		if (entry->isPrivate) {
			FilePrintFormat(output, "#if defined(ES_PRIVATE_APIS)\n");
		}

		if (entry->type == ENTRY_DEFINE) {
			FilePrintFormat(output, "#define %s (%s)\n", entry->name, entry->define.value);
		} else if (entry->type == ENTRY_STRUCT) {
			FilePrintFormat(output, "typedef struct %s {\n", entry->name);
			OutputCRecord(entry, 0);
			FilePrintFormat(output, "#ifdef %s_MEMBER_FUNCTIONS\n\t%s_MEMBER_FUNCTIONS\n#endif\n", entry->name, entry->name);
			FilePrintFormat(output, "} %s;\n\n", entry->name);
		} else if (entry->type == ENTRY_ENUM) {
			bool isSyscallType = 0 == strcmp(entry->name, "EsSyscallType");
			FilePrintFormat(output, "typedef enum %s {\n", entry->name);

			if (outputEnumStringsArray.ready) {
				FilePrintFormat(outputEnumStringsArray, "static const EnumString enumStrings_%s[] = {\n", entry->name);
			}

			for (int i = 0; i < arrlen(entry->children); i++) {
				if (entry->children[i].define.value) {
					FilePrintFormat(output, "\t%s = %s,\n", entry->children[i].name, entry->children[i].define.value);
				} else {
					FilePrintFormat(output, "\t%s,\n", entry->children[i].name);
				}

				if (isSyscallType && outputSyscallArray.ready) {
					FilePrintFormat(outputSyscallArray, "Do%s,\n", entry->children[i].name);
				}

				if (outputEnumStringsArray.ready) {
					FilePrintFormat(outputEnumStringsArray, "\t{ \"%s\", %s },\n", entry->children[i].name, 
							entry->children[i].define.value ?: "-1");
				}
			}

			if (outputEnumStringsArray.ready) {
				FilePrintFormat(outputEnumStringsArray, "\t{ nullptr, -1 },\n};\n\n");
			}

			FilePrintFormat(output, "} %s;\n\n", entry->name);
		} else if (entry->type == ENTRY_API_TYPE) {
			FilePrintFormat(output, "#ifdef __cplusplus\nstruct %s;\n#else\n#define %s %s\n#endif\n", 
					entry->name, entry->name, 0 == strcmp(entry->apiType.parent, "none") ? "void" : entry->apiType.parent);
		} else if (entry->type == ENTRY_FUNCTION) {
			OutputCFunction(entry);
		} else if (entry->type == ENTRY_TYPE_NAME) {
			FilePrintFormat(output, "typedef %s %s;\n", entry->oldTypeName, entry->name);
		}

		if (entry->isPrivate) {
			FilePrintFormat(output, "#endif\n");
		}
	}

	for (int i = 0; i < arrlen(root->children); i++) {
		Entry *entry = root->children + i;

		if (entry->type == ENTRY_API_TYPE) {
			bool hasParent = 0 != strcmp(entry->apiType.parent, "none");

			if (!hasParent) {
				FilePrintFormat(output, "#ifdef __cplusplus\n#ifndef ES_API\n#ifndef KERNEL\nstruct %s {\n\tvoid *_private;\n", entry->name);
			} else {
				FilePrintFormat(output, "#ifdef __cplusplus\n#ifndef ES_API\n#ifndef KERNEL\nstruct %s : %s {\n", entry->name, entry->apiType.parent);
			}

			FilePrintFormat(output, "};\n#endif\n#endif\n#endif\n");
		}
	}

	FilePrintFormat(output, "#endif\n");
}

char *TrimPrefix(char *in) {
	if (in[0] == 'E' && in[1] == 's' && isupper(in[2])) {
		return in + 2;
	} else if (in[0] == 'E' && in[1] == 'S' && in[2] == '_') {
		return in + 3;
	} else {
		return in;
	}
}

#define REPLACE(x, y) (0 == strcmp(copy, x) || (!exact && strstr(copy, x))) memcpy(strstr(copy, x), y "                              ", strlen(x))

char *OdinReplaceTypes(const char *string, bool exact) {
	char *copy = (char *) malloc(strlen(string) + 1);
	strcpy(copy, string);

	if REPLACE("uint64_t", "u64");
	else if REPLACE("int64_t", "i64");
	else if REPLACE("uint32_t", "u32");
	else if REPLACE("int32_t", "i32");
	else if REPLACE("uint16_t", "u16");
	else if REPLACE("int16_t", "i16");
	else if REPLACE("uint8_t", "u8");
	else if REPLACE("int8_t", "i8");
	else if REPLACE("char", "i8");
	else if REPLACE("intptr_t", "int");
	else if REPLACE("size_t", "int");
	else if REPLACE("ptrdiff_t", "int");
	else if REPLACE("uintptr_t", "uint");
	else if REPLACE("(_EsLongConstant)", "");
	else if REPLACE("unsigned", "u32");
	else if REPLACE("int", "i32");
	else if REPLACE("long", "i64");
	else if REPLACE("double", "f64");
	else if REPLACE("float", "f32");
	else if REPLACE("EsCString", "cstring");

	while REPLACE("Es", "");
	while REPLACE("ES_", "");

	return TrimPrefix(copy);
}

void OutputOdinType(Entry *entry) {
	if (0 == strcmp(entry->variable.type, "void")) {
		assert(entry->variable.pointer);
		entry->variable.type = (char *) "rawptr";
		entry->variable.pointer--;
	}

	FilePrintFormat(output, "%c%s%c%.*s%s", entry->variable.isArray ? '[' : ' ', entry->variable.arraySize ? OdinReplaceTypes(entry->variable.arraySize, true) : "",
			entry->variable.isArray ? ']' : ' ', entry->variable.pointer, "^^^^^^^^^^^^^^^^^", OdinReplaceTypes(entry->variable.type, true));
}

void OutputOdinVariable(Entry *entry, bool expandStrings, const char *nameSuffix) {
	if (!entry->variable.type) {
		FilePrintFormat(output, "_varargs%s : ..any", nameSuffix);
		return;
	}

	if (0 == strcmp(entry->name, "context")) {
		entry->name = (char *) "_context";
	}

	if (0 == strcmp(entry->variable.type, "STRING")) {
		if (expandStrings) {
			FilePrintFormat(output, "%s%s : ^u8, %sBytes%s : int", entry->name, nameSuffix, entry->name, nameSuffix);
		} else {
			FilePrintFormat(output, "%s%s : string", entry->name, nameSuffix);
		}
	} else {
		FilePrintFormat(output, "%s%s : ", 0 == strcmp(entry->name, "in") ? "In" : entry->name, nameSuffix);
		OutputOdinType(entry);
	}
}

void OutputOdinRecord(Entry *record, int indent) {
	for (int i = 0; i < arrlen(record->children); i++) {
		Entry *entry = record->children + i;
		for (int i = 0; i < indent + 1; i++) FilePrintFormat(output, "\t");

		if (entry->type == ENTRY_VARIABLE) {
			OutputOdinVariable(entry, false, "");
			FilePrintFormat(output, ",\n");
		} else if (entry->type == ENTRY_UNION) {
			FilePrintFormat(output, "using _ : struct #raw_union {\n");
			OutputOdinRecord(entry, indent + 1);
			for (int i = 0; i < indent + 1; i++) FilePrintFormat(output, "\t");
			FilePrintFormat(output, "},\n");
		} else if (entry->type == ENTRY_STRUCT) {
			FilePrintFormat(output, "%s : struct {\n", entry->name ?: "using _ ");
			OutputOdinRecord(entry, indent + 1);
			for (int i = 0; i < indent + 1; i++) FilePrintFormat(output, "\t");
			FilePrintFormat(output, "},\n");
		}
	}
}

void OutputOdinFunction(Entry *entry, Entry *root) {
	bool hasReturnValue = strcmp(entry->children[0].variable.type, "void") || entry->children[0].variable.pointer;

	for (int i = 0; i < arrlen(entry->children); i++) {
		if (entry->children[i].variable.type && strstr(entry->children[i].variable.type, "va_list")) {
			return;
		}
	}

	if (entry->function.functionPointer) {
		FilePrintFormat(output, "%s :: distinct #type proc \"c\" (", TrimPrefix(entry->children[0].name));

		for (int i = 1; i < arrlen(entry->children); i++) {
			Entry *variable = entry->children + i;
			if (i >= 2) FilePrintFormat(output, ", ");
			OutputOdinType(variable);
		}

		FilePrintFormat(output, ")");

		if (hasReturnValue) {
			FilePrintFormat(output, " -> ");
			OutputOdinType(entry->children + 0);
		}

		FilePrintFormat(output, ";\n");

		return;
	}

	FilePrintFormat(output, "%s :: #force_inline proc \"c\" (", TrimPrefix(entry->children[0].name));

	for (int i = 1; i < arrlen(entry->children); i++) {
		Entry *variable = entry->children + i;
		if (i >= 2) FilePrintFormat(output, ", ");
		OutputOdinVariable(variable, false, "_");

		if (variable->variable.initialValue) {
			// FilePrintFormat(stderr, "initial value: %s\n", variable->variable.initialValue);

			const char *initialValue = TrimPrefix(variable->variable.initialValue);

			if (0 == strcmp(initialValue, "NULL")) {
				initialValue = "nil";
			} else if (0 == strcmp(initialValue, "DEFAULT_PROPERTIES")) {
				initialValue = "{}";
			} else if (0 == strcmp(initialValue, "BLANK_STRING")) {
				initialValue = "\"\"";
			}

			bool needLeadingDot = false;

			for (int i = 0; i < arrlen(root->children); i++) {
				Entry *entry = root->children + i;

				if (entry->type == ENTRY_ENUM && 0 == strcmp(variable->variable.type, entry->name)) {
					needLeadingDot = true;
					break;
				}
			}

			FilePrintFormat(output, " = %c%s", needLeadingDot ? '.' : ' ', initialValue);
		}
	}

	FilePrintFormat(output, ")");

	if (hasReturnValue) {
		FilePrintFormat(output, " -> ");
		OutputOdinType(entry->children + 0);
	}

	FilePrintFormat(output, "{ addr := 0x1000 + %d * size_of(int); fp := (rawptr(((^uintptr)(uintptr(addr)))^)); ", entry->function.apiArrayIndex);

	if (hasReturnValue) {
		FilePrintFormat(output, "return ");
	}

	FilePrintFormat(output, "((proc \"c\" (");

	for (int i = 1; i < arrlen(entry->children); i++) {
		Entry *variable = entry->children + i;
		if (i >= 2) FilePrintFormat(output, ", ");

		if (variable->variable.type) {
			if (0 == strcmp(variable->variable.type, "STRING")) {
				FilePrintFormat(output, "^u8, int");
			} else {
				OutputOdinType(variable);
			}
		} else {
			FilePrintFormat(output, "..any");
		}
	}

	FilePrintFormat(output, ")");

	if (hasReturnValue) {
		FilePrintFormat(output, " -> ");
		OutputOdinType(entry->children + 0);
	}

	FilePrintFormat(output, ") (fp))(");

	for (int i = 1; i < arrlen(entry->children); i++) {
		Entry *variable = entry->children + i;
		if (i >= 2) FilePrintFormat(output, ", ");

		if (variable->variable.type && 0 == strcmp(variable->variable.type, "STRING")) {
			FilePrintFormat(output, "raw_data(%s_), len(%s_)", variable->name, variable->name);
		} else {
			FilePrintFormat(output, "%s_", variable->name ?: "_varargs");
		}
	}

	FilePrintFormat(output, "); ");

	FilePrintFormat(output, "}\n");
}

void OutputOdin(Entry *root) {
	FilePrintFormat(output, "package es\n");

	FilePrintFormat(output, "Generic :: rawptr;\n");
	FilePrintFormat(output, "INSTANCE_TYPE :: Instance;\n");

	for (int i = 0; i < arrlen(root->children); i++) {
		Entry *entry = root->children + i;
		if (entry->isPrivate) continue;

		if (entry->type == ENTRY_DEFINE) {
			const char *styleCast = strstr(entry->define.value, "STYLE_CAST(");

			if (styleCast) {
				FilePrintFormat(output, "%s :: (^Style)(uintptr(%d));\n", TrimPrefix(entry->name), atoi(styleCast + 11));
			} else {
				const char *name = TrimPrefix(entry->name);
				const char *value = OdinReplaceTypes(entry->define.value, false);

				const char *enumPrefix = NULL;
				char e[64];
				int ep = 0;

				for (uintptr_t i = 0; value[i]; i++) {
					if (value[i] == ' ' || value[i] == '(' || value[i] == ')' 
							|| value[i] == '\t' || ep == sizeof(e) - 1) {
						// Ignore.
					} else {
						e[ep++] = value[i];
						e[ep] = 0;
					}
				}

				for (int i = 0; i < arrlen(root->children); i++) {
					if (root->children[i].type == ENTRY_ENUM) {
						for (int j = 0; j < arrlen(root->children[i].children); j++) {
							const char *enumName = TrimPrefix(root->children[i].children[j].name);

							if (0 == strcmp(enumName, e)) {
								enumPrefix = TrimPrefix(root->children[i].name);
								value = enumName;
								goto gotEnumPrefix;
							}
						}
					}
				}

				gotEnumPrefix:;
				FilePrintFormat(output, "%s :: %s%s%s;\n", name, enumPrefix ? enumPrefix : "", enumPrefix ? "." : "", value);
			}
		} else if (entry->type == ENTRY_STRUCT) {
			FilePrintFormat(output, "%s :: struct {\n", TrimPrefix(entry->name));
			OutputOdinRecord(entry, 0);
			FilePrintFormat(output, "}\n");
		} else if (entry->type == ENTRY_ENUM) {
			FilePrintFormat(output, "%s :: enum i32 {\n", TrimPrefix(entry->name));

			for (int i = 0; i < arrlen(entry->children); i++) {
				if (entry->children[i].define.value) {
					FilePrintFormat(output, "\t%s = %s,\n", TrimPrefix(entry->children[i].name), entry->children[i].define.value);
				} else {
					FilePrintFormat(output, "\t%s,\n", TrimPrefix(entry->children[i].name));
				}
			}

			FilePrintFormat(output, "}\n");
		} else if (entry->type == ENTRY_API_TYPE) {
			bool hasParent = 0 != strcmp(entry->apiType.parent, "none");
			FilePrintFormat(output, "%s :: #type %s;\n", TrimPrefix(entry->name), hasParent ? TrimPrefix(entry->apiType.parent) : "rawptr");
		} else if (entry->type == ENTRY_FUNCTION) {
			OutputOdinFunction(entry, root);
		} else if (entry->type == ENTRY_TYPE_NAME) {
			FilePrintFormat(output, "%s :: #type %s;\n", TrimPrefix(entry->name), TrimPrefix(OdinReplaceTypes(entry->oldTypeName, true)));
		}
	}
}

char *ZigReplaceTypes(const char *string, bool exact) {
	char *copy = (char *) malloc(strlen(string) + 1);
	strcpy(copy, string);

	if REPLACE("uint64_t", "u64");
	else if REPLACE("int64_t", "i64");
	else if REPLACE("uint32_t", "u32");
	else if REPLACE("int32_t", "i32");
	else if REPLACE("uint16_t", "u16");
	else if REPLACE("int16_t", "i16");
	else if REPLACE("uint8_t", "u8");
	else if REPLACE("int8_t", "i8");
	else if REPLACE("char", "i8");
	else if REPLACE("intptr_t", "isize");
	else if REPLACE("size_t", "usize");
	else if REPLACE("ptrdiff_t", "isize");
	else if REPLACE("uintptr_t", "usize");
	else if REPLACE("(_EsLongConstant)", "");
	else if REPLACE("unsigned", "u32");
	else if REPLACE("int", "i32");
	else if REPLACE("long", "i64");
	else if REPLACE("double", "f64");
	else if REPLACE("float", "f32");
	else if REPLACE("EsCString", "?*u8");

	while REPLACE("Es", "");
	while REPLACE("ES_", "");
	while REPLACE("\t", " ");

	return TrimPrefix(copy);
}

char *ZigRemoveTabs(const char *string) {
	bool exact = false;
	char *copy = (char *) malloc(strlen(string) + 1);
	strcpy(copy, string);
	while REPLACE("\t", " ");
	return copy;
}

void OutputZigType(Entry *entry) {
	if (0 == strcmp(entry->variable.type, "void") && entry->variable.pointer) {
		entry->variable.type = (char *) "u8";
	} else if (0 == strcmp(entry->variable.type, "EsStyle")) {
		assert(entry->variable.pointer);
		entry->variable.type = (char *) "usize";
		entry->variable.pointer--;
	}

	FilePrintFormat(output, "%c%.*s%s%s", 
			entry->variable.pointer ? '?' : ' ', 
			entry->variable.pointer, "***************", 
			entry->variable.isConst && entry->variable.pointer ? "const " : "",
			ZigReplaceTypes(entry->variable.type, true));
}

void OutputZigVariable(Entry *entry, bool expandStrings, const char *nameSuffix) {
	(void) expandStrings;

	if (0 == strcmp(entry->name, "error")) {
		entry->name[3] = ' ';
		entry->name[4] = ' ';
	}

	FilePrintFormat(output, "%s%s : ", entry->name, nameSuffix);
	OutputZigType(entry);
}

void OutputZigRecord(Entry *record, int indent, bool inUnion) {
	for (int i = 0; i < arrlen(record->children); i++) {
		Entry *entry = record->children + i;
		for (int i = 0; i < indent + 1; i++) FilePrintFormat(output, "    ");

		if (entry->type == ENTRY_VARIABLE) {
			OutputZigVariable(entry, false, "");

			if (!inUnion) {
				if (entry->variable.pointer) {
					FilePrintFormat(output, "= null");
				} else if (0 == strcmp(entry->variable.type, "uint64_t")
						|| 0 == strcmp(entry->variable.type, "int64_t")
						|| 0 == strcmp(entry->variable.type, "uint32_t")
						|| 0 == strcmp(entry->variable.type, "int32_t")
						|| 0 == strcmp(entry->variable.type, "uint16_t")
						|| 0 == strcmp(entry->variable.type, "int16_t")
						|| 0 == strcmp(entry->variable.type, "uint8_t")
						|| 0 == strcmp(entry->variable.type, "int8_t")
						|| 0 == strcmp(entry->variable.type, "char")
						|| 0 == strcmp(entry->variable.type, "intptr_t")
						|| 0 == strcmp(entry->variable.type, "size_t")
						|| 0 == strcmp(entry->variable.type, "ptrdiff_t")
						|| 0 == strcmp(entry->variable.type, "uintptr_t")
						|| 0 == strcmp(entry->variable.type, "unsigned")
						|| 0 == strcmp(entry->variable.type, "int")
						|| 0 == strcmp(entry->variable.type, "long")
						|| 0 == strcmp(entry->variable.type, "double")
						|| 0 == strcmp(entry->variable.type, "float")) {
					FilePrintFormat(output, "= 0");
				} else if (0 == strcmp(entry->variable.type, "bool")) {
					FilePrintFormat(output, "= false");
				} else {
					FilePrintFormat(output, "= %s {}", TrimPrefix(entry->variable.type));
				}
			}
			
			FilePrintFormat(output, ",\n");
		} else if (entry->type == ENTRY_UNION) {
			FilePrintFormat(output, "_unnamed : extern union {\n");
			OutputZigRecord(entry, indent + 1, true);
			for (int i = 0; i < indent + 1; i++) FilePrintFormat(output, "    ");
			FilePrintFormat(output, "},\n");
		} else if (entry->type == ENTRY_STRUCT) {
			FilePrintFormat(output, "%s : extern struct {\n", entry->name ?: "_unnamed ");
			OutputZigRecord(entry, indent + 1, false);
			for (int i = 0; i < indent + 1; i++) FilePrintFormat(output, "    ");
			FilePrintFormat(output, "},\n");
		}
	}
}

void OutputZigFunction(Entry *entry) {
	for (int i = 0; i < arrlen(entry->children); i++) {
		if (!entry->children[i].variable.type || strstr(entry->children[i].variable.type, "va_list")) {
			return;
		}
	}

	if (entry->function.functionPointer) {
		FilePrintFormat(output, "const %s = fn (", TrimPrefix(entry->children[0].name));

		for (int i = 1; i < arrlen(entry->children); i++) {
			Entry *variable = entry->children + i;
			if (i >= 2) FilePrintFormat(output, ", ");
			OutputZigType(variable);
		}

		FilePrintFormat(output, ") callconv(.C)");

		OutputZigType(entry->children + 0);

		FilePrintFormat(output, ";\n");

		return;
	}

	bool hasReturnValue = strcmp(entry->children[0].variable.type, "void") || entry->children[0].variable.pointer;

	FilePrintFormat(output, "pub fn %s(", TrimPrefix(entry->children[0].name));

	for (int i = 1; i < arrlen(entry->children); i++) {
		Entry *variable = entry->children + i;
		if (i >= 2) FilePrintFormat(output, ", ");
		OutputZigVariable(variable, false, "_");
	}

	FilePrintFormat(output, ") ");
	OutputZigType(entry->children + 0);
	FilePrintFormat(output, "{ ");

	if (hasReturnValue) {
		FilePrintFormat(output, "return ");
	}

	FilePrintFormat(output, "((@intToPtr(*fn (");

	for (int i = 1; i < arrlen(entry->children); i++) {
		Entry *variable = entry->children + i;
		if (i >= 2) FilePrintFormat(output, ", ");
		OutputZigVariable(variable, false, "_");
	}

	FilePrintFormat(output, ") callconv(.C) ");
	OutputZigType(entry->children + 0);
	FilePrintFormat(output, ", 0x1000 + %d * 8)).*)(", entry->function.apiArrayIndex);

	for (int i = 1; i < arrlen(entry->children); i++) {
		Entry *variable = entry->children + i;
		if (i >= 2) FilePrintFormat(output, ", ");
		FilePrintFormat(output, "%s_", variable->name ?: "_varargs");
	}

	FilePrintFormat(output, "); }\n");
}

void OutputZig(Entry *root) {
	FilePrintFormat(output, "pub const Generic = ?*u8;\n");
	FilePrintFormat(output, "const INSTANCE_TYPE = Instance;\n");
	FilePrintFormat(output, "pub const STRING = extern struct { ptr : [*] const u8, len : usize, };\n");
	FilePrintFormat(output, "pub fn Str(x : [] const u8) STRING { return STRING { .ptr = x.ptr, .len = x.len }; }\n");
	FilePrintFormat(output, "fn STYLE_CAST (x : isize) isize { return x; }\n");

	for (int i = 0; i < arrlen(root->children); i++) {
		Entry *entry = root->children + i;
		if (entry->isPrivate) continue;

		if (entry->type == ENTRY_DEFINE) {
			FilePrintFormat(output, "pub const %s = %s;\n", TrimPrefix(entry->name), ZigReplaceTypes(entry->define.value, false));
		} else if (entry->type == ENTRY_STRUCT) {
			FilePrintFormat(output, "pub const %s = extern struct {\n", TrimPrefix(entry->name));
			OutputZigRecord(entry, 0, false);
			FilePrintFormat(output, "};\n");
		} else if (entry->type == ENTRY_ENUM) {
			FilePrintFormat(output, "pub const %s = extern enum {\n", TrimPrefix(entry->name));

			for (int i = 0; i < arrlen(entry->children); i++) {
				if (entry->children[i].define.value) {
					FilePrintFormat(output, "    %s = %s,\n", TrimPrefix(entry->children[i].name), ZigRemoveTabs(entry->children[i].define.value));
				} else {
					FilePrintFormat(output, "    %s,\n", TrimPrefix(entry->children[i].name));
				}
			}

			FilePrintFormat(output, "};\n");
		} else if (entry->type == ENTRY_API_TYPE) {
			bool hasParent = 0 != strcmp(entry->apiType.parent, "none");
			FilePrintFormat(output, "pub const %s = %s;\n", TrimPrefix(entry->name), hasParent ? TrimPrefix(entry->apiType.parent) : "?*u8");
		} else if (entry->type == ENTRY_FUNCTION) {
			OutputZigFunction(entry);
		} else if (entry->type == ENTRY_TYPE_NAME) {
			FilePrintFormat(output, "pub const %s = %s;\n", TrimPrefix(entry->name), TrimPrefix(ZigReplaceTypes(entry->oldTypeName, true)));
		}
	}
}

int HeaderGeneratorMain(int argc, char **argv) {
	if (argc < 2) {
		outputDependencies = FileOpen(DEST_DEPENDENCIES ".tmp", 'w');
		FilePrintFormat(outputDependencies, ": \n");
	}

	Entry root = {};
	ParseFile(&root, "desktop/os.header");

	const char *language = "c";

	if (argc == 3) {
		language = argv[1];
		output = FileOpen(argv[2], 'w');

		if (0 == strcmp(argv[1], "system")) {
			outputAPIArray = FileOpen(DEST_API_ARRAY, 'w');
			outputSyscallArray = FileOpen(DEST_SYSCALL_ARRAY, 'w');
			outputEnumStringsArray = FileOpen(DEST_ENUM_STRINGS_ARRAY, 'w');
		}
	} else {
		Log("Usage: %s <language> <path-to-output-file>\n", argv[0]);
		return 1;
	}

	{
		{
			EsINIState s = { (char *) LoadFile("util/api_table.ini", &s.bytes) };

			int32_t highestIndex = -1;

			struct {
				const char *cName;
				int32_t index;
			} *entries = NULL;

			while (EsINIParse(&s)) {
				EsINIZeroTerminate(&s);
				int32_t index = atoi(s.value);
				if (index < 0 || !s.key[0]) goto done;
				if (index > highestIndex) highestIndex = index;
				arraddn(entries, 1);
				char *copy = (char *) malloc(strlen(s.key) + 1);
				arrlast(entries).cName = copy;
				strcpy(copy, s.key);
				arrlast(entries).index = index;
			}

			done:;

			arrsetlen(apiTableEntries, (size_t) (highestIndex + 1));

			for (int i = 0; i < highestIndex + 1; i++) {
				apiTableEntries[i] = "EsUnimplemented";
			}

			for (int i = 0; i < arrlen(entries); i++) {
				apiTableEntries[entries[i].index] = entries[i].cName;
			}

			arrfree(entries);
		}

		for (int i = 0; i < arrlen(root.children); i++) {
			Entry *entry = root.children + i;
			if (entry->type != ENTRY_FUNCTION || entry->function.functionPointer) continue;
			const char *name = entry->children[0].name;
			bool found = false;

			for (int i = 0; i < arrlen(apiTableEntries); i++) {
				if (0 == strcmp(apiTableEntries[i], name)) {
					entry->function.apiArrayIndex = i;
					found = true;
					break;
				}
			}

			if (!found) {
				for (int i = 0; i < arrlen(apiTableEntries); i++) {
					if (0 == strcmp(apiTableEntries[i], "EsUnimplemented")) {
						apiTableEntries[i] = name;
						entry->function.apiArrayIndex = i;
						found = true;
						break;
					}
				}

				if (!found) {
					entry->function.apiArrayIndex = arrlen(apiTableEntries);
					arrput(apiTableEntries, name);
				}
			}
		}

		for (int i = 0; i < arrlen(apiTableEntries); i++) {
			apiTableEntries[i] = "EsUnimplemented";
		}

		for (int i = 0; i < arrlen(root.children); i++) {
			Entry *entry = root.children + i;
			if (entry->type != ENTRY_FUNCTION || entry->function.functionPointer) continue;
			const char *name = entry->children[0].name;
			apiTableEntries[entry->function.apiArrayIndex] = name;
		}

		if (outputAPIArray.ready) {
			File f = FileOpen("util/api_table.ini", 'w');

			for (int i = 0; i < arrlen(apiTableEntries); i++) {
				FilePrintFormat(outputAPIArray, "(void *) %s,\n", apiTableEntries[i]);

				if (strcmp(apiTableEntries[i], "EsUnimplemented")) {
					FilePrintFormat(f, "%s=%d\n", apiTableEntries[i], i);
				}
			}

			FileClose(f);
		}
	}

	if (0 == strcmp(language, "c") || 0 == strcmp(language, "system")) {
		OutputC(&root);
	} else if (0 == strcmp(language, "odin")) {
		OutputOdin(&root);
	} else if (0 == strcmp(language, "zig")) {
		OutputZig(&root);
	} else {
		Log("Unsupported language '%s'.\nLanguage must be one of: 'c', 'odin'.\n", language);
	}

	if (argc < 2) {
		rename(DEST_DEPENDENCIES ".tmp", DEST_DEPENDENCIES);
	}

	if (outputDependencies.ready) FileClose(outputDependencies);
	if (output.ready) FileClose(output);
	if (outputAPIArray.ready) FileClose(outputAPIArray);
	if (outputSyscallArray.ready) FileClose(outputSyscallArray);
	if (outputEnumStringsArray.ready) FileClose(outputEnumStringsArray);

	return 0;
}
