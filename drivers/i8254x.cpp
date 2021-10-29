// TODO Initialise on a separate thread.
// TODO Checksum off-loading.

#include <module.h>

#define RD_REGISTER_CTRL()		Read(0x00)			// Device control.
#define WR_REGISTER_CTRL(x)		Write(0x00, (x) & ~0x20000416)
#define RD_REGISTER_STATUS()		Read(0x08)			// Device status.
#define RD_REGISTER_EECD()		Read(0x10)			// EEPROM data.
#define RD_REGISTER_EERD()		Read(0x14)			// EEPROM read.
#define WR_REGISTER_EERD(x)		Write(0x14, x)
#define WR_REGISTER_FCAL(x)		Write(0x28, x)			// Flow control low.
#define WR_REGISTER_FCAH(x)		Write(0x2C, x)			// Flow control high.
#define WR_REGISTER_FCT(x)		Write(0x30, x)			// Flow control type.
#define RD_REGISTER_ICR()		Read(0xC0)			// Interrupt cause read.
#define RD_REGISTER_IMS()		Read(0xD0)			// Interrupt mask set/read.
#define WR_REGISTER_IMS(x)		Write(0xD0, x)
#define WR_REGISTER_IMC(x)		Write(0xD8, x)			// Interrupt mask clear.
#define RD_REGISTER_RCTL()		Read(0x100)			// Receive control.
#define WR_REGISTER_RCTL(x)		Write(0x100, (x) & ~0xF9204C01)
#define RD_REGISTER_RDBAL()		Read(0x2800)			// Receive descriptor base address low.
#define WR_REGISTER_RDBAL(x)		Write(0x2800, x)
#define RD_REGISTER_RDBAH()		Read(0x2804)			// Receive descriptor base address high.
#define WR_REGISTER_RDBAH(x)		Write(0x2804, x)
#define RD_REGISTER_RDLEN()		Read(0x2808)			// Receive descriptor length.
#define WR_REGISTER_RDLEN(x)		Write(0x2808, x)
#define RD_REGISTER_RDH()		Read(0x2810)			// Receive descriptor head.
#define WR_REGISTER_RDH(x)		Write(0x2810, x)
#define RD_REGISTER_RDT()		Read(0x2818)			// Receive descriptor tail.
#define WR_REGISTER_RDT(x)		Write(0x2818, x)
#define WR_REGISTER_MTA(x, y)		Write(0x5200 + x * 4, y)	// Multicast table array.
#define RD_REGISTER_RAL()		Read(0x5400)			// Receive address low.
#define WR_REGISTER_RAL(x)		Write(0x5400, x)
#define RD_REGISTER_RAH()		Read(0x5404)			// Receive address high.
#define WR_REGISTER_RAH(x)		Write(0x5404, x)
#define RD_REGISTER_TCTL()		Read(0x400)			// Transmit control.
#define WR_REGISTER_TCTL(x)		Write(0x400, (x) & ~0xFC800005)
#define RD_REGISTER_TDBAL()		Read(0x3800)			// Transmit descriptor base address low.
#define WR_REGISTER_TDBAL(x)		Write(0x3800, x)
#define RD_REGISTER_TDBAH()		Read(0x3804)			// Transmit descriptor base address high.
#define WR_REGISTER_TDBAH(x)		Write(0x3804, x)
#define RD_REGISTER_TDLEN()		Read(0x3808)			// Transmit descriptor length.
#define WR_REGISTER_TDLEN(x)		Write(0x3808, x)
#define RD_REGISTER_TDH()		Read(0x3810)			// Transmit descriptor head.
#define WR_REGISTER_TDH(x)		Write(0x3810, x)
#define RD_REGISTER_TDT()		Read(0x3818)			// Transmit descriptor tail.
#define WR_REGISTER_TDT(x)		Write(0x3818, x)

#define RECEIVE_DESCRIPTOR_COUNT (64)
#define TRANSMIT_DESCRIPTOR_COUNT (64)

