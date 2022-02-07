// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

// TODO Prevent Meltdown/Spectre exploits.
// TODO Kernel debugger.
// TODO Passing data to userspace - zeroing padding bits of structures.
// TODO Restoring all registers after system call.
// TODO Remove file extensions?
// TODO Thread-local variables for native applications (already working under the POSIX subsystem).

#include "kernel.h"
#define IMPLEMENTATION
#include "kernel.h"

void KernelInitialise() {										
	kernelProcess = ProcessSpawn(PROCESS_KERNEL);			// Spawn the kernel process.
	MMInitialise();							// Initialise the memory manager.
	KThreadCreate("KernelMain", KernelMain);			// Create the KernelMain thread.
	ArchInitialise(); 						// Start processors and initialise CPULocalStorage. 
	scheduler.started = true;					// Start the pre-emptive scheduler.
}

void KernelMain(uintptr_t) {
	EsPrint("Test");
	desktopProcess = ProcessSpawn(PROCESS_DESKTOP);			// Spawn the desktop process.
	DriversInitialise();						// Load the root device.
	ProcessStart(desktopProcess, EsLiteral(K_DESKTOP_EXECUTABLE));	// Start the desktop process.
	KEventWait(&shutdownEvent, ES_WAIT_NO_TIMEOUT);			// Wait for a shutdown request.
	ProcessTerminateAll();						// Terminate all user processes.
	FSShutdown();							// Flush file cache and unmount filesystems.
	DriversShutdown();						// Inform drivers of shutdown.
	ArchShutdown();							// Power off or restart the computer. 
}
