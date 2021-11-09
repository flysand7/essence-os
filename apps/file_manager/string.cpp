// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

struct String {
	char *text;
	size_t bytes, allocated;
};

String StringAllocateAndFormat(const char *format, ...) {
	String string = {};
	va_list arguments;
	va_start(arguments, format);
	string.text = EsStringAllocateAndFormatV(&string.bytes, format, arguments);
	va_end(arguments);
	string.allocated = string.bytes;
	return string;
}

String StringFromLiteral(const char *literal) {
	String string = {};
	string.text = (char *) literal;
	string.bytes = EsCStringLength(literal);
	return string;
}

String StringFromLiteralWithSize(const char *literal, ptrdiff_t bytes) {
	String string = {};
	string.text = (char *) literal;
	string.bytes = bytes == -1 ? EsCStringLength(literal) : bytes;
	return string;
}

void StringAppend(String *string, String with) {
	if (string->bytes + with.bytes > string->allocated) {
		string->allocated = (string->allocated + with.bytes) * 2;
		string->text = (char *) EsHeapReallocate(string->text, string->allocated, false);
	}

	EsMemoryCopy(string->text + string->bytes, with.text, with.bytes);
	string->bytes += with.bytes;
}

void StringDestroy(String *string) {
	EsAssert(string->allocated == string->bytes); // Attempting to free a partial string.
	EsHeapFree(string->text);
	string->text = nullptr;
	string->bytes = string->allocated = 0;
}

String StringDuplicate(String string) {
	String result = {};
	result.bytes = result.allocated = string.bytes;
	result.text = (char *) EsHeapAllocate(result.bytes + 1, false);
	result.text[result.bytes] = 0;
	EsMemoryCopy(result.text, string.text, result.bytes);
	return result;
}

inline bool StringStartsWith(String a, String b) {
	return a.bytes >= b.bytes && 0 == EsMemoryCompare(a.text, b.text, b.bytes);
}

inline bool StringEndsWith(String a, String b) {
	return a.bytes >= b.bytes && 0 == EsMemoryCompare(a.text + a.bytes - b.bytes, b.text, b.bytes);
}

inline bool StringEquals(String a, String b) {
	return a.bytes == b.bytes && 0 == EsMemoryCompare(a.text, b.text, a.bytes);
}

String StringSlice(String string, uintptr_t offset, ptrdiff_t length) {
	if (length == -1) {
		length = string.bytes - offset;
	}

	string.text += offset;
	string.bytes = length;
	string.allocated = 0;
	return string;
}

#define STRING(x) x.text, x.bytes
#define STRFMT(x) x.bytes, x.text

uintptr_t PathCountSections(String string) {
	ptrdiff_t sectionCount = 0;

	for (uintptr_t i = 0; i < string.bytes; i++) {
		if (string.text[i] == '/') {
			sectionCount++;
		}
	}

	return sectionCount;
}

String PathGetSection(String string, ptrdiff_t index) {
	String output = {};
	size_t stringBytes = string.bytes;
	char *text = string.text;

	if (index < 0) {
		index += PathCountSections(string);
	}

	if (index < 0) {
		return output;
	}

	uintptr_t i = 0, bytes = 0;

	for (; index && i < stringBytes; i++) {
		if (text[i] == '/') {
			index--;
		}
	}

	if (index) {
		return output;
	}

	output.text = text + i;

	for (; i < stringBytes; i++) {
		if (text[i] == '/') {
			break;
		} else {
			bytes++;
		}
	}

	output.bytes = bytes;

	return output;
}

String PathGetParent(String string, uintptr_t index) {
	if (index == PathCountSections(string)) return string;
	String section = PathGetSection(string, index);
	string.bytes = section.bytes + section.text - string.text + 1;
	return string;
}

String PathGetExtension(String string) {
	String extension = {};
	int lastSeparator = 0;

	for (intptr_t i = string.bytes - 1; i >= 0; i--) {
		if (string.text[i] == '.') {
			lastSeparator = i;
			break;
		}
	}

	if (!lastSeparator && string.text[0] != '.') {
		extension.text = string.text + string.bytes;
		extension.bytes = 0;
		return extension;
	} else {
		extension.text = string.text + lastSeparator + 1;
		extension.bytes = string.bytes - lastSeparator - 1;
		return extension;
	}
}

String PathGetParent(String string) {
	size_t newPathBytes = 0;

	for (uintptr_t i = 0; i < string.bytes - 1; i++) {
		if (string.text[i] == '/') {
			newPathBytes = i + 1;
		}
	}

	String result = {};
	result.bytes = newPathBytes;
	result.text = string.text;

	return result;
}

bool PathHasTrailingSlash(String path) {
	return path.bytes && path.text[path.bytes - 1] == '/';
}

String PathRemoveTrailingSlash(String path) {
	if (PathHasTrailingSlash(path)) path.bytes--;
	return path;
}

String PathGetName(String path) {
	intptr_t i = path.bytes - 2;
	while (i >= 0 && path.text[i] != '/') i--;
	path.text += i + 1, path.bytes -= i + 1;
	return path;
}

String PathGetDrive(String path) {
	uintptr_t i = 0;
	while (i < path.bytes && path.text[i] != '/') i++;
	path.bytes = i;
	return path;
}

bool PathHasPrefix(String path, String prefix) {
	prefix = PathRemoveTrailingSlash(prefix);
	return StringStartsWith(path, prefix) && ((path.bytes > prefix.bytes && path.text[prefix.bytes] == '/') || (path.bytes == prefix.bytes));
}

bool PathReplacePrefix(String *knownPath, String oldPath, String newPath) {
	if (PathHasPrefix(*knownPath, oldPath)) {
		String after = StringSlice(*knownPath, oldPath.bytes, -1);
		String path = StringAllocateAndFormat("%s%s", STRFMT(newPath), STRFMT(after));
		StringDestroy(knownPath);
		*knownPath = path;
		return true;
	}

	return false;
}
