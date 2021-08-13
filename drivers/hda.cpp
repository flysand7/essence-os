#include <module.h>

#define RD_REGISTER_GCAP()        controller->pci->ReadBAR16(0, 0x00)                  // Global capabilities. 
#define RD_REGISTER_VMIN()        controller->pci->ReadBAR8(0, 0x02)                   // Minor version number.
#define RD_REGISTER_VMAJ()        controller->pci->ReadBAR8(0, 0x03)                   // Major version number.
#define RD_REGISTER_GCTL()        controller->pci->ReadBAR32(0, 0x08)                  // Global control.
#define WR_REGISTER_GCTL(x)       controller->pci->WriteBAR32(0, 0x08, x)              
#define RD_REGISTER_STATESTS()    controller->pci->ReadBAR16(0, 0x0E)                  // State change status.
#define RD_REGISTER_INTCTL()      controller->pci->ReadBAR32(0, 0x20)                  // Interrupt control.
#define WR_REGISTER_INTCTL(x)     controller->pci->WriteBAR32(0, 0x20, x)
#define RD_REGISTER_INTSTS()      controller->pci->ReadBAR32(0, 0x24)                  // Interrupt status.
#define WR_REGISTER_CORBLBASE(x)  controller->pci->WriteBAR32(0, 0x40, x)              // CORB base address.
#define WR_REGISTER_CORBUBASE(x)  controller->pci->WriteBAR32(0, 0x44, x)              
#define RD_REGISTER_CORBWP(x)     controller->pci->ReadBAR16(0, 0x48)                  // CORB write pointer.
#define WR_REGISTER_CORBWP(x)     controller->pci->WriteBAR16(0, 0x48, x)              
#define RD_REGISTER_CORBRP(x)     controller->pci->ReadBAR16(0, 0x4A)                  // CORB read pointer.
#define WR_REGISTER_CORBRP(x)     controller->pci->WriteBAR16(0, 0x4A, x)              
#define RD_REGISTER_CORBCTL()     controller->pci->ReadBAR8(0, 0x4C)                   // CORB control.
#define WR_REGISTER_CORBCTL(x)    controller->pci->WriteBAR8(0, 0x4C, x)
#define RD_REGISTER_CORBSIZE()    controller->pci->ReadBAR8(0, 0x4E)                   // CORB size.
#define WR_REGISTER_CORBSIZE(x)   controller->pci->WriteBAR8(0, 0x4E, x)
#define WR_REGISTER_RIRBLBASE(x)  controller->pci->WriteBAR32(0, 0x50, x)              // RIRB base address.
#define WR_REGISTER_RIRBUBASE(x)  controller->pci->WriteBAR32(0, 0x54, x)              
#define RD_REGISTER_RIRBWP(x)     controller->pci->ReadBAR16(0, 0x58)                  // RIRB write pointer.
#define WR_REGISTER_RIRBWP(x)     controller->pci->WriteBAR16(0, 0x58, x)              
#define RD_REGISTER_RINTCNT()     controller->pci->ReadBAR16(0, 0x5A)                  // Response interrupt count.
#define WR_REGISTER_RINTCNT(x)    controller->pci->WriteBAR16(0, 0x5A, x)              
#define RD_REGISTER_RIRBCTL()     controller->pci->ReadBAR8(0, 0x5C)                   // RIRB control.
#define WR_REGISTER_RIRBCTL(x)    controller->pci->WriteBAR8(0, 0x5C, x)
#define RD_REGISTER_RIRBSTS()     controller->pci->ReadBAR8(0, 0x5D)                   // RIRB status.
#define WR_REGISTER_RIRBSTS(x)    controller->pci->WriteBAR8(0, 0x5D, x)
#define RD_REGISTER_RIRBSIZE()    controller->pci->ReadBAR8(0, 0x5E)                   // RIRB size.
#define WR_REGISTER_RIRBSIZE(x)   controller->pci->WriteBAR8(0, 0x5E, x)
#define WR_REGISTER_ImmComOut(x)  controller->pci->WriteBAR32(0, 0x60, x)              // Immediate command output.
#define RD_REGISTER_ImmComIn()    controller->pci->ReadBAR32(0, 0x64)                  // Immediate command input.
#define RD_REGISTER_ImmComStat()  controller->pci->ReadBAR16(0, 0x68)                  // Immediate command status.
#define WR_REGISTER_ImmComStat(x) controller->pci->WriteBAR16(0, 0x68, x)     
#define RD_REGISTER_SDCTL(n)      controller->pci->ReadBAR32(0, 0x80 + 0x20 * (n))     // Stream descriptor control.
#define WR_REGISTER_SDCTL(n, x)   controller->pci->WriteBAR32(0, 0x80 + 0x20 * (n), x) 
#define RD_REGISTER_SDSTS(n)      controller->pci->ReadBAR8(0, 0x83 + 0x20 * (n))      // Stream descriptor status.
#define WR_REGISTER_SDSTS(n, x)   controller->pci->WriteBAR8(0, 0x83 + 0x20 * (n), x)  
#define RD_REGISTER_SDLPIB(n)     controller->pci->ReadBAR32(0, 0x84 + 0x20 * (n))     // Stream descriptor link position in cyclic buffer.
#define WR_REGISTER_SDCBL(n, x)   controller->pci->WriteBAR32(0, 0x88 + 0x20 * (n), x) // Stream descriptor cyclic buffer length.
#define RD_REGISTER_SDLVI(n)      controller->pci->ReadBAR16(0, 0x8C + 0x20 * (n))     // Stream descriptor last valid index.
#define WR_REGISTER_SDLVI(n, x)   controller->pci->WriteBAR16(0, 0x8C + 0x20 * (n), x)
#define RD_REGISTER_SDFMT(n)      controller->pci->ReadBAR16(0, 0x92 + 0x20 * (n))     // Stream descriptor format.
#define WR_REGISTER_SDFMT(n, x)   controller->pci->WriteBAR16(0, 0x92 + 0x20 * (n), x) 
#define WR_REGISTER_SDBDPL(n, x)  controller->pci->WriteBAR32(0, 0x98 + 0x20 * (n), x) // Stream descriptor BDL pointer lower base address.
#define WR_REGISTER_SDBDPU(n, x)  controller->pci->WriteBAR32(0, 0x9C + 0x20 * (n), x) // Stream descriptor BDL pointer upper base address.

