#ifdef API_TESTS_FOR_RUNNER

#define TEST(_callback) { .cName = #_callback }
typedef struct Test { const char *cName; } Test;

#else

#define ES_PRIVATE_APIS
#include <essence.h>
#include <shared/crc.h>

#define TEST(_callback) { .callback = _callback }
struct Test { bool (*callback)(); };

//////////////////////////////////////////////////////////////

bool BasicFileOperationsDoByteCount(size_t byteCount) {
	uint8_t *buffer = (uint8_t *) EsHeapAllocate(byteCount, false);

	for (uintptr_t i = 0; i < byteCount; i++) {
		buffer[i] = EsRandomU8();
	}

	EsError error = EsFileWriteAll(EsLiteral("|Settings:/temp.dat"), buffer, byteCount); 

	if (error != ES_SUCCESS) {
		EsPrint("Error %d writing file of size %d.\n", error, byteCount);
		return false;
	}

	size_t readSize;
	uint8_t *read = (uint8_t *) EsFileReadAll(EsLiteral("|Settings:/temp.dat"), &readSize, &error);

	if (error != ES_SUCCESS) {
		EsPrint("Error %d reading file of size %d.\n", error, byteCount);
		return false;
	}

	if (readSize != byteCount) {
		EsPrint("Read size mismatch: got %d, expected %d.\n", readSize, byteCount);
		return false;
	}

	if (EsMemoryCompare(buffer, read, byteCount)) {
		EsPrint("Read data mismatch.\n");
		return false;
	}

	EsHeapFree(buffer);
	EsHeapFree(read);

	return true;
}

bool BasicFileOperations() {
	for (uintptr_t i = 0; i < 24; i += 2) {
		if (!BasicFileOperationsDoByteCount(1 << i)) {
			return false;
		}
	}

	for (uintptr_t i = 18; i > 0; i -= 3) {
		if (!BasicFileOperationsDoByteCount(1 << i)) {
			return false;
		}
	}

	EsError error = EsPathDelete(EsLiteral("|Settings:/temp.dat"));

	if (error != ES_SUCCESS) {
		EsPrint("Error %d deleting file.\n", error);
		return false;
	}

	EsFileReadAll(EsLiteral("|Settings:/temp.dat"), nullptr, &error);

	if (error != ES_ERROR_FILE_DOES_NOT_EXIST) {
		EsPrint("Checking file does not exist after deleting, instead got error %d.\n", error);
		return false;
	}

	return true;
}

//////////////////////////////////////////////////////////////

bool AreFloatsRoughlyEqual(float a, float b) { return a - b < 0.0001f && a - b > -0.0001f; }
bool AreDoublesRoughlyEqual(double a, double b) { return a - b < 0.00000000001 && a - b > -0.00000000001; }
bool AreFloatsRoughlyEqual2(float a, float b) { return a - b < 0.01f && a - b > -0.01f; }

