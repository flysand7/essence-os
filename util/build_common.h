#define BINUTILS_VERSION "2.36.1"
#define GCC_VERSION "11.1.0"

#ifndef OS_ESSENCE

#include <sys/stat.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

typedef struct EsINIState {
	char *buffer, *sectionClass, *section, *key, *value;
	size_t bytes, sectionClassBytes, sectionBytes, keyBytes, valueBytes;
} EsINIState;

#include "../shared/ini.h"

#endif

#include "stb_ds.h"

#define INI_READ_BOOL(x, y) if (0 == strcmp(#x, s.key)) y = !!atoi(s.value)
#define INI_READ_STRING(x, y) if (0 == strcmp(#x, s.key)) snprintf(y, sizeof(y), "%s", s.value)
#define INI_READ_STRING_PTR(x, y) if (0 == strcmp(#x, s.key)) y = s.value

typedef struct ApplicationDependencyList {
	char **files;
	time_t timeStamp;
	bool optimised;
} ApplicationDependencyList;

typedef struct ApplicationDependencies {
	char *key;
	ApplicationDependencyList value;
} ApplicationDependencies;

typedef struct FontFile {
	const char *type;
	const char *path;
} FontFile;

typedef struct BuildFont {
	const char *name;
	const char *license;
	const char *category;
	const char *scripts;
	FontFile *files;
} BuildFont;

ApplicationDependencies *applicationDependencies;
time_t buildStartTimeStamp;
bool _optimisationsEnabled;
bool forceRebuild;
uint64_t configurationHash;

void *LoadFile(const char *path, size_t *length) {
#ifdef OS_ESSENCE
	size_t path2Bytes;
	char *path2 = EsPOSIXConvertPath(path, &path2Bytes, true);
	size_t bytes;
	void *result = EsFileReadAll(path2, path2Bytes, &bytes, NULL);
	EsHeapFree(path2, 0, NULL);
	if (!result) return NULL;
	void *buffer = malloc(bytes + 1);
	memcpy(buffer, result, bytes);
	EsHeapFree(result, 0, NULL);
	((uint8_t *) buffer)[bytes] = 0;
	if (length) *length = bytes;
	return buffer;
#else
	FILE *file = fopen(path, "rb");
	if (!file) return NULL;
	fseek(file, 0, SEEK_END);
	size_t fileSize = ftell(file);
	fseek(file, 0, SEEK_SET);
	char *buffer = (char *) malloc(fileSize + 1);
	buffer[fileSize] = 0;
	fread(buffer, 1, fileSize, file);
	fclose(file);
	if (length) *length = fileSize;
	return buffer;
#endif
}

bool IsStringEqual(const char *string, size_t stringBytes, const char *cLiteral) {
	return stringBytes == strlen(cLiteral) && 0 == memcmp(string, cLiteral, stringBytes);
}

bool CheckDependencies(const char *applicationName) {
#ifdef OS_ESSENCE
	// TODO.
	return true;
#else
	bool needsRebuild = false;
	ApplicationDependencyList dependencies = shget(applicationDependencies, applicationName);

	if (!dependencies.timeStamp || dependencies.optimised != _optimisationsEnabled || forceRebuild) {
		needsRebuild = true;
	}

	for (int i = 0; !needsRebuild && i < arrlen(dependencies.files); i++) {
		struct stat s;

		// printf("%s, %s\n", applicationName, dependencies.files[i]);

		if (stat(dependencies.files[i], &s) || s.st_mtime > dependencies.timeStamp) {
			needsRebuild = true;
			break;
		}
	}

	if (needsRebuild) {
		for (int i = 0; i < arrlen(dependencies.files); i++) {
			free(dependencies.files[i]);
		}

		arrfree(dependencies.files);
		(void) shdel(applicationDependencies, applicationName);
	}

	return needsRebuild;
#endif
}

