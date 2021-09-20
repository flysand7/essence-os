// TODO Better configuration over what files are imported to the drive image.
// TODO Make build_core responsible for generating the header.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef OS_ESSENCE

#define ES_CRT_WITHOUT_PREFIX
#include <essence.h>
#include <bits/syscall.h>

typedef struct File {
	bool error, ready;
	EsHandle handle;
	EsFileOffset offset;
} File;

#define FileSeek(file, _offset) (file.offset = _offset)
#define FileRead(file, size, buffer) (_FileRead(file.handle, &file.offset, size, buffer))
#define FileWrite(file, size, buffer) (_FileWrite(file.handle, &file.offset, size, buffer))
#define FileClose(file) EsHandleClose(file.handle)
#define FilePrintFormat(file, ...) (_FilePrintFormat(&file, __VA_ARGS__))

void Log(const char *format, ...) {
	va_list arguments;
	va_start(arguments, format);
	char buffer[4096];
	int bytes = EsCRTvsnprintf(buffer, sizeof(buffer), format, arguments); 
	EsAssert(bytes < sizeof(buffer));
	va_end(arguments);
	EsPOSIXSystemCall(SYS_write, (intptr_t) 1, (intptr_t) buffer, (intptr_t) bytes, 0, 0, 0);
}

File FileOpen(const char *path, char mode) {
	size_t path2Bytes;
	char *path2 = EsPOSIXConvertPath(path, &path2Bytes, true);
	EsFileInformation information = EsFileOpen(path2, path2Bytes, mode == 'r' ? (ES_FILE_READ | ES_NODE_FAIL_IF_NOT_FOUND) : ES_FILE_WRITE);
	EsHeapFree(path2, 0, NULL);
	if (information.error != ES_SUCCESS) return (File) { .error = true };
	if (mode == 'w') EsFileResize(information.handle, 0);
	return (File) { .ready = true, .handle = information.handle };
}

EsFileOffset _FileRead(EsHandle handle, EsFileOffset *offset, size_t bytes, void *buffer) {
	bytes = EsFileReadSync(handle, *offset, bytes, buffer);
	if (bytes > 0) *offset += bytes;
	return bytes;
}

EsFileOffset _FileWrite(EsHandle handle, EsFileOffset *offset, size_t bytes, void *buffer) {
	bytes = EsFileWriteSync(handle, *offset, bytes, buffer);
	if (bytes > 0) *offset += bytes;
	return bytes;
}

void _FilePrintFormat(File *file, const char *format, ...) {
	va_list arguments;
	va_start(arguments, format);
	char buffer[4096];
	int bytes = EsCRTvsnprintf(buffer, sizeof(buffer), format, arguments); 
	EsAssert(bytes < sizeof(buffer));
	va_end(arguments);
	FileWrite((*file), bytes, buffer);
}

#define _exit(x)          EsPOSIXSystemCall(SYS_exit_group, (intptr_t) x, 0, 0, 0, 0, 0)
#define close(x)          EsPOSIXSystemCall(SYS_close, (intptr_t) x, 0, 0, 0, 0, 0)
#define dup2(x, y)        EsPOSIXSystemCall(SYS_dup2, (intptr_t) x, (intptr_t) y, 0, 0, 0, 0)
#define execve(x, y, z)   EsPOSIXSystemCall(SYS_execve, (intptr_t) x, (intptr_t) y, (intptr_t) z, 0, 0, 0)
#define exit(x)           EsPOSIXSystemCall(SYS_exit_group, (intptr_t) x, 0, 0, 0, 0, 0)
#define pipe(x)           EsPOSIXSystemCall(SYS_pipe, (intptr_t) x, 0, 0, 0, 0, 0)
#define read(x, y, z)     EsPOSIXSystemCall(SYS_read, (intptr_t) x, (intptr_t) y, (intptr_t) z, 0, 0, 0)
#define rename(x, y)      EsPOSIXSystemCall(SYS_rename, (intptr_t) x, (intptr_t) y, 0, 0, 0, 0)
#define truncate(x, y)    EsPOSIXSystemCall(SYS_truncate, (intptr_t) x, (intptr_t) y, 0, 0, 0, 0)
#define unlink(x)         EsPOSIXSystemCall(SYS_unlink, (intptr_t) x, 0, 0, 0, 0, 0)
#define vfork()           EsPOSIXSystemCall(SYS_vfork, 0, 0, 0, 0, 0, 0)
#define wait4(x, y, z, w) EsPOSIXSystemCall(SYS_wait4, (intptr_t) x, (intptr_t) y, (intptr_t) z, (intptr_t) w, 0, 0)

typedef uint64_t pid_t;

typedef uint64_t time_t;
time_t time(time_t *timer) { return 0; } // TODO.

#else

#include <assert.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifdef PARALLEL_BUILD
#include <pthread.h>
#endif

typedef struct File {
	bool error, ready;
	FILE *f;
} File;

File FileOpen(const char *path, char mode) {
	FILE *f = NULL;
	if (mode == 'r') f = fopen(path, "rb");
	if (mode == 'w') f = fopen(path, "wb");
	if (mode == '+') f = fopen(path, "r+b");
	return (File) { .error = f == NULL, .ready = f != NULL, .f = f };
}

#define FileSeek(file, offset) (fseek(file.f, offset, SEEK_SET))
#define FileRead(file, size, buffer) (fread(buffer, 1, size, file.f))
#define FileWrite(file, size, buffer) (fwrite(buffer, 1, size, file.f))
#define FileClose(file) fclose(file.f)
#define FilePrintFormat(file, ...) fprintf(file.f, __VA_ARGS__)

#define Log(...) fprintf(stderr, __VA_ARGS__)

#define EsFileOffset uint64_t

#endif

#define EsFSError() exit(1)

#include "../shared/hash.cpp"
#include "../shared/partitions.cpp"
#include "build_common.h"
#include "../shared/esfs2.h"
#include "header_generator.c"

// Toolchain flags:

char commonCompileFlags[4096] = " -Wall -Wextra -Wno-missing-field-initializers -Wno-frame-address "
	"-Wno-unused-function -Wno-format-truncation -ffreestanding -fno-exceptions -g -I. ";
char cCompileFlags[4096] = "";
char cppCompileFlags[4096] = " -std=c++14 -Wno-pmf-conversions -Wno-invalid-offsetof -fno-rtti ";
char kernelCompileFlags[4096] = " -mno-red-zone -mcmodel=kernel -fno-omit-frame-pointer ";
char applicationLinkFlags[4096] = " -ffreestanding -nostdlib -lgcc -g -z max-page-size=0x1000 ";
char apiLinkFlags1[4096] = " -T util/linker/api64.ld -ffreestanding -nostdlib -g -z max-page-size=0x1000 -Wl,--start-group ";
char apiLinkFlags2[4096] = " -lgcc ";
char apiLinkFlags3[4096] = " -Wl,--end-group -Lroot/Applications/POSIX/lib ";
char kernelLinkFlags[4096] = " -ffreestanding -nostdlib -lgcc -g -z max-page-size=0x1000 ";

// Specific configuration options:

bool verbose;
bool useColoredOutput;
bool forEmulator, bootUseVBE;
bool systemBuild;
bool convertFonts = true;
EsINIState *fontLines;
EsINIState *generalOptions;

// State:

char *builtinModules;
volatile uint8_t encounteredErrors;

//////////////////////////////////

#define COLOR_ERROR "\033[0;33m"
#define COLOR_HIGHLIGHT "\033[0;36m"
#define COLOR_NORMAL "\033[0m"