bool CRTMathFunctions() {
	for (int i = 0; i <= 5; i++) if (EsCRTabs(i) != i) return false;
	for (int i = -5; i <= 0; i++) if (EsCRTabs(i) != -i) return false;

	for (float i = -1.0f; i <= 1.0f; i += 0.01f) if (!AreFloatsRoughlyEqual(i, EsCRTcosf(EsCRTacosf(i)))) return false;
	for (float i = 0.0f; i <= 10.0f; i += 0.1f) if (!AreFloatsRoughlyEqual(EsCRTcosf(i), EsCRTcosf(-i))) return false;
	for (float i = 0.0f; i <= 10.0f; i += 0.1f) if (!AreFloatsRoughlyEqual(EsCRTcosf(i), EsCRTcosf(i + 2 * ES_PI))) return false;
	for (double i = 0.0; i <= 10.0; i += 0.1) if (!AreDoublesRoughlyEqual(EsCRTcos(i), EsCRTcos(-i))) return false;
	for (double i = 0.0; i <= 10.0; i += 0.1) if (!AreDoublesRoughlyEqual(EsCRTcos(i), EsCRTcos(i + 2 * ES_PI))) return false;
	for (double i = 0.0; i <= 10.0; i += 0.1) if (!AreFloatsRoughlyEqual(EsCRTcos(i), EsCRTcosf(i))) return false;
	if (!AreFloatsRoughlyEqual(EsCRTacosf(-1.0f), ES_PI)) return false;
	if (!AreFloatsRoughlyEqual(EsCRTacosf(0.0f), ES_PI / 2.0f)) return false;
	if (!AreFloatsRoughlyEqual(EsCRTacosf(1.0f), 0.0f)) return false;
	if (!AreFloatsRoughlyEqual(EsCRTcosf(0.0f), 1.0f)) return false;
	if (!AreFloatsRoughlyEqual(EsCRTcosf(ES_PI / 2.0f), 0.0f)) return false;
	if (!AreFloatsRoughlyEqual(EsCRTcosf(ES_PI), -1.0f)) return false;
	if (!AreDoublesRoughlyEqual(EsCRTcos(0.0), 1.0)) return false;
	if (!AreDoublesRoughlyEqual(EsCRTcos(ES_PI / 2.0), 0.0)) return false;
	if (!AreDoublesRoughlyEqual(EsCRTcos(ES_PI), -1.0)) return false;

	for (float i = -1.0f; i <= 1.0f; i += 0.01f) if (!AreFloatsRoughlyEqual(i, EsCRTsinf(EsCRTasinf(i)))) return false;
	for (float i = 0.0f; i <= 10.0f; i += 0.1f) if (!AreFloatsRoughlyEqual(EsCRTsinf(i), -EsCRTsinf(-i))) return false;
	for (float i = 0.0f; i <= 10.0f; i += 0.1f) if (!AreFloatsRoughlyEqual(EsCRTsinf(i), EsCRTsinf(i + 2 * ES_PI))) return false;
	for (double i = 0.0; i <= 10.0; i += 0.1) if (!AreDoublesRoughlyEqual(EsCRTsin(i), -EsCRTsin(-i))) return false;
	for (double i = 0.0; i <= 10.0; i += 0.1) if (!AreDoublesRoughlyEqual(EsCRTsin(i), EsCRTsin(i + 2 * ES_PI))) return false;
	for (double i = 0.0; i <= 10.0; i += 0.1) if (!AreFloatsRoughlyEqual(EsCRTsin(i), EsCRTsinf(i))) return false;
	if (!AreFloatsRoughlyEqual(EsCRTasinf(-1.0f), ES_PI / -2.0f)) return false;
	if (!AreFloatsRoughlyEqual(EsCRTasinf(0.0f), 0.0f)) return false;
	if (!AreFloatsRoughlyEqual(EsCRTasinf(1.0f), ES_PI / 2.0f)) return false;
	if (!AreFloatsRoughlyEqual(EsCRTsinf(0.0f), 0.0f)) return false;
	if (!AreFloatsRoughlyEqual(EsCRTsinf(ES_PI / 2.0f), 1.0f)) return false;
	if (!AreFloatsRoughlyEqual(EsCRTsinf(ES_PI), 0.0f)) return false;
	if (!AreDoublesRoughlyEqual(EsCRTsin(0.0), 0.0)) return false;
	if (!AreDoublesRoughlyEqual(EsCRTsin(ES_PI / 2.0), 1.0)) return false;
	if (!AreDoublesRoughlyEqual(EsCRTsin(ES_PI), 0.0)) return false;

	for (float i = 0.0f; i <= 5.0f; i += 0.1f) if (!AreFloatsRoughlyEqual(EsCRTsqrtf(i) * EsCRTsqrtf(i), i)) return false;
	for (float i = -5.0f; i <= 5.0f; i += 0.1f) if (!AreFloatsRoughlyEqual(EsCRTcbrtf(i) * EsCRTcbrtf(i) * EsCRTcbrtf(i), i)) return false;
	for (double i = 0.0; i <= 5.0; i += 0.1) if (!AreDoublesRoughlyEqual(EsCRTsqrt(i) * EsCRTsqrt(i), i)) return false;
	for (double i = -5.0; i <= 5.0; i += 0.1) if (!AreDoublesRoughlyEqual(EsCRTcbrt(i) * EsCRTcbrt(i) * EsCRTcbrt(i), i)) return false;

	for (float i = -5.0f; i <= 0.0f; i += 0.01f) if (-i != EsCRTfabsf(i)) return false;
	for (float i = 0.0f; i <= 5.0f; i += 0.01f) if (i != EsCRTfabsf(i)) return false;
	for (double i = -5.0; i <= 0.0; i += 0.01) if (-i != EsCRTfabs(i)) return false;
	for (double i = 0.0; i <= 5.0; i += 0.01) if (i != EsCRTfabs(i)) return false;

	// This tests avoid angles near the y axis with atan2, because y/x blows up there. TODO Is this acceptable behaviour?
	for (float i = -ES_PI / 4.0f; i < ES_PI / 4.0f; i += 0.01f) if (!AreFloatsRoughlyEqual(i, EsCRTatan2f(EsCRTsinf(i), EsCRTcosf(i)))) return false;
	for (float i = -ES_PI / 4.0f; i < ES_PI / 4.0f; i += 0.01f) if (!AreFloatsRoughlyEqual(i, EsCRTatanf(EsCRTsinf(i) / EsCRTcosf(i)))) return false;
	for (float i = -ES_PI * 7.0f / 8.0f; i < -ES_PI * 5.0f / 8.0f; i += 0.01f) if (!AreFloatsRoughlyEqual(i, EsCRTatan2f(EsCRTsinf(i), EsCRTcosf(i)))) return false;
	for (float i = ES_PI * 5.0f / 8.0f; i < ES_PI * 7.0f / 8.0f; i += 0.01f) if (!AreFloatsRoughlyEqual(i, EsCRTatan2f(EsCRTsinf(i), EsCRTcosf(i)))) return false;
	for (float i = -2.25f * ES_PI; i < -1.75f * ES_PI; i += 0.01f) if (!AreFloatsRoughlyEqual(i + 2 * ES_PI, EsCRTatan2f(EsCRTsinf(i), EsCRTcosf(i)))) return false;
	for (float i = 1.75f * ES_PI; i < 2.25f * ES_PI; i += 0.01f) if (!AreFloatsRoughlyEqual(i - 2 * ES_PI, EsCRTatan2f(EsCRTsinf(i), EsCRTcosf(i)))) return false;
	for (float i = -2.25f * ES_PI; i < -1.75f * ES_PI; i += 0.01f) if (!AreFloatsRoughlyEqual(i + 2 * ES_PI, EsCRTatanf(EsCRTsinf(i) / EsCRTcosf(i)))) return false;
	for (float i = 1.75f * ES_PI; i < 2.25f * ES_PI; i += 0.01f) if (!AreFloatsRoughlyEqual(i - 2 * ES_PI, EsCRTatanf(EsCRTsinf(i) / EsCRTcosf(i)))) return false;
	for (double i = -ES_PI / 4.0; i < ES_PI / 4.0; i += 0.01) if (!AreDoublesRoughlyEqual(i, EsCRTatan2(EsCRTsin(i), EsCRTcos(i)))) return false;
	for (double i = -ES_PI * 7.0 / 8.0; i < -ES_PI * 5.0 / 8.0; i += 0.01) if (!AreDoublesRoughlyEqual(i, EsCRTatan2(EsCRTsin(i), EsCRTcos(i)))) return false;
	for (double i = ES_PI * 5.0 / 8.0; i < ES_PI * 7.0 / 8.0; i += 0.01) if (!AreDoublesRoughlyEqual(i, EsCRTatan2(EsCRTsin(i), EsCRTcos(i)))) return false;
	for (double i = -2.25 * ES_PI; i < -1.75 * ES_PI; i += 0.01) if (!AreDoublesRoughlyEqual(i + 2 * ES_PI, EsCRTatan2(EsCRTsin(i), EsCRTcos(i)))) return false;
	for (double i = 1.75 * ES_PI; i < 2.25 * ES_PI; i += 0.01) if (!AreDoublesRoughlyEqual(i - 2 * ES_PI, EsCRTatan2(EsCRTsin(i), EsCRTcos(i)))) return false;

	for (float i = -1000.0f; i <= 1000.0f; i += 1.0f) if (EsCRTceilf(i) != i) return false;
	for (float i = -1000.0f; i <= 1000.0f; i += 1.0f) if (EsCRTfloorf(i) != i) return false;
	for (float i = -1000.0f; i <= 1000.0f; i += 1.0f) if (EsCRTceilf(i - 0.000001f) != i) return false;
	for (float i = -1000.0f; i <= 1000.0f; i += 1.0f) if (EsCRTfloorf(i + 0.000001f) != i) return false;
	for (float i = -1000.0f; i <= 1000.0f; i += 1.0f) if (EsCRTceilf(i - 0.1f) != i) return false;
	for (float i = -1000.0f; i <= 1000.0f; i += 1.0f) if (EsCRTfloorf(i + 0.1f) != i) return false;
	for (float i = -1000.0f; i <= 1000.0f; i += 1.0f) if (EsCRTceilf(i - 0.5f) != i) return false;
	for (float i = -1000.0f; i <= 1000.0f; i += 1.0f) if (EsCRTfloorf(i + 0.5f) != i) return false;
	for (float i = -1000.0f; i <= 1000.0f; i += 1.0f) if (EsCRTceilf(i - 0.9f) != i) return false;
	for (float i = -1000.0f; i <= 1000.0f; i += 1.0f) if (EsCRTfloorf(i + 0.9f) != i) return false;
	for (float i = -1000.0f; i <= 1000.0f; i += 1.0f) if (EsCRTceilf(i - 0.1f) != EsCRTceilf(i - 0.9f)) return false;
	for (float i = -1000.0f; i <= 1000.0f; i += 1.0f) if (EsCRTfloorf(i + 0.1f) != EsCRTfloorf(i + 0.9f)) return false;
	for (double i = -1000.0; i <= 1000.0; i += 1.0) if (EsCRTceil(i) != i) return false;
	for (double i = -1000.0; i <= 1000.0; i += 1.0) if (EsCRTfloor(i) != i) return false;
	for (double i = -1000.0; i <= 1000.0; i += 1.0) if (EsCRTceil(i - 0.00000000001) != i) return false;
	for (double i = -1000.0; i <= 1000.0; i += 1.0) if (EsCRTfloor(i + 0.00000000001) != i) return false;
	for (double i = -1000.0; i <= 1000.0; i += 1.0) if (EsCRTceil(i - 0.1) != i) return false;
	for (double i = -1000.0; i <= 1000.0; i += 1.0) if (EsCRTfloor(i + 0.1) != i) return false;
	for (double i = -1000.0; i <= 1000.0; i += 1.0) if (EsCRTceil(i - 0.5) != i) return false;
	for (double i = -1000.0; i <= 1000.0; i += 1.0) if (EsCRTfloor(i + 0.5) != i) return false;
	for (double i = -1000.0; i <= 1000.0; i += 1.0) if (EsCRTceil(i - 0.9) != i) return false;
	for (double i = -1000.0; i <= 1000.0; i += 1.0) if (EsCRTfloor(i + 0.9) != i) return false;
	for (double i = -1000.0; i <= 1000.0; i += 1.0) if (EsCRTceil(i - 0.1) != EsCRTceil(i - 0.9)) return false;
	for (double i = -1000.0; i <= 1000.0; i += 1.0) if (EsCRTfloor(i + 0.1) != EsCRTfloor(i + 0.9)) return false;

	for (float x = -10.0f; x <= 10.0f; x += 0.1f) for (float y = -10.0f; y <= 10.0f; y += 0.1f) if (!AreFloatsRoughlyEqual(x - (int64_t) (x / y) * y, EsCRTfmodf(x, y))) return false;
	for (double x = -10.0; x <= 10.0; x += 0.1) for (double y = -10.0; y <= 10.0; y += 0.1) if (!AreDoublesRoughlyEqual(x - (int64_t) (x / y) * y, EsCRTfmod(x, y))) return false;

	if (!EsCRTisnanf(0.0f / 0.0f)) return false;
	if (EsCRTisnanf(0.0f / 1.0f)) return false;
	if (EsCRTisnanf(1.0f / 0.0f)) return false;
	if (EsCRTisnanf(-1.0f / 0.0f)) return false;

	// TODO The precision of powf is really bad!! Get it to the point where it can use AreFloatsRoughlyEqual.
	for (float i = 0.0f; i <= 10.0f; i += 0.1f) if (!AreFloatsRoughlyEqual(EsCRTsqrtf(i), EsCRTpowf(i, 1.0f / 2.0f))) return false;
	for (float i = 0.0f; i <= 10.0f; i += 0.1f) if (!AreFloatsRoughlyEqual(EsCRTcbrtf(i), EsCRTpowf(i, 1.0f / 3.0f))) return false;
	for (float i = 0.0f; i <= 10.0f; i += 0.1f) if (!AreFloatsRoughlyEqual2(i * i, EsCRTpowf(i, 2.0f))) return false;
	for (float i = 0.1f; i <= 10.0f; i += 0.1f) if (!AreFloatsRoughlyEqual2(1.0f / (i * i), EsCRTpowf(i, -2.0f))) return false;
	for (double i = 0.0; i <= 10.0; i += 0.1) if (!AreDoublesRoughlyEqual(EsCRTsqrt(i), EsCRTpow(i, 1.0 / 2.0))) return false;
	for (double i = 0.0; i <= 10.0; i += 0.1) if (!AreDoublesRoughlyEqual(EsCRTcbrt(i), EsCRTpow(i, 1.0 / 3.0))) return false;
	for (double i = 0.0; i <= 10.0; i += 0.1) if (!AreDoublesRoughlyEqual(i * i, EsCRTpow(i, 2.0))) return false;
	for (double i = 0.1; i <= 10.0; i += 0.1) if (!AreDoublesRoughlyEqual(1.0 / (i * i), EsCRTpow(i, -2.0))) return false;

	if (!AreFloatsRoughlyEqual(EsCRTexpf(1.0f), 2.718281828459f)) return false;
	for (float i = -10.0f; i <= 4.0f; i += 0.1f) if (!AreFloatsRoughlyEqual(EsCRTexpf(i), EsCRTpowf(2.718281828459f, i))) return false;
	if (!AreDoublesRoughlyEqual(EsCRTexp(1.0), 2.718281828459)) return false;
	for (double i = -10.0; i <= 4.0; i += 0.1) if (!AreDoublesRoughlyEqual(EsCRTexp(i), EsCRTpow(2.718281828459, i))) return false;
	if (!AreFloatsRoughlyEqual(EsCRTexp2f(1.0f), 2.0f)) return false;
	for (float i = -10.0f; i <= 4.0f; i += 0.1f) if (!AreFloatsRoughlyEqual(EsCRTexp2f(i), EsCRTpowf(2.0f, i))) return false;
	if (!AreDoublesRoughlyEqual(EsCRTexp2(1.0), 2.0)) return false;
	for (double i = -10.0; i <= 4.0; i += 0.1) if (!AreDoublesRoughlyEqual(EsCRTexp2(i), EsCRTpow(2.0, i))) return false;
	if (!AreFloatsRoughlyEqual(EsCRTlog2f(2.0f), 1.0f)) return false;
	for (float i = -10.0f; i <= 4.0f; i += 0.1f) if (!AreFloatsRoughlyEqual(EsCRTlog2f(EsCRTexp2f(i)), i)) return false;
	if (!AreDoublesRoughlyEqual(EsCRTlog2(2.0), 1.0)) return false;
	for (double i = -10.0; i <= 4.0; i += 0.1) if (!AreDoublesRoughlyEqual(EsCRTlog2(EsCRTexp2(i)), i)) return false;

	return true;
}

