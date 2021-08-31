#include <module.h>

// TODO Sometimes completion interrupts get missed?
// TODO How many IO completion/submission queues should we create, and how many entries should they contain?
// TODO Command timeout.

#define GENERAL_TIMEOUT (5000)

#define RD_REGISTER_CAP()         pci-> ReadBAR64(0, 0x00)                                       // Controller capababilities.
#define WR_REGISTER_CAP(x)        pci->WriteBAR64(0, 0x00, x)                                        
#define RD_REGISTER_VS()          pci-> ReadBAR32(0, 0x08)                                       // Version.
#define WR_REGISTER_VS(x)         pci->WriteBAR32(0, 0x08, x)                                        
#define RD_REGISTER_INTMS()       pci-> ReadBAR32(0, 0x0C)                                       // Interrupt mask set.
#define WR_REGISTER_INTMS(x)      pci->WriteBAR32(0, 0x0C, x)                                        
#define RD_REGISTER_INTMC()       pci-> ReadBAR32(0, 0x10)                                       // Interrupt mask clear.
#define WR_REGISTER_INTMC(x)      pci->WriteBAR32(0, 0x10, x)                                        
#define RD_REGISTER_CC()          pci-> ReadBAR32(0, 0x14)                                       // Controller configuration.
#define WR_REGISTER_CC(x)         pci->WriteBAR32(0, 0x14, x)                                        
#define RD_REGISTER_CSTS()        pci-> ReadBAR32(0, 0x1C)                                       // Controller status.
#define WR_REGISTER_CSTS(x)       pci->WriteBAR32(0, 0x1C, x)                                        
#define RD_REGISTER_AQA()         pci-> ReadBAR32(0, 0x24)                                       // Admin queue attributes.
#define WR_REGISTER_AQA(x)        pci->WriteBAR32(0, 0x24, x)                                        
#define RD_REGISTER_ASQ()         pci-> ReadBAR64(0, 0x28)                                       // Admin submission queue base address.
#define WR_REGISTER_ASQ(x)        pci->WriteBAR64(0, 0x28, x)                                        
#define RD_REGISTER_ACQ()         pci-> ReadBAR64(0, 0x30)                                       // Admin completion queue base address.
#define WR_REGISTER_ACQ(x)        pci->WriteBAR64(0, 0x30, x)                                

#define RD_REGISTER_SQTDBL(i)     pci-> ReadBAR32(0, 0x1000 + doorbellStride * (2 * (i) + 0))    // Submission queue tail doorbell.
#define WR_REGISTER_SQTDBL(i, x)  pci->WriteBAR32(0, 0x1000 + doorbellStride * (2 * (i) + 0), x) 
#define RD_REGISTER_CQHDBL(i)     pci-> ReadBAR32(0, 0x1000 + doorbellStride * (2 * (i) + 1))    // Completion queue head doorbell.
#define WR_REGISTER_CQHDBL(i, x)  pci->WriteBAR32(0, 0x1000 + doorbellStride * (2 * (i) + 1), x) 

#define ADMIN_QUEUE_ENTRY_COUNT      (2) 
#define IO_QUEUE_ENTRY_COUNT         (256)
#define SUBMISSION_QUEUE_ENTRY_BYTES (64)
#define COMPLETION_QUEUE_ENTRY_BYTES (16)

struct NVMeController : KDevice {
	KPCIDevice *pci;

	uint64_t capabilities;
	uint32_t version;

	size_t doorbellStride;
	uint64_t readyTransitionTimeout;
	uint64_t maximumDataTransferBytes;
	uint32_t rtd3EntryLatencyUs;
	uint16_t maximumOutstandingCommands;

	uint8_t *adminCompletionQueue, *adminSubmissionQueue;
	uint32_t adminCompletionQueueHead, adminSubmissionQueueTail;
	KEvent adminCompletionQueueReceived;
	bool adminCompletionQueuePhase;
	uint32_t adminCompletionQueueLastResult;
	uint16_t adminCompletionQueueLastStatus;

	uint8_t *ioCompletionQueue, *ioSubmissionQueue;
	uint32_t ioCompletionQueueHead, ioSubmissionQueueTail;
	volatile uint32_t ioSubmissionQueueHead;
	bool ioCompletionQueuePhase;
	KEvent ioSubmissionQueueNonFull;
	KSpinlock ioQueueSpinlock;
	KWorkGroup *dispatchGroups[IO_QUEUE_ENTRY_COUNT];
	uint64_t prpListPages[IO_QUEUE_ENTRY_COUNT];
	uint64_t *prpListVirtual;

	void Initialise();
	void Shutdown();

	bool HandleIRQ();
	bool IssueAdminCommand(const void *command, uint32_t *result);
	bool Access(struct NVMeDrive *drive, uint64_t offsetBytes, size_t countBytes, int operation, 
			KDMABuffer *buffer, uint64_t flags, KWorkGroup *dispatchGroup);
	void DumpState();
};

struct NVMeDrive : KBlockDevice {
	NVMeController *controller;
	uint32_t nsid;
};