const char *toolchainAR = "/Applications/POSIX/bin/ar";
const char *toolchainCC = "/Applications/POSIX/bin/gcc";
const char *toolchainCXX = "/Applications/POSIX/bin/g++";
const char *toolchainLD = "/Applications/POSIX/bin/ld";
const char *toolchainNM = "/Applications/POSIX/bin/nm";
const char *toolchainStrip = "/Applications/POSIX/bin/strip";
const char *toolchainNasm = "/Applications/POSIX/bin/nasm";
const char *toolchainConvertSVG = "/Applications/POSIX/bin/render_svg";
const char *toolchainLinkerScripts = "/Applications/POSIX/lib";
const char *toolchainCRTObjects = "/Applications/POSIX/lib";

char *executeEnvironment[3] = {
	(char *) "PATH=/Applications/POSIX/bin",
	(char *) "TMPDIR=/Applications/POSIX/tmp",
	NULL,
};

#define Execute(...) _Execute(NULL, __VA_ARGS__, NULL, NULL)
#define ExecuteWithOutput(...) _Execute(__VA_ARGS__, NULL, NULL)
#define ExecuteForApp(application, ...) if (!application->error && ExecuteWithOutput(&application->output, __VA_ARGS__)) application->error = true
#define ArgString(x) NULL, x

int _Execute(char **output, const char *executable, ...) {
	char *argv[64];
	va_list argList;
	va_start(argList, executable);

	argv[0] = (char *) executable;

	for (uintptr_t i = 1; i <= 64; i++) {
		assert(i != 64);
		argv[i] = va_arg(argList, char *);

		if (!argv[i]) {
			char *string = va_arg(argList, char *);

			if (!string) {
				break;
			}

			char *copy = (char *) malloc(strlen(string) + 2);
			strcpy(copy, string);
			strcat(copy, " ");
			uintptr_t start = 0;
			bool inQuotes = false;

			for (uintptr_t j = 0; copy[j]; j++) {
				assert(i != 64);

				if ((!inQuotes && copy[j] == ' ') || !copy[j]) {
					if (start != j) {
						argv[i] = copy + start;
						if (argv[i][0] == '"') argv[i]++;
						i++;
					}

					if (j && copy[j - 1] == '"') copy[j - 1] = 0;
					copy[j] = 0;
					start = j + 1;
				} else if (copy[j] == '"') {
					inQuotes = !inQuotes;
				}
			}

			i--;
		}
	}

	va_end(argList);

	if (verbose) {
		for (uintptr_t i = 0; i < 64; i++) {
			if (!argv[i]) break;
			Log("\"%s\" ", argv[i]);
		}

		Log("\n");
	}

	int stdoutPipe[2];

	if (output) {
		pipe(stdoutPipe);
	}

	int status = -1;
	pid_t pid = vfork();

	if (pid == 0) {
		if (output) {
			dup2(stdoutPipe[1], 1);
			dup2(stdoutPipe[1], 2);
			close(stdoutPipe[1]);
		}

		execve(executable, argv, executeEnvironment);
		_exit(-1);
	} else if (pid > 0) {
		if (output) {
			close(stdoutPipe[1]);

			while (true) {
				char buffer[1024];
				intptr_t bytesRead = read(stdoutPipe[0], buffer, 1024);

				if (bytesRead <= 0) {
					break;
				} else {
					size_t previousLength = arrlenu(*output);
					arrsetlen(*output, previousLength + bytesRead);
					memcpy(*output + previousLength, buffer, bytesRead);
				}
			}
		}

		wait4(-1, &status, 0, NULL);
	} else {
		Log("Error: could not vfork process.\n");
		exit(1);
	}

	if (output) {
		close(stdoutPipe[0]);
	}

	if (verbose && status) {
		Log("(status = %d)\n", status);
	}

	if (status) __sync_fetch_and_or(&encounteredErrors, 1);
	return status;
}

void MakeDirectory(const char *path) {
#ifdef OS_ESSENCE
	EsPOSIXSystemCall(SYS_mkdir, (intptr_t) path, 0, 0, 0, 0, 0);
#else
	mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
#endif
}

void DeleteFile(const char *path) {
	unlink(path);
}

void CopyFile(const char *oldPath, const char *newPath, bool quietError) {
	File source = FileOpen(oldPath, 'r');
	File destination = FileOpen(newPath, 'w');

	if (source.error) {
		if (quietError) return;
		Log("Error: Could not open file '%s' as a copy source.\n", oldPath);
		__sync_fetch_and_or(&encounteredErrors, 1);
		return;
	}

	if (destination.error) {
		if (quietError) return;
		Log("Error: Could not open file '%s' as a copy destination.\n", newPath);
		__sync_fetch_and_or(&encounteredErrors, 1);
		return;
	}

	char buffer[4096];

	while (true) {
		size_t bytesRead = FileRead(source, sizeof(buffer), buffer);
		if (!bytesRead) break;
		FileWrite(destination, bytesRead, buffer);
	}

	FileClose(source);
	FileClose(destination);
}

void MoveFile(const char *oldPath, const char *newPath) {
	rename(oldPath, newPath);
}

bool FileExists(const char *path) {
#ifdef OS_ESSENCE
	size_t path2Bytes;
	char *path2 = EsPOSIXConvertPath(path, &path2Bytes, true);
	bool exists = EsPathExists(path2, path2Bytes, NULL);
	EsHeapFree(path2, 0, NULL);
	return exists;
#else
	struct stat s;
	return !stat(path, &s);
#endif
}

void CreateImportNode(const char *path, ImportNode *node) {
	char pathBuffer[256];

#ifdef OS_ESSENCE
	size_t path2Bytes;
	char *path2 = EsPOSIXConvertPath(path, &path2Bytes, true);
	EsDirectoryChild *children;
	ptrdiff_t childCount = EsDirectoryEnumerateChildren(path2, path2Bytes, &children);
	EsHeapFree(path2, 0, NULL);

	for (intptr_t i = 0; i < childCount; i++) {
		snprintf(pathBuffer, sizeof(pathBuffer), "%s/%.*s", path, (int) children[i].nameBytes, children[i].name);

		ImportNode child = {};

		if (children[i].type == ES_NODE_DIRECTORY) {
			CreateImportNode(pathBuffer, &child);
		} else {
			child.isFile = true;
		}

		child.name = EsStringAllocateAndFormat(NULL, "%s", children[i].nameBytes, children[i].name);
		child.path = strdup(pathBuffer);
		arrput(node->children, child);
	}

	EsHeapFree(children, 0, NULL);
#else
	DIR *d = opendir(path);
	struct dirent *dir;

	if (!d) {
		return;
	}

	while ((dir = readdir(d))) {
		if (dir->d_name[0] == '.') {
			continue;
		}

		snprintf(pathBuffer, sizeof(pathBuffer), "%s/%s", path, dir->d_name);
		struct stat s = {};
		lstat(pathBuffer, &s);

		ImportNode child = {};

		if (S_ISDIR(s.st_mode)) {
			CreateImportNode(pathBuffer, &child);
		} else if ((s.st_mode & S_IFMT) == S_IFLNK) {
			continue;
		} else {
			child.isFile = true;
		}

		child.name = strdup(dir->d_name);
		child.path = strdup(pathBuffer);
		arrput(node->children, child);
	}

	closedir(d);
#endif
}

//////////////////////////////////

typedef struct BundleInput {
	const char *path;
	const char *name;
	uint64_t alignment;
} BundleInput;

