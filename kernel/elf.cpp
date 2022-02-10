// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

// TODO Use a custom executable format?

#define MEMORY_MAPPED_EXECUTABLES

#ifndef IMPLEMENTATION

struct ElfHeader {
	uint32_t magicNumber; // 0x7F followed by 'ELF'
	uint8_t bits; // 1 = 32 bit, 2 = 64 bit
	uint8_t endianness; // 1 = LE, 2 = BE
	uint8_t version1;
	uint8_t abi; // 0 = System V
	uint8_t _unused0[8];
	uint16_t type; // 1 = relocatable, 2 = executable, 3 = shared
	uint16_t instructionSet; // 0x03 = x86, 0x28 = ARM, 0x3E = x86-64, 0xB7 = AArch64
	uint32_t version2;

#ifdef ES_BITS_32
	uint32_t entry;
	uint32_t programHeaderTable;
	uint32_t sectionHeaderTable;
	uint32_t flags;
	uint16_t headerSize;
	uint16_t programHeaderEntrySize;
	uint16_t programHeaderEntries;
	uint16_t sectionHeaderEntrySize;
	uint16_t sectionHeaderEntries;
	uint16_t sectionNameIndex;
#else
	uint64_t entry;
	uint64_t programHeaderTable;
	uint64_t sectionHeaderTable;
	uint32_t flags;
	uint16_t headerSize;
	uint16_t programHeaderEntrySize;
	uint16_t programHeaderEntries;
	uint16_t sectionHeaderEntrySize;
	uint16_t sectionHeaderEntries;
	uint16_t sectionNameIndex;
#endif
};

#ifdef ES_BITS_32
struct ElfSectionHeader {
	uint32_t name;
	uint32_t type;
	uint32_t flags;
	uint32_t address;
	uint32_t offset;
	uint32_t size;
	uint32_t link;
	uint32_t info;
	uint32_t align;
	uint32_t entrySize;
};

struct ElfProgramHeader {
	uint32_t type; // 0 = unused, 1 = load, 2 = dynamic, 3 = interp, 4 = note
	uint32_t fileOffset;
	uint32_t virtualAddress;
	uint32_t _unused0;
	uint32_t dataInFile;
	uint32_t segmentSize;
	uint32_t flags; // 1 = executable, 2 = writable, 4 = readable
	uint32_t alignment;
};

struct ElfRelocation {
	uint32_t offset;
	uint32_t info;
	int32_t addend;
};

struct ElfSymbol {
	uint32_t name, value, size;
	uint8_t info, _reserved1;
	uint16_t sectionIndex;
};
#else
struct ElfSectionHeader {
	uint32_t name; // Offset into section header->sectionNameIndex.
	uint32_t type; // 4 = rela
	uint64_t flags;
	uint64_t address;
	uint64_t offset;
	uint64_t size;
	uint32_t link;
	uint32_t info;
	uint64_t align;
	uint64_t entrySize;
};

struct ElfProgramHeader {
	uint32_t type; // 0 = unused, 1 = load, 2 = dynamic, 3 = interp, 4 = note
	uint32_t flags; // 1 = executable, 2 = writable, 4 = readable
	uint64_t fileOffset;
	uint64_t virtualAddress;
	uint64_t _unused0;
	uint64_t dataInFile;
	uint64_t segmentSize;
	uint64_t alignment;
};

struct ElfRelocation {
	uint64_t offset;
	uint64_t info;
	int64_t addend;
};

struct ElfSymbol {
	uint32_t name;
	uint8_t info, _reserved1;
	uint16_t sectionIndex;
	uint64_t value, size;
};
#endif

#else

