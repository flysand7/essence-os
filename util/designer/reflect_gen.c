#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STB_DS_IMPLEMENTATION
#include "../stb_ds.h"

#define TOKEN_BLOCK		(0)
#define TOKEN_LEFT_BRACE 	(1)
#define TOKEN_RIGHT_BRACE 	(2)
#define TOKEN_LEFT_PAREN 	(3)
#define TOKEN_RIGHT_PAREN 	(4)
#define TOKEN_SEMICOLON 	(5)
#define TOKEN_IDENTIFIER 	(6)
#define TOKEN_STRING 		(7)
#define TOKEN_EOF 		(8)
#define TOKEN_COMMA 		(9)
#define TOKEN_NUMBER 		(10)
#define TOKEN_HASH 		(11)
#define TOKEN_QUESTION 		(12)
#define TOKEN_EQUALS		(13)
#define TOKEN_LEFT_BRACKET 	(14)
#define TOKEN_RIGHT_BRACKET 	(15)
#define TOKEN_COLON 		(16)
#define TOKEN_EXCLAMATION	(17)

typedef struct Parse {
	const char *position;
	int line;
	bool success;
} Parse;

typedef struct Token {
	int type;
	double number;
	char *string;
} Token;

typedef struct ParsedField {
	char *rfType, *cTypeBefore, *fieldName, *cTypeAfter;
	int firstVersion, lastVersion;
	char **flagsInclude, **flagsExclude;
	char *optionsType, *optionsBlock;
} ParsedField;

typedef struct ParsedType {
	bool isStruct, isUnion, isCustom, isEnum;
	char *cName, *rfName, *opFunction;
	ParsedField *fields;
} ParsedType;

ParsedType *parsedTypes;

bool IsDigit(char c) { return (c >= '0' && c <= '9'); }
bool IsAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool IsAlnum(char c) { return (IsDigit(c) || IsAlpha(c)); }

