// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

// TODO Event-based API for userland.

// TODO Locking: In NetTCPReceive, lock on getting task so it can't be destroyed by NetConnectionDestroy.

// TODO Limiting the size of the ARP table; LRU.
// TODO Sending ARP requests not working in VBox.

// TODO Domain name resolve button doesn't work in the test program.
// TODO Resolved domain name cache.

// TODO UDP and TCP: checking packets are received from the correct NetInterface, MAC and IP.
// TODO UDP and TCP: checking source port matches expected value on received packets.
// TODO UDP and TCP (and possibly others): lock in the NetTask callback when processing a received packet, 
// 	to allow for a NetInterface to have multiple dispatcher threads.
// TODO TCP: merging ACK responses.
// TODO TCP: high performance extensions.
// TODO TCP: reducing duplication of non-reply code.

// TODO Cancelling tasks after losing connection; retrying tasks; timeout tasks.
// TODO Retrying the NetAddressSetupTask if it completes with error.

// TODO Handling more ICMP messages.
// TODO Sending ICMP messages on certain bad packets.
// TODO IPv6.

// TODO Check receive buffer is treated as empty when read pointer == write pointer.

// TODO Make most log messages LOG_VERBOSE.

#ifndef IMPLEMENTATION

struct EthernetHeader {
	KMACAddress destinationMAC;
	KMACAddress sourceMAC;
	uint16_t type;
} ES_STRUCT_PACKED;

#define ETHERNET_HEADER(ethernet, _type, _destinationMAC) \
	if (ethernet) { \
		ethernet->destinationMAC = _destinationMAC; \
		ethernet->sourceMAC = interface->macAddress; \
		ethernet->type = SwapBigEndian16(_type); \
	}

#define ETHERNET_TYPE_IPV4 (0x0800)
#define ETHERNET_TYPE_ARP (0x0806)

struct IPHeader {
	uint8_t versionAndLength;
	uint8_t serviceType;
	uint16_t totalLength;
	uint16_t identification;
	uint16_t flagsAndFragmentOffset;
	uint8_t timeToLive;
	uint8_t protocol;
	uint16_t headerChecksum;
	KIPAddress sourceAddress;
	KIPAddress destinationAddress;

	uint16_t CalculateHeaderChecksum() const {
		const uint8_t *in = (const uint8_t *) this;
		uint32_t sum = 0;

		for (uintptr_t i = 0; i < 20; i += 2) {
			if (i == 10) continue;
			sum += ((uint16_t) in[i] << 8) + (uint16_t) in[i + 1];
		}

		while (sum > 0xFFFF) {
			sum = (sum >> 16) + (sum & 0xFFFF);
		}

		return SwapBigEndian16(~sum);
	}
} ES_STRUCT_PACKED;

#define IP_HEADER(ip, _destinationAddress, _protocol) \
	if (ip) { \
		ip->versionAndLength = (4 << 4) | (5 << 0); /* We're using IPv4 with a 5 DWORD header. */ \
		ip->identification = ByteSwap16(++interface->ipIdentification); \
		ip->timeToLive = 64; /* Live for at most 64 seconds. */ \
		ip->protocol = _protocol; \
		ip->sourceAddress = interface->ipAddress; \
		ip->destinationAddress = _destinationAddress; \
	}

#define IP_PROTOCOL_ICMP (1)
#define IP_PROTOCOL_TCP (6)
#define IP_PROTOCOL_UDP (17)

struct UDPHeader {
	uint16_t sourcePort;
	uint16_t destinationPort;
	uint16_t length;
	uint16_t checksum;

	uint16_t CalculateChecksum() const {
		const IPHeader *ipHeader = (const IPHeader *) this - 1;

		struct {
			KIPAddress sourceAddress;
			KIPAddress destinationAddress;
			uint8_t zero;
			uint8_t protocol;
			uint16_t udpLength;
		} pseudoHeader = {
			.sourceAddress = ipHeader->sourceAddress,
			.destinationAddress = ipHeader->destinationAddress,
			.zero = 0,
			.protocol = ipHeader->protocol,
			.udpLength = length,
		};

		const uint8_t *in = (const uint8_t *) this;
		const uint8_t *in2 = (const uint8_t *) &pseudoHeader;
		const uint8_t *data = (const uint8_t *) (this + 1);

		uint32_t sum = 0;

		for (uintptr_t i = 0; i < 8; i += 2) {
			if (i == 6) continue;
			sum += ((uint16_t) in[i] << 8) + (uint16_t) in[i + 1];
		}

		for (uintptr_t i = 0; i < 12; i += 2) {
			sum += ((uint16_t) in2[i] << 8) + (uint16_t) in2[i + 1];
		}

		uintptr_t dataBytes = ByteSwap16(length) - 8;

		for (uintptr_t i = 0; i < dataBytes; i += 2) {
			if (i + 1 == dataBytes) {
				sum += (uint16_t) data[i] << 8;
			} else {
				sum += ((uint16_t) data[i] << 8) + (uint16_t) data[i + 1];
			}
		}

		while (sum > 0xFFFF) {
			sum = (sum >> 16) + (sum & 0xFFFF);
		}

		return (sum == 0xFFFF) ? 0xFFFF : SwapBigEndian16(~sum);
	}
} ES_STRUCT_PACKED;

#define UDP_HEADER(udp, _sourcePort, _destinationPort) \
	if (udp) { \
		udp->sourcePort = SwapBigEndian16(_sourcePort); \
		udp->destinationPort = SwapBigEndian16(_destinationPort); \
	}

#define UDP_PORT_BASE (49152)

struct TCPHeader {
	uint16_t sourcePort;
	uint16_t destinationPort;
	uint32_t sequenceNumber;
	uint32_t ackNumber;
	uint16_t flags;
	uint16_t window;
	uint16_t checksum;
	uint16_t urgentPointer;

	uint16_t CalculateChecksum(uint16_t length) const {
		const IPHeader *ipHeader = (const IPHeader *) this - 1;

		struct {
			KIPAddress sourceAddress;
			KIPAddress destinationAddress;
			uint8_t zero;
			uint8_t protocol;
			uint16_t tcpLength;
		} pseudoHeader = {
			.sourceAddress = ipHeader->sourceAddress,
			.destinationAddress = ipHeader->destinationAddress,
			.zero = 0,
			.protocol = ipHeader->protocol,
			.tcpLength = ByteSwap16(length),
		};

		const uint8_t *in = (const uint8_t *) this;
		const uint8_t *in2 = (const uint8_t *) &pseudoHeader;

		uint32_t sum = 0;

		for (uintptr_t i = 0; i < length; i += 2) {
			if (i == 16) {
				// Checksum field; skip.
			} else if (i + 1 == length) {
				sum += (uint16_t) in[i] << 8;
			} else {
				sum += ((uint16_t) in[i] << 8) + (uint16_t) in[i + 1];
			}
		}

		for (uintptr_t i = 0; i < 12; i += 2) {
			sum += ((uint16_t) in2[i] << 8) + (uint16_t) in2[i + 1];
		}

		while (sum > 0xFFFF) {
			sum = (sum >> 16) + (sum & 0xFFFF);
		}

		return SwapBigEndian16(~sum);
	}
} ES_STRUCT_PACKED;

struct TCPReceivedData {
	uint16_t flags;
	uint16_t segmentLength;
	uint32_t ackNumber;
	uint32_t sequenceNumber;

	const EthernetHeader *ethernet;
	const IPHeader *ip;
	const TCPHeader *tcp;
	const void *segment;
};

// NOTE Keep these in order!
// We've sent a packet asking to start a connection. We're expecting the server to ACK it with a matching request.
#define TCP_STEP_SYN_SENT (1) 
// We've received a packet asking to start a connection. We've sent a matching request back. We're waiting for that to be ACK'd.
#define TCP_STEP_SYN_RECEIVED (2) 
// We're in normal data communication.
#define TCP_STEP_ESTABLISHED (3)
// Closing steps:
#define TCP_STEP_FIN_WAIT_1 (4)
#define TCP_STEP_FIN_WAIT_2 (5)
#define TCP_STEP_CLOSE_WAIT (6)
#define TCP_STEP_CLOSING (7)
#define TCP_STEP_LAST_ACK (8)

#define TCP_FIN (1 << 0)
#define TCP_SYN (1 << 1)
#define TCP_RST (1 << 2)
#define TCP_PSH (1 << 3)
#define TCP_ACK (1 << 4)

#define TCP_PORT_BASE (49152)

#define TCP_PREPARE_REPLY(_data1, _data1Bytes, _data2, _data2Bytes) \
	EthernetHeader *ethernetReply = (EthernetHeader *) buffer.Write(nullptr, sizeof(EthernetHeader)); \
	ETHERNET_HEADER(ethernetReply, ETHERNET_TYPE_IPV4, data->ethernet->sourceMAC); \
	IPHeader *ipReply = (IPHeader *) buffer.Write(nullptr, sizeof(IPHeader)); \
	IP_HEADER(ipReply, data->ip->sourceAddress, IP_PROTOCOL_TCP); \
	TCPHeader *tcpReply = (TCPHeader *) buffer.Write(nullptr, sizeof(TCPHeader)); \
	buffer.Write(_data1, _data1Bytes); \
	buffer.Write(_data2, _data2Bytes); \
	\
	if (buffer.error) { \
		KernelPanic("TCP_PREPARE_REPLY - Network interface buffer size too small.\n"); \
	} \
	\
	ipReply->totalLength = ByteSwap16(buffer.position - sizeof(*ethernetReply)); \
	ipReply->flagsAndFragmentOffset = SwapBigEndian16(1 << 14 /* do not fragment */); \
	ipReply->headerChecksum = ipReply->CalculateHeaderChecksum(); \
	\
	tcpReply->sourcePort = data->tcp->destinationPort; \
	tcpReply->destinationPort = data->tcp->sourcePort;

#define TCP_PREPARE_REPLY_2(_data1, _data1Bytes, _data2, _data2Bytes, _flags, _headerDWORDs) \
	TCP_PREPARE_REPLY(_data1, _data1Bytes, _data2, _data2Bytes); \
	tcpReply->flags = SwapBigEndian16((_flags) | ((_headerDWORDs) << 12 /* header is 5 DWORDs */)); \
	tcpReply->sequenceNumber = SwapBigEndian32(task->sendNext); \
	tcpReply->ackNumber = SwapBigEndian32(task->receiveNext); \
	tcpReply->window = SwapBigEndian16(task->receiveWindow); \

#define TCP_MAKE_STANDARD_REPLY(_flags) \
	{ \
		EsBuffer buffer = NetTransmitBufferGet(); \
		if (buffer.error) { NetTaskComplete(task, ES_ERROR_INSUFFICIENT_RESOURCES); return; } \
		TCP_PREPARE_REPLY_2(nullptr, 0, nullptr, 0, _flags, 5); \
		if (!NetTransmit(interface, &buffer, NET_PACKET_ETHERNET)) NetTaskComplete(task, ES_ERROR_INSUFFICIENT_RESOURCES); \
	}

struct DHCPHeader {
	uint8_t opCode;
	uint8_t hardwareAddressType;
	uint8_t hardwareAddressLength;
	uint8_t hops;
	uint32_t transactionID;
	uint16_t seconds;
	uint16_t flags;
	KIPAddress clientIPAddress;
	KIPAddress yourIPAddress;
	KIPAddress nextServerIPAddress;
	KIPAddress relayAgentIPAddress;
	uint8_t clientHardwareAddress[16];
	uint8_t serverHostName[64];
	uint8_t bootFileName[128];
	uint8_t optionsMagic[4];
} ES_STRUCT_PACKED; 