#define READ_PARAMETER_VENDOR_ID                   (0xF0000)
#define READ_PARAMETER_REVISION_ID                 (0xF0002)
#define READ_PARAMETER_CHILD_NODES                 (0xF0004)
#define READ_PARAMETER_FUNCTION_GROUP_TYPE         (0xF0005)
#define READ_PARAMETER_AUDIO_FUNCTION_CAPABILITIES (0xF0008)
#define READ_PARAMETER_AUDIO_WIDGET_CAPABILITIES   (0xF0009)
#define READ_PARAMETER_FORMAT_CAPABILITIES         (0xF000A)
#define READ_PARAMETER_STREAM_FORMATS              (0xF000B)
#define READ_PARAMETER_PIN_CAPABILITIES            (0xF000C)
#define READ_PARAMETER_INPUT_AMP_CAPABILITIES      (0xF000D)
#define READ_PARAMETER_CONNECTION_LIST_LENGTH      (0xF000E)
#define READ_PARAMETER_OUTPUT_AMP_CAPABILITIES     (0xF0012)

#define COMMAND_GET_CONNECTION_LIST_ENTRY(offset)                     (0xF0200 | (offset))
#define COMMAND_SET_CONNECTION_SELECT(index)                          (0x70100 | (index))
#define COMMAND_GET_AMPLIFIER_GAIN_MUTE(out, left, index)             (0xB0000 | ((out) ? (1 << 15) : 0) | ((left) ? (1 << 13) : 0) | (index))
#define COMMAND_SET_AMPLIFIER_GAIN_MUTE(out, left, index, mute, gain) (0x30000 | ((out) ? (1 << 15) : (1 << 14)) | ((left) ? (1 << 13) : (1 << 12)) \
		                                                               | ((index) << 8) | ((mute) ? (1 << 7) : 0) | (gain))