#define RECEIVE_BUFFER_SIZE (8192)
#define TRANSMIT_BUFFER_SIZE (8192)
#define TRANSFER_BUFFER_EXTRA (16) // TODO What are these for?

struct ReceiveDescriptor {
	uint64_t address;
	uint16_t length;
	uint16_t checksum;
	uint8_t status, errors;
	uint16_t special;
};

struct TransmitDescriptor {
	uint64_t address;
	uint16_t length;
	uint8_t checksumOffset;
	uint8_t command;
	uint8_t status;
	uint8_t checksumStartField;
	uint16_t special;
};

struct Controller : NetInterface {
	KPCIDevice *pci;

	bool hasEEPROM;

	ReceiveDescriptor *receiveDescriptors;
	TransmitDescriptor *transmitDescriptors;
	uint8_t *receiveBuffers[RECEIVE_DESCRIPTOR_COUNT];
	void *transmitBuffers[TRANSMIT_DESCRIPTOR_COUNT];
	uintptr_t receiveTail;
	uintptr_t transmitTail;

	// Used by the dispatch thread.
	uint8_t *dispatchBuffers[RECEIVE_DESCRIPTOR_COUNT];
	size_t dispatchByteCounts[RECEIVE_DESCRIPTOR_COUNT];
	uintptr_t dispatchPhysicalAddresses[RECEIVE_DESCRIPTOR_COUNT];

	KMutex transmitMutex;
	KEvent receiveEvent;

	uint32_t Read(uintptr_t offset);
	void Write(uintptr_t offset, uint32_t value);
	bool ReadEEPROM(uint8_t address, uint16_t *data);

	void Initialise();
	bool Transmit(void *dataVirtual, uintptr_t dataPhysical, size_t dataBytes);
	bool HandleIRQ();
	void DispatchThread();
	void DumpState();
};

void Controller::DumpState() {
	EsPrint("I8254x controller state:\n");

	EsPrint("\t--- Internal ---\n");
	EsPrint("\t\tHas EEPROM: %z.\n", hasEEPROM ? "yes" : "no");
	EsPrint("\t\tMAC address: %X:%X:%X:%X:%X:%X.\n", macAddress.d[0], macAddress.d[1], macAddress.d[2], 
			macAddress.d[3], macAddress.d[4], macAddress.d[5]);

	EsPrint("\t--- Registers ---\n");
	EsPrint("\t\tDevice control: %x.\n", RD_REGISTER_CTRL());
	EsPrint("\t\tDevice status: %x.\n", RD_REGISTER_STATUS());
	EsPrint("\t\tEEPROM data: %x.\n", RD_REGISTER_EECD());
	EsPrint("\t\tEEPROM read: %x.\n", RD_REGISTER_EERD());
	EsPrint("\t\tReceive descriptor base address: %x.\n", (uint64_t) RD_REGISTER_RDBAL() | ((uint64_t) RD_REGISTER_RDBAH() << 32));
	EsPrint("\t\tReceive descriptor length: %x.\n", RD_REGISTER_RDLEN());
	EsPrint("\t\tReceive descriptor head: %x.\n", RD_REGISTER_RDH());
	EsPrint("\t\tReceive descriptor tail: %x.\n", RD_REGISTER_RDT());
	EsPrint("\t\tReceive control: %x.\n", RD_REGISTER_RCTL());
	EsPrint("\t\tReceive MAC address: %x.\n", (uint64_t) RD_REGISTER_RAL() | ((uint64_t) RD_REGISTER_RAH() << 32));
	EsPrint("\t\tTransmit descriptor base address: %x.\n", (uint64_t) RD_REGISTER_TDBAL() | ((uint64_t) RD_REGISTER_TDBAH() << 32));
	EsPrint("\t\tTransmit descriptor length: %x.\n", RD_REGISTER_TDLEN());
	EsPrint("\t\tTransmit descriptor head: %x.\n", RD_REGISTER_TDH());
	EsPrint("\t\tTransmit descriptor tail: %x.\n", RD_REGISTER_TDT());
	EsPrint("\t\tTransmit control: %x.\n", RD_REGISTER_TCTL());
}

