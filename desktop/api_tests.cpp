#ifdef API_TESTS_FOR_RUNNER

#define TEST(_callback, _timeoutSeconds) { .cName = #_callback, .timeoutSeconds = _timeoutSeconds }
typedef struct Test { const char *cName; int timeoutSeconds; } Test;

#else

#define ES_PRIVATE_APIS
#include <essence.h>
#include <shared/crc.h>
#include <shared/array.cpp>
#include <shared/arena.cpp>
#include <shared/range_set.cpp>
#undef EsUTF8IsValid
#include <shared/unicode.cpp>

#define TEST(_callback, _timeoutSeconds) { .callback = _callback }
struct Test { bool (*callback)(); };

#define CHECK(x) do { if ((x)) { checkIndex++; } else { EsPrint("Failed check %d: " #x, checkIndex); return false; } } while (0)

//////////////////////////////////////////////////////////////

bool AreFloatsRoughlyEqual(float a, float b) { return a - b < 0.0001f && a - b > -0.0001f; }
bool AreDoublesRoughlyEqual(double a, double b) { return a - b < 0.00000000001 && a - b > -0.00000000001; }
bool AreFloatsRoughlyEqual2(float a, float b) { return a - b < 0.01f && a - b > -0.01f; }

int CompareU8(const void *_a, const void *_b) {
	uint8_t a = *(const uint8_t *) _a, b = *(const uint8_t *) _b;
	return a < b ? -1 : a > b;
}

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
	int checkIndex = 0;

	for (uintptr_t i = 0; i < 24; i += 2) {
		CHECK(BasicFileOperationsDoByteCount(1 << i));
	}

	for (uintptr_t i = 18; i > 0; i -= 3) {
		CHECK(BasicFileOperationsDoByteCount(1 << i));
	}

	EsError error = EsPathDelete(EsLiteral("|Settings:/temp.dat"));

	if (error != ES_SUCCESS) {
		EsPrint("Error %d deleting file.\n", error);
		CHECK(false);
	}

	EsFileReadAll(EsLiteral("|Settings:/temp.dat"), nullptr, &error);

	if (error != ES_ERROR_FILE_DOES_NOT_EXIST) {
		EsPrint("Checking file does not exist after deleting, instead got error %d.\n", error);
		CHECK(false);
	}

	return true;
}

//////////////////////////////////////////////////////////////

