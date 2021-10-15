#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#define WARNING_FLAGS \
	" -Wall -Wextra -Wno-missing-field-initializers -Wno-pmf-conversions -Wno-frame-address -Wno-unused-function -Wno-format-truncation -Wno-invalid-offsetof "
#define WARNING_FLAGS_C \
	" -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-function -Wno-format-truncation -Wno-unused-parameter "

#include <stdint.h>
#include <stdarg.h>

#define CROSS_COMPILER_INDEX (11)

#include <stdio.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <dirent.h>
#include <ctype.h>
#include <assert.h>
#include <setjmp.h>
#include <pthread.h>
#include <sys/wait.h>
#include <spawn.h>
#include "../shared/hash.cpp"

#define ColorHighlight "\033[0;36m"
#define ColorNormal "\033[0m"

bool acceptedLicense;
bool foundValidCrossCompiler;
bool coloredOutput;
bool encounteredErrors;
bool interactiveMode;
bool canBuildLuigi;
FILE *systemLog;
char compilerPath[4096];
int argc;
char **argv;

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#include "build_common.h"

BuildFont fonts[] = {
	{ "Inter", "Inter License.txt", "Sans", "Latn,Grek,Cyrl", (FontFile []) {
		{ "1", "Inter Thin.otf" },
		{ "1i", "Inter Thin Italic.otf" },
		{ "2", "Inter Extra Light.otf" },
		{ "2i", "Inter Extra Light Italic.otf" },
		{ "3", "Inter Light.otf" },
		{ "3i", "Inter Light Italic.otf" },
		{ "4", "Inter Regular.otf" },
		{ "4i", "Inter Regular Italic.otf" },
		{ "5", "Inter Medium.otf" },
		{ "5i", "Inter Medium Italic.otf" },
		{ "6", "Inter Semi Bold.otf" },
		{ "6i", "Inter Semi Bold Italic.otf" },
		{ "7", "Inter Bold.otf" },
		{ "7i", "Inter Bold Italic.otf" },
		{ "8", "Inter Extra Bold.otf" },
		{ "8i", "Inter Extra Bold Italic.otf" },
		{ "9", "Inter Black.otf" },
		{ "9i", "Inter Black Italic.otf" },
		{},
	} },

	{ "Hack", "Hack License.md", "Mono", "Latn,Grek,Cyrl", (FontFile []) {
		{ "4", "Hack Regular.ttf" },
		{ "4i", "Hack Regular Italic.ttf" },
		{ "7", "Hack Bold.ttf" },
		{ "7i", "Hack Bold Italic.ttf" },
		{},
	} },

	{ "Atkinson Hyperlegible", "Atkinson Hyperlegible License.txt", "Sans", "Latn", (FontFile []) {
		{ "4", "Atkinson Hyperlegible Regular.ttf" },
		{ "4i", "Atkinson Hyperlegible Regular Italic.ttf" },
		{ "7", "Atkinson Hyperlegible Bold.ttf" },
		{ "7i", "Atkinson Hyperlegible Bold Italic.ttf" },
		{},
	} },

	{ "OpenDyslexic", "OpenDyslexic License.txt", "Sans", "Latn", (FontFile []) {
		{ "4", "OpenDyslexic Regular.otf" },
		{ "4i", "OpenDyslexic Regular Italic.otf" },
		{ "7", "OpenDyslexic Bold.otf" },
		{ "7i", "OpenDyslexic Bold Italic.otf" },
		{},
	} },

	{},
};

bool GetYes() {
	char *line = NULL;
	size_t pos;
	getline(&line, &pos, stdin);
	line[strlen(line) - 1] = 0;
	bool result = 0 == strcmp(line, "yes") || 0 == strcmp(line, "y");
	free(line);
	return result;
}

int CallSystem(const char *buffer) {
	struct timespec startTime, endTime;
	clock_gettime(CLOCK_REALTIME, &startTime);
	if (systemLog) fprintf(systemLog, "%s\n", buffer);
	int result = system(buffer);
	clock_gettime(CLOCK_REALTIME, &endTime);
	if (systemLog) fprintf(systemLog, "%fs\n", 
			(double) (endTime.tv_sec - startTime.tv_sec)
			+ (double) (endTime.tv_nsec - startTime.tv_nsec) / 1000000000);
	if (result) encounteredErrors = true;
	return result;
}

int CallSystemF(const char *format, ...) {
	va_list arguments;
	va_start(arguments, format);
	char tempBuffer[65536];
	vsnprintf(tempBuffer, sizeof(tempBuffer), format, arguments);
	int result = CallSystem(tempBuffer);
	va_end(arguments);
	return result;
}

void BuildAPIDependencies() {
	if (CheckDependencies("API Header")) {
		CallSystem("bin/build_core headers");
		ParseDependencies("bin/api_header.d", "API Header", false);
	}

	CallSystem("cp `x86_64-essence-gcc -print-file-name=\"crtbegin.o\"` bin/crtbegin.o");
	CallSystem("cp `x86_64-essence-gcc -print-file-name=\"crtend.o\"` bin/crtend.o");

	CallSystem("ports/musl/build.sh > /dev/null");

	CallSystem("ports/freetype/build.sh");
	CallSystem("ports/harfbuzz/build.sh");

	CallSystem("cp -p kernel/module.h root/Applications/POSIX/include");
}

void OutputStartOfBuildINI(FILE *f, bool forceDebugBuildOff) {
	LoadOptions();

	FILE *f2 = popen("which nasm", "r");
	char buffer[1024];
	buffer[fread(buffer, 1, sizeof(buffer), f2) - 1] = 0;
	pclose(f2);

	fprintf(f, "[toolchain]\npath=%s\ntmpdir=%s\n"
			"ar=%s/x86_64-essence-ar\n"
			"cc=%s/x86_64-essence-gcc\n"
			"cxx=%s/x86_64-essence-g++\n"
			"ld=%s/x86_64-essence-ld\n"
			"nm=%s/x86_64-essence-nm\n"
			"strip=%s/x86_64-essence-strip\n"
			"nasm=%s\n"
			"convert_svg=bin/render_svg\n"
			"linker_scripts=util/\n"
			"crt_objects=bin/\n"
			"\n[general]\nsystem_build=1\nminimal_rebuild=1\ncolored_output=%d\nthread_count=%d\nskip_header_generation=1\nverbose=%d\ncommon_compile_flags=",
			compilerPath, getenv("TMPDIR") ?: "",
			compilerPath, compilerPath, compilerPath, compilerPath, compilerPath, compilerPath, 
			buffer, coloredOutput, (int) sysconf(_SC_NPROCESSORS_CONF), IsOptionEnabled("BuildCore.Verbose"));

	for (uintptr_t i = 0; i < sizeof(options) / sizeof(options[0]); i++) {
		Option *option = &options[i];

		if (!option->id || memcmp(option->id, "Flag.", 5)) {
			continue;
		}

		if (0 == strcmp(option->id, "Flag.DEBUG_BUILD") && forceDebugBuildOff) {
			continue;
		}

		if (option->type == OPTION_TYPE_BOOL && option->state.b) {
			fprintf(f, "-D%s ", option->id + 5);
		} else if (option->type == OPTION_TYPE_STRING) {
			fprintf(f, "-D%s=%s ", option->id + 5, option->state.s);
		}
	}

	fprintf(f, "\n\n");

	fprintf(f, "[options]\n");

	for (uintptr_t i = 0; i < sizeof(options) / sizeof(options[0]); i++) {
		if (options[i].type == OPTION_TYPE_BOOL) {
			fprintf(f, "%s=%s\n", options[i].id, options[i].state.b ? "1" : "0");
		} else if (options[i].type == OPTION_TYPE_STRING) {
			fprintf(f, "%s=%s\n", options[i].id, options[i].state.s ?: "");
		} else {
			// TODO.
		}
	}
	
	fprintf(f, "\n");
}

void BuildUtilities();

#define COMPILE_SKIP_COMPILE         (1 << 1)
#define COMPILE_DO_BUILD             (1 << 2)
#define COMPILE_FOR_EMULATOR         (1 << 3)
#define OPTIMISE_OFF                 (1 << 4)
#define OPTIMISE_ON                  (1 << 5)
#define OPTIMISE_FULL                (1 << 6)

