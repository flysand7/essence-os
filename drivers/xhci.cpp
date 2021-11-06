#include <module.h>

// TODO Device attach and detach.
// TODO Babble error recovery.

#define SPEED_LOW (1)
#define SPEED_FULL (2)
#define SPEED_HIGH (3)
#define SPEED_SUPER (4)

#define COMMAND_RING_ENTRIES (255)
#define EVENT_RING_ENTRIES (252)
#define TRANSFER_RING_ENTRIES (255)
#define INPUT_CONTEXT_BYTES ((contextSize64 ? 64 : 32) * 33)
#define OUTPUT_CONTEXT_BYTES ((contextSize64 ? 64 : 32) * 32)
#define CONTEXT_INDEX(i) ((i) * (contextSize64 ? 16 : 8))

// Capability registers:

#define RD_REGISTER_CAPLENGTH()         pci-> ReadBAR32(0, 0x00)                                                     // Capability register length and interface version number.
#define WR_REGISTER_CAPLENGTH(x)        pci->WriteBAR32(0, 0x00, x)                                                      
#define RD_REGISTER_HCSPARAMS1()        pci-> ReadBAR32(0, 0x04)                                                     // Structural parameters 1.
#define WR_REGISTER_HCSPARAMS1(x)       pci->WriteBAR32(0, 0x04, x)                                                      
#define RD_REGISTER_HCSPARAMS2()        pci-> ReadBAR32(0, 0x08)                                                     // Structural parameters 2.
#define WR_REGISTER_HCSPARAMS2(x)       pci->WriteBAR32(0, 0x08, x)                                                      
#define RD_REGISTER_HCSPARAMS3()        pci-> ReadBAR32(0, 0x0C)                                                     // Structural parameters 3.
#define WR_REGISTER_HCSPARAMS3(x)       pci->WriteBAR32(0, 0x0C, x)                                                      
#define RD_REGISTER_HCCPARAMS1()        pci-> ReadBAR32(0, 0x10)                                                     // Capability parameters 1.
#define WR_REGISTER_HCCPARAMS1(x)       pci->WriteBAR32(0, 0x10, x)                                                      
#define RD_REGISTER_DBOFF()             pci-> ReadBAR32(0, 0x14)                                                     // Doorbell offset.
#define WR_REGISTER_DBOFF(x)            pci->WriteBAR32(0, 0x14, x)                                                      
#define RD_REGISTER_RTSOFF()            pci-> ReadBAR32(0, 0x18)                                                     // Runtime register space offset.
#define WR_REGISTER_RTSOFF(x)           pci->WriteBAR32(0, 0x18, x)                                                      
#define RD_REGISTER_HCCPARAMS2()        pci-> ReadBAR32(0, 0x1C)                                                     // Capability parameters 2.
#define WR_REGISTER_HCCPARAMS2(x)       pci->WriteBAR32(0, 0x1C, x)                                                      

// Host controller operational registers:

#define RD_REGISTER_USBCMD()            pci-> ReadBAR32(0, operationalRegistersOffset + 0x00)                        // USB command.
#define WR_REGISTER_USBCMD(x)           pci->WriteBAR32(0, operationalRegistersOffset + 0x00, x)                                                      
#define RD_REGISTER_USBSTS()            pci-> ReadBAR32(0, operationalRegistersOffset + 0x04)                        // USB status.
#define WR_REGISTER_USBSTS(x)           pci->WriteBAR32(0, operationalRegistersOffset + 0x04, x)                                                      
#define RD_REGISTER_PAGESIZE()          pci-> ReadBAR32(0, operationalRegistersOffset + 0x08)                        // Page size.
#define WR_REGISTER_PAGESIZE(x)         pci->WriteBAR32(0, operationalRegistersOffset + 0x08, x)                                                      
#define RD_REGISTER_DNCTRL()            pci-> ReadBAR32(0, operationalRegistersOffset + 0x14)                        // Device notification control.
#define WR_REGISTER_DNCTRL(x)           pci->WriteBAR32(0, operationalRegistersOffset + 0x14, x)                                                      
#define RD_REGISTER_CRCR()              pci-> ReadBAR64(0, operationalRegistersOffset + 0x18)                        // Command ring control.
#define WR_REGISTER_CRCR(x)             pci->WriteBAR64(0, operationalRegistersOffset + 0x18, x)                                                      
#define RD_REGISTER_DCBAAP()            pci-> ReadBAR64(0, operationalRegistersOffset + 0x30)                        // Device context base address array pointer.
#define WR_REGISTER_DCBAAP(x)           pci->WriteBAR64(0, operationalRegistersOffset + 0x30, x)                                                      
#define RD_REGISTER_CONFIG()            pci-> ReadBAR32(0, operationalRegistersOffset + 0x38)                        // Configure.
#define WR_REGISTER_CONFIG(x)           pci->WriteBAR32(0, operationalRegistersOffset + 0x38, x)                                                      

// Port register sets:

#define RD_REGISTER_PORTSC(n)           pci-> ReadBAR32(0, operationalRegistersOffset + 0x400 + (n) * 0x10)          // Port status and control.
#define WR_REGISTER_PORTSC(n, x)        pci->WriteBAR32(0, operationalRegistersOffset + 0x400 + (n) * 0x10, x)                                        
#define RD_REGISTER_PORTPMSC(n)         pci-> ReadBAR32(0, operationalRegistersOffset + 0x404 + (n) * 0x10)          // Port port management status and control.
#define WR_REGISTER_PORTPMSC(n, x)      pci->WriteBAR32(0, operationalRegistersOffset + 0x404 + (n) * 0x10, x)                                        
#define RD_REGISTER_PORTLI(n)           pci-> ReadBAR32(0, operationalRegistersOffset + 0x408 + (n) * 0x10)          // Port link info.
#define WR_REGISTER_PORTLI(n, x)        pci->WriteBAR32(0, operationalRegistersOffset + 0x408 + (n) * 0x10, x)                                        
#define RD_REGISTER_PORTHLPMC(n)        pci-> ReadBAR32(0, operationalRegistersOffset + 0x40C + (n) * 0x10)          // Port hardware LPM control.
#define WR_REGISTER_PORTHLPMC(n, x)     pci->WriteBAR32(0, operationalRegistersOffset + 0x40C + (n) * 0x10, x)                                        

// Host controller runtime registers:

#define RD_REGISTER_MFINDEX()           pci-> ReadBAR32(0, runtimeRegistersOffset + 0x00)                            // Microframe index.
#define WR_REGISTER_MFINDEX(x)          pci->WriteBAR32(0, runtimeRegistersOffset + 0x00, x)                                                      

// Interrupter register sets:

#define RD_REGISTER_IMAN(n)             pci-> ReadBAR32(0, runtimeRegistersOffset + 0x20 + (n) * 0x20)               // Interrupter management.
#define WR_REGISTER_IMAN(n, x)          pci->WriteBAR32(0, runtimeRegistersOffset + 0x20 + (n) * 0x20, x)                                        
#define RD_REGISTER_IMOD(n)             pci-> ReadBAR32(0, runtimeRegistersOffset + 0x24 + (n) * 0x20)               // Interrupter moderation.
#define WR_REGISTER_IMOD(n, x)          pci->WriteBAR32(0, runtimeRegistersOffset + 0x24 + (n) * 0x20, x)                                        
#define RD_REGISTER_ERSTSZ(n)           pci-> ReadBAR32(0, runtimeRegistersOffset + 0x28 + (n) * 0x20)               // Event ring segment table size.
#define WR_REGISTER_ERSTSZ(n, x)        pci->WriteBAR32(0, runtimeRegistersOffset + 0x28 + (n) * 0x20, x)                                        
#define RD_REGISTER_ERSTBA(n)           pci-> ReadBAR64(0, runtimeRegistersOffset + 0x30 + (n) * 0x20)               // Event ring segment table base address.
#define WR_REGISTER_ERSTBA(n, x)        pci->WriteBAR64(0, runtimeRegistersOffset + 0x30 + (n) * 0x20, x)                                        
#define RD_REGISTER_ERDP(n)             pci-> ReadBAR64(0, runtimeRegistersOffset + 0x38 + (n) * 0x20)               // Event ring dequeue pointer.
#define WR_REGISTER_ERDP(n, x)          pci->WriteBAR64(0, runtimeRegistersOffset + 0x38 + (n) * 0x20, x)                                        