bool CRTMathFunctions() {
	int checkIndex = 0;

	for (int i = 0; i <= 5; i++) CHECK(EsCRTabs(i) == i);
	for (int i = -5; i <= 0; i++) CHECK(EsCRTabs(i) == -i);

	for (float i = -1.0f; i <= 1.0f; i += 0.01f) CHECK(AreFloatsRoughlyEqual(i, EsCRTcosf(EsCRTacosf(i))));
	for (float i = 0.0f; i <= 10.0f; i += 0.1f) CHECK(AreFloatsRoughlyEqual(EsCRTcosf(i), EsCRTcosf(-i)));
	for (float i = 0.0f; i <= 10.0f; i += 0.1f) CHECK(AreFloatsRoughlyEqual(EsCRTcosf(i), EsCRTcosf(i + 2 * ES_PI)));
	for (double i = 0.0; i <= 10.0; i += 0.1) CHECK(AreDoublesRoughlyEqual(EsCRTcos(i), EsCRTcos(-i)));
	for (double i = 0.0; i <= 10.0; i += 0.1) CHECK(AreDoublesRoughlyEqual(EsCRTcos(i), EsCRTcos(i + 2 * ES_PI)));
	for (double i = 0.0; i <= 10.0; i += 0.1) CHECK(AreFloatsRoughlyEqual(EsCRTcos(i), EsCRTcosf(i)));
	CHECK(AreFloatsRoughlyEqual(EsCRTacosf(-1.0f), ES_PI));
	CHECK(AreFloatsRoughlyEqual(EsCRTacosf(0.0f), ES_PI / 2.0f));
	CHECK(AreFloatsRoughlyEqual(EsCRTacosf(1.0f), 0.0f));
	CHECK(AreFloatsRoughlyEqual(EsCRTcosf(0.0f), 1.0f));
	CHECK(AreFloatsRoughlyEqual(EsCRTcosf(ES_PI / 2.0f), 0.0f));
	CHECK(AreFloatsRoughlyEqual(EsCRTcosf(ES_PI), -1.0f));
	CHECK(AreDoublesRoughlyEqual(EsCRTcos(0.0), 1.0));
	CHECK(AreDoublesRoughlyEqual(EsCRTcos(ES_PI / 2.0), 0.0));
	CHECK(AreDoublesRoughlyEqual(EsCRTcos(ES_PI), -1.0));

	for (float i = -1.0f; i <= 1.0f; i += 0.01f) CHECK(AreFloatsRoughlyEqual(i, EsCRTsinf(EsCRTasinf(i))));
	for (float i = 0.0f; i <= 10.0f; i += 0.1f) CHECK(AreFloatsRoughlyEqual(EsCRTsinf(i), -EsCRTsinf(-i)));
	for (float i = 0.0f; i <= 10.0f; i += 0.1f) CHECK(AreFloatsRoughlyEqual(EsCRTsinf(i), EsCRTsinf(i + 2 * ES_PI)));
	for (double i = 0.0; i <= 10.0; i += 0.1) CHECK(AreDoublesRoughlyEqual(EsCRTsin(i), -EsCRTsin(-i)));
	for (double i = 0.0; i <= 10.0; i += 0.1) CHECK(AreDoublesRoughlyEqual(EsCRTsin(i), EsCRTsin(i + 2 * ES_PI)));
	for (double i = 0.0; i <= 10.0; i += 0.1) CHECK(AreFloatsRoughlyEqual(EsCRTsin(i), EsCRTsinf(i)));
	CHECK(AreFloatsRoughlyEqual(EsCRTasinf(-1.0f), ES_PI / -2.0f));
	CHECK(AreFloatsRoughlyEqual(EsCRTasinf(0.0f), 0.0f));
	CHECK(AreFloatsRoughlyEqual(EsCRTasinf(1.0f), ES_PI / 2.0f));
	CHECK(AreFloatsRoughlyEqual(EsCRTsinf(0.0f), 0.0f));
	CHECK(AreFloatsRoughlyEqual(EsCRTsinf(ES_PI / 2.0f), 1.0f));
	CHECK(AreFloatsRoughlyEqual(EsCRTsinf(ES_PI), 0.0f));
	CHECK(AreDoublesRoughlyEqual(EsCRTsin(0.0), 0.0));
	CHECK(AreDoublesRoughlyEqual(EsCRTsin(ES_PI / 2.0), 1.0));
	CHECK(AreDoublesRoughlyEqual(EsCRTsin(ES_PI), 0.0));

	for (float i = 0.0f; i <= 5.0f; i += 0.1f) CHECK(AreFloatsRoughlyEqual(EsCRTsqrtf(i) * EsCRTsqrtf(i), i));
	for (float i = -5.0f; i <= 5.0f; i += 0.1f) CHECK(AreFloatsRoughlyEqual(EsCRTcbrtf(i) * EsCRTcbrtf(i) * EsCRTcbrtf(i), i));
	for (double i = 0.0; i <= 5.0; i += 0.1) CHECK(AreDoublesRoughlyEqual(EsCRTsqrt(i) * EsCRTsqrt(i), i));
	for (double i = -5.0; i <= 5.0; i += 0.1) CHECK(AreDoublesRoughlyEqual(EsCRTcbrt(i) * EsCRTcbrt(i) * EsCRTcbrt(i), i));

	for (float i = -5.0f; i <= 0.0f; i += 0.01f) CHECK(-i == EsCRTfabsf(i));
	for (float i = 0.0f; i <= 5.0f; i += 0.01f) CHECK(i == EsCRTfabsf(i));
	for (double i = -5.0; i <= 0.0; i += 0.01) CHECK(-i == EsCRTfabs(i));
	for (double i = 0.0; i <= 5.0; i += 0.01) CHECK(i == EsCRTfabs(i));

	// This tests avoid angles near the y axis with atan2, because y/x blows up there. TODO Is this acceptable behaviour?
	for (float i = -ES_PI / 4.0f; i < ES_PI / 4.0f; i += 0.01f) CHECK(AreFloatsRoughlyEqual(i, EsCRTatan2f(EsCRTsinf(i), EsCRTcosf(i))));
	for (float i = -ES_PI / 4.0f; i < ES_PI / 4.0f; i += 0.01f) CHECK(AreFloatsRoughlyEqual(i, EsCRTatanf(EsCRTsinf(i) / EsCRTcosf(i))));
	for (float i = -ES_PI * 7.0f / 8.0f; i < -ES_PI * 5.0f / 8.0f; i += 0.01f) CHECK(AreFloatsRoughlyEqual(i, EsCRTatan2f(EsCRTsinf(i), EsCRTcosf(i))));
	for (float i = ES_PI * 5.0f / 8.0f; i < ES_PI * 7.0f / 8.0f; i += 0.01f) CHECK(AreFloatsRoughlyEqual(i, EsCRTatan2f(EsCRTsinf(i), EsCRTcosf(i))));
	for (float i = -2.25f * ES_PI; i < -1.75f * ES_PI; i += 0.01f) CHECK(AreFloatsRoughlyEqual(i + 2 * ES_PI, EsCRTatan2f(EsCRTsinf(i), EsCRTcosf(i))));
	for (float i = 1.75f * ES_PI; i < 2.25f * ES_PI; i += 0.01f) CHECK(AreFloatsRoughlyEqual(i - 2 * ES_PI, EsCRTatan2f(EsCRTsinf(i), EsCRTcosf(i))));
	for (float i = -2.25f * ES_PI; i < -1.75f * ES_PI; i += 0.01f) CHECK(AreFloatsRoughlyEqual(i + 2 * ES_PI, EsCRTatanf(EsCRTsinf(i) / EsCRTcosf(i))));
	for (float i = 1.75f * ES_PI; i < 2.25f * ES_PI; i += 0.01f) CHECK(AreFloatsRoughlyEqual(i - 2 * ES_PI, EsCRTatanf(EsCRTsinf(i) / EsCRTcosf(i))));
	for (double i = -ES_PI / 4.0; i < ES_PI / 4.0; i += 0.01) CHECK(AreDoublesRoughlyEqual(i, EsCRTatan2(EsCRTsin(i), EsCRTcos(i))));
	for (double i = -ES_PI * 7.0 / 8.0; i < -ES_PI * 5.0 / 8.0; i += 0.01) CHECK(AreDoublesRoughlyEqual(i, EsCRTatan2(EsCRTsin(i), EsCRTcos(i))));
	for (double i = ES_PI * 5.0 / 8.0; i < ES_PI * 7.0 / 8.0; i += 0.01) CHECK(AreDoublesRoughlyEqual(i, EsCRTatan2(EsCRTsin(i), EsCRTcos(i))));
	for (double i = -2.25 * ES_PI; i < -1.75 * ES_PI; i += 0.01) CHECK(AreDoublesRoughlyEqual(i + 2 * ES_PI, EsCRTatan2(EsCRTsin(i), EsCRTcos(i))));
	for (double i = 1.75 * ES_PI; i < 2.25 * ES_PI; i += 0.01) CHECK(AreDoublesRoughlyEqual(i - 2 * ES_PI, EsCRTatan2(EsCRTsin(i), EsCRTcos(i))));

	for (float i = -1000.0f; i <= 1000.0f; i += 1.0f) CHECK(EsCRTceilf(i) == i);
	for (float i = -1000.0f; i <= 1000.0f; i += 1.0f) CHECK(EsCRTfloorf(i) == i);
	for (float i = -1000.0f; i <= 1000.0f; i += 1.0f) CHECK(EsCRTceilf(i - 0.000001f) == i);
	for (float i = -1000.0f; i <= 1000.0f; i += 1.0f) CHECK(EsCRTfloorf(i + 0.000001f) == i);
	for (float i = -1000.0f; i <= 1000.0f; i += 1.0f) CHECK(EsCRTceilf(i - 0.1f) == i);
	for (float i = -1000.0f; i <= 1000.0f; i += 1.0f) CHECK(EsCRTfloorf(i + 0.1f) == i);
	for (float i = -1000.0f; i <= 1000.0f; i += 1.0f) CHECK(EsCRTceilf(i - 0.5f) == i);
	for (float i = -1000.0f; i <= 1000.0f; i += 1.0f) CHECK(EsCRTfloorf(i + 0.5f) == i);
	for (float i = -1000.0f; i <= 1000.0f; i += 1.0f) CHECK(EsCRTceilf(i - 0.9f) == i);
	for (float i = -1000.0f; i <= 1000.0f; i += 1.0f) CHECK(EsCRTfloorf(i + 0.9f) == i);
	for (float i = -1000.0f; i <= 1000.0f; i += 1.0f) CHECK(EsCRTceilf(i - 0.1f) == EsCRTceilf(i - 0.9f));
	for (float i = -1000.0f; i <= 1000.0f; i += 1.0f) CHECK(EsCRTfloorf(i + 0.1f) == EsCRTfloorf(i + 0.9f));
	for (double i = -1000.0; i <= 1000.0; i += 1.0) CHECK(EsCRTceil(i) == i);
	for (double i = -1000.0; i <= 1000.0; i += 1.0) CHECK(EsCRTfloor(i) == i);
	for (double i = -1000.0; i <= 1000.0; i += 1.0) CHECK(EsCRTceil(i - 0.00000000001) == i);
	for (double i = -1000.0; i <= 1000.0; i += 1.0) CHECK(EsCRTfloor(i + 0.00000000001) == i);
	for (double i = -1000.0; i <= 1000.0; i += 1.0) CHECK(EsCRTceil(i - 0.1) == i);
	for (double i = -1000.0; i <= 1000.0; i += 1.0) CHECK(EsCRTfloor(i + 0.1) == i);
	for (double i = -1000.0; i <= 1000.0; i += 1.0) CHECK(EsCRTceil(i - 0.5) == i);
	for (double i = -1000.0; i <= 1000.0; i += 1.0) CHECK(EsCRTfloor(i + 0.5) == i);
	for (double i = -1000.0; i <= 1000.0; i += 1.0) CHECK(EsCRTceil(i - 0.9) == i);
	for (double i = -1000.0; i <= 1000.0; i += 1.0) CHECK(EsCRTfloor(i + 0.9) == i);
	for (double i = -1000.0; i <= 1000.0; i += 1.0) CHECK(EsCRTceil(i - 0.1) == EsCRTceil(i - 0.9));
	for (double i = -1000.0; i <= 1000.0; i += 1.0) CHECK(EsCRTfloor(i + 0.1) == EsCRTfloor(i + 0.9));

	for (float x = -10.0f; x <= 10.0f; x += 0.1f) for (float y = -10.0f; y <= 10.0f; y += 0.1f) CHECK(AreFloatsRoughlyEqual(x - (int64_t) (x / y) * y, EsCRTfmodf(x, y)));
	for (double x = -10.0; x <= 10.0; x += 0.1) for (double y = -10.0; y <= 10.0; y += 0.1) CHECK(AreDoublesRoughlyEqual(x - (int64_t) (x / y) * y, EsCRTfmod(x, y)));

	CHECK(EsCRTisnanf(0.0f / 0.0f));
	CHECK(!EsCRTisnanf(0.0f / 1.0f));
	CHECK(!EsCRTisnanf(1.0f / 0.0f));
	CHECK(!EsCRTisnanf(-1.0f / 0.0f));

	// TODO The precision of powf is really bad!! Get it to the point where it can use AreFloatsRoughlyEqual.
	for (float i = 0.0f; i <= 10.0f; i += 0.1f) CHECK(AreFloatsRoughlyEqual(EsCRTsqrtf(i), EsCRTpowf(i, 1.0f / 2.0f)));
	for (float i = 0.0f; i <= 10.0f; i += 0.1f) CHECK(AreFloatsRoughlyEqual(EsCRTcbrtf(i), EsCRTpowf(i, 1.0f / 3.0f)));
	for (float i = 0.0f; i <= 10.0f; i += 0.1f) CHECK(AreFloatsRoughlyEqual2(i * i, EsCRTpowf(i, 2.0f)));
	for (float i = 0.1f; i <= 10.0f; i += 0.1f) CHECK(AreFloatsRoughlyEqual2(1.0f / (i * i), EsCRTpowf(i, -2.0f)));
	for (double i = 0.0; i <= 10.0; i += 0.1) CHECK(AreDoublesRoughlyEqual(EsCRTsqrt(i), EsCRTpow(i, 1.0 / 2.0)));
	for (double i = 0.0; i <= 10.0; i += 0.1) CHECK(AreDoublesRoughlyEqual(EsCRTcbrt(i), EsCRTpow(i, 1.0 / 3.0)));
	for (double i = 0.0; i <= 10.0; i += 0.1) CHECK(AreDoublesRoughlyEqual(i * i, EsCRTpow(i, 2.0)));
	for (double i = 0.1; i <= 10.0; i += 0.1) CHECK(AreDoublesRoughlyEqual(1.0 / (i * i), EsCRTpow(i, -2.0)));

	CHECK(AreFloatsRoughlyEqual(EsCRTexpf(1.0f), 2.718281828459f));
	for (float i = -10.0f; i <= 4.0f; i += 0.1f) CHECK(AreFloatsRoughlyEqual(EsCRTexpf(i), EsCRTpowf(2.718281828459f, i)));
	CHECK(AreDoublesRoughlyEqual(EsCRTexp(1.0), 2.718281828459));
	for (double i = -10.0; i <= 4.0; i += 0.1) CHECK(AreDoublesRoughlyEqual(EsCRTexp(i), EsCRTpow(2.718281828459, i)));
	CHECK(AreFloatsRoughlyEqual(EsCRTexp2f(1.0f), 2.0f));
	for (float i = -10.0f; i <= 4.0f; i += 0.1f) CHECK(AreFloatsRoughlyEqual(EsCRTexp2f(i), EsCRTpowf(2.0f, i)));
	CHECK(AreDoublesRoughlyEqual(EsCRTexp2(1.0), 2.0));
	for (double i = -10.0; i <= 4.0; i += 0.1) CHECK(AreDoublesRoughlyEqual(EsCRTexp2(i), EsCRTpow(2.0, i)));
	CHECK(AreFloatsRoughlyEqual(EsCRTlog2f(2.0f), 1.0f));
	for (float i = -10.0f; i <= 4.0f; i += 0.1f) CHECK(AreFloatsRoughlyEqual(EsCRTlog2f(EsCRTexp2f(i)), i));
	CHECK(AreDoublesRoughlyEqual(EsCRTlog2(2.0), 1.0));
	for (double i = -10.0; i <= 4.0; i += 0.1) CHECK(AreDoublesRoughlyEqual(EsCRTlog2(EsCRTexp2(i)), i));

	return true;
}