const char *genericCommandStatusValues[] = {
	"Successful completion",
	"Invalid command opcode",
	"Invalid field in command",
	"Command ID conflict",
	"Data transfer error",
	"Commands aborted due to powerloss notification",
	"Internal error",
	"Command abort requested",
	"Command aborted due to SQ deletion",
	"Command aborted due to failed fused command",
	"Command aborted due to missing fused command",
	"Invalid namespace or format",
	"Command sequence error",
	"Invalid SGL segment descriptor",
	"Invalid number of SGL descriptors",
	"Data SGL length invalid",
	"Metadata SGL length invalid",
	"SGL descriptor type invalid",
	"Invalid use of controller memory buffer",
	"PRP offset invalid",
	"Atomic write unit exceeded",
	"Operation denied",
	"SGL offset invalid",
	"Reserved",
	"Host identifier inconsistent format",
	"Keep alive timer expired",
	"Keep alive timeout invalid",
	"Command aborted due to preempt and abort",
	"Sanitize failed",
	"Sanitize in progress",
	"SGL data block granularity invalid",
	"Command not supported for queue in CMB",
	"Namespace is write protected",
	"Command interrupted",
	"Transient transport error",
};

const char *genericCommandStatusValuesNVM[] = {
	"LBA out of range",
	"Capacity exceeded",
	"Namespace not ready",
	"Reservation conflict",
	"Format in progress",
};

const char *commandSpecificStatusValues[] = {
	"Completion queue invalid",
	"Invalid queue identifier",
	"Invalid queue size",
	"Abort command limit exceeded",
	"Reserved",
	"Asynchronous event request limit exceeded",
	"Invalid firmware slot",
	"Invalid firmware image",
	"Invalid interrupt vector",
	"Invalid log page",
	"Invalid format",
	"Firmware activation requirse conventional reset",
	"Invalid queue deletion",
	"Feature identifier not saveable",
	"Feature not changeable",
	"Feature not namespace specific",
	"Firmware activation requires NVM subsystem reset",
	"Firmware activation requires controller level reset",
	"Firmware activation requires maximum time violation",
	"Firmware activation prohibited",
	"Overlapping range",
	"Namespace insufficient capacity",
	"Namespace identifier unavailable",
	"Reserved",
	"Namespace already attached",
	"Namespace is private",
	"Namespace not attached",
	"Thin provisioning not supported",
	"Controller list invalid",
	"Device self-test in progress",
	"Boot partition write prohibited",
	"Invalid controller identifier",
	"Invalid secondary controller state",
	"Invalid number of controller resources",
	"Invalid resource identifier",
	"Sanitize prohibited while persistent memory region is enabled",
	"ANA group identifier invalid",
	"ANA attach failed",
};

const char *commandSpecificStatusValuesNVM[] = {
	"Confliciting attributes",
	"Invalid protection information",
	"Attempted write to read only range",
};

const char *mediaAndDataIntegrityErrorValuesNVM[] = {
	"Write fault",
	"Unrecovered read error",
	"End-to-end guard check error",
	"End-to-end application tag check error",
	"End-to-end reference tag check error",
	"Compare failure",
	"Access denied",
	"Dealocated or unwritten logical block",
};

const char *pathRelatedStatusValues[] = {
	"Internal path error",
	"Asymmetric access persistent loss",
	"Asymmetric access inaccessible",
	"Asymmetric access transition",
};

const char *GetErrorMessage(uint8_t statusCodeType, uint8_t statusCode) {
	if (statusCodeType == 0) {
		if (statusCode < sizeof(genericCommandStatusValues) / sizeof(genericCommandStatusValues[0])) {
			return genericCommandStatusValues[statusCode];
		} else if (statusCode > 0x80 && (uint8_t) (statusCode - 0x80) < sizeof(genericCommandStatusValuesNVM) / sizeof(genericCommandStatusValuesNVM[0])) {
			return genericCommandStatusValuesNVM[statusCode - 0x80];
		}
	} else if (statusCodeType == 1) {
		if (statusCode < sizeof(commandSpecificStatusValues) / sizeof(commandSpecificStatusValues[0])) {
			return commandSpecificStatusValues[statusCode];
		} else if (statusCode > 0x80 && (uint8_t) (statusCode - 0x80) < sizeof(commandSpecificStatusValuesNVM) / sizeof(commandSpecificStatusValuesNVM[0])) {
			return commandSpecificStatusValuesNVM[statusCode - 0x80];
		}
	} else if (statusCodeType == 2) {
		if (statusCode > 0x80 && (uint8_t) (statusCode - 0x80) < sizeof(mediaAndDataIntegrityErrorValuesNVM) / sizeof(mediaAndDataIntegrityErrorValuesNVM[0])) {
			return mediaAndDataIntegrityErrorValuesNVM[statusCode - 0x80];
		}
	} else if (statusCodeType == 3) {
		if (statusCode < sizeof(pathRelatedStatusValues) / sizeof(pathRelatedStatusValues[0])) {
			return pathRelatedStatusValues[statusCode];
		}
	}

	return "Unknown error";
}