void Compile(uint32_t flags, int partitionSize, const char *volumeLabel) {
	buildStartTimeStamp = time(NULL);
	BuildUtilities();
	BuildAPIDependencies();

	LoadOptions();

	FILE *f = fopen("bin/build.ini", "wb");

	OutputStartOfBuildINI(f, flags & OPTIMISE_FULL);

	{
		size_t kernelConfigBytes;
		char *kernelConfig = (char *) LoadFile("kernel/config.ini", &kernelConfigBytes);

		EsINIState s = {};
		s.buffer = kernelConfig;
		s.bytes = kernelConfigBytes;

		char *source = NULL, *name = NULL;
		bool builtin = false;

		while (EsINIParse(&s)) {
			EsINIZeroTerminate(&s);

			if (strcmp(s.sectionClass, "driver")) {
				continue;
			}

			name = s.section;

			if (0 == strcmp(s.key, "source")) source = s.value;
			if (0 == strcmp(s.key, "builtin")) builtin = !!atoi(s.value);

			if (!EsINIPeek(&s) || !s.keyBytes) {
				if (IsDriverEnabled(name)) {
					fprintf(f, "[@driver]\nname=%s\nsource=%s\nbuiltin=%d\n\n", name, source ?: "", builtin);
				}

				source = name = NULL;
				builtin = false;
			}
		}

		free(kernelConfig);
	}

	fprintf(f, "[general]\nfor_emulator=%d\noptimise=%d\nskip_compile=%d\n\n", 
			!!(flags & COMPILE_FOR_EMULATOR), (flags & OPTIMISE_ON) || (flags & OPTIMISE_FULL), !!(flags & COMPILE_SKIP_COMPILE));

	uintptr_t fontIndex = 0;

	while (fonts[fontIndex].files) {
		BuildFont *font = fonts + fontIndex;
		fprintf(f, "[@font %s]\ncategory=%s\nscripts=%s\nlicense=%s\n", font->name, font->category, font->scripts, font->license);
		uintptr_t fileIndex = 0;

		while (font->files[fileIndex].path) {
			FontFile *file = font->files + fileIndex;
			fprintf(f, ".%s=%s\n", file->type, file->path);
			fileIndex++;
		}

		fprintf(f, "\n");
		fontIndex++;
	}

	if (~flags & COMPILE_SKIP_COMPILE) {
		struct dirent *entry;
		DIR *root = opendir("apps/");

		while (root && (entry = readdir(root))) {
			const char *manifestSuffix = ".ini";

			if (strlen(entry->d_name) <= strlen(manifestSuffix) || strcmp(entry->d_name + strlen(entry->d_name) - strlen(manifestSuffix), manifestSuffix)) {
				continue;
			}

			fprintf(f, "[@application]\nmanifest=apps/%s\n\n", entry->d_name);
		}

		closedir(root);

		EsINIState s = { (char *) LoadFile("bin/extra_applications.ini", &s.bytes) };

		while (s.buffer && EsINIParse(&s)) {
			if (!s.keyBytes || s.valueBytes || (s.keyBytes >= 1 && s.key[0] == ';')) continue;
			fprintf(f, "[@application]\nmanifest=%.*s\n\n", (int) s.keyBytes, s.key);
		}
	}

	if (flags & COMPILE_DO_BUILD) {
		fprintf(f, "[install]\nfile=bin/drive\npartition_size=%d\npartition_label=%s\n\n", partitionSize, volumeLabel ?: "Essence HD");
	}

	fclose(f);

	fflush(stdout);
	CallSystem("bin/build_core standard bin/build.ini");

	CallSystem("x86_64-essence-gcc -o root/Applications/POSIX/bin/hello ports/gcc/hello.c");

	forceRebuild = false;
}

void BuildUtilities() {
	buildStartTimeStamp = time(NULL);

#define BUILD_UTILITY(x, y, z) \
	if (CheckDependencies("Utilities." x)) { \
		if (!CallSystem("gcc -MMD util/" z x ".c -o bin/" x " -g -std=c2x " WARNING_FLAGS_C " " y)) { \
			ParseDependencies("bin/" x ".d", "Utilities." x, false); \
		} \
	}

	BUILD_UTILITY("render_svg", "-lm", "");
	BUILD_UTILITY("build_core", "-pthread -DPARALLEL_BUILD", "");

	if (canBuildLuigi) {
		BUILD_UTILITY("config_editor", "-lX11 -Wno-unused-parameter", "");

		if (CheckDependencies("Utilities.Designer")) {
			if (!CallSystem("g++ -MMD -D UI_LINUX -O3 util/designer2.cpp -o bin/designer2 -g -lX11 -Wno-unused-parameter " WARNING_FLAGS)) {
				ParseDependencies("bin/designer2.d", "Utilities.Designer", false);
			}
		}
	}
}

void Build(int optimise, bool compile) {
	struct timespec startTime, endTime;
	clock_gettime(CLOCK_REALTIME, &startTime);

	encounteredErrors = false;
	srand(time(NULL));
	printf("Build started...\n");
	CallSystem("mkdir -p root root/Applications root/Applications/POSIX root/Applications/POSIX/bin "
			"root/Applications/POSIX/lib root/Applications/POSIX/include ");

#if 0
	if (_installationIdentifier) {
		strcpy(_installationIdentifier, installationIdentifier);
	}
#endif

#ifndef __APPLE__
	CallSystem("ctags -R "
			"--exclude=old/* "
			"--exclude=root/* "
			"--exclude=bin/* "
			"--exclude=cross/* "
			"--exclude=ports/acpica/* "
			"--langdef=Eshg --langmap=Eshg:.header --regex-Eshg=\"/^struct[ \\t]*([a-zA-Z0-9_]+)/\\1/s,structure/\" "
			"--regex-Eshg=\"/^define[ \\t]*([a-zA-Z0-9_]+)/\\1/d,definition/\" --regex-Eshg=\"/^enum[ \\t]*([a-zA-Z0-9_]+)/\\1/e,enumeration/\" "
			"--regex-Eshg=\"/^function[ \\t]*([a-zA-Z0-9_]+)[ \\t\\*]*([a-zA-Z0-9_]+)/\\2/f,function/\" ."
			" &" /* don't block */);
#endif

	LoadOptions();
	Compile(optimise | (compile ? 0 : COMPILE_SKIP_COMPILE) | COMPILE_DO_BUILD | COMPILE_FOR_EMULATOR, 
			atoi(GetOptionString("Emulator.PrimaryDriveMB")), NULL);

	clock_gettime(CLOCK_REALTIME, &endTime);

	printf(ColorHighlight "%s" ColorNormal " in %.2fs.\n", encounteredErrors ? "\033[0;33mBuild failed" : "Build complete",
			(double) (endTime.tv_sec - startTime.tv_sec) + (double) (endTime.tv_nsec - startTime.tv_nsec) / 1000000000);
}

#define LOG_VERBOSE (0)
#define LOG_NORMAL (1)
#define LOG_NONE (2)
#define EMULATOR_QEMU (0)
#define EMULATOR_BOCHS (1)
#define EMULATOR_VIRTUALBOX (2)
#define DEBUG_LATER (0)
#define DEBUG_START (1)
#define DEBUG_NONE (2)