// Doorbell registers:

#define RD_REGISTER_DOORBELL(n)         pci-> ReadBAR32(0, doorbellsOffset + (n) * 0x04)                             // Doorbell.
#define WR_REGISTER_DOORBELL(n, x)      pci->WriteBAR32(0, doorbellsOffset + (n) * 0x04, x)                                        

struct XHCIDevice : KUSBDevice {
	uintptr_t port;
};

struct XHCIEndpoint {
	uint32_t *data;
	uintptr_t physical;

	uint32_t lastStatus;
	uint16_t maximumPacketSize;
	uint16_t index : 15, phase : 1;

	KUSBTransferCallback callback;
	EsGeneric context;
	KAsyncTask callbackAsyncTask;

	bool CreateTransferRing();

	// TODO Detecting when the transfer ring is full.
	void AdvanceTransferRingIndex();
};

struct XHCIPort {
	bool usb2, statusChangeEvent, enabled;
	uint8_t slotType, slotID, speed;
	// uintptr_t maximumPacketSize;

	XHCIEndpoint controlEndpoint;
	volatile uint32_t *controlTransferResult;
	volatile uintptr_t controlTransferLastTRBAddress;
	KEvent controlTransferCompleteEvent;

	XHCIEndpoint ioEndpoints[30];

	uint32_t *outputContext;
	uintptr_t outputContextPhysical;

	XHCIDevice *device;

	KMutex mutex;
};

struct XHCIController : KDevice {
	KPCIDevice *pci;

	uintptr_t operationalRegistersOffset,
		  extendedCapabilitiesOffset,
		  doorbellsOffset,
		  runtimeRegistersOffset;
	size_t maximumDeviceSlots, 
	       maximumInterrupters, 
	       maximumPorts,
	       maximumEventRingSegments;
	bool contextSize64;

	uint64_t *deviceContextBaseAddressArray;

	uint32_t *commandRing;
	uintptr_t commandRingIndex;
	bool commandRingPhase;
	volatile uint32_t *commandResult;
	KEvent commandCompleteEvent;

	uint32_t *eventRing;
	uintptr_t eventRingPhysical;
	uintptr_t eventRingIndex;
	bool eventRingPhase;

	uint32_t *inputContext;
	uintptr_t inputContextPhysical;

	KEvent portStatusChangeEvent;
	KSpinlock portResetSpinlock;

	XHCIPort *ports;

	void Initialise();
	void DumpState();
	bool HandleIRQ();

	void OnPortEnable(uintptr_t port);
	void OnPortDisable(uintptr_t port);

	bool RunCommand(uint32_t *dw);
	bool AddTransferDescriptors(uintptr_t buffer, size_t length, int operation, XHCIEndpoint *endpoint, 
			uint32_t trbType, bool interruptOnLast);

	bool ControlTransfer(uintptr_t port, uint8_t flags, uint8_t request, uint16_t value, uint16_t index, 
			void *buffer, uint16_t length, int operation, uint16_t *transferred, bool alreadyLocked);
	bool SelectConfigurationAndInterface(uintptr_t port, KUSBDevice *device);
	bool QueueInterruptTransfer(uintptr_t portIndex, uint8_t endpointAddress, KUSBTransferCallback callback, 
			void *buffer, size_t bufferBytes, EsGeneric context);
};

const char *commandCompletionCodes[] = {
	"Invalid",
	"Success",
	"Data buffer error",
	"Babble detected error",
	"USB transaction error",
	"TRB error",
	"Stall error",
	"Resource error",
	"Bandwidth error",
	"No slot available error",
	"Invalid stream type error",
	"Slot not enabled error",
	"Endpoint not enabled error",
	"Short packet",
	"Ring underrun",
	"Ring overrun",
	"VF event ring full error",
	"Parameter error",
	"Bandwidth overrun error",
	"Context state error",
	"No ping response error",
	"Event ring full error",
	"Incompatible device error",
	"Missed service error",
	"Command ring stopped",
	"Command aborted",
	"Stopped",
	"Stopped - length invalid",
	"Stopped - short packet",
	"Max exit latency too large error",
	"Reserved",
	"Isochronous buffer overrun",
	"Event lost error",
	"Undefined error",
	"Invalid stream ID error",
	"Secondary bandwidth error",
	"Split transcation error",
};

void XHCIController::DumpState() {
	EsPrint("xHCI controller state:\n");

	EsPrint("\t--- Registers ---\n");

	EsPrint("\t\tCapability register length: %x.\n", RD_REGISTER_CAPLENGTH());
	EsPrint("\t\tStructural parameters 1: %x.\n", RD_REGISTER_HCSPARAMS1());
	EsPrint("\t\tStructural parameters 2: %x.\n", RD_REGISTER_HCSPARAMS2());
	EsPrint("\t\tStructural parameters 3: %x.\n", RD_REGISTER_HCSPARAMS3());
	EsPrint("\t\tCapability parameters 1: %x.\n", RD_REGISTER_HCCPARAMS1());
	EsPrint("\t\tDoorbell offset: %x.\n", RD_REGISTER_DBOFF());
	EsPrint("\t\tRuntime register space offset: %x.\n", RD_REGISTER_RTSOFF());
	EsPrint("\t\tCapability parameters 2: %x.\n", RD_REGISTER_HCCPARAMS2());

	EsPrint("\t\tUSB command: %x.\n", RD_REGISTER_USBCMD());
	EsPrint("\t\tUSB status: %x.\n", RD_REGISTER_USBSTS());
	EsPrint("\t\tPage size: %x.\n", RD_REGISTER_PAGESIZE());
	EsPrint("\t\tDevice notification control: %x.\n", RD_REGISTER_DNCTRL());
	EsPrint("\t\tCommand ring control: %x.\n", RD_REGISTER_CRCR());
	EsPrint("\t\tDevice context base address array pointer (64-bit): %x.\n", RD_REGISTER_DCBAAP());
	EsPrint("\t\tConfigure: %x.\n", RD_REGISTER_CONFIG());

	for (uintptr_t i = 0; i < maximumPorts; i++) {
		EsPrint("\t\tPort %d:\n", i);
		EsPrint("\t\t\tPort status and control: %x.\n", RD_REGISTER_PORTSC(i));
		EsPrint("\t\t\tPort port management status and control: %x.\n", RD_REGISTER_PORTPMSC(i));
		EsPrint("\t\t\tPort link info: %x.\n", RD_REGISTER_PORTLI(i));
		EsPrint("\t\t\tPort hardware LPM control: %x.\n", RD_REGISTER_PORTHLPMC(i));
	}

	for (uintptr_t i = 0; i < maximumInterrupters; i++) {
		EsPrint("\t\tInterrupter %d:\n", i);
		EsPrint("\t\t\tInterrupter management: %x.\n", RD_REGISTER_IMAN(i));
		EsPrint("\t\t\tInterrupter moderation: %x.\n", RD_REGISTER_IMOD(i));
		EsPrint("\t\t\tEvent ring segment table size: %x.\n", RD_REGISTER_ERSTSZ(i));
		EsPrint("\t\t\tEvent ring segment table base address: %x.\n", RD_REGISTER_ERSTBA(i));
		EsPrint("\t\t\tEvent ring dequeue pointer: %x.\n", RD_REGISTER_ERDP(i));
	}
}