//////////////////////////////////////////////////////////////

bool CRTStringFunctions() {
	// TODO strncmp, strncpy, strnlen, atod, atoi, atof, strtod, strtof, strtol, strtoul.

	int checkIndex = 0;

	for (int i = 0; i < 256; i++) CHECK(EsCRTisalpha(i) == ((i >= 'a' && i <= 'z') || (i >= 'A' && i <= 'Z')));
	for (int i = 0; i < 256; i++) CHECK(EsCRTisdigit(i) == (i >= '0' && i <= '9'));
	for (int i = 0; i < 256; i++) CHECK(EsCRTisupper(i) == (i >= 'A' && i <= 'Z'));
	for (int i = 0; i < 256; i++) CHECK(EsCRTisxdigit(i) == ((i >= '0' && i <= '9') || (i >= 'A' && i <= 'F') || (i >= 'a' && i <= 'f')));
	for (int i = 0; i <= 256; i++)  CHECK((EsCRTtolower(i) != i) == (i >= 'A' && i <= 'Z'));

	CHECK(0 > EsCRTstrcmp("a", "ab"));
	CHECK(0 < EsCRTstrcmp("ab", "a"));
	CHECK(0 > EsCRTstrcmp("ab", "ac"));
	CHECK(0 > EsCRTstrcmp("ac", "bc"));
	CHECK(0 > EsCRTstrcmp("", "a"));
	CHECK(0 < EsCRTstrcmp("a", ""));
	CHECK(0 < EsCRTstrcmp("a", "A"));
	CHECK(0 == EsCRTstrcmp("", ""));
	CHECK(0 == EsCRTstrcmp("a", "a"));
	CHECK(0 == EsCRTstrcmp("ab", "ab"));

	char x[10];
	EsCRTstrcpy(x, "hello");
	CHECK(0 == EsCRTstrcmp(x, "hello"));
	EsCRTstrcat(x, "!");
	CHECK(0 == EsCRTstrcmp(x, "hello!"));

	CHECK(!EsCRTstrchr(x, '.'));
	CHECK(x + 0 == EsCRTstrchr(x, 'h'));
	CHECK(x + 1 == EsCRTstrchr(x, 'e'));
	CHECK(x + 2 == EsCRTstrchr(x, 'l'));
	CHECK(x + 4 == EsCRTstrchr(x, 'o'));
	CHECK(x + 6 == EsCRTstrchr(x, 0));
	CHECK(!EsCRTstrstr(x, "."));
	CHECK(!EsCRTstrstr(x, "le"));
	CHECK(!EsCRTstrstr(x, "oo"));
	CHECK(!EsCRTstrstr(x, "ah"));
	CHECK(!EsCRTstrstr(x, "lle"));
	CHECK(x + 0 == EsCRTstrstr(x, ""));
	CHECK(x + 0 == EsCRTstrstr(x, "h"));
	CHECK(x + 0 == EsCRTstrstr(x, "he"));
	CHECK(x + 0 == EsCRTstrstr(x, "hello"));
	CHECK(x + 0 == EsCRTstrstr(x, "hello!"));
	CHECK(x + 1 == EsCRTstrstr(x, "ell"));
	CHECK(x + 1 == EsCRTstrstr(x, "ello!"));
	CHECK(x + 3 == EsCRTstrstr(x, "lo"));

	CHECK(0 == EsCRTstrlen(""));
	CHECK(6 == EsCRTstrlen(x));

	char *copy = EsCRTstrdup(x);
	CHECK(0 == EsCRTstrcmp(copy, x));
	EsCRTfree(copy);

	return true;
}