#define COMMAND_SET_CONVERTER_FORMAT(format)                          (0x20000 | (format))
#define COMMAND_SET_STREAM_NUMBER(stream, channel)                    (0x70600 | ((stream) << 4) | ((channel) << 0))
#define COMMAND_GET_PIN_WIDGET_CONTROL()                              (0xF0700)
#define COMMAND_SET_PIN_WIDGET_CONTROL(control)                       (0x70700 | (control))
#define COMMAND_PIN_SENSE()                                           (0xF0900)
#define COMMAND_GET_PIN_CONFIGURATION()                               (0xF1C00)
#define COMMAND_RESET()	                                              (0x7FF00)

#define HDA_WIDGET_AUDIO_OUTPUT (0)
#define HDA_WIDGET_AUDIO_INPUT  (1)
#define HDA_WIDGET_PIN_COMPLEX  (4)

#define PIN_MAYBE_CONNECTED (0)
#define PIN_UNCONNECTED     (1)
#define PIN_CONNECTED       (2)

struct HDAWidget : KDevice {
	uint32_t codec;
	uint32_t node;
	uint32_t functionGroup;
	uint32_t type;

#define MAXIMUM_INPUTS (32)
	uint8_t inputs[MAXIMUM_INPUTS];

	union {
		struct {
			uint32_t pinCapabilities, pinConfiguration;
			uint8_t pinIsConnected;
			bool pinIsInput, pinIsOutput;
		};
	};
};

struct HDAController : KDevice {
	KPCIDevice *pci;

	size_t outputStreamsSupported;
	size_t inputStreamsSupported;
	size_t bidirectionalStreamsSupported;

	size_t corbEntries, rirbEntries;
	uint8_t *corbVirtual, *rirbVirtual;
	uintptr_t corbPhysical, rirbPhysical;
	uintptr_t corbWritePointer, rirbReadPointer;
	uint32_t rirbLastSolicitedResponse;
	KEvent rirbReceivedSolicitedResponse;
};

static const char *const widgetTypeStrings[] = {
	"Audio output",
	"Audio input",
	"Audio mixer",
	"Audio selector",
	"Pin complex",
	"Power widget",
	"Volume knob",
	"Beep generator",
};

static const char *const portConnectivityStrings[] = {
	"jack",
	"none",
	"integrated",
	"jack and integrated",
};

static const char *const locationHighStrings[] = {
	"external",
	"internal",
	"separate",
	"other",
};

static const char *const locationLowStrings[] = {
	"??",
	"rear",
	"front",
	"left",
	"right",
	"top",
	"bottom",
	"special",
	"special",
	"special",
	"??",
	"??",
	"??",
	"??",
	"??",
	"??",
};

static const char *const defaultDeviceStrings[] = {
	"line out",
	"speaker",
	"HP out",
	"CD",
	"SPDIF out",
	"digital other out",
	"modem line side",
	"modem handset side",
	"line in",
	"AUX",
	"microphone in",
	"telephony",
	"SPDIF in",
	"digital other in",
	"??",
	"??",
};

static const char *const connectionTypeStrings[] = {
	"unknown",
	"1/8\" stereo/mono",
	"1/4\" stereo/mono",
	"ATAPI internal",
	"RCA",
	"optical",
	"other digital",
	"other analog",
	"multichannel analog (DIN)",
	"XLR/Professional",
	"RJ-11 (Modem)",
	"combination",
	"??",
	"??",
	"??",
	"??",
};

static const char *const colorStrings[] = {
	"unknown",
	"black",
	"grey",
	"blue",
	"green",
	"red",
	"orange",
	"yellow",
	"purple",
	"pink",
	"??",
	"??",
	"??",
	"??",
	"white",
	"other",
};