#define DHCP_HEADER(dhcp, _opCode) \
	if (dhcp) { \
		dhcp->opCode = _opCode; \
		dhcp->hardwareAddressType = 1; /* Ethernet. */ \
		dhcp->hardwareAddressLength = 6; \
		dhcp->transactionID = (uint32_t) EsRandomU64(); \
		EsMemoryCopy(&dhcp->clientHardwareAddress, &interface->macAddress, sizeof(KMACAddress)); \
		dhcp->optionsMagic[0] = 99; /* Magic cookie. */ \
		dhcp->optionsMagic[1] = 130; \
		dhcp->optionsMagic[2] = 83; \
		dhcp->optionsMagic[3] = 99; \
	}

#define DHCP_START() \
	EthernetHeader *ethernet = (EthernetHeader *) buffer.Write(nullptr, sizeof(EthernetHeader)); \
	ETHERNET_HEADER(ethernet, ETHERNET_TYPE_IPV4, broadcastMAC); \
	IPHeader *ip = (IPHeader *) buffer.Write(nullptr, sizeof(IPHeader)); \
	IP_HEADER(ip, broadcastIP, IP_PROTOCOL_UDP); \
	UDPHeader *udp = (UDPHeader *) buffer.Write(nullptr, sizeof(UDPHeader)); \
	UDP_HEADER(udp, 68, 67); \
	DHCPHeader *dhcp = (DHCPHeader *) buffer.Write(nullptr, sizeof(DHCPHeader)); \
	DHCP_HEADER(dhcp, DHCP_BOOTREQUEST);

#define DHCP_END() \
	if (!buffer.error) { \
		ip->totalLength = ByteSwap16(buffer.position - sizeof(*ethernet)); \
		udp->length = ByteSwap16(buffer.position - sizeof(*ethernet) - sizeof(*ip)); \
	}

#define DHCP_OPTION_MESSAGE_TYPE(x) 53, 1, (x)
#define DHCP_OPTION_REQUESTED_IP(x) 50, 4, (x).d[0], (x).d[1], (x).d[2], (x).d[3]
#define DHCP_OPTION_SERVER_IDENTIFIER(x) 54, 4, (x).d[0], (x).d[1], (x).d[2], (x).d[3]

#define DHCPDISCOVER (1)
#define DHCPOFFER (2)
#define DHCPREQUEST (3)
#define DHCPACK (5)
#define DHCPNAK (6)
#define DHCPRELEASE (7)

#define DHCP_BOOTREQUEST (1)
#define DHCP_BOOTREPLY (2)

struct ARPHeader {
	uint16_t hardwareAddressSpace;
	uint16_t protocolAddressSpace;
	uint8_t hardwareAddressLength;
	uint8_t protocolAddressLength;
	uint16_t opCode;
} ES_STRUCT_PACKED;

#define ARP_ETHERNET (0x0001)
#define ARP_IPV4 (0x0800)

#define ARP_REQUEST (1)
#define ARP_REPLY (2)

struct ARPEntry {
	KIPAddress ip;
	KMACAddress mac;
};

struct ARPRequest {
	KIPAddress ip;
	NetTask *task;
};

struct ICMPHeader {
	uint8_t type;
	uint8_t code;
	uint16_t checksum;

	uint16_t CalculateChecksum(size_t dataBytes) const {
		const uint8_t *in = (const uint8_t *) this;
		uint32_t sum = 0;

		for (uintptr_t i = 0; i < 4 + dataBytes; i += 2) {
			if (i == 2) continue;
			sum += ((uint16_t) in[i] << 8) + (uint16_t) in[i + 1];
		}

		while (sum > 0xFFFF) {
			sum = (sum >> 16) + (sum & 0xFFFF);
		}

		return SwapBigEndian16(~sum);
	}
} ES_STRUCT_PACKED;

struct DNSHeader {
	uint16_t identifier;
	uint16_t flags;
	uint16_t questionCount;
	uint16_t answerCount;
	uint16_t authorityCount;
	uint16_t additionalCount;
} ES_STRUCT_PACKED;

struct Networking {
	KMutex interfacesListMutex;
	SimpleList interfaces;

#define MAX_UDP_TASKS (1024)
	NetTask *udpTasks[MAX_UDP_TASKS]; 
	KMutex udpTaskBitsetMutex;
	Bitset udpTaskBitset;

#define MAX_TCP_TASKS (1024)
	uintptr_t tcpTasks[MAX_TCP_TASKS]; // If (1 << 0) set, task is in use.
	uint16_t tcpTaskLRU, tcpTaskMRU;
	KMutex tcpTaskListMutex;

	KMutex echoRequestTaskMutex;
	NetTask *echoRequestTask;

	KMutex transmitBufferPoolMutex;
	EsArena transmitBufferPool;
};

struct NetDomainNameResolveTask : NetTask {
	const char *name;
	size_t nameBytes;
	EsAddress *address;
	uint16_t identifier;
	KEvent *event;
};

struct NetEchoRequestTask : NetTask {
	uint8_t *data;
	EsAddress *address;
	KEvent *event;
};

struct NetTCPConnectionTask : NetTask {
	uint32_t sendUnacknowledged; // Points at the end of the data the server has acknowledged receiving from us.
	uint32_t sendNext; // Points at the end of data we've sent.
	uint32_t sendWindow; // The maximum distance sendNext can be past sendUnacknowledged.
	uint32_t receiveNext; // Points at the end of data we've acknowledged receiving from the server.
	uint16_t receiveWindow; // The maximum distance the server can sent data past receiveNext.

	uint32_t initialSend, initialReceive;
	uint32_t finSequence;
	uint32_t sendWL1;
	uint32_t sendWL2;

	KMACAddress destinationMAC;
};

struct NetConnection {
	NetTCPConnectionTask task;

	MMSharedRegion *bufferRegion;
	uint8_t *sendBuffer;
	uint8_t *receiveBuffer;
	size_t sendBufferBytes;
	size_t receiveBufferBytes;

	uintptr_t sendReadPointer; // The end of the data that we've sent to the server (possibly unacknolwedged).
	uintptr_t sendWritePointer; // The end of the data that the application has written for us to send.
	uintptr_t receiveWritePointer; // The end of the data that we've received from the server with no missing segments.
	uintptr_t receiveReadPointer; // The end of the data that the user has processed from the receive buffer.

	RangeSet receivedData;

	EsAddress address;
	KMutex mutex;

	volatile uintptr_t handles;
};

void NetDomainNameResolve(NetTask *_task, void *data);
void NetEchoRequest(NetTask *_task, void *data);
void NetTCPConnection(NetTask *_task, void *data);
void NetAddressSetup(NetTask *_task, void *data);

NetConnection *NetConnectionOpen(EsAddress *address, size_t sendBufferBytes, size_t receiveBufferBytes, uint32_t flags);
void NetConnectionClose(NetConnection *connection);
void NetConnectionNotify(NetConnection *connection, uintptr_t sendWritePointer, uintptr_t receiveReadPointer);
void NetConnectionDestroy(NetConnection *connection);

extern Networking networking;

#else

Networking networking;

const KIPAddress broadcastIP = { 255, 255, 255, 255 };
const KMACAddress broadcastMAC = { 255, 255, 255, 255, 255, 255 };

void NetPrintPacket(const char *cName, const void *packet, size_t bytes) {
	EsPrint("%z packet: ", cName);

	for (uintptr_t i = 0; i < bytes; i++) {
		EsPrint("%X ", ((uint8_t *) packet)[i]);
	}

	EsPrint("\n");
}

EsBuffer NetTransmitBufferGet() {
	KMutexAcquire(&networking.transmitBufferPoolMutex);
	EsBuffer buffer = {};
	buffer.out = (uint8_t *) EsArenaAllocate(&networking.transmitBufferPool, false);
	buffer.bytes = networking.transmitBufferPool.slotSize;

	if (!buffer.out) {
		buffer.error = true, buffer.bytes = 0;
		KernelLog(LOG_ERROR, "Networking", "out of memory", "Could not allocate a transmit buffer.\n");
	}

	KMutexRelease(&networking.transmitBufferPoolMutex);
	return buffer;
}

void NetTransmitBufferReturn(void *data) {
	KMutexAcquire(&networking.transmitBufferPoolMutex);
	EsArenaFree(&networking.transmitBufferPool, data);
	KMutexRelease(&networking.transmitBufferPoolMutex);
}

bool NetTransmit(NetInterface *interface, EsBuffer *buffer, NetPacketType packetType) {
	if (buffer->error) {
		KernelPanic("NetTransmit - Trying to transmit a write buffer with errors.\n");
	}

	if (packetType == NET_PACKET_ETHERNET) {
		if (buffer->position < 64) {
			buffer->Write(nullptr, 64 - buffer->position);
		}

		EthernetHeader *ethernet = (EthernetHeader *) buffer->out;

		if (ethernet->type == SwapBigEndian16(ETHERNET_TYPE_IPV4)) {
			IPHeader *ip = (IPHeader *) (ethernet + 1);

			if (ip->protocol == IP_PROTOCOL_UDP) {
				UDPHeader *udp = (UDPHeader *) (ip + 1);
				udp->checksum = udp->CalculateChecksum();
			} else if (ip->protocol == IP_PROTOCOL_TCP) {
				TCPHeader *tcp = (TCPHeader *) (ip + 1);
				tcp->checksum = tcp->CalculateChecksum(ByteSwap16(ip->totalLength) - sizeof(*ip));
			} else if (ip->protocol == IP_PROTOCOL_ICMP) {
				ICMPHeader *icmp = (ICMPHeader *) (ip + 1);
				icmp->checksum = icmp->CalculateChecksum(ByteSwap16(ip->totalLength) - sizeof(*ip));
			}

			ip->headerChecksum = ip->CalculateHeaderChecksum();
		}
	}

	uintptr_t address = (uintptr_t) buffer->out;
	uintptr_t physical = (address & (K_PAGE_SIZE - 1)) + MMArchTranslateAddress(kernelMMSpace, address); 

	if (!interface->transmit(interface, buffer->out, physical, buffer->position)) {
		NetTransmitBufferReturn(buffer->out);
		return false;
	}

	return true;
}

