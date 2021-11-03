#include <stdint.h>
#include <stddef.h>

extern "C" int EsStringCompareRaw(const char *s1, size_t b1, const char *s2, size_t b2);
void EsPrint(const char *format, ...);

extern "C" void KernelInitialise();
extern "C" void ProcessorHalt();

struct ExportedKernelFunction {
	void *address;
	const char *name;
};

ExportedKernelFunction exportedKernelFunctions[] = {
#include <bin/kernel_symbols.h>
};

static bool linked;

void *ResolveKernelSymbol(const char *name, size_t nameBytes) {
	// EsPrint("Resolve: '%s'.\n", nameBytes, name);

	if (!linked) {
		linked = true;

		// As we get the function addresses before the kernel is linked (this file needs to be linked with the kernel),
		// they are relative to wherever the kernel_all.o's text is placed in the executable's text section.

		uintptr_t offset = (uintptr_t) ResolveKernelSymbol("KernelInitialise", 10) - (uintptr_t) KernelInitialise;

		for (uintptr_t i = 0; i < sizeof(exportedKernelFunctions) / sizeof(exportedKernelFunctions[0]); i++) {
			exportedKernelFunctions[i].address = (void *) ((uintptr_t) exportedKernelFunctions[i].address - offset);
		}
	}

	for (uintptr_t i = 0; i < sizeof(exportedKernelFunctions) / sizeof(exportedKernelFunctions[0]); i++) {
		if (0 == EsStringCompareRaw(exportedKernelFunctions[i].name, -1, name, nameBytes)) {
			return exportedKernelFunctions[i].address;
		}
	}

	EsPrint("ResolveKernelSymbol - Could not find symbol '%s'.\n", nameBytes, name);

	return nullptr;
}