bool XHCIEndpoint::CreateTransferRing() {
	if (!MMPhysicalAllocateAndMap(16 * (TRANSFER_RING_ENTRIES + 1), 16, 0, true, 
				0, (uint8_t **) &data, &physical)) {
		KernelLog(LOG_ERROR, "xHCI", "allocation failure", "Could not allocate the transfer ring.\n");
		return false;
	}

	index = 0;
	phase = 1;

	data[TRANSFER_RING_ENTRIES * 4 + 0] = physical & 0xFFFFFFFF;
	data[TRANSFER_RING_ENTRIES * 4 + 1] = (physical >> 32) & 0xFFFFFFFF;
	data[TRANSFER_RING_ENTRIES * 4 + 3] = (1 << 1 /* toggle cycle */) | (6 /* link TRB */ << 10);

	return true;
}

void XHCIEndpoint::AdvanceTransferRingIndex() {
	if (phase) {
		data[index * 4 + 3] |= 1 << 0;
	}

	index++;

	if (index == TRANSFER_RING_ENTRIES) {
		// Toggle cycle bit on link TRB.
		data[TRANSFER_RING_ENTRIES * 4 + 3] ^= 1 << 0;
		index = 0;
		phase = phase ? 0 : 1;
	}
}

bool XHCIController::AddTransferDescriptors(uintptr_t buffer, size_t length, int operation, XHCIEndpoint *endpoint, uint32_t trbType, bool interruptOnLast) {
	if (this->flags & K_DEVICE_REMOVED) {
		return false;
	}

	uintptr_t position = 0;

	while (position != length) {
		uintptr_t count = K_PAGE_SIZE - ((buffer + position) & (K_PAGE_SIZE - 1));
		bool last = count >= length - position;
		if (last) count = length - position;
		uintptr_t physical = MMArchTranslateAddress(MMGetKernelSpace(), buffer + position);
		physical += (buffer + position) & (K_PAGE_SIZE - 1);
		size_t remainingPackets = (length - position + endpoint->maximumPacketSize - 1) / endpoint->maximumPacketSize;

		uint32_t *dw = endpoint->data + endpoint->index * 4;

		dw[0] = (physical >>  0) & 0xFFFFFFFF;
		dw[1] = (physical >> 32) & 0xFFFFFFFF;
		dw[2] = count | ((last ? 0 : remainingPackets < 31 ? remainingPackets : 31) << 17); 

		dw[3] = trbType << 10;
		if (operation == K_ACCESS_READ) dw[3] |= 1 << 16 /* input */;
		if (last && interruptOnLast) dw[3] |= 1 << 5 /* interrupt on completion */;
		if (!last) dw[3] |= 1 << 4 /* chain */;

		endpoint->AdvanceTransferRingIndex();
		position += count;
	}

	return true;
}

bool XHCIController::ControlTransfer(uintptr_t portIndex, uint8_t flags, uint8_t request, uint16_t value, uint16_t index, 
		void *buffer, uint16_t length, int operation, uint16_t *transferred, bool alreadyLocked) {
	XHCIPort *port = ports + portIndex;

	if (!alreadyLocked) KMutexAcquire(&port->mutex);
	EsDefer(if (!alreadyLocked) KMutexRelease(&port->mutex));
	if (alreadyLocked) KMutexAssertLocked(&port->mutex);

	if (this->flags & K_DEVICE_REMOVED) {
		return false;
	}

	KEventReset(&port->controlTransferCompleteEvent);

	uint32_t transferResult[4];
	port->controlTransferResult = transferResult;

	{
		uint32_t *dw = port->controlEndpoint.data + port->controlEndpoint.index * 4;
		dw[0] = (uint32_t) flags | ((uint32_t) request << 8) | ((uint32_t) value << 16);
		dw[1] = (uint32_t) index | ((uint32_t) length << 16);
		dw[2] = 8 /* transfer length */;
		dw[3] = (1 << 6 /* immediate data */) | (2 /* setup TRB */ << 10);

		if (length) {
			dw[3] |= (operation == K_ACCESS_READ ? 3 : 2) << 16;
		}

		port->controlEndpoint.AdvanceTransferRingIndex();
	}

	if (!AddTransferDescriptors((uintptr_t) buffer, length, operation, &port->controlEndpoint, 3 /* data TRB */, false)) {
		return false;
	}

	{
		uint32_t *dw = port->controlEndpoint.data + port->controlEndpoint.index * 4;
		port->controlTransferLastTRBAddress = port->controlEndpoint.physical + port->controlEndpoint.index * 16;
		dw[0] = dw[1] = dw[2] = 0;
		dw[3] = (1 << 5 /* interrupt on completion */) | (operation == K_ACCESS_WRITE ? (1 << 16) : 0) | (4 /* status TRB */ << 10);
		port->controlEndpoint.AdvanceTransferRingIndex();
	}

	WR_REGISTER_DOORBELL(port->slotID, 1 /* control endpoint */);

	if (!KEventWait(&port->controlTransferCompleteEvent, 1000)) {
		// Timeout.
		return false;
	}

	uint32_t status = transferResult[2];
	uint8_t completionCode = (status >> 24) & 0xFF;

	if (completionCode != 1) {
		if (completionCode < sizeof(commandCompletionCodes) / sizeof(commandCompletionCodes[0])) {
			KernelLog(LOG_ERROR, "xHCI", "failed control transfer", "Control transfer failed with completion code '%z'.\n", commandCompletionCodes[completionCode]);
		} else {
			KernelLog(LOG_ERROR, "xHCI", "failed control transfer", "Control transfer failed with unrecognised completion code %d.\n", completionCode);
		}

		if ((port->outputContext[CONTEXT_INDEX(1) + 0] & 7) == 2 /* halted */) {
			// Reset the endpoint.

			uint32_t dw[4] = {};
			dw[3] = (port->slotID << 24) | (1 /* control endpoint */ << 16) 
				| (14 /* reset endpoint command */ << 10) | (0 /* reset transfer state */ << 9);

			if (!RunCommand(dw)) {
				KernelLog(LOG_ERROR, "xHCI", "reset control endpoint failure", "Could not reset the control endpoint (1).\n");
				// TODO Force detach the device.
				return false;
			}

			// Reset the dequeue pointer.

			port->controlEndpoint.index = 0;
			port->controlEndpoint.phase = 1;

			EsMemoryZero(port->controlEndpoint.data, 16 * (TRANSFER_RING_ENTRIES + 1));

			dw[0] = (port->controlEndpoint.physical & 0xFFFFFFFF) | (1 << 0 /* phase bit */);
			dw[1] = (port->controlEndpoint.physical >> 32) & 0xFFFFFFFF;
			dw[2] = 0;
			dw[3] = (port->slotID << 24) | (1 /* control endpoint */ << 16)
				| (16 /* set TR dequeue pointer command */ << 10);

			if (!RunCommand(dw)) {
				KernelLog(LOG_ERROR, "xHCI", "reset control endpoint failure", "Could not reset the control endpoint (2).\n");
				// TODO Force detach the device.
				return false;
			}
		}

		return false;
	}

	if (transferred) *transferred = length - (status & 0xFFFFFF);
	return true;
}

