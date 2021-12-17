// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

#include <stdint.h>
#include <stddef.h>

extern "C" void KernelInitialise();
extern "C" int EsStringCompareRaw(const char *s1, size_t b1, const char *s2, size_t b2);
void EsPrint(const char *format, ...);

struct ExportedKernelFunction {
	void *address;
	const char *name;
};

const ExportedKernelFunction exportedKernelFunctions[] = {
#include <bin/generated_code/kernel_symbols.h>
};

static uintptr_t linkOffset;

void *_ResolveKernelSymbol(const char *name, size_t nameBytes) {
	for (uintptr_t i = 0; i < sizeof(exportedKernelFunctions) / sizeof(exportedKernelFunctions[0]); i++) {
		if (0 == EsStringCompareRaw(exportedKernelFunctions[i].name, -1, name, nameBytes)) {
			return (void *) ((uintptr_t) exportedKernelFunctions[i].address - linkOffset);
		}
	}

	EsPrint("ResolveKernelSymbol - Could not find symbol '%s'.\n", nameBytes, name);
	return nullptr;
}

void *ResolveKernelSymbol(const char *name, size_t nameBytes) {
	if (!linkOffset) {
		// As we get the function addresses before the kernel is linked (this file needs to be linked with the kernel),
		// they are relative to wherever the kernel_all.o's text is placed in the executable's text section.
		linkOffset = (uintptr_t) _ResolveKernelSymbol("KernelInitialise", 10) - (uintptr_t) KernelInitialise;
	}

	return _ResolveKernelSymbol(name, nameBytes);
}