//////////////////////////////////////////////////////////////

bool CRTOtherFunctions() {
	// TODO setjmp, longjmp.
	// Note that malloc, free and realloc are assumed to be working if EsHeapAllocate, EsHeapFree and EsHeapReallocate are.

	int checkIndex = 0;

	uint8_t x[4] = { 1, 2, 3, 4 };
	uint8_t y[4] = { 1, 3, 3, 4 };
	uint8_t z[4] = { 6, 7, 8, 9 };

	CHECK(0 > EsCRTmemcmp(x, y, 4));
	CHECK(0 > EsCRTmemcmp(x, y, 3));
	CHECK(0 > EsCRTmemcmp(x, y, 2));
	CHECK(0 < EsCRTmemcmp(y, x, 4));
	CHECK(0 < EsCRTmemcmp(y, x, 3));
	CHECK(0 < EsCRTmemcmp(y, x, 2));
	CHECK(0 == EsCRTmemcmp(x, y, 1));
	CHECK(0 == EsCRTmemcmp(x, y, 0));
	CHECK(0 == EsCRTmemcmp(y, x, 1));
	CHECK(0 == EsCRTmemcmp(y, x, 0));

	for (int i = 0; i < 4; i++) CHECK(x + i == EsCRTmemchr(x, i + 1, 4));
	for (int i = 4; i < 8; i++) CHECK(!EsCRTmemchr(x, i + 1, 4));

	y[3] = 5;
	EsCRTmemcpy(x, y, 2);
	CHECK(x[0] == 1 && x[1] == 3 && x[2] == 3 && x[3] == 4);
	CHECK(y[0] == 1 && y[1] == 3 && y[2] == 3 && y[3] == 5);
	EsCRTmemcpy(x, y, 4);
	CHECK(x[0] == 1 && x[1] == 3 && x[2] == 3 && x[3] == 5);
	CHECK(y[0] == 1 && y[1] == 3 && y[2] == 3 && y[3] == 5);
	EsCRTmemset(x, 0xCC, 4);
	for (int i = 0; i < 4; i++) CHECK(x[i] == 0xCC);
	CHECK(y[0] == 1 && y[1] == 3 && y[2] == 3 && y[3] == 5);
	EsCRTmemset(y, 0x00, 4);
	for (int i = 0; i < 4; i++) CHECK(y[i] == 0x00);
	for (int i = 0; i < 4; i++) CHECK(x[i] == 0xCC);
	CHECK(z[0] == 6 && z[1] == 7 && z[2] == 8 && z[3] == 9);

	y[0] = 0, y[1] = 1, y[2] = 2, y[3] = 3;
	EsCRTmemmove(y + 1, y + 2, 2);
	CHECK(y[0] == 0 && y[1] == 2 && y[2] == 3 && y[3] == 3);
	y[0] = 0, y[1] = 1, y[2] = 2, y[3] = 3;
	EsCRTmemmove(y + 2, y + 1, 2);
	CHECK(y[0] == 0 && y[1] == 1 && y[2] == 1 && y[3] == 2);

	for (int i = 0; i < 100000; i++) {
		uint8_t *p = (uint8_t *) EsCRTcalloc(i, 1);
		uint8_t *q = (uint8_t *) EsCRTmalloc(i);
		if (i == 0) { CHECK(!p && !q); continue; }
		CHECK(p != q);
		CHECK(p && q);
		for (int j = 0; j < i; j++) CHECK(!p[j]);
		EsCRTfree(p);
		EsCRTfree(q);
		if (i > 1000) i += 100;
		if (i > 10000) i += 1000;
	}

	uint8_t *array = (uint8_t *) EsCRTmalloc(10000);
	size_t *count = (size_t *) EsCRTcalloc(256, sizeof(size_t));
	EsRandomSeed(15); 
	array[0] = 100;
	count[100]++;
	for (int i = 1; i < 10000; i++) { array[i] = (EsRandomU8() & ~1) ?: 2; count[array[i]]++; }
	EsCRTqsort(array, 10000, 1, CompareU8);
	int p = 0;
	for (uintptr_t i = 0; i < 256; i++) for (uintptr_t j = 0; j < count[i]; j++, p++) CHECK(array[p] == i);
	CHECK(p == 10000);
	uint8_t n = 100;
	uint8_t *q = (uint8_t *) EsCRTbsearch(&n, array, 10000, 1, CompareU8);
	CHECK(q && q[0] == 100);
	n = 0;
	CHECK(!EsCRTbsearch(&n, array, 10000, 1, CompareU8));
	n = 255;
	CHECK(!EsCRTbsearch(&n, array, 10000, 1, CompareU8));

	for (n = 0; n < 255; n++) {
		q = (uint8_t *) EsCRTbsearch(&n, array, 10000, 1, CompareU8);
		if (count[n]) CHECK(q && *q == n);
		else CHECK(!q);
	}

	EsCRTfree(array);
	EsCRTfree(count);

#ifdef ES_BITS_32
	CHECK(!EsCRTcalloc(0x7FFFFFFF, 0x7FFFFFFF));
	CHECK(!EsCRTcalloc(0xFFFFFFFF, 0xFFFFFFFF));
#endif
#ifdef ES_BITS_64
	CHECK(!EsCRTcalloc(0x7FFFFFFFFFFFFFFF, 0x7FFFFFFFFFFFFFFF));
	CHECK(!EsCRTcalloc(0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF));
#endif

	return true;
}

//////////////////////////////////////////////////////////////

bool PerformanceTimerDrift() {
	int checkIndex = 0;

	EsDateComponents start, end;

	EsDateNowUTC(&start);
	EsPerformanceTimerPush();

	for (uintptr_t i = 0; i < 50000000; i++) {
		EsCStringLength("Test"); 
	}

	double performanceTime = EsPerformanceTimerPop();
	EsDateNowUTC(&end);
	double mainTime = (DateToLinear(&end) - DateToLinear(&start)) / 1000.0;

	EsPrint("Performance timer: %F s.\n", performanceTime);
	EsPrint("Main timer: %F s.\n", mainTime);

	// TODO Improve the quality of the performance timer, if better timer sources are available, like the HPET.
	CHECK(EsCRTfabs(performanceTime - mainTime) / mainTime < 0.2); // Less than a 20% drift.
	return true;
}

//////////////////////////////////////////////////////////////

EsTextbox *textbox;

Array<char> master;
Array<Array<char>> undo;

void OffsetToLineAndByte(uintptr_t offset, int32_t *_line, int32_t *_byte) {
	int32_t line = 0, byte = 0;

	for (uintptr_t i = 0; i < offset; i++) {
		if (master[i] == '\n') {
			line++;
			byte = 0;
		} else {
			byte++;
		}
	}

	*_line = line;
	*_byte = byte;
}

bool Compare() {
	size_t bytes;
	char *contents = EsTextboxGetContents(textbox, &bytes);
	// EsPrint("\tContents: '%e'\n\tMaster:   '%e'\n", bytes, contents, arrlenu(master), master);
	if (bytes != master.Length()) return false;
	if (EsMemoryCompare(&master[0], contents, bytes)) return false;
	EsHeapFree(contents);
	return true;
}

