#include <module.h>

// TODO Inserting/removing CDs.

#define GENERAL_TIMEOUT (5000)

#define COMMAND_LIST_SIZE  (0x400)
#define RECEIVED_FIS_SIZE  (0x100)
#define PRDT_ENTRY_COUNT   (0x48) // If one page each, this covers more than CC_ACTIVE_SECTION_SIZE. This must be a multiple of 8.
#define COMMAND_TABLE_SIZE (0x80 + PRDT_ENTRY_COUNT * 0x10) 

// Global registers.
#define RD_REGISTER_CAP()         pci->ReadBAR32(5, 0x00)                   // HBA capababilities.
#define RD_REGISTER_GHC()         pci->ReadBAR32(5, 0x04)                   // Global host control.
#define WR_REGISTER_GHC(x)        pci->WriteBAR32(5, 0x04, x)
#define RD_REGISTER_IS()          pci->ReadBAR32(5, 0x08)                   // Interrupt status.
#define WR_REGISTER_IS(x)         pci->WriteBAR32(5, 0x08, x)
#define RD_REGISTER_PI()          pci->ReadBAR32(5, 0x0C)                   // Ports implemented.
#define RD_REGISTER_CAP2()        pci->ReadBAR32(5, 0x24)                   // HBA capababilities extended.
#define RD_REGISTER_BOHC()        pci->ReadBAR32(5, 0x28)                   // BIOS/OS handoff control and status.
#define WR_REGISTER_BOHC(x)       pci->WriteBAR32(5, 0x28, x)

// Port-specific registers.
#define RD_REGISTER_PCLB(p)       pci->ReadBAR32(5, 0x100 + (p) * 0x80)     // Command list base address (low DWORD).
#define WR_REGISTER_PCLB(p, x)    pci->WriteBAR32(5, 0x100 + (p) * 0x80, x)
#define RD_REGISTER_PCLBU(p)      pci->ReadBAR32(5, 0x104 + (p) * 0x80)     // Command list base address (high DWORD).
#define WR_REGISTER_PCLBU(p, x)   pci->WriteBAR32(5, 0x104 + (p) * 0x80, x)
#define RD_REGISTER_PFB(p)        pci->ReadBAR32(5, 0x108 + (p) * 0x80)     // FIS base address (low DWORD).
#define WR_REGISTER_PFB(p, x)     pci->WriteBAR32(5, 0x108 + (p) * 0x80, x)
#define RD_REGISTER_PFBU(p)       pci->ReadBAR32(5, 0x10C + (p) * 0x80)     // FIS base address (high DWORD).
#define WR_REGISTER_PFBU(p, x)    pci->WriteBAR32(5, 0x10C + (p) * 0x80, x)
#define RD_REGISTER_PIS(p)        pci->ReadBAR32(5, 0x110 + (p) * 0x80)     // Interrupt status.
#define WR_REGISTER_PIS(p, x)     pci->WriteBAR32(5, 0x110 + (p) * 0x80, x)
#define RD_REGISTER_PIE(p)        pci->ReadBAR32(5, 0x114 + (p) * 0x80)     // Interrupt enable.
#define WR_REGISTER_PIE(p, x)     pci->WriteBAR32(5, 0x114 + (p) * 0x80, x) 
#define RD_REGISTER_PCMD(p)       pci->ReadBAR32(5, 0x118 + (p) * 0x80)     // Command and status.
#define WR_REGISTER_PCMD(p, x)    pci->WriteBAR32(5, 0x118 + (p) * 0x80, x) 
#define RD_REGISTER_PTFD(p)       pci->ReadBAR32(5, 0x120 + (p) * 0x80)     // Task file data.
#define RD_REGISTER_PSIG(p)       pci->ReadBAR32(5, 0x124 + (p) * 0x80)     // Signature.
#define RD_REGISTER_PSSTS(p)      pci->ReadBAR32(5, 0x128 + (p) * 0x80)     // SATA status.
#define RD_REGISTER_PSCTL(p)      pci->ReadBAR32(5, 0x12C + (p) * 0x80)     // SATA control.
#define WR_REGISTER_PSCTL(p, x)   pci->WriteBAR32(5, 0x12C + (p) * 0x80, x)
#define RD_REGISTER_PSERR(p)      pci->ReadBAR32(5, 0x130 + (p) * 0x80)     // SATA error.
#define WR_REGISTER_PSERR(p, x)   pci->WriteBAR32(5, 0x130 + (p) * 0x80, x) 
#define RD_REGISTER_PCI(p)        pci->ReadBAR32(5, 0x138 + (p) * 0x80)     // Command issue.
#define WR_REGISTER_PCI(p, x)     pci->WriteBAR32(5, 0x138 + (p) * 0x80, x) 

struct AHCIPort {
	bool connected, atapi, ssd;

	uint32_t *commandList;
	uint8_t *commandTables;

	size_t sectorBytes;
	uint64_t sectorCount;

	KWorkGroup *commandContexts[32]; // Set to indicate command in use.
	uint64_t commandStartTimeStamps[32];
	uint32_t runningCommands;

	KSpinlock commandSpinlock;
	KEvent commandSlotsAvailable;

	char model[41];
};

struct AHCIController : KDevice {
	KPCIDevice *pci;

	uint32_t capabilities, capabilities2;
	bool dma64Supported;
	size_t commandSlotCount;

	KTimer timeoutTimer;

#define MAX_PORTS (32)
	AHCIPort ports[MAX_PORTS];