void Run(int emulator, int log, int debug) {
	LoadOptions();

	switch (emulator) {
		case EMULATOR_QEMU: {
			const char *biosFlags = "";
			const char *drivePrefix = "-drive file=bin/drive";

			if (IsOptionEnabled("Emulator.QemuEFI")) {
				CallSystem("util/uefi.sh");
				biosFlags = " -bios /usr/share/ovmf/x64/OVMF.fd ";
				drivePrefix = "-drive file=bin/uefi_drive";
			}

			const char *driveFlags = IsOptionEnabled("Emulator.ATA")  ? ",format=raw,media=disk,index=0 " : 
						 IsOptionEnabled("Emulator.AHCI") ? ",if=none,id=mydisk,format=raw,media=disk,index=0 "
						    		                    "-device ich9-ahci,id=ahci "
							       	                    "-device ide-hd,drive=mydisk,bus=ahci.0 "
						                                  : ",if=none,id=mydisk,format=raw "
                                                                                    "-device nvme,drive=mydisk,serial=1234 ";

			const char *cdromImage = GetOptionString("Emulator.CDROMImage");
			char cdromFlags[256];
			if (cdromImage && cdromImage[0]) {
				if (IsOptionEnabled("Emulator.ATA")) {
					snprintf(cdromFlags, sizeof(cdromFlags), " -cdrom %s ", cdromImage);
				} else {
					snprintf(cdromFlags, sizeof(cdromFlags), "-drive file=%s,if=none,id=mycdrom,format=raw,media=cdrom,index=1 "
							"-device ide-cd,drive=mycdrom,bus=ahci.1", cdromImage);
				}
			} else {
				cdromFlags[0] = 0;
			}

			const char *usbImage = GetOptionString("Emulator.USBImage");
			char usbFlags[256];
			if (usbImage && usbImage[0]) {
				snprintf(usbFlags, sizeof(usbFlags), " -drive if=none,id=stick,file=%s -device usb-storage,bus=xhci.0,drive=stick ", usbImage);
			} else {
				usbFlags[0] = 0;
			}

			const char *usbHostVendor = GetOptionString("Emulator.USBHostVendor");
			const char *usbHostProduct = GetOptionString("Emulator.USBHostProduct");
			char usbFlags2[256];
			if (usbHostVendor && usbHostVendor[0] && usbHostProduct && usbHostProduct[0]) {
				snprintf(usbFlags2, sizeof(usbFlags2), " -device usb-host,vendorid=0x%s,productid=0x%s,bus=xhci.0,id=myusb ", usbHostVendor, usbHostProduct);
			} else {
				usbFlags2[0] = 0;
			}

			bool withAudio = IsOptionEnabled("Emulator.Audio");
			const char *audioFlags = withAudio ? "QEMU_AUDIO_DRV=wav QEMU_WAV_PATH=bin/audio.wav " : "";
			const char *audioFlags2 = withAudio ? "-soundhw pcspk,hda" : "";
			unlink("bin/audio.wav");

			const char *secondaryDriveMB = GetOptionString("Emulator.SecondaryDriveMB");
			char secondaryDriveFlags[256];

			if (secondaryDriveMB && atoi(secondaryDriveMB)) {
				CallSystemF("dd if=/dev/zero of=bin/drive2 bs=1048576 count=%d", atoi(secondaryDriveMB));
				snprintf(secondaryDriveFlags, sizeof(secondaryDriveFlags), 
						"-drive file=bin/drive2,if=none,id=mydisk2,format=raw "
						"-device nvme,drive=mydisk2,serial=1234 ");
			} else {
				secondaryDriveFlags[0] = 0;
			}

			const char *logFlags = log == LOG_VERBOSE ? "-d cpu_reset,int > bin/qemu_log.txt 2>&1" 
				: (log == LOG_NORMAL ? " > bin/qemu_log.txt 2>&1" : " > /dev/null 2>&1");

			int cpuCores = atoi(GetOptionString("Emulator.Cores"));

			if (debug == DEBUG_NONE) {
				cpuCores = sysconf(_SC_NPROCESSORS_CONF);
				if (cpuCores < 1) cpuCores = 1;
				if (cpuCores > 16) cpuCores = 16;
			}

			char serialFlags[256];

			if (IsOptionEnabled("Emulator.SerialToFile")) {
				system("mv bin/qemu_serial7.txt bin/qemu_serial8.txt 2> /dev/null");
				system("mv bin/qemu_serial6.txt bin/qemu_serial7.txt 2> /dev/null");
				system("mv bin/qemu_serial5.txt bin/qemu_serial6.txt 2> /dev/null");
				system("mv bin/qemu_serial4.txt bin/qemu_serial5.txt 2> /dev/null");
				system("mv bin/qemu_serial3.txt bin/qemu_serial4.txt 2> /dev/null");
				system("mv bin/qemu_serial2.txt bin/qemu_serial3.txt 2> /dev/null");
				system("mv bin/qemu_serial1.txt bin/qemu_serial2.txt 2> /dev/null");
				strcpy(serialFlags, "-serial file:bin/qemu_serial1.txt");
			} else {
				serialFlags[0] = 0;
			}

			CallSystemF("%s %s qemu-system-x86_64 %s%s %s -m %d %s -smp cores=%d -cpu Haswell "
					" -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0,id=mykeyboard -device usb-mouse,bus=xhci.0,id=mymouse "
					" -netdev user,id=u1 -device e1000,netdev=u1 -object filter-dump,id=f1,netdev=u1,file=bin/net.dat "
					" %s %s %s %s %s %s %s ", 
					audioFlags, IsOptionEnabled("Emulator.RunWithSudo") ? "sudo " : "", drivePrefix, driveFlags, cdromFlags, 
					atoi(GetOptionString("Emulator.MemoryMB")), 
					debug ? (debug == DEBUG_NONE ? "-enable-kvm" : "-s -S") : "-s", 
					cpuCores, audioFlags2, logFlags, usbFlags, usbFlags2, secondaryDriveFlags, biosFlags, serialFlags);
		} break;

		case EMULATOR_BOCHS: {
			CallSystem("bochs -f bochs.config -q");
		} break;

		case EMULATOR_VIRTUALBOX: {
			// TODO Automatically setup the Essence VM if it doesn't exist. 

			CallSystem("VBoxManage storageattach Essence --storagectl AHCI --port 0 --device 0 --type hdd --medium none");
			CallSystem("VBoxManage closemedium disk bin/vbox.vdi --delete");

			if (IsOptionEnabled("Emulator.VBoxEFI")) {
				CallSystem("util/uefi.sh");
				CallSystem("VBoxManage convertfromraw bin/uefi_drive bin/vbox.vdi --format VDI");
				CallSystem("VBoxManage modifyvm Essence --firmware efi");
			} else {
				CallSystem("VBoxManage convertfromraw bin/drive bin/vbox.vdi --format VDI");
				CallSystem("VBoxManage modifyvm Essence --firmware bios");
			}

			CallSystem("VBoxManage storageattach Essence --storagectl AHCI --port 0 --device 0 --type hdd --medium bin/vbox.vdi");

			CallSystem("VBoxManage startvm --putenv VBOX_GUI_DBG_ENABLED=true Essence"); 
		} break;
	}
}