void FakeUndoItem(const void *, EsUndoManager *manager, EsMessage *message) {
	if (message->type == ES_MSG_UNDO_INVOKE) {
		EsUndoPush(manager, FakeUndoItem, nullptr, 0); 
	}
}

void AddUndoItem() {
	Array<char> copy = {};
	copy.SetLength(master.Length());
	if (copy.Length()) EsMemoryCopy(&copy[0], &master[0], copy.Length());
	undo.Add(copy);
}

void Complete() {
	EsUndoPush(textbox->instance->undoManager, FakeUndoItem, nullptr, 0); 
	EsUndoEndGroup(textbox->instance->undoManager);
}

bool Insert(uintptr_t offset, const char *string, size_t stringBytes) {
	if (!stringBytes) return true;
	AddUndoItem();
	// EsPrint("Insert '%e' at %d.\n", stringBytes, string, offset);
	int32_t line, byte;
	OffsetToLineAndByte(offset, &line, &byte);
	EsTextboxSetSelection(textbox, line, byte, line, byte);
	EsTextboxInsert(textbox, string, stringBytes);
	master.InsertMany(offset, stringBytes);
	EsMemoryCopy(&master[offset], string, stringBytes);
	if (!Compare()) return false;
	Complete();
	return true;
}

bool Delete(uintptr_t from, uintptr_t to) {
	if (from == to) return true;
	AddUndoItem();
	// EsPrint("Delete from %d to %d.\n", from, to);
	int32_t fromLine, fromByte, toLine, toByte;
	OffsetToLineAndByte(from, &fromLine, &fromByte);
	OffsetToLineAndByte(to, &toLine, &toByte);
	EsTextboxSetSelection(textbox, fromLine, fromByte, toLine, toByte);
	EsTextboxInsert(textbox, 0, 0);
	if (to > from) master.DeleteMany(from, to - from);
	else master.DeleteMany(to, from - to);
	if (!Compare()) return false;
	Complete();
	return true;
}

bool TextboxEditOperations() {
	int checkIndex = 0;
	textbox = EsTextboxCreate(EsWindowCreate(_EsInstanceCreate(sizeof(EsInstance), nullptr), ES_WINDOW_PLAIN), ES_TEXTBOX_ALLOW_TABS | ES_TEXTBOX_MULTILINE);
	EsRandomSeed(10); 
	EsTextboxSetUndoManager(textbox, textbox->instance->undoManager);

	char *initialText = (char *) EsHeapAllocate(100000, false);
	for (uintptr_t i = 0; i < 100000; i++) initialText[i] = EsRandomU8() < 0x40 ? '\n' : ((EsRandomU8() % 26) + 'a');
	CHECK(Insert(0, initialText, 100000));
	EsHeapFree(initialText);

	for (uintptr_t i = 0; i < 10000; i++) {
		uint8_t action = EsRandomU8();

		if (action < 0x70) {
			size_t stringBytes = EsRandomU8() & 0x1F;
			char string[0x20];

			for (uintptr_t i = 0; i < stringBytes; i++) {
				string[i] = EsRandomU8() < 0x40 ? '\n' : ((EsRandomU8() % 26) + 'a');
			}

			CHECK(Insert(EsRandomU64() % (master.Length() + 1), string, stringBytes));
		} else if (action < 0xE0) {
			if (master.Length()) {
				CHECK(Delete(EsRandomU64() % master.Length(), EsRandomU64() % master.Length()));
			}
		} else {
			if (!EsUndoIsEmpty(textbox->instance->undoManager, false)) {
				// EsPrint("Undo.\n");
				EsUndoInvokeGroup(textbox->instance->undoManager, false);
				master.Free();
				master = undo.Last();
				undo.Pop();
				CHECK(Compare());
			}
		}
	}

	return true;
}

//////////////////////////////////////////////////////////////

struct {
	int a, b;
} testStruct = {
	.a = 1, .b = 2,
};

const int testVariable = 3;

bool DirectoryEnumerateChildrenRecursive(const char *path, size_t pathBytes) {
	EsDirectoryChild *buffer;
	ptrdiff_t count = EsDirectoryEnumerateChildren(path, pathBytes, &buffer);

	if (count < 0) {
		EsPrint("Error %i enumerating at path \"%s\".\n", (EsError) count, pathBytes, path);
		return false;
	}

	for (intptr_t i = 0; i < count; i++) {
		char *childPath = (char *) EsHeapAllocate(pathBytes + 1 + buffer[i].nameBytes, false);
		size_t childPathBytes = EsStringFormat(childPath, ES_STRING_FORMAT_ENOUGH_SPACE, "%s/%s", pathBytes, path, buffer[i].nameBytes, buffer[i].name);

		if (buffer[i].type == ES_NODE_FILE) {
			size_t dataBytes;
			EsError error;
			void *data = EsFileReadAll(childPath, childPathBytes, &dataBytes, &error);

			if (error != ES_SUCCESS) {
				EsPrint("Error %i reading path \"%s\".\n", (EsError) count, childPathBytes, childPath);
				return false;
			}

			if (dataBytes != (size_t) buffer[i].fileSize) {
				EsPrint("File size mismatch reading path \"%s\" (got %d from EsFileReadAll, got %d from EsDirectoryEnumerateChildren).\n", 
						childPathBytes, childPath, dataBytes, buffer[i].fileSize);
				return false;
			}

			EsHeapFree(data);
		} else if (buffer[i].type == ES_NODE_DIRECTORY) {
			if (!DirectoryEnumerateChildrenRecursive(childPath, childPathBytes)) {
				return false;
			}
		}
	}

	EsHeapFree(buffer);
	return true;
}