	void Initialise();
	bool Access(uintptr_t port, uint64_t offsetBytes, size_t countBytes, int operation, 
			KDMABuffer *buffer, uint64_t flags, KWorkGroup *dispatchGroup);
	bool HandleIRQ();
	bool SendSingleCommand(uintptr_t port);
	void DumpState();
};

struct AHCIDrive : KBlockDevice {
	AHCIController *controller;
	uintptr_t port;
};

struct InterruptEvent {
	uint64_t timeStamp;
	uint32_t globalInterruptStatus;
	uint32_t port0CommandsRunning;
	uint32_t port0CommandsIssued;
	bool complete;
};

volatile uintptr_t recentInterruptEventsPointer;
volatile InterruptEvent recentInterruptEvents[64];

void AHCIController::DumpState() {
	uint64_t timeStamp = KGetTimeInMs();

	EsPrint("AHCI controller state:\n");

	EsPrint("\t--- Registers ---\n");
	EsPrint("\t\tHBA capabilities: %x.\n", RD_REGISTER_CAP());
	EsPrint("\t\tGlobal host control: %x.\n", RD_REGISTER_GHC());
	EsPrint("\t\tInterrupt status: %x.\n", RD_REGISTER_IS());
	EsPrint("\t\tPorts implemented: %x.\n", RD_REGISTER_PI());
	EsPrint("\t\tHBA capabilities extended: %x.\n", RD_REGISTER_CAP2());
	EsPrint("\t\tBIOS/OS handoff control and status: %x.\n", RD_REGISTER_BOHC());

	for (uintptr_t i = 0; i < MAX_PORTS; i++) {
		AHCIPort *port = ports + i;

		if (!port->connected) {
			continue;
		}

		EsPrint("\t--- Port %d ---\n", i);
		EsPrint("\t\tCommand list base address (low DWORD): %x.\n", RD_REGISTER_PCLB(i));
		EsPrint("\t\tCommand list base address (high DWORD): %x.\n", RD_REGISTER_PCLBU(i));
		EsPrint("\t\tFIS base address (low DWORD): %x.\n", RD_REGISTER_PFB(i));
		EsPrint("\t\tFIS base address (high DWORD): %x.\n", RD_REGISTER_PFBU(i));
		EsPrint("\t\tInterrupt status: %x.\n", RD_REGISTER_PIS(i));
		EsPrint("\t\tInterrupt enable: %x.\n", RD_REGISTER_PIE(i));
		EsPrint("\t\tCommand and status: %x.\n", RD_REGISTER_PCMD(i));
		EsPrint("\t\tTask file data: %x.\n", RD_REGISTER_PTFD(i));
		EsPrint("\t\tSignature: %x.\n", RD_REGISTER_PSIG(i));
		EsPrint("\t\tSATA status: %x.\n", RD_REGISTER_PSSTS(i));
		EsPrint("\t\tSATA error: %x.\n", RD_REGISTER_PSERR(i));
		EsPrint("\t\tCommand issue: %x.\n", RD_REGISTER_PCI(i));
		EsPrint("\t\tATAPI: %d.\n", port->atapi);
		EsPrint("\t\tBytes per sector: %D.\n", port->sectorBytes);
		EsPrint("\t\tTotal capacity: %D.\n", port->sectorBytes * port->sectorCount);
		EsPrint("\t\tCommand slots available: %d.\n", port->commandSlotsAvailable.state);
		EsPrint("\t\tRunning commands: %x.\n", port->runningCommands);

		uint8_t *receivedFIS = (uint8_t *) port->commandList + 0x400;

		EsPrint("\t\tReceived FIS D2H register: type %X, interrupt %X, status %X, error %X, device/head %X, sector %x, sector count %x.\n",
				receivedFIS[0x40 + 0], receivedFIS[0x40 + 1], receivedFIS[0x40 + 2], receivedFIS[0x40 + 3], receivedFIS[0x40 + 7],
				(uint64_t) receivedFIS[0x40 + 4] | ((uint64_t) receivedFIS[0x40 + 5] << 8) | ((uint64_t) receivedFIS[0x40 + 6] << 16)
				| ((uint64_t) receivedFIS[0x40 + 8] << 24) | ((uint64_t) receivedFIS[0x40 + 9] << 32) | ((uint64_t) receivedFIS[0x40 + 10] << 40),
				(uint64_t) receivedFIS[0x40 + 12] | ((uint64_t) receivedFIS[0x40 + 13] << 8));
		EsPrint("\t\tReceived FIS set device bits: type %X, interrupt %X, status %X, error %X.\n",
				receivedFIS[0x58 + 0], receivedFIS[0x58 + 1], receivedFIS[0x58 + 2], receivedFIS[0x58 + 3]);
		EsPrint("\t\tReceived FIS DMA setup: type %X, flags %X, buffer identifier low %x, buffer identifier high %x, buffer offset %x, transfer count %x.\n",
				receivedFIS[0], receivedFIS[1], ((uint32_t *) receivedFIS)[1], 
				((uint32_t *) receivedFIS)[2], ((uint32_t *) receivedFIS)[4], ((uint32_t *) receivedFIS)[5]);

		for (uintptr_t j = 0; j < 32; j++) {
			if (~port->runningCommands & (1 << j)) continue;

			EsPrint("\t\tCommand %d: started %dms ago for dispatch group %x.\n", j, 
					timeStamp - port->commandStartTimeStamps[j], port->commandContexts[j]);

			EsPrint("\t\t\tDW0 %x: %d FIS bytes, %z, %z, %d PRDT entries.\n", 
					port->commandList[j * 8 + 0], (port->commandList[j * 8 + 0]) & 31, 
					((port->commandList[j * 8 + 0]) & (1 << 5)) ? "SATAPI" : "SATA",
					((port->commandList[j * 8 + 0]) & (1 << 6)) ? "write" : "read",
					port->commandList[j * 8 + 0] >> 16);
			EsPrint("\t\t\tDW1 transferring %D.\n", port->commandList[j * 8 + 1]);
			EsPrint("\t\t\tDW2/3 command table base address: %x.\n", *(uint64_t *) &port->commandList[j * 8 + 2]);

			uint8_t *commandFIS = port->commandTables + COMMAND_TABLE_SIZE * j;

			EsPrint("\t\t\tH2D FIS: type %X, command %X, features %X, device/head %X, features 2 %X, control %X, sector %x, sector count %x.\n", 
					commandFIS[0], commandFIS[2], commandFIS[3], commandFIS[7], commandFIS[11], commandFIS[15],
					(uint64_t) commandFIS[4] | ((uint64_t) commandFIS[5] << 8) | ((uint64_t) commandFIS[6] << 16)
					| ((uint64_t) commandFIS[8] << 24) | ((uint64_t) commandFIS[9] << 32) | ((uint64_t) commandFIS[10] << 40),
					(uint64_t) commandFIS[12] | ((uint64_t) commandFIS[13] << 8));

			uint8_t *atapiCommand = commandFIS + 64;

			if (((port->commandList[j * 8 + 0]) & (1 << 5))) {
				EsPrint("\t\t\tATAPI command: %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X.\n",
						atapiCommand[0],  atapiCommand[1],  atapiCommand[2],  atapiCommand[3],
						atapiCommand[4],  atapiCommand[5],  atapiCommand[6],  atapiCommand[7],
						atapiCommand[8],  atapiCommand[9],  atapiCommand[10], atapiCommand[11],
						atapiCommand[12], atapiCommand[13], atapiCommand[14], atapiCommand[15]);
			}

			uint32_t *prdt = (uint32_t *) (commandFIS + 128);

			for (uintptr_t k = 0; k < (port->commandList[j * 8 + 0] >> 16); k++) {
				EsPrint("\t\t\tPRDT entry %d: base address %x, byte count %x%z.\n", 
						k, *(uint64_t *) (prdt + k * 4), (prdt[k * 4 + 3] & 0xFFFFFFF) + 1, 
						(prdt[k * 4 + 3] & 0x80000000) ? ", interrupt on completion" : "");
			}
		}
	}

	EsPrint("\t--- Most recent interrupts ---\n");

	for (uintptr_t i = 0; i < sizeof(recentInterruptEvents) / sizeof(recentInterruptEvents[0]); i++) {
		if (recentInterruptEventsPointer) recentInterruptEventsPointer--; 
		else recentInterruptEventsPointer = sizeof(recentInterruptEvents) / sizeof(recentInterruptEvents[0]) - 1;

		volatile InterruptEvent *event = recentInterruptEvents + recentInterruptEventsPointer;

		EsPrint("\t\tEvent %d: fired %dms ago, GIS %x, CI0 %x, CR0 %x%z.\n",
				i, timeStamp - event->timeStamp,
				event->globalInterruptStatus, event->port0CommandsIssued, 
				event->port0CommandsRunning, event->complete ? "" : ", incomplete");
	}
}