//////////////////////////////////////////////////////////////

bool CRTStringFunctions() {
	// TODO strncmp, strncpy, strnlen, atod, atoi, atof, strtod, strtof, strtol, strtoul.

	for (int i = 0; i < 256; i++) if (EsCRTisalpha(i) != ((i >= 'a' && i <= 'z') || (i >= 'A' && i <= 'Z'))) return false;
	for (int i = 0; i < 256; i++) if (EsCRTisdigit(i) != (i >= '0' && i <= '9')) return false;
	for (int i = 0; i < 256; i++) if (EsCRTisupper(i) != (i >= 'A' && i <= 'Z')) return false;
	for (int i = 0; i < 256; i++) if (EsCRTisxdigit(i) != ((i >= '0' && i <= '9') || (i >= 'A' && i <= 'F') || (i >= 'a' && i <= 'f'))) return false;
	for (int i = 0; i <= 256; i++)  if ((EsCRTtolower(i) != i) != (i >= 'A' && i <= 'Z')) return false;

	if (0 <= EsCRTstrcmp("a", "ab")) return false;
	if (0 >= EsCRTstrcmp("ab", "a")) return false;
	if (0 <= EsCRTstrcmp("ab", "ac")) return false;
	if (0 <= EsCRTstrcmp("ac", "bc")) return false;
	if (0 <= EsCRTstrcmp("", "a")) return false;
	if (0 >= EsCRTstrcmp("a", "")) return false;
	if (0 >= EsCRTstrcmp("a", "A")) return false;
	if (EsCRTstrcmp("", "")) return false;
	if (EsCRTstrcmp("a", "a")) return false;
	if (EsCRTstrcmp("ab", "ab")) return false;

	char x[10];
	EsCRTstrcpy(x, "hello");
	if (EsCRTstrcmp(x, "hello")) return false;
	EsCRTstrcat(x, "!");
	if (EsCRTstrcmp(x, "hello!")) return false;

	if (EsCRTstrchr(x, '.')) return false;
	if (x + 0 != EsCRTstrchr(x, 'h')) return false;
	if (x + 1 != EsCRTstrchr(x, 'e')) return false;
	if (x + 2 != EsCRTstrchr(x, 'l')) return false;
	if (x + 4 != EsCRTstrchr(x, 'o')) return false;
	if (x + 6 != EsCRTstrchr(x, 0)) return false;
	if (EsCRTstrstr(x, ".")) return false;
	if (EsCRTstrstr(x, "le")) return false;
	if (EsCRTstrstr(x, "oo")) return false;
	if (EsCRTstrstr(x, "ah")) return false;
	if (EsCRTstrstr(x, "lle")) return false;
	if (x + 0 != EsCRTstrstr(x, "")) return false;
	if (x + 0 != EsCRTstrstr(x, "h")) return false;
	if (x + 0 != EsCRTstrstr(x, "he")) return false;
	if (x + 0 != EsCRTstrstr(x, "hello")) return false;
	if (x + 0 != EsCRTstrstr(x, "hello!")) return false;
	if (x + 1 != EsCRTstrstr(x, "ell")) return false;
	if (x + 1 != EsCRTstrstr(x, "ello!")) return false;
	if (x + 3 != EsCRTstrstr(x, "lo")) return false;

	if (0 != EsCRTstrlen("")) return false;
	if (6 != EsCRTstrlen(x)) return false;

	char *copy = EsCRTstrdup(x);
	if (EsCRTstrcmp(copy, x)) return false;
	EsCRTfree(copy);

	return true;
}