bool OldTests2018() {
	int checkIndex = 0;

	CHECK(testStruct.a == 1);
	CHECK(testStruct.b == 2);
	CHECK(testVariable == 3);
	testStruct.a += 3;
	CHECK(testStruct.a == 4);
	CHECK(testStruct.b == 2);
	CHECK(testVariable == 3);

	CHECK(DirectoryEnumerateChildrenRecursive(EsLiteral("0:")));

	for (int count = 16; count < 100; count += 30) {
		EsHandle handles[100];

		for (int i = 0; i < count; i++) {
			char buffer[256];
			size_t length = EsStringFormat(buffer, 256, "0:/TestFolder/%d", i);
			EsFileInformation node = EsFileOpen(buffer, length, ES_NODE_CREATE_DIRECTORIES | ES_NODE_FAIL_IF_FOUND | ES_FILE_WRITE);
			CHECK(node.error == ES_SUCCESS);
			handles[i] = node.handle;
		}

		for (int i = 0; i < count; i++) {
			CHECK(ES_SUCCESS == EsFileDelete(handles[i]));
		}

		for (int i = 0; i < count; i++) {
			EsHandleClose(handles[i]);
		}
	}

	{
		EsFileWriteAll(EsLiteral("0:/TestFolder/a.txt"), EsLiteral("hello"));
		EsFileWriteAll(EsLiteral("0:/b.txt"), EsLiteral("world"));
		CHECK(EsPathExists(EsLiteral("0:/TestFolder/a.txt")));
		CHECK(EsPathExists(EsLiteral("0:/b.txt")));
		CHECK(!EsPathExists(EsLiteral("0:/TestFolder/b.txt")));
		CHECK(!EsPathExists(EsLiteral("0:/a.txt")));
		CHECK(ES_SUCCESS == EsPathMove(EsLiteral("0:/TestFolder/a.txt"), EsLiteral("0:/a.txt"), ES_FLAGS_DEFAULT));
		CHECK(!EsPathExists(EsLiteral("0:/TestFolder/a.txt")));
		CHECK(EsPathExists(EsLiteral("0:/b.txt")));
		CHECK(!EsPathExists(EsLiteral("0:/TestFolder/b.txt")));
		CHECK(EsPathExists(EsLiteral("0:/a.txt")));
		CHECK(ES_ERROR_FILE_DOES_NOT_EXIST == EsPathMove(EsLiteral("0:/TestFolder/a.txt"), EsLiteral("0:/a.txt"), ES_FLAGS_DEFAULT));
		CHECK(ES_ERROR_FILE_ALREADY_EXISTS == EsPathMove(EsLiteral("0:/a.txt"), EsLiteral("0:/b.txt"), ES_FLAGS_DEFAULT));
		CHECK(ES_ERROR_FILE_ALREADY_EXISTS == EsPathMove(EsLiteral("0:/a.txt"), EsLiteral("0:/a.txt"), ES_FLAGS_DEFAULT));
		CHECK(ES_ERROR_VOLUME_MISMATCH == EsPathMove(EsLiteral("0:/"), EsLiteral("0:/TestFolder/TargetWithinSource"), ES_FLAGS_DEFAULT));
		CHECK(!EsPathExists(EsLiteral("0:/TestFolder/a.txt")));
		CHECK(EsPathExists(EsLiteral("0:/b.txt")));
		CHECK(!EsPathExists(EsLiteral("0:/TestFolder/b.txt")));
		CHECK(EsPathExists(EsLiteral("0:/a.txt")));
	}

	CHECK(DirectoryEnumerateChildrenRecursive(EsLiteral("0:")));

	{
		void *a = EsCRTmalloc(0x100000);
		CHECK(a);
		void *b = EsCRTrealloc(a, 0x1000);
		CHECK(b);
		void *c = EsCRTrealloc(b, 0x100000);
		CHECK(c);
		EsCRTfree(c);
	}

	{
		char b[] = "abcdef";
		CHECK(EsCRTstrlen(b) == 6);
		CHECK(EsCRTstrnlen(b, 3) == 3);
		CHECK(EsCRTstrnlen(b, 10) == 6);
	}

	{
		CHECK(EsCRTstrtol("\n\f\n -0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaAAAAAAAaaaaaa", nullptr, 0) == LONG_MIN);
		char *x = (char *) "+03", *y;
		CHECK(EsCRTstrtol(x, &y, 4) == 3 && y == x + 3);
	}

	{
		EsFileInformation node = EsFileOpen(EsLiteral("0:/ResizeFileTest.txt"), ES_FILE_WRITE | ES_NODE_FAIL_IF_FOUND);
		CHECK(node.error == ES_SUCCESS);

		// TODO Failing large file resizes.
#if 0
		EsFileResize(node.handle, (uint64_t) 0xFFFFFFFFFFFF);
#endif

		uint8_t buffer[512];

		for (uintptr_t i = 1; i < 128; i++) {
			for (uintptr_t j = 0; j < 512; j++) {
				buffer[j] = i;
			}

			// OSPrint("Resizing file to %d\n", i * 512);
			EsFileResize(node.handle, i * 512);
			// OSPrint("Write to %d\n", (i - 1) * 512);
			EsFileWriteSync(node.handle, (i - 1) * 512, 512, buffer);
		}

		for (uintptr_t i = 1; i < 128; i++) {
			// OSPrint("Read from %d\n", (i - 1) * 512);
			EsFileReadSync(node.handle, (i - 1) * 512, 512, buffer);

			for (uintptr_t j = 0; j < 512; j++) {
				CHECK(buffer[j] == i);
			}
		}

		for (uintptr_t i = 126; i > 0; i--) {
			// OSPrint("Resizing file to %d\n", i * 512);
			EsFileResize(node.handle, i * 512);
		}

		for (uintptr_t i = 1; i < 2; i++) {
			// OSPrint("Read from %d\n", (i - 1) * 512);
			EsFileReadSync(node.handle, (i - 1) * 512, 512, buffer);

			for (uintptr_t j = 0; j < 512; j++) {
				CHECK(buffer[j] == i);
			}
		}

		EsHandleClose(node.handle);
	}

	{
		EsFileInformation node = EsFileOpen(EsLiteral("0:/MapFile.txt"), ES_FILE_WRITE_SHARED);
		CHECK(node.error == ES_SUCCESS);
		EsFileResize(node.handle, 1048576);
		uint32_t *buffer = (uint32_t *) EsHeapAllocate(1048576, false);
		for (int i = 0; i < 262144; i++) buffer[i] = i;
		EsFileWriteSync(node.handle, 0, 1048576, buffer);
		EsFileReadSync(node.handle, 0, 1048576, buffer);
		for (uintptr_t i = 0; i < 262144; i++) CHECK(buffer[i] == i);
		EsFileInformation node2 = EsFileOpen(EsLiteral("0:/MapFile.txt"), ES_FILE_READ_SHARED);
		CHECK(node.error == ES_SUCCESS);
		uint32_t *pointer = (uint32_t *) EsMemoryMapObject(node2.handle, 0, ES_MEMORY_MAP_OBJECT_ALL, ES_MEMORY_MAP_OBJECT_READ_ONLY);
		CHECK(pointer);
		uint32_t *pointer2 = (uint32_t *) EsMemoryMapObject(node2.handle, 0, ES_MEMORY_MAP_OBJECT_ALL, ES_MEMORY_MAP_OBJECT_READ_ONLY);
		CHECK(pointer2);
		for (uintptr_t i = 4096; i < 262144; i++) CHECK(pointer[i] == buffer[i]);
		for (int i = 0; i < 262144; i++) buffer[i] = i + 100;
		EsFileWriteSync(node.handle, 0, 1048576, buffer);
		EsFileReadSync(node.handle, 0, 1048576, buffer);
		for (uintptr_t i = 0; i < 262144; i++) CHECK(buffer[i] == i + 100);
		for (uintptr_t i = 4096; i < 262144; i++) CHECK(pointer[i] == buffer[i]);
		for (uintptr_t i = 4096; i < 262144; i++) CHECK(pointer2[i] == buffer[i]);
		EsMemoryUnreserve(pointer);
		EsHandleClose(node.handle);
		EsHandleClose(node2.handle);
		EsMemoryUnreserve(pointer2);
	}

	{
		const char *path = "0:/OS/new_dir/test2.txt";
		EsFileInformation node = EsFileOpen(path, EsCStringLength(path), ES_FILE_WRITE | ES_NODE_CREATE_DIRECTORIES);
		CHECK(node.error == ES_SUCCESS);
		CHECK(ES_SUCCESS == EsFileResize(node.handle, 8));
		char buffer[8];
		buffer[0] = 'a';
		buffer[1] = 'b';
		EsFileWriteSync(node.handle, 0, 1, buffer);
		buffer[0] = 'b';
		buffer[1] = 'c';
		size_t bytesRead = EsFileReadSync(node.handle, 0, 8, buffer);
		CHECK(bytesRead == 8);
		CHECK(buffer[0] == 'a' && buffer[1] == 0 && buffer[2] == 0);
		CHECK(EsFileGetSize(node.handle) == 8);
		EsHandleClose(node.handle);
	}
	
	return true;
}

//////////////////////////////////////////////////////////////