bool MakeBundle(const char *outputFile, BundleInput *inputFiles, size_t inputFileCount, uint64_t mapAddress) {
	File output = FileOpen(outputFile, 'w');
	
	if (output.error) {
		Log("Error: Could not open output file '%s'.\n", outputFile);
		return false;
	}

	uint32_t signature = 0x63BDAF45;
	FileWrite(output, sizeof(uint32_t), &signature);
	uint32_t version = 1;
	FileWrite(output, sizeof(uint32_t), &version);
	uint32_t fileCount = inputFileCount;
	FileWrite(output, sizeof(uint32_t), &fileCount);
	uint32_t zero = 0;
	FileWrite(output, sizeof(uint32_t), &zero);
	FileWrite(output, sizeof(uint64_t), &mapAddress);

	for (uintptr_t i = 0; i < fileCount; i++) {
		uint64_t zero = 0;
		FileWrite(output, sizeof(uint64_t), &zero);
		FileWrite(output, sizeof(uint64_t), &zero);
		FileWrite(output, sizeof(uint64_t), &zero);
	}

	uint64_t outputPosition = 24 + fileCount * 24;

	for (uintptr_t i = 0; i < fileCount; i++) {
		FileSeek(output, 24 + i * 24);

		const char *nameString = inputFiles[i].name;
		outputPosition = (outputPosition + inputFiles[i].alignment - 1) & ~(inputFiles[i].alignment - 1); 

		uint64_t name = CalculateCRC64(nameString, strlen(nameString), 0);
		FileWrite(output, sizeof(uint64_t), &name);

		size_t size;
		void *buffer = LoadFile(inputFiles[i].path, &size);

		if (!buffer) {
			Log("Error: Could not open input file '%s'.\n", inputFiles[i].path);
			return false;
		}

		if (size > 0xFFFFFFFF) {
			Log("Error: Input file '%s' too large (max: 4GB).\n", inputFiles[i].path);
			return false;
		}

		FileWrite(output, sizeof(uint64_t), &size);
		FileWrite(output, sizeof(uint64_t), &outputPosition);
		FileSeek(output, outputPosition);
		FileWrite(output, size, buffer);
		outputPosition += size;
	}

	FileClose(output);
	return true;
}

//////////////////////////////////

typedef struct FileType {
	const char *extension;
	const char *name;
	const char *icon;
	int id, openID;
	bool hasThumbnailGenerator;
} FileType;

typedef struct Handler {
	const char *extension;
	const char *action;
	int fileTypeID;
} Handler;

typedef struct DependencyFile {
	char path[256];
	const char *name;
} DependencyFile;

typedef struct Application {
	const char *name; 
	EsINIState *properties;
	int id;

	FileType *fileTypes;
	Handler *handlers;

	bool install, builtin;

	const char **sources;
	const char *compileFlags;
	const char *linkFlags;
	const char *customCompileCommand;
	const char *manifestPath;

	DependencyFile *dependencyFiles;
	char *output;
	bool error, skipped;

	void (*buildCallback)(struct Application *); // Called on a build thread.

	BundleInput *bundleInputFiles;
} Application;

int nextID = 1;
Application *applications;
const char **kernelModules;

#define ADD_BUNDLE_INPUT(_path, _name, _alignment) do { \
	BundleInput bundleInputFile = {}; \
	bundleInputFile.path = _path; \
	bundleInputFile.name = _name; \
	bundleInputFile.alignment = _alignment; \
	arrput(application->bundleInputFiles, bundleInputFile); \
} while (0)

void BuildDesktop(Application *application) {
	ExecuteForApp(application, toolchainNasm, "-felf64", "desktop/api.s", "-MD", "bin/api1.d", "-o", "bin/api1.o", "-Fdwarf");
	ExecuteForApp(application, toolchainCXX, "-MD", "-c", "desktop/api.cpp", "-o", "bin/api2.o", ArgString(commonCompileFlags));
	ExecuteForApp(application, toolchainCXX, "-MD", "-c", "desktop/posix.cpp", "-o", "bin/api3.o", ArgString(commonCompileFlags));
	ExecuteForApp(application, toolchainCC, "-o", "bin/Desktop", "bin/crti.o", "bin/crtbegin.o", 
			"bin/api1.o", "bin/api2.o", "bin/api3.o", "bin/crtend.o", "bin/crtn.o", 
			ArgString(apiLinkFlags1), ArgString(apiLinkFlags2), ArgString(apiLinkFlags3));
	ExecuteForApp(application, toolchainStrip, "-o", "bin/Desktop.no_symbols", "--strip-all", "bin/Desktop");

	for (uintptr_t i = 0; i < arrlenu(fontLines); i++) {
		if (fontLines[i].key[0] == '.' || 0 == strcmp(fontLines[i].key, "license")) {
			char buffer[4096];
			snprintf(buffer, sizeof(buffer), "res/Fonts/%s", fontLines[i].value);
			ADD_BUNDLE_INPUT(strdup(buffer), fontLines[i].value, 16);
		}
	}

	ADD_BUNDLE_INPUT("res/Themes/Theme.dat", "Theme.dat", 16);
	ADD_BUNDLE_INPUT("res/Themes/elementary Icons.dat", "Icons.dat", 16);
	ADD_BUNDLE_INPUT("res/Themes/elementary Icons License.txt", "Icons License.txt", 16);
	ADD_BUNDLE_INPUT("bin/Desktop.no_symbols", "$Executables/x86_64", 0x1000);

	MakeBundle("root/Essence/Desktop.esx", application->bundleInputFiles, arrlenu(application->bundleInputFiles), 0);
}

void BuildApplication(Application *application) {
	if (application->customCompileCommand) {
#ifdef OS_ESSENCE
		// TODO.
#else
		application->error = system(application->customCompileCommand);
#endif
	} else {
		char symbolFile[256];
		char objectFiles[4096];
		char strippedFile[256];
		char executable[256];
		char linkerScript[256];
		char crti[256];
		char crtbegin[256];
		char crtend[256];
		char crtn[256];
		size_t objectFilesPosition = 0;

		snprintf(symbolFile, sizeof(symbolFile), "bin/%s", application->name);
		snprintf(strippedFile, sizeof(strippedFile), "bin/%s.no_symbols", application->name);
		snprintf(linkerScript, sizeof(linkerScript), "%s/linker/userland64.ld", toolchainLinkerScripts);
		snprintf(crti, sizeof(crti), "%s/crti.o", toolchainCRTObjects);
		snprintf(crtbegin, sizeof(crtbegin), "%s/crtbegin.o", toolchainCRTObjects);
		snprintf(crtend, sizeof(crtend), "%s/crtend.o", toolchainCRTObjects);
		snprintf(crtn, sizeof(crtn), "%s/crtn.o", toolchainCRTObjects);

		if (systemBuild) {
			snprintf(executable, sizeof(executable), "root/Applications/%s.esx", application->name);
		} else {
			snprintf(executable, sizeof(executable), "bin/%s.esx", application->name);
		}

		for (uintptr_t i = 0; i < arrlenu(application->sources); i++) {
			const char *source = application->sources[i];
			size_t sourceBytes = strlen(source);

			char objectFile[256];
			snprintf(objectFile, sizeof(objectFile), "bin/%s_%d.o", application->name, (int) i);
			objectFilesPosition += sprintf(objectFiles + objectFilesPosition, "\"%s\" ", objectFile);

			if (sourceBytes > 2 && source[sourceBytes - 1] == 'c' && source[sourceBytes - 2] == '.') {
				ExecuteForApp(application, toolchainCC, "-MD", "-o", objectFile, "-c", source, 
						ArgString(cCompileFlags), ArgString(commonCompileFlags), ArgString(application->compileFlags));
			} else {
				ExecuteForApp(application, toolchainCXX, "-MD", "-o", objectFile, "-c", source, 
						ArgString(cppCompileFlags), ArgString(commonCompileFlags), ArgString(application->compileFlags));
			}
		}

		assert(objectFilesPosition < sizeof(objectFiles));
		objectFiles[objectFilesPosition] = 0;

		ExecuteForApp(application, toolchainCC, "-o", symbolFile, 
				"-Wl,--start-group", ArgString(application->linkFlags), crti, crtbegin, ArgString(objectFiles), crtend, crtn, "-Wl,--end-group", 
				ArgString(applicationLinkFlags), "-T", linkerScript);
		ExecuteForApp(application, toolchainStrip, "-o", strippedFile, "--strip-all", symbolFile);

		ADD_BUNDLE_INPUT(strippedFile, "$Executables/x86_64", 0x1000);

		// Convert any files for the bundle marked with a '!'.

		for (uintptr_t i = 0; i < arrlenu(application->bundleInputFiles); i++) {
			const char *path = application->bundleInputFiles[i].path;

			if (path[0] == '!') {
				if (strlen(path) > 5 && 0 == memcmp(path + strlen(path) - 4, ".svg", 4)) {
					char output[256];
					snprintf(output, sizeof(output), "bin/temp_%d_%d", application->id, (int) i);
					ExecuteForApp(application, toolchainConvertSVG, "convert", path + 1, output);
					application->bundleInputFiles[i].path = output;
				} else {
					char buffer[256];
					snprintf(buffer, sizeof(buffer), "Error: Unknown embed convertion file type '%s'.\n", path);
					size_t previousLength = arrlenu(application->output);
					arrsetlen(application->output, previousLength + strlen(buffer));
					memcpy(application->output + previousLength, buffer, strlen(buffer));
					application->error = true;
				}
			}
		}

		MakeBundle(executable, application->bundleInputFiles, arrlenu(application->bundleInputFiles), 0);
	}
}