//////////////////////////////////////////////////////////////

int CompareU8(const void *_a, const void *_b) {
	uint8_t a = *(const uint8_t *) _a, b = *(const uint8_t *) _b;
	return a < b ? -1 : a > b;
}

bool CRTOtherFunctions() {
	// Note that malloc, free and realloc are assumed to be working if EsHeapAllocate, EsHeapFree and EsHeapReallocate are.

	uint8_t x[4] = { 1, 2, 3, 4 };
	uint8_t y[4] = { 1, 3, 3, 4 };
	uint8_t z[4] = { 6, 7, 8, 9 };

	if (0 <= EsCRTmemcmp(x, y, 4)) return false;
	if (0 <= EsCRTmemcmp(x, y, 3)) return false;
	if (0 <= EsCRTmemcmp(x, y, 2)) return false;
	if (0 >= EsCRTmemcmp(y, x, 4)) return false;
	if (0 >= EsCRTmemcmp(y, x, 3)) return false;
	if (0 >= EsCRTmemcmp(y, x, 2)) return false;
	if (EsCRTmemcmp(x, y, 1)) return false;
	if (EsCRTmemcmp(x, y, 0)) return false;
	if (EsCRTmemcmp(y, x, 1)) return false;
	if (EsCRTmemcmp(y, x, 0)) return false;

	for (int i = 0; i < 4; i++) if (x + i != EsCRTmemchr(x, i + 1, 4)) return false;
	for (int i = 4; i < 8; i++) if (EsCRTmemchr(x, i + 1, 4)) return false;

	y[3] = 5;
	EsCRTmemcpy(x, y, 2);
	if (x[0] != 1 || x[1] != 3 || x[2] != 3 || x[3] != 4) return false;
	if (y[0] != 1 || y[1] != 3 || y[2] != 3 || y[3] != 5) return false;
	EsCRTmemcpy(x, y, 4);
	if (x[0] != 1 || x[1] != 3 || x[2] != 3 || x[3] != 5) return false;
	if (y[0] != 1 || y[1] != 3 || y[2] != 3 || y[3] != 5) return false;
	EsCRTmemset(x, 0xCC, 4);
	for (int i = 0; i < 4; i++) if (x[i] != 0xCC) return false;
	if (y[0] != 1 || y[1] != 3 || y[2] != 3 || y[3] != 5) return false;
	EsCRTmemset(y, 0x00, 4);
	for (int i = 0; i < 4; i++) if (y[i] != 0x00) return false;
	for (int i = 0; i < 4; i++) if (x[i] != 0xCC) return false;
	if (z[0] != 6 || z[1] != 7 || z[2] != 8 || z[3] != 9) return false;

	y[0] = 0, y[1] = 1, y[2] = 2, y[3] = 3;
	EsCRTmemmove(y + 1, y + 2, 2);
	if (y[0] != 0 || y[1] != 2 || y[2] != 3 || y[3] != 3) return false;
	y[0] = 0, y[1] = 1, y[2] = 2, y[3] = 3;
	EsCRTmemmove(y + 2, y + 1, 2);
	if (y[0] != 0 || y[1] != 1 || y[2] != 1 || y[3] != 2) return false;

	for (int i = 0; i < 100000; i++) {
		uint8_t *p = (uint8_t *) EsCRTcalloc(i, 1);
		uint8_t *q = (uint8_t *) EsCRTmalloc(i);
		if (i == 0) { if (p || q) return false; else continue; }
		if (p == q) return false;
		if (!p || !q) return false;
		for (int j = 0; j < i; j++) if (p[j]) return false;
		EsCRTfree(p);
		EsCRTfree(q);
		if (i >= 1000) i += 100;
		if (i >= 10000) i += 1000;
	}

	uint8_t *array = (uint8_t *) EsCRTmalloc(10000);
	size_t *count = (size_t *) EsCRTcalloc(256, sizeof(size_t));
	EsRandomSeed(15); 
	array[0] = 100;
	count[100]++;
	for (int i = 1; i < 10000; i++) { array[i] = (EsRandomU8() & ~1) ?: 2; count[array[i]]++; }
	EsCRTqsort(array, 10000, 1, CompareU8);
	int p = 0;
	for (uintptr_t i = 0; i < 256; i++) for (uintptr_t j = 0; j < count[i]; j++, p++) if (array[p] != i) return false;
	if (p != 10000) return false;
	uint8_t n = 100;
	uint8_t *q = (uint8_t *) EsCRTbsearch(&n, array, 10000, 1, CompareU8);
	if (!q || q[0] != 100) return false;
	n = 0;
	if (EsCRTbsearch(&n, array, 10000, 1, CompareU8)) return false;
	n = 255;
	if (EsCRTbsearch(&n, array, 10000, 1, CompareU8)) return false;
	for (n = 1; n < 255; n += 2) if (EsCRTbsearch(&n, array, 10000, 1, CompareU8)) return false;
	EsCRTfree(array);
	EsCRTfree(count);

#ifdef ES_BITS_32
	if (EsCRTcalloc(0x7FFFFFFF, 0x7FFFFFFF)) return false;
	if (EsCRTcalloc(0xFFFFFFFF, 0xFFFFFFFF)) return false;
#endif
#ifdef ES_BITS_64
	if (EsCRTcalloc(0x7FFFFFFFFFFFFFFF, 0x7FFFFFFFFFFFFFFF)) return false;
	if (EsCRTcalloc(0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF)) return false;
#endif

	return true;
}