static bool HDAControllerSendCommand(HDAController *controller, uint32_t codec, uint32_t node, uint32_t data, uint32_t *response) {
	// TODO Test wrap-around.

	uint32_t command = (codec << 28) | (node << 20) | data;
	uintptr_t corbWritePointer = controller->corbWritePointer;
	corbWritePointer = (corbWritePointer + 1) % controller->corbEntries;
	((volatile uint32_t *) controller->corbVirtual)[corbWritePointer] = command;
	WR_REGISTER_CORBWP(ES_ISOLATE_BITS(RD_REGISTER_CORBWP(), 15, 8) | corbWritePointer);
	controller->corbWritePointer = corbWritePointer;
	if (!KEventWait(&controller->rirbReceivedSolicitedResponse, 500 /* half a second timeout */)) return false;
	if (response) *response = controller->rirbLastSolicitedResponse;
	return true;
}

static bool HDAControllerHandleIRQ(uintptr_t, void *context) {
	HDAController *controller = (HDAController *) context;
	uint32_t interruptStatus = RD_REGISTER_INTSTS();

	if (~interruptStatus & (1 << 31 /* global interrupt status */)) {
		return false;
	}

	if (interruptStatus & (1 << 30)) {
		uint8_t rirbStatus = RD_REGISTER_RIRBSTS();
		WR_REGISTER_RIRBSTS(rirbStatus);

		if (rirbStatus & (1 << 0 /* response interrupt */)) {
			uint8_t rirbWritePointer = RD_REGISTER_RIRBWP();

			while (controller->rirbReadPointer != rirbWritePointer) {
				controller->rirbReadPointer = (controller->rirbReadPointer + 1) % controller->rirbEntries;
				uint32_t response = ((volatile uint32_t *) controller->rirbVirtual)[controller->rirbReadPointer * 2 + 0];
				uint32_t extended = ((volatile uint32_t *) controller->rirbVirtual)[controller->rirbReadPointer * 2 + 1];

				if (~extended & (1 << 4)) {
					controller->rirbLastSolicitedResponse = response;
					KEventSet(&controller->rirbReceivedSolicitedResponse);
				}
			}
		}

	}

	return true;
}

