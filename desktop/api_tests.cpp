#ifdef API_TESTS_FOR_RUNNER

#define TEST(_callback) { .cName = #_callback }
typedef struct Test { const char *cName; } Test;

#else

#define ES_PRIVATE_APIS
#include <essence.h>
#include <shared/crc.h>

#define TEST(_callback) { .callback = _callback }
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