//////////////////////////////////////////////////////////////

#endif

const Test tests[] = {
	TEST(BasicFileOperations),
	TEST(CRTMathFunctions),
	TEST(CRTStringFunctions),
	TEST(CRTOtherFunctions),
};

#ifndef API_TESTS_FOR_RUNNER

void RunTests() {
	size_t fileSize;
	EsError error;
	void *fileData = EsFileReadAll(EsLiteral("|Settings:/test.dat"), &fileSize, &error); 

	if (error == ES_ERROR_FILE_DOES_NOT_EXIST) {
		return; // Not in test mode.
	} else if (error != ES_SUCCESS) {
		EsPrint("Could not read test.dat (error %d).\n", error);
	} else if (fileSize != sizeof(uint32_t) * 2) {
		EsPrint("test.dat is the wrong size (got %d, expected %d).\n", fileSize, sizeof(uint32_t));
	} else {
		uint32_t index, mode;
		EsMemoryCopy(&index, fileData, sizeof(uint32_t));
		EsMemoryCopy(&mode, (uint8_t *) fileData + sizeof(uint32_t), sizeof(uint32_t));
		EsHeapFree(fileData);
		
		if (index >= sizeof(tests) / sizeof(tests[0])) {
			EsPrint("Test index out of bounds.\n");
		} else if (tests[index].callback()) {
			EsPrint("[APITests-Success]\n");
		} else {
			EsPrint("[APITests-Failure]\n");
			if (~mode & 1) EsSyscall(ES_SYSCALL_DEBUG_COMMAND, 2, 0, 0, 0);
		}
	}

	EsSyscall(ES_SYSCALL_SHUTDOWN, SHUTDOWN_ACTION_POWER_OFF, 0, 0, 0);
	EsProcessTerminateCurrent();
}

void _start() {
	_init();

	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_INSTANCE_CREATE) {
			EsInstance *instance = EsInstanceCreate(message, EsLiteral("API Tests"));
			EsApplicationStartupRequest request = EsInstanceGetStartupRequest(instance);

			if (request.flags & ES_APPLICATION_STARTUP_BACKGROUND_SERVICE) {
				RunTests();
			}
		}
	}
}

#endif