EsError KLoadELF(KNode *node, KLoadedExecutable *executable) {
	Process *thisProcess = GetCurrentThread()->process;

	uintptr_t executableOffset = 0;
	size_t fileSize = FSNodeGetTotalSize(node);

	{
		BundleHeader header;
		size_t bytesRead = FSFileReadSync(node, (uint8_t *) &header, 0, sizeof(BundleHeader), 0);

		if (bytesRead != sizeof(BundleHeader)) {
			KernelLog(LOG_ERROR, "ELF", "executable load error", "Could not read the bundle header.\n");
			return ES_ERROR_UNSUPPORTED;
		}

		if (header.signature == BUNDLE_SIGNATURE 
				&& header.fileCount < 0x100000
				&& header.fileCount * sizeof(BundleFile) + sizeof(BundleHeader) < fileSize) {
			if (!header.mapAddress) {
				if (executable->isDesktop) {
					header.mapAddress = BUNDLE_FILE_DESKTOP_MAP_ADDRESS;
				} else {
					header.mapAddress = BUNDLE_FILE_MAP_ADDRESS;
				}
			}

			if (ArchCheckBundleHeader() || header.mapAddress & (K_PAGE_SIZE - 1)) {
				KernelLog(LOG_ERROR, "ELF", "executable load error", "Invalid bundle mapping addresses.\n");
				return ES_ERROR_UNSUPPORTED;
			}

			if (header.version != 1) {
				KernelLog(LOG_ERROR, "ELF", "executable load error", "Invalid bundle version.\n");
				return ES_ERROR_UNSUPPORTED;
			}

			// Map the bundle file.

			if (!MMMapFile(thisProcess->vmm, (FSFile *) node, 
					0, fileSize, ES_MEMORY_MAP_OBJECT_READ_ONLY, 
					(uint8_t *) header.mapAddress)) {
				KernelLog(LOG_ERROR, "ELF", "executable load error", "Could not map the bundle file.\n");
				return ES_ERROR_INSUFFICIENT_RESOURCES;
			}

			// Look for the executable in the bundle.

			uint64_t name = CalculateCRC64(EsLiteral("$Executables/" K_ARCH_NAME), 0);
			BundleFile *files = (BundleFile *) ((BundleHeader *) header.mapAddress + 1);

			bool found = false;

			for (uintptr_t i = 0; i < header.fileCount; i++) {
				if (files[i].nameCRC64 == name) {
					executableOffset = files[i].offset;
					found = true;
					break;
				}
			}

			if (executableOffset >= fileSize || !found) {
				KernelLog(LOG_ERROR, "ELF", "executable load error", "Could not find the executable section for the current architecture.\n");
				return ES_ERROR_UNSUPPORTED;
			}

			executable->isBundle = true;
		}
	}

	// EsPrint("executableOffset: %x\n", executableOffset);

	ElfHeader header;
	size_t bytesRead = FSFileReadSync(node, (uint8_t *) &header, executableOffset, sizeof(ElfHeader), 0);

	if (bytesRead != sizeof(ElfHeader)) {
		KernelLog(LOG_ERROR, "ELF", "executable load error", "Could not read the ELF header.\n");
		return ES_ERROR_UNSUPPORTED;
	}

	size_t programHeaderEntrySize = header.programHeaderEntrySize;

	if (header.magicNumber != 0x464C457F) {
		KernelLog(LOG_ERROR, "ELF", "executable load error", "Incorrect executable magic number.\n");
		return ES_ERROR_UNSUPPORTED;
	}

	if (header.bits != 2) {
		KernelLog(LOG_ERROR, "ELF", "executable load error", "Incorrect executable bits.\n");
		return ES_ERROR_UNSUPPORTED;
	}

	if (header.endianness != 1) {
		KernelLog(LOG_ERROR, "ELF", "executable load error", "Incorrect executable endianness.\n");
		return ES_ERROR_UNSUPPORTED;
	}

	if (header.abi != 0) {
		KernelLog(LOG_ERROR, "ELF", "executable load error", "Incorrect executable ABI.\n");
		return ES_ERROR_UNSUPPORTED;
	}

	if (header.type != 2) {
		KernelLog(LOG_ERROR, "ELF", "executable load error", "Incorrect executable type.\n");
		return ES_ERROR_UNSUPPORTED;
	}

	if (header.instructionSet != 0x3E) {
		KernelLog(LOG_ERROR, "ELF", "executable load error", "Incorrect executable instruction set.\n");
		return ES_ERROR_UNSUPPORTED;
	}

	ElfProgramHeader *programHeaders = (ElfProgramHeader *) EsHeapAllocate(programHeaderEntrySize * header.programHeaderEntries, false, K_PAGED);

	if (!programHeaders) {
		KernelLog(LOG_ERROR, "ELF", "executable load error", "Could not allocate the program headers.\n");
		return ES_ERROR_INSUFFICIENT_RESOURCES;
	}

	EsDefer(EsHeapFree(programHeaders, 0, K_PAGED));

	bytesRead = FSFileReadSync(node, (uint8_t *) programHeaders, executableOffset + header.programHeaderTable, programHeaderEntrySize * header.programHeaderEntries, 0);

	if (bytesRead != programHeaderEntrySize * header.programHeaderEntries) {
		KernelLog(LOG_ERROR, "ELF", "executable load error", "Could not read the program headers.\n");
		return ES_ERROR_UNSUPPORTED;
	}

	for (uintptr_t i = 0; i < header.programHeaderEntries; i++) {
		ElfProgramHeader *header = (ElfProgramHeader *) ((uint8_t *) programHeaders + programHeaderEntrySize * i);

		if (header->type == 1 /* PT_LOAD */) {
			if (ArchCheckELFHeader()) {
				KernelLog(LOG_ERROR, "ELF", "executable load error", "Rejected ELF program header.\n");
				return ES_ERROR_UNSUPPORTED;
			}

#if 0
			EsPrint("FileOffset %x    VirtualAddress %x    SegmentSize %x    DataInFile %x\n",
					header->fileOffset, header->virtualAddress, header->segmentSize, header->dataInFile);
#endif

			void *success;

#ifndef MEMORY_MAPPED_EXECUTABLES
			success = MMStandardAllocate(thisProcess->vmm, RoundUp(header->segmentSize, K_PAGE_SIZE), ES_FLAGS_DEFAULT, 
					(uint8_t *) RoundDown(header->virtualAddress, K_PAGE_SIZE));

			if (success) {
				bytesRead = FSFileReadSync(node, (void *) header->virtualAddress, executableOffset + header->fileOffset, header->dataInFile, 0);
				if (bytesRead != header->dataInFile) return ES_ERROR_UNSUPPORTED;
			}
#else
			uintptr_t fileStart = RoundDown(header->virtualAddress, K_PAGE_SIZE);
			uintptr_t fileOffset = RoundDown(header->fileOffset, K_PAGE_SIZE);
			uintptr_t zeroStart = RoundUp(header->virtualAddress + header->dataInFile, K_PAGE_SIZE);
			uintptr_t end = RoundUp(header->virtualAddress + header->segmentSize, K_PAGE_SIZE);

			// TODO This doesn't need to be all COPY_ON_WRITE.

			// EsPrint("MMMapFile - %x, %x, %x, %x\n", fileStart, fileOffset, zeroStart, end);

			success = MMMapFile(thisProcess->vmm, (FSFile *) node, 
					executableOffset + fileOffset, zeroStart - fileStart, 
					ES_MEMORY_MAP_OBJECT_COPY_ON_WRITE, 
					(uint8_t *) fileStart, end - zeroStart);

			if (success) {
				uint8_t *from = (uint8_t *) header->virtualAddress + header->dataInFile;
				EsMemoryZero(from, (uint8_t *) zeroStart - from);
			} else {
				KernelLog(LOG_ERROR, "ELF", "executable load error", "Could not memory map program header %d.\n", i);
			}
#endif

			if (!success) return ES_ERROR_INSUFFICIENT_RESOURCES;
		} else if (header->type == 7 /* PT_TLS */) {
			executable->tlsImageStart = header->virtualAddress;
			executable->tlsImageBytes = header->dataInFile;
			executable->tlsBytes = header->segmentSize;
			KernelLog(LOG_INFO, "ELF", "executable TLS", "Executable requests %d bytes of TLS, with %d bytes from the image at %x.\n",
					executable->tlsBytes, executable->tlsImageBytes, executable->tlsImageStart);
		}
	}

	executable->startAddress = header.entry;
	return ES_SUCCESS;
}

