// TODO Include external events on the flame graph, such as context switches.

struct ProfilingEntry {
	void *thisFunction;
	uint64_t timeStamp;
};

ProfilingEntry *gfProfilingBuffer;
size_t gfProfilingBufferSize;
uintptr_t gfProfilingBufferPosition;
volatile ThreadLocalStorage *gfProfilingThread;
uint64_t gfProfilingTicksPerMs;

#define GF_PROFILING_FUNCTION(_exiting) \
	(void) callSite; \
	\
	if (gfProfilingBufferPosition < gfProfilingBufferSize \
			&& gfProfilingThread == (ThreadLocalStorage *) ProcessorTLSRead(tlsStorageOffset)) { \
		ProfilingEntry *entry = (ProfilingEntry *) &gfProfilingBuffer[gfProfilingBufferPosition++]; \
		entry->thisFunction = thisFunction; \
		entry->timeStamp = (ProcessorReadTimeStamp() - gfProfilingThread->timerAdjustTicks) | ((uint64_t) _exiting << 63); \
	}

extern "C" __attribute__((no_instrument_function))
void __cyg_profile_func_enter(void *thisFunction, void *callSite) {
	GF_PROFILING_FUNCTION(0);
}

extern "C" __attribute__((no_instrument_function))
void __cyg_profile_func_exit(void *thisFunction, void *callSite) {
	GF_PROFILING_FUNCTION(1);
}

__attribute__((no_instrument_function))
void GfProfilingInitialise(ProfilingEntry *buffer, size_t size, uint64_t ticksPerMs) {
	gfProfilingTicksPerMs = ticksPerMs;
	gfProfilingBuffer = buffer;
	gfProfilingBufferSize = size;
}

__attribute__((no_instrument_function))
void GfProfilingStart() {
	gfProfilingThread = GetThreadLocalStorage();
	gfProfilingBufferPosition = 0;
}

__attribute__((no_instrument_function))
void GfProfilingStop() {
	gfProfilingThread = nullptr;
	__sync_synchronize();
}
