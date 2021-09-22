#include <efi.h>

#define ENTRIES_PER_PAGE_TABLE (512)
#define ENTRIES_PER_PAGE_TABLE_BITS (9)
#define K_PAGE_SIZE (4096)
#define K_PAGE_BITS (12)

typedef struct VideoModeInformation {
	uint8_t valid : 1, edidValid : 1;
	uint8_t bitsPerPixel;
	uint16_t widthPixels, heightPixels;
	uint16_t bytesPerScanlineLinear;
	uint64_t bufferPhysical;
	uint8_t edid[128];
} VideoModeInformation;

typedef struct ElfHeader {
	uint32_t magicNumber; // 0x7F followed by 'ELF'
	uint8_t bits; // 1 = 32 bit, 2 = 64 bit
	uint8_t endianness; // 1 = LE, 2 = BE
	uint8_t version1;
	uint8_t abi; // 0 = System V
	uint8_t _unused0[8];
	uint16_t type; // 1 = relocatable, 2 = executable, 3 = shared
	uint16_t instructionSet; // 0x03 = x86, 0x28 = ARM, 0x3E = x86-64, 0xB7 = AArch64
	uint32_t version2;
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
} ElfHeader;

typedef struct ElfSectionHeader {
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
} ElfSectionHeader;

typedef struct ElfProgramHeader {
	uint32_t type; // 0 = unused, 1 = load, 2 = dynamic, 3 = interp, 4 = note
	uint32_t flags; // 1 = executable, 2 = writable, 4 = readable
	uint64_t fileOffset;
	uint64_t virtualAddress;
	uint64_t _unused0;
	uint64_t dataInFile;
	uint64_t segmentSize;
	uint64_t alignment;
} ElfProgramHeader;

typedef struct __attribute__((packed)) GDTData {
	uint16_t length;
	uint64_t address;
} GDTData;

typedef struct MemoryRegion {
	uintptr_t base, pages;
} MemoryRegion;

#define MAX_MEMORY_REGIONS (1024)
MemoryRegion memoryRegions[1024];
#define KERNEL_BUFFER_SIZE (1048576)
#define kernelBuffer ((char *) 0x200000)
#define IID_BUFFER_SIZE (16)
char iidBuffer[IID_BUFFER_SIZE];
#define MEMORY_MAP_BUFFER_SIZE (16384)
char memoryMapBuffer[MEMORY_MAP_BUFFER_SIZE];

EFI_SYSTEM_TABLE *systemTable;

void ZeroMemory(void *pointer, uint64_t size) {
	char *d = (char *) pointer;

	for (uintptr_t i = 0; i < size; i++) {
		d[i] = 0;
	}
}

void Print(WCHAR *message) {
	uefi_call_wrapper(systemTable->ConOut->OutputString, 2, systemTable->ConOut, message);
}

void Error(WCHAR *message) {
	Print(message);
	while (1);
}