void ParseDependencies(const char *dependencyFile, const char *applicationName, bool append) {
#ifdef OS_ESSENCE
	// TODO.
#else
	char *dependencies = (char *) LoadFile(dependencyFile, NULL);

	if (!dependencies) {
		return;
	}

	ApplicationDependencyList list = {};

	if (append) {
		list = shget(applicationDependencies, applicationName);
	}

	int i = 0;

	while (dependencies[i++] != ':');

	for (; dependencies[i]; i++) {
		if (dependencies[i] == '/' || isalpha(dependencies[i])) {
			int j = i; for (; dependencies[j] && !isspace(dependencies[j]); j++);
			char *string = (char *) malloc(j - i + 1);
			memcpy(string, dependencies + i, j - i);
			string[j - i] = 0;

			if (0 == memcmp(string, "/usr", 4)) {
				// Assume system header files do not change.
			} else {
				arrput(list.files, string);
			}

			// printf("%s->%s\n", applicationName, string);
			i = j - 1;
		}
	}

	struct stat s;

	if (stat(dependencyFile, &s) == 0) {
		list.timeStamp = s.st_mtime;
		if (buildStartTimeStamp < s.st_mtime) list.timeStamp = buildStartTimeStamp;
		list.optimised = _optimisationsEnabled;
		shput(applicationDependencies, applicationName, list);
	}

	free(dependencies);
#endif
}

void DependenciesListRead() {
	EsINIState s = { .buffer = (char *) LoadFile("bin/dependencies.ini", &s.bytes) };
	char *start = s.buffer;
	if (!start) return;

	char *key = NULL;
	ApplicationDependencyList list = {};

	while (EsINIParse(&s)) {
		EsINIZeroTerminate(&s);

		if (0 == strcmp(s.section, "general")) {
			if (0 == strcmp(s.key, "configuration_hash")) {
				uint64_t previousConfigurationHash = strtoul(s.value, NULL, 10);

				if (previousConfigurationHash != configurationHash) {
					shfree(applicationDependencies);
					sh_new_strdup(applicationDependencies);
					return;
				}
			}
		} else if (s.keyBytes) {
			key = s.section;
		}

		if (0 == strcmp(s.key, "time_stamp")) {
			list.timeStamp = strtoul(s.value, NULL, 10);
		} else if (0 == strcmp(s.key, "file")) {
			arrput(list.files, strdup(s.value));
		}

		if (!EsINIPeek(&s) || !s.keyBytes) {
			if (key) {
				shput(applicationDependencies, key, list);
			}

			key = NULL;
			memset(&list, 0, sizeof(list));
		}
	}

	free(start);
}

void DependenciesListWrite() {
#ifdef OS_ESSENCE
	// TODO.
#else
	FILE *f = fopen("bin/dependencies.ini", "wb");

	fprintf(f, "[general]\nconfiguration_hash=%lu\n", configurationHash);

	for (uintptr_t i = 0; i < shlenu(applicationDependencies); i++) {
		ApplicationDependencyList *list = &applicationDependencies[i].value;

		fprintf(f, "[%s]\ntime_stamp=%lu\n", applicationDependencies[i].key, list->timeStamp);

		for (uintptr_t j = 0; j < arrlenu(list->files); j++) {
			fprintf(f, "file=%s\n", list->files[j]);
		}
	}

	fclose(f);
#endif
}

typedef union OptionVariant {
	bool b;
	char *s;
} OptionVariant;

typedef struct Option {
	const char *id;
#define OPTION_TYPE_BOOL (1)
#define OPTION_TYPE_STRING (2)
	int type;
	OptionVariant defaultState;
	OptionVariant state;
	bool useDefaultState;
	const char *warning;
} Option;