bool AHCIController::Access(uintptr_t portIndex, uint64_t offsetBytes, size_t countBytes, int operation, 
		KDMABuffer *buffer, uint64_t, KWorkGroup *dispatchGroup) {
	AHCIPort *port = ports + portIndex;

#if 0
	if (operation == K_ACCESS_WRITE) {
		KernelPanic("AHCIController::Access - Attempted write.\n");
	}
#endif

	// Find a command slot to use.

	uintptr_t commandIndex = 0;

	while (true) {
		KSpinlockAcquire(&port->commandSpinlock);

		uint32_t commandsAvailable = ~RD_REGISTER_PCI(portIndex);
		bool found = false;

		for (uintptr_t i = 0; i < commandSlotCount; i++) {
			if ((commandsAvailable & (1 << i)) && !port->commandContexts[i]) {
				commandIndex = i;
				found = true;
				break;
			}
		}

		if (!found) {
			KEventReset(&port->commandSlotsAvailable);
		} else {
			port->commandContexts[commandIndex] = dispatchGroup;
		}

		KSpinlockRelease(&port->commandSpinlock);

		if (!found) {
			KEventWait(&port->commandSlotsAvailable);
		} else {
			break;
		}
	}

	// Setup the command FIS.

	uint32_t countSectors = countBytes / port->sectorBytes;
	uint64_t offsetSectors = offsetBytes / port->sectorBytes;

	if (countSectors & ~0xFFFF) {
		KernelPanic("AHCIController::Access - Too many sectors to read.\n");
	}

	uint32_t *commandFIS = (uint32_t *) (port->commandTables + COMMAND_TABLE_SIZE * commandIndex);
	commandFIS[0] = 0x27 /* H2D */ | (1 << 15) /* command */ | ((operation == K_ACCESS_WRITE ? 0x35 /* write DMA 48 */ : 0x25 /* read DMA 48 */) << 16);
	commandFIS[1] = (offsetSectors & 0xFFFFFF) | (1 << 30);
	commandFIS[2] = (offsetSectors >> 24) & 0xFFFFFF;
	commandFIS[3] = countSectors & 0xFFFF;
	commandFIS[4] = 0;

	// Setup the PRDT.

	size_t prdtEntryCount = 0;
	uint32_t *prdt = (uint32_t *) (port->commandTables + COMMAND_TABLE_SIZE * commandIndex + 0x80);

	while (!KDMABufferIsComplete(buffer)) {
		if (prdtEntryCount == PRDT_ENTRY_COUNT) {
			KernelPanic("AHCIController::Access - Too many PRDT entries.\n");
		}

		KDMASegment segment = KDMABufferNextSegment(buffer);

		prdt[0 + 4 * prdtEntryCount] = segment.physicalAddress;
		prdt[1 + 4 * prdtEntryCount] = segment.physicalAddress >> 32;
		prdt[2 + 4 * prdtEntryCount] = 0;
		prdt[3 + 4 * prdtEntryCount] = (segment.byteCount - 1) | (segment.isLast ? (1 << 31) /* IRQ when done */ : 0);

		prdtEntryCount++;
	}

	// Setup the command list entry, and issue the command.

	port->commandList[commandIndex * 8 + 0] = 5 /* FIS is 5 DWORDs */ | (prdtEntryCount << 16) | (operation == K_ACCESS_WRITE ? (1 << 6) : 0);
	port->commandList[commandIndex * 8 + 1] = 0;

	// Setup SCSI command if ATAPI.
	
	if (port->atapi) {
		port->commandList[commandIndex * 8 + 0] |= (1 << 5) /* ATAPI */;
		commandFIS[0] = 0x27 /* H2D */ | (1 << 15) /* command */ | (0xA0 /* packet */ << 16);
		commandFIS[1] = countBytes << 8; 

		uint8_t *scsiCommand = (uint8_t *) commandFIS + 0x40;
		EsMemoryZero(scsiCommand, 10);
		scsiCommand[0] = 0xA8 /* READ (12) */;
		scsiCommand[2] = (offsetSectors >> 0x18) & 0xFF;
		scsiCommand[3] = (offsetSectors >> 0x10) & 0xFF;
		scsiCommand[4] = (offsetSectors >> 0x08) & 0xFF;
		scsiCommand[5] = (offsetSectors >> 0x00) & 0xFF;
		scsiCommand[9] = countSectors;
	}

	// Start executing the command.

	KSpinlockAcquire(&port->commandSpinlock);
	port->runningCommands |= 1 << commandIndex;
	__sync_synchronize();
	WR_REGISTER_PCI(portIndex, 1 << commandIndex);
	port->commandStartTimeStamps[commandIndex] = KGetTimeInMs();
	KSpinlockRelease(&port->commandSpinlock);

	return true;
}