void NVMeController::DumpState() {
	EsPrint("NVMe controller state:\n");

	EsPrint("\t--- Registers ---\n");
	EsPrint("\t\tController capababilities: %x.\n", RD_REGISTER_CAP());
	EsPrint("\t\tVersion: %x.\n", RD_REGISTER_VS());
	EsPrint("\t\tInterrupt mask set: %x.\n", RD_REGISTER_INTMS());
	EsPrint("\t\tInterrupt mask clear: %x.\n", RD_REGISTER_INTMC());
	EsPrint("\t\tController configuration: %x.\n", RD_REGISTER_CC());
	EsPrint("\t\tController status: %x.\n", RD_REGISTER_CSTS());
	EsPrint("\t\tAdmin queue attributes: %x.\n", RD_REGISTER_AQA());
	EsPrint("\t\tAdmin submission queue base address: %x.\n", RD_REGISTER_ASQ());
	EsPrint("\t\tAdmin completion queue base address: %x.\n", RD_REGISTER_ACQ());
	EsPrint("\t\tAdmin submission queue tail doorbell: %x.\n", RD_REGISTER_SQTDBL(0));
	EsPrint("\t\tAdmin completion queue head doorbell: %x.\n", RD_REGISTER_CQHDBL(0));
	EsPrint("\t\tIO submission queue tail doorbell: %x.\n", RD_REGISTER_SQTDBL(1));
	EsPrint("\t\tIO completion queue head doorbell: %x.\n", RD_REGISTER_CQHDBL(1));

	EsPrint("\t--- Internal ---\n");
	EsPrint("\t\tAdmin completion queue: %x.\n", adminCompletionQueue);
	EsPrint("\t\tAdmin completion queue head: %x.\n", adminCompletionQueueHead);
	EsPrint("\t\tAdmin completion queue phase: %d.\n", adminCompletionQueuePhase);
	EsPrint("\t\tAdmin submission queue: %x.\n", adminSubmissionQueue);
	EsPrint("\t\tAdmin submission queue tail: %x.\n", adminSubmissionQueueTail);
	EsPrint("\t\tIO completion queue: %x.\n", ioCompletionQueue);
	EsPrint("\t\tIO completion queue head: %x.\n", ioCompletionQueueHead);
	EsPrint("\t\tIO completion queue phase: %d.\n", ioCompletionQueuePhase);
	EsPrint("\t\tIO submission queue: %x.\n", ioSubmissionQueue);
	EsPrint("\t\tIO submission queue tail: %x.\n", ioSubmissionQueueTail);
	EsPrint("\t\tIO submission queue head: %x.\n", ioSubmissionQueueHead);
	EsPrint("\t\tIO submission queue non full: %d.\n", ioSubmissionQueueNonFull.state);
	EsPrint("\t\tPRP list virtual: %x.\n", prpListVirtual);

	EsPrint("\t--- Outstanding commands ---\n");

	for (uintptr_t i = ioSubmissionQueueHead; i != ioSubmissionQueueTail; i = (i + 1) % IO_QUEUE_ENTRY_COUNT) {
		EsPrint("\t\t(%d) %x, %x, %x, %x, %x, %x, %x, %x.\n", i, 
				((uint64_t *) ioSubmissionQueue)[i * 8 + 0], ((uint64_t *) ioSubmissionQueue)[i * 8 + 1],
				((uint64_t *) ioSubmissionQueue)[i * 8 + 2], ((uint64_t *) ioSubmissionQueue)[i * 8 + 3],
				((uint64_t *) ioSubmissionQueue)[i * 8 + 4], ((uint64_t *) ioSubmissionQueue)[i * 8 + 5],
				((uint64_t *) ioSubmissionQueue)[i * 8 + 6], ((uint64_t *) ioSubmissionQueue)[i * 8 + 7]);
	}
}

bool NVMeController::IssueAdminCommand(const void *command, uint32_t *result) {
	EsMemoryCopy(adminSubmissionQueue + adminSubmissionQueueTail * SUBMISSION_QUEUE_ENTRY_BYTES, command, SUBMISSION_QUEUE_ENTRY_BYTES);
	adminSubmissionQueueTail = (adminSubmissionQueueTail + 1) % ADMIN_QUEUE_ENTRY_COUNT;
	KEventReset(&adminCompletionQueueReceived);
	__sync_synchronize();
	WR_REGISTER_SQTDBL(0, adminSubmissionQueueTail);

	if (!KEventWait(&adminCompletionQueueReceived, GENERAL_TIMEOUT)) {
		// TODO Timeout. Now what?
		KernelLog(LOG_ERROR, "NVMe", "admin command timeout", "Admin command timeout when sending command %x.\n", command);
		return false;
	}

	if (adminCompletionQueueLastStatus) {
		bool doNotRetry = adminCompletionQueueLastStatus & 0x8000;
		bool more = adminCompletionQueueLastStatus & 0x4000;
		uint8_t commandRetryDelay = (adminCompletionQueueLastStatus >> 12) & 0x03;
		uint8_t statusCodeType = (adminCompletionQueueLastStatus >> 9) & 0x07;
		uint8_t statusCode = (adminCompletionQueueLastStatus >> 1) & 0xFF;

		KernelLog(LOG_ERROR, "NVMe", "admin command failed", "Admin command failed - '%z': %z%zretry delay - %d, type - %d, code - %d.\n", 
				GetErrorMessage(statusCodeType, statusCode), 
				doNotRetry ? "do not retry, " : "", more ? "more info in log page, " : "", commandRetryDelay, statusCodeType, statusCode);

		return false;
	}

	if (result) *result = adminCompletionQueueLastResult;
	return true;
}

