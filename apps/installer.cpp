#define INSTALLER

#define ES_CRT_WITHOUT_PREFIX
#include <essence.h>

#include <shared/hash.cpp>
#include <ports/lzma/LzmaDec.c>

#include <shared/array.cpp>
#define IMPLEMENTATION
#include <shared/array.cpp>
#undef IMPLEMENTATION

#define Log(...)
#define exit(x) EsThreadTerminate(ES_CURRENT_THREAD)
#include <shared/esfs2.h>

Array<EsMessageDevice> connectedDrives;

/////////////////////////////////////////////

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

EsError Extract(const char *pathIn, size_t pathInBytes, const char *pathOut, size_t pathOutBytes) {
	Extractor *e = (Extractor *) EsHeapAllocate(sizeof(Extractor), true);
	if (!e) return ES_ERROR_INSUFFICIENT_RESOURCES;
	EsDefer(EsHeapFree(e));

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

void ReadBlock(uint64_t, uint64_t, void *) {
	// TODO.
}

void WriteBlock(uint64_t, uint64_t, void *) {
	// TODO.
}

void WriteBytes(uint64_t, uint64_t, void *) {
	// TODO.
}

void ConnectedDriveAdd(EsMessageDevice device) {
	if (device.type != ES_DEVICE_BLOCK) {
		return;
	}

	EsBlockDeviceInformation information;
	EsDeviceControl(device.handle, ES_DEVICE_CONTROL_BLOCK_GET_INFORMATION, 0, &information);

	if (information.nestLevel) {
		return;
	}

	connectedDrives.Add(device);
}

void ConnectedDriveRemove(EsMessageDevice device) {
	for (uintptr_t i = 0; i < connectedDrives.Length(); i++) {
		if (connectedDrives[i].id == device.id) {
			connectedDrives.Delete(i);
			return;
		}
	}
}

void _start() {
	_init();

	EsDeviceEnumerate([] (EsMessageDevice device, EsGeneric) {
		ConnectedDriveAdd(device);
	}, 0);

	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_DEVICE_CONNECTED) {
			ConnectedDriveAdd(message->device);
		} else if (message->type == ES_MSG_DEVICE_DISCONNECTED) {
			ConnectedDriveRemove(message->device);
		}
	}
}