bool HeapReallocate() {
	void *a = EsHeapReallocate(nullptr, 128, true);
	EsHeapValidate();
	a = EsHeapReallocate(a, 256, true);
	EsHeapValidate();
	a = EsHeapReallocate(a, 128, true);
	EsHeapValidate();
	a = EsHeapReallocate(a, 65536, true);
	EsHeapValidate();
	a = EsHeapReallocate(a, 128, true);
	EsHeapValidate();
	a = EsHeapReallocate(a, 128, true);
	EsHeapValidate();
	void *b = EsHeapReallocate(nullptr, 64, true);
	EsHeapValidate();
	void *c = EsHeapReallocate(nullptr, 64, true);
	EsHeapValidate();
	EsHeapReallocate(b, 0, true);
	EsHeapValidate();
	a = EsHeapReallocate(a, 128 + 88, true);
	EsHeapValidate();
	a = EsHeapReallocate(a, 128, true);
	EsHeapValidate();
	EsHeapReallocate(a, 0, true);
	EsHeapValidate();
	EsHeapReallocate(c, 0, true);
	EsHeapValidate();
	return true;
}

//////////////////////////////////////////////////////////////

bool ArenaRandomAllocations() {
	int checkIndex = 0;
	Arena arena = {};
	ArenaInitialise(&arena, 3, sizeof(int));
	Array<void *> allocations = {};
	EsRandomSeed(20);

	for (uintptr_t i = 0; i < 500000; i++) {
		if ((EsRandomU8() & 1) || !allocations.Length()) {
			void *allocation = ArenaAllocate(&arena, false);
			for (uintptr_t i = 0; i < allocations.Length(); i++) CHECK(allocations[i] != allocation);
			allocations.Add(allocation);
		} else {
			int index = EsRandomU64() % allocations.Length();
			ArenaFree(&arena, allocations[index]);
			allocations.DeleteSwap(index);
		}
	}

	return true;
}

//////////////////////////////////////////////////////////////

bool rangeSetCheck[1000];
RangeSet rangeSet = {};

bool RangeSetModify(bool set, int x, int y) {
	for (int i = x; i < y; i++) {
		rangeSetCheck[i] = set;
	}

	if (set) {
		if (!rangeSet.Set(x, y, nullptr, true)) {
			return false;
		}
	} else {
		if (!rangeSet.Clear(x, y, nullptr, true)) {
			return false;
		}
	}

	for (uintptr_t i = 0; i < sizeof(rangeSetCheck); i++) {
		if (rangeSetCheck[i]) {
			if (!rangeSet.Find(i, false)) {
				return false;
			}
		} else {
			if (rangeSet.Find(i, false)) {
				return false;
			}
		}
	}

	return true;
}

bool RangeSetTests() {
	int checkIndex = 0;

	CHECK(RangeSetModify(true, 2, 3));
	CHECK(RangeSetModify(true, 4, 5));
	CHECK(RangeSetModify(true, 0, 1));
	CHECK(RangeSetModify(true, 1, 2));
	CHECK(RangeSetModify(true, 3, 4));
	CHECK(RangeSetModify(true, 10, 15));
	CHECK(RangeSetModify(true, 4, 10));
	CHECK(RangeSetModify(true, 20, 30));
	CHECK(RangeSetModify(true, 15, 21));
	CHECK(RangeSetModify(true, 50, 55));
	CHECK(RangeSetModify(true, 60, 65));
	CHECK(RangeSetModify(true, 40, 70));
	CHECK(RangeSetModify(true, 0, 100));

	CHECK(RangeSetModify(false, 50, 60));
	CHECK(RangeSetModify(false, 55, 56));
	CHECK(RangeSetModify(false, 50, 55));
	CHECK(RangeSetModify(false, 55, 60));
	CHECK(RangeSetModify(false, 50, 60));
	CHECK(RangeSetModify(false, 49, 60));
	CHECK(RangeSetModify(false, 49, 61));

	CHECK(RangeSetModify(true, 50, 51));
	CHECK(RangeSetModify(false, 48, 62));
	CHECK(RangeSetModify(true, 50, 51));
	CHECK(RangeSetModify(false, 48, 62));
	CHECK(RangeSetModify(true, 50, 51));
	CHECK(RangeSetModify(true, 52, 53));
	CHECK(RangeSetModify(false, 48, 62));
	CHECK(RangeSetModify(true, 50, 51));
	CHECK(RangeSetModify(true, 52, 53));
	CHECK(RangeSetModify(false, 47, 62));
	CHECK(RangeSetModify(true, 50, 51));
	CHECK(RangeSetModify(true, 52, 53));
	CHECK(RangeSetModify(false, 47, 63));
	CHECK(RangeSetModify(true, 50, 51));
	CHECK(RangeSetModify(true, 52, 53));
	CHECK(RangeSetModify(false, 46, 64));

	EsRandomSeed(20);

	for (uintptr_t i = 0; i < 100000; i++) {
		int a = EsRandomU64() % 1000, b = EsRandomU64() % 1000;
		if (b <= a) continue;
		CHECK(RangeSetModify(EsRandomU8() & 1, a, b));
	}

	return true;
}

//////////////////////////////////////////////////////////////