bool NVMeController::Access(struct NVMeDrive *drive, uint64_t offsetBytes, size_t countBytes, int operation, 
		KDMABuffer *buffer, uint64_t, KWorkGroup *dispatchGroup) {
	// if (operation == K_ACCESS_WRITE) KernelPanic("NVMeController::Access - Attempted write.\n");

	// Build the PRPs.

	KDMASegment segment1 = KDMABufferNextSegment(buffer);
	uint64_t prp1 = segment1.physicalAddress, prp2 = 0;

	if (!segment1.isLast) {
		KDMASegment segment2 = KDMABufferNextSegment(buffer, true /* peek */);
		if (segment2.isLast) prp2 = segment2.physicalAddress;
	}

	retry:;
	KSpinlockAcquire(&ioQueueSpinlock);

	// Is there space in the submission queue?

	uintptr_t newTail = (ioSubmissionQueueTail + 1) % IO_QUEUE_ENTRY_COUNT;
	bool submissionQueueFull = newTail == ioSubmissionQueueHead;

	if (!submissionQueueFull) {
		KernelLog(LOG_VERBOSE, "NVMe", "start access", "Start access of %d, offset %D, count %D, using slot %d.\n", 
				drive->nsid, offsetBytes, countBytes, ioSubmissionQueueTail);

		uint64_t offsetSector = offsetBytes / drive->sectorSize;
		uint64_t countSectors = countBytes / drive->sectorSize;

		// Build the PRP list.

		if (!prp2) {
			prp2 = prpListPages[ioSubmissionQueueTail];
			MMArchRemap(MMGetKernelSpace(), prpListVirtual, prp2);
			uintptr_t index = 0;

			while (!KDMABufferIsComplete(buffer)) {
				if (index == K_PAGE_SIZE / sizeof(uint64_t)) {
					KernelPanic("NVMeController::Access - Out of bounds in PRP list.\n");
				}

				prpListVirtual[index++] = KDMABufferNextSegment(buffer).physicalAddress;
			}
		}

		// Create the command.

		uint32_t *command = (uint32_t *) (ioSubmissionQueue + ioSubmissionQueueTail * SUBMISSION_QUEUE_ENTRY_BYTES);
		command[0] = (ioSubmissionQueueTail << 16) /* command identifier */ | (operation == K_ACCESS_WRITE ? 0x01 : 0x02) /* opcode */;
		command[1] = drive->nsid;
		command[2] = command[3] = command[4] = command[5] = 0;
		command[6] = prp1 & 0xFFFFFFFF;
		command[7] = (prp1 >> 32) & 0xFFFFFFFF;
		command[8] = prp2 & 0xFFFFFFFF;
		command[9] = (prp2 >> 32) & 0xFFFFFFFF;
		command[10] = offsetSector & 0xFFFFFFFF;
		command[11] = (offsetSector >> 32) & 0xFFFFFFFF;
		command[12] = (countSectors - 1) & 0xFFFF;
		command[13] = command[14] = command[15] = 0;

		// Store the dispatch group, and update the queue tail.

		dispatchGroups[ioSubmissionQueueTail] = dispatchGroup;
		ioSubmissionQueueTail = newTail;
		__sync_synchronize();
		WR_REGISTER_SQTDBL(1, newTail);
	} else {
		KEventReset(&ioSubmissionQueueNonFull);
	}

	KSpinlockRelease(&ioQueueSpinlock);

	if (submissionQueueFull) {
		// Wait for the controller to consume an entry in the submission queue.

		KEventWait(&ioSubmissionQueueNonFull);
		goto retry;
	}

	return true;
}

bool NVMeController::HandleIRQ() {
	bool fromAdmin = false, fromIO = false;

	// Check the phase bit of the completion queue head entry.

	if (adminCompletionQueue && (adminCompletionQueue[adminCompletionQueueHead * COMPLETION_QUEUE_ENTRY_BYTES + 14] & (1 << 0)) != adminCompletionQueuePhase) {
		fromAdmin = true;

		adminCompletionQueueLastResult = *(uint32_t *) (adminCompletionQueue + adminCompletionQueueHead * COMPLETION_QUEUE_ENTRY_BYTES + 0);
		adminCompletionQueueLastStatus = *(uint16_t *) (adminCompletionQueue + adminCompletionQueueHead * COMPLETION_QUEUE_ENTRY_BYTES + 14) & 0xFFFE;

		// Advance the queue head.

		adminCompletionQueueHead++;

		if (adminCompletionQueueHead == ADMIN_QUEUE_ENTRY_COUNT) {
			adminCompletionQueuePhase = !adminCompletionQueuePhase;
			adminCompletionQueueHead = 0;
		}

		WR_REGISTER_CQHDBL(0, adminCompletionQueueHead);

		// Signal the event.

		KEventSet(&adminCompletionQueueReceived);
	}

	// Check the phase bit of the IO completion queue head entry.

	while (ioCompletionQueue && (ioCompletionQueue[ioCompletionQueueHead * COMPLETION_QUEUE_ENTRY_BYTES + 14] & (1 << 0)) != ioCompletionQueuePhase) {
		fromIO = true;

		uint16_t index = *(uint16_t *) (ioCompletionQueue + ioCompletionQueueHead * COMPLETION_QUEUE_ENTRY_BYTES + 12);
		uint16_t status = *(uint16_t *) (ioCompletionQueue + ioCompletionQueueHead * COMPLETION_QUEUE_ENTRY_BYTES + 14) & 0xFFFE;

		KernelLog(LOG_VERBOSE, "NVMe", "end access", "End access of slot %d.\n", index);

		if (index >= IO_QUEUE_ENTRY_COUNT) {
			KernelLog(LOG_ERROR, "NVMe", "invalid completion entry", "Completion entry reported invalid command index of %d.\n", 
					index);
		} else {
			KWorkGroup *dispatchGroup = dispatchGroups[index];

			if (status) {
				uint8_t statusCodeType = (status >> 9) & 0x07, statusCode = (status >> 1) & 0xFF;
				KernelLog(LOG_ERROR, "NVMe", "command failed", "Command failed with status %X/%X: %z.\n", 
						statusCodeType, statusCode, GetErrorMessage(statusCodeType, statusCode));
				dispatchGroup->End(false /* failed */);
			} else {
				dispatchGroup->End(true /* success */);
			}

			dispatchGroups[index] = nullptr;
		}

		// Indicate the submission queue entry was consumed.

		__sync_synchronize();
		ioSubmissionQueueHead = *(uint16_t *) (ioCompletionQueue + ioCompletionQueueHead * COMPLETION_QUEUE_ENTRY_BYTES + 8);
		KEventSet(&ioSubmissionQueueNonFull, false, true);

		// Advance the queue head.

		ioCompletionQueueHead++;

		if (ioCompletionQueueHead == IO_QUEUE_ENTRY_COUNT) {
			ioCompletionQueuePhase = !ioCompletionQueuePhase;
			ioCompletionQueueHead = 0;
		}

		WR_REGISTER_CQHDBL(1, ioCompletionQueueHead); 
	}

	return fromAdmin || fromIO;
}