void BuildCrossCompiler() {
	if (!CallSystem("whoami | grep root")) {
		printf("Error: Build should not be run as root.\n");
		return;
	}

	{
		printf("\n");
		printf("A cross compiler for Essence needs to be built.\n");
		printf("- You need to be connected to the internet. ~100MB will be downloaded.\n");
		printf("- You need ~3GB of drive space available.\n");
		printf("- You need ~8GB of RAM available.\n");
		printf("- This should take ~20 minutes on a modern computer.\n");
		printf("- This does *not* require root permissions.\n");
		printf("- You must fully update your system before building.\n\n");

		bool missingPackages = false;
		if (CallSystem("which g++ > /dev/null 2>&1")) { printf("Error: Missing GCC/G++.\n"); missingPackages = true; }
		if (CallSystem("which make > /dev/null 2>&1")) { printf("Error: Missing GNU Make.\n"); missingPackages = true; }
		if (CallSystem("which bison > /dev/null 2>&1")) { printf("Error: Missing GNU Bison.\n"); missingPackages = true; }
		if (CallSystem("which flex > /dev/null 2>&1")) { printf("Error: Missing Flex.\n"); missingPackages = true; }
		if (CallSystem("which curl > /dev/null 2>&1")) { printf("Error: Missing curl.\n"); missingPackages = true; }
		if (CallSystem("which nasm > /dev/null 2>&1")) { printf("Error: Missing nasm.\n"); missingPackages = true; }
		if (CallSystem("which ctags > /dev/null 2>&1")) { printf("Error: Missing ctags.\n"); missingPackages = true; }
		if (CallSystem("which xz > /dev/null 2>&1")) { printf("Error: Missing xz.\n"); missingPackages = true; }
		if (CallSystem("which gzip > /dev/null 2>&1")) { printf("Error: Missing gzip.\n"); missingPackages = true; }
		if (CallSystem("which tar > /dev/null 2>&1")) { printf("Error: Missing tar.\n"); missingPackages = true; }
		if (CallSystem("which grep > /dev/null 2>&1")) { printf("Error: Missing grep.\n"); missingPackages = true; }
		if (CallSystem("which sed > /dev/null 2>&1")) { printf("Error: Missing sed.\n"); missingPackages = true; }
		if (CallSystem("which awk > /dev/null 2>&1")) { printf("Error: Missing awk.\n"); missingPackages = true; }

#ifdef __APPLE__
		if (CallSystem("gcc -L/opt/homebrew/lib -lmpc 2>&1 | grep -i undefined > /dev/null")) { printf("Error: Missing GNU MPC.\n"); missingPackages = true; }
		if (CallSystem("gcc -L/opt/homebrew/lib -lmpfr 2>&1 | grep -i undefined > /dev/null")) { printf("Error: Missing GNU MPFR.\n"); missingPackages = true; }
		if (CallSystem("gcc -L/opt/homebrew/lib -lgmp 2>&1 | grep -i undefined > /dev/null")) { printf("Error: Missing GNU GMP.\n"); missingPackages = true; }
#else
		if (CallSystem("gcc -lmpc 2>&1 | grep -i undefined > /dev/null")) { printf("Error: Missing GNU MPC.\n"); missingPackages = true; }
		if (CallSystem("gcc -lmpfr 2>&1 | grep -i undefined > /dev/null")) { printf("Error: Missing GNU MPFR.\n"); missingPackages = true; }
		if (CallSystem("gcc -lgmp 2>&1 | grep -i undefined > /dev/null")) { printf("Error: Missing GNU GMP.\n"); missingPackages = true; }
#endif

		if (missingPackages) exit(0);

		char installationFolder[4096];
		char sysrootFolder[4096];
		char path[65536];

		int processorCount = sysconf(_SC_NPROCESSORS_CONF);
		if (processorCount < 1) processorCount = 1;
		if (processorCount > 16) processorCount = 16;

		printf("Type 'yes' if you have updated your system.\n");
		if (!GetYes()) { printf("The build has been canceled.\n"); exit(0); }

		{
			getcwd(installationFolder, 4096);
			strcat(installationFolder, "/cross");
			getcwd(sysrootFolder, 4096);
			strcat(sysrootFolder, "/root");
			strcpy(compilerPath, installationFolder);
			strcat(compilerPath, "/bin");
			printf("\nType 'yes' to install the compiler into '%s'.\n", installationFolder);
			if (!GetYes()) { printf("The build has been canceled.\n"); exit(0); }
		}

		{
			char *originalPath = getenv("PATH");
			if (strlen(originalPath) > 32768) {
				printf("PATH too long\n");
				goto fail;
			}
			strcpy(path, compilerPath);
			strcat(path, ":");
			strcat(path, originalPath);
			setenv("PATH", path, 1);
		}

		{
			FILE *f = fopen("bin/running_makefiles", "r");

			if (f) {
				fclose(f);
				printf("\nThe build system has detected a build was started, but was not completed.\n");
				printf("Type 'yes' to attempt to resume this build.\n");

				if (GetYes()) {
					printf("Resuming build...\n");
					StartSpinner();
					goto runMakefiles;
				}
			}
		}

		CallSystemF("echo \"Started build of cross compiler index %d, with GCC " GCC_VERSION " and Binutils " BINUTILS_VERSION ". Using %d processors.\" > bin/build_cross.log", 
				CROSS_COMPILER_INDEX, processorCount);

		CallSystem("rm -rf cross bin/build-binutils bin/build-gcc bin/gcc-" GCC_VERSION " bin/binutils-" BINUTILS_VERSION);

		{
			CallSystem("echo Preparing C standard library headers... >> bin/build_cross.log");
			CallSystem("mkdir -p root/" SYSTEM_FOLDER_NAME " root/Applications/POSIX/include root/Applications/POSIX/lib root/Applications/POSIX/bin");
			CallSystem("ports/musl/build.sh >> bin/build_cross.log 2>&1");
		}

		printf("Downloading and extracting source...\n");

		if (CallSystem("ports/gcc/port.sh download-only")) {
			goto fail;
		}

		printf("Building GCC...\n");
		StartSpinner();

		{
			CallSystem("echo Running configure... >> bin/build_cross.log");
			if (CallSystem("mkdir bin/build-binutils")) goto fail;
			if (CallSystem("mkdir bin/build-gcc")) goto fail;
			if (chdir("bin/build-binutils")) goto fail;
			if (CallSystemF("../binutils-src/configure --target=x86_64-essence --prefix=\"%s\" --with-sysroot=%s --disable-nls --disable-werror >> ../build_cross.log 2>&1", 
						installationFolder, sysrootFolder)) goto fail;
			if (chdir("../..")) goto fail;
			if (chdir("bin/build-gcc")) goto fail;
			// Add --without-headers for a x86_64-elf build.
			if (CallSystemF("../gcc-src/configure --target=x86_64-essence --prefix=\"%s\" --enable-languages=c,c++ --with-sysroot=%s --disable-nls >> ../build_cross.log 2>&1", 
						installationFolder, sysrootFolder)) goto fail;
			if (chdir("../..")) goto fail;
		}

		runMakefiles:;

		{
			CallSystem("touch bin/running_makefiles");

			CallSystem("echo Building Binutils... >> bin/build_cross.log");
			if (chdir("bin/build-binutils")) goto fail;
			if (CallSystemF("make -j%d >> ../build_cross.log 2>&1", processorCount)) goto fail;
			if (CallSystem("make install >> ../build_cross.log 2>&1")) goto fail;
			if (chdir("../..")) goto fail;

			CallSystem("echo Building GCC... >> bin/build_cross.log");
			if (chdir("bin/build-gcc")) goto fail;
			if (CallSystemF("make all-gcc -j%d >> ../build_cross.log 2>&1", processorCount)) goto fail;
			if (CallSystem("make all-target-libgcc >> ../build_cross.log 2>&1")) goto fail;
			if (CallSystem("make install-gcc >> ../build_cross.log 2>&1")) goto fail;
			if (CallSystem("make install-target-libgcc >> ../build_cross.log 2>&1")) goto fail;
			if (chdir("../..")) goto fail;
		}

		{
			CallSystem("echo Removing debug symbols... >> bin/build_cross.log");
			CallSystemF("strip %s/bin/* >> bin/build_cross.log 2>&1", installationFolder);
			CallSystemF("strip %s/libexec/gcc/x86_64-essence/" GCC_VERSION "/* >> bin/build_cross.log 2>&1", installationFolder);
		}

		{
			CallSystem("echo Modifying headers... >> bin/build_cross.log");
			sprintf(path, "%s/lib/gcc/x86_64-essence/" GCC_VERSION "/include/mm_malloc.h", installationFolder);
			FILE *file = fopen(path, "w");
			if (!file) {
				printf("Couldn't modify header files\n");
				goto fail;
			} else {
				fprintf(file, "/*Removed*/\n");
				fclose(file);
			}
		}

		StopSpinner();

		{
			BuildUtilities();
			BuildAPIDependencies();
			FILE *f = fopen("bin/build.ini", "wb");
			OutputStartOfBuildINI(f, false);
			fprintf(f, "[general]\nwithout_kernel=1\n");
			fclose(f);
			if (CallSystem("bin/build_core standard bin/build.ini")) goto fail;
		}

		StartSpinner();

		{
			if (chdir("bin/build-gcc")) goto fail;
			if (CallSystemF("make -j%d all-target-libstdc++-v3 >> ../build_cross.log 2>&1", processorCount)) goto fail;
			if (CallSystem("make install-target-libstdc++-v3 >> ../build_cross.log 2>&1")) goto fail;
			if (chdir("../..")) goto fail;
		}

		{
			CallSystem("echo Cleaning up... >> bin/build_cross.log");
			CallSystem("rm -rf bin/binutils-src bin/gcc-src bin/gmp-src bin/mpc-src bin/mpfr-src");
			CallSystem("rm -rf bin/build-binutils bin/build-gcc");
			CallSystem("rm bin/running_makefiles");
		}

		StopSpinner();

		printf(ColorHighlight "\nThe cross compiler has built successfully.\n" ColorNormal);
		foundValidCrossCompiler = true;
	}

	return;
	fail:;
	StopSpinner();
	printf("\nThe build has failed. A log is available in " ColorHighlight "bin/build_cross.log" ColorNormal ".\n");
	exit(0);
}

void SaveConfig() {
	FILE *f = fopen("bin/build_config.ini", "wb");
	fprintf(f, "accepted_license=%d\ncompiler_path=%s\n"
			"cross_compiler_index=%d\n",
			acceptedLicense, compilerPath, 
			foundValidCrossCompiler ? CROSS_COMPILER_INDEX : 0);
	fclose(f);
}

const char *folders[] = {
	"apps",
	"desktop",
	"kernel",
	"shared",
	"boot",
	"drivers",
};

