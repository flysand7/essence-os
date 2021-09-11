#include <essence.h>
#include <ports/lzma/LzmaDec.c>
#include <shared/hash.cpp>

#define BUFFER_SIZE (1048576)
#define NAME_MAX (4096)

struct Extractor {
	EsFileInformation fileIn;
	CLzmaDec state;
	uint8_t inBuffer[BUFFER_SIZE], outBuffer[BUFFER_SIZE], copyBuffer[BUFFER_SIZE], pathBuffer[NAME_MAX];
	size_t inFileOffset, inBytes, inPosition;
	uintptr_t positionInBlock, blockSize;
};

void *DecompressAllocate(ISzAllocPtr, size_t size) { return EsHeapAllocate(size, false); }
void DecompressFree(ISzAllocPtr, void *address) { EsHeapFree(address); }
const ISzAlloc decompressAllocator = { DecompressAllocate, DecompressFree };

ptrdiff_t DecompressBlock(Extractor *e) {
	if (e->inBytes == e->inPosition) {
		e->inBytes = EsFileReadSync(e->fileIn.handle, e->inFileOffset, BUFFER_SIZE, e->inBuffer);
		if (!e->inBytes) return -1;
		e->inPosition = 0;
		e->inFileOffset += e->inBytes;
	}

	size_t inProcessed = e->inBytes - e->inPosition;
	size_t outProcessed = BUFFER_SIZE;
	ELzmaStatus status;
	LzmaDec_DecodeToBuf(&e->state, e->outBuffer, &outProcessed, e->inBuffer + e->inPosition, &inProcessed, LZMA_FINISH_ANY, &status);
	e->inPosition += inProcessed;
	return outProcessed;
}

bool Decompress(Extractor *e, void *_buffer, size_t bytes) {
	uint8_t *buffer = (uint8_t *) _buffer;

	while (bytes) {
		if (e->positionInBlock == e->blockSize) {
			ptrdiff_t processed = DecompressBlock(e);
			if (processed == -1) return false;
			e->blockSize = processed;
			e->positionInBlock = 0;
		}

		size_t copyBytes = bytes > e->blockSize - e->positionInBlock ? e->blockSize - e->positionInBlock : bytes;
		EsMemoryCopy(buffer, e->outBuffer + e->positionInBlock, copyBytes);
		e->positionInBlock += copyBytes, buffer += copyBytes, bytes -= copyBytes;
	}

	return true;
}

EsError Extract(Extractor *e, const char *pathIn, size_t pathInBytes, const char *pathOut, size_t pathOutBytes) {
	EsMemoryZero(e, sizeof(Extractor));

	e->fileIn = EsFileOpen(pathIn, pathInBytes, ES_FILE_READ);
	if (e->fileIn.error != ES_SUCCESS) return e->fileIn.error;

	uint8_t header[LZMA_PROPS_SIZE + 8];
	EsFileReadSync(e->fileIn.handle, 0, sizeof(header), header);

	LzmaDec_Construct(&e->state);
	LzmaDec_Allocate(&e->state, header, LZMA_PROPS_SIZE, &decompressAllocator);
	LzmaDec_Init(&e->state);

	e->inFileOffset = sizeof(header);

	uint64_t crc64 = 0, actualCRC64 = 0;

	EsMemoryCopy(e->pathBuffer, pathOut, pathOutBytes);

	while (true) {
		uint64_t fileSize;
		if (!Decompress(e, &fileSize, sizeof(fileSize))) break;
		actualCRC64 = fileSize;
		uint16_t nameBytes;
		if (!Decompress(e, &nameBytes, sizeof(nameBytes))) break;
		if (nameBytes > NAME_MAX - pathOutBytes) break;
		if (!Decompress(e, e->pathBuffer + pathOutBytes, nameBytes)) break;

		EsFileInformation fileOut = EsFileOpen((const char *) e->pathBuffer, pathOutBytes + nameBytes, 
				ES_FILE_WRITE | ES_NODE_CREATE_DIRECTORIES | ES_NODE_FAIL_IF_FOUND);
		EsFileOffset fileOutPosition = 0;

		if (fileOut.error != ES_SUCCESS) {
			LzmaDec_Free(&e->state, &decompressAllocator);
			EsHandleClose(e->fileIn.handle);
			return fileOut.error;
		}

		while (fileOutPosition < fileSize) {
			size_t copyBytes = (fileSize - fileOutPosition) > BUFFER_SIZE ? BUFFER_SIZE : (fileSize - fileOutPosition);
			Decompress(e, e->copyBuffer, copyBytes);
			EsFileWriteSync(fileOut.handle, fileOutPosition, copyBytes, e->copyBuffer);
			fileOutPosition += copyBytes;
			crc64 = CalculateCRC64(e->copyBuffer, copyBytes, crc64);
		}

		EsHandleClose(fileOut.handle);
	}

	LzmaDec_Free(&e->state, &decompressAllocator);
	EsHandleClose(e->fileIn.handle);

	return crc64 == actualCRC64 ? ES_SUCCESS : ES_ERROR_CORRUPT_DATA;
}

void _start() {
	_init();
	EsPerformanceTimerPush();
	Extractor *e = (Extractor *) EsHeapAllocate(sizeof(Extractor), false);
	EsAssert(ES_SUCCESS == Extract(e, EsLiteral("0:/installer_archive.dat"), EsLiteral("0:/extracted")));
	EsHeapFree(e);
	EsPrint("time: %Fs\n", EsPerformanceTimerPop());
}