bool Controller::Transmit(void *dataVirtual, uintptr_t dataPhysical, size_t dataBytes) {
	if (!dataBytes) {
		KernelPanic("Controller::Transmit - dataBytes is zero.\n");
	}

	KMutexAcquire(&transmitMutex);
	EsDefer(KMutexRelease(&transmitMutex));

	// Get a next index to use for the transmit descriptor.

	uint32_t head = RD_REGISTER_TDH();
	uint32_t index = transmitTail;
	uint32_t tail = (index + 1) % TRANSMIT_DESCRIPTOR_COUNT;

	if (head == tail) {
		// Wait upto 20ms for the head to move.
		KTimeout timeout(20);
		while (!timeout.Hit() && (RD_REGISTER_TDH() == tail));
		head = RD_REGISTER_TDH();
	}

	if (head == tail) {
		KernelLog(LOG_ERROR, "I8254x", "transmit overrun", "Attempting to transmit a packet with the head at the same position as the tail.\n");
		return false;
	}

	// Free any unused transmit buffers.

	if (transmitBuffers[index]) {
		NetTransmitBufferReturn(transmitBuffers[index]);
	}

	for (uintptr_t i = tail; i != head; i = (i + 1) % TRANSMIT_DESCRIPTOR_COUNT) {
		if (transmitBuffers[i]) {
			NetTransmitBufferReturn(transmitBuffers[i]);
			transmitBuffers[i] = nullptr;
		}
	}

	// Set up transmit descriptor.

	transmitDescriptors[index].length = dataBytes;
	transmitDescriptors[index].status = 0;
	transmitDescriptors[index].address = dataPhysical;
	transmitDescriptors[index].command = (1 << 0 /* end of packet */) | (1 << 1 /* insert CRC in Ethernet packet */) | (1 << 3 /* report status */);

	transmitBuffers[index] = dataVirtual;

	if ((dataPhysical >> K_PAGE_BITS) != ((dataPhysical + dataBytes - 1) >> K_PAGE_BITS)) {
		KernelPanic("Controller::Transmit - Data spanned over page boundary.\n");
	}

	// Submit the transmit descriptor to the controller.

	transmitTail = tail;
	__sync_synchronize();
	WR_REGISTER_TDT(tail);

	return true;
}