#ifdef PARALLEL_BUILD
volatile uintptr_t applicationsIndex = 0;

void *BuildApplicationThread(void *_unused) {
	while (true) {
		uintptr_t i = __sync_fetch_and_add(&applicationsIndex, 1);

		if (i >= arrlenu(applications)) {
			return NULL;
		}

		if (applications[i].skipped) continue;
		applications[i].buildCallback(&applications[i]);
	}
}
#endif

void ParseApplicationManifest(const char *manifestPath) {
	EsINIState s = {};
	char *manifest = (char *) LoadFile(manifestPath, &s.bytes);
	s.buffer = manifest;

	const char *require = "";
	bool disabled = false;

	Application application = {};
	application.id = nextID++;
	application.manifestPath = manifestPath;
	application.compileFlags = "";
	application.linkFlags = "";
	Handler *handler = NULL;
	FileType *fileType = NULL;

	while (EsINIParse(&s)) {
		EsINIZeroTerminate(&s);

		if (0 == strcmp(s.section, "build")) {
			if (0 == strcmp("source", s.key)) {
				arrput(application.sources, s.value);
			}

			INI_READ_STRING_PTR(compile_flags, application.compileFlags);
			INI_READ_STRING_PTR(link_flags, application.linkFlags);
			INI_READ_STRING_PTR(custom_compile_command, application.customCompileCommand);
			INI_READ_STRING_PTR(require, require);
		} else if (0 == strcmp(s.section, "general")) {
			INI_READ_STRING_PTR(name, application.name);
			else INI_READ_BOOL(disabled, disabled);
			else if (s.keyBytes && s.valueBytes) arrput(application.properties, s);
		} else if (0 == strcmp(s.sectionClass, "handler")) {
			if (!s.keyBytes) {
				Handler _handler = {};
				arrput(application.handlers, _handler);
				handler = &arrlast(application.handlers);
			}

			INI_READ_STRING_PTR(extension, handler->extension);
			INI_READ_STRING_PTR(action, handler->action);
		} else if (0 == strcmp(s.sectionClass, "file_type")) {
			if (!s.keyBytes) {
				FileType _fileType = {};
				_fileType.id = nextID++;
				arrput(application.fileTypes, _fileType);
				fileType = &arrlast(application.fileTypes);
			}

			INI_READ_STRING_PTR(extension, fileType->extension);
			INI_READ_STRING_PTR(name, fileType->name);
			INI_READ_STRING_PTR(icon, fileType->icon);
			INI_READ_BOOL(has_thumbnail_generator, fileType->hasThumbnailGenerator);
		} else if (0 == strcmp(s.section, "embed") && s.key[0] != ';' && s.value[0]) {
			BundleInput input = { 0 };
			input.path = s.value;
			input.name = s.key;
			input.alignment = 1;
			arrput(application.bundleInputFiles, input);
		}
	}

	if (disabled || (require[0] && !FileExists(require))) {
		return;
	}

	for (uintptr_t i = 0; i < arrlenu(application.sources); i++) {
		DependencyFile dependencyFile = {};
		dependencyFile.name = application.name;
		snprintf(dependencyFile.path, sizeof(dependencyFile.path), "bin/%s_%d.d", application.name, (int) i);
		arrput(application.dependencyFiles, dependencyFile);
	}

	application.buildCallback = BuildApplication;
	application.install = true;

	arrput(applications, application);
}

void OutputSystemConfiguration() {
	EsINIState s = {};
	char *config = (char *) LoadFile("res/System Configuration Template.ini", &s.bytes);
	s.buffer = config;
	File file = FileOpen("root/Essence/Default.ini", 'w');

	while (EsINIParse(&s)) {
		EsINIZeroTerminate(&s);

		char buffer[4096];
		FileWrite(file, EsINIFormat(&s, buffer, sizeof(buffer)), buffer);

		if (0 == strcmp(s.section, "general") && (!EsINIPeek(&s) || !s.keyBytes)) {
			FilePrintFormat(file, "next_id=%d\n", nextID);

			for (uintptr_t i = 0; i < arrlenu(generalOptions); i++) {
				FileWrite(file, EsINIFormat(generalOptions + i, buffer, sizeof(buffer)), buffer);
			}
		}
	}

	for (uintptr_t i = 0; i < arrlenu(applications); i++) {
		if (!applications[i].install) {
			continue;
		}

		for (uintptr_t j = 0; j < arrlenu(applications[i].handlers); j++) {
			Handler *handler = applications[i].handlers + j;
			int handlerID = applications[i].id;

			for (uintptr_t i = 0; i < arrlenu(applications); i++) {
				for (uintptr_t j = 0; j < arrlenu(applications[i].fileTypes); j++) {
					FileType *fileType = applications[i].fileTypes + j;

					if (0 == strcmp(handler->extension, fileType->extension)) {
						handler->fileTypeID = fileType->id;

						if (0 == strcmp(handler->action, "open")) {
							fileType->openID = handlerID;
						} else {
							Log("Warning: unrecognised handler action '%s'.\n", handler->action);
						}
					}
				}
			}

			if (!handler->fileTypeID) {
				Log("Warning: could not find a file_type entry for handler with extension '%s' in application '%s'.\n",
						handler->extension, applications[i].name);
			}
		}
	}

	for (uintptr_t i = 0; i < arrlenu(applications); i++) {
		if (!applications[i].install) {
			continue;
		}

		FilePrintFormat(file, "\n[@application %d]\n", applications[i].id);
		FilePrintFormat(file, "name=%s\n", applications[i].name);
		FilePrintFormat(file, "executable=0:/Applications/%s.esx\n", applications[i].name);
		FilePrintFormat(file, "settings_path=0:/Essence/Settings/%s\n", applications[i].name);

		for (uintptr_t j = 0; j < arrlenu(applications[i].properties); j++) {
			FilePrintFormat(file, "%s=%s\n", applications[i].properties[j].key, applications[i].properties[j].value);
		}

		for (uintptr_t j = 0; j < arrlenu(applications[i].fileTypes); j++) {
			FilePrintFormat(file, "\n[@file_type %d]\n", applications[i].fileTypes[j].id);
			FilePrintFormat(file, "extension=%s\n", applications[i].fileTypes[j].extension);
			FilePrintFormat(file, "name=%s\n", applications[i].fileTypes[j].name);
			FilePrintFormat(file, "icon=%s\n", applications[i].fileTypes[j].icon);
			FilePrintFormat(file, "open=%d\n", applications[i].fileTypes[j].openID);
			FilePrintFormat(file, "has_thumbnail_generator=%d\n", applications[i].fileTypes[j].hasThumbnailGenerator);
		}

		for (uintptr_t j = 0; j < arrlenu(applications[i].handlers); j++) {
			FilePrintFormat(file, "\n[@handler]\n");
			FilePrintFormat(file, "action=%s\n", applications[i].handlers[j].action);
			FilePrintFormat(file, "application=%d\n", applications[i].id);
			FilePrintFormat(file, "file_type=%d\n", applications[i].handlers[j].fileTypeID);
		}
	}

	for (uintptr_t i = 0; i < arrlenu(fontLines); i++) {
		char buffer[4096];

		if (fontLines[i].key[0] == '.') {
			if (convertFonts) {
#ifdef OS_ESSENCE
				// TODO.
#else
				snprintf(buffer, sizeof(buffer), "bin/designer --make-font \"res/Fonts/%s\" \"bin/%.*s.dat\"", 
						fontLines[i].value, (int) fontLines[i].valueBytes - 4, fontLines[i].value);
				system(buffer);
				FilePrintFormat(file, "%s=|Fonts:/%.*s.dat\n", fontLines[i].key, (int) fontLines[i].valueBytes - 4, fontLines[i].value);
#endif
			} else {
				FilePrintFormat(file, "%s=:%s\n", fontLines[i].key, fontLines[i].value);
			}
		} else {
			size_t bytes = EsINIFormat(fontLines + i, buffer, sizeof(buffer));
			FileWrite(file, bytes, buffer);
		}
	}

	FileClose(file);
}