bool _NextToken(Parse *parse, Token *token) {
	if (!parse->success) return false;
	
	while (true) {
		char c = *parse->position;
		parse->position++;
		
		if (c == ' ' || c == '\t' || c == '\r') {
			continue;
		} else if (c == '\n') {
			parse->line++;
			continue;
		} else if (c == '/' && *parse->position == '/') {
			while (*parse->position != '\n') parse->position++;
			continue;
		} else if (c == '/' && *parse->position == '*') {
			while (parse->position[0] != '*' || parse->position[1] != '/') parse->position++;
			parse->position += 2;
			continue;
#define PARSE_CHARACTER(a, b) \
		} else if (c == a) { \
			Token t = {}; \
			t.type = b; \
			*token = t; \
			return true
		PARSE_CHARACTER('{', TOKEN_LEFT_BRACE );
		PARSE_CHARACTER('}', TOKEN_RIGHT_BRACE );
		PARSE_CHARACTER('(', TOKEN_LEFT_PAREN );
		PARSE_CHARACTER(')', TOKEN_RIGHT_PAREN );
		PARSE_CHARACTER('[', TOKEN_LEFT_BRACKET );
		PARSE_CHARACTER(']', TOKEN_RIGHT_BRACKET );
		PARSE_CHARACTER(';', TOKEN_SEMICOLON );
		PARSE_CHARACTER(':', TOKEN_COLON );
		PARSE_CHARACTER(',', TOKEN_COMMA );
		PARSE_CHARACTER('#', TOKEN_HASH );
		PARSE_CHARACTER('?', TOKEN_QUESTION );
		PARSE_CHARACTER('=', TOKEN_EQUALS );
		PARSE_CHARACTER('!', TOKEN_EXCLAMATION );
		} else if (IsAlpha(c) || c == '_') {
			const char *start = parse->position - 1;
			
			while (true) {
				char c2 = *parse->position;
				if (c2 == 0) break;
				if (!IsAlnum(c2) && c2 != '_') break;
				parse->position++;
			}
			
			Token _token = { 0 };
			_token.type = TOKEN_IDENTIFIER;
			_token.string = malloc(parse->position - start + 1);
			if (!_token.string) { parse->success = false; return false; }
			memcpy(_token.string, start, parse->position - start);
			_token.string[parse->position - start] = 0;
			*token = _token;
			return true;
		} else if (IsDigit(c) || c == '-') {
			const char *start = parse->position - 1;
			
			while (true) {
				char c2 = *parse->position;
				if (c2 == 0) break;
				if (!IsAlnum(c2) && c2 != '.') break;
				parse->position++;
			}
			
			Token _token = { 0 };
			_token.type = TOKEN_NUMBER;
			_token.string = malloc(parse->position - start + 1);
			if (!_token.string) { parse->success = false; return false; }
			memcpy(_token.string, start, parse->position - start);
			_token.string[parse->position - start] = 0;
			
			bool negate = _token.string[0] == '-';
			bool afterDot = false;
			bool hexadecimal = false;
			double fraction = 0.1;
			
			for (uintptr_t i = negate ? 1 : 0; _token.string[i]; i++) {
				char c = _token.string[i];
				
				if (c == '.') {
					if (hexadecimal) {
						parse->success = false; 
						return false;
					}

					afterDot = true;
				} else if (c == 'x') {
					if (i != 1 || _token.string[0] != '0' || afterDot) {
						parse->success = false; 
						return false;
					}

					hexadecimal = true;
				} else if (afterDot) {
					if (!IsDigit(c)) {
						parse->success = false; 
						return false;
					}

					_token.number += (c - '0') * fraction;
					fraction *= 0.1;
				} else {
					if (hexadecimal) {
						_token.number *= 16;

						if (c >= '0' && c <= '9') {
							_token.number += c - '0';
						} else if (c >= 'a' && c <= 'f') {
							_token.number += c - 'a' + 10;
						} else if (c >= 'A' && c <= 'F') {
							_token.number += c - 'A' + 10;
						} else {
							parse->success = false; 
							return false;
						}
					} else {
						if (!IsDigit(c)) {
							parse->success = false; 
							return false;
						}

						_token.number *= 10;
						_token.number += c - '0';
					}
				}
				
			}
			
			if (negate) _token.number = -_token.number;
			*token = _token;
			return true;
		} else if (c == '"') {
			const char *start = parse->position;
			
			while (true) {
				char c2 = *parse->position;
				if (c2 == 0) break;
				parse->position++;
				if (c2 == '"') break;
			}
			
			Token _token = { 0 };
			_token.type = TOKEN_STRING;
			_token.string = (char *) malloc(parse->position - start);
			if (!_token.string) { parse->success = false; return false; }
			memcpy(_token.string, start, parse->position - start - 1);
			_token.string[parse->position - start - 1] = 0;
			*token = _token;
			return true;
		} else if (c == 0) {
			Token t = {};
			t.type = TOKEN_EOF;
			*token = t;
			return true;
		} else {
			parse->success = false;
			return false;
		}
	}
}

Token NextToken(Parse *parse) {
	Token token = { 0 };
	bool success = _NextToken(parse, &token);

	if (!success) {
		fprintf(stderr, "error: invalid token on line %d\n", parse->line);
		exit(1);
	}

	return token;
}

char *NextString(Parse *parse) {
	Token token = NextToken(parse);

	if (token.type == TOKEN_IDENTIFIER || token.type == TOKEN_STRING) {
		return token.string;
	} else {
		fprintf(stderr, "error: expected string or identifier on line %d\n", parse->line);
		exit(1);
		return NULL;
	}
}

Token ExpectToken(Parse *parse, int type) {
	Token token = NextToken(parse);

	if (token.type == type) {
		return token;
	} else {
		fprintf(stderr, "error: expected token of type %d of line %d\n", type, parse->line);
		exit(1);
		return (Token) { 0 };
	}
}

char *NextBlock(Parse *parse) {
	ExpectToken(parse, TOKEN_LEFT_BRACE);

	int depth = 1;
	const char *start = parse->position;

	while (depth) {
		if (*parse->position == '{') {
			depth++;
			parse->position++;
		} else if (*parse->position == '}') {
			depth--;
			parse->position++;
		} else if (*parse->position == '"') {
			ExpectToken(parse, TOKEN_STRING);
		} else if (*parse->position == 0) {
			fprintf(stderr, "error: unexpected end of file during block\n");
			exit(1);
		} else {
			parse->position++;
		}
	}

	char *result = malloc(parse->position - start);
	memcpy(result, start, parse->position - start - 1);
	result[parse->position - start - 1] = 0;
	return result;
}

Token PeekToken(Parse *parse) {
	Parse old = *parse;
	Token token = NextToken(parse);
	*parse = old;
	return token;
}