void Controller::DispatchThread() {
	while (true) {
		KEvent *events[] = { &receiveEvent };
		KWaitEvents(events, 1);

		NetInterfaceSetConnected(this, RD_REGISTER_STATUS() & (1 << 1));

		uint32_t tail = receiveTail, head = RD_REGISTER_RDH();
		uintptr_t dispatchCount = 0;

		while (true) {
			uint32_t nextTail = (tail + 1) % RECEIVE_DESCRIPTOR_COUNT;

			if (nextTail == head) {
				// Keep the tail one behind the head, otherwise controller assumes queue is empty of usable slots.
				break;
			}

			if (~receiveDescriptors[nextTail].status & (1 << 0 /* descriptor done */)) {
				break;
			}

			tail = nextTail;

			uint16_t status = receiveDescriptors[tail].status;
			receiveDescriptors[tail].status = 0;

			if (~status & (1 << 1 /* end of packet */)) {
				KernelLog(LOG_ERROR, "I8254x", "clear EOP bit", "Received descriptor with clear end of packet bit; this is unsupported.\n");
				goto next;
			}

			if (receiveDescriptors[tail].errors) {
				KernelLog(LOG_ERROR, "I8254x", "received error", "Received descriptor with error bits %X set.\n", receiveDescriptors[tail].errors);
				goto next;
			}

			if (receiveDescriptors[tail].length < 60) {
				KernelLog(LOG_ERROR, "I8254x", "short packet", "Received descriptor with packet less than 60 bytes; this is unsupported.\n");
				goto next;
			}

			KernelLog(LOG_VERBOSE, "I8254x", "received packet", "Received packet at index %d with length %D.\n", tail, receiveDescriptors[tail].length);

			uint8_t *newVirtualAddress;
			uintptr_t newPhysicalAddress;

			if (!MMPhysicalAllocateAndMap(RECEIVE_BUFFER_SIZE + TRANSFER_BUFFER_EXTRA, 
						16, 64, false, 0, &newVirtualAddress, &newPhysicalAddress)) {
				// If we couldn't allocate memory, dispatch the buffer immediately.

				NetInterfaceReceive(this, receiveBuffers[tail], receiveDescriptors[tail].length, NET_PACKET_ETHERNET);
			} else {
				// Queue the buffer to be dispatched.

				dispatchBuffers[dispatchCount] = receiveBuffers[tail];
				dispatchByteCounts[dispatchCount] = receiveDescriptors[tail].length;
				dispatchPhysicalAddresses[dispatchCount] = receiveDescriptors[tail].address;
				dispatchCount++;

				receiveBuffers[tail] = newVirtualAddress;
				receiveDescriptors[tail].address = newPhysicalAddress;
			}

			next:;
		}

		__sync_synchronize();
		receiveTail = tail;
		WR_REGISTER_RDT(tail);

		for (uintptr_t i = 0; i < dispatchCount; i++) {
			NetInterfaceReceive(this, dispatchBuffers[i], dispatchByteCounts[i], NET_PACKET_ETHERNET);
			MMFree(MMGetKernelSpace(), dispatchBuffers[i], RECEIVE_BUFFER_SIZE + TRANSFER_BUFFER_EXTRA);
			MMPhysicalFree(dispatchPhysicalAddresses[i], false, (RECEIVE_BUFFER_SIZE + TRANSFER_BUFFER_EXTRA + K_PAGE_SIZE - 1) / K_PAGE_SIZE);
		}
	}
}

bool Controller::HandleIRQ() {
	uint32_t cause = RD_REGISTER_ICR();

	if (!cause) {
		return false;
	}

	KernelLog(LOG_VERBOSE, "I8254x", "received IRQ", "Received IRQ with cause %x.\n", cause);

	if (cause & (1 << 2)) {
		KernelLog(LOG_INFO, "I8254x", "link status change", "Link is now %z.\n", 
				(RD_REGISTER_STATUS() & (1 << 1)) ? "up" : "down");
		KEventSet(&receiveEvent, false, true);
	}

	if (cause & (1 << 6)) {
		KernelLog(LOG_ERROR, "I8254x", "receive underrun", "Controller reported receive underrun; packets have been lost.\n");
	}

	if (cause & ((1 << 6) | (1 << 7) | (1 << 4))) {
		KEventSet(&receiveEvent, false, true);
	}

	return true;
}

uint32_t Controller::Read(uintptr_t offset) {
	if (pci->baseAddresses[0] & 1) {
		pci->WriteBAR32(0, 0, offset);
		return pci->ReadBAR32(0, 4);
	} else {
		return pci->ReadBAR32(0, offset);
	}
}

void Controller::Write(uintptr_t offset, uint32_t value) {
	if (pci->baseAddresses[0] & 1) {
		pci->WriteBAR32(0, 0, offset);
		pci->WriteBAR32(0, 4, value);
	} else {
		pci->WriteBAR32(0, offset, value);
	}
}

bool Controller::ReadEEPROM(uint8_t address, uint16_t *data) {
	if (!hasEEPROM) {
		KernelPanic("Controller::ReadEEPROM - EEPROM not present.\n");
	}

	WR_REGISTER_EERD(1 | ((uint32_t) address << 8));
	KTimeout timeout(20);
	while (!timeout.Hit() && (RD_REGISTER_EERD() & (1 << 4)));
	if (data) *data = RD_REGISTER_EERD() >> 16;
	return RD_REGISTER_EERD() & (1 << 4);
}