void NVMeController::Initialise() {
	capabilities = RD_REGISTER_CAP();
	version = RD_REGISTER_VS();

	KernelLog(LOG_INFO, "NVMe", "initial register dump", 
			"Registers at initialisation: capabilities - %x; version - %x, configuration - %x, status - %x, admin queue attributes - %x. Mapped at %x.\n",
			capabilities, version, RD_REGISTER_CC(), RD_REGISTER_CSTS(), RD_REGISTER_AQA());

	// Check the version is acceptable.

	if ((version >> 16) < 1) {
		KernelLog(LOG_ERROR, "NVMe", "unsupported version", "Controller reports major version 0, which is not supported.\n");
		return;
	}

	if ((version >> 16) == 1 && ((version >> 8) & 0xFF) < 1) {
		KernelLog(LOG_ERROR, "NVMe", "unsupported version", "Controller reports version before 1.1, which is not supported.\n");
		return;
	}

	// Check the capabilities are acceptable.

	if ((capabilities & 0xFFFF) == 0) {
		KernelLog(LOG_ERROR, "NVMe", "unsupported capabilities", "Invalid CAP.MQES value, expected at least 1.\n");
		return;
	}

	if (~capabilities & (1UL << 37)) {
		KernelLog(LOG_ERROR, "NVMe", "unsupported capabilities", "Controller does not support NVMe command set.\n");
		return;
	}

	if (((capabilities >> 48) & 0xF) > K_PAGE_BITS - 12) {
		KernelLog(LOG_ERROR, "NVMe", "unsupported capabilities", "Controller requires a minimum page size greater than the host uses.\n");
		return;
	}

	if (((capabilities >> 52) & 0xF) < K_PAGE_BITS - 12) {
		KernelLog(LOG_ERROR, "NVMe", "unsupported capabilities", "Controller requires a maximum page size less than the host uses.\n");
		return;
	}

	doorbellStride         = 4 << ((capabilities >> 32) & 0xF);
	readyTransitionTimeout = ((capabilities >> 24) & 0xFF) * 500;

	uint32_t previousConfiguration = RD_REGISTER_CC();

	// Reset the controller.

	if (previousConfiguration & (1 << 0)) {
		KTimeout timeout(readyTransitionTimeout);
		while ((~RD_REGISTER_CSTS() & (1 << 0)) && !timeout.Hit());
		if (timeout.Hit()) { KernelLog(LOG_ERROR, "NVMe", "reset timeout", "Timeout during reset sequence (1).\n"); return; }

		WR_REGISTER_CC(RD_REGISTER_CC() & ~(1 << 0));
	}

	{
		KTimeout timeout(readyTransitionTimeout);
		while ((RD_REGISTER_CSTS() & (1 << 0)) && !timeout.Hit());
		if (timeout.Hit()) { KernelLog(LOG_ERROR, "NVMe", "reset timeout", "Timeout during reset sequence (2).\n"); return; }
	}

	// Configure the controller to use the NVMe command set, the host page size, the IO queue entry size, and round robin arbitration.

	WR_REGISTER_CC((RD_REGISTER_CC() & (0xFF00000F)) | (0x00460000) | ((K_PAGE_BITS - 12) << 7));

	// Configure the admin queues to use our desired entry count, and allocate memory for them.

	WR_REGISTER_AQA((RD_REGISTER_AQA() & 0xF000F000) | ((ADMIN_QUEUE_ENTRY_COUNT - 1) << 16) | (ADMIN_QUEUE_ENTRY_COUNT - 1));

	uint64_t adminSubmissionQueueBytes = ADMIN_QUEUE_ENTRY_COUNT * SUBMISSION_QUEUE_ENTRY_BYTES;
	uint64_t adminCompletionQueueBytes = ADMIN_QUEUE_ENTRY_COUNT * COMPLETION_QUEUE_ENTRY_BYTES;
	uint64_t adminQueuePages = (adminSubmissionQueueBytes + K_PAGE_SIZE - 1) / K_PAGE_SIZE + (adminCompletionQueueBytes + K_PAGE_SIZE - 1) / K_PAGE_SIZE;
	uintptr_t adminQueuePhysicalAddress = MMPhysicalAllocate(MM_PHYSICAL_ALLOCATE_CAN_FAIL | MM_PHYSICAL_ALLOCATE_COMMIT_NOW | MM_PHYSICAL_ALLOCATE_ZEROED, 
			adminQueuePages, (4096 + K_PAGE_SIZE - 1) / K_PAGE_SIZE);

	if (!adminQueuePhysicalAddress) {
		KernelLog(LOG_ERROR, "NVMe", "allocation failure", "Could not allocate %d pages of contiguous physical memory for admin queues.\n", adminQueuePages);
		return;
	}

	uintptr_t adminSubmissionQueuePhysicalAddress = adminQueuePhysicalAddress;
	uintptr_t adminCompletionQueuePhysicalAddress = adminQueuePhysicalAddress + ((adminSubmissionQueueBytes + K_PAGE_SIZE - 1) & ~(K_PAGE_SIZE - 1));

	WR_REGISTER_ASQ(adminSubmissionQueuePhysicalAddress);
	WR_REGISTER_ACQ(adminCompletionQueuePhysicalAddress);

	adminSubmissionQueue = (uint8_t *) MMMapPhysical(MMGetKernelSpace(), adminSubmissionQueuePhysicalAddress, adminSubmissionQueueBytes, ES_FLAGS_DEFAULT);
	adminCompletionQueue = (uint8_t *) MMMapPhysical(MMGetKernelSpace(), adminCompletionQueuePhysicalAddress, adminCompletionQueueBytes, ES_FLAGS_DEFAULT);

	if (!adminSubmissionQueue || !adminCompletionQueue) {
		KernelLog(LOG_ERROR, "NVMe", "allocation failure", "Could not map admin queues.\n");
		return;
	}

	KernelLog(LOG_INFO, "NVMe", "admin queue configuration", "Configured admin queues to use physical addresses %x and %x with %d entries each.\n", 
			adminSubmissionQueuePhysicalAddress, adminCompletionQueuePhysicalAddress, ADMIN_QUEUE_ENTRY_COUNT);

	// Start the controller.

	WR_REGISTER_CC(RD_REGISTER_CC() | (1 << 0));

	{
		KTimeout timeout(readyTransitionTimeout);

		while (!timeout.Hit()) {
			uint32_t status = RD_REGISTER_CSTS();

			if (status & (1 << 1)) {
				KernelLog(LOG_ERROR, "NVMe", "fatal error", "Fatal error while enabling controller.\n"); 
				return;
			} else if (status & (1 << 0)) {
				break;
			}
		}

		if (timeout.Hit()) { KernelLog(LOG_ERROR, "NVMe", "reset timeout", "Timeout during reset sequence (3).\n"); return; }
	}

	// Enable IRQs for the admin queue, and register our interrupt handler.

	if (!pci->EnableSingleInterrupt([] (uintptr_t, void *context) { return ((NVMeController *) context)->HandleIRQ(); }, this, "NVMe")) {
		KernelLog(LOG_ERROR, "NVMe", "IRQ registration failure", "Could not register IRQ %d.\n", pci->interruptLine);
		return;
	}

	WR_REGISTER_INTMC(1 << 0);

	// Identify controller. 

	uintptr_t identifyDataPhysicalAddress = MMPhysicalAllocate(MM_PHYSICAL_ALLOCATE_CAN_FAIL | MM_PHYSICAL_ALLOCATE_COMMIT_NOW | MM_PHYSICAL_ALLOCATE_ZEROED, 
			(4096 * 2 + K_PAGE_SIZE - 1) / K_PAGE_SIZE);

	if (!identifyDataPhysicalAddress) {
		KernelLog(LOG_ERROR, "NVMe", "allocation failure", "Could not allocate physical memory for receiving identify data.\n");
		return;
	}

	uint8_t *identifyData = (uint8_t *) MMMapPhysical(MMGetKernelSpace(), identifyDataPhysicalAddress, 4096 * 2, ES_FLAGS_DEFAULT);

	if (!identifyData) {
		KernelLog(LOG_ERROR, "NVMe", "allocation failure", "Could not map memory for receiving identify data.\n");
		return;
	}
	
	{
		uint32_t command[16] = {};
		command[0] = 0x06; // Identify opcode.
		command[6] = identifyDataPhysicalAddress & 0xFFFFFFFF;
		command[7] = (identifyDataPhysicalAddress >> 32) & 0xFFFFFFFF;
		command[10] = 0x01; // Identify controller.

		if (!IssueAdminCommand(command, nullptr)) {
			KernelLog(LOG_ERROR, "NVMe", "identify controller failure", "The identify controller admin command failed.\n");
			return;
		}

		maximumDataTransferBytes = identifyData[77] ? (1 << (12 + identifyData[77] + (((capabilities >> 48) & 0xF)))) : 0;
		rtd3EntryLatencyUs = *(uint32_t *) (identifyData + 88);
		maximumOutstandingCommands = *(uint16_t *) (identifyData + 514);

		if (rtd3EntryLatencyUs > 250 * 1000) {
			rtd3EntryLatencyUs = 250 * 1000; // Maximum shutdown delay: 250ms.
		}

		if (identifyData[111] > 0x01) {
			KernelLog(LOG_ERROR, "NVMe", "unsupported controller type", "Controller type %X is not supported. Only IO controllers are currently supported.\n", 
					identifyData[111]);
			return;
		}

		KernelLog(LOG_INFO, "NVMe", "controller identify data", "Controller identify reported the following information: "
				"serial number - '%s', model number - '%s', firmware revision - '%s', "
				"maximum data transfer - %D, RTD3 entry latency - %dus, maximum outstanding commands - %d.\n",
				20, identifyData + 4, 40, identifyData + 24, 8, identifyData + 64,
				maximumDataTransferBytes, rtd3EntryLatencyUs, maximumOutstandingCommands);

		if (maximumDataTransferBytes == 0 || maximumDataTransferBytes >= 2097152) {
			maximumDataTransferBytes = 2097152;
		}
	}

	// Reset the software progress marker.

	{
		// TODO How to check if this feature is supported?
		uint32_t command[16] = {};
		command[0] = 0x09; // Set features opcode.
		command[10] = 0x80; // Software progress marker feature.
		command[11] = 0x00; // Reset to 0.
		IssueAdminCommand(command, nullptr); // Ignore errors.
	}

	// Create IO completion queue.
	
	{
		uint64_t bytes = IO_QUEUE_ENTRY_COUNT * COMPLETION_QUEUE_ENTRY_BYTES;
		uint64_t pages = (bytes + K_PAGE_SIZE - 1) / K_PAGE_SIZE;
		uintptr_t physicalAddress = MMPhysicalAllocate(MM_PHYSICAL_ALLOCATE_CAN_FAIL | MM_PHYSICAL_ALLOCATE_COMMIT_NOW | MM_PHYSICAL_ALLOCATE_ZEROED, pages);

		if (!physicalAddress) {
			KernelLog(LOG_ERROR, "NVMe", "allocation failure", "Could not allocate IO completion queue memory.\n");
			return;
		}

		ioCompletionQueue = (uint8_t *) MMMapPhysical(MMGetKernelSpace(), physicalAddress, bytes, ES_FLAGS_DEFAULT);

		if (!ioCompletionQueue) {
			KernelLog(LOG_ERROR, "NVMe", "allocation failure", "Could not map IO completion queue memory.\n");
			return;
		}

		uint32_t command[16] = {};
		command[0] = 0x05; // Create IO completion queue opcode.
		command[6] = physicalAddress & 0xFFFFFFFF;
		command[7] = (physicalAddress >> 32) & 0xFFFFFFFF;
		command[10] = 1 /* identifier */ | ((IO_QUEUE_ENTRY_COUNT - 1) << 16);
		command[11] = (1 << 0) /* physically contiguous */ | (1 << 1) /* interrupts enabled */;

		if (!IssueAdminCommand(command, nullptr)) {
			KernelLog(LOG_ERROR, "NVMe", "create queue failure", "Could not create the IO completion queue.\n");
			return;
		}
	}

	// Create IO submission queue.
	
	{
		uint64_t bytes = IO_QUEUE_ENTRY_COUNT * SUBMISSION_QUEUE_ENTRY_BYTES;
		uint64_t pages = (bytes + K_PAGE_SIZE - 1) / K_PAGE_SIZE;
		uintptr_t physicalAddress = MMPhysicalAllocate(MM_PHYSICAL_ALLOCATE_CAN_FAIL | MM_PHYSICAL_ALLOCATE_COMMIT_NOW | MM_PHYSICAL_ALLOCATE_ZEROED, pages);

		if (!physicalAddress) {
			KernelLog(LOG_ERROR, "NVMe", "allocation failure", "Could not allocate IO submission queue memory.\n");
			return;
		}

		ioSubmissionQueue = (uint8_t *) MMMapPhysical(MMGetKernelSpace(), physicalAddress, bytes, ES_FLAGS_DEFAULT);

		if (!ioSubmissionQueue) {
			KernelLog(LOG_ERROR, "NVMe", "allocation failure", "Could not map IO submission queue memory.\n");
			return;
		}

		uint32_t command[16] = {};
		command[0] = 0x01; // Create IO submission queue opcode.
		command[6] = physicalAddress & 0xFFFFFFFF;
		command[7] = (physicalAddress >> 32) & 0xFFFFFFFF;
		command[10] = 1 /* identifier */ | ((IO_QUEUE_ENTRY_COUNT - 1) << 16);
		command[11] = (1 << 0) /* physically contiguous */ | (1 << 16) /* completion queue identifier */;

		if (!IssueAdminCommand(command, nullptr)) {
			KernelLog(LOG_ERROR, "NVMe", "create queue failure", "Could not create the IO submission queue.\n");
			return;
		}
	}

	// Allocate physical memory for PRP lists.

	{
		for (uintptr_t i = 0; i < IO_QUEUE_ENTRY_COUNT; i++) {
			prpListPages[i] = MMPhysicalAllocate(MM_PHYSICAL_ALLOCATE_CAN_FAIL | MM_PHYSICAL_ALLOCATE_COMMIT_NOW, 1);

			if (!prpListPages[i]) {
				KernelLog(LOG_ERROR, "NVMe", "allocation failure", "Could not allocate physical memory for PRP lists.\n");
				return;
			}
		}

		prpListVirtual = (uint64_t *) MMMapPhysical(MMGetKernelSpace(), prpListPages[0], K_PAGE_SIZE, ES_FLAGS_DEFAULT);

		if (!prpListVirtual) {
			KernelLog(LOG_ERROR, "NVMe", "allocation failure", "Could not allocate virtual memory to modify PRP lists.\n");
			return;
		}
	}

	// Identify active namespace IDs.

	uint32_t nsid = 0;

	while (true) {
		uint32_t command[16] = {};
		command[0] = 0x06; // Identify opcode.
		command[1] = nsid; // List NSIDs greater than the last one we saw.
		command[6] = identifyDataPhysicalAddress & 0xFFFFFFFF;
		command[7] = (identifyDataPhysicalAddress >> 32) & 0xFFFFFFFF;
		command[10] = 0x02; // Identify active namespace IDs.

		if (!IssueAdminCommand(command, nullptr)) {
			KernelLog(LOG_ERROR, "NVMe", "identify controller failure", "The identify controller admin command failed.\n");
			return;
		}

		for (uintptr_t i = 0; i < 1024; i++) {
			nsid = ((uint32_t *) identifyData)[i];

			if (!nsid) {
				goto allNamespacesIdentified;
			}

			KernelLog(LOG_INFO, "NVMe", "active namespace ID", "Namespace ID %d is active.\n", nsid);

			// Identify the namespace.

			command[0] = 0x06; // Identify opcode.
			command[1] = nsid;
			command[6] = (identifyDataPhysicalAddress + 4096) & 0xFFFFFFFF;
			command[7] = ((identifyDataPhysicalAddress + 4096) >> 32) & 0xFFFFFFFF;
			command[10] = 0x00; // Identify namespace.

			if (!IssueAdminCommand(command, nullptr)) {
				KernelLog(LOG_ERROR, "NVMe", "identify namespace failure", "Could not identify namespace %d.\n", nsid);
				continue;
			}

			uint8_t formattedLBASize = identifyData[4096 + 26];
			uint32_t lbaFormat = *(uint32_t *) (identifyData + 4096 + 128 + 4 * (formattedLBASize & 0xF));
			
			if (lbaFormat & 0xFFFF) {
				KernelLog(LOG_ERROR, "NVMe", "metadata unsupported", "Namespace %d has %D of metadata per block, which is unsupported.\n", 
						nsid, lbaFormat & 0xFFFF);
				continue;
			}

			uint8_t sectorBytesExponent = (lbaFormat >> 16) & 0xFF;

			if (sectorBytesExponent < 9 || sectorBytesExponent > 16) {
				KernelLog(LOG_ERROR, "NVMe", "unsupported block size", "Namespace %d uses blocks of size 2^%d bytes, which is unsupported.\n", 
						nsid, sectorBytesExponent);
				continue;
			}

			uint64_t sectorBytes = 1 << sectorBytesExponent;
			uint64_t capacity = *(uint64_t *) (identifyData + 4096 + 8) * sectorBytes;

			bool readOnly = identifyData[4096 + 99] & (1 << 0);

			KernelLog(LOG_INFO, "NVMe", "namespace identified", "Identifier namespace %d with sectors of size %D, and a capacity of %D.%z\n",
					nsid, sectorBytes, capacity, readOnly ? " The namespace is read-only." : "");

			NVMeDrive *device = (NVMeDrive *) KDeviceCreate("NVMe namespace", this, sizeof(NVMeDrive));

			if (!device) {
				KernelLog(LOG_ERROR, "NVMe", "allocation failure", "Could not create device for namespace %d.\n", nsid);
				goto allNamespacesIdentified;
			}

			device->controller = this;
			device->nsid = nsid;

			device->sectorSize = sectorBytes;
			device->sectorCount = capacity / sectorBytes;
			device->maxAccessSectorCount = maximumDataTransferBytes / sectorBytes;
			device->readOnly = readOnly;
			device->driveType = ES_DRIVE_TYPE_SSD;

			device->access = [] (KBlockDeviceAccessRequest request) {
				NVMeDrive *drive = (NVMeDrive *) request.device;

				request.dispatchGroup->Start();

				if (!drive->controller->Access(drive, request.offset, request.count, request.operation, 
							request.buffer, request.flags, request.dispatchGroup)) {
					request.dispatchGroup->End(false);
				}
			};

			FSRegisterBlockDevice(device);
		}
	}

	allNamespacesIdentified:;
}