void Find(char *l2, char *in, bool wordOnly) {
	for (uintptr_t i = 0; i < sizeof(folders) / sizeof(folders[0]); i++) {
		char buffer[256];
		sprintf(buffer, "grep --color -nr '%s' -e '%s%s%s'", in ?: folders[i], wordOnly ? "\\b" : "", l2, wordOnly ? "\\b" : "");
		CallSystem(buffer);
		if (in) break;
	}
}

void Replace(char *l2, char *l3, char *in) {
	for (uintptr_t i = 0; i < sizeof(folders) / sizeof(folders[0]); i++) {
		char buffer[256];
		sprintf(buffer, "find %s -type f -exec sed -i 's/\\b%s\\b/%s/g' {} \\;", in ?: folders[i], l2, l3);
		CallSystem(buffer);
		if (in) break;
	}
}

typedef struct ReplacedFieldNameError {
	char *file;
	int line, position;
} ReplacedFieldNameError;

void FixReplacedFieldName(const char *oldName, const char *newName) {
	CallSystem("./start.sh c 2> bin/errors.tmp");
	char *errors = (char *) LoadFile("bin/errors.tmp", NULL); 
	char *position = errors;

	char needle[256];
	snprintf(needle, sizeof(needle), "has no member named '%s'", oldName);

	ReplacedFieldNameError *parsedErrors = NULL;

	while (position) {
		char *s = strstr(position, needle);

		if (!s) {
			break;
		}

		while (s != position) {
			if (*s == '\n') {
				break;
			}

			s--;
		}

		s++;

		char *fileStart = s;
		char *fileEnd = strchr(fileStart, ':');
		char *lineStart = fileEnd + 1;
		char *lineEnd = strchr(lineStart, ':');
		char *positionStart = lineEnd + 1;
		char *positionEnd = strchr(positionStart, ':');

		position = strchr(s, '\n');

		*fileEnd = 0;
		*lineEnd = 0;
		*positionEnd = 0;

		ReplacedFieldNameError error = { fileStart, atoi(lineStart), atoi(positionStart) };
		arrput(parsedErrors, error);
		printf("Parsed error: file %s, line %d, position %d.\n", error.file, error.line, error.position);
	}

	while (arrlenu(parsedErrors)) {
		char *path = arrlast(parsedErrors).file;
		size_t fileLength;
		char *file = (char *) LoadFile(path, &fileLength);

		if (!file) {
			printf("Error: Could not access '%s'.\n", path);
			(void) arrpop(parsedErrors);
			continue;
		}

		printf("Replacing errors in file %s...\n", path);

		for (uintptr_t i = 0; i < arrlenu(parsedErrors); i++) {
			if (strcmp(parsedErrors[i].file, path)) {
				continue;
			}

			// printf("(%s:%d:%d)\n", parsedErrors[i].file, parsedErrors[i].line, parsedErrors[i].position);

			int line = 1;
			char *position = file;

			while (line != parsedErrors[i].line) {
				position = strchr(position, '\n');

				if (!position) {
					printf("Error: File '%s' has less lines (%d) than expected (%d).\n", path, line, parsedErrors[i].line);
					goto nextError;
				}

				position++;
				line++;
			}

			{
				char *end = strchr(position, '\n');
				if (!end) end = position + strlen(position);
				size_t lineLength = end - position;

				if (parsedErrors[i].position + strlen(oldName) > lineLength) {
					printf("Error: Line %s:%d was shorter than expected (want to replace field at %d).\n", path, line, parsedErrors[i].position);
					goto nextError;
				}

				if (memcmp(parsedErrors[i].position + position - 1, oldName, strlen(oldName))) {
					printf("Warning: Line %s:%d did not contain old field name at position %d.\n", 
							path, line, parsedErrors[i].position);
					goto nextError;
				}

				size_t oldFileLength = fileLength;
				size_t positionOffset = position - file + parsedErrors[i].position - 1;
				fileLength = oldFileLength - strlen(oldName) + strlen(newName);
				if (fileLength > oldFileLength) file = (char *) realloc(file, fileLength + 1);
				position = file + positionOffset;
				memmove(position + strlen(newName), position + strlen(oldName), oldFileLength - positionOffset - strlen(oldName));
				memcpy(position, newName, strlen(newName));
				file[fileLength] = 0;

				for (uintptr_t j = i + 1; j < arrlenu(parsedErrors); j++) {
					if (strcmp(parsedErrors[i].file, parsedErrors[j].file) 
							|| parsedErrors[i].line != parsedErrors[j].line
							|| parsedErrors[i].position > parsedErrors[j].position) {
						continue;
					}

					if (parsedErrors[i].position == parsedErrors[j].position) {
						arrdel(parsedErrors, j);
						j--;
					} else {
						parsedErrors[j].position += strlen(newName) - strlen(oldName);
					}
				}
			}

			nextError:;
			arrdel(parsedErrors, i);
			i--;
		}

		FILE *f = fopen(path, "wb");
		fwrite(file, 1, fileLength, f);
		fclose(f);
	}

	arrfree(parsedErrors);
	free(errors);
	CallSystem("unlink bin/errors.tmp");
}

void LineCountFile(const char *folder, const char *name) {
	int lineCountBefore = 0;

	{
		CallSystem("paste -sd+ bin/count.tmp | bc > bin/count2.tmp");
		FILE *f = fopen("bin/count2.tmp", "rb");
		char buffer[16] = {};
		fread(buffer, 1, sizeof(buffer), f);
		fclose(f);
		lineCountBefore = atoi(buffer);
	}

	CallSystemF("awk 'NF' \"%s%s\" | wc -l >> bin/count.tmp", folder, name);

	{
		CallSystem("paste -sd+ bin/count.tmp | bc > bin/count2.tmp");
		FILE *f = fopen("bin/count2.tmp", "rb");
		char buffer[16] = {};
		fread(buffer, 1, sizeof(buffer), f);
		fclose(f);
		printf("%5d   %s%s\n", atoi(buffer) - lineCountBefore, folder, name);
	}
}

void AddressToLine(const char *symbolFile) {
	char buffer[4096];
	sprintf(buffer, "echo %s > bin/all_symbol_files.dat", symbolFile);
	system(buffer);
		
	system("echo bin/Kernel >> bin/all_symbol_files.dat && echo bin/Desktop >> bin/all_symbol_files.dat");
	char symbolFiles[4096] = {};
	fread(symbolFiles, 1, 4096, fopen("bin/all_symbol_files.dat", "rb"));
	for (int i = 0; i < 4096; i++) if (symbolFiles[i] == '\n') symbolFiles[i] = 0;
	system("rm -f bin/all_symbol_files.dat");

	char root[4096];
	getcwd(root, 4096);

	while (true) {
		char *line = NULL;
		size_t bytes = 0;
		getline(&line, &bytes, stdin);

		for (uintptr_t i = 0; i < strlen(line); i++) {
			if (line[i] == '0' && line[i + 1] == 'x') {
				int si = i;
				uint64_t address = 0;
				i += 2;

				for (int j = 0; j < 16; j++) {
					if (line[i] == '_') i++;
					if (line[i] >= '0' && line[i] <= '9') address = (address * 16) + line[i++] - '0';
					else if (line[i] >= 'A' && line[i] <= 'F') address = (address * 16) + line[i++] - 'A' + 10;
					else if (line[i] >= 'a' && line[i] <= 'f') address = (address * 16) + line[i++] - 'a' + 10;
					else break;
				}

				char *file = symbolFiles;

				while (*file) {
					sprintf(buffer, "addr2line --exe=\"%s\" 0x%lx | grep -v ? >> bin/result.tmp", file, address);
					system(buffer);
					file += strlen(file) + 1;
				}

				{
					char result[4096];
					FILE *file = fopen("bin/result.tmp", "rb");
					result[fread(result, 1, 4096, file)] = 0;
					for (int i = 0; i < 4096; i++) if (result[i] == '\n') result[i] = 0;

					if (result[0] == 0) {
						putchar('0');
						i = si;
					} else {
						free(file);
						fputs(strstr(result, "essence") ?: "", stdout);
						system("rm -f bin/result.tmp");
						i--;
					}
				}
			} else {
				putchar(line[i]);
			}
		}

		free(line);
	}
}