uintptr_t KFindSymbol(KModule *module, const char *name, size_t nameBytes) {
	uint8_t *buffer = module->buffer;
	ElfHeader *header = (ElfHeader *) buffer;

	for (uintptr_t i = 0; i < header->sectionHeaderEntries; i++) {
		ElfSectionHeader *section = (ElfSectionHeader *) (buffer + header->sectionHeaderTable + header->sectionHeaderEntrySize * i);
		if (section->type != 2 /* SHT_SYMTAB */) continue;

		ElfSectionHeader *strings = (ElfSectionHeader *) (buffer + header->sectionHeaderTable + header->sectionHeaderEntrySize * section->link);

		for (uintptr_t i = 0; i < section->size / sizeof(ElfSymbol); i++) {
			ElfSymbol *symbol = (ElfSymbol *) (buffer + section->offset + i * sizeof(ElfSymbol));
			if (!symbol->name) continue;
			if (symbol->sectionIndex == 0 /* SHN_UNDEF */ || (symbol->info >> 4) != 1 /* STB_GLOBAL */) continue;

			uint8_t *symbolName = buffer + symbol->name + strings->offset;

			if (0 == EsStringCompareRaw(name, nameBytes, (const char *) symbolName, -1)) {
				return symbol->value;
			}
		}
	}

	return 0;
}