bool AHCIController::HandleIRQ() {
	uint32_t globalInterruptStatus = RD_REGISTER_IS();
	KernelLog(LOG_VERBOSE, "AHCI", "received IRQ", "Received IRQ with status: %x.\n", globalInterruptStatus);
	if (!globalInterruptStatus) return false;
	WR_REGISTER_IS(globalInterruptStatus);

	volatile InterruptEvent *event = recentInterruptEvents + recentInterruptEventsPointer;
	event->timeStamp = KGetTimeInMs();
	event->globalInterruptStatus = globalInterruptStatus;
	event->complete = false;
	recentInterruptEventsPointer = (recentInterruptEventsPointer + 1) % (sizeof(recentInterruptEvents) / sizeof(recentInterruptEvents[0]));

	bool commandCompleted = false;

	for (uintptr_t i = 0; i < MAX_PORTS; i++) {
		if (~globalInterruptStatus & (1 << i)) continue;

		uint32_t interruptStatus = RD_REGISTER_PIS(i);
		if (!interruptStatus) continue;
		WR_REGISTER_PIS(i, interruptStatus);

		AHCIPort *port = ports + i;

		if (interruptStatus & ((1 << 30) | (1 << 29) | (1 << 28) | (1 << 27) | (1 << 26) | (1 << 24) | (1 << 23))) {
			KernelLog(LOG_ERROR, "AHCI", "error IRQ", "Received IRQ error interrupt status bit set: %x.\n", interruptStatus);

			KSpinlockAcquire(&port->commandSpinlock);

			// Stop command processing.

			WR_REGISTER_PCMD(i, RD_REGISTER_PCMD(i) & ~(1 << 0));

			// Fail all outstanding commands.

			for (uintptr_t j = 0; j < 32; j++) {
				if (port->runningCommands & (1 << j)) {
					port->commandContexts[j]->End(false /* failed */);
					port->commandContexts[j] = nullptr;
				}
			}

			port->runningCommands = 0;
			KEventSet(&port->commandSlotsAvailable, false, true /* maybe already set */);

			// Restart command processing.

			WR_REGISTER_PSERR(i, 0xFFFFFFFF);
			KTimeout timeout(5);
			while ((RD_REGISTER_PCMD(i) & (1 << 15)) && !timeout.Hit());
			WR_REGISTER_PCMD(i, RD_REGISTER_PCMD(i) | (1 << 0));

			KSpinlockRelease(&port->commandSpinlock);

			continue;
		} 

		KSpinlockAcquire(&port->commandSpinlock);

		uint32_t commandsIssued = RD_REGISTER_PCI(i);

		if (i == 0) event->port0CommandsIssued = commandsIssued, event->port0CommandsRunning = port->runningCommands;

		for (uintptr_t j = 0; j < 32; j++) {
			if (~port->runningCommands & (1 << j)) continue; // Command not started.
			if (commandsIssued & (1 << j)) continue; // Command still running.

			// The command has completed.

			port->commandContexts[j]->End(true /* success */);
			port->commandContexts[j] = nullptr;
			KEventSet(&port->commandSlotsAvailable, false, true /* maybe already set */);
			port->runningCommands &= ~(1 << j);

			commandCompleted = true;
		}

		KSpinlockRelease(&port->commandSpinlock);
	}

	if (commandCompleted) {
		KSwitchThreadAfterIRQ();
	}

	event->complete = true;

	return true;
}

