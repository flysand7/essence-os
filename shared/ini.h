bool EsINIParse(EsINIState *s) {
#define INI_READ(destination, counter, c1, c2) \
	s->destination = s->buffer, s->counter = 0; \
	while (s->bytes && *s->buffer != c1 && *s->buffer != c2) s->counter++, s->buffer++, s->bytes--; \
	if (s->bytes && *s->buffer == c1) s->buffer++, s->bytes--;

	while (s->bytes) {
		char c = *s->buffer;

		if (c == ' ' || c == '\n' || c == '\r') { 
			s->buffer++, s->bytes--; 
			continue;
		} else if (c == ';') {
			s->valueBytes = 0;
			INI_READ(key, keyBytes, '\n', 0);
		} else if (c == '[') {
			s->keyBytes = s->valueBytes = 0;
			s->buffer++, s->bytes--;
			INI_READ(section, sectionBytes, ']', 0);
		} else {
			INI_READ(key, keyBytes, '=', '\n');
			INI_READ(value, valueBytes, '\n', 0);
		}

		return true;
	}

	return false;
}

bool EsINIPeek(EsINIState *s) {
	char *oldBuffer = s->buffer;
	size_t oldBytes = s->bytes;
	bool result = EsINIParse(s);
	s->buffer = oldBuffer;
	s->bytes = oldBytes;
	return result;
}

size_t EsINIFormat(EsINIState *s, char *buffer, size_t bytes) {
#define INI_WRITE(x, b) for (uintptr_t i = 0; i < b; i++) if (bytes) *buffer = x[i], buffer++, bytes--

	char *start = buffer;

	if (s->keyBytes || s->valueBytes) {
		if (s->key[0] == '[') return 0;
		INI_WRITE(s->key, s->keyBytes);
		if (s->key[0] != ';') INI_WRITE("=", 1);
		INI_WRITE(s->value, s->valueBytes);
		INI_WRITE("\n", 1);
	} else {
		INI_WRITE("\n[", 2);
		INI_WRITE(s->section, s->sectionBytes);
		INI_WRITE("]\n", 2);
	}

	return buffer - start;
}

void EsINIZeroTerminate(EsINIState *s) {
	static char emptyString = 0;
	if (s->sectionBytes) s->section[s->sectionBytes] = 0; else s->section = &emptyString;
	if (s->keyBytes) s->key[s->keyBytes] = 0; else s->key = &emptyString;
	if (s->valueBytes) s->value[s->valueBytes] = 0; else s->value = &emptyString;
}