void Controller::Initialise() {
	KEvent wait = {};

	// Create a thread for dispatching received packets.

	receiveEvent.autoReset = true;

	if (!KThreadCreate("I8254xDispatch", [] (uintptr_t self) { ((Controller *) self)->DispatchThread(); }, (uintptr_t) this)) {
		KernelLog(LOG_ERROR, "I8254x", "thread error", "Could not create the dispatch thread.\n");
		return;
	}

	// Detect the EEPROM.

	hasEEPROM = true;
	hasEEPROM = ReadEEPROM(0, nullptr);

	// Reset the controller.

	uint32_t controlReset = RD_REGISTER_CTRL();

	WR_REGISTER_CTRL(controlReset | (1 << 31 /* PHY_RST */));
	RD_REGISTER_STATUS();
	KEventWait(&wait, 20 /* specification says minimum time is 10ms, double for safety */);

	WR_REGISTER_CTRL(controlReset | (1 << 26 /* RST */));
	RD_REGISTER_STATUS();
	KEventWait(&wait, 20);

	if (RD_REGISTER_CTRL() & (1 << 26)) {
		KernelLog(LOG_ERROR, "I8254x", "reset timeout", "Reset bit in control register did not clear after 20ms.\n");
		return;
	}

	// Configure the control register.

	uint32_t controlClearBits = (1 << 3 /* LRST */) | (1 << 31 /* PHY_RST */) | (1 << 7 /* ILOS */) | (1 << 30 /* VME */);
	uint32_t controlSetBits = (1 << 5 /* ASDE */) | (1 << 6 /* SLU */);
	WR_REGISTER_CTRL((RD_REGISTER_CTRL() & ~controlClearBits) | controlSetBits);

	// Allocate receive and transmit descriptors and their buffers.

	uintptr_t receiveDescriptorsPhysical, transmitDescriptorsPhysical;

	if (!MMPhysicalAllocateAndMap(sizeof(ReceiveDescriptor) * RECEIVE_DESCRIPTOR_COUNT, 16, 64, true, 
			0, (uint8_t **) &receiveDescriptors, &receiveDescriptorsPhysical)) {
		KernelLog(LOG_ERROR, "I8254x", "allocation failure", "Could not allocate receive descriptors.\n");
		return;
	}

	if (!MMPhysicalAllocateAndMap(sizeof(TransmitDescriptor) * TRANSMIT_DESCRIPTOR_COUNT, 16, 64, true, 
			0, (uint8_t **) &transmitDescriptors, &transmitDescriptorsPhysical)) {
		KernelLog(LOG_ERROR, "I8254x", "allocation failure", "Could not allocate transmit descriptors.\n");
		return;
	}

	for (uintptr_t i = 0; i < RECEIVE_DESCRIPTOR_COUNT; i++) {
		uintptr_t address;

		if (!MMPhysicalAllocateAndMap(RECEIVE_BUFFER_SIZE + TRANSFER_BUFFER_EXTRA, 
					16, 64, false, 0, receiveBuffers + i, &address)) {
			KernelLog(LOG_ERROR, "I8254x", "allocation failure", "Could not allocate receive buffers.\n");
			return;
		}

		receiveDescriptors[i].address = address;
	}

	// Disable flow control.

	WR_REGISTER_FCAL(0);
	WR_REGISTER_FCAH(0);
	WR_REGISTER_FCT(0);

	// Get the MAC address.

	if (hasEEPROM) {
		if (!ReadEEPROM(0, (uint16_t *) &macAddress.d[0]) 
				|| !ReadEEPROM(1, (uint16_t *) &macAddress.d[2])
				|| !ReadEEPROM(2, (uint16_t *) &macAddress.d[4])) {
			KernelLog(LOG_ERROR, "I8254x", "EEPROM error", "Could not read the MAC address from the EEPROM.\n");
			return;
		}
	} else {
		macAddress64 = ((uint64_t) RD_REGISTER_RAL() | ((uint64_t) RD_REGISTER_RAH() << 32)) & 0xFFFFFFFFFFFF;
	}

	// Enable interrupts and the register the handler.

	WR_REGISTER_IMC((1 << 17) - 1);
	WR_REGISTER_IMS((1 << 6 /* RXO */) | (1 << 7 /* RXT */) | (1 << 4 /* RXDMT */) | (1 << 2 /* LSC */));
	RD_REGISTER_ICR();

	if (!pci->EnableSingleInterrupt([] (uintptr_t, void *context) { return ((Controller *) context)->HandleIRQ(); }, this, "I8254x")) {
		KernelLog(LOG_ERROR, "I8254x", "IRQ registration failure", "Could not register IRQ %d.\n", pci->interruptLine);
		return;
	}

	// Setup receive registers.

	WR_REGISTER_RAL(macAddress64 & 0xFFFFFFFF);
	WR_REGISTER_RAH(((macAddress64 >> 32) & 0xFFFF) | (1 << 31 /* AV */));

	WR_REGISTER_RDBAL(receiveDescriptorsPhysical & 0xFFFFFFFF);
	WR_REGISTER_RDBAH(receiveDescriptorsPhysical >> 32);
	WR_REGISTER_RDLEN(sizeof(ReceiveDescriptor) * RECEIVE_DESCRIPTOR_COUNT);

	WR_REGISTER_RDH(0);
	WR_REGISTER_RDT(RECEIVE_DESCRIPTOR_COUNT - 1);
	receiveTail = RECEIVE_DESCRIPTOR_COUNT - 1;

	for (uintptr_t i = 0; i < 128; i++) {
		// Clear the multicast table array.
		WR_REGISTER_MTA(i, 0);
	}

	uint32_t receiveControlClearBits = (1 << 2) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 16) | (1 << 17) | (1 << 25) | (1 << 26);
	uint32_t receiveControlSetBits = (1 << 1) | (1 << 4) | (1 << 15);
	WR_REGISTER_RCTL((RD_REGISTER_RCTL() & ~receiveControlClearBits) | receiveControlSetBits);

	// Setup transmit registers.
	
	WR_REGISTER_TDBAL(transmitDescriptorsPhysical & 0xFFFFFFFF);
	WR_REGISTER_TDBAH(transmitDescriptorsPhysical >> 32);
	WR_REGISTER_TDLEN(sizeof(TransmitDescriptor) * TRANSMIT_DESCRIPTOR_COUNT);

	WR_REGISTER_TDH(0);
	WR_REGISTER_TDT(0);
	transmitTail = 0;

	// TODO Is the TCTL.COLD value correct?
	WR_REGISTER_TCTL((1 << 1) | (1 << 3) | (0x0F << 4) | (0x40 << 12));

	// Register the device.

	transmit = [] (NetInterface *self, void *dataVirtual, uintptr_t dataPhysical, size_t dataBytes) {
		return ((Controller *) self)->Transmit(dataVirtual, dataPhysical, dataBytes);
	};

	KRegisterNetInterface(this);

	if (RD_REGISTER_STATUS() & (1 << 1 /* link up */)) {
		NetInterfaceSetConnected(this, true);
	}
}

static void DeviceAttach(KDevice *_parent) {
	KPCIDevice *parent = (KPCIDevice *) _parent;

	Controller *device = (Controller *) KDeviceCreate("I8254x", parent, sizeof(Controller));
	if (!device) return;

	device->shutdown = [] (KDevice *device) {
		NetInterfaceShutdown((Controller *) device);

		// Wait a little bit for the transmit descriptors to be processed.
		// TODO Can this be done asynchronously?
		KEvent wait = {};
		KEventWait(&wait, 50);
	};

	parent->EnableFeatures(K_PCI_FEATURE_MEMORY_SPACE_ACCESS 
			| K_PCI_FEATURE_BUSMASTERING_DMA 
			| K_PCI_FEATURE_INTERRUPTS
			| K_PCI_FEATURE_IO_PORT_ACCESS
			| K_PCI_FEATURE_BAR_0);

	KernelLog(LOG_INFO, "I8254x", "found controller", "Found I8254x controller with ID %x.\n", parent->deviceID);

	device->pci = parent;
	device->Initialise();
	// device->DumpState();
}

KDriver driverI8254x = {
	.attach = DeviceAttach,
};