bool XHCIController::SelectConfigurationAndInterface(uintptr_t portIndex, KUSBDevice *device) {
	XHCIPort *port = ports + portIndex;

	KMutexAcquire(&port->mutex);
	EsDefer(KMutexRelease(&port->mutex));

	if (this->flags & K_DEVICE_REMOVED) {
		return false;
	}

	// Setup the input context.

	EsMemoryZero(inputContext, INPUT_CONTEXT_BYTES);

	uintptr_t lastIndex = 0;

	for (uintptr_t i = 0; i < device->interfaceDescriptor.endpointCount; i++) {
		KUSBEndpointDescriptor *descriptor = (KUSBEndpointDescriptor *) device->GetCommonDescriptor(5 /* endpoint */, i);

		if (!descriptor) {
			KernelLog(LOG_ERROR, "xHCI", "endpoint descriptor missing", "Could not find endpoint descriptor %d (%d total).\n", 
					i, device->interfaceDescriptor.endpointCount);
			return false;
		}

		KUSBEndpointCompanionDescriptor *companion = nullptr;
		KUSBEndpointIsochronousCompanionDescriptor *isochronousCompanion = nullptr;

		if (port->speed == SPEED_SUPER) {
			companion = (KUSBEndpointCompanionDescriptor *) ((uint8_t *) descriptor + descriptor->length);

			if (companion->length < sizeof(KUSBEndpointCompanionDescriptor)
					|| (size_t) ((uint8_t *) companion + companion->length - device->configurationDescriptors) > device->configurationDescriptorsBytes
					|| companion->descriptorType != 48 /* superspeed endpoint companion */) {
				companion = nullptr;
			} else if (companion->HasISOCompanion()) {
				isochronousCompanion = (KUSBEndpointIsochronousCompanionDescriptor *) ((uint8_t *) companion + companion->length);

				if (isochronousCompanion->length < sizeof(KUSBEndpointCompanionDescriptor)
						|| (size_t) ((uint8_t *) isochronousCompanion + isochronousCompanion->length - device->configurationDescriptors) 
							> device->configurationDescriptorsBytes
						|| isochronousCompanion->descriptorType != 49 /* superspeed isochronous endpoint companion */) {
					isochronousCompanion = nullptr;
				}
			}
		}

		if (descriptor->IsControl()) {
			// Already enabled.
			continue;
		}

		uint32_t maximumBurst = companion ? companion->maxBurst : descriptor->IsBulk() ? 0 : ((descriptor->maximumPacketSize & 0x1800) >> 11);
		uint32_t maximumPacketSize = descriptor->GetMaximumPacketSize();
		uint32_t maximumESITPayload = descriptor->IsBulk() ? 0 : (companion 
			? (isochronousCompanion ? isochronousCompanion->bytesPerInterval : companion->bytesPerInterval)
			: (maximumPacketSize * (maximumBurst + 1)));

		uintptr_t index = (descriptor->IsInput() ? 2 : 1) + descriptor->GetAddress() * 2;
		inputContext[CONTEXT_INDEX(0) + 1] |= 1 << (index - 1);
		if (lastIndex < index - 1) lastIndex = index - 1;

		XHCIEndpoint *endpoint = port->ioEndpoints + index - 3;

		endpoint->maximumPacketSize = maximumPacketSize;

		if (!endpoint->CreateTransferRing()) {
			return false;
		}

		// See "4.8.2 Endpoint Context Initialization".
		inputContext[CONTEXT_INDEX(index) + 0] = 0;
		inputContext[CONTEXT_INDEX(index) + 1] = (maximumBurst << 8) | (maximumPacketSize << 16) | (((maximumESITPayload >> 16) & 0xFF) << 24);
		inputContext[CONTEXT_INDEX(index) + 2] = (1 << 0 /* phase */) | (endpoint->physical & 0xFFFFFFFF);
		inputContext[CONTEXT_INDEX(index) + 3] = ((endpoint->physical >> 32) & 0xFFFFFFFF);
		inputContext[CONTEXT_INDEX(index) + 4] = ((maximumESITPayload & 0xFFFF)) << 16;

		if (descriptor->IsInterrupt()) {
			inputContext[CONTEXT_INDEX(index) + 1] |= (3 /* error count */ << 1) | ((descriptor->IsInput() ? 7 : 3) << 3 /* endpoint type */);
		} else if (descriptor->IsBulk() && (!companion || !companion->GetMaximumStreams())) {
			inputContext[CONTEXT_INDEX(index) + 1] |= (3 /* error count */ << 1) | ((descriptor->IsInput() ? 6 : 2) << 3 /* endpoint type */);
		} else {
			// TODO Isochronous and stream bulk endpoints.
			KernelLog(LOG_ERROR, "xHCI", "unsupported endpoint type", "Isochronous and bulk endpoints are currently unsupported.\n");
			return false;
		}
	}

	inputContext[CONTEXT_INDEX(0) + 1] |= 1 << 0 /* slot context */;

	// See "4.5.2 Slot Context Initialization".
	inputContext[CONTEXT_INDEX(1) + 0] = lastIndex << 27;

	// Send the configure endpoint command.

	{
		uint32_t dw[4];

		dw[0] = (inputContextPhysical >>  0) & 0xFFFFFFFF;
		dw[1] = (inputContextPhysical >> 32) & 0xFFFFFFFF;
		dw[2] = 0;
		dw[3] = (port->slotID << 24) | (12 /* configure endpoint command */ << 10);

		if (!RunCommand(dw)) {
			KernelLog(LOG_ERROR, "xHCI", "configure endpoint failure", "The configure endpoint command failed.\n");
			return false;
		}
	}

	// Set the configuration.

	return ControlTransfer(portIndex, 0, 0x09 /* set configuration */, 
			device->configurationDescriptor.configurationIndex, 0, 
				nullptr, 0, K_ACCESS_WRITE, nullptr, true /* already locked */);
}

bool XHCIController::QueueInterruptTransfer(uintptr_t portIndex, uint8_t endpointAddress, KUSBTransferCallback callback, 
			void *buffer, size_t bufferBytes, EsGeneric context) {
	XHCIPort *port = ports + portIndex;

	KMutexAcquire(&port->mutex);
	EsDefer(KMutexRelease(&port->mutex));

	if (this->flags & K_DEVICE_REMOVED) {
		return false;
	}

	XHCIEndpoint *endpoint = port->ioEndpoints + endpointAddress - 2;
	endpoint->callback = callback;
	endpoint->context = context;

	if (!AddTransferDescriptors((uintptr_t) buffer, bufferBytes, (endpointAddress & 1) ? K_ACCESS_READ : K_ACCESS_WRITE, 
			endpoint, 1 /* normal TRB */, true)) {
		return false;
	}

	WR_REGISTER_DOORBELL(port->slotID, endpointAddress);
	return true;
}