char *LoadFile(const char *inputFileName, size_t *byteCount) {
	FILE *inputFile = fopen(inputFileName, "rb");
	
	if (!inputFile) {
		return NULL;
	}
	
	fseek(inputFile, 0, SEEK_END);
	size_t inputFileBytes = ftell(inputFile);
	fseek(inputFile, 0, SEEK_SET);
	
	char *inputBuffer = (char *) malloc(inputFileBytes + 1);
	size_t inputBytesRead = fread(inputBuffer, 1, inputFileBytes, inputFile);
	inputBuffer[inputBytesRead] = 0;
	fclose(inputFile);
	
	if (byteCount) *byteCount = inputBytesRead;
	return inputBuffer;
}

void ParseAdditionalFieldInformation(Parse *parse, ParsedField *field) {
	while (PeekToken(parse).type != TOKEN_SEMICOLON) {
		Token token = NextToken(parse);

		if (token.type == TOKEN_IDENTIFIER && 0 == strcmp(token.string, "from") && !field->firstVersion) {
			field->firstVersion = ExpectToken(parse, TOKEN_NUMBER).number;
		} else if (token.type == TOKEN_IDENTIFIER && 0 == strcmp(token.string, "to") && !field->lastVersion) {
			field->lastVersion = ExpectToken(parse, TOKEN_NUMBER).number;
		} else if (token.type == TOKEN_IDENTIFIER && 0 == strcmp(token.string, "if") && !field->flagsInclude && !field->flagsExclude) {
			ExpectToken(parse, TOKEN_LEFT_PAREN);

			while (PeekToken(parse).type != TOKEN_RIGHT_PAREN) {
				if (PeekToken(parse).type == TOKEN_EXCLAMATION) {
					ExpectToken(parse, TOKEN_EXCLAMATION);
					arrput(field->flagsExclude, NextString(parse));
				} else {
					arrput(field->flagsInclude, NextString(parse));
				}
			}

			ExpectToken(parse, TOKEN_RIGHT_PAREN);
		} else if (token.type == TOKEN_HASH) {
			field->optionsType = NextString(parse);
			field->optionsBlock = NextBlock(parse);
		} else {
			fprintf(stderr, "error: unexpected token in field on line %d\n", parse->line);
			exit(1);
		}
	}
}

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "usage: reflect_gen <input file>\n");
		return 1;
	}

	char *input = LoadFile(argv[1], NULL);

	if (!input) {
		fprintf(stderr, "error: could not open input file '%s'\n", argv[1]);
		return 1;
	}

	Parse parse = { 0 };
	parse.position = input;
	parse.line = 1;
	parse.success = true;

	while (true) {
		Token token = NextToken(&parse);

		if (token.type == TOKEN_EOF) {
			break;
		}

		if (token.type == TOKEN_IDENTIFIER && (0 == strcmp(token.string, "struct") || 0 == strcmp(token.string, "union"))) {
			ParsedType type = { 0 };
			type.isStruct = 0 == strcmp(token.string, "struct");
			type.isUnion = 0 == strcmp(token.string, "union");
			type.cName = NextString(&parse);
			type.rfName = NextString(&parse);
			type.opFunction = NextString(&parse);
			ExpectToken(&parse, TOKEN_LEFT_BRACE);

			while (PeekToken(&parse).type != TOKEN_RIGHT_BRACE) {
				ParsedField field = { 0 };
				field.rfType = NextString(&parse);
				field.cTypeBefore = NextString(&parse);
				field.fieldName = NextString(&parse);

				if (PeekToken(&parse).type == TOKEN_STRING) {
					field.cTypeAfter = NextString(&parse);
				}

				ParseAdditionalFieldInformation(&parse, &field);
				ExpectToken(&parse, TOKEN_SEMICOLON);
				arrput(type.fields, field);
			}

			ExpectToken(&parse, TOKEN_RIGHT_BRACE);
			ExpectToken(&parse, TOKEN_SEMICOLON);
			arrput(parsedTypes, type);
		} else if (token.type == TOKEN_IDENTIFIER && 0 == strcmp(token.string, "type")) {
			ParsedType type = { 0 };
			type.isCustom = true;
			type.rfName = NextString(&parse);
			type.opFunction = NextString(&parse);
			ExpectToken(&parse, TOKEN_SEMICOLON);
			arrput(parsedTypes, type);
		} else if (token.type == TOKEN_IDENTIFIER && 0 == strcmp(token.string, "enum")) {
			ParsedType type = { 0 };
			type.isEnum = true;
			type.rfName = NextString(&parse);
			type.opFunction = NextString(&parse);
			ExpectToken(&parse, TOKEN_LEFT_BRACE);

			while (PeekToken(&parse).type != TOKEN_RIGHT_BRACE) {
				ParsedField field = { 0 };
				field.fieldName = NextString(&parse);
				ParseAdditionalFieldInformation(&parse, &field);
				ExpectToken(&parse, TOKEN_SEMICOLON);
				arrput(type.fields, field);
			}

			ExpectToken(&parse, TOKEN_RIGHT_BRACE);
			ExpectToken(&parse, TOKEN_SEMICOLON);
			arrput(parsedTypes, type);
		} else {
			fprintf(stderr, "error: unexpected token at root on line %d\n", parse.line);
			exit(1);
		}
	}

	// Output C type declarations.

	for (uintptr_t i = 0; i < arrlenu(parsedTypes); i++) {
		ParsedType *type = parsedTypes + i;

		if (type->isCustom) {
			continue;
		} else if (type->isEnum) {
			for (uintptr_t j = 0; j < arrlenu(type->fields); j++) {
				ParsedField *field = type->fields + j;
				printf("#define %s (%d)\n", field->fieldName, (int) j);
			}

			printf("\n");
			continue;
		}

		printf("typedef struct %s {\n", type->cName);
		const char *indent = "\t";

		if (type->isUnion) {
			printf("\tuint32_t tag;\n\n\tunion {\n");
			indent = "\t\t";
		}

		for (uintptr_t j = 0; j < arrlenu(type->fields); j++) {
			ParsedField *field = type->fields + j;
			printf("%s%s %s %s;\n", indent, field->cTypeBefore, field->fieldName, field->cTypeAfter ? field->cTypeAfter : "");
		}

		if (type->isUnion) {
			printf("\t};\n");
		}

		printf("} %s;\n\n", type->cName);
	}

	// Forward-declare op functions.

	for (uintptr_t i = 0; i < arrlenu(parsedTypes); i++) {
		ParsedType *type = parsedTypes + i;
		printf("void %s(RfState *state, RfItem *item, void *pointer);\n", type->opFunction);
	}

	// Output reflect type information.
	
	for (uintptr_t i = 0; i < arrlenu(parsedTypes); i++) {
		ParsedType *type = parsedTypes + i;

		printf("#ifdef REFLECT_IMPLEMENTATION\n");
		printf("RfType %s = {\n", type->rfName);
		printf("\t.op = %s,\n\t.cName = \"%s\",\n\t.fieldCount = %d,\n", type->opFunction, type->cName, (int) arrlen(type->fields));

		if (arrlenu(type->fields)) {
			printf("\n\t.fields = (RfField []) {\n");

			for (uintptr_t j = 0; j < arrlenu(type->fields); j++) {
				ParsedField *field = type->fields + j;

				if (type->isEnum) {
					printf("\t\t{ .cName = \"%s\", ", field->fieldName);
				} else {
					printf("\t\t{ .item.type = &%s, .item.byteCount = RF_SIZE_OF(%s, %s), .cName = \"%s\", .offset = offsetof(%s, %s), ",
						field->rfType, type->cName, field->fieldName, field->fieldName, type->cName, field->fieldName);
				}

				printf(".firstVersion = %d, .lastVersion = %d, .flagsInclude = 0", field->firstVersion, field->lastVersion);

				for (uintptr_t k = 0; k < arrlenu(field->flagsInclude); k++) {
					printf(" | %s", field->flagsInclude[k]);
				}

				printf(", .flagsExclude = 0");

				for (uintptr_t k = 0; k < arrlenu(field->flagsExclude); k++) {
					printf(" | %s", field->flagsExclude[k]);
				}

				if (field->optionsType) {
					printf(", .item.options = &(%s) { %s }", field->optionsType, field->optionsBlock);
				}

				printf(" },\n");
			}

			printf("\t},\n");
		}

		printf("};\n\n");
		printf("#else\nextern RfType %s;\n#endif\n", type->rfName);

		if (!type->isEnum) {
			for (uintptr_t j = 0; j < arrlenu(type->fields); j++) {
				ParsedField *field = type->fields + j;

				printf("#define %s_%s (%d)\n", type->cName, field->fieldName, (int) j);
			}
		}
	}

	return 0;
}