void NVMeController::Shutdown() {
	// Delete the IO queues.

	uint32_t command[16] = {};
	command[0] = 0x00; // Delete IO submission queue opcode.
	command[10] = 1 /* identifier */;
	IssueAdminCommand(command, nullptr);
	command[0] = 0x04; // Delete IO completion queue opcode.
	IssueAdminCommand(command, nullptr);

	// Inform the controller of shutdown.

	WR_REGISTER_CC(RD_REGISTER_CC() | (1 << 14));

	// Wait for shutdown processing to complete.

	KTimeout timeout(rtd3EntryLatencyUs / 1000 + 1);
	while (!timeout.Hit() && (RD_REGISTER_CSTS() & 12) != 8);
}

static void DeviceAttach(KDevice *_parent) {
	KPCIDevice *parent = (KPCIDevice *) _parent;

	NVMeController *device = (NVMeController *) KDeviceCreate("NVMe controller", parent, sizeof(NVMeController));
	if (!device) return;
	device->pci = parent;

	device->shutdown = [] (KDevice *device) {
		((NVMeController *) device)->Shutdown();
	};

	device->dumpState = [] (KDevice *device) {
		((NVMeController *) device)->DumpState();
	};

	KernelLog(LOG_INFO, "NVMe", "found controller", "Found NVMe controller at PCI function %d/%d/%d.\n", parent->bus, parent->slot, parent->function);

	// Enable PCI features.
	parent->EnableFeatures(K_PCI_FEATURE_INTERRUPTS 
			| K_PCI_FEATURE_BUSMASTERING_DMA
			| K_PCI_FEATURE_MEMORY_SPACE_ACCESS
			| K_PCI_FEATURE_BAR_0);

	// Initialise the controller.
	device->Initialise();
};

KDriver driverNVMe = {
	.attach = DeviceAttach,
};