void GatherFilesForInstallerArchive(FILE *file, const char *path1, const char *path2, uint64_t *crc64, uint64_t *totalUncompressedSize) {
	char path[4096], path3[4096];
	snprintf(path, sizeof(path), "%s%s", path1, path2);

	DIR *directory = opendir(path);
	struct dirent *entry;

	while ((entry = readdir(directory))) {
		if (0 == strcmp(entry->d_name, ".") || 0 == strcmp(entry->d_name, "..")) {
			continue;
		}

		snprintf(path, sizeof(path), "%s%s/%s", path1, path2, entry->d_name);
		snprintf(path3, sizeof(path3), "%s/%s", path2, entry->d_name);

		struct stat s = {};
		lstat(path, &s);

		if (S_ISDIR(s.st_mode)) {
			GatherFilesForInstallerArchive(file, path1, path3, crc64, totalUncompressedSize);
		} else if (S_ISREG(s.st_mode)) {
			size_t _length;
			void *data = LoadFile(path, &_length);
			printf("%s, %d KB\n", path3, (int) (_length / 1000));
			uint64_t length = _length;
			fwrite(&length, 1, sizeof(length), file);
			uint16_t pathBytes = strlen(path3);
			fwrite(&pathBytes, 1, sizeof(pathBytes), file);
			fwrite(path3, 1, pathBytes, file);
			fwrite(data, 1, length, file);
			*crc64 = CalculateCRC64(data, length, *crc64);
			*totalUncompressedSize += length;
			free(data);
		} else {
			printf("skipping: %s\n", path3);
		}
	}

	closedir(directory);
}

void BuildAndRun(int optimise, bool compile, int debug, int emulator) {
	Build(optimise, compile);

	if (encounteredErrors) {
		printf("Errors were encountered during the build.\n");
	} else if (emulator != -1) {
		Run(emulator, LOG_NORMAL, debug);
	}
}