bool NetARPLookup(NetTask *task, KIPAddress targetIP, KMACAddress *targetMAC) {
	NetInterface *interface = task->interface;

	KWriterLockTake(&interface->arpTableLock, K_LOCK_SHARED);

	for (uintptr_t i = 0; i < interface->arpTable.Length(); i++) {
		if (0 == EsMemoryCompare(&interface->arpTable[i].ip, &targetIP, sizeof(KIPAddress))) {
			*targetMAC = interface->arpTable[i].mac;
			KWriterLockReturn(&interface->arpTableLock, K_LOCK_SHARED);
			return true;
		}
	}

	KWriterLockReturn(&interface->arpTableLock, K_LOCK_SHARED);

	KernelLog(LOG_INFO, "Networking", "send ARP", "Sending ARP to find MAC address of IP %d.%d.%d.%d.\n",
			targetIP.d[0], targetIP.d[1], targetIP.d[2], targetIP.d[3]);

	// TODO Prevent sending multiple requests for a given MAC address at the same time.

	EsBuffer buffer = NetTransmitBufferGet();

	if (buffer.error) {
		NetTaskComplete(task, ES_ERROR_INSUFFICIENT_RESOURCES);
		return false;
	}

	EthernetHeader *ethernet = (EthernetHeader *) buffer.Write(nullptr, sizeof(EthernetHeader));
	ETHERNET_HEADER(ethernet, ETHERNET_TYPE_ARP, broadcastMAC);
	ARPHeader *arp = (ARPHeader *) buffer.Write(nullptr, sizeof(ARPHeader));

	if (arp) {
		arp->hardwareAddressSpace = SwapBigEndian16(ARP_ETHERNET);
		arp->protocolAddressSpace = SwapBigEndian16(ARP_IPV4);
		arp->hardwareAddressLength = 6;
		arp->protocolAddressLength = 4;
		arp->opCode = SwapBigEndian16(ARP_REQUEST);
	}

	buffer.Write(&interface->macAddress, sizeof(KMACAddress));
	buffer.Write(&interface->ipAddress, sizeof(KIPAddress));
	buffer.Write(nullptr, sizeof(KMACAddress));
	buffer.Write(&targetIP, sizeof(KIPAddress));

	if (buffer.error) {
		KernelPanic("NetARPLookup - Network interface buffer size too small.\n");
	}

	KWriterLockTake(&interface->arpTableLock, K_LOCK_EXCLUSIVE);

	if (!NetTransmit(interface, &buffer, NET_PACKET_ETHERNET)) {
		NetTaskComplete(task, ES_ERROR_INSUFFICIENT_RESOURCES);
	} else {
		ARPRequest request = { targetIP, task };
		interface->arpRequests.Add(request);
	}

	KWriterLockReturn(&interface->arpTableLock, K_LOCK_EXCLUSIVE);

	return false;
}

void NetARPReceive(NetInterface *interface, EsBuffer *buffer) {
	const ARPHeader *arp = (const ARPHeader *) buffer->Read(sizeof(ARPHeader));

	if (!arp) {
		KernelLog(LOG_ERROR, "Networking", "bad packet", "Missing ARP header.\n");
		return;
	}

	if (SwapBigEndian16(arp->hardwareAddressSpace) != ARP_ETHERNET
			|| SwapBigEndian16(arp->protocolAddressSpace) != ARP_IPV4) {
		KernelLog(LOG_ERROR, "Networking", "ignored packet", "ARP packet has unrecognised address space(s).\n");
		return;
	}

	const KMACAddress *senderMAC = (const KMACAddress *) buffer->Read(sizeof(KMACAddress));
	const KIPAddress *senderIP = (const KIPAddress *) buffer->Read(sizeof(KIPAddress));
	const KMACAddress *targetMAC = (const KMACAddress *) buffer->Read(sizeof(KMACAddress));
	const KIPAddress *targetIP = (const KIPAddress *) buffer->Read(sizeof(KIPAddress));

	if (!senderMAC || !senderIP || !targetMAC || !targetIP) {
		KernelLog(LOG_ERROR, "Networking", "bad packet", "ARP packet too short.\n");
		return;
	}

	KernelLog(LOG_INFO, "Networking", "received ARP packet",
			"Received ARP packet. Sender: %d.%d.%d.%d (%X:%X:%X:%X:%X:%X). Destination: %d.%d.%d.%d (%X:%X:%X:%X:%X:%X). Op code %d.\n",
			senderIP->d[0], senderIP->d[1], senderIP->d[2], senderIP->d[3],
			senderMAC->d[0], senderMAC->d[1], senderMAC->d[2], senderMAC->d[3], senderMAC->d[4], senderMAC->d[5],
			targetIP->d[0], targetIP->d[1], targetIP->d[2], targetIP->d[3],
			targetMAC->d[0], targetMAC->d[1], targetMAC->d[2], targetMAC->d[3], targetMAC->d[4], targetMAC->d[5],
			SwapBigEndian16(arp->opCode));

	bool merged = false;

	Array<NetTask *, K_FIXED> completedRequests = {};

	KWriterLockTake(&interface->arpTableLock, K_LOCK_EXCLUSIVE);

	for (uintptr_t i = 0; i < interface->arpTable.Length(); i++) {
		if (0 == EsMemoryCompare(&interface->arpTable[i].ip, senderIP, sizeof(KIPAddress))) {
			interface->arpTable[i].mac = *senderMAC;
			merged = true;
		}
	}
	
	if (interface->hasIP && 0 == EsMemoryCompare(targetIP, &interface->ipAddress, sizeof(KIPAddress))) {
		if (!merged) {
			ARPEntry entry = {};
			entry.ip = *senderIP;
			entry.mac = *senderMAC;
			
			if (!interface->arpTable.Add(entry)) {
				KernelLog(LOG_ERROR, "Networking", "allocation error", "Could not add entry to ARP table.\n");
			}

			for (uintptr_t i = 0; i < interface->arpRequests.Length(); i++) {
				if (0 == EsMemoryCompare(&interface->arpRequests[i].ip, &entry.ip, sizeof(KIPAddress))) {
					completedRequests.Add(interface->arpRequests[i].task);
					interface->arpRequests.DeleteSwap(i);
					i--;
				}
			}
		}

		if (SwapBigEndian16(arp->opCode) == ARP_REQUEST) {
			// Reply with our MAC address.
			EsBuffer buffer = NetTransmitBufferGet();

			if (!buffer.error) {
				EthernetHeader *ethernet = (EthernetHeader *) buffer.Write(nullptr, sizeof(EthernetHeader));
				ETHERNET_HEADER(ethernet, ETHERNET_TYPE_ARP, broadcastMAC);
				ARPHeader *arpReply = (ARPHeader *) buffer.Write(arp, sizeof(ARPHeader));
				buffer.Write(&interface->macAddress, sizeof(KMACAddress));
				buffer.Write(targetIP, sizeof(KIPAddress));
				buffer.Write(senderMAC, sizeof(KMACAddress));
				buffer.Write(senderIP, sizeof(KIPAddress));

				if (buffer.error) {
					KernelPanic("NetARPReceive - Network interface buffer size too small.\n");
				} else {
					arpReply->opCode = ARP_REPLY;
					NetTransmit(interface, &buffer, NET_PACKET_ETHERNET); // Don't care about errors.
				}
			}
		} else if (SwapBigEndian16(arp->opCode) == ARP_REPLY) {
			// Don't need to do anything.
		} else {
			KernelLog(LOG_ERROR, "Networking", "ignored packet", "Unrecognised ARP op code %d.\n", SwapBigEndian16(arp->opCode));
		}
	} else {
		KernelLog(LOG_ERROR, "Networking", "ignored packet", "ARP packet not destined for us.\n");
	}

	KWriterLockReturn(&interface->arpTableLock, K_LOCK_EXCLUSIVE);

	for (uintptr_t i = 0; i < completedRequests.Length(); i++) {
		completedRequests[i]->callback(completedRequests[i], nullptr);
	}

	completedRequests.Free();
}

void NetICMPReceive(NetInterface *interface, EsBuffer *buffer, const IPHeader *ip, const EthernetHeader *ethernet) {
	const ICMPHeader *icmp = (const ICMPHeader *) buffer->Read(sizeof(ICMPHeader));

	if (!icmp) {
		KernelLog(LOG_ERROR, "Networking", "bad packet", "Missing ICMP header.\n");
		return;
	}

	if (icmp->checksum != icmp->CalculateChecksum(buffer->bytes - buffer->position)) {
		KernelLog(LOG_ERROR, "Networking", "bad packet", "Incorrect ICMP checksum.\n");
		return;
	}

	if (icmp->type == 8 /* echo request */) {
		// Send echo reply.
		// TODO Test this.

		EsBuffer bufferReply = NetTransmitBufferGet();
		if (bufferReply.error) return;

		EthernetHeader *ethernetReply = (EthernetHeader *) bufferReply.Write(nullptr, sizeof(EthernetHeader));
		ETHERNET_HEADER(ethernetReply, ETHERNET_TYPE_IPV4, ethernet->sourceMAC);
		IPHeader *ipReply = (IPHeader *) bufferReply.Write(nullptr, sizeof(IPHeader));
		IP_HEADER(ipReply, ip->sourceAddress, IP_PROTOCOL_ICMP);
		ICMPHeader *icmpReply = (ICMPHeader *) bufferReply.Write(icmp, sizeof(ICMPHeader));
		bufferReply.Write(icmp + 1, buffer->bytes - buffer->position);

		if (bufferReply.error) {
			KernelPanic("NetICMPReceive - Network interface buffer size too small.\n");
		} else {
			icmpReply->type = 0; // Echo reply.
			ipReply->totalLength = ByteSwap16(bufferReply.position - sizeof(*ethernetReply));
			NetTransmit(interface, &bufferReply, NET_PACKET_ETHERNET); // Don't care about errors.
		}
	} else if (icmp->type == 0 /* echo reply */) {
		if (networking.echoRequestTask) {
			networking.echoRequestTask->callback(networking.echoRequestTask, buffer);
		} else {
			KernelLog(LOG_ERROR, "Networking", "ignored packet", "Unexpected ICMP echo reply.\n");
		}
	}
}

void NetTCPReceive(NetInterface *interface, EsBuffer *buffer, const IPHeader *ip, const EthernetHeader *ethernet) {
	// Validate the TCP header.

	size_t tcpPosition = buffer->position;
	const TCPHeader *tcp = (const TCPHeader *) buffer->Read(sizeof(TCPHeader));

	if (!tcp) {
		KernelLog(LOG_ERROR, "Networking", "bad packet", "Missing TCP header.\n");
		return;
	}

	uint16_t flags = SwapBigEndian16(tcp->flags);
	uint32_t headerDWORDs = flags >> 12;

	if (headerDWORDs < 5) {
		KernelLog(LOG_ERROR, "Networking", "bad packet", "TCP header DWORDs was less than 5.\n");
		return;
	}

	if (tcp->CalculateChecksum(buffer->bytes - tcpPosition) != tcp->checksum) {
		KernelLog(LOG_ERROR, "Networking", "bad packet", "TCP header has incorrect checksum.\n");
		return;
	}

	if (!buffer->Read((headerDWORDs - 5) * sizeof(uint32_t))) {
		KernelLog(LOG_ERROR, "Networking", "bad packet", "TCP header is shorter than expected.\n");
		return;
	}

	uint32_t segmentLength = buffer->bytes - buffer->position;

	// If a task exists at the destination port, send it the segment.
	// TODO Locking on getting the task.

	NetTCPConnectionTask *task = nullptr;
	uint16_t destinationPort = SwapBigEndian16(tcp->destinationPort);

	if (destinationPort >= TCP_PORT_BASE 
			&& destinationPort < TCP_PORT_BASE + MAX_TCP_TASKS
			&& (networking.tcpTasks[destinationPort - TCP_PORT_BASE] & 1)) {
		task = (NetTCPConnectionTask *) (networking.tcpTasks[destinationPort - TCP_PORT_BASE] & ~1);
		OpenHandleToObject(task, KERNEL_OBJECT_CONNECTION);
	}

	TCPReceivedData _data = {};
	_data.ethernet = ethernet;
	_data.ip = ip;
	_data.tcp = tcp;
	_data.flags = flags;
	_data.segmentLength = segmentLength;
	_data.segment = buffer->Read(segmentLength);
	_data.sequenceNumber = SwapBigEndian32(tcp->sequenceNumber);
	_data.ackNumber = SwapBigEndian32(tcp->ackNumber);
	TCPReceivedData *data = &_data;

	if (task) {
		task->callback(task, data);
		CloseHandleToObject(task, KERNEL_OBJECT_CONNECTION);
		return;
	}

	// The destination port is closed.
	// If the segment does not have the RST flag, then we should send a RST segment in response.

	if (~flags & (1 << 2 /* RST */)) {
		EsBuffer buffer = NetTransmitBufferGet();

		if (buffer.error) {
			return; // Ignore.
		}

		TCP_PREPARE_REPLY(nullptr, 0, nullptr, 0);

		tcpReply->sourcePort = tcp->destinationPort;
		tcpReply->destinationPort = tcp->sourcePort;
		tcpReply->flags = SwapBigEndian16(TCP_RST | ~(flags & TCP_ACK) | (5 << 12 /* header is 5 DWORDs */));
		tcpReply->sequenceNumber = (flags & TCP_ACK) ? tcp->ackNumber : 0;
		tcpReply->ackNumber = (flags & TCP_ACK) ? 0 : SwapBigEndian32(SwapBigEndian32(tcp->sequenceNumber) + segmentLength);

		if (!NetTransmit(interface, &buffer, NET_PACKET_ETHERNET)) {
			NetTaskComplete(task, ES_ERROR_INSUFFICIENT_RESOURCES);
		}
	}
}