Option options[] = {
	{ "Driver.ACPI", OPTION_TYPE_BOOL, { .b = true } },
	{ "Driver.AHCI", OPTION_TYPE_BOOL, { .b = true } },
	{ "Driver.BGA", OPTION_TYPE_BOOL, { .b = true } },
	{ "Driver.EssenceFS", OPTION_TYPE_BOOL, { .b = true } },
	{ "Driver.Ext2", OPTION_TYPE_BOOL, { .b = true } },
	{ "Driver.FAT", OPTION_TYPE_BOOL, { .b = true } },
	{ "Driver.HDAudio", OPTION_TYPE_BOOL, { .b = true } },
	{ "Driver.I8254x", OPTION_TYPE_BOOL, { .b = true } },
	{ "Driver.IDE", OPTION_TYPE_BOOL, { .b = true } },
	{ "Driver.ISO9660", OPTION_TYPE_BOOL, { .b = true } },
	{ "Driver.NTFS", OPTION_TYPE_BOOL, { .b = false }, .warning = "The NTFS driver has not been updated to work with the new FS layer." },
	{ "Driver.NVMe", OPTION_TYPE_BOOL, { .b = true } },
	{ "Driver.Networking", OPTION_TYPE_BOOL, { .b = true } },
	{ "Driver.PCI", OPTION_TYPE_BOOL, { .b = true } },
	{ "Driver.PS2", OPTION_TYPE_BOOL, { .b = true } },
	{ "Driver.Root", OPTION_TYPE_BOOL, { .b = true } },
	{ "Driver.SVGA", OPTION_TYPE_BOOL, { .b = true } },
	{ "Driver.USB", OPTION_TYPE_BOOL, { .b = true } },
	{ "Driver.USBBulk", OPTION_TYPE_BOOL, { .b = true } },
	{ "Driver.USBHID", OPTION_TYPE_BOOL, { .b = true } },
	{ "Driver.xHCI", OPTION_TYPE_BOOL, { .b = true } },
	{ "Flag.ENABLE_POSIX_SUBSYSTEM", OPTION_TYPE_BOOL, { .b = false } },
	{ "Flag.DEBUG_BUILD", OPTION_TYPE_BOOL, { .b = true } },
	{ "Flag.USE_SMP", OPTION_TYPE_BOOL, { .b = true } },
	{ "Flag.BGA_RESOLUTION_WIDTH", OPTION_TYPE_STRING, { .s = "1600" } },
	{ "Flag.BGA_RESOLUTION_HEIGHT", OPTION_TYPE_STRING, { .s = "900" } },
	{ "Flag.VGA_TEXT_MODE", OPTION_TYPE_BOOL, { .b = false } },
	{ "Flag.CHECK_FOR_NOT_RESPONDING", OPTION_TYPE_BOOL, { .b = true } },
	{ "Flag._ALWAYS_USE_VBE", OPTION_TYPE_BOOL, { .b = false } },
	{ "Dependency.ACPICA", OPTION_TYPE_BOOL, { .b = true } },
	{ "Dependency.stb_image", OPTION_TYPE_BOOL, { .b = true } },
	{ "Dependency.stb_image_write", OPTION_TYPE_BOOL, { .b = true } },
	{ "Dependency.stb_sprintf", OPTION_TYPE_BOOL, { .b = true } },
	{ "Dependency.HarfBuzz", OPTION_TYPE_BOOL, { .b = true } },
	{ "Dependency.FreeType", OPTION_TYPE_BOOL, { .b = true } },
	{ "Emulator.AHCI", OPTION_TYPE_BOOL, { .b = true } },
	{ "Emulator.ATA", OPTION_TYPE_BOOL, { .b = false } },
	{ "Emulator.NVMe", OPTION_TYPE_BOOL, { .b = false }, .warning = "Recent versions of Qemu have trouble booting from NVMe drives." },
	{ "Emulator.CDROMImage", OPTION_TYPE_STRING, { .s = NULL } },
	{ "Emulator.USBImage", OPTION_TYPE_STRING, { .s = NULL } },
	{ "Emulator.USBHostVendor", OPTION_TYPE_STRING, { .s = NULL } },
	{ "Emulator.USBHostProduct", OPTION_TYPE_STRING, { .s = NULL } },
	{ "Emulator.RunWithSudo", OPTION_TYPE_BOOL, { .b = false } },
	{ "Emulator.Audio", OPTION_TYPE_BOOL, { .b = false } },
	{ "Emulator.MemoryMB", OPTION_TYPE_STRING, { .s = "1024" } },
	{ "Emulator.Cores", OPTION_TYPE_STRING, { .s = "1" } },
	{ "Emulator.PrimaryDriveMB", OPTION_TYPE_STRING, { .s = "1024" } },
	{ "Emulator.SecondaryDriveMB", OPTION_TYPE_STRING, { .s = NULL } },
	{ "General.first_application", OPTION_TYPE_STRING, { .s = NULL } },
	{ "General.wallpaper", OPTION_TYPE_STRING, { .s = NULL } },
	{ "General.installation_state", OPTION_TYPE_STRING, { .s = "0" } },
};