static void HDAControllerExploreFunctionGroup(HDAController *controller, uint32_t codec, uint32_t functionGroupNode) {
	uint32_t type, childNodeCount;

	if (!HDAControllerSendCommand(controller, codec, functionGroupNode, READ_PARAMETER_FUNCTION_GROUP_TYPE, &type)
			|| !HDAControllerSendCommand(controller, codec, functionGroupNode, READ_PARAMETER_CHILD_NODES, &childNodeCount)) {
		return;
	}

	uint32_t firstChildNode = ES_EXTRACT_BITS(childNodeCount, 23, 16);
	childNodeCount = ES_EXTRACT_BITS(childNodeCount, 7, 0);
	type = ES_EXTRACT_BITS(type, 7, 0);

	KernelLog(LOG_INFO, "HDA", "found function group", "Found function group with type %d (%z), and child nodes %d to %d.\n", 
			type, type == 1 ? "audio" : type == 2 ? "modem" : "??", firstChildNode, firstChildNode + childNodeCount - 1);

	for (uintptr_t j = firstChildNode; j < firstChildNode + childNodeCount; j++) {
		uint32_t widgetCapabilities;

		if (!HDAControllerSendCommand(controller, codec, j, READ_PARAMETER_AUDIO_WIDGET_CAPABILITIES, &widgetCapabilities)) {
			continue;
		}

		uint32_t widgetType = ES_EXTRACT_BITS(widgetCapabilities, 23, 20);

		HDAWidget *widget = (HDAWidget *) KDeviceCreate("HD Audio widget", controller, sizeof(HDAWidget));
		widget->codec = codec;
		widget->node = j;
		widget->functionGroup = functionGroupNode;
		widget->type = widgetType;

		KernelLog(LOG_INFO, "HDA", "found widget", "Widget at node %d has type \"%z\".\n", 
				widget->node, widgetType >= sizeof(widgetTypeStrings) / sizeof(widgetTypeStrings[0]) ? "Other" : widgetTypeStrings[widgetType]);

		if (widgetCapabilities & (1 << 8)) {
			uint32_t connectionListLength, connectionList;
			
			if (HDAControllerSendCommand(controller, codec, widget->node, READ_PARAMETER_CONNECTION_LIST_LENGTH, &connectionListLength)
					&& (~connectionListLength & (1 << 7) /* long form not supported */)
					&& ES_EXTRACT_BITS(connectionListLength, 6, 0)) {
				uintptr_t index = 0;

				for (uintptr_t command = 0; command < (connectionListLength + 3) / 4; command++) {
					if (!HDAControllerSendCommand(controller, codec, widget->node, COMMAND_GET_CONNECTION_LIST_ENTRY(command * 4), &connectionList)) {
						break;
					}

					for (uintptr_t i = 0; i < (connectionListLength - command * 4) && index < MAXIMUM_INPUTS; i++) {
						uint8_t entry = connectionList >> (i * 8);

						if ((entry & 0x80) && index) {
							for (uintptr_t node = widget->inputs[index - 1]; node <= (entry & 0x7F) && index < MAXIMUM_INPUTS; node++) {
								widget->inputs[index++] = node;
							}
						} else {
							widget->inputs[index++] = entry;
						}
					}
				}
			}
		}

		for (uintptr_t i = 0; i < MAXIMUM_INPUTS; i++) {
			if (!widget->inputs[i]) break;
			KernelLog(LOG_INFO, "HDA", "widget connection", "Widget %d has possible input %d.\n", widget->node, widget->inputs[i]);
		}
	}

	for (uintptr_t i = 0; i < controller->children.Length(); i++) {
		HDAWidget *widget = (HDAWidget *) controller->children[i];

		if (widget->type == HDA_WIDGET_PIN_COMPLEX) {
			if (!HDAControllerSendCommand(controller, codec, widget->node, READ_PARAMETER_PIN_CAPABILITIES, &widget->pinCapabilities)
					|| !HDAControllerSendCommand(controller, codec, widget->node, COMMAND_GET_PIN_CONFIGURATION(), &widget->pinConfiguration)) {
				continue;
			}

			widget->pinIsOutput = widget->pinCapabilities & (1 << 4);
			widget->pinIsInput = widget->pinCapabilities & (1 << 5);

			KernelLog(LOG_INFO, "HDA", "pin information", "Pin %d has capabilities %x and configuration %x.%z%z\n", 
					widget->node, widget->pinCapabilities, widget->pinConfiguration,
					widget->pinIsOutput ? " Output." : "", widget->pinIsInput ? " Input." : "");

			if (!widget->pinIsOutput && !widget->pinIsInput) {
				continue;
			}

			uint32_t portConnectivity = ES_EXTRACT_BITS(widget->pinConfiguration, 31, 30);
			uint32_t location = ES_EXTRACT_BITS(widget->pinConfiguration, 29, 24);
			uint32_t defaultDevice = ES_EXTRACT_BITS(widget->pinConfiguration, 23, 20);
			uint32_t connectionType = ES_EXTRACT_BITS(widget->pinConfiguration, 19, 16);
			uint32_t color = ES_EXTRACT_BITS(widget->pinConfiguration, 15, 12);

			KernelLog(LOG_INFO, "HDA", "pin information", "Connectivity: %z; location: %z %z; default device: %z; connection type: %z; color: %z.\n", 
					portConnectivityStrings[portConnectivity],
					locationHighStrings[ES_EXTRACT_BITS(location, 5, 4)], locationLowStrings[ES_EXTRACT_BITS(location, 3, 0)],
					defaultDeviceStrings[defaultDevice], connectionTypeStrings[connectionType], colorStrings[color]);

			if (widget->pinCapabilities & (1 << 2)) {
				uint32_t pinSense;

				if (HDAControllerSendCommand(controller, codec, widget->node, COMMAND_PIN_SENSE(), &pinSense)) {
					widget->pinIsConnected = (pinSense & (1 << 31)) ? PIN_CONNECTED : PIN_UNCONNECTED;
					KernelLog(LOG_INFO, "HDA", "pin sense", "Pin sense: %z.\n", widget->pinIsConnected == PIN_CONNECTED ? "connected" : "unconnected");
				}
			}

			// TODO Register the device with the audio subsystem.
		}
	}
}