void BuildModule(Application *application) {
	char output[4096];
	snprintf(output, sizeof(output), "bin/%s.ekm", application->name);

	assert(arrlenu(application->sources) == 1);
	ExecuteForApp(application, toolchainCXX, "-MD", "-c", application->sources[0], "-o", 
			output, ArgString(cppCompileFlags), ArgString(kernelCompileFlags), ArgString(commonCompileFlags), 
			application->builtin ? "-DBUILTIN_MODULE" : "-DKERNEL_MODULE");

	if (!application->builtin) {
		char target[4096];
		snprintf(target, sizeof(target), "root/Essence/Modules/%s.ekm", application->name);
		MakeDirectory("root/Essence/Modules");
		MoveFile(output, target);
	}
}

bool IsModuleEnabled(const char *name, size_t nameBytes) {
	for (uintptr_t i = 0; i < arrlenu(kernelModules); i++) {
		if (IsStringEqual(name, nameBytes, kernelModules[i])) {
			return true;
		}
	}

	return false;
}

void ParseKernelConfiguration() {
	if (!CheckDependencies("Kernel Config")) {
		return;
	}

	size_t kernelConfigBytes;
	char *kernelConfig = (char *) LoadFile("kernel/config.ini", &kernelConfigBytes);

	File f = FileOpen("bin/kernel_config.h", 'w');

	EsINIState s = {};
	s.buffer = (char *) kernelConfig;
	s.bytes = kernelConfigBytes;

	EsINIState previous = s;

	while (EsINIParse(&s)) {
		if (!IsStringEqual(s.sectionClass, s.sectionClassBytes, "driver")
				|| (previous.sectionBytes == s.sectionBytes && 0 == memcmp(previous.section, s.section, s.sectionBytes))
				|| !IsModuleEnabled(s.section, s.sectionBytes)) {
			continue;
		}

		FilePrintFormat(f, "extern \"C\" KDriver driver%.*s;\n", (int) s.sectionBytes, s.section);
		previous = s;
	}

	FilePrintFormat(f, "#ifdef K_IN_CORE_KERNEL\n");
	FilePrintFormat(f, "KInstalledDriver builtinDrivers[] = {\n");

	s.buffer = (char *) kernelConfig;
	s.bytes = kernelConfigBytes;

	char *moduleName = NULL, *parentName = NULL, *dataStart = s.buffer;
	size_t moduleNameBytes = 0, parentNameBytes = 0;
	bool builtin = false;
	bool foundMatchingArchitecture = false, anyArchitecturesListed = false;

	while (EsINIParse(&s)) {
		if (!IsStringEqual(s.sectionClass, s.sectionClassBytes, "driver")) {
			continue;
		}

		moduleName = s.section;
		moduleNameBytes = s.sectionBytes;

		if (IsStringEqual(s.key, s.keyBytes, "parent")) {
			parentName = s.value, parentNameBytes = s.valueBytes;
		}

		if (IsStringEqual(s.key, s.keyBytes, "builtin")) {
			builtin = s.valueBytes && s.value[0] == '1';
		}

		if (!foundMatchingArchitecture && IsStringEqual(s.key, s.keyBytes, "arch")) {
			anyArchitecturesListed = true;

			if (IsStringEqual(s.value, s.valueBytes, "x86_common")
					|| IsStringEqual(s.value, s.valueBytes, "x86_64")) {
				foundMatchingArchitecture = true;
			}
		}

		char *dataEnd = s.buffer;

		if (!EsINIPeek(&s) || !s.keyBytes) {
			if ((foundMatchingArchitecture || !anyArchitecturesListed) && IsModuleEnabled(moduleName, moduleNameBytes)) {
				FilePrintFormat(f, "\t{\n");
				FilePrintFormat(f, "\t\t.name = (char *) \"%.*s\",\n", (int) moduleNameBytes, moduleName);
				FilePrintFormat(f, "\t\t.nameBytes = %d,\n", (int) moduleNameBytes);
				FilePrintFormat(f, "\t\t.parent = (char *) \"%.*s\",\n", (int) parentNameBytes, parentName);
				FilePrintFormat(f, "\t\t.parentBytes = %d,\n", (int) parentNameBytes);
				FilePrintFormat(f, "\t\t.config = (char *) R\"(%.*s)\",\n", (int) (dataEnd - dataStart), dataStart);
				FilePrintFormat(f, "\t\t.configBytes = %d,\n", (int) (dataEnd - dataStart));
				FilePrintFormat(f, "\t\t.builtin = %d,\n", builtin);
				FilePrintFormat(f, "\t\t.loadedDriver = &driver%.*s,\n", (int) moduleNameBytes, moduleName);
				FilePrintFormat(f, "\t},\n");
			}

			moduleName = parentName = NULL;
			moduleNameBytes = parentNameBytes = 0;
			builtin = false;
			foundMatchingArchitecture = anyArchitecturesListed = false;
			dataStart = dataEnd;
		}
	}

	FilePrintFormat(f, "};\n");
	FilePrintFormat(f, "#endif");
	FileClose(f);

	f = FileOpen("bin/system_config.d", 'w');
	FilePrintFormat(f, ": kernel/config.ini\n");
	FileClose(f);
	ParseDependencies("bin/system_config.d", "Kernel Config", false);
	DeleteFile("bin/system_config.d");
}

void LinkKernel() {
	arrput(builtinModules, 0);

	if (Execute(toolchainLD, "-r", "bin/kernel.o", "bin/kernel_x86_64.o", ArgString(builtinModules), "-o" "bin/kernel_all.o")) {
		return;
	}
	
	{
		char *output = NULL;

		if (_Execute(&output, toolchainNM, "bin/kernel_all.o", NULL, NULL)) {
			return;
		} else {
			File f = FileOpen("bin/kernel_symbols.h", 'w');
			uintptr_t lineStart = 0, position = 0;

			while (position < arrlenu(output)) {
				if (output[position] == '\n') {
					output[position] = 0;
					const char *line = output + lineStart;
					const char *t = strstr(line, " T ");

					if (t) {
						FilePrintFormat(f, "{ (void *) 0x%.*s, \"%s\" },\n", (int) (t - line), line, t + 3);
					}

					lineStart = position + 1;
				}

				position++;
			}

			FileClose(f);

			Execute(toolchainCXX, "-c", "kernel/symbols.cpp", "-o", "bin/kernel_symbols.o", 
					ArgString(cppCompileFlags), ArgString(kernelCompileFlags), ArgString(commonCompileFlags));
		}
	}

	Execute(toolchainCXX, "-T", "util/linker/kernel64.ld", "-o", "bin/Kernel", "bin/kernel_symbols.o", "bin/kernel_all.o", "-mno-red-zone", ArgString(kernelLinkFlags));
	Execute(toolchainStrip, "-o", "bin/Kernel.esx", "--strip-all", "bin/Kernel");
	CopyFile("bin/Kernel.esx", "root/Essence/Kernel.esx", false);
}