void NetUDPReceive(NetInterface *interface, EsBuffer *buffer) {
	const UDPHeader *udp = (const UDPHeader *) buffer->Read(sizeof(UDPHeader));

	if (!udp) {
		KernelLog(LOG_ERROR, "Networking", "bad packet", "Missing UDP header.\n");
		return;
	}

	if (!interface->hasIP && udp->destinationPort != SwapBigEndian16(68)) {
		KernelLog(LOG_ERROR, "Networking", "ignored packet", "Only expecting DHCP packets until IP address accepted.\n");
		return;
	}

	if (SwapBigEndian16(udp->length) > buffer->bytes - buffer->position + sizeof(UDPHeader)) {
		KernelLog(LOG_ERROR, "Networking", "bad packet", "UDP length was longer than remaining buffer size.\n");
		return;
	}

	if (udp->CalculateChecksum() != udp->checksum) { // NOTE Don't compute the checksum until the length field has been validated!
		KernelLog(LOG_ERROR, "Networking", "bad packet", "Incorrect checksum in UDP header.\n");
		return;
	}

	uint16_t destinationPort = SwapBigEndian16(udp->destinationPort);

	if (destinationPort == 68 /* DHCP */) {
		interface->addressSetupTask.callback(&interface->addressSetupTask, buffer);
	} else if (destinationPort >= UDP_PORT_BASE 
			&& destinationPort < UDP_PORT_BASE + MAX_UDP_TASKS
			&& !networking.udpTaskBitset.Read(destinationPort - UDP_PORT_BASE)) {
		NetTask *task = networking.udpTasks[destinationPort - UDP_PORT_BASE];
		task->callback(task, buffer);
	} else {
		KernelLog(LOG_ERROR, "Networking", "ignored packet", "Unknown destination UDP port %d.\n", udp->destinationPort);
	}
}

void NetIPReceive(NetInterface *interface, EsBuffer *buffer, const EthernetHeader *ethernet) {
	const IPHeader *ip = (const IPHeader *) buffer->Read(sizeof(IPHeader));

	if (!ip) {
		KernelLog(LOG_ERROR, "Networking", "bad packet", "Missing IP header.\n");
		return;
	}

	if ((ip->versionAndLength >> 4) != 4) {
		KernelLog(LOG_ERROR, "Networking", "bad packet", "Unsupported IP version %d.\n", ip->versionAndLength >> 4);
		return;
	}

	size_t headerLength = (ip->versionAndLength & 0xF) * 4;

	if (headerLength < 20) {
		KernelLog(LOG_ERROR, "Networking", "bad packet", "IP header too short.\n");
		return;
	}

	uint16_t totalLength = SwapBigEndian16(ip->totalLength);

	if (totalLength > buffer->bytes - buffer->position + sizeof(IPHeader)) {
		KernelLog(LOG_ERROR, "Networking", "bad packet", "IP header total length was longer than remaining buffer size.\n");
		return;
	}

	buffer->bytes = buffer->position + totalLength - sizeof(IPHeader);

	if (totalLength < headerLength) {
		KernelLog(LOG_ERROR, "Networking", "bad packet", "IP header total length was less than header length.\n");
		return;
	}

	if (interface->hasIP && EsMemoryCompare(&interface->ipAddress, &ip->destinationAddress, 4) 
			&& EsMemoryCompare(&broadcastIP, &ip->destinationAddress, 4)) {
		KernelLog(LOG_ERROR, "Networking", "ignored packet", "Destination IP address mismatch (%d.%d.%d.%d).\n", 
				ip->destinationAddress.d[0], ip->destinationAddress.d[1], ip->destinationAddress.d[2], ip->destinationAddress.d[3]);
		return;
	}

	if (ip->CalculateHeaderChecksum() != ip->headerChecksum) {
		KernelLog(LOG_ERROR, "Networking", "bad packet", "Incorrect checksum in IP header.\n");
		return;
	}

	buffer->Read(headerLength - 20);

	if (!interface->hasIP && ip->protocol != IP_PROTOCOL_UDP) {
		KernelLog(LOG_ERROR, "Networking", "ignored packet", "Only expecting DHCP packets over UDP until IP address accepted.\n");
		return;
	}

	if (ip->protocol == IP_PROTOCOL_ICMP) {
		NetICMPReceive(interface, buffer, ip, ethernet);
	} else if (ip->protocol == IP_PROTOCOL_TCP) {
		NetTCPReceive(interface, buffer, ip, ethernet);
	} else if (ip->protocol == IP_PROTOCOL_UDP) {
		NetUDPReceive(interface, buffer);
	} else {
		KernelLog(LOG_ERROR, "Networking", "ignored packet", "Unrecognised IP protocol type %d.\n", ip->protocol);
	}
}

void NetEthernetReceive(NetInterface *interface, EsBuffer *buffer) {
	const EthernetHeader *ethernet = (const EthernetHeader *) buffer->Read(sizeof(EthernetHeader));

	if (!ethernet) {
		KernelLog(LOG_ERROR, "Networking", "bad packet", "Missing ethernet header.\n");
		return;
	}

	if (EsMemoryCompare(&ethernet->destinationMAC, &interface->macAddress, sizeof(KMACAddress))
			&& EsMemoryCompare(&ethernet->destinationMAC, &broadcastMAC, sizeof(KMACAddress))) {
		KernelLog(LOG_ERROR, "Networking", "ignored packet", "Destination MAC address mismatch.\n");
		return;
	}

	if (SwapBigEndian16(ethernet->type) == ETHERNET_TYPE_IPV4) {
		NetIPReceive(interface, buffer, ethernet);
	} else if (SwapBigEndian16(ethernet->type) == ETHERNET_TYPE_ARP) {
		NetARPReceive(interface, buffer);
	} else {
		KernelLog(LOG_ERROR, "Networking", "ignored packet", "Unrecognised ethernet packet type %x.\n", SwapBigEndian16(ethernet->type));
	}
}

void NetInterfaceReceive(NetInterface *interface, const uint8_t *data, size_t dataBytes, NetPacketType packetType) {
	KWriterLockTake(&interface->connectionLock, K_LOCK_SHARED);

	EsBuffer buffer = { .in = data, .bytes = dataBytes };

	if (!interface->connected) {
		KernelLog(LOG_ERROR, "Networking", "packet while disconnected", "Interface %x is disconnected.\n", interface);
	} else if (packetType == NET_PACKET_ETHERNET) {
		NetEthernetReceive(interface, &buffer);
	} else {
		KernelLog(LOG_ERROR, "Networking", "ignored packet", "Unsupported packet type %d.\n", packetType);
	}

	KWriterLockReturn(&interface->connectionLock, K_LOCK_SHARED);

	if (interface->addressSetupTask.changedState) {
		interface->addressSetupTask.changedState = false;
		KWriterLockTake(&interface->connectionLock, K_LOCK_EXCLUSIVE);

		interface->hasIP = interface->addressSetupTask.error == ES_SUCCESS;

		if (!interface->hasIP) {
			// TODO Report connection lost and cancel in progress tasks.
			// TODO Retry the addressSetupTask.
			KernelLog(LOG_INFO, "Networking", "interface disconnected", "NetInterfaceReceive - Interface %x has lost IP address.\n", interface);
		}

		KWriterLockReturn(&interface->connectionLock, K_LOCK_EXCLUSIVE);
	}
}

void NetInterfaceShutdown(NetInterface *interface) {
	if (!interface->hasIP) {
		return;
	}

	// Release our IP address.

	EsBuffer buffer = NetTransmitBufferGet();
	if (buffer.error) return;
	DHCP_START();

	uint8_t dhcpOptions[] = {
		DHCP_OPTION_MESSAGE_TYPE(DHCPRELEASE),
		DHCP_OPTION_SERVER_IDENTIFIER(interface->serverIdentifier),
		255, // End of options.
	};

	buffer.Write(dhcpOptions, sizeof(dhcpOptions));
	DHCP_END();

	if (buffer.error) {
		KernelPanic("NetInterface::ReceiveDHCPReply - Network interface buffer size too small.\n");
	}

	NetTransmit(interface, &buffer, NET_PACKET_ETHERNET); // Don't care about errors.
}

void NetInterfaceSetConnected(NetInterface *interface, bool connected) {
	if (interface->connected == connected) {
		return;
	}

	KWriterLockTake(&interface->connectionLock, K_LOCK_EXCLUSIVE);

	interface->connected = connected;

	if (!connected) {
		// TODO Report connection lost and cancel in progress tasks.
		KernelLog(LOG_INFO, "Networking", "interface disconnected", "NetInterface::SetConnected - Interface %x has disconnected.\n", interface);
		interface->hasIP = false;
		interface->arpTable.SetLength(0);
		interface->arpRequests.SetLength(0);
	} else {
		KernelLog(LOG_INFO, "Networking", "interface connected", "NetInterface::SetConnected - Interface %x has connected. Requesting an IP address...\n", interface);
	}

	KWriterLockReturn(&interface->connectionLock, K_LOCK_EXCLUSIVE);

	if (connected) {
		NetTaskBegin(&interface->addressSetupTask);
	}
}