void TimeoutTimerHit(EsGeneric argument) {
	AHCIController *controller = (AHCIController *) argument.p;

	uint64_t currentTimeStamp = KGetTimeInMs();

	for (uintptr_t i = 0; i < MAX_PORTS; i++) {
		AHCIPort *port = controller->ports + i;

		KSpinlockAcquire(&port->commandSpinlock);

		for (uintptr_t j = 0; j < controller->commandSlotCount; j++) {
			if ((port->runningCommands & (1 << j))
					&& port->commandStartTimeStamps[j] + GENERAL_TIMEOUT < currentTimeStamp) {
				KernelLog(LOG_ERROR, "AHCI", "command timeout", "Command %d on port %d timed out.\n", j, i);
				
				port->commandContexts[j]->End(false /* failure */);
				port->commandContexts[j] = nullptr;
				port->runningCommands &= ~(1 << j);

				// Don't set the commandSlotsAvailable event, since the controller still thinks the command is in use.
				// TODO What happens if there are no commands left?
			}
		}

		KSpinlockRelease(&port->commandSpinlock);
	}

	KTimerSet(&controller->timeoutTimer, GENERAL_TIMEOUT, TimeoutTimerHit, controller);
}

bool AHCIController::SendSingleCommand(uintptr_t port) {
	KTimeout timeout(GENERAL_TIMEOUT);

	// Wait for the port to be ready, then issue the command.

	while ((RD_REGISTER_PTFD(port) & ((1 << 7) | (1 << 3))) && !timeout.Hit());

	if (timeout.Hit()) {
		KernelLog(LOG_ERROR, "AHCI", "port hung", "Port %d bits DRQ/BSY won't clear.\n", port);
		return false;
	}

	__sync_synchronize();
	WR_REGISTER_PCI(port, 1 << 0);

	// Wait for command to complete.

	bool complete = false;

	while (!timeout.Hit()) {
		if (~RD_REGISTER_PCI(port) & (1 << 0)) {
			complete = true;
			break;
		}
	}

	return complete;
}