void PrintHex(uint64_t value) {
	const WCHAR *hexChars = L"0123456789ABCDEF";

	for (uintptr_t i = 0; i < 16; i++) {
		WCHAR b[2] = { hexChars[(value >> (60 - i * 4)) & 0xF], 0 };
		Print((WCHAR *) b);
	}

	Print(L", ");
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE imageHandle, EFI_SYSTEM_TABLE *_systemTable) {
	UINTN mapKey = 0;
	systemTable = _systemTable;
	uint32_t *framebuffer, horizontalResolution, verticalResolution, pixelsPerScanline;
	ElfHeader *header;

	// Make sure 0x100000 -> 0x300000 is identity mapped.
	{
		EFI_PHYSICAL_ADDRESS address = 0x100000;

		if (EFI_SUCCESS != uefi_call_wrapper(systemTable->BootServices->AllocatePages, 4, AllocateAddress, EfiLoaderData, 0x200, &address)) {
			Error(L"Error: Could not allocate 1MB->3MB.\n");
		}
	}

	// Find the RSDP.
	{
		uint8_t foundRSDP = 0;

		for (uintptr_t i = 0; i < systemTable->NumberOfTableEntries; i++) {
			EFI_CONFIGURATION_TABLE *entry = systemTable->ConfigurationTable + i;
			if (entry->VendorGuid.Data1 == 0x8868E871 && entry->VendorGuid.Data2 == 0xE4F1 && entry->VendorGuid.Data3 == 0x11D3
					&& entry->VendorGuid.Data4[0] == 0xBC && entry->VendorGuid.Data4[1] == 0x22 && entry->VendorGuid.Data4[2] == 0x00
					&& entry->VendorGuid.Data4[3] == 0x80 && entry->VendorGuid.Data4[4] == 0xC7 && entry->VendorGuid.Data4[5] == 0x3C
					&& entry->VendorGuid.Data4[6] == 0x88 && entry->VendorGuid.Data4[7] == 0x81) {
				*((uint64_t *) 0x107FE8) = (uint64_t) entry->VendorTable;
				foundRSDP = 1;
				break;
			}
		}

		if (!foundRSDP) {
			Error(L"Error: Could not find the RSDP.\n");
		}
	}

	// Read the kernel, IID and loader files.
	{
		EFI_GUID loadedImageProtocolGUID = LOADED_IMAGE_PROTOCOL;
		EFI_GUID simpleFilesystemProtocolGUID = SIMPLE_FILE_SYSTEM_PROTOCOL;

		EFI_LOADED_IMAGE_PROTOCOL *loadedImageProtocol;
		EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *simpleFilesystemProtocol;

		EFI_FILE *filesystemRoot, *kernelFile, *iidFile, *loaderFile;

		UINTN size;

		if (EFI_SUCCESS != uefi_call_wrapper(systemTable->BootServices->OpenProtocol, 6, imageHandle, &loadedImageProtocolGUID, 
					(void **) &loadedImageProtocol, imageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL)) {
			Error(L"Error: Could not open protocol 1.\n");
		}

		EFI_HANDLE deviceHandle = loadedImageProtocol->DeviceHandle; 

		if (EFI_SUCCESS != uefi_call_wrapper(systemTable->BootServices->OpenProtocol, 6, deviceHandle, &simpleFilesystemProtocolGUID, 
					(void **) &simpleFilesystemProtocol, imageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL)) {
			Error(L"Error: Could not open procotol 2.\n");
		}

		if (EFI_SUCCESS != uefi_call_wrapper(simpleFilesystemProtocol->OpenVolume, 2, simpleFilesystemProtocol, &filesystemRoot)) {
			Error(L"Error: Could not open ESP volume.\n");
		}

		if (EFI_SUCCESS != uefi_call_wrapper(filesystemRoot->Open, 5, filesystemRoot, &kernelFile, L"eskernel.esx", EFI_FILE_MODE_READ, 0)) {
			Error(L"Error: Could not open eskernel.esx.\n");
		}

		size = KERNEL_BUFFER_SIZE;

		if (EFI_SUCCESS != uefi_call_wrapper(kernelFile->Read, 3, kernelFile, &size, kernelBuffer)) {
			Error(L"Error: Could not load eskernel.esx.\n");
		}

		// Print(L"Kernel size: %d bytes\n", size);

		if (size == KERNEL_BUFFER_SIZE) {
			Error(L"Error: Kernel too large to fit into buffer.\n");
		}

		if (EFI_SUCCESS != uefi_call_wrapper(filesystemRoot->Open, 5, filesystemRoot, &iidFile, L"esiid.dat", EFI_FILE_MODE_READ, 0)) {
			Error(L"Error: Could not open esiid.dat.\n");
		}

		size = IID_BUFFER_SIZE;

		if (EFI_SUCCESS != uefi_call_wrapper(iidFile->Read, 3, iidFile, &size, iidBuffer)) {
			Error(L"Error: Could not load esiid.dat.\n");
		}

		if (EFI_SUCCESS != uefi_call_wrapper(filesystemRoot->Open, 5, filesystemRoot, &loaderFile, L"esloader.bin", EFI_FILE_MODE_READ, 0)) {
			Error(L"Error: Could not open esloader.bin.\n");
		}

		size = 0x80000;

		if (EFI_SUCCESS != uefi_call_wrapper(loaderFile->Read, 3, loaderFile, &size, (char *) 0x180000)) {
			Error(L"Error: Could not load esloader.bin.\n");
		}
	}

	// Get the graphics mode information.
	// TODO Mode picking.
	// TODO Get EDID information, if available.
	{
		EFI_GRAPHICS_OUTPUT_PROTOCOL *graphicsOutputProtocol;
		EFI_GUID graphicsOutputProtocolGUID = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

		if (EFI_SUCCESS != uefi_call_wrapper(systemTable->BootServices->LocateProtocol, 3, &graphicsOutputProtocolGUID, NULL, (void **) &graphicsOutputProtocol)) {
			Error(L"Error: Could not open protocol 3.\n");
		}

		horizontalResolution = graphicsOutputProtocol->Mode->Info->HorizontalResolution;
		verticalResolution = graphicsOutputProtocol->Mode->Info->VerticalResolution;
		pixelsPerScanline = graphicsOutputProtocol->Mode->Info->PixelsPerScanLine;
		framebuffer = (uint32_t *) graphicsOutputProtocol->Mode->FrameBufferBase;
	}

	// Get the memory map.
	{
		UINTN descriptorSize, size = MEMORY_MAP_BUFFER_SIZE;
		UINT32 descriptorVersion;

		if (EFI_SUCCESS != uefi_call_wrapper(systemTable->BootServices->GetMemoryMap, 5, &size, 
					(EFI_MEMORY_DESCRIPTOR *) memoryMapBuffer, &mapKey, &descriptorSize, &descriptorVersion)) {
			Error(L"Error: Could not get memory map.\n");
		}

		uintptr_t memoryRegionCount = 0;

		for (uintptr_t i = 0; i < size / descriptorSize && memoryRegionCount != MAX_MEMORY_REGIONS - 1; i++) {
			EFI_MEMORY_DESCRIPTOR *descriptor = (EFI_MEMORY_DESCRIPTOR *) (memoryMapBuffer + i * descriptorSize);

			if (descriptor->Type == EfiConventionalMemory && descriptor->PhysicalStart >= 0x300000) {
				memoryRegions[memoryRegionCount].base = descriptor->PhysicalStart;
				memoryRegions[memoryRegionCount].pages = descriptor->NumberOfPages;
				memoryRegionCount++;
			}
		}

		memoryRegions[memoryRegionCount].base = 0;
	}

	// Exit boot services.
	{
		if (EFI_SUCCESS != uefi_call_wrapper(systemTable->BootServices->ExitBootServices, 2, imageHandle, mapKey)) {
			Error(L"Error: Could not exit boot services.\n");
		}
	}

	// Identity map the first 3MB for the loader.
	{
		uint64_t *paging = (uint64_t *) 0x140000;
		ZeroMemory(paging, 0x5000);

		paging[0x1FE] = 0x140003; // Recursive
		paging[0x000] = 0x141003; // L4
		paging[0x200] = 0x142003; // L3
		paging[0x400] = 0x143003; // L2
		paging[0x401] = 0x144003;

		for (uintptr_t i = 0; i < 0x400; i++) {
			paging[0x600 + i] = (i * 0x1000) | 3; // L1
		}
	}

	// Copy the installation ID across.
	{
		uint8_t *destination = (uint8_t *) (0x107FF0);

		for (uintptr_t i = 0; i < 16; i++) {
			destination[i] = iidBuffer[i];
		}
	}

	// Copy the graphics information across.
	{
		VideoModeInformation *destination = (VideoModeInformation *) (0x107000);
		destination->widthPixels = horizontalResolution;
		destination->heightPixels = verticalResolution;
		destination->bufferPhysical = (uint64_t) framebuffer;
		destination->bytesPerScanlineLinear = pixelsPerScanline * 4;
		destination->bitsPerPixel = 32;
		destination->valid = 1;
		destination->edidValid = 0;
	}

	// Allocate and map memory for the kernel.
	{
		uint64_t nextPageTable = 0x1C0000;

		header = (ElfHeader *) kernelBuffer;
		ElfProgramHeader *programHeaders = (ElfProgramHeader *) (kernelBuffer + header->programHeaderTable);
		uintptr_t programHeaderEntrySize = header->programHeaderEntrySize;

		for (uintptr_t i = 0; i < header->programHeaderEntries; i++) {
			ElfProgramHeader *header = (ElfProgramHeader *) ((uint8_t *) programHeaders + programHeaderEntrySize * i);
			if (header->type != 1) continue;

			uintptr_t pagesToAllocate = header->segmentSize >> 12;
			if (header->segmentSize & 0xFFF) pagesToAllocate++;
			uintptr_t physicalAddress = 0;

			for (uintptr_t j = 0; j < MAX_MEMORY_REGIONS; j++) {
				MemoryRegion *region = memoryRegions + j;
				if (!region->base) break;
				if (region->pages < pagesToAllocate) continue;
				physicalAddress = region->base;
				region->pages -= pagesToAllocate;
				region->base += pagesToAllocate << 12;
				break;
			}

			if (!physicalAddress) {
				// TODO Error handling.
				*((uint32_t *) framebuffer + 3) = 0xFFFF00FF;
				while (1);
			}

			for (uintptr_t j = 0; j < pagesToAllocate; j++, physicalAddress += 0x1000) {
				uintptr_t virtualAddress = header->virtualAddress + j * K_PAGE_SIZE;
				physicalAddress &= 0xFFFFFFFFFFFFF000;
				virtualAddress  &= 0x0000FFFFFFFFF000;

				uintptr_t indexL4 = (virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 3)) & (ENTRIES_PER_PAGE_TABLE - 1);
				uintptr_t indexL3 = (virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 2)) & (ENTRIES_PER_PAGE_TABLE - 1);
				uintptr_t indexL2 = (virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 1)) & (ENTRIES_PER_PAGE_TABLE - 1);
				uintptr_t indexL1 = (virtualAddress >> (K_PAGE_BITS + ENTRIES_PER_PAGE_TABLE_BITS * 0)) & (ENTRIES_PER_PAGE_TABLE - 1);

				uint64_t *tableL4 = (uint64_t *) 0x140000;

				if (!(tableL4[indexL4] & 1)) {
					tableL4[indexL4] = nextPageTable | 7;
					ZeroMemory((void *) nextPageTable, K_PAGE_SIZE);
					nextPageTable += K_PAGE_SIZE;
				}

				uint64_t *tableL3 = (uint64_t *) (tableL4[indexL4] & ~(K_PAGE_SIZE - 1));

				if (!(tableL3[indexL3] & 1)) {
					tableL3[indexL3] = nextPageTable | 7;
					ZeroMemory((void *) nextPageTable, K_PAGE_SIZE);
					nextPageTable += K_PAGE_SIZE;
				}

				uint64_t *tableL2 = (uint64_t *) (tableL3[indexL3] & ~(K_PAGE_SIZE - 1));

				if (!(tableL2[indexL2] & 1)) {
					tableL2[indexL2] = nextPageTable | 7;
					ZeroMemory((void *) nextPageTable, K_PAGE_SIZE);
					nextPageTable += K_PAGE_SIZE;
				}

				uint64_t *tableL1 = (uint64_t *) (tableL2[indexL2] & ~(K_PAGE_SIZE - 1));
				uintptr_t value = physicalAddress | 3;
				tableL1[indexL1] = value;
			}
		}
	}

	// Copy the memory regions information across.
	{
		MemoryRegion *destination = (MemoryRegion *) 0x160000;

		for (uintptr_t i = 0; i < MAX_MEMORY_REGIONS; i++) {
			destination[i] = memoryRegions[i];
		}
	}

	// Start the loader.
	{
		((void (*)()) 0x180000)();
	}

	while (1);
	return EFI_SUCCESS;
}