bool XHCIController::HandleIRQ() {
	// Clear interrupt status.

	uint32_t usbStatus = RD_REGISTER_USBSTS();
	WR_REGISTER_USBSTS((1 << 3) | (1 << 4));

	// Check for an interrupt.

	uint32_t status = RD_REGISTER_IMAN(0);

	KernelLog(LOG_VERBOSE, "xHCI", "IRQ", "Received IRQ. USB status: %x. Interrupter status: %x.\n", usbStatus, status);

	// Acknowledge the interrupt.

	WR_REGISTER_IMAN(0, status);
	RD_REGISTER_IMAN(0); // Read the value back.

	// Consume events.

	while (true) {
		uint32_t dw0 = eventRing[0x10 + 0x04 * eventRingIndex + 0x00];
		uint32_t dw1 = eventRing[0x10 + 0x04 * eventRingIndex + 0x01];
		uint32_t dw2 = eventRing[0x10 + 0x04 * eventRingIndex + 0x02];
		uint32_t dw3 = eventRing[0x10 + 0x04 * eventRingIndex + 0x03];

		if ((dw3 & (1 << 0)) != eventRingPhase) {
			break;
		}

		uint32_t type = (dw3 >> 10) & 0x3F;
		uint8_t completionCode = (dw2 >> 24) & 0xFF;

		KernelLog(LOG_VERBOSE, "xHCI", "got event", "Received event of type %d with code %d from %x.\n", 
				type, completionCode, (uint64_t) dw0 | ((uint64_t) dw1 << 32));

		if (type == 32 /* transfer completion event */) {
			uint8_t slotID = (dw3 >> 24) & 0xFF;
			uint8_t endpointID = (dw3 >> 16) & 0x1F;

			if (endpointID == 1) {
				for (uintptr_t i = 0; i < maximumPorts; i++) {
					XHCIPort *port = ports + i;
					if (port->slotID != slotID) continue;
					if (!port->enabled) break;

					if (!port->controlTransferResult) {
						KernelLog(LOG_ERROR, "xHCI", "spurious transfer completion event", 
								"Received an unexpected transfer command completion event on port %d. "
								"Contents: %x, %x, %x, %x.\n", i, dw0, dw1, dw2, dw3);
					} else {
						bool lastTransfer = dw0 == (port->controlTransferLastTRBAddress & 0xFFFFFFFF) 
							&& dw1 == ((port->controlTransferLastTRBAddress >> 32) & 0xFFFFFFFF);

						if (completionCode != 1 || lastTransfer) {
							port->controlTransferResult[0] = dw0;
							port->controlTransferResult[1] = dw1;
							port->controlTransferResult[2] = dw2;
							port->controlTransferResult[3] = dw3;
							KEventSet(&port->controlTransferCompleteEvent);
						}
					}

					break;
				}
			} else if (endpointID >= 2) {
				for (uintptr_t i = 0; i < maximumPorts; i++) {
					XHCIPort *port = ports + i;
					if (port->slotID != slotID) continue;
					if (!port->enabled) break;
					XHCIEndpoint *endpoint = port->ioEndpoints + endpointID - 2;
					endpoint->lastStatus = dw2;

					if (completionCode != 1) {
						if (completionCode < sizeof(commandCompletionCodes) / sizeof(commandCompletionCodes[0])) {
							KernelLog(LOG_ERROR, "xHCI", "failed transfer", "Transfer failed with completion code '%z' (%x).\n", 
									commandCompletionCodes[completionCode], dw2);
						} else {
							KernelLog(LOG_ERROR, "xHCI", "failed transfer", "Transfer failed with unrecognised completion code %d.\n", completionCode);
						}
					}

					if (endpoint->callback) {
						KRegisterAsyncTask(&endpoint->callbackAsyncTask, [] (KAsyncTask *task) {
							XHCIEndpoint *endpoint = EsContainerOf(XHCIEndpoint, callbackAsyncTask, task);
							KUSBTransferCallback callback = endpoint->callback;
							endpoint->callback = nullptr;
							callback((endpoint->lastStatus >> 24) != 1 ? -1 : endpoint->lastStatus & 0xFFFFFF, endpoint->context);
						});
					}

					break;
				}
			}
		} else if (type == 33 /* command completion event */) {
			if (!commandResult) {
				KernelLog(LOG_ERROR, "xHCI", "spurious command completion event", 
						"Received a spurious command completion event (no commands issued since last expected received completion event). "
						"Contents: %x, %x, %x, %x.\n", dw0, dw1, dw2, dw3);
			} else {
				commandResult[0] = dw0;
				commandResult[1] = dw1;
				commandResult[2] = dw2;
				commandResult[3] = dw3;
				KEventSet(&commandCompleteEvent);
			}
		} else if (type == 34 /* port status change event */) {
			uintptr_t port = (dw0 >> 24) - 1;

			KSpinlockAcquire(&portResetSpinlock);

			// Acknowledge the status change.

			uint32_t status = RD_REGISTER_PORTSC(port);
			uint32_t linkState = (status >> 5) & 0x0F;
			WR_REGISTER_PORTSC(port, ((1 << 9) | (1 << 17) | (1 << 18) | (1 << 19) | (1 << 20) | (1 << 21) | (1 << 22) | (1 << 23)) & status);
			KernelLog(LOG_INFO, "xHCI", "port status change", "Port %d has new status: %x.\n", port, status);

			if (!ports[port].enabled && (status & (1 << 1 /* port enabled */))) {
				KernelLog(LOG_INFO, "xHCI", "port enabled", "Port %d has been enabled.\n", port);
				ports[port].statusChangeEvent = true;
				ports[port].enabled = true;
				KEventSet(&portStatusChangeEvent, false, true);
			} else if (ports[port].usb2 && (linkState == 7 || linkState == 4) && (~status & (1 << 4))) {
				KernelLog(LOG_INFO, "xHCI", "port reset", "Attempting to reset USB 2 port %d... (1)\n", port);
				WR_REGISTER_PORTSC(port, (status & (1 << 9)) | (1 << 4));
			} else if (ports[port].enabled && linkState == 5 && (~status & (1 << 0)) && (status & (1 << 17))) {
				KernelLog(LOG_INFO, "xHCI", "port detach", "Device detached from port %d.\n", port);
				ports[port].statusChangeEvent = true;
				ports[port].enabled = false;
				KEventSet(&portStatusChangeEvent, false, true);
			}

			KSpinlockRelease(&portResetSpinlock);
		}

		eventRingIndex++;

		if (eventRingIndex == EVENT_RING_ENTRIES) {
			eventRingIndex = 0;
			eventRingPhase = !eventRingPhase;
		}
	}

	WR_REGISTER_ERDP(0, (eventRingPhysical + 0x40 + (eventRingIndex << 4)) | (1 << 3));

	return true;
}

bool XHCIController::RunCommand(uint32_t *dw) {
	KEventReset(&commandCompleteEvent);
	commandResult = dw;

	commandRing[commandRingIndex * 4 + 0] = dw[0];
	commandRing[commandRingIndex * 4 + 1] = dw[1];
	commandRing[commandRingIndex * 4 + 2] = dw[2];
	commandRing[commandRingIndex * 4 + 3] = dw[3] | (commandRingPhase ? (1 << 0) : 0);

	commandRingIndex++;

	if (commandRingIndex == COMMAND_RING_ENTRIES) {
		commandRingIndex = 0;
		commandRingPhase = !commandRingPhase;
		commandRing[COMMAND_RING_ENTRIES * 4 + 3] ^= 1 << 0; // Toggle phase of link TRB.
	}

	// TODO Timeout, and command abortion.
	WR_REGISTER_DOORBELL(0, 0);
	KEventWait(&commandCompleteEvent);

	commandResult = nullptr;

	uint8_t completionCode = dw[2] >> 24;

	if (completionCode != 1) {
		if (completionCode < sizeof(commandCompletionCodes) / sizeof(commandCompletionCodes[0])) {
			KernelLog(LOG_ERROR, "xHCI", "failed command", "Command failed with completion code '%z'.\n", commandCompletionCodes[completionCode]);
		} else {
			KernelLog(LOG_ERROR, "xHCI", "failed command", "Command failed with unrecognised completion code %d.\n", completionCode);
		}
	}

	return completionCode == 1;
}

static bool SelectConfigurationAndInterfaceWrapper(KUSBDevice *_device) {
	XHCIDevice *device = (XHCIDevice *) _device;
	XHCIController *controller = (XHCIController *) device->parent;
	return controller->SelectConfigurationAndInterface(device->port, device);
}

static bool QueueTransferWrapper(KUSBDevice *_device, KUSBEndpointDescriptor *endpoint, KUSBTransferCallback callback, 
					void *buffer, size_t bufferBytes, EsGeneric context) {
	XHCIDevice *device = (XHCIDevice *) _device;
	XHCIController *controller = (XHCIController *) device->parent;
	return controller->QueueInterruptTransfer(device->port, endpoint->GetAddress() * 2 + (endpoint->IsInput() ? 1 : 0), 
			callback, buffer, bufferBytes, context);
}