bool UTF8Tests() {
	int checkIndex = 0;

	// Strings taken from https://www.cl.cam.ac.uk/~mgk25/ucs/examples/UTF-8-test.txt under CC BY 4.0.

	const char *goodStrings[] = {
		"\xCE\xBA\xE1\xBD\xB9\xCF\x83\xCE\xBC\xCE\xB5",
		"\x01",
		"\xC2\x80",
		"\xE0\xA0\x80",
		"\xF0\x90\x80\x80",
		"\x7F",
		"\xDF\xBF",
		"\xEF\xBF\xBF", 
		"\xF7\xBF\xBF\xBF", 
		"\xED\x9F\xBF", 
		"\xEE\x80\x80", 
		"\xEF\xBF\xBD", 
		"\xF4\x8F\xBF\xBF", 
		"\xF4\x90\x80\x80",

		// Overlong sequences for non-ASCII characters are allowed.
		"\xE0\x9F\xBF",
		"\xF0\x8F\xBF\xBF",

		// Surrogate characters are allowed (for compatability with things like NTFS).
		"\xED\xA0\x80",
		"\xED\xAD\xBF",
		"\xED\xAE\x80",
		"\xED\xAF\xBF",
		"\xED\xB0\x80",
		"\xED\xBE\x80",
		"\xED\xBF\xBF",
		"\xED\xA0\x80\xED\xB0\x80",
		"\xED\xA0\x80\xED\xBF\xBF",
		"\xED\xAD\xBF\xED\xB0\x80",
		"\xED\xAD\xBF\xED\xBF\xBF",
		"\xED\xAE\x80\xED\xB0\x80",
		"\xED\xAE\x80\xED\xBF\xBF",
		"\xED\xAF\xBF\xED\xB0\x80",
		"\xED\xAF\xBF\xED\xBF\xBF",
	};

	const char *badStrings[] = {
		// We don't support 5 and 6 byte characters, as they shouldn't appear in Unicode text.
		"\xF8\x88\x80\x80\x80",
		"\xFC\x84\x80\x80\x80\x80",
		"\xFB\xBF\xBF\xBF\xBF", 
		"\xFD\xBF\xBF\xBF\xBF\xBF",

		"\x80",
		"\xBF",
		"\x80\xBF",
		"\x80\xBF\x80",
		"\x80\xBF\x80\xBF",
		"\x80\xBF\x80\xBF\x80",
		"\x80\xBF\x80\xBF\x80\xBF\x80",
		"\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8A\x8B\x8C\x8D\x8E\x8F\x90\x91\x92\x93\x94\x95\x96"
			"\x97\x98\x99\x9A\x9B\x9C\x9D\x9E\x9F\xA0\xA1\xA2\xA3\xA4\xA5\xA6\xA7\xA8\xA9\xAA\xAB"
			"\xAC\xAD\xAE\xAF\xB0\xB1\xB2\xB3\xB4\xB5\xB6\xB7\xB8\xB9\xBA\xBB\xBC\xBD\xBE\xBF",
		"\xC0\x20\xC1\x20\xC2\x20\xC3\x20\xC4\x20\xC5\x20\xC6\x20\xC7\x20\xC8\x20\xC9\x20\xCA\x20\xCB"
			"\x20\xCC\x20\xCD\x20\xCE\x20\xCF\x20\xD0\x20\xD1\x20\xD2\x20\xD3\x20\xD4\x20\xD5\x20"
			"\xD6\x20\xD7\x20\xD8\x20\xD9\x20\xDA\x20\xDB\x20\xDC\x20\xDD\x20\xDE\x20\xDF\x20",
		"\xE0\x20\xE1\x20\xE2\x20\xE3\x20\xE4\x20\xE5\x20\xE6\x20\xE7\x20\xE8\x20\xE9\x20\xEA\x20\xEB"
			"\x20\xEC\x20\xED\x20\xEE\x20\xEF\x20",
		"\xF0\x20\xF1\x20\xF2\x20\xF3\x20\xF4\x20\xF5\x20\xF6\x20\xF7\x20",
		"\xF8\x20\xF9\x20\xFA\x20\xFB\x20",
		"\xFC\x20\xFD\x20",
		"\xC0",
		"\xE0\x80",
		"\xF0\x80\x80",
		"\xF8\x80\x80\x80",
		"\xFC\x80\x80\x80\x80",
		"\xDF",
		"\xEF\xBF",
		"\xF7\xBF\xBF",
		"\xFB\xBF\xBF\xBF",
		"\xFD\xBF\xBF\xBF\xBF",
		"\xC0\xE0\x80\xF0\x80\x80\xF8\x80\x80\x80\xFC\x80\x80\x80\x80\xDF\xEF\xBF\xF7\xBF\xBF\xFB\xBF"
			"\xBF\xBF\xFD\xBF\xBF\xBF\xBF",
		"\xFE",
		"\xFF",
		"\xFE\xFE\xFF\xFF",
		"\xC0\xAF",
		"\xE0\x80\xAF",
		"\xF0\x80\x80\xAF",
		"\xF8\x80\x80\x80\xAF",
		"\xFC\x80\x80\x80\x80\xAF",
		"\xC1\xBF",
		"\xC0\x80",
		"\xE0\x80\x80",
		"\xF0\x80\x80\x80",
		"\xF8\x80\x80\x80\x80",
		"\xFC\x80\x80\x80\x80\x80",
	};

	for (uintptr_t i = 0; i < sizeof(goodStrings) / sizeof(goodStrings[0]); i++) {
		CHECK(EsUTF8IsValid(goodStrings[i], -1));

		const char *position = goodStrings[i];
		
		while (*position) {
			CHECK(utf8_value(position));
			position = utf8_advance(position);
			CHECK(position);
		}

		while (position != goodStrings[i]) {
			position = utf8_retreat(position);
			CHECK(position);
		}
	}

	for (uintptr_t i = 0; i < sizeof(badStrings) / sizeof(badStrings[0]); i++) {
		CHECK(!EsUTF8IsValid(badStrings[i], -1));
	}

	return true;
}

//////////////////////////////////////////////////////////////

EsHandle pipeRead, pipeWrite;

void PipeTestsThread2(EsGeneric) {
	for (uint16_t i = 0; i < 1000; i++) {
		EsPipeWrite(pipeWrite, &i, sizeof(i));
	}

	uint16_t *buffer = (uint16_t *) EsHeapAllocate(10000, false);

	for (uint16_t i = 0; i < 1000; i++) {
		for (uintptr_t i = 0; i < 5000; i++) buffer[i] = i;
		EsPipeWrite(pipeWrite, buffer, 10000);
	}

	uint16_t s = 0x1234;
	EsPipeWrite(pipeWrite, &s, sizeof(s));
	EsSleep(2000);
	s = 0xFEDC;
	EsPipeWrite(pipeWrite, &s, sizeof(s));
	EsHandleClose(pipeWrite);

	EsHeapFree(buffer);
}

void PipeTestsThread3(EsGeneric) {
	uint8_t data[200];
	EsPipeRead(pipeRead, data, sizeof(data), false);
	EsHandleClose(pipeRead);
}

bool PipeTests() {
	EsPipeCreate(&pipeRead, &pipeWrite);

	int checkIndex = 0;
	EsThreadInformation information;
	CHECK(EsThreadCreate(PipeTestsThread2, &information, nullptr) == ES_SUCCESS);
	EsHandleClose(information.handle);

	for (uint16_t i = 0; i < 1000; i++) {
		uint16_t j;
		CHECK(sizeof(j) == EsPipeRead(pipeRead, &j, sizeof(j), true));
		CHECK(i == j);
	}

	uint16_t *buffer = (uint16_t *) EsHeapAllocate(10000, false);

	for (uint16_t i = 0; i < 1000; i++) {
		EsMemoryZero(buffer, 10000);
		uintptr_t position = 0;
		
		while (position < 10000) {
			size_t read = EsPipeRead(pipeRead, (uint8_t *) buffer + position, 10000 - position, i >= 500);
			if (i < 500) CHECK(read == 10000);
			CHECK(read);
			position += read;
		}

		CHECK(position == 10000);

		for (uintptr_t i = 0; i < 5000; i++) CHECK(buffer[i] == i);
	}

	EsSleep(1000);

	uint32_t s = 0x5678ABCD;
	CHECK(2 == EsPipeRead(pipeRead, &s, sizeof(s), true));
	CHECK(s == 0x56781234); // TODO Big endian support.
	s = 0x5678ABCD;
	CHECK(2 == EsPipeRead(pipeRead, &s, sizeof(s), false));
	CHECK(s == 0x5678FEDC); // TODO Big endian support.
	CHECK(0 == EsPipeRead(pipeRead, &s, sizeof(s), false));

	EsHandleClose(pipeRead);

	EsPipeCreate(&pipeRead, &pipeWrite);
	CHECK(EsThreadCreate(PipeTestsThread3, &information, nullptr) == ES_SUCCESS);
	EsHandleClose(information.handle);
	size_t written = EsPipeWrite(pipeWrite, buffer, 10000);
	CHECK(written > 0 && written < 10000); // The actual amountn written depends on the size of the internal pipe buffer, and whether the read happens in time.
	CHECK(0 == EsPipeWrite(pipeWrite, buffer, 10000));
	EsHandleClose(pipeWrite);

	EsHeapFree(buffer);

	return true;
}

//////////////////////////////////////////////////////////////

#endif

const Test tests[] = {
	TEST(BasicFileOperations, 60),
	TEST(CRTMathFunctions, 60),
	TEST(CRTStringFunctions, 60),
	TEST(CRTOtherFunctions, 60),
	TEST(PerformanceTimerDrift, 60),
	TEST(TextboxEditOperations, 240),
	TEST(OldTests2018, 60),
	TEST(HeapReallocate, 60),
	TEST(ArenaRandomAllocations, 60),
	TEST(RangeSetTests, 60),
	TEST(UTF8Tests, 60),
	TEST(PipeTests, 60),
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