void AHCIController::Initialise() {
	// Perform BIOS/OS handoff, if necessary.

	if (RD_REGISTER_CAP2() & (1 << 0)) {
		KernelLog(LOG_INFO, "AHCI", "perform handoff", "Performing BIOS/OS handoff...\n");

		WR_REGISTER_BOHC(RD_REGISTER_BOHC() | (1 << 1));

		KTimeout timeout(25 /* ms */);
		uint32_t status;

		while (true) {
			status = RD_REGISTER_BOHC();
			if (~status & (1 << 0)) break;
			if (timeout.Hit()) break;
		}

		if (status & (1 << 0)) {
			KEvent event = {};
			KernelLog(LOG_ERROR, "AHCI", "handoff error", "BIOS/OS handoff did not succeed, waiting 2 seconds to proceed...\n");
			KEventWait(&event, 2000 /* ms */);
		}
	}

	// Reset controller.

	{
		KTimeout timeout(GENERAL_TIMEOUT);
		WR_REGISTER_GHC(RD_REGISTER_GHC() | (1 << 0));
		while ((RD_REGISTER_GHC() & (1 << 0)) && !timeout.Hit());

		if (timeout.Hit()) {
			KernelLog(LOG_ERROR, "AHCI", "reset controller timeout", "The controller did not reset within the timeout.\n");
			return;
		}
	}

	// Register IRQ handler.

	KIRQHandler handler = [] (uintptr_t, void *context) { return ((AHCIController *) context)->HandleIRQ(); };

	if (!pci->EnableSingleInterrupt(handler, this, "AHCI")) {
		KernelLog(LOG_ERROR, "AHCI", "IRQ registration failure", "Could not register intrrupt handler.\n");
		return;
	}

	// Enable AHCI mode and interrupts.

	WR_REGISTER_GHC(RD_REGISTER_GHC() | (1 << 31) | (1 << 1));

	capabilities = RD_REGISTER_CAP();
	capabilities2 = RD_REGISTER_CAP2();
	commandSlotCount = ((capabilities >> 8) & 31) + 1;
	dma64Supported = capabilities & (1 << 31);

#ifdef ARCH_64
	if (!dma64Supported) {
		KernelLog(LOG_ERROR, "AHCI", "controller cannot DMA", "The controller reports it cannot use 64-bit addresses in DMA transfer.\n");
		return;
	}
#endif

	// Work out which ports have drives connected.

	size_t maximumNumberOfPorts = (capabilities & 31) + 1;
	size_t portsFound = 0;

	uint32_t portsImplemented = RD_REGISTER_PI();

	for (uintptr_t i = 0; i < MAX_PORTS; i++) {
		if (portsImplemented & (1 << i)) {
			portsFound++;

			if (portsFound <= maximumNumberOfPorts) {
				ports[i].connected = true;
			}
		}
	}

	KernelLog(LOG_INFO, "AHCI", "information", "Capabilities: %x, %x. Command slot count: %d. Implemented ports: %x.\n", 
			capabilities, capabilities2, commandSlotCount, portsImplemented);

	// Setup the command lists, FISes and command tables.

	for (uintptr_t i = 0; i < MAX_PORTS; i++) {
		if (!ports[i].connected) continue;

		size_t bytesNeeded = COMMAND_LIST_SIZE + RECEIVED_FIS_SIZE + COMMAND_TABLE_SIZE * commandSlotCount;

		uint8_t *virtualAddress;
		uintptr_t physicalAddress;

		if (!MMPhysicalAllocateAndMap(bytesNeeded, K_PAGE_SIZE, dma64Supported ? 64 : 32, true, MM_REGION_NOT_CACHEABLE, &virtualAddress, &physicalAddress)) {
			KernelLog(LOG_ERROR, "AHCI", "allocation failure", "Could not allocate physical memory for port %d.\n", i);
			break;
		}

		ports[i].commandList = (uint32_t *) virtualAddress;
		ports[i].commandTables = virtualAddress + COMMAND_LIST_SIZE + RECEIVED_FIS_SIZE;

		// Set the registers to the physical addresses.

		WR_REGISTER_PCLB(i, physicalAddress); 
		if (dma64Supported) WR_REGISTER_PCLBU(i, physicalAddress >> 32);
		WR_REGISTER_PFB(i, (physicalAddress + 0x400));
		if (dma64Supported) WR_REGISTER_PFBU(i, (physicalAddress + 0x400) >> 32);

		// Point each command list entry to the corresponding command table.

		uint32_t *commandList = ports[i].commandList;

		for (uintptr_t j = 0; j < commandSlotCount; j++) {
			uintptr_t address = physicalAddress + COMMAND_LIST_SIZE + RECEIVED_FIS_SIZE + COMMAND_TABLE_SIZE * j;
			commandList[j * 8 + 2] = address;
			commandList[j * 8 + 3] = address >> 32;
		}

		// Reset the port.

		KTimeout timeout(GENERAL_TIMEOUT);

		const uint32_t runningBits = ((1 << 0 /* start */) | (1 << 4 /* receive FIS enable */)
				| (1 << 15 /* command list running */) | (1 << 14 /* receive FIS running */));

		while (true) {
			uint32_t status = RD_REGISTER_PCMD(i);

			if (!(status & runningBits) || timeout.Hit()) {
				break;
			}

			// Stop command list processing and receive FIS.
			WR_REGISTER_PCMD(i, status & ~((1 << 0) | (1 << 4)));
		}

		if (RD_REGISTER_PCMD(i) & runningBits) {
			KernelLog(LOG_ERROR, "AHCI", "reset port timeout", "Resetting port %d timed out. PCMD: %x.\n", 
					i, RD_REGISTER_PCMD(i));
			ports[i].connected = false;
			continue;
		}

		// Clear IRQs.

		WR_REGISTER_PIE(i, RD_REGISTER_PIE(i) & 0x0E3FFF00);
		WR_REGISTER_PIS(i, RD_REGISTER_PIS(i));

		// Enable receive FIS and activate the drive.

		WR_REGISTER_PSCTL(i, RD_REGISTER_PSCTL(i) | (3 << 8)); // Disable transitions to partial and slumber states.
		WR_REGISTER_PCMD(i, (RD_REGISTER_PCMD(i) & 0x0FFFFFFF) 
				| (1 << 1 /* spin up */) | (1 << 2 /* power on */) | (1 << 4 /* FIS receive */) | (1 << 28 /* activate */));

		KTimeout linkTimeout(10);

		while ((RD_REGISTER_PSSTS(i) & 0x0F) != 3 && !linkTimeout.Hit());

		if ((RD_REGISTER_PSSTS(i) & 0x0F) != 3) {
			KernelLog(LOG_ERROR, "AHCI", "activate port timeout", "Activating port %d timed out. PSSTS: %x.\n", 
					i, RD_REGISTER_PSSTS(i));
			ports[i].connected = false;
			continue;
		}

		// Clear errors.

		WR_REGISTER_PSERR(i, RD_REGISTER_PSERR(i));

		// Wait for device to be ready.

		while ((RD_REGISTER_PTFD(i) & 0x88 /* BSY and DRQ */) && !timeout.Hit());

		if (RD_REGISTER_PTFD(i) & 0x88) {
			KernelLog(LOG_ERROR, "AHCI", "port ready timeout", "Port %d hung at busy state. PTFD: %x.\n", 
					i, RD_REGISTER_PTFD(i));
			ports[i].connected = false;
			continue;
		}

		// Start command list processing.

		KernelLog(LOG_INFO, "AHCI", "start command processing", "Starting command processing for port %d...\n", i);
		WR_REGISTER_PCMD(i, RD_REGISTER_PCMD(i) | (1 << 0));

		// Enable interrupts.

		KernelLog(LOG_INFO, "AHCI", "enable interrupts", "Enabling interrupts for port %d...\n", i);
		WR_REGISTER_PIE(i, RD_REGISTER_PIE(i) | (1 << 5) /* descriptor complete */ | (1 << 0) /* D2H */
				| (1 << 30) | (1 << 29) | (1 << 28) | (1 << 27) | (1 << 26) | (1 << 24) | (1 << 23) /* errors */);
	}

	// Read the status and signature for each implemented port to work out if it is connected.

	for (uintptr_t i = 0; i < MAX_PORTS; i++) {
		if (!ports[i].connected) continue;

		uint32_t status = RD_REGISTER_PSSTS(i);

		if ((status & 0x00F) != 0x003 || (status & 0x0F0) == 0x000 || (status & 0xF00) != 0x100) {
			ports[i].connected = false;
			KernelLog(LOG_INFO, "AHCI", "no drive", "No drive connected to port %d (1).\n", i);
			continue;
		}

		uint32_t signature = RD_REGISTER_PSIG(i);

		if (signature == 0x00000101) {
			// SATA drive.
			KernelLog(LOG_INFO, "AHCI", "found drive", "Found SATA drive on port %d.\n", i);
		} else if (signature == 0xEB140101) {
			// SATAPI drive.
			ports[i].atapi = true;
			KernelLog(LOG_INFO, "AHCI", "found drive", "Found SATAPI drive on port %d.\n", i);
		} else if (!signature) {
			// No drive connected.
			ports[i].connected = false;
			KernelLog(LOG_INFO, "AHCI", "no drive", "No drive connected to port %d (2).\n", i);
		} else {
			KernelLog(LOG_ERROR, "AHCI", "unrecognised drive signature", "Unrecognised drive signature %x on port %d.\n", signature, i);
			ports[i].connected = false;
		}
	}

	// Identify each connected drive.

	uint16_t *identifyData;
	uintptr_t identifyDataPhysical;

	if (!MMPhysicalAllocateAndMap(0x200, K_PAGE_SIZE, dma64Supported ? 64 : 32, true, MM_REGION_NOT_CACHEABLE, (uint8_t **) &identifyData, &identifyDataPhysical)) {
		KernelLog(LOG_ERROR, "AHCI", "allocation failure", "Could not allocate physical memory for identify data buffer.\n");
		return;
	}

	KernelLog(LOG_INFO, "AHCI", "identify data", "Identify data buffer allocated at physical address %x.\n", identifyDataPhysical);

	for (uintptr_t i = 0; i < MAX_PORTS; i++) {
		if (!ports[i].connected) continue;

		EsMemoryZero(identifyData, 0x200);

		// Setup the command list entry.

		ports[i].commandList[0] = 5 /* FIS is 5 DWORDs */ | (1 << 16) /* 1 PRDT entry */;
		ports[i].commandList[1] = 0;

		// Setup the command FIS.

		uint8_t opcode = ports[i].atapi ? 0xA1 /* IDENTIFY PACKET */ : 0xEC /* IDENTIFY */;
		uint32_t *commandFIS = (uint32_t *) ports[i].commandTables;
		commandFIS[0] = 0x27 /* H2D */ | (1 << 15) /* command */ | (opcode << 16);
		commandFIS[1] = commandFIS[2] = commandFIS[3] = commandFIS[4] = 0;

		// Setup the PRDT.

		uint32_t *prdt = (uint32_t *) (ports[i].commandTables + 0x80);
		prdt[0] = identifyDataPhysical;
		prdt[1] = identifyDataPhysical >> 32;
		prdt[2] = 0;
		prdt[3] = 0x200 - 1;

		KernelLog(LOG_INFO, "AHCI", "identifying drive", "Sending IDENTIFY command to port %d...\n", i);

		if (!SendSingleCommand(i)) {
			KernelLog(LOG_ERROR, "AHCI", "identify failure", "Could not read identify data for port %d.\n", i);
			WR_REGISTER_PCMD(i, RD_REGISTER_PCMD(i) & ~(1 << 0)); // Stop command processing.
			ports[i].connected = false;
			continue;
		}

		ports[i].sectorBytes = 0x200;

		if ((identifyData[106] & (1 << 14)) && (~identifyData[106] & (1 << 15)) && (identifyData[106] & (1 << 12))) {
			// Device has a logical sector size larger than 0x200 bytes.
			ports[i].sectorBytes = (uint32_t) identifyData[117] | ((uint32_t) identifyData[118] << 16);
		}

		ports[i].sectorCount = ((uint64_t) identifyData[100] << 0) + ((uint64_t) identifyData[101] << 16) 
			+ ((uint64_t) identifyData[102] << 32) + ((uint64_t) identifyData[103] << 48);

		if (!((identifyData[49] & (1 << 9)) && (identifyData[49] & (1 << 8)))) {
			KernelLog(LOG_ERROR, "AHCI", "unsupported feature", "Drive on port %d does not support a required feature.\n", i);
			ports[i].connected = false;
			continue;
		}

		if (ports[i].atapi) {
			// Send a read capacity command.

			ports[i].commandList[0] = 5 /* FIS is 5 DWORDs */ | (1 << 16) /* 1 PRDT entry */ | (1 << 5) /* ATAPI */;
			commandFIS[0] = 0x27 /* H2D */ | (1 << 15) /* command */ | (0xA0 /* packet */ << 16);
			commandFIS[1] = 8 /* maximum byte count transfer */ << 8; 
			prdt[3] = 8 - 1;

			uint8_t *scsiCommand = (uint8_t *) commandFIS + 0x40;
			EsMemoryZero(scsiCommand, 10);
			scsiCommand[0] = 0x25 /* READ CAPACITY (10) */;

			if (!SendSingleCommand(i)) {
				KernelLog(LOG_ERROR, "AHCI", "identify failure", "Could not read SCSI read capacity data for port %d.\n", i);
				WR_REGISTER_PCMD(i, RD_REGISTER_PCMD(i) & ~(1 << 0)); // Stop command processing.
				ports[i].connected = false;
				continue;
			}

			uint8_t *capacity = (uint8_t *) identifyData;

			ports[i].sectorCount = (((uint64_t) capacity[3] << 0) + ((uint64_t) capacity[2] << 8) 
				+ ((uint64_t) capacity[1] << 16) + ((uint64_t) capacity[0] << 24)) + 1;
			ports[i].sectorBytes = ((uint64_t) capacity[7] << 0) + ((uint64_t) capacity[6] << 8) 
				+ ((uint64_t) capacity[5] << 16) + ((uint64_t) capacity[4] << 24);
		}

		if (ports[i].sectorCount <= 128 || (ports[i].sectorBytes & 0x1FF) || !ports[i].sectorBytes || ports[i].sectorBytes > 0x1000) {
			KernelLog(LOG_ERROR, "AHCI", "unsupported feature", "Drive on port %d has invalid sector configuration (count: %d, size: %D).\n", 
					i, ports[i].sectorCount, ports[i].sectorBytes);
			ports[i].connected = false;
			continue;
		}

		for (uintptr_t j = 0; j < 20; j++) {
			ports[i].model[j * 2 + 0] = identifyData[27 + j] >> 8;
			ports[i].model[j * 2 + 1] = identifyData[27 + j] & 0xFF;
		}

		ports[i].model[40] = 0;

		for (uintptr_t j = 39; j > 0; j--) {
			if (ports[i].model[j] == ' ') {
				ports[i].model[j] = 0;
			} else {
				break;
			}
		}

		ports[i].ssd = identifyData[217] == 1;

		for (uintptr_t i = 10; i < 20; i++) identifyData[i] = (identifyData[i] >> 8) | (identifyData[i] << 8);
		for (uintptr_t i = 23; i < 27; i++) identifyData[i] = (identifyData[i] >> 8) | (identifyData[i] << 8);
		for (uintptr_t i = 27; i < 47; i++) identifyData[i] = (identifyData[i] >> 8) | (identifyData[i] << 8);

		KernelLog(LOG_INFO, "AHCI", "identified drive", "Identified drive on port %d; serial - '%s', firmware - '%s', model - '%s', sector size - %D, sector count - %d.\n",
				i, 20, identifyData + 10, 8, identifyData + 23, 40, identifyData + 27, ports[i].sectorBytes, ports[i].sectorCount);
	}

	MMFree(MMGetKernelSpace(), identifyData);
	MMPhysicalFree(identifyDataPhysical);

	// Start the timeout timer.

	KTimerSet(&timeoutTimer, GENERAL_TIMEOUT, TimeoutTimerHit, this);

	// Register drives.

	for (uintptr_t i = 0; i < MAX_PORTS; i++) {
		if (!ports[i].connected) continue;

		AHCIDrive *device = (AHCIDrive *) KDeviceCreate("AHCI drive", this, sizeof(AHCIDrive));

		if (!device) {
			KernelLog(LOG_ERROR, "AHCI", "allocation failure", "Could not create device for port %d.\n", i);
			break;
		}

		device->controller = this;
		device->port = i;

		device->sectorSize = ports[i].sectorBytes;
		device->sectorCount = ports[i].sectorCount;
		device->maxAccessSectorCount = ports[i].atapi ? (65535 / device->sectorSize) 
			: ((PRDT_ENTRY_COUNT - 1 /* need one extra if not page aligned */) * K_PAGE_SIZE / device->sectorSize);
		device->readOnly = ports[i].atapi;
		device->cModel = ports[i].model;
		device->driveType = ports[i].atapi ? ES_DRIVE_TYPE_CDROM : ports[i].ssd ? ES_DRIVE_TYPE_SSD : ES_DRIVE_TYPE_HDD;

		device->access = [] (KBlockDeviceAccessRequest request) {
			AHCIDrive *drive = (AHCIDrive *) request.device;

			request.dispatchGroup->Start();

			if (!drive->controller->Access(drive->port, request.offset, request.count, request.operation, 
						request.buffer, request.flags, request.dispatchGroup)) {
				request.dispatchGroup->End(false);
			}
		};

		FSRegisterBlockDevice(device);
	}
}

static void DeviceAttach(KDevice *_parent) {
	KPCIDevice *parent = (KPCIDevice *) _parent;

	AHCIController *device = (AHCIController *) KDeviceCreate("AHCI controller", parent, sizeof(AHCIController));
	if (!device) return;
	device->pci = parent;

	device->dumpState = [] (KDevice *device) {
		((AHCIController *) device)->DumpState();
	};

	// Enable PCI features.
	parent->EnableFeatures(K_PCI_FEATURE_INTERRUPTS 
			| K_PCI_FEATURE_BUSMASTERING_DMA
			| K_PCI_FEATURE_MEMORY_SPACE_ACCESS
			| K_PCI_FEATURE_BAR_5);

	// Initialise the controller.
	device->Initialise();
}

KDriver driverAHCI = {
	.attach = DeviceAttach,
};