static void HDAControllerDestroy(KDevice *_controller) {
	HDAController *controller = (HDAController *) _controller;
	if (controller->corbVirtual) MMPhysicalFreeAndUnmap(controller->corbVirtual, controller->corbPhysical);
	if (controller->rirbVirtual) MMPhysicalFreeAndUnmap(controller->rirbVirtual, controller->rirbPhysical);
	// TODO Unregister interrupt handler.
}

static void HDAControllerAttach(KDevice *_parent) {
	HDAController *controller = (HDAController *) KDeviceCreate("HD Audio controller", _parent, sizeof(HDAController));

	if (!controller) {
		return;
	}

	controller->destroy = HDAControllerDestroy;
	controller->rirbReceivedSolicitedResponse.autoReset = true;

	controller->pci = (KPCIDevice *) _parent;
	controller->pci->EnableFeatures(K_PCI_FEATURE_INTERRUPTS | K_PCI_FEATURE_BUSMASTERING_DMA 
			| K_PCI_FEATURE_MEMORY_SPACE_ACCESS | K_PCI_FEATURE_BAR_0);

	uint16_t globalCapabilities = RD_REGISTER_GCAP();
	bool supports64BitAddresses = globalCapabilities & (1 << 0);

#ifdef ARCH_64
	if (!supports64BitAddresses) {
		KernelLog(LOG_ERROR, "HDA", "controller unsupported", "Controller does not support 64-bit addresses.\n");
		KDeviceDestroy(controller);
		return;
	}
#endif

	controller->outputStreamsSupported = ES_EXTRACT_BITS(globalCapabilities, 15, 12);
	controller->inputStreamsSupported = ES_EXTRACT_BITS(globalCapabilities, 11, 8);
	controller->bidirectionalStreamsSupported = ES_EXTRACT_BITS(globalCapabilities, 7, 3);

	KernelLog(LOG_INFO, "HDA", "global capabilities", "Controller supports %d output streams, %d input streams and %d bidi streams.\n", 
			controller->outputStreamsSupported, controller->inputStreamsSupported, controller->bidirectionalStreamsSupported);
	KernelLog(LOG_INFO, "HDA", "version", "Controller reports version %d.%d.\n", 
			RD_REGISTER_VMAJ(), RD_REGISTER_VMIN());

	KTimeout timeout(1000); // The initialisation process shouldn't take more than a second.

#define CHECK_TIMEOUT(message) \
	if (timeout.Hit()) { \
		KernelLog(LOG_ERROR, "HDA", "timeout", "Timeout during initialization: " message ".\n"); \
		KDeviceDestroy(controller); \
		return; \
	}

	// Reset the controller.

	WR_REGISTER_GCTL(RD_REGISTER_GCTL() & ~(1 << 0 /* CRST */));
	while ((RD_REGISTER_GCTL() & (1 << 0)) && timeout.Hit());
	CHECK_TIMEOUT("clear CRST bit");
	WR_REGISTER_GCTL(RD_REGISTER_GCTL() | (1 << 0 /* CRST */));
	while ((~RD_REGISTER_GCTL() & (1 << 0)) && timeout.Hit());
	CHECK_TIMEOUT("set CRST bit");

	// Setup CORB/RIRB.

	uint8_t corbSize = RD_REGISTER_CORBSIZE();
	controller->corbEntries = (corbSize & (1 << 6)) ? 256 : (corbSize & (1 << 5)) ? 16 : (corbSize & (1 << 4)) ? 2 : 0;
	uint8_t rirbSize = RD_REGISTER_RIRBSIZE();
	controller->rirbEntries = (rirbSize & (1 << 6)) ? 256 : (rirbSize & (1 << 5)) ? 16 : (rirbSize & (1 << 4)) ? 2 : 0;

	if (!controller->corbEntries || !controller->rirbEntries) {
		KernelLog(LOG_ERROR, "HDA", "unsupported", "Controller does not support any recognised CORB/RIRB sizes.\n");
		KDeviceDestroy(controller);
		return;
	}

	if (!MMPhysicalAllocateAndMap(controller->corbEntries * 4, 128, 0, true, 
				MM_REGION_NOT_CACHEABLE, &controller->corbVirtual, &controller->corbPhysical)
			|| !MMPhysicalAllocateAndMap(controller->rirbEntries * 8, 128, 0, true, 
				MM_REGION_NOT_CACHEABLE, &controller->rirbVirtual, &controller->rirbPhysical)) {
		KernelLog(LOG_ERROR, "HDA", "insufficient resources", "Could not allocate memory for CORB/RIRB.\n");
		KDeviceDestroy(controller);
		return;
	}

	WR_REGISTER_CORBSIZE(ES_ISOLATE_BITS(RD_REGISTER_CORBSIZE(), 7, 2) | (controller->corbEntries == 16 ? 1 : controller->corbEntries == 256 ? 2 : 0));
	WR_REGISTER_RIRBSIZE(ES_ISOLATE_BITS(RD_REGISTER_RIRBSIZE(), 7, 2) | (controller->rirbEntries == 16 ? 1 : controller->rirbEntries == 256 ? 2 : 0));

	WR_REGISTER_CORBLBASE(controller->corbPhysical & 0xFFFFFFFF);
	if (supports64BitAddresses) WR_REGISTER_CORBUBASE(controller->corbPhysical >> 32);
	WR_REGISTER_RIRBLBASE(controller->rirbPhysical & 0xFFFFFFFF);
	if (supports64BitAddresses) WR_REGISTER_RIRBUBASE(controller->rirbPhysical >> 32);

	WR_REGISTER_RINTCNT(ES_ISOLATE_BITS(RD_REGISTER_RINTCNT(), 15, 8) | 1 /* interrupt after every response */);

	WR_REGISTER_CORBCTL(ES_ISOLATE_BITS(RD_REGISTER_CORBCTL(), 7, 2) | (1 << 1 /* run */) | (1 << 0 /* interrupt on error */));
	WR_REGISTER_RIRBCTL(ES_ISOLATE_BITS(RD_REGISTER_RIRBCTL(), 7, 3) | (1 << 1 /* run */) | (1 << 0 /* interrupt on response */));

	if ((~RD_REGISTER_CORBCTL() & (1 << 1)) || (~RD_REGISTER_RIRBCTL() & (1 << 1))) {
		KernelLog(LOG_ERROR, "HDA", "start error", "Could not start the CORB/RIRB.\n");
		KDeviceDestroy(controller);
		return;
	}

	// Setup interrupts.

	if (!controller->pci->EnableSingleInterrupt(HDAControllerHandleIRQ, controller, "HDA")) {
		KernelLog(LOG_ERROR, "HDA", "insufficient resources", "Could not register interrupt handler.\n");
		KDeviceDestroy(controller);
		return;
	}

	WR_REGISTER_INTCTL((1 << 31) | (1 << 30));

	// Enumerate codecs.
	
	uint16_t codecs = RD_REGISTER_STATESTS();

	for (uintptr_t i = 0; i < 15; i++) {
		if (~codecs & (1 << i)) {
			continue;
		}

		uint32_t vendorID, childNodeCount;

		if (HDAControllerSendCommand(controller, i, 0, READ_PARAMETER_VENDOR_ID, &vendorID)
				&& HDAControllerSendCommand(controller, i, 0, READ_PARAMETER_CHILD_NODES, &childNodeCount)) {
			uint32_t firstChildNode = ES_EXTRACT_BITS(childNodeCount, 23, 16);
			childNodeCount = ES_EXTRACT_BITS(childNodeCount, 7, 0);

			KernelLog(LOG_INFO, "HDA", "found codec", "Found codec with vendor ID %x, and child nodes %d to %d.\n", 
					vendorID, firstChildNode, firstChildNode + childNodeCount - 1);

			for (uintptr_t j = firstChildNode; j < firstChildNode + childNodeCount; j++) {
				HDAControllerExploreFunctionGroup(controller, i, j);
			}
		}
	}

	// TODO Enable unsolicited responses.
	// TODO Support hotplugging.

	KernelLog(LOG_INFO, "HDA", "ready", "Controller %x successfully initialized.\n", controller);
}

KDriver driverHDAudio = {
	.attach = HDAControllerAttach,
};
