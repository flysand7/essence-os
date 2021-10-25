// TODO Prevent Meltdown/Spectre exploits.
// TODO Kernel debugger.
// TODO Passing data to userspace - zeroing padding bits of structures.
// TODO Restoring all registers after system call.
// TODO Remove file extensions?
// TODO Thread-local variables for native applications (already working under the POSIX subsystem).

#include "kernel.h"
#define IMPLEMENTATION
#include "kernel.h"

extern "C" void KernelMain() {										
	kernelProcess = scheduler.SpawnProcess(PROCESS_KERNEL);		// Spawn the kernel process.
	ArchInitialise(); 						// Start processors and initialise CPULocalStorage. 
	scheduler.started = true;					// Start the pre-emptive scheduler.
	// Continues in KernelInitialise.
}

void KernelInitialise() {						
	desktopProcess = scheduler.SpawnProcess(PROCESS_DESKTOP);	// Spawn the desktop process.
	DriversInitialise();						// Load the root device.
	desktopProcess->Start(EsLiteral(K_DESKTOP_EXECUTABLE));		// Start the desktop process.
}

void KernelShutdown(uintptr_t action) {
	scheduler.Shutdown();						// Kill user processes.
	FSShutdown();							// Flush file cache and unmount filesystems.
	DriversShutdown();						// Inform drivers of shutdown.
	ArchShutdown(action);						// Power off or restart the computer. 
}