void BuildKernel(Application *application) {
	ExecuteForApp(application, toolchainNasm, "-MD", "bin/kernel2.d", "-D", "COM_OUTPUT", 
			"-felf64", "kernel/x86_64.s", "-o", "bin/kernel_x86_64.o", "-Fdwarf");
	ExecuteForApp(application, toolchainCXX, "-MD", "-c", "kernel/main.cpp", "-o", "bin/kernel.o", 
			ArgString(kernelCompileFlags), ArgString(cppCompileFlags), ArgString(commonCompileFlags));
}

void BuildBootloader(Application *application) {
	ExecuteForApp(application, toolchainNasm, "-MD", "bin/boot1.d", "-fbin", 
			forEmulator ? "boot/x86/mbr.s" : "boot/x86/mbr-emu.s" , "-obin/mbr");
	ExecuteForApp(application, toolchainNasm, "-MD", "bin/boot2.d", "-fbin", 
			"boot/x86/esfs-stage1.s", "-obin/stage1");
	ExecuteForApp(application, toolchainNasm, "-MD", "bin/boot3.d", "-fbin", 
			"boot/x86/loader.s", "-obin/stage2", 
			"-Pboot/x86/esfs-stage2.s", (forEmulator && !bootUseVBE) ? "" : "-D BOOT_USE_VBE");
	ExecuteForApp(application, toolchainNasm, "-MD", "bin/boot4.d", "-fbin", 
			"boot/x86/uefi_loader.s", "-obin/uefi_loader");
}

File _drive;
uint64_t _partitionOffset;

bool ReadBlock(uint64_t block, uint64_t count, void *buffer) {
	FileSeek(_drive, block * blockSize + _partitionOffset);
	// printf("read of block %ld\n", block);

	if (FileRead(_drive, blockSize * count, buffer) != blockSize * count) {
		Log("Error: Could not read blocks %d->%d of drive.\n", (int) block, (int) (block + count));
		exit(1);
	}

	return true;
}

bool WriteBlock(uint64_t block, uint64_t count, void *buffer) {
	FileSeek(_drive, block * blockSize + _partitionOffset);
	assert(block < 4294967296);

	if (FileWrite(_drive, blockSize * count, buffer) != blockSize * count) {
		Log("Error: Could not write to blocks %d->%d of drive.\n", (int) block, (int) (block + count));
		exit(1);
	}
	
	return true;
}

bool WriteBytes(uint64_t offset, uint64_t count, void *buffer) {
	FileSeek(_drive, offset + _partitionOffset);

	if (FileWrite(_drive, count, buffer) != count) {
		Log("Error: Could not write to bytes %d->%d of drive.\n", (int) offset, (int) (offset + count));
		exit(1);
	}

	return true;
}

void Install(const char *driveFile, uint64_t partitionSize, const char *partitionLabel) {
	Log("Installing...\n");

	EsUniqueIdentifier installationIdentifier;

#ifndef OS_ESSENCE
	srand(time(NULL));
#endif

	for (int i = 0; i < 16; i++) {
		installationIdentifier.d[i] = rand();
	}

	File iid = FileOpen("bin/iid.dat", 'w');
	FileWrite(iid, 16, &installationIdentifier);
	FileClose(iid);

	File f = FileOpen(driveFile, 'w');

	File mbr = FileOpen("bin/mbr", 'r');
	char mbrBuffer[446] = {};
	FileRead(mbr, 446, mbrBuffer);
	FileWrite(f, 446, mbrBuffer);
	FileClose(mbr);

	uint32_t partitions[16] = { 0x80 /* bootable */, 0x83 /* type */, 0x800 /* offset */, (uint32_t) ((partitionSize / 0x200) - 0x800) /* sector count */ };
	uint16_t bootSignature = 0xAA55;
	MBRFixPartition(partitions);

	FileWrite(f, 64, partitions);
	FileWrite(f, 2, &bootSignature);

	void *blank = calloc(1, 0x800 * 0x200 - 0x200);
	FileWrite(f, 0x800 * 0x200 - 0x200, blank);
	free(blank);

	File stage1 = FileOpen("bin/stage1", 'r');
	char stage1Buffer[0x200] = {};
	FileRead(stage1, 0x200, stage1Buffer);
	FileWrite(f, 0x200, stage1Buffer);
	FileClose(stage1);

	File stage2 = FileOpen("bin/stage2", 'r');
	char stage2Buffer[0x200 * 15] = {};

	if (sizeof(stage2Buffer) == FileRead(stage2, sizeof(stage2Buffer), stage2Buffer)) {
		Log("Error: Stage 2 bootloader too large. Must fit in 7.5KB.\n");
		exit(1);
	}

	FileWrite(f, sizeof(stage2Buffer), stage2Buffer);
	FileClose(stage2);

	FileClose(f);

	size_t kernelBytes;
	void *kernel = LoadFile("bin/Kernel.esx", &kernelBytes);

	if (truncate(driveFile, partitionSize)) {
		Log("Error: Could not change the file's size to %d bytes.\n", (int) partitionSize);
		exit(1);
	}

	_drive = FileOpen(driveFile, '+');
	_partitionOffset = 1048576;
	Format(partitionSize - _partitionOffset, partitionLabel, installationIdentifier, kernel, kernelBytes);

	Log("Copying files to the drive... ");

	ImportNode root = {};
	CreateImportNode("root", &root);

	// TODO Update this.
#if 0
	if (convertFonts) {
		ImportNode *fontsFolder = ImportNodeMakeDirectory(ImportNodeFindChild(&root, "Essence"), "Fonts");

		for (uintptr_t i = 0; i < arrlenu(fontLines); i++) {
			if (fontLines[i].key[0] == '.') {
				char source[4096], destination[4096];
				snprintf(source, sizeof(source), "bin/%.*s.dat", (int) fontLines[i].valueBytes - 4, fontLines[i].value);
				snprintf(destination, sizeof(destination), "%.*s.dat", (int) fontLines[i].valueBytes - 4, fontLines[i].value);
				ImportNodeAddFile(fontsFolder, strdup(destination), strdup(source));
			}
		}
	}
#endif

	MountVolume();
	Import(root, superblock.root);
	UnmountVolume();
	Log("(%u MB)\n", (unsigned) (copiedCount / 1048576));

	FileClose(_drive);
}

//////////////////////////////////