void NetDomainNameResolve(NetTask *_task, void *_buffer) {
	EsBuffer *buffer = (EsBuffer *) _buffer;
	NetDomainNameResolveTask *task = (NetDomainNameResolveTask *) _task;
	NetInterface *interface = task->interface;

	if (task->completed) {
		if (task->index != 0xFFFF) {
			KMutexAcquire(&networking.udpTaskBitsetMutex);
			networking.udpTaskBitset.Put(task->index);
			KMutexRelease(&networking.udpTaskBitsetMutex);
		}

		if (task->event) {
			// This must be the last thing we do, otherwise the NetTask might be freed.
			KEventSet(task->event);
		}
	} else if (task->step == 0) {
		KMACAddress dnsServerMAC;

		if (!NetARPLookup(task, interface->dnsServerIP, &dnsServerMAC)) {
			return;
		}

		KMutexAcquire(&networking.udpTaskBitsetMutex);
		ptrdiff_t taskIndex = networking.udpTaskBitset.Get();

		if (taskIndex == -1) {
			KMutexRelease(&networking.udpTaskBitsetMutex);
			NetTaskComplete(task, ES_ERROR_INSUFFICIENT_RESOURCES);
			return;
		}

		networking.udpTasks[taskIndex] = task;
		task->index = taskIndex;
		KMutexRelease(&networking.udpTaskBitsetMutex);

		EsBuffer buffer = NetTransmitBufferGet();

		if (buffer.error) {
			NetTaskComplete(task, ES_ERROR_INSUFFICIENT_RESOURCES);
			return;
		}

		EthernetHeader *ethernet = (EthernetHeader *) buffer.Write(nullptr, sizeof(EthernetHeader)); 
		ETHERNET_HEADER(ethernet, ETHERNET_TYPE_IPV4, dnsServerMAC); 
		IPHeader *ip = (IPHeader *) buffer.Write(nullptr, sizeof(IPHeader)); 
		IP_HEADER(ip, interface->dnsServerIP, IP_PROTOCOL_UDP); 
		UDPHeader *udp = (UDPHeader *) buffer.Write(nullptr, sizeof(UDPHeader)); 
		UDP_HEADER(udp, taskIndex + UDP_PORT_BASE, 53 /* DNS server */); 

		DNSHeader *dns = (DNSHeader *) buffer.Write(nullptr, sizeof(DNSHeader));
		dns->identifier = task->identifier = EsRandomU64();
		dns->flags = SwapBigEndian16(1 << 8 /* recursion desired */);
		dns->questionCount = SwapBigEndian16(1);

		for (uintptr_t i = 0, j = 0; i <= task->nameBytes; i++) {
			if (i == task->nameBytes || task->name[i] == '.') {
				uint8_t bytes = i - j;
				buffer.Write(&bytes, 1);
				buffer.Write(task->name + j, bytes);

				j = i + 1;
			}
		}

		{
			uint8_t zero = 0;
			buffer.Write(&zero, 1);

			uint16_t queryType = SwapBigEndian16(1 /* A - IPv4 address */);
			buffer.Write(&queryType, 2);

			uint16_t queryClass = SwapBigEndian16(1 /* IN - the internet */);
			buffer.Write(&queryClass, 2);
		}

		if (buffer.error) {
			KernelPanic("NetInterface::DomainNameResolve - Network interface buffer size too small.\n");
		}

		ip->totalLength = ByteSwap16(buffer.position - sizeof(*ethernet)); 
		udp->length = ByteSwap16(buffer.position - sizeof(*ethernet) - sizeof(*ip)); 

		task->step++;

		if (!NetTransmit(interface, &buffer, NET_PACKET_ETHERNET)) {
			NetTaskComplete(task, ES_ERROR_INSUFFICIENT_RESOURCES);
			return;
		}
	} else if (task->step == 1) {
		const DNSHeader *header = (const DNSHeader *) buffer->Read(sizeof(DNSHeader));

		if (!header) {
			KernelLog(LOG_ERROR, "Networking", "bad packet", "Missing DNS header.\n");
			return;
		}

		if (header->identifier != task->identifier) {
			KernelLog(LOG_ERROR, "Networking", "bad packet", "Received DNS packet with wrong identifier.\n");
			return;
		}

		uint16_t flags = SwapBigEndian16(header->flags);

		if (~flags & (1 << 15)) {
			KernelLog(LOG_ERROR, "Networking", "bad packet", "Received DNS request (expecting reponse).\n");
			return;
		}

		EsError error = ES_ERROR_UNKNOWN;

		if ((flags & 15) == 3) {
			error = ES_ERROR_NO_ADDRESS_FOR_DOMAIN_NAME;
		} else if ((flags & 15) == 0) {
			error = ES_SUCCESS;
		}

		for (uintptr_t i = 0; i < SwapBigEndian16(header->questionCount); i++) {
			while (true) {
				const uint8_t *length = (const uint8_t *) buffer->Read(1);
				if (!length) break;

				if ((*length & 0xC0) == 0xC0) {
					buffer->Read(1);
					break;
				} else if (*length == 0) {
					break;
				}

				buffer->Read(*length);
			}

			buffer->Read(4);
		}

		bool foundAddress = false;

		for (uintptr_t i = 0; i < SwapBigEndian16(header->answerCount); i++) {
			while (true) {
				const uint8_t *length = (const uint8_t *) buffer->Read(1);
				if (!length) break;

				if ((*length & 0xC0) == 0xC0) {
					buffer->Read(1);
					break;
				} else if (*length == 0) {
					break;
				}

				buffer->Read(*length);
			}

			const uint16_t *type = (const uint16_t *) buffer->Read(2);
			const uint16_t *classType = (const uint16_t *) buffer->Read(2);
			const uint32_t *timeToLive = (const uint32_t *) buffer->Read(4);
			const uint16_t *dataLength = (const uint16_t *) buffer->Read(2);

			if (!type || !classType || !timeToLive || !dataLength) {
				break;
			}

			const void *data = (const void *) buffer->Read(SwapBigEndian16(*dataLength));

			if (!data) {
				break;
			}

			if (SwapBigEndian16(*type) == 1 /* A - IPv4 address */
					&& SwapBigEndian16(*classType) == 1 /* IN - the internet */) {
				if (SwapBigEndian16(*dataLength) != 4) {
					KernelLog(LOG_ERROR, "Networking", "bad packet", "IPv4 address was not 4 bytes.\n");
					return;
				}

				EsMemoryCopy(&task->address->ipv4, data, 4);
				foundAddress = true;
				break;
			}
		}

		if (buffer->error) {
			KernelLog(LOG_ERROR, "Networking", "bad packet", "Missing data after DNS header.\n");
			return;
		}

		if (!foundAddress) {
			KernelLog(LOG_ERROR, "Networking", "bad packet", "Could not find IP address in DNS packet.\n");
			error = ES_ERROR_UNKNOWN;
		}

		NetTaskComplete(task, error);
	} else {
		KernelPanic("NetDomainNameResolve - Invalid step.\n");
	}
}

void NetEchoRequest(NetTask *_task, void *_buffer) {
	EsBuffer *buffer = (EsBuffer *) _buffer;
	NetEchoRequestTask *task = (NetEchoRequestTask *) _task;
	NetInterface *interface = task->interface;

	if (task->completed) {
		KMutexAcquire(&networking.echoRequestTaskMutex);

		if (networking.echoRequestTask == task) {
			networking.echoRequestTask = nullptr;
		}

		KMutexRelease(&networking.echoRequestTaskMutex);

		if (task->event) {
			// This must be the last thing we do, otherwise the NetTask might be freed.
			KEventSet(task->event);
		}
	} else if (task->step == 0) {
		KMACAddress routerMAC;

		if (!NetARPLookup(task, interface->routerIP, &routerMAC)) {
			return;
		}

		KMutexAcquire(&networking.echoRequestTaskMutex);

		if (networking.echoRequestTask) {
			KMutexRelease(&networking.echoRequestTaskMutex);
			NetTaskComplete(task, ES_ERROR_INSUFFICIENT_RESOURCES);
			return;
		}

		networking.echoRequestTask = task;
		KMutexRelease(&networking.echoRequestTaskMutex);

		EsBuffer buffer = NetTransmitBufferGet();

		if (buffer.error) {
			NetTaskComplete(task, ES_ERROR_INSUFFICIENT_RESOURCES);
			return;
		}

		EthernetHeader *ethernet = (EthernetHeader *) buffer.Write(nullptr, sizeof(EthernetHeader)); 
		ETHERNET_HEADER(ethernet, ETHERNET_TYPE_IPV4, routerMAC); 
		IPHeader *ip = (IPHeader *) buffer.Write(nullptr, sizeof(IPHeader)); 
		KIPAddress destinationIP;
		EsMemoryCopy(&destinationIP, &task->address->ipv4, sizeof(KIPAddress));
		IP_HEADER(ip, destinationIP, IP_PROTOCOL_ICMP);
		ICMPHeader *icmp = (ICMPHeader *) buffer.Write(nullptr, sizeof(ICMPHeader));
		buffer.Write(task->data, ES_ECHO_REQUEST_MAX_LENGTH);

		if (buffer.error) {
			KernelPanic("NetInterface::DomainNameResolve - Network interface buffer size too small.\n");
		}

		icmp->type = 8; // Echo request.
		ip->totalLength = ByteSwap16(buffer.position - sizeof(*ethernet)); 
		task->step++;

		if (!NetTransmit(interface, &buffer, NET_PACKET_ETHERNET)) {
			NetTaskComplete(task, ES_ERROR_INSUFFICIENT_RESOURCES);
			return;
		}
	} else if (task->step == 1) {
		size_t dataBytes = buffer->bytes - buffer->position;
		const uint8_t *data = (const uint8_t *) buffer->Read(dataBytes);

		if (dataBytes > ES_ECHO_REQUEST_MAX_LENGTH) {
			dataBytes = ES_ECHO_REQUEST_MAX_LENGTH;
		}

		EsMemoryZero(task->data, ES_ECHO_REQUEST_MAX_LENGTH);
		EsMemoryCopy(task->data, data, dataBytes);

		NetTaskComplete(task, ES_SUCCESS);
	} else {
		KernelPanic("NetEchoRequest - Invalid step.\n");
	}
}

bool NetTCPIsBetween(uint32_t low, uint32_t value, uint32_t high) {
	if (low <= high) {
		return low <= value && value <= high;
	} else {
		return high <= value && value <= low;
	}
}

bool NetTCPIsLessThan(uint32_t left, uint32_t right) {
	return left - right > 0x80000000;
}

bool NetTCPIsLessThanOrEqual(uint32_t left, uint32_t right) {
	return left - right - 1 > 0x80000000;
}

void NetTCPFreeTaskIndex(uint16_t index, bool initialFree = false) {
	if (index != 0xFFFF) {
		// Free the port index.

		KMutexAcquire(&networking.tcpTaskListMutex);

		if ((~networking.tcpTasks[index] & 1) && !initialFree) {
			KernelPanic("NetTCPFreeTaskIndex - TCP task list double-free.\n");
		} else if (networking.tcpTaskLRU == 0xFFFF && networking.tcpTaskMRU == 0xFFFF) {
			networking.tcpTaskLRU = index;
			networking.tcpTaskMRU = index;
			networking.tcpTasks[index] = 0xFFFF << 1;
		} else if (networking.tcpTaskLRU == 0xFFFF || networking.tcpTaskMRU == 0xFFFF 
				|| networking.tcpTasks[networking.tcpTaskMRU] != (0xFFFF << 1)) {
			KernelPanic("NetTCPFreeTaskIndex - Broken TCP task list.\n");
		} else {
			networking.tcpTasks[networking.tcpTaskMRU] = index << 1;
			networking.tcpTaskMRU = index;
			networking.tcpTasks[index] = 0xFFFF << 1;
		}

		KMutexRelease(&networking.tcpTaskListMutex);
	}
}

bool NetTCPAllocateTaskIndex(NetTask *task) {
	KMutexAcquire(&networking.tcpTaskListMutex);
	uint16_t taskIndex = networking.tcpTaskLRU;

	if (taskIndex == 0xFFFF) {
		KMutexRelease(&networking.tcpTaskListMutex);
		return false;
	}

	networking.tcpTaskLRU = networking.tcpTasks[taskIndex] >> 1;
	if (networking.tcpTaskLRU == 0xFFFF) networking.tcpTaskMRU = 0xFFFF;
	networking.tcpTasks[taskIndex] = (uintptr_t) task | 1;
	task->index = taskIndex;
	KMutexRelease(&networking.tcpTaskListMutex);
	return true;
}