void DoCommand(const char *l) {
	while (l && (*l == ' ' || *l == '\t')) l++;

	{
		struct stat s = {};
		static time_t buildSystemTimeStamp = 0;

		if (stat("util/build.c", &s) || s.st_mtime > buildSystemTimeStamp) {
			if (buildSystemTimeStamp) {
				printf("\033[0;33mWarning: The build system appears to be out of date.\nPlease exit and re-run ./start.sh.\n" ColorNormal);
			} else {
				buildSystemTimeStamp = s.st_mtime;
			}
		}
	}

	if (0 == strcmp(l, "b") || 0 == strcmp(l, "build")) {
		BuildAndRun(OPTIMISE_OFF, true /* compile */, false /* debug */, -1);
	} else if (0 == strcmp(l, "opt") || 0 == strcmp(l, "build-optimised")) {
		BuildAndRun(OPTIMISE_ON, true /* compile */, false /* debug */, -1);
	} else if (0 == strcmp(l, "d") || 0 == strcmp(l, "debug")) {
		BuildAndRun(OPTIMISE_OFF, true /* compile */, true /* debug */, EMULATOR_QEMU);
	} else if (0 == strcmp(l, "d3") || 0 == strcmp(l, "debug-without-compile")) {
		BuildAndRun(OPTIMISE_OFF, false /* compile */, true /* debug */, EMULATOR_QEMU);
	} else if (0 == strcmp(l, "v") || 0 == strcmp(l, "vbox")) {
		BuildAndRun(OPTIMISE_ON, true /* compile */, false /* debug */, EMULATOR_VIRTUALBOX);
	} else if (0 == strcmp(l, "v2") || 0 == strcmp(l, "vbox-without-opt")) {
		BuildAndRun(OPTIMISE_OFF, true /* compile */, false /* debug */, EMULATOR_VIRTUALBOX);
	} else if (0 == strcmp(l, "v3") || 0 == strcmp(l, "vbox-without-compile")) {
		BuildAndRun(OPTIMISE_OFF, false /* compile */, false /* debug */, EMULATOR_VIRTUALBOX);
	} else if (0 == strcmp(l, "t") || 0 == strcmp(l, "qemu-with-opt")) {
		BuildAndRun(OPTIMISE_ON, true /* compile */, false /* debug */, EMULATOR_QEMU);
	} else if (0 == strcmp(l, "t2") || 0 == strcmp(l, "test")) {
		BuildAndRun(OPTIMISE_OFF, true /* compile */, false /* debug */, EMULATOR_QEMU);
	} else if (0 == strcmp(l, "t3") || 0 == strcmp(l, "qemu-without-compile")) {
		BuildAndRun(OPTIMISE_OFF, false /* compile */, false /* debug */, EMULATOR_QEMU);
	} else if (0 == strcmp(l, "k") || 0 == strcmp(l, "qemu-with-kvm")) {
		BuildAndRun(OPTIMISE_FULL, true /* compile */, DEBUG_NONE /* debug */, EMULATOR_QEMU);
	} else if (0 == strcmp(l, "exit") || 0 == strcmp(l, "x") || 0 == strcmp(l, "quit") || 0 == strcmp(l, "q")) {
		exit(0);
	} else if (0 == strcmp(l, "compile") || 0 == strcmp(l, "c")) {
		LoadOptions();
		Compile(COMPILE_FOR_EMULATOR, atoi(GetOptionString("Emulator.PrimaryDriveMB")), NULL);
	} else if (0 == strcmp(l, "build-cross")) {
		BuildCrossCompiler();
		SaveConfig();
		printf("Please restart the build system.\n");
		exit(0);
	} else if (0 == strcmp(l, "build-utilities") || 0 == strcmp(l, "u")) {
		BuildUtilities();
	} else if (0 == strcmp(l, "make-installer-archive")) {
		CallSystem("gcc -O3 -o bin/lzma ports/lzma/LzmaUtil.c ports/lzma/LzmaDec.c ports/lzma/LzmaEnc.c "
				"ports/lzma/7zStream.c ports/lzma/Threads.c ports/lzma/LzFindMt.c ports/lzma/LzFind.c "
				"ports/lzma/7zFile.c ports/lzma/Alloc.c ports/lzma/CpuArch.c -pthread");
		FILE *f = fopen("bin/temp.dat", "wb");
		uint64_t crc64 = 0, uncompressed = 0;
		GatherFilesForInstallerArchive(f, "root", "", &crc64, &uncompressed);
		uint32_t sizeMB = ftell(f) / 1000000;
		fclose(f);
		printf("Compressing %d MB...\n", sizeMB);
		CallSystem("bin/lzma e bin/temp.dat bin/installer_archive.dat");
		struct stat s = {};
		lstat("bin/installer_archive.dat", &s);
		printf("Compressed to %d MB.\n", (uint32_t) (s.st_size / 1000000));
		unlink("bin/temp.dat");
		f = fopen("bin/installer_metadata.dat", "wb");
		fwrite(&uncompressed, 1, sizeof(uncompressed), f);
		fwrite(&crc64, 1, sizeof(crc64), f);
		fclose(f);
	} else if (0 == strcmp(l, "make-installer-root")) {
		CallSystem("rm -r root/Installer\\ Data");
		DoCommand("make-installer-archive");
		CallSystem("util/uefi_compile.sh");
		CallSystem("mkdir root/Installer\\ Data");
		CallSystem("cp bin/mbr root/Installer\\ Data/mbr.dat");
		CallSystem("cp bin/stage1 root/Installer\\ Data/stage1.dat");
		CallSystem("cp bin/stage2 root/Installer\\ Data/stage2.dat");
		CallSystem("cp bin/uefi root/Installer\\ Data/uefi1.dat");
		CallSystem("cp bin/uefi_loader root/Installer\\ Data/uefi2.dat");
		CallSystem("cp LICENSE.md root/Installer\\ Data/licenses.txt");
		CallSystem("mv bin/installer_archive.dat root/Installer\\ Data/archive.dat");
		CallSystem("mv bin/installer_metadata.dat root/Installer\\ Data/metadata.dat");
	} else if (0 == strcmp(l, "config")) {
		BuildUtilities();
		CallSystem("bin/config_editor");
	} else if (0 == strcmp(l, "designer2")) {
		BuildUtilities();
		CallSystem("bin/designer2");
	} else if (0 == strcmp(l, "replace-many")) {
		forceRebuild = true;
		printf("Enter the name of the replacement file: ");
		char *l2 = NULL;
		size_t pos;
		getline(&l2, &pos, stdin);
		l2[strlen(l2) - 1] = 0;
		FILE *f = fopen(l2, "r");
		free(l2);
		while (!feof(f)) {
			char a[512], b[512];
			fscanf(f, "%s %s", a, b);
			printf("%s -> %s\n", a, b);
			Replace(a, b, NULL);
		}
		fclose(f);
	} else if (0 == strcmp(l, "find")) {
		printf("Enter the query to be found: ");
		char *l2 = NULL;
		size_t pos;
		getline(&l2, &pos, stdin);
		l2[strlen(l2) - 1] = 0;
		Find(l2, NULL, false);
		free(l2);
	} else if (0 == strcmp(l, "find-word")) {
		printf("Enter the query to be found: ");
		char *l2 = NULL;
		size_t pos;
		getline(&l2, &pos, stdin);
		l2[strlen(l2) - 1] = 0;
		Find(l2, NULL, true);
		free(l2);
	} else if (0 == strcmp(l, "find-in")) {
		printf("Enter the folder to search in: ");
		char *in = NULL;
		size_t pos;
		getline(&in, &pos, stdin);
		in[strlen(in) - 1] = 0;
		printf("Enter the query to be found: ");
		char *l2 = NULL;
		getline(&l2, &pos, stdin);
		l2[strlen(l2) - 1] = 0;
		Find(l2, in, false);
		free(l2);
		free(in);
	} else if (0 == strcmp(l, "replace")) {
		forceRebuild = true;
		printf("Enter the word to be replaced: ");
		char *l2 = NULL;
		size_t pos;
		getline(&l2, &pos, stdin);
		printf("Enter the word to replace it with: ");
		char *l3 = NULL;
		getline(&l3, &pos, stdin);
		l2[strlen(l2) - 1] = 0;
		l3[strlen(l3) - 1] = 0;
		Replace(l2, l3, NULL);
		free(l2);
		free(l3);
	} else if (0 == strcmp(l, "replace-in")) {
		forceRebuild = true;
		printf("Enter the folder to replace in: ");
		char *in = NULL;
		size_t pos;
		getline(&in, &pos, stdin);
		in[strlen(in) - 1] = 0;
		printf("Enter the word to be replaced: ");
		char *l2 = NULL;
		getline(&l2, &pos, stdin);
		printf("Enter the word to replace it with: ");
		char *l3 = NULL;
		getline(&l3, &pos, stdin);
		l2[strlen(l2) - 1] = 0;
		l3[strlen(l3) - 1] = 0;
		Replace(l2, l3, in);
		free(l2);
		free(l3);
		free(in);
	} else if (0 == strcmp(l, "fix-replaced-field-name")) {
		size_t pos;

		printf(">> Please make sure you have saved a backup before running this command!! <<\n");
		printf("Do not try to rename fields from different structures at the same time.\n\n");

		printf("Enter the old name of the field: ");
		char *oldName = NULL;
		getline(&oldName, &pos, stdin);
		oldName[strlen(oldName) - 1] = 0;

		printf("Enter the new name of the field: ");
		char *newName = NULL;
		getline(&newName, &pos, stdin);
		newName[strlen(newName) - 1] = 0;

		FixReplacedFieldName(oldName, newName);

		free(oldName);
		free(newName);
	} else if (0 == memcmp(l, "ascii ", 6)) {
		const char *text = l + 6;

		while (*text) {
			char c = *text;
			printf("0x%.2X - %.3d - '%c'\n", c, c, c);
			text++;
		}
	} else if (0 == strcmp(l, "ascii")) {
		for (int c = 32; c < 127; c++) {
			printf("0x%.2X - %.3d - '%c'", c, c, c);
			if ((c & 3) == 3) printf("\n"); else printf("       ");
		}

		printf("\n");
	} else if (0 == strcmp(l, "build-optional-ports")) {
		DIR *directory = opendir("ports");
		struct dirent *entry;

		while ((entry = readdir(directory))) {
			CallSystemF("ports/%s/port.sh", entry->d_name);
		}

		closedir(directory);
	} else if (0 == memcmp(l, "do ", 3)) {
		CallSystem(l + 3);
	} else if (0 == memcmp(l, "live ", 5)) {
		if (interactiveMode) {
			fprintf(stderr, "This command cannot be used in interactive mode. Type \"quit\" and then run \"./start.sh live <...>\".\n");
			return;
		}

		if (argc < 4) {
			fprintf(stderr, "Usage: \"./start.sh live <iso/raw> <drive size in MB> [extra options]\".\n");
			return;
		}

		bool wantISO = 0 == strcmp(argv[2], "iso");
		uint32_t flags = OPTIMISE_ON | COMPILE_DO_BUILD;
		const char *label = NULL;

		for (int i = 4; i < argc; i++) {
			if (0 == strcmp(argv[i], "noopt")) {
				flags &= ~OPTIMISE_ON;
				flags |= OPTIMISE_OFF;
			} else if (0 == memcmp(argv[i], "label=", 6)) {
				label = argv[i] + 6;
			}
		}

		forceRebuild = true;
		Compile(flags, atoi(argv[3]), label);

		if (encounteredErrors) {
			printf("\033[0;33mBuild failed.\n" ColorNormal);
			return;
		}

		if (!wantISO) {
			printf("You can copy the image to the device with "
					ColorHighlight "sudo dd if=bin/drive of=<path to drive> bs=1024 count=%d conv=notrunc" ColorNormal "\n", 
					atoi(argv[3]) * 1024);
			return;
		}

		if (CallSystem("xorriso --version")) {
			printf("\033[0;33mCould not produce iso image; xorriso not found.\n" ColorNormal);
			return;
		}

		char buffer[512];
		FILE *f = fopen("bin/drive", "rb");
		fseek(f, 0x102000, SEEK_SET);
		fread(buffer, 1, sizeof(buffer), f);
		fclose(f);

		if (memcmp(buffer, "!EssenceFS2", 11)) {
			printf("\033[0;33mCould not produce iso image (1).\n" ColorNormal);
			return;
		}

		f = fopen("bin/appuse.txt", "wb");
		fprintf(f, "Essence::%.*s", 16, buffer + 152);
		fclose(f);

		unlink("bin/essence.iso");

		if (CallSystem("xorriso -rockridge \"off\" -outdev bin/essence.iso -blank as_needed -volid \"Essence Installation Disc\" "
				"-map bin/drive /ESSENCE.DAT -boot_image any bin_path=/ESSENCE.DAT -boot_image any emul_type=hard_disk "
				"-application_use bin/appuse.txt")) {
			printf("\033[0;33mCould not produce iso image (2).\n" ColorNormal);
			return;
		}

		printf("Created " ColorHighlight "bin/essence.iso" ColorNormal ".\n");
	} else if (0 == strcmp(l, "line-count")) {
		FILE *f = fopen("bin/count.tmp", "wb");
		fprintf(f, "0");
		fclose(f);

		LineCountFile("", "start.sh");

		const char *folders[] = {
			"desktop/", "boot/x86/", "drivers/", "kernel/", "apps/", "apps/file_manager/", "shared/", "util/"
		};

		for (uintptr_t i = 0; i < sizeof(folders) / sizeof(folders[0]); i++) {
			const char *folder = folders[i];
			DIR *directory = opendir(folder);
			struct dirent *entry;

			while ((entry = readdir(directory))) {
				if (0 == strcmp(entry->d_name, "nanosvg.h")) continue;
				if (0 == strcmp(entry->d_name, "hsluv.h")) continue;
				if (0 == strcmp(entry->d_name, "stb_ds.h")) continue;
				if (0 == strcmp(entry->d_name, "stb_image.h")) continue;
				if (0 == strcmp(entry->d_name, "stb_sprintf.h")) continue;
				if (0 == strcmp(entry->d_name, "stb_truetype.h")) continue;
				if (entry->d_type != DT_REG) continue;

				LineCountFile(folder, entry->d_name);
			}

			closedir(directory);
		}

		printf("\nTotal line count:" ColorHighlight "\n");
		CallSystem("paste -sd+ bin/count.tmp | bc");
		unlink("bin/count.tmp");
		unlink("bin/count2.tmp");
		printf(ColorNormal);
	} else if (0 == memcmp(l, "a2l ", 4)) {
		AddressToLine(l + 3);
	} else if (0 == memcmp(l, "get-source ", 11)) {
		if (CallSystem("mkdir -p bin/cache && rm -rf bin/source")) {
			exit(1);
		}

		const char *folder = l + 11;
		const char *url = NULL;

		for (int i = 0; folder[i]; i++) {
			if (folder[i] == ' ') {
				url = folder + i + 1;
				break;
			}
		}

		assert(url);

		char name[1024];
		strcpy(name, "bin/cache/");

		const char *extension = url;

		for (int i = 0; url[i]; i++) {
			name[i + 10] = isalnum(url[i]) ? url[i] : '_';
			name[i + 11] = 0;
			if (url[i] == '.') extension = url + i;
		}

		char decompressFlag;

		if (0 == strcmp(extension, ".bz2")) {
			decompressFlag = 'j';
		} else if (0 == strcmp(extension, ".xz")) {
			decompressFlag = 'J';
		} else if (0 == strcmp(extension, ".gz")) {
			decompressFlag = 'z';
		} else {
			fprintf(stderr, "Unknown archive format.\n");
			exit(1);
		}

		FILE *f = fopen(name, "rb");

		if (f) {
			fclose(f);
		} else if (CallSystemF("curl %s > %s", url, name)) {
			CallSystemF("rm %s", name); // Remove partially downloaded file.
			exit(1);
		}

		if (CallSystemF("tar -x%cf %s", decompressFlag, name)) exit(1);
		if (CallSystemF("mv %.*s bin/source", (int) (url - folder), folder)) exit(1);
	} else if (0 == strcmp(l, "make-crash-report")) {
		system("rm crash-report.tar.gz");
		system("mkdir crash-report");
		system("cp bin/qemu_serial* crash-report");
		system("cp bin/Kernel crash-report");
		system("cp bin/Desktop crash-report");
		system("cp bin/File\\ Manager crash-report");
		system("cp bin/config.ini crash-report");
		system("cp bin/build.ini crash-report");
		system("cp root/Essence/Default.ini crash-report");
		system("cp -r kernel crash-report");
		system("cp -r desktop crash-report");
		system("git diff > crash-report/git-diff.txt");
		system("tar -czf crash-report.tar.gz crash-report");
		system("rm -r crash-report");
		char cwd[PATH_MAX];
		getcwd(cwd, sizeof(cwd));
		strcat(cwd, "/crash-report.tar.gz");
		fprintf(stderr, "Crash report made at " ColorHighlight "%s" ColorNormal ".\n", cwd);
	} else if (0 == strcmp(l, "help") || 0 == strcmp(l, "h") || 0 == strcmp(l, "?")) {
		printf(ColorHighlight "\n=== Common Commands ===\n" ColorNormal);
		printf("build         (b)                 - Build.\n");
		printf("qemu-with-kvm (k)                 - Build (with optimisations enabled) and run in Qemu with KVM.\n");
		printf("test          (t2)                - Build and run in Qemu.\n");
		printf("vbox          (v)                 - Build (with optimisations enabled) and run in VirtualBox.\n");
		printf("debug         (d)                 - Build and run in Qemu (GDB server enabled).\n");
		printf("config                            - Open the local configuration editor.\n");
		printf("exit                              - Exit the build system.\n");

		printf(ColorHighlight "\n=== Search and replace ===\n" ColorNormal);
		printf("find, find-word, find-in          - Search the project's source code.\n");
		printf("replace-many, replace, replace-in - Replace a word in throughout the source code.\n");
		printf("fix-replaced-field-name           - Replace usages of a struct field after changing it.\n");

		printf(ColorHighlight "\n=== Special builds ===\n" ColorNormal);
		printf("build-optional-ports              - Build the applications in ports/.\n");
		printf("live                              - Create a live USB or CDROM.\n");

		printf(ColorHighlight "\n=== Utilities ===\n" ColorNormal);
		printf("designer2                         - Open the interface style designer.\n");
		printf("line-count                        - Count lines of code.\n");
		printf("ascii <string>                    - Convert a string to a list of ASCII codepoints.\n");
		printf("a2l <executable>                  - Translate addresses to lines.\n");
		printf("make-crash-report                 - Make a crash report.\n");
	} else {
		printf("Unrecognised command '%s'. Enter 'help' to get a list of commands.\n", l);
	}
}