#ifdef OS_ESSENCE
int BuildCore(int argc, char **argv) {
#else
int main(int argc, char **argv) {
#endif
	if (argc < 2) {
		Log("Usage: build_core <mode> <options...>\n");
		return 1;
	}

	char **applicationManifests = NULL;
	bool skipCompile = false;
	bool skipHeaderGeneration = false;
	bool minimalRebuild = false;

#ifdef PARALLEL_BUILD
	size_t threadCount = 1;
#endif

	const char *driveFile = NULL;
	uint64_t partitionSize = 0;
	const char *partitionLabel = "New Volume";

	char *driverSource = NULL, *driverName = NULL;
	bool driverBuiltin = false;

	if (0 == strcmp(argv[1], "standard")) {
		if (argc != 3) {
			Log("Usage: standard <configuration>\n");
			return 1;
		}

		EsINIState s = {};
		s.buffer = (char *) LoadFile(argv[2], &s.bytes);

		if (!s.buffer) {
			Log("Error: could not load configuration file '%s'.\n", argv[2]);
			return 1;
		}

		while (EsINIParse(&s)) {
			EsINIZeroTerminate(&s);

			if (0 == strcmp(s.section, "toolchain")) {
				if (0 == strcmp(s.key, "path")) {
					executeEnvironment[0] = (char *) malloc(5 + s.valueBytes + 1);
					strcpy(executeEnvironment[0], "PATH=");
					strcat(executeEnvironment[0], s.value);
				} else if (0 == strcmp(s.key, "tmpdir")) {
					if (s.value[0]) {
						executeEnvironment[1] = (char *) malloc(7 + s.valueBytes + 1);
						strcpy(executeEnvironment[1], "TMPDIR=");
						strcat(executeEnvironment[1], s.value);
					} else {
						executeEnvironment[1] = NULL;
					}
				} else if (0 == strcmp(s.key, "ar")) {
					toolchainAR = s.value;
				} else if (0 == strcmp(s.key, "cc")) {
					toolchainCC = s.value;
				} else if (0 == strcmp(s.key, "cxx")) {
					toolchainCXX = s.value;
				} else if (0 == strcmp(s.key, "ld")) {
					toolchainLD = s.value;
				} else if (0 == strcmp(s.key, "nm")) {
					toolchainNM = s.value;
				} else if (0 == strcmp(s.key, "strip")) {
					toolchainStrip = s.value;
				} else if (0 == strcmp(s.key, "nasm")) {
					toolchainNasm = s.value;
				} else if (0 == strcmp(s.key, "convert_svg")) {
					toolchainConvertSVG = s.value;
				} else if (0 == strcmp(s.key, "linker_scripts")) {
					toolchainLinkerScripts = s.value;
				} else if (0 == strcmp(s.key, "crt_objects")) {
					toolchainCRTObjects = s.value;
				}
			} else if (0 == strcmp(s.sectionClass, "application")) {
				if (0 == strcmp(s.key, "manifest")) {
					arrput(applicationManifests, s.value);
				}
			} else if (0 == strcmp(s.section, "options")) {
				if (0 == strcmp(s.key, "Dependency.ACPICA") && atoi(s.value)) {
					strcat(kernelLinkFlags, " -lacpica -Lports/acpica ");
					strcat(kernelCompileFlags, " -DUSE_ACPICA ");
				} else if (0 == strcmp(s.key, "Dependency.stb_image") && atoi(s.value)) {
					strcat(commonCompileFlags, " -DUSE_STB_IMAGE ");
				} else if (0 == strcmp(s.key, "Dependency.stb_image_write") && atoi(s.value)) {
					strcat(commonCompileFlags, " -DUSE_STB_IMAGE_WRITE ");
				} else if (0 == strcmp(s.key, "Dependency.stb_sprintf") && atoi(s.value)) {
					strcat(commonCompileFlags, " -DUSE_STB_SPRINTF ");
				} else if (0 == strcmp(s.key, "Dependency.HarfBuzz") && atoi(s.value)) {
					strcat(apiLinkFlags2, " -lharfbuzz ");
					strcat(commonCompileFlags, " -DUSE_HARFBUZZ ");
				} else if (0 == strcmp(s.key, "Dependency.FreeType") && atoi(s.value)) {
					strcat(apiLinkFlags2, " -lfreetype ");
					strcat(commonCompileFlags, " -DUSE_FREETYPE ");
					convertFonts = false;
				} else if (0 == strcmp(s.key, "Flag._ALWAYS_USE_VBE")) {
					bootUseVBE = !!atoi(s.value);
				} else if (0 == memcmp(s.key, "General.", 8)) {
					EsINIState s2 = s;
					s2.key += 8, s2.keyBytes -= 8;
					arrput(generalOptions, s2);
				}
			} else if (0 == strcmp(s.section, "general")) {
				if (0 == strcmp(s.key, "skip_compile")) {
					skipCompile = !!atoi(s.value);
				} else if (0 == strcmp(s.key, "skip_header_generation")) {
					skipHeaderGeneration = !!atoi(s.value);
				} else if (0 == strcmp(s.key, "verbose")) {
					verbose = !!atoi(s.value);
				} else if (0 == strcmp(s.key, "for_emulator")) {
					forEmulator = !!atoi(s.value);
				} else if (0 == strcmp(s.key, "system_build")) {
					systemBuild = !!atoi(s.value);
				} else if (0 == strcmp(s.key, "minimal_rebuild")) {
					minimalRebuild = !!atoi(s.value);
				} else if (0 == strcmp(s.key, "optimise")) {
					if (atoi(s.value)) {
						strcat(commonCompileFlags, " -O2 ");
					}
				} else if (0 == strcmp(s.key, "colored_output")) {
					if (atoi(s.value)) {
						strcat(commonCompileFlags, " -fdiagnostics-color=always ");
						useColoredOutput = true;
					}
				} else if (0 == strcmp(s.key, "common_compile_flags")) {
					strcat(commonCompileFlags, s.value);
				} else if (0 == strcmp(s.key, "thread_count")) {
#ifndef PARALLEL_BUILD
					Log("Warning: thread_count not supported.\n");
#else
					threadCount = atoi(s.value);
					if (threadCount < 1) threadCount = 1;
					if (threadCount > 100) threadCount = 100;
#endif
				}
			} else if (0 == strcmp(s.section, "install")) {
				if (0 == strcmp(s.key, "file")) {
					driveFile = s.value;
				} else if (0 == strcmp(s.key, "partition_label")) {
					partitionLabel = s.value;
				} else if (0 == strcmp(s.key, "partition_size")) {
					partitionSize = atoi(s.value) * 1048576UL;
				}
			} else if (0 == strcmp(s.sectionClass, "font")) {
				arrput(fontLines, s);
			} else if (0 == strcmp(s.sectionClass, "driver")) {
				if (0 == strcmp(s.key, "name")) driverName = s.value;
				if (0 == strcmp(s.key, "source")) driverSource = s.value;
				if (0 == strcmp(s.key, "builtin")) driverBuiltin = !!atoi(s.value);

				if (!EsINIPeek(&s) || !s.keyBytes) {
					arrput(kernelModules, driverName);

					if (driverSource && *driverSource) {
						DependencyFile dependencyFile = {};
						dependencyFile.name = driverName;
						snprintf(dependencyFile.path, sizeof(dependencyFile.path), "bin/%s.d", driverName);

						Application application = {};
						arrput(application.sources, driverSource);
						application.name = driverName;
						application.builtin = driverBuiltin;
						application.buildCallback = BuildModule;
						arrput(application.dependencyFiles, dependencyFile);
						arrput(applications, application);

						if (driverBuiltin) {
							char append[256];
							snprintf(append, sizeof(append), " bin/%s.ekm ", driverName);
							size_t previousLength = arrlenu(builtinModules);
							arrsetlen(builtinModules, previousLength + strlen(append));
							memcpy(builtinModules + previousLength, append, strlen(append));
						}
					}

					driverSource = driverName = NULL;
					driverBuiltin = false;
				}
			}

			if (0 != strcmp(s.section, "install")) {
				configurationHash = CalculateCRC64(s.sectionClass, s.sectionClassBytes, configurationHash);
				configurationHash = CalculateCRC64(s.section, s.sectionBytes, configurationHash);
				configurationHash = CalculateCRC64(s.key, s.keyBytes, configurationHash);
				configurationHash = CalculateCRC64(s.value, s.valueBytes, configurationHash);
			}
		}
	} else if (0 == strcmp(argv[1], "application")) {
		if (argc != 3) {
			Log("Usage: application <configuration>\n");
			return 1;
		}

		arrput(applicationManifests, argv[2]);
	} else if (0 == strcmp(argv[1], "headers")) {
		MakeDirectory("bin");
		return HeaderGeneratorMain(argc - 1, argv + 1);
	} else {
		Log("Error: Unsupported mode '%s'.\n", argv[1]);
		return 1;
	}

	buildStartTimeStamp = time(NULL);
	sh_new_strdup(applicationDependencies);

	if (minimalRebuild) {
		DependenciesListRead();
	}

	MakeDirectory("bin");

	if (systemBuild) {
		MakeDirectory("root");
		MakeDirectory("root/Applications");
		MakeDirectory("root/Applications/POSIX");
		MakeDirectory("root/Applications/POSIX/bin");
		MakeDirectory("root/Applications/POSIX/include");
		MakeDirectory("root/Applications/POSIX/lib");
		MakeDirectory("root/Applications/POSIX/tmp");
		MakeDirectory("root/Applications/POSIX/lib/linker");
		MakeDirectory("root/Essence");

		if (!skipHeaderGeneration) {
			HeaderGeneratorMain(1, NULL);
		}
	}

	if (!skipCompile) {
		if (systemBuild) {
			Execute(toolchainNasm, "-felf64", "desktop/crti.s", "-o", "bin/crti.o", "-Fdwarf");
			Execute(toolchainNasm, "-felf64", "desktop/crtn.s", "-o", "bin/crtn.o", "-Fdwarf");
			Execute(toolchainCC, "-c", "desktop/crt1.c", "-o", "bin/crt1.o", ArgString(cCompileFlags), ArgString(commonCompileFlags));
			Execute(toolchainCC, "-c", "desktop/crtglue.c", "-o" "bin/crtglue.o", ArgString(cCompileFlags), ArgString(commonCompileFlags));
			CopyFile("bin/crti.o", "root/Applications/POSIX/lib/crti.o", false);
			CopyFile("bin/crtbegin.o", "root/Applications/POSIX/lib/crtbegin.o", false);
			CopyFile("bin/crtend.o", "root/Applications/POSIX/lib/crtend.o", false);
			CopyFile("bin/crtn.o", "root/Applications/POSIX/lib/crtn.o", false);
			CopyFile("bin/crt1.o", "root/Applications/POSIX/lib/crt1.o", false);
			CopyFile("bin/crtglue.o", "root/Applications/POSIX/lib/crtglue.o", false);
			CopyFile("bin/crt1.o", "cross/lib/gcc/x86_64-essence/" GCC_VERSION "/crt1.o", true);
			CopyFile("bin/crtglue.o", "cross/lib/gcc/x86_64-essence/" GCC_VERSION "/crtglue.o", true);
			CopyFile("util/linker/userland64.ld", "root/Applications/POSIX/lib/linker/userland64.ld", false);

			char linkerScript[256];
			snprintf(linkerScript, sizeof(linkerScript), "%s/linker/userland64.ld", toolchainLinkerScripts);
			Execute(toolchainCC, "util/build_core.c", "-o", "root/Applications/POSIX/bin/build_core", "-g", 
					"-nostdlib", "bin/crti.o", "bin/crtbegin.o", "bin/crtend.o", "bin/crtn.o", "-T", linkerScript);
		}

#define ADD_DEPENDENCY_FILE(application, _path, _name) \
		{ \
			DependencyFile file = {}; \
			strcpy(file.path, _path); \
			file.name = _name; \
			arrput(application.dependencyFiles, file); \
		}

		if (systemBuild) {
			Application application = {};
			application.name = "Bootloader";
			application.buildCallback = BuildBootloader;
			ADD_DEPENDENCY_FILE(application, "bin/boot1.d", "Boot1");
			ADD_DEPENDENCY_FILE(application, "bin/boot2.d", "Boot2");
			ADD_DEPENDENCY_FILE(application, "bin/boot3.d", "Boot3");
			ADD_DEPENDENCY_FILE(application, "bin/boot4.d", "Boot4");
			arrput(applications, application);
		}

		if (systemBuild) {
			Application application = {};
			application.name = "Desktop";
			application.buildCallback = BuildDesktop;
			ADD_DEPENDENCY_FILE(application, "bin/api1.d", "API1");
			ADD_DEPENDENCY_FILE(application, "bin/api2.d", "API2");
			ADD_DEPENDENCY_FILE(application, "bin/api3.d", "API3");
			arrput(applications, application);
		}

		for (uintptr_t i = 0; i < arrlenu(applicationManifests); i++) {
			ParseApplicationManifest(applicationManifests[i]);
		}

		if (systemBuild) {
			ParseKernelConfiguration();

			Application application = {};
			application.name = "Kernel";
			application.buildCallback = BuildKernel;
			ADD_DEPENDENCY_FILE(application, "bin/kernel.d", "Kernel1");
			ADD_DEPENDENCY_FILE(application, "bin/kernel2.d", "Kernel2");
			arrput(applications, application);
		}

		// Check which applications need to be rebuilt.

		for (uintptr_t i = 0; i < arrlenu(applications); i++) {
			bool rebuild = false;

			for (uintptr_t j = 0; j < arrlenu(applications[i].dependencyFiles); j++) {
				if (CheckDependencies(applications[i].dependencyFiles[j].name)) {
					rebuild = true;
					break;
				}
			}

			if (!rebuild && arrlenu(applications[i].dependencyFiles)) {
				applications[i].skipped = true;
			}
		}

		// Build all these applications.

#ifdef PARALLEL_BUILD
		if (useColoredOutput) StartSpinner();
		pthread_t *threads = (pthread_t *) malloc(sizeof(pthread_t) * threadCount);

		for (uintptr_t i = 0; i < threadCount; i++) {
			pthread_create(threads + i, NULL, BuildApplicationThread, NULL);
		}

		for (uintptr_t i = 0; i < threadCount; i++) {
			pthread_join(threads[i], NULL);
		}

		if (useColoredOutput) StopSpinner();
#else
		for (uintptr_t i = 0; i < arrlenu(applications); i++) {
			Log("[%d/%d] Compiling %s...\n", i + 1, arrlenu(applications), applications[i].name);
			if (applications[i].skipped) continue;
			applications[i].buildCallback(&applications[i]);
		}
#endif

		// Output information about the built applications,
		// and parse the dependency files for successfully built ones.

		bool firstEmptyOutput = true;

		for (uintptr_t i = 0; i < arrlenu(applications); i++) {
			if (applications[i].skipped) {
				continue;
			}

			if (!applications[i].error && !arrlenu(applications[i].output)) {
				if (firstEmptyOutput) {
					firstEmptyOutput = false;
					Log("Compiled ");
				} else {
					Log(", ");
				}

				if (useColoredOutput) {
					Log(COLOR_HIGHLIGHT "%s" COLOR_NORMAL, applications[i].name);
				} else {
					Log("%s", applications[i].name);
				}
			}
		}

		if (!firstEmptyOutput) {
			Log(".\n");
		}

		for (uintptr_t i = 0; i < arrlenu(applications); i++) {
			if (applications[i].skipped) {
				continue;
			}

			if (applications[i].error) {
				if (useColoredOutput) {
					Log(">> Could not build " COLOR_ERROR "%s" COLOR_NORMAL ".\n", applications[i].name);
				} else {
					Log(">> Could not build %s.\n", applications[i].name);
				}
			} else if (arrlenu(applications[i].output)) {
				if (useColoredOutput) {
					Log(">> Built " COLOR_HIGHLIGHT "%s" COLOR_NORMAL ".\n", applications[i].name);
				} else {
					Log(">> Built %s.\n", applications[i].name);
				}
			}

			Log("%.*s", (int) arrlen(applications[i].output), applications[i].output);

			if (!applications[i].error) {
				for (uintptr_t j = 0; j < arrlenu(applications[i].dependencyFiles); j++) {
					ParseDependencies(applications[i].dependencyFiles[j].path, 
							applications[i].dependencyFiles[j].name, j > 0);
				}
			}
		}

		if (systemBuild) {
			LinkKernel();
			OutputSystemConfiguration();
		}
	}

	if (driveFile) {
		Install(driveFile, partitionSize, partitionLabel);
	}

	if (minimalRebuild) {
		DependenciesListWrite();
	}

	return encounteredErrors;
}

#ifdef OS_ESSENCE
void _start() {
	int argc; 
	char **argv;
	_init();
	EsPOSIXInitialise(&argc, &argv);
	exit(BuildCore(argc, argv));
}
#endif