bool NetConnectionTransmitData(NetConnection *connection) {
	// TODO Send segments as a NetTask, so that they can be retransmitted.

	NetTCPConnectionTask *task = &connection->task;
	NetInterface *interface = task->interface;
	KWriterLockAssertShared(&interface->connectionLock);

	bool sent = false;

	while (connection->sendReadPointer != connection->sendWritePointer) {
		uintptr_t bytesAvailable = connection->sendWritePointer - connection->sendReadPointer;

		if (connection->sendReadPointer > connection->sendWritePointer) {
			bytesAvailable += connection->sendBufferBytes;
		}

		uintptr_t maximumBytesPerSegment = 1000; // TODO Parse the maximum segment size option.
		uintptr_t bytesToSendInSegment = MinimumInteger(maximumBytesPerSegment, bytesAvailable);

		const void *data1 = connection->sendBuffer + connection->sendReadPointer;
		const void *data2 = connection->sendBuffer;
		size_t dataBytes1 = bytesToSendInSegment;
		size_t dataBytes2 = 0;

		if (connection->sendReadPointer + bytesToSendInSegment > connection->sendBufferBytes) {
			dataBytes1 = connection->sendBufferBytes - connection->sendReadPointer;
			dataBytes2 = bytesToSendInSegment - dataBytes1;
		}

		EsBuffer buffer = NetTransmitBufferGet();

		if (buffer.error) { 
			NetTaskComplete(task, ES_ERROR_INSUFFICIENT_RESOURCES); 
			return true;
		}

		EthernetHeader *ethernet = (EthernetHeader *) buffer.Write(nullptr, sizeof(EthernetHeader));
		ETHERNET_HEADER(ethernet, ETHERNET_TYPE_IPV4, task->destinationMAC);
		IPHeader *ip = (IPHeader *) buffer.Write(nullptr, sizeof(IPHeader));
		IP_HEADER(ip, *(KIPAddress *) &connection->address.ipv4, IP_PROTOCOL_TCP);
		TCPHeader *tcp = (TCPHeader *) buffer.Write(nullptr, sizeof(TCPHeader));
		buffer.Write(data1, dataBytes1);
		buffer.Write(data2, dataBytes2);
	
		if (buffer.error) {
			KernelPanic("NetConnectionTransmitData - Network interface buffer size too small.\n");
		}
	
		ip->totalLength = ByteSwap16(buffer.position - sizeof(*ethernet));
		ip->flagsAndFragmentOffset = SwapBigEndian16(1 << 14 /* do not fragment */);
		ip->headerChecksum = ip->CalculateHeaderChecksum();
	
		tcp->sourcePort = SwapBigEndian16(task->index + TCP_PORT_BASE);
		tcp->destinationPort = SwapBigEndian16(connection->address.port);
		tcp->flags = SwapBigEndian16(TCP_ACK | (5 << 12 /* header is 5 DWORDs */));
		tcp->sequenceNumber = SwapBigEndian32(task->sendNext);
		tcp->ackNumber = SwapBigEndian32(task->receiveNext);
		tcp->window = SwapBigEndian16(task->receiveWindow);

		if (!NetTransmit(interface, &buffer, NET_PACKET_ETHERNET)) {
			NetTaskComplete(task, ES_ERROR_INSUFFICIENT_RESOURCES);
			return true;
		}

		sent = true;
		connection->sendReadPointer = (connection->sendReadPointer + bytesToSendInSegment) % connection->sendBufferBytes;
		task->sendNext += bytesToSendInSegment;
	}

	return sent;
}

bool NetConnectionUpdateReceiveWindow(NetConnection *connection) {
	NetTCPConnectionTask *task = &connection->task;
	uint16_t oldReceiveWindow = task->receiveWindow;

	if (connection->receiveReadPointer == connection->receiveWritePointer) {
		task->receiveWindow = MinimumInteger(0xF000, connection->receiveBufferBytes - 1);
	} else if (connection->receiveReadPointer < connection->receiveWritePointer) {
		task->receiveWindow = MinimumInteger(0xF000, connection->receiveReadPointer - connection->receiveWritePointer + connection->receiveBufferBytes);
	} else {
		task->receiveWindow = MinimumInteger(0xF000, connection->receiveReadPointer - connection->receiveWritePointer);
	}

	EsPrint("ORW: %d; RW: %d; RRP: %d; RWP: %d; RBB: %d\n", oldReceiveWindow, task->receiveWindow, 
			connection->receiveReadPointer, connection->receiveWritePointer, connection->receiveBufferBytes);

	return oldReceiveWindow != task->receiveWindow;
}

void NetConnectionNotify(NetConnection *connection, uintptr_t sendWritePointer, uintptr_t receiveReadPointer) {
	NetTCPConnectionTask *task = &connection->task;
	NetInterface *interface = task->interface;
	KWriterLockTake(&interface->connectionLock, K_LOCK_SHARED);
	KMutexAcquire(&connection->mutex);

	connection->sendWritePointer = sendWritePointer % connection->sendBufferBytes;
	connection->receiveReadPointer = receiveReadPointer % connection->receiveBufferBytes;

	bool receiveWindowModified = NetConnectionUpdateReceiveWindow(connection);

	if (task->step == TCP_STEP_ESTABLISHED
			&& !NetConnectionTransmitData(connection)
			&& receiveWindowModified) {
		// ACK the new window size.

		EsBuffer buffer = NetTransmitBufferGet();

		if (buffer.error) { 
			NetTaskComplete(task, ES_ERROR_INSUFFICIENT_RESOURCES); 
		} else {
			EthernetHeader *ethernet = (EthernetHeader *) buffer.Write(nullptr, sizeof(EthernetHeader));
			ETHERNET_HEADER(ethernet, ETHERNET_TYPE_IPV4, task->destinationMAC);
			IPHeader *ip = (IPHeader *) buffer.Write(nullptr, sizeof(IPHeader));
			IP_HEADER(ip, *(KIPAddress *) &connection->address.ipv4, IP_PROTOCOL_TCP);
			TCPHeader *tcp = (TCPHeader *) buffer.Write(nullptr, sizeof(TCPHeader));

			if (buffer.error) {
				KernelPanic("NetConnectionNotify - Network interface buffer size too small.\n");
			}

			ip->totalLength = ByteSwap16(buffer.position - sizeof(*ethernet));
			ip->flagsAndFragmentOffset = SwapBigEndian16(1 << 14 /* do not fragment */);
			ip->headerChecksum = ip->CalculateHeaderChecksum();

			tcp->sourcePort = SwapBigEndian16(task->index + TCP_PORT_BASE);
			tcp->destinationPort = SwapBigEndian16(connection->address.port);
			tcp->flags = SwapBigEndian16(TCP_ACK | (5 << 12 /* header is 5 DWORDs */));
			tcp->sequenceNumber = SwapBigEndian32(task->sendNext);
			tcp->ackNumber = SwapBigEndian32(task->receiveNext);
			tcp->window = SwapBigEndian16(task->receiveWindow);

			if (!NetTransmit(interface, &buffer, NET_PACKET_ETHERNET)) {
				NetTaskComplete(task, ES_ERROR_INSUFFICIENT_RESOURCES);
			}
		}
	}

	KMutexRelease(&connection->mutex);
	KWriterLockReturn(&interface->connectionLock, K_LOCK_SHARED);
}