int main(int _argc, char **_argv) {
	argc = _argc;
	argv = _argv;

	char cwd[PATH_MAX];
	getcwd(cwd, sizeof(cwd));

	if (strchr(cwd, ' ')) {
		printf("Error: The path to your essence directory, '%s', contains spaces.\n", cwd);
		return 1;
	}

	sh_new_strdup(applicationDependencies);
	unlink("bin/dependencies.ini");

	coloredOutput = isatty(STDERR_FILENO);

	if (argc == 1) {
		printf(ColorHighlight "Essence Build" ColorNormal "\nPress Ctrl-C to exit.\n");
	}

	systemLog = fopen("bin/system.log", "w");

	{
		EsINIState s = { (char *) LoadFile("bin/build_config.ini", &s.bytes) };
		char path[32768 + PATH_MAX + 16];
		path[0] = 0;

		while (EsINIParse(&s)) {
			if (s.sectionClassBytes || s.sectionBytes) continue;
			EsINIZeroTerminate(&s);

			INI_READ_BOOL(accepted_license, acceptedLicense);
			INI_READ_STRING(compiler_path, path);

			if (0 == strcmp("cross_compiler_index", s.key)) {
				foundValidCrossCompiler = atoi(s.value) == CROSS_COMPILER_INDEX;
			}
		}

		if (path[0]) {
			strcpy(compilerPath, path);
			strcat(path, ":");
			char *originalPath = getenv("PATH");

			if (strlen(originalPath) < 32768) {
				strcat(path, originalPath);
				setenv("PATH", path, 1);
			} else {
				printf("Warning: PATH too long\n");
			}
		}
	}

	if (!acceptedLicense) {
		printf("\n=== Essence License ===\n\n");
		CallSystem("cat LICENSE.md");
		printf("\nType 'yes' to acknowledge you have read the license, or press Ctrl-C to exit.\n");
		if (!GetYes()) exit(0);
		acceptedLicense = true;
	}

	SaveConfig();

	if (argc >= 2) {
		char buffer[4096];
		buffer[0] = 0;

		for (int i = 1; i < argc; i++) {
			if (strlen(argv[i]) + 1 > sizeof(buffer) - strlen(buffer)) break;
			if (i > 1) strcat(buffer, " ");
			strcat(buffer, argv[i]);
		}

		DoCommand(buffer);
		return 0;
	} else {
		interactiveMode = true;
	}

	const char *runFirstCommand = NULL;

	if (CallSystem("x86_64-essence-gcc --version > /dev/null 2>&1 ")) {
		BuildCrossCompiler();
		runFirstCommand = "b";
		foundValidCrossCompiler = true;
	}

	SaveConfig();

	if (runFirstCommand) {
		DoCommand(runFirstCommand);
	}

	if (!foundValidCrossCompiler) {
		printf("Warning: Your cross compiler appears to be out of date.\n");
		printf("Please rebuild the compiler using the command " ColorHighlight "build-cross" ColorNormal " before attempting to build the OS.\n");
	}

	{
		CallSystem("x86_64-essence-gcc -mno-red-zone -print-libgcc-file-name > bin/valid_compiler.txt");
		FILE *f = fopen("bin/valid_compiler.txt", "r");
		char buffer[256];
		buffer[fread(buffer, 1, 256, f)] = 0;
		fclose(f);

		if (!strstr(buffer, "no-red-zone")) {
			printf("Error: Compiler built without no-red-zone support.\n");
			exit(1);
		}

		unlink("bin/valid_compiler.txt");
	}

	printf("Enter 'help' to get a list of commands.\n");
	char *prev = NULL;

	canBuildLuigi = !CallSystem("gcc -o bin/luigi.h.gch util/luigi.h -D UI_IMPLEMENTATION -D UI_LINUX");

	while (true) {
		char *l = NULL;
		size_t pos = 0;
		printf("\n> ");
		printf(ColorHighlight);
		getline(&l, &pos, stdin);
		printf(ColorNormal);

		if (strlen(l) == 1) {
			l = prev;
			if (!l) {
				l = (char *) malloc(5);
				strcpy(l, "help");
			}
			printf("(%s)\n", l);
		} else {
			l[strlen(l) - 1] = 0;
		}

		printf(ColorNormal);
		fflush(stdout);
		DoCommand(l);

		if (prev != l) free(prev);
		prev = l;
	}

	return 0;
}
