// TODO Adjust time stamps for thread preemption.

#include <stdint.h>
#include <stddef.h>

struct ProfilingEntry {
	void *thisFunction;
	uint64_t timeStamp;
};

extern ptrdiff_t tlsStorageOffset;
extern "C" uintptr_t ProcessorTLSRead(uintptr_t offset);
extern "C" uint64_t ProcessorReadTimeStamp();
void EnterDebugger();

extern size_t profilingBufferSize;
extern uintptr_t profilingBufferPosition;
void ProfilingSetup(ProfilingEntry *buffer, size_t size /* number of entries */);

#ifdef PROFILING_IMPLEMENTATION

ProfilingEntry *profilingBuffer;
size_t profilingBufferSize;
uintptr_t profilingBufferPosition;
uintptr_t profilingThread;

#define PROFILING_FUNCTION(_exiting) \
	(void) callSite; \
	\
	if (profilingBufferPosition < profilingBufferSize && profilingThread == ProcessorTLSRead(tlsStorageOffset)) { \
		ProfilingEntry *entry = (ProfilingEntry *) &profilingBuffer[profilingBufferPosition++]; \
		entry->thisFunction = thisFunction; \
		entry->timeStamp = ProcessorReadTimeStamp() | ((uint64_t) _exiting << 63); \
	} else if (profilingBufferSize && profilingThread == ProcessorTLSRead(tlsStorageOffset)) { \
		profilingBufferSize = 0; \
		EnterDebugger(); \
	}

extern "C" void __cyg_profile_func_enter(void *thisFunction, void *callSite) {
	PROFILING_FUNCTION(0);
}

extern "C" void __cyg_profile_func_exit(void *thisFunction, void *callSite) {
	PROFILING_FUNCTION(1);
}

void ProfilingSetup(ProfilingEntry *buffer, size_t size) {
	profilingThread = ProcessorTLSRead(tlsStorageOffset);
	__sync_synchronize();
	profilingBuffer = buffer;
	profilingBufferSize = size;
	profilingBufferPosition = 0;
}

#endif