void NetTCPConnection(NetTask *_task, void *_data) {
	TCPReceivedData *data = (TCPReceivedData *) _data;
	NetTCPConnectionTask *task = (NetTCPConnectionTask *) _task;
	NetInterface *interface = task->interface;
	NetConnection *connection = EsContainerOf(NetConnection, task, task);

	if (task->completed) {
		NetTCPFreeTaskIndex(task->index);
		CloseHandleToObject(connection, KERNEL_OBJECT_CONNECTION);
		return;
	}

	KMutexAcquire(&connection->mutex);
	EsDefer(KMutexRelease(&connection->mutex));

	if (task->step == 0) {
		if (!NetARPLookup(task, interface->routerIP, &task->destinationMAC)) {
			return;
		}

		if (!NetTCPAllocateTaskIndex(task)) {
			NetTaskComplete(task, ES_ERROR_INSUFFICIENT_RESOURCES);
			return;
		}

		EsBuffer buffer = NetTransmitBufferGet();

		if (buffer.error) {
			NetTaskComplete(task, ES_ERROR_INSUFFICIENT_RESOURCES);
			return;
		}

		task->initialSend = (uint32_t) EsRandomU64() & 0x0FFFFFFF;
		task->sendUnacknowledged = task->initialSend;
		task->sendNext = task->initialSend + 1;
		task->step = TCP_STEP_SYN_SENT;

		EthernetHeader *ethernet = (EthernetHeader *) buffer.Write(nullptr, sizeof(EthernetHeader)); 
		ETHERNET_HEADER(ethernet, ETHERNET_TYPE_IPV4, task->destinationMAC); 
		IPHeader *ip = (IPHeader *) buffer.Write(nullptr, sizeof(IPHeader)); 
		KIPAddress destinationIP = {};
		EsMemoryCopy(&destinationIP, &connection->address.ipv4, sizeof(KIPAddress));
		IP_HEADER(ip, destinationIP, IP_PROTOCOL_TCP);
		TCPHeader *tcp = (TCPHeader *) buffer.Write(nullptr, sizeof(TCPHeader));

		if (buffer.error) {
			KernelPanic("NetTCPConnection - Network interface buffer size too small.\n");
		}

		ip->totalLength = ByteSwap16(buffer.position - sizeof(*ethernet)); 
		ip->flagsAndFragmentOffset = SwapBigEndian16(1 << 14 /* do not fragment */); 

		tcp->window = SwapBigEndian16(task->receiveWindow);
		tcp->flags = SwapBigEndian16(TCP_SYN | (5 << 12 /* header is 5 DWORDs */));
		tcp->sourcePort = SwapBigEndian16(task->index + TCP_PORT_BASE);
		tcp->destinationPort = SwapBigEndian16(connection->address.port);
		tcp->sequenceNumber = SwapBigEndian32(task->initialSend);

		if (!NetTransmit(interface, &buffer, NET_PACKET_ETHERNET)) {
			NetTaskComplete(task, ES_ERROR_INSUFFICIENT_RESOURCES);
		}
	} else if (task->step == TCP_STEP_SYN_SENT) {
		if ((data->flags & TCP_ACK) && !NetTCPIsBetween(task->sendUnacknowledged, data->ackNumber, task->sendNext)) {
			if (data->flags & TCP_RST) return;
			EsBuffer buffer = NetTransmitBufferGet();
			if (buffer.error) return;
			TCP_PREPARE_REPLY(nullptr, 0, nullptr, 0);
			tcpReply->flags = SwapBigEndian16(TCP_RST | (5 << 12 /* header is 5 DWORDs */));
			tcpReply->sequenceNumber = data->tcp->ackNumber;
			if (!NetTransmit(interface, &buffer, NET_PACKET_ETHERNET)) NetTaskComplete(task, ES_ERROR_INSUFFICIENT_RESOURCES);
		} else if (data->flags & TCP_RST) {
			NetTaskComplete(task, ES_ERROR_CONNECTION_RESET);
		} else if (data->flags & TCP_SYN) {
			task->initialReceive = data->sequenceNumber;
			task->receiveNext = data->sequenceNumber + 1;

			if (data->flags & TCP_ACK) {
				task->sendUnacknowledged = data->ackNumber;
				task->step = TCP_STEP_ESTABLISHED;
				TCP_MAKE_STANDARD_REPLY(TCP_ACK);
				NetConnectionTransmitData(connection);
			} else {
				task->step = TCP_STEP_SYN_RECEIVED;
				TCP_MAKE_STANDARD_REPLY(TCP_SYN | TCP_ACK);
			}
		}
	} else {
		bool acceptable = NetTCPIsBetween(task->receiveNext, data->sequenceNumber, task->receiveNext + task->receiveWindow - 1);

		if (!data->segmentLength && !task->receiveWindow) {
			acceptable = data->sequenceNumber == task->receiveNext;
		} else if (!data->segmentLength && task->receiveWindow) {
			acceptable = NetTCPIsBetween(task->receiveNext, data->sequenceNumber, task->receiveNext + task->receiveWindow - 1);
		} else if (data->segmentLength && task->receiveWindow) {
			acceptable = NetTCPIsBetween(task->receiveNext, data->sequenceNumber, task->receiveNext + task->receiveWindow - 1)
				|| NetTCPIsBetween(task->receiveNext, data->sequenceNumber + data->segmentLength - 1, task->receiveNext + task->receiveWindow - 1);
		}

		if (!acceptable) {
			// ACK the unacceptable segment (if the RST flag wasn't set).
			if (~data->flags & TCP_RST) TCP_MAKE_STANDARD_REPLY(TCP_ACK);
			return;
		}

		{
			// Truncate segments that are partially outside the window.
			// TODO Test this!

			if (NetTCPIsLessThan(data->sequenceNumber, task->receiveNext)) {
				data->segment = (uint8_t *) data->segment + task->receiveNext - data->sequenceNumber;
				data->segmentLength -= task->receiveNext - data->sequenceNumber;
				data->sequenceNumber = task->receiveNext;
			}

			if (NetTCPIsLessThan(task->receiveNext + task->receiveWindow, data->sequenceNumber + data->segmentLength)) {
				data->segmentLength = (task->receiveNext + task->receiveWindow) - data->sequenceNumber;
			}
		}

		if (data->flags & TCP_RST) {
			if (task->step == TCP_STEP_SYN_RECEIVED) {
				NetTaskComplete(task, ES_ERROR_CONNECTION_REFUSED);
			} else if (task->step >= TCP_STEP_ESTABLISHED && task->step <= TCP_STEP_CLOSE_WAIT) {
				NetTaskComplete(task, ES_ERROR_CONNECTION_RESET);
			} else {
				NetTaskComplete(task, ES_SUCCESS);
			}
		} else if (data->flags & TCP_SYN) {
			TCP_MAKE_STANDARD_REPLY(TCP_RST);
			NetTaskComplete(task, ES_ERROR_CONNECTION_RESET);
		} else if (data->flags & TCP_ACK) {
			if (task->step == TCP_STEP_SYN_RECEIVED) {
				if (NetTCPIsBetween(task->sendUnacknowledged, data->ackNumber, task->sendNext)) {
					task->step = TCP_STEP_ESTABLISHED;
					task->sendUnacknowledged = data->ackNumber;
					task->sendWindow = SwapBigEndian16(data->tcp->window);
					TCP_MAKE_STANDARD_REPLY(TCP_ACK);
					NetConnectionTransmitData(connection);
				} else {
					task->sendNext = data->ackNumber;
					task->receiveNext = 0;
					TCP_MAKE_STANDARD_REPLY(TCP_RST);
					return;
				}
			} else if (task->step == TCP_STEP_LAST_ACK) {
				if (NetTCPIsLessThanOrEqual(task->finSequence, task->sendUnacknowledged)) {
					NetTaskComplete(task, ES_SUCCESS);
				}
			} else {
				if (NetTCPIsBetween(task->sendUnacknowledged + 1, data->ackNumber, task->sendNext)) {
					task->sendUnacknowledged = data->ackNumber;

					// Don't update the window using old packets.
					if (NetTCPIsLessThan(task->sendWL1, data->sequenceNumber) 
							|| (task->sendWL1 == data->sequenceNumber && NetTCPIsLessThanOrEqual(task->sendWL2, data->ackNumber))) {
						task->sendWindow = SwapBigEndian16(data->tcp->window);
						task->sendWL1 = data->sequenceNumber;
						task->sendWL2 = data->ackNumber;
					}
				} else if (NetTCPIsLessThanOrEqual(data->ackNumber, task->sendUnacknowledged)) {
					TCP_MAKE_STANDARD_REPLY(TCP_ACK);
					return;
				}

				if (NetTCPIsLessThanOrEqual(task->finSequence, task->sendUnacknowledged)) {
					if (task->step == TCP_STEP_FIN_WAIT_1) {
						task->step = TCP_STEP_FIN_WAIT_2;
					} else if (task->step == TCP_STEP_CLOSING) { 
						NetTaskComplete(task, ES_SUCCESS);
						return;
					}
				}
			}
		}

		if (task->step >= TCP_STEP_ESTABLISHED && task->step <= TCP_STEP_FIN_WAIT_2 && data->segmentLength) {
			uint32_t start = data->sequenceNumber - task->receiveNext;
			uint32_t end = data->sequenceNumber + data->segmentLength - task->receiveNext;

			if (start >= task->receiveWindow || end > task->receiveWindow || (end - start) >= connection->receiveBufferBytes) {
				KernelPanic("NetTCPConnection - Segment %x incorrectly truncated to fit inside receive window of task %x.\n", data, task);
			}

			uintptr_t startWrite = (start + connection->receiveWritePointer) % connection->receiveBufferBytes;
			uintptr_t endWrite = (end + connection->receiveWritePointer) % connection->receiveBufferBytes;

			if (startWrite < endWrite) {
				EsMemoryCopy(connection->receiveBuffer + startWrite, data->segment, endWrite - startWrite);
#if 0
				EsPrint("Writing segment RSN %d to %d->%d: %s\n", 
						data->sequenceNumber - task->initialReceive, startWrite, endWrite,
						data->segmentLength, data->segment);
#endif
			} else {
				uintptr_t firstCopy = connection->receiveBufferBytes - startWrite;
				EsMemoryCopy(connection->receiveBuffer + startWrite, data->segment, firstCopy);
				EsMemoryCopy(connection->receiveBuffer, (uint8_t *) data->segment + firstCopy, endWrite);
#if 0
				EsPrint("Writing segment RSN %d to %d->%d and %d->%d: %s\n", 
						data->sequenceNumber - task->initialReceive, 
						startWrite, firstCopy, 0, endWrite,
						data->segmentLength, data->segment);
#endif
			}

			if (start) {
				if (!connection->receivedData.Set(start, end, nullptr, true)) {
					NetTaskComplete(task, ES_ERROR_INSUFFICIENT_RESOURCES);
					return;
				}
			} else {
				uintptr_t advanceBy = end;

				if (connection->receivedData.contiguous) {
					KernelPanic("NetTCPConnection - Received data range set in %x was unexpectedly contiguous.\n", connection);
				}

				if (connection->receivedData.ranges.Length()) {
					if (connection->receivedData.ranges.Length()
							&& connection->receivedData.ranges[0].from <= advanceBy) {
						advanceBy = connection->receivedData.ranges[0].to;
						connection->receivedData.ranges.Delete(0);
					}

					for (uintptr_t i = 0; i < connection->receivedData.ranges.Length(); i++) {
						connection->receivedData.ranges[0].from -= advanceBy;
						connection->receivedData.ranges[0].to -= advanceBy;
					}

					connection->receivedData.Validate();
				}

				task->receiveNext += advanceBy;
				connection->receiveWritePointer = (connection->receiveWritePointer + advanceBy) % connection->receiveBufferBytes;
				NetConnectionUpdateReceiveWindow(connection);

				TCP_MAKE_STANDARD_REPLY(TCP_ACK);
			}
		}

		if (data->flags & TCP_FIN) {
			task->receiveNext = data->sequenceNumber + 1;
			TCP_MAKE_STANDARD_REPLY(TCP_ACK);

			if (task->step == TCP_STEP_SYN_RECEIVED || task->step == TCP_STEP_ESTABLISHED) {
				task->step = TCP_STEP_CLOSE_WAIT;
			} else if (task->step == TCP_STEP_FIN_WAIT_1) {
				if (NetTCPIsLessThanOrEqual(task->finSequence, task->sendUnacknowledged)) {
					NetTaskComplete(task, ES_SUCCESS);
				} else {
					task->step = TCP_STEP_CLOSING;
				}
			} else if (task->step == TCP_STEP_FIN_WAIT_2) {
				NetTaskComplete(task, ES_SUCCESS);
			}
		}
	}
}