uint8_t *modulesLocation = (uint8_t *) MM_MODULES_START;
KMutex modulesMutex;

uint8_t *AllocateForModule(size_t size) {
	KMutexAssertLocked(&modulesMutex);
	if ((uintptr_t) modulesLocation + RoundUp(size, K_PAGE_SIZE) > MM_MODULES_START + MM_MODULES_SIZE) return nullptr;
	uint8_t *buffer = (uint8_t *) MMStandardAllocate(kernelMMSpace, size, MM_REGION_FIXED, modulesLocation);
	if (!buffer) return nullptr;
	modulesLocation += RoundUp(size, K_PAGE_SIZE);
	return (uint8_t *) buffer;
}

EsError KLoadELFModule(KModule *module) {
#ifdef ES_ARCH_X86_64
	KMutexAcquire(&modulesMutex);
	EsDefer(KMutexRelease(&modulesMutex));

	uint64_t fileFlags = ES_FILE_READ | ES_NODE_FAIL_IF_NOT_FOUND;
	KNodeInformation node = FSNodeOpen(module->path, module->pathBytes, fileFlags);
	if (node.error != ES_SUCCESS) return node.error;

	uint8_t *buffer = AllocateForModule(node.node->directoryEntry->totalSize);
	module->buffer = buffer;

	{
		// TODO Free module buffer on error.

		EsDefer(CloseHandleToObject(node.node, KERNEL_OBJECT_NODE, fileFlags));

		if (!buffer) {
			return ES_ERROR_INSUFFICIENT_RESOURCES;
		}

		if (node.node->directoryEntry->totalSize != (size_t) FSFileReadSync(node.node, buffer, 0, node.node->directoryEntry->totalSize, 0)) {
			return ES_ERROR_UNKNOWN;
		}
	}

	ElfHeader *header = (ElfHeader *) buffer;
	uint8_t *sectionStringTable = buffer + ((ElfSectionHeader *) (buffer + header->sectionHeaderTable + header->sectionHeaderEntrySize * header->sectionNameIndex))->offset;
	(void) sectionStringTable;

	for (uintptr_t i = 0; i < header->sectionHeaderEntries; i++) {
		ElfSectionHeader *section = (ElfSectionHeader *) (buffer + header->sectionHeaderTable + header->sectionHeaderEntrySize * i);
		if (section->type != 8 /* SHT_NOBITS */ || !section->size) continue;
		uint8_t *memory = AllocateForModule(section->size);
		if (!memory) return ES_ERROR_INSUFFICIENT_RESOURCES; // TODO Free allocations.
		section->offset = memory - buffer;
	}

	bool unresolvedSymbols = false;

	for (uintptr_t i = 0; i < header->sectionHeaderEntries; i++) {
		ElfSectionHeader *section = (ElfSectionHeader *) (buffer + header->sectionHeaderTable + header->sectionHeaderEntrySize * i);
		if (section->type != 2 /* SHT_SYMTAB */) continue;

		// EsPrint("%d: '%z' - symbol table\n", i, sectionStringTable + section->name);
		ElfSectionHeader *strings = (ElfSectionHeader *) (buffer + header->sectionHeaderTable + header->sectionHeaderEntrySize * section->link);

		for (uintptr_t i = 0; i < section->size / sizeof(ElfSymbol); i++) {
			ElfSymbol *symbol = (ElfSymbol *) (buffer + section->offset + i * sizeof(ElfSymbol));

			uint8_t *name = buffer + symbol->name + strings->offset;

			if (symbol->sectionIndex == 0 /* SHN_UNDEF */) {
				if (!symbol->name) continue;

				// TODO Check that EsCStringLength stays within bounds.
				void *address = module->resolveSymbol((const char *) name, EsCStringLength((const char *) name));

				if (!address) {
					unresolvedSymbols = true;
				} else {
					symbol->value = (uintptr_t) address;
				}
			} else if (symbol->sectionIndex < header->sectionHeaderEntries) {
				ElfSectionHeader *section = (ElfSectionHeader *) (buffer + header->sectionHeaderTable + header->sectionHeaderEntrySize * symbol->sectionIndex);
				symbol->value += (uintptr_t) buffer + section->offset;
			}

			// EsPrint("'%z' -> %x\n", name, symbol->value);
		}
	}

	if (unresolvedSymbols) {
		return ES_ERROR_COULD_NOT_RESOLVE_SYMBOL;
	}

	for (uintptr_t i = 0; i < header->sectionHeaderEntries; i++) {
		ElfSectionHeader *section = (ElfSectionHeader *) (buffer + header->sectionHeaderTable + header->sectionHeaderEntrySize * i);
		if (section->type != 4 /* SHT_RELA */) continue;

		ElfSectionHeader *target = (ElfSectionHeader *) (buffer + header->sectionHeaderTable + header->sectionHeaderEntrySize * section->info);
		ElfSectionHeader *symbols = (ElfSectionHeader *) (buffer + header->sectionHeaderTable + header->sectionHeaderEntrySize * section->link);
		ElfSectionHeader *strings = (ElfSectionHeader *) (buffer + header->sectionHeaderTable + header->sectionHeaderEntrySize * symbols->link);
		(void) strings;

		// EsPrint("%d: '%z' - relocation table (for %x)\n", i, sectionStringTable + section->name, target->offset);

		for (uintptr_t i = 0; i < section->size / sizeof(ElfRelocation); i++) {
			ElfRelocation *relocation = (ElfRelocation *) (buffer + section->offset + i * sizeof(ElfRelocation));
			ElfSymbol *symbol = (ElfSymbol *) (buffer + symbols->offset + (relocation->info >> 32) * sizeof(ElfSymbol));
			uintptr_t offset = relocation->offset + target->offset, type = relocation->info & 0xFF;
			// EsPrint("\t%d: %z (%x), %d, %x, %x\n", i, buffer + symbol->name + strings->offset, symbol->value, type, offset, relocation->addend);

			uintptr_t result = symbol->value + relocation->addend;
			EsError error = ArchApplyRelocation(type, buffer, offset, result);
			if (error != ES_SUCCESS) return error;
		}
	}

	KGetKernelVersionCallback getVersion = (KGetKernelVersionCallback) KFindSymbol(module, EsLiteral("GetKernelVersion"));

	if (!getVersion || getVersion() != KERNEL_VERSION) {
		KernelLog(LOG_ERROR, "Modules", "invalid module kernel version", 
				"KLoadELFModule - Attempted to load module '%s' for invalid kernel version.\n", module->pathBytes, module->path);
		return ES_ERROR_UNSUPPORTED;
	}

	return ES_SUCCESS;
#else
	(void) module;
	return ES_ERROR_UNSUPPORTED;
#endif
}

#endif