char *previousOptionsBuffer;

void LoadOptions() {
	free(previousOptionsBuffer);
	EsINIState s = { .buffer = (char *) LoadFile("bin/config.ini", &s.bytes) };
	previousOptionsBuffer = s.buffer;

	for (uintptr_t i = 0; i < sizeof(options) / sizeof(options[0]); i++) {
		options[i].state = options[i].defaultState;
		options[i].useDefaultState = true;

		if (options[i].type == OPTION_TYPE_STRING && options[i].state.s) {
			options[i].state.s = strdup(options[i].state.s);
		}
	}

	while (s.buffer && EsINIParse(&s)) {
		EsINIZeroTerminate(&s);

		for (uintptr_t i = 0; i < sizeof(options) / sizeof(options[0]); i++) {
			if (options[i].id && 0 == strcmp(options[i].id, s.key)) {
				if (options[i].type == OPTION_TYPE_BOOL) {
					options[i].state.b = atoi(s.value);
				} else if (options[i].type == OPTION_TYPE_STRING) {
					options[i].state.s = strdup(s.value);
				} else {
					// TODO.
				}

				options[i].useDefaultState = false;
				break;
			}
		}
	}
}

bool IsOptionEnabled(const char *name) {
	for (uintptr_t i = 0; i < sizeof(options) / sizeof(options[0]); i++) {
		if (options[i].id && 0 == strcmp(options[i].id, name)) {
			return options[i].state.b;
		}
	}

	return false;
}

const char *GetOptionString(const char *name) {
	for (uintptr_t i = 0; i < sizeof(options) / sizeof(options[0]); i++) {
		if (options[i].id && 0 == strcmp(options[i].id, name)) {
			return options[i].state.s;
		}
	}

	return "";
}

bool IsDriverEnabled(const char *name) {
	for (uintptr_t i = 0; i < sizeof(options) / sizeof(options[0]); i++) {
		if (options[i].id && 0 == memcmp(options[i].id, "Driver.", 7) && 0 == strcmp(options[i].id + 7, name)) {
			return options[i].state.b;
		}
	}

	return false;
}

#ifdef PARALLEL_BUILD

volatile uint8_t spinnerRunning;
pthread_t spinnerThread;

void *SpinnerThread(void *_unused) {
	(void) _unused;

	char spinner[4] = { '/', '-', '\\', '|' };
	int index = 0;

	fflush(stdout);

	do {
		fprintf(stderr, "\033[0;36m[%c]\033[0m Compiling...\n", spinner[index]);
		index = (index + 1) % 4;
		struct timespec sleepTime;
		sleepTime.tv_sec = 0;
		sleepTime.tv_nsec = 1000000 * 200;
		nanosleep(&sleepTime, NULL);
		fprintf(stderr, "\033[A");
	} while (__sync_val_compare_and_swap(&spinnerRunning, 2, 2) /* use sync function to silence thread sanitizer warning */);

	fprintf(stderr, "\033[K");
	return NULL;
}

void StopSpinner() {
	if (spinnerThread) {
		__sync_fetch_and_sub(&spinnerRunning, 1);
		pthread_join(spinnerThread, NULL);
		spinnerThread = 0;
	}
}

void StartSpinner() {
	__sync_fetch_and_add(&spinnerRunning, 1);
	pthread_create(&spinnerThread, NULL, SpinnerThread, NULL);
}

#endif