void NetAddressSetup(NetTask *_task, void *_buffer) {
	EsBuffer *buffer = (EsBuffer *) _buffer;
	NetAddressSetupTask *task = (NetAddressSetupTask *) _task;
	NetInterface *interface = task->interface;
	const DHCPHeader *dhcp;

	if (task->completed) {
		return;
	} else if (task->step == 0) {
		// Broadcast a DHCPDISCOVER request to get an IP address.

		EsBuffer buffer = NetTransmitBufferGet();

		if (buffer.error) {
			return;
		}

		DHCP_START();

		uint8_t dhcpOptions[] = {
			DHCP_OPTION_MESSAGE_TYPE(DHCPDISCOVER), 
			55, 2, 3 /* get the router's IP */, 6 /* get the DNS server's IP */,
			255, // End of options.
		};

		buffer.Write(dhcpOptions, sizeof(dhcpOptions));
		DHCP_END();

		if (buffer.error) {
			KernelPanic("NetAddressSetup - Network interface buffer size too small.\n");
		}

		task->dhcpTransactionID = dhcp->transactionID;
		task->step++;

		if (!NetTransmit(interface, &buffer, NET_PACKET_ETHERNET)) {
			NetTaskComplete(task, ES_ERROR_INSUFFICIENT_RESOURCES);
		}

		return;
	}

	dhcp = (const DHCPHeader *) buffer->Read(sizeof(DHCPHeader));

	if (dhcp->opCode != DHCP_BOOTREPLY /* boot reply */
			|| dhcp->hardwareAddressType != 0x01 /* ethernet */
			|| dhcp->hardwareAddressLength != 6
			|| dhcp->transactionID != task->dhcpTransactionID
			|| dhcp->optionsMagic[0] != 99
			|| dhcp->optionsMagic[1] != 130
			|| dhcp->optionsMagic[2] != 83
			|| dhcp->optionsMagic[3] != 99) {
		KernelLog(LOG_ERROR, "Networking", "ignored packet", "Unsupported DHCP packet.\n");
		return;
	}

	task->dhcpTransactionID = 0;

	const KIPAddress *dnsServerOption = nullptr;
	const KIPAddress *serverIdentifierOption = nullptr;
	const KIPAddress *routerIPOption = nullptr;
	const uint8_t *messageTypeOption = nullptr;

	while (true) {
		const uint8_t *optionType = (const uint8_t *) buffer->Read(sizeof(uint8_t));

		if (!optionType) {
			KernelLog(LOG_ERROR, "Networking", "bad packet", "Missing DHCP option type.\n");
			return;
		}

		if (*optionType == 0) {
			continue;
		} else if (*optionType == 255) {
			break;
		} else {
			const uint8_t *optionLength = (const uint8_t *) buffer->Read(sizeof(uint8_t));

			if (!optionLength) {
				KernelLog(LOG_ERROR, "Networking", "bad packet", "Missing DHCP option length.\n");
				return;
			}

			const uint8_t *optionValue = (const uint8_t *) buffer->Read(*optionLength);

			if (!optionValue) {
				KernelLog(LOG_ERROR, "Networking", "bad packet", "Missing DHCP option value.\n");
				return;
			}

			if (*optionType == 6 /* DNS server */) {
				if (*optionLength < 4) {
					KernelLog(LOG_ERROR, "Networking", "bad packet", "DNS server IP in DHCP option too short.\n");
					return;
				}

				dnsServerOption = (const KIPAddress *) optionValue;
			} else if (*optionType == 3 /* Router IP */) {
				if (*optionLength < 4) {
					KernelLog(LOG_ERROR, "Networking", "bad packet", "Router IP in DHCP option too short.\n");
					return;
				}

				routerIPOption = (const KIPAddress *) optionValue;
			} else if (*optionType == 54 /* server identifier */) {
				if (*optionLength < 4) {
					KernelLog(LOG_ERROR, "Networking", "bad packet", "Server identifier in DHCP option too short.\n");
					return;
				}

				serverIdentifierOption = (const KIPAddress *) optionValue;
			} else if (*optionType == 53 /* message type */) {
				if (*optionLength < 1) {
					KernelLog(LOG_ERROR, "Networking", "bad packet", "Message type in DHCP option too short.\n");
					return;
				}

				messageTypeOption = (const uint8_t *) optionValue;
			}
		}
	}

	if (!messageTypeOption) {
		KernelLog(LOG_ERROR, "Networking", "bad packet", "Message type option in DHCP missing.\n");
		return;
	}

	uint8_t messageType = *messageTypeOption;

	if (messageType == DHCPOFFER) {
		if (task->step != 1) {
			KernelLog(LOG_ERROR, "Networking", "ignored packet", "Received DHCPOFFER, but task step is %d.\n", task->step);
			return;
		}

		if (!serverIdentifierOption) {
			KernelLog(LOG_ERROR, "Networking", "bad packet", "Server identifier option missing in DHCPOFFER.\n");
			return;
		}

		KIPAddress offer = dhcp->yourIPAddress;
		KernelLog(LOG_INFO, "Networking", "received IP", "Network interface %x has been offered IP address %d.%d.%d.%d.\n",
				interface, offer.d[0], offer.d[1], offer.d[2], offer.d[3]);

		// Broadcast a DHCPREQUEST message to accept this IP address.

		EsBuffer buffer = NetTransmitBufferGet();
		if (buffer.error) return;
		DHCP_START();

		uint8_t dhcpOptions[] = {
			DHCP_OPTION_MESSAGE_TYPE(DHCPREQUEST),
			DHCP_OPTION_REQUESTED_IP(offer),
			DHCP_OPTION_SERVER_IDENTIFIER(*serverIdentifierOption),
			55, 2, 3 /* get the router's IP */, 6 /* get the DNS server's IP */, 
			255, // End of options.
		};

		buffer.Write(dhcpOptions, sizeof(dhcpOptions));
		DHCP_END();

		if (buffer.error) {
			KernelPanic("KNetworkInterface::ReceiveDHCPReply - Network interface buffer size too small.\n");
		}

		task->dhcpTransactionID = dhcp->transactionID;
		task->step++;

		if (!NetTransmit(interface, &buffer, NET_PACKET_ETHERNET)) {
			NetTaskComplete(task, ES_ERROR_INSUFFICIENT_RESOURCES);
		}
	} else if (messageType == DHCPACK) {
		if (task->step != 2) {
			KernelLog(LOG_ERROR, "Networking", "ignored packet", "Received DHCPACK, but task step is %d.\n", task->step);
			return;
		}

		if (!dnsServerOption || !serverIdentifierOption || !routerIPOption) {
			KernelLog(LOG_ERROR, "Networking", "bad packet", "Missing options in DHCPACK.\n");
			return;
		}

		interface->ipAddress = dhcp->yourIPAddress;
		interface->routerIP = *routerIPOption;
		interface->dnsServerIP = *dnsServerOption;
		interface->serverIdentifier = *serverIdentifierOption;

		KernelLog(LOG_INFO, "Networking", "accepted IP", 
				"Network interface %x has accepted IP address %d.%d.%d.%d with DNS server %d.%d.%d.%d and router %d.%d.%d.%d.\n",
				interface, dhcp->yourIPAddress.d[0], dhcp->yourIPAddress.d[1], dhcp->yourIPAddress.d[2], dhcp->yourIPAddress.d[3],
				dnsServerOption->d[0], dnsServerOption->d[1], dnsServerOption->d[2], dnsServerOption->d[3],
				routerIPOption->d[0], routerIPOption->d[1], routerIPOption->d[2], routerIPOption->d[3]);

		task->changedState = true;
		NetTaskComplete(task, ES_SUCCESS);
	} else if (messageType == DHCPNAK) {
		KernelLog(LOG_INFO, "Networking", "lost IP", "Network interface %x has lost IP address %d.%d.%d.%d.\n",
				interface, interface->ipAddress.d[0], interface->ipAddress.d[1], interface->ipAddress.d[2], interface->ipAddress.d[3]);
		task->changedState = true;
		NetTaskComplete(task, ES_ERROR_LOST_IP_ADDRESS);
	}
}

void NetTaskBegin(NetTask *task) {
	if (!task->interface) {
		KMutexAcquire(&networking.interfacesListMutex);
		SimpleList *item = networking.interfaces.first;
 
		while (item && item != &networking.interfaces) {
			NetInterface *interface = EsContainerOf(NetInterface, item, item);
			KWriterLockTake(&interface->connectionLock, K_LOCK_SHARED);

			if (interface->connected && interface->hasIP) {
				task->interface = interface;
				break;
			}

			KWriterLockReturn(&interface->connectionLock, K_LOCK_SHARED);
			item = item->next;
		}

		KMutexRelease(&networking.interfacesListMutex);
	} else {
		KWriterLockTake(&task->interface->connectionLock, K_LOCK_SHARED);
	}

	if (!task->interface) {
		NetTaskComplete(task, ES_ERROR_NO_CONNECTED_NETWORK_INTERFACES);
	} else {
		task->index = 0xFFFF;
		task->callback(task, nullptr);
		KWriterLockReturn(&task->interface->connectionLock, K_LOCK_SHARED);
	}
}

void NetTaskComplete(NetTask *task, EsError error) {
	KWriterLockAssertShared(&task->interface->connectionLock);

	if (task->completed) {
		KernelPanic("NetTaskComplete - Task already completed.\n");
	}

	task->error = error;
	task->completed = true;
	task->callback(task, nullptr);
}

void NetConnectionDestroy(NetConnection *connection) {
	MMFree(kernelMMSpace, connection->sendBuffer, connection->sendBufferBytes + connection->receiveBufferBytes);
	connection->receivedData.ranges.Free();
	CloseHandleToObject(connection->bufferRegion, KERNEL_OBJECT_SHMEM);
	EsHeapFree(connection, sizeof(NetConnection), K_FIXED);
}

NetConnection *NetConnectionOpen(EsAddress *address, size_t sendBufferBytes, size_t receiveBufferBytes, uint32_t flags) {
	(void) flags;

	NetConnection *connection = (NetConnection *) EsHeapAllocate(sizeof(NetConnection), true, K_FIXED);

	if (!connection) {
		return nullptr;
	}

	connection->sendBufferBytes = sendBufferBytes;
	connection->receiveBufferBytes = receiveBufferBytes;
	connection->address = *address;
	connection->handles = 2;

	connection->bufferRegion = MMSharedCreateRegion(sendBufferBytes + receiveBufferBytes, true);

	if (!connection->bufferRegion) {
		EsHeapFree(connection, sizeof(NetConnection), K_FIXED);
		return nullptr;
	}

	connection->sendBuffer = (uint8_t *) MMMapShared(kernelMMSpace, connection->bufferRegion, 0, sendBufferBytes + receiveBufferBytes);
	connection->receiveBuffer = connection->sendBuffer + sendBufferBytes;

	connection->task.callback = NetTCPConnection;
	connection->task.receiveWindow = MinimumInteger(receiveBufferBytes - 1, 0xF000);
	NetTaskBegin(&connection->task);

	return connection;
}

void NetConnectionClose(NetConnection *connection) {
	bool destroy = false;

	NetTCPConnectionTask *task = &connection->task;
	NetInterface *interface = task->interface;
	KWriterLockTake(&interface->connectionLock, K_LOCK_SHARED);
	KMutexAcquire(&connection->mutex);
	connection->handles++; // Prevent NetTaskComplete from destroying the connection.

	if (task->completed) {
		destroy = true;
	} else if (task->step == TCP_STEP_SYN_RECEIVED || task->step == TCP_STEP_ESTABLISHED || task->step == TCP_STEP_CLOSE_WAIT) {
		// Send a FIN packet.

		EsBuffer buffer = NetTransmitBufferGet();

		if (buffer.error) { 
			NetTaskComplete(task, ES_ERROR_INSUFFICIENT_RESOURCES); 
		} else {
			EthernetHeader *ethernet = (EthernetHeader *) buffer.Write(nullptr, sizeof(EthernetHeader));
			ETHERNET_HEADER(ethernet, ETHERNET_TYPE_IPV4, task->destinationMAC);
			IPHeader *ip = (IPHeader *) buffer.Write(nullptr, sizeof(IPHeader));
			IP_HEADER(ip, *(KIPAddress *) &connection->address.ipv4, IP_PROTOCOL_TCP);
			TCPHeader *tcp = (TCPHeader *) buffer.Write(nullptr, sizeof(TCPHeader));

			if (buffer.error) {
				KernelPanic("NetConnectionClose - Network interface buffer size too small.\n");
			}

			ip->totalLength = ByteSwap16(buffer.position - sizeof(*ethernet));
			ip->flagsAndFragmentOffset = SwapBigEndian16(1 << 14 /* do not fragment */);
			ip->headerChecksum = ip->CalculateHeaderChecksum();

			tcp->sourcePort = SwapBigEndian16(task->index + TCP_PORT_BASE);
			tcp->destinationPort = SwapBigEndian16(connection->address.port);
			tcp->flags = SwapBigEndian16(TCP_FIN | TCP_ACK | (5 << 12 /* header is 5 DWORDs */));
			tcp->sequenceNumber = SwapBigEndian32(task->sendNext);
			tcp->ackNumber = SwapBigEndian32(task->receiveNext);
			tcp->window = SwapBigEndian16(task->receiveWindow);

			task->finSequence = task->sendNext;
			task->sendNext++;
			task->step = TCP_STEP_FIN_WAIT_1;

			if (!NetTransmit(interface, &buffer, NET_PACKET_ETHERNET)) {
				NetTaskComplete(task, ES_ERROR_INSUFFICIENT_RESOURCES);
			}
		}
	}

	connection->handles--;
	KMutexRelease(&connection->mutex);
	KWriterLockReturn(&interface->connectionLock, K_LOCK_SHARED);

	if (destroy) {
		NetConnectionDestroy(connection);
	}
}

void KRegisterNetInterface(NetInterface *interface) {
	KernelLog(LOG_INFO, "Networking", "register interface", "Registered interface with MAC address %X:%X:%X:%X:%X:%X and name '%z'.\n",
			interface->macAddress.d[0], interface->macAddress.d[1], interface->macAddress.d[2], 
			interface->macAddress.d[3], interface->macAddress.d[4], interface->macAddress.d[5],
			interface->cDebugName);

	KMutexAcquire(&networking.interfacesListMutex);
	networking.interfaces.Insert(&interface->item, false /* end */);
	KMutexRelease(&networking.interfacesListMutex);

	interface->addressSetupTask.interface = interface;
	interface->addressSetupTask.callback = NetAddressSetup;
}

void NetInitialise(KDevice *) {
	networking.udpTaskBitset.Initialise(MAX_UDP_TASKS);
	networking.udpTaskBitset.PutAll();
	EsArenaInitialise(&networking.transmitBufferPool, 1048576, 2048);

	networking.tcpTaskLRU = networking.tcpTaskMRU = 0xFFFF;

	for (uintptr_t i = 0; i < MAX_TCP_TASKS; i++) {
		NetTCPFreeTaskIndex(i, true);
	}
}

KDriver driverNetworking = {
	.attach = NetInitialise,
};

#endif