static bool ControlTransferWrapper(KUSBDevice *_device, uint8_t flags, uint8_t request, uint16_t value, uint16_t index, 
					void *buffer, uint16_t length, int operation, uint16_t *transferred) {
	XHCIDevice *device = (XHCIDevice *) _device;
	XHCIController *controller = (XHCIController *) device->parent;
	KernelLog(LOG_VERBOSE, "xHCI", "control transfer", "Control transfer: %X, %X, %d, %d, %D.\n", flags, request, value, index, length);
	bool success = controller->ControlTransfer(device->port, flags, request, value, index, buffer, length, operation, transferred, false);
	KernelLog(LOG_VERBOSE, "xHCI", "control transfer complete", "Control transfer complete. Success = %d.\n", success);
	return success;
}

void XHCIController::OnPortEnable(uintptr_t port) {
	uint32_t speed = (RD_REGISTER_PORTSC(port) >> 10) & 0xF;
	uint32_t maximumPacketSize = speed == SPEED_LOW ? 8 : speed == SPEED_FULL ? 8 : speed == SPEED_HIGH ? 64 : speed == 4 ? SPEED_SUPER : 0;
	ports[port].speed = speed;
	ports[port].controlEndpoint.maximumPacketSize = maximumPacketSize;

	KernelLog(LOG_INFO, "xHCI", "initialising port", "Port %d is enabled, initialising. Speed: %z.\n", 
			port, speed == SPEED_LOW ? "low" : speed == SPEED_FULL ? "full" : speed == SPEED_HIGH ? "high" : speed == SPEED_SUPER ? "super" : "unknown");

	if (!maximumPacketSize) {
		KernelLog(LOG_ERROR, "xHCI", "unrecognised device speed", "Unrecognised device speed %d on port %d.\n", speed, port);
		return;
	}

	// Assign a device slot.

	uint32_t dw[4] = {};
	dw[3] = (9 /* enable slot */ << 10) | (ports[port].slotType << 16);

	if (!RunCommand(dw) || !(dw[3] >> 24)) {
		KernelLog(LOG_ERROR, "xHCI", "enable slot error", "Could not enable a slot for the device on port %d.\n", port);
		return;
	}

	ports[port].slotID = dw[3] >> 24;

	KernelLog(LOG_INFO, "xHCI", "assign device slot", "Port %d assigned device slot %d.\n", port, ports[port].slotID);

	// Allocate the transfer ring.

	if (!ports[port].controlEndpoint.CreateTransferRing()) {
		return;
	}

	// Setup the input context.

	EsMemoryZero(inputContext, INPUT_CONTEXT_BYTES);

	inputContext[CONTEXT_INDEX(0) + 1] = (1 << 0 /* slot context valid */) | (1 << 1 /* control endpoint context valid */);

	inputContext[CONTEXT_INDEX(1) + 0] = (1 /* 1 context entry */ << 27) | (speed << 20);
	inputContext[CONTEXT_INDEX(1) + 1] = (port + 1) << 16;

	// TODO Update this with the maximum packet size from the device descriptor.
	// 	This field is interpreted differently on USB 2/3.

	inputContext[CONTEXT_INDEX(2) + 1] = (3 /* error count */ << 1) | (4 /* control endpoint */ << 3) | (maximumPacketSize << 16);
	inputContext[CONTEXT_INDEX(2) + 2] = (ports[port].controlEndpoint.physical & 0xFFFFFFFF) | (1 << 0 /* phase bit */);
	inputContext[CONTEXT_INDEX(2) + 3] = (ports[port].controlEndpoint.physical >> 32) & 0xFFFFFFFF;
	inputContext[CONTEXT_INDEX(2) + 4] = 8 /* average TRB length */;

	// Allocate the output context.

	if (!MMPhysicalAllocateAndMap(OUTPUT_CONTEXT_BYTES, K_PAGE_SIZE, 0, true, 
				0, (uint8_t **) &ports[port].outputContext, &ports[port].outputContextPhysical)) {
		KernelLog(LOG_ERROR, "xHCI", "allocation failure", "Could not allocate the output context for port %d.\n", port);
		return;
	}

	deviceContextBaseAddressArray[ports[port].slotID] = ports[port].outputContextPhysical;

	// Address the device.
	// TODO Full-speed devices need their maximum packet size queried before addressing.

	dw[0] = (inputContextPhysical >>  0) & 0xFFFFFFFF;
	dw[1] = (inputContextPhysical >> 32) & 0xFFFFFFFF;
	dw[2] = 0;
	dw[3] = (ports[port].slotID << 24) | (0 /* send SET_ADDRESS */ << 9) | (11 /* address device command */ << 10);

	if (!RunCommand(dw)) {
		KernelLog(LOG_ERROR, "xHCI", "address device error", "Could not address the device on port %d.\n", port);
		return;
	}

	KernelLog(LOG_INFO, "xHCI", "address device", "Port %d successfully addressed.\n", port);

	// Register the device with USB subsystem.

	XHCIDevice *device = (XHCIDevice *) KDeviceCreate("XHCI device", this, sizeof(XHCIDevice));
	ports[port].device = device;
	device->port = port;
	device->controlTransfer = ControlTransferWrapper;
	device->selectConfigurationAndInterface = SelectConfigurationAndInterfaceWrapper;
	device->queueTransfer = QueueTransferWrapper;
	KRegisterUSBDevice(device);

	KernelLog(LOG_INFO, "xHCI", "register device", "Port %d registered with USB subsystem.\n", port);
}

void XHCIController::OnPortDisable(uintptr_t port) {
	KMutexAcquire(&ports[port].mutex);
	EsDefer(KMutexRelease(&ports[port].mutex));

	if (ports[port].device) {
		KDeviceRemoved(ports[port].device);
		ports[port].device = nullptr;
	}

	uint32_t dw[4];

	// Stop endpoints.

	for (uintptr_t i = 0; i < sizeof(ports[port].ioEndpoints) / sizeof(ports[port].ioEndpoints[0]); i++) {
		XHCIEndpoint *endpoint = ports[port].ioEndpoints + i;

		if (!endpoint->data) {
			continue;
		}

		dw[0] = dw[1] = dw[2] = 0;
		dw[3] = (15 /* stop endpoint */ << 10) | ((i + 2) << 16) | (ports[port].slotID << 24);
		RunCommand(dw);

		MMPhysicalFreeAndUnmap(endpoint->data, endpoint->physical);
		endpoint->data = nullptr;
		endpoint->physical = 0;

		KUSBTransferCallback callback = endpoint->callback;
		endpoint->callback = nullptr;

		if (callback) {
			callback(-1, endpoint->context);
		}
	}
	
	// Disable slot.

	dw[0] = dw[1] = dw[2] = 0;
	dw[3] = (10 /* disable slot */ << 10) | (ports[port].slotID << 24);
	RunCommand(dw);

	// Free the output context.

	MMPhysicalFreeAndUnmap(ports[port].outputContext, ports[port].outputContextPhysical);
	deviceContextBaseAddressArray[ports[port].slotID] = ports[port].outputContextPhysical;
	ports[port].outputContext = nullptr;
	ports[port].outputContextPhysical = 0;

	// Free the control endpoint transfer ring.

	MMPhysicalFreeAndUnmap(ports[port].controlEndpoint.data, ports[port].controlEndpoint.physical);
	ports[port].controlEndpoint.data = nullptr;
	ports[port].controlEndpoint.physical = 0;

	// Zero out remaining fields in the port structure.

	ports[port].slotID = 0;
	ports[port].speed = 0;
	ports[port].controlTransferResult = nullptr;
	ports[port].controlTransferLastTRBAddress = 0;
}

void XHCIController::Initialise() {
	portStatusChangeEvent.autoReset = true;

	// Read capabilities.

	operationalRegistersOffset = RD_REGISTER_CAPLENGTH() & 0xFF;

	if ((RD_REGISTER_CAPLENGTH() & 0xFF000000) == 0) {
		KernelLog(LOG_ERROR, "xHCI", "unsupported version", "xHCI controller reports unsupported version number %x.\n", 
				RD_REGISTER_CAPLENGTH() >> 16);
		return;
	}

	uint32_t hcsp1 = RD_REGISTER_HCSPARAMS1();
	maximumDeviceSlots = (hcsp1 >> 0) & 0xFF;
	maximumInterrupters = (hcsp1 >> 8) & 0x7FF;
	maximumPorts = (hcsp1 >> 24) & 0xFF;

	ports = (XHCIPort *) EsHeapAllocate(sizeof(XHCIPort) * maximumPorts, true, K_FIXED);

	if (!ports) {
		KernelLog(LOG_ERROR, "xHCI", "allocation failure", "Could not allocate ports array.\n");
		return;
	}

	uint32_t hcsp2 = RD_REGISTER_HCSPARAMS2();
	maximumEventRingSegments = 1 << ((hcsp2 >> 4) & 0xF);

	uint32_t scratchpadBufferCount = ((hcsp2 >> 27) & 0x1F) | (((hcsp2 >> 21) & 0x1F) << 5);
	KernelLog(LOG_INFO, "xHCI", "scratchpad buffers", "Controller requests %d scratchpad buffers (HCSP2 = %x).\n", scratchpadBufferCount, hcsp2);

	uint32_t hccp1 = RD_REGISTER_HCCPARAMS1();
	contextSize64 = (hccp1 & (1 << 2)) ? true : false;
	extendedCapabilitiesOffset = ((hccp1 >> 16) & 0xFFFF) << 2;

#ifdef ES_BITS_64
	if (~hccp1 & (1 << 0)) {
		KernelLog(LOG_ERROR, "xHCI", "missing feature", "xHCI controller does not support 64-bit addresses.\n");
		return;
	}
#endif

	doorbellsOffset = RD_REGISTER_DBOFF();
	runtimeRegistersOffset = RD_REGISTER_RTSOFF();

	KernelLog(LOG_INFO, "xHCI", "capabilities", "xHC reports capabilities: maximum ports - %d, maximum interrupts - %d, "
			"maximum device slots - %d, maximum event ring segments - %d.\n",
			maximumPorts, maximumInterrupters, maximumDeviceSlots, maximumEventRingSegments);

	// Enumerate extended capabilities.

	if (extendedCapabilitiesOffset) {
		uintptr_t offset = extendedCapabilitiesOffset;

		for (uintptr_t i = 0; i < 256; i++) {
			uint32_t extendedCapability = pci->ReadBAR32(0, offset);
			uint8_t id = (extendedCapability >> 0) & 0xFF;
			uint8_t next = (extendedCapability >> 8) & 0xFF;

			if (id == 1) {
				// Perform BIOS handoff, if necessary.

				if (extendedCapability & (1 << 16)) {
					pci->WriteBAR32(0, offset, extendedCapability & (1 << 24));

					KTimeout timeout(1000);
					while (!timeout.Hit() && (pci->ReadBAR32(0, offset) & (1 << 16)));

					if (pci->ReadBAR32(0, offset) & (1 << 16)) {
						KernelLog(LOG_ERROR, "xHCI", "BIOS handoff failure", "Could not take control of the xHC from the BIOS.\n");
						return;
					}

					uint32_t control = pci->ReadBAR32(0, offset + 4);

					KernelLog(LOG_INFO, "xHCI", "legacy control", "USB legacy control/status register is %x.\n", control);

					// Disable SMIs.
					control &= ~((1 << 0) | (1 << 4) | (1 << 13) | (1 << 14) | (1 << 15));

					// Clear RsvdZ bits.
					control &= ~((1 << 21) | (1 << 22) | (1 << 23) | (1 << 24) | (1 << 25) | (1 << 26) | (1 << 27) | (1 << 28));

					// Acknowledge any status bits.
					control |= (1 << 29) | (1 << 30) | (1 << 31);

					KernelLog(LOG_INFO, "xHCI", "legacy control", "Writing legacy control/status %x.\n", control);

					pci->WriteBAR32(0, offset + 4, control);
				}
			} else if (id == 2) {
				uint32_t nameString = pci->ReadBAR32(0, offset + 4);
				uint32_t portRange = pci->ReadBAR32(0, offset + 8);
				uintptr_t portOffset = (portRange & 0xFF) - 1;
				size_t portCount = (portRange >> 8) & 0xFF;
				uint32_t slotType = pci->ReadBAR32(0, offset + 12) & 0x1F;

				KernelLog(LOG_INFO, "xHCI", "protocol enumerated", 
						"Controller supports protocol on ports %d->%d with version %d.%d (%s) and slot type %d.\n",
						portOffset, portOffset + portCount - 1,
						extendedCapability >> 24, (extendedCapability >> 16) & 0xFF, 4, &nameString, slotType);

				for (uintptr_t i = portOffset; i < portOffset + portCount; i++) {
					ports[i].usb2 = (extendedCapability >> 24) == 2;
					ports[i].slotType = slotType;
				}
			}

			if (next) {
				offset += next << 2;
			} else {
				break;
			}
		}
	}

	// Check page size is supported.

	if (~RD_REGISTER_PAGESIZE() & (1 << (K_PAGE_BITS - 12))) {
		KernelLog(LOG_ERROR, "xHCI", "page size not supported", "xHC does not support native page size of %x; supported mask is %x.\n", 
				K_PAGE_SIZE, RD_REGISTER_PAGESIZE());
		return;
	}

	// Stop the controller.

	KernelLog(LOG_INFO, "xHCI", "stop controller", "Stopping the controller...\n");

	WR_REGISTER_USBCMD(RD_REGISTER_USBCMD() & ~(1 << 0));

	{
		KTimeout timeout(20);
		while (!timeout.Hit() && (~RD_REGISTER_USBSTS() & (1 << 0)));

		if ((~RD_REGISTER_USBSTS() & (1 << 0))) {
			KernelLog(LOG_ERROR, "xHCI", "stop failure", "Could not stop the xHC to perform a reset.\n");
			return;
		}
	}

	// Reset the controller.

	KernelLog(LOG_INFO, "xHCI", "reset controller", "Resetting the controller...\n");

	WR_REGISTER_USBCMD(RD_REGISTER_USBCMD() | (1 << 1));

	{
		KTimeout timeout(100);
		while (!timeout.Hit() && (RD_REGISTER_USBCMD() & (1 << 1)));

		if (RD_REGISTER_USBCMD() & (1 << 1)) {
			KernelLog(LOG_ERROR, "xHCI", "reset failure", "Could not reset the xHC within 100ms.\n");
			return;
		}
	}

	KernelLog(LOG_INFO, "xHCI", "reset controller", "Controller successfully reset.\n");

#if defined(ES_ARCH_X86_32) || defined(ES_ARCH_X86_64)
	// Any PS/2 emulation should have been disabled, so its controller is safe to initialise.
	KPS2SafeToInitialise();
#endif

	// Allocate the input context.

	if (!MMPhysicalAllocateAndMap(INPUT_CONTEXT_BYTES, K_PAGE_SIZE, 0, true, 
				0, (uint8_t **) &inputContext, &inputContextPhysical)) {
		KernelLog(LOG_ERROR, "xHCI", "allocation failure", "Could not allocate the input context.\n");
		return;
	}

	// Allocate the device context base address array.

	uintptr_t deviceContextBaseAddressArrayPhysical;
	
	if (!MMPhysicalAllocateAndMap(256 * 8, 64, 0, true, 
				0, (uint8_t **) &deviceContextBaseAddressArray, &deviceContextBaseAddressArrayPhysical)) {
		KernelLog(LOG_ERROR, "xHCI", "allocation failure", "Could not allocate the device context base address array.\n");
		return;
	}

	WR_REGISTER_DCBAAP(deviceContextBaseAddressArrayPhysical);

	// Allocate the command ring.

	uintptr_t commandRingPhysical;
	
	if (!MMPhysicalAllocateAndMap(16 * COMMAND_RING_ENTRIES, 64, 0, true, 
				0, (uint8_t **) &commandRing, &commandRingPhysical)) {
		KernelLog(LOG_ERROR, "xHCI", "allocation failure", "Could not allocate the command ring.\n");
		return;
	}

	if (RD_REGISTER_CRCR() & (1 << 3)) {
		KernelLog(LOG_ERROR, "xHCI", "command ring running", "Command ring running while controller stopped.\n");
		return;
	}

	KernelLog(LOG_INFO, "xHCI", "command ring allocation", "Allocated command ring at physical address %x.\n", commandRingPhysical);

	WR_REGISTER_CRCR(commandRingPhysical | 1);

	commandRing[COMMAND_RING_ENTRIES * 4 + 0] = commandRingPhysical & 0xFFFFFFFF;
	commandRing[COMMAND_RING_ENTRIES * 4 + 1] = (commandRingPhysical >> 32) & 0xFFFFFFFF;
	commandRing[COMMAND_RING_ENTRIES * 4 + 3] = (1 << 1 /* toggle cycle */) | (6 /* link TRB */ << 10);

	commandRingPhase = true;

	// Allocate the event ring.

	if (!MMPhysicalAllocateAndMap(64 + 16 * EVENT_RING_ENTRIES, 64, 0, true, 
				0, (uint8_t **) &eventRing, &eventRingPhysical)) {
		KernelLog(LOG_ERROR, "xHCI", "allocation failure", "Could not allocate the command ring.\n");
		return;
	}

	eventRing[0] = ((eventRingPhysical + 0x40) >>  0) & 0xFFFFFFFF;
	eventRing[1] = ((eventRingPhysical + 0x40) >> 32) & 0xFFFFFFFF;
	eventRing[2] = EVENT_RING_ENTRIES;
	eventRing[3] = 0;
	eventRingPhase = true;

	WR_REGISTER_ERDP(0, (eventRingPhysical + 0x40) | (1 << 3));
	WR_REGISTER_ERSTSZ(0, (RD_REGISTER_ERSTSZ(0) & 0xFFFF0000) | 1);
	WR_REGISTER_ERSTBA(0, (RD_REGISTER_ERSTBA(0) & 0x3F) | eventRingPhysical);

	// Setup the CONFIG register.

	WR_REGISTER_CONFIG((RD_REGISTER_CONFIG() & 0xFFFFFC00) | maximumDeviceSlots);

	// Register the interrupt handler and enable interrupts.

	KIRQHandler handler = [] (uintptr_t, void *context) { return ((XHCIController *) context)->HandleIRQ(); };

	if (!pci->EnableSingleInterrupt(handler, this, "xHCI")) {
		KernelLog(LOG_ERROR, "xHCI", "IRQ registration failure", "Could not register interrupt handler.\n");
		return;
	}

	WR_REGISTER_IMOD(0, 4000 /* in 250ns increments */); // Interrupts should be sent at most every millisecond.
	WR_REGISTER_IMAN(0, RD_REGISTER_IMAN(0) | (1 << 1));
	WR_REGISTER_USBCMD(RD_REGISTER_USBCMD() | (1 << 2));

	// Allocate scratchpad buffers.

	if (scratchpadBufferCount) {
		uint64_t *scratchpadArray;
		uintptr_t scratchpadArrayPhysical;

		if (!MMPhysicalAllocateAndMap(scratchpadBufferCount * sizeof(uint64_t), 64, 0, true, 
					0, (uint8_t **) &scratchpadArray, &scratchpadArrayPhysical)) {
			KernelLog(LOG_ERROR, "xHCI", "allocation failure", "Could not allocate the scratchpad buffer array.\n");
			return;
		}

		for (uintptr_t i = 0; i < scratchpadBufferCount; i++) {
			scratchpadArray[i] = MMPhysicalAllocate(MM_PHYSICAL_ALLOCATE_CAN_FAIL | MM_PHYSICAL_ALLOCATE_COMMIT_NOW | MM_PHYSICAL_ALLOCATE_ZEROED);

			if (!scratchpadArray[i]) {
				KernelLog(LOG_ERROR, "xHCI", "allocation failure", "Could not allocate scratchpad buffer %d.\n", i);
				return;
			}
		}

		deviceContextBaseAddressArray[0] = scratchpadArrayPhysical;
	}

	// Start the controller!

	KernelLog(LOG_INFO, "xHCI", "start controller", "Starting controller...\n");

	WR_REGISTER_USBCMD(RD_REGISTER_USBCMD() | (1 << 0));

	{
		KTimeout timeout(20);
		while (!timeout.Hit() && (RD_REGISTER_USBSTS() & (1 << 0)));

		if ((RD_REGISTER_USBSTS() & (1 << 0))) {
			KernelLog(LOG_ERROR, "xHCI", "start failure", "Could not start the xHC after reset.\n");
			return;
		}
	}

	KernelLog(LOG_INFO, "xHCI", "start controller", "Controller successfully started.\n");

	// Reset USB2 ports.
	// TODO Will we receive events for connected USB3 ports automatically, or do we need to do something?

	KSpinlockAcquire(&portResetSpinlock);

	for (uintptr_t i = 0; i < maximumPorts; i++) {
		uint32_t status = RD_REGISTER_PORTSC(i);
		uint32_t linkState = (status >> 5) & 0x0F;
		KernelLog(LOG_INFO, "xHCI", "port status", "Port %d (USB %d) has status %x (link state = %d).\n", i, ports[i].usb2 ? 2 : 3, status, linkState);

		if (ports[i].usb2 && (linkState == 7 || linkState == 4) && (~status & (1 << 4))) {
			KernelLog(LOG_INFO, "xHCI", "port reset", "Attempting to reset USB 2 port %d... (2)\n", i);
			WR_REGISTER_PORTSC(i, (status & (1 << 9)) | (1 << 4));
		}
	}

	KSpinlockRelease(&portResetSpinlock);

	// Wait for events to process.

	while (true) {
		uintptr_t port = 0;

		for (uintptr_t i = 0; i < maximumPorts; i++) {
			if (ports[i].statusChangeEvent) {
				port = i;
				goto found;
			}
		}

		KEventWait(&portStatusChangeEvent);
		continue;

		found:;
		ports[port].statusChangeEvent = false;

		if (ports[port].enabled) {
			OnPortEnable(port);
		} else {
			OnPortDisable(port);
		}
	}
}

static void DeviceAttach(KDevice *_parent) {
	KPCIDevice *parent = (KPCIDevice *) _parent;

	XHCIController *device = (XHCIController *) KDeviceCreate("XHCI controller", parent, sizeof(XHCIController));
	if (!device) return;
	device->pci = parent;

	device->dumpState = [] (KDevice *device) {
		((XHCIController *) device)->DumpState();
	};

	KernelLog(LOG_INFO, "xHCI", "found controller", "Found XHCI controller at PCI function %d/%d/%d.\n", parent->bus, parent->slot, parent->function);

	// Enable PCI features.
	parent->EnableFeatures(K_PCI_FEATURE_INTERRUPTS 
			| K_PCI_FEATURE_BUSMASTERING_DMA
			| K_PCI_FEATURE_MEMORY_SPACE_ACCESS
			| K_PCI_FEATURE_BAR_0);

	// Initialise the controller on a new thread.
	KThreadCreate("XHCIInitialisation", [] (uintptr_t context) {
		((XHCIController *) context)->Initialise();
	}, (uintptr_t) device);
}

KDriver driverxHCI = {
	.attach = DeviceAttach,
};
