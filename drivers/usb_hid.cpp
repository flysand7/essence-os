#include <module.h>

// #define TRACE_REPORTS

// TODO Key repeat not working on Qemu.

struct ReportItem {
	uint32_t usage, application, arrayCount;
	int32_t logicalMinimum, logicalMaximum;

	uint8_t reportPrefix;
	uint8_t bits;
	uint8_t group;

#define REPORT_ITEM_CONSTANT   (1 << 0)
#define REPORT_ITEM_RELATIVE   (1 << 1)
#define REPORT_ITEM_WRAP       (1 << 2)
#define REPORT_ITEM_NON_LINEAR (1 << 3)
#define REPORT_ITEM_SIGNED     (1 << 4)
#define REPORT_ITEM_ARRAY      (1 << 5)
	uint8_t flags;

#define REPORT_ITEM_INPUT   (1)
#define REPORT_ITEM_OUTPUT  (2)
#define REPORT_ITEM_FEATURE (3)
	uint8_t type;
};

struct BitBuffer {
	const uint8_t *buffer;
	size_t bytes;
	uintptr_t index;

	void Discard(size_t count);
	uint32_t ReadUnsigned(size_t count);
	int32_t ReadSigned(size_t count);
};

struct GameController {
	uint64_t id;
	uint8_t reportPrefix;
};

struct HIDDevice : KHIDevice {
	KUSBDevice *device;

	Array<ReportItem, K_FIXED> reportItems;
	bool usesReportPrefixes;

	Array<GameController, K_FIXED> gameControllers;

	KUSBEndpointDescriptor *reportEndpoint;
	uint8_t *lastReport;
	size_t lastReportBytes;

	void Initialise();
	bool ParseReportDescriptor(const uint8_t *report, size_t reportBytes);
	void ReportReceived(BitBuffer *buffer);
};

struct HIDDescriptorLink {
	uint8_t type;
	uint8_t length[2];
};

struct HIDDescriptor : KUSBDescriptorHeader {
	uint8_t specification[2];
	uint8_t countryCode;
	uint8_t linkCount;
	HIDDescriptorLink links[1];
};

struct ReportGlobalState {
	int32_t logicalMinimum, logicalMaximum;
	uint16_t usagePage;
	uint8_t reportSize, reportCount;
	uint8_t reportID;
};

struct ReportLocalState {
#define USAGE_ARRAY_SIZE (32)
	uint32_t usages[USAGE_ARRAY_SIZE];
	uint32_t usageMinimum, usageMaximum;
	uint8_t usageCount;

#define DELIMITER_NONE   (0)
#define DELIMITER_FIRST  (1)
#define DELIMITER_IGNORE (2)
	uint8_t delimiterState;
};

struct UsageString {
	uint32_t usage;
	const char *string;
};

#define HID_APPLICATION_MOUSE 		(0x010002)
#define HID_APPLICATION_JOYSTICK	(0x010004)
#define HID_APPLICATION_KEYBOARD 	(0x010006)
#define HID_USAGE_X_AXIS      		(0x010030)
#define HID_USAGE_Y_AXIS      		(0x010031)
#define HID_USAGE_Z_AXIS      		(0x010032)
#define HID_USAGE_X_ROTATION      	(0x010033)
#define HID_USAGE_Y_ROTATION      	(0x010034)
#define HID_USAGE_Z_ROTATION      	(0x010035)
#define HID_USAGE_WHEEL    		(0x010038)
#define HID_USAGE_HAT_SWITCH		(0x010039)
#define HID_USAGE_KEYCODES		(0x070000)
#define HID_USAGE_BUTTON_1    		(0x090001)
#define HID_USAGE_BUTTON_2    		(0x090002)
#define HID_USAGE_BUTTON_3    		(0x090003)
#define HID_USAGE_BUTTON_16    		(0x090010)

UsageString usageStrings[] = {
	{ 0x000000, "padding" },

	// Generic desktop page.
	{ 0x010001, "pointer" },
	{ 0x010002, "mouse" },
	{ 0x010004, "joystick" },
	{ 0x010005, "gamepad" },
	{ 0x010006, "keyboard" },
	{ 0x010007, "keypad" },
	{ 0x010008, "multi-axis controller" },
	{ 0x010009, "tablet PC system controls" },
	{ 0x010030, "X axis" },
	{ 0x010031, "Y axis" },
	{ 0x010032, "Z axis" },
	{ 0x010033, "X rotation" },
	{ 0x010034, "Y rotation" },
	{ 0x010035, "Z rotation" },
	{ 0x010036, "slider" },
	{ 0x010037, "dial" },
	{ 0x010038, "wheel" },
	{ 0x010039, "hat switch" },

	// Keyboard/keypad page.
	{ 0x070000, "keycodes" },
	{ 0x0700E0, "left ctrl" },
	{ 0x0700E1, "left shift" },
	{ 0x0700E2, "left alt" },
	{ 0x0700E3, "left gui" },
	{ 0x0700E4, "right ctrl" },
	{ 0x0700E5, "right shift" },
	{ 0x0700E6, "right alt" },
	{ 0x0700E7, "right gui" },

	// LED page.
	{ 0x080001, "num lock" },
	{ 0x080002, "caps lock" },
	{ 0x080003, "scroll lock" },
	{ 0x080004, "compose" },
	{ 0x080005, "kana" },

	// Button page.
	{ 0x090001, "button 1" },
	{ 0x090002, "button 2" },
	{ 0x090003, "button 3" },
	{ 0x090004, "button 4" },
	{ 0x090005, "button 5" },
	{ 0x090006, "button 6" },
	{ 0x090007, "button 7" },
	{ 0x090008, "button 8" },
	{ 0x090009, "button 9" },
	{ 0x09000A, "button 10" },
	{ 0x09000B, "button 11" },
	{ 0x09000C, "button 12" },
	{ 0x09000D, "button 13" },
	{ 0x09000E, "button 14" },
	{ 0x09000F, "button 15" },
	{ 0x090010, "button 16" },
};

const char *LookupUsageString(uint32_t usage) {
	if (usage > 0xFF000000) {
		return "vendor-specific";
	}

	for (uintptr_t i = 0; i < sizeof(usageStrings) / sizeof(usageStrings[0]); i++) {
		if (usageStrings[i].usage == usage) {
			return usageStrings[i].string;
		}
	}

	EsPrint("unknown usage %x\n", usage);
	return "unknown";
}

void BitBuffer::Discard(size_t count) {
	index += count;
}

uint32_t BitBuffer::ReadUnsigned(size_t count) {
	uint32_t result = 0;
	uint32_t bit = 0;

	while (bit != count) {
		uintptr_t byte = index >> 3;

		if (byte >= bytes) {
			break;
		}

		if (buffer[byte] & (1 << (index & 7))) {
			result |= 1 << bit;
		}

		bit++, index++;
	}

	return result;
}

int32_t BitBuffer::ReadSigned(size_t count) {
	if (!count) return 0;

	uint32_t result = ReadUnsigned(count);

	if (result & (1 << (count - 1))) {
		for (uintptr_t i = count; i < 32; i++) {
			result |= 1 << i;
		}
	}

	return result;
}

bool HIDDevice::ParseReportDescriptor(const uint8_t *report, size_t reportBytes) {
#define REPORT_GLOBAL_STACK_SIZE (8)
	ReportGlobalState global[REPORT_GLOBAL_STACK_SIZE] = {};
	uintptr_t gIndex = 0;
	ReportLocalState local = {};
	uint32_t application = 0;
	uint8_t group = 0;

	uintptr_t position = 0;

	while (position < reportBytes) {
		uint8_t header = report[position];

		if (header == 0xFE) {
			// Long items, unused.
			if (position + 3 > reportBytes) return false;
			position += 3 + report[position + 1];
			continue;
		}

		uint8_t size = header & 3;
		uint8_t type = header & ~3;
		position++;

		if (size == 3) size++;
		if (position + size > reportBytes) return false;

		uint32_t uData = 0;
		int32_t sData = 0;

		for (uintptr_t i = 0; i < size; i++) {
			uData |= report[position + i] << (i * 8);
		}

		sData = uData;

		if (size && (report[position + size - 1] & 0x80)) {
			for (uintptr_t i = size; i < 4; i++) {
				sData |= 0xFF << (i * 8);
			}
		}

		position += size;

		switch (type) {
			case 0b00000100: { global[gIndex].usagePage      = uData; } break;
			case 0b00010100: { global[gIndex].logicalMinimum = sData; } break;
			case 0b00100100: { global[gIndex].logicalMaximum = sData; } break;
			case 0b01110100: { global[gIndex].reportSize     = uData; } break;
			case 0b10010100: { global[gIndex].reportCount    = uData; } break;

			case 0b10000100: { 
				global[gIndex].reportID = uData; 
				if (uData) usesReportPrefixes = true;
			} break;

			case 0b10100100: {
				if (gIndex + 1 == REPORT_GLOBAL_STACK_SIZE) return false;
				gIndex++;
				global[gIndex] = global[gIndex - 1];
			} break;

			case 0b10110100: {
				if (gIndex == 0) return false;
				gIndex--;
			} break;

			case 0b00001000: {
				if (local.usageCount == USAGE_ARRAY_SIZE) return false;

				if (local.delimiterState != DELIMITER_IGNORE) {
					local.usages[local.usageCount++] = uData | (size < 4 ? (global[gIndex].usagePage << 16) : 0);
				}

				if (local.delimiterState == DELIMITER_FIRST) local.delimiterState = DELIMITER_IGNORE;
			} break;

			case 0b00011000: { local.usageMinimum = uData | (size < 4 ? (global[gIndex].usagePage << 16) : 0); } break;
			case 0b00101000: { local.usageMaximum = uData | (size < 4 ? (global[gIndex].usagePage << 16) : 0); } break;

			case 0b10101000: { 
				if (uData) local.delimiterState = DELIMITER_FIRST;
				else local.delimiterState = DELIMITER_NONE;
			} break;

			case 0b10100000: { 
				if (uData == 1) {
					if (local.usageCount == 0) return false;
					application = local.usages[0]; 
					group++;
				}
			} break;

			case 0b10010000:
			case 0b10110000:
			case 0b10000000: {
				for (uintptr_t i = 0; i < global[gIndex].reportCount; i++) {
					ReportItem item = {
						.application = application,
						.logicalMinimum = global[gIndex].logicalMinimum,
						.logicalMaximum = global[gIndex].logicalMaximum,
						.reportPrefix = global[gIndex].reportID,
						.bits = global[gIndex].reportSize,
					};

					if (type == 0b10000000) {
						item.type = REPORT_ITEM_INPUT;
					} else if (type == 0b10010000) {
						item.type = REPORT_ITEM_OUTPUT;
					} else if (type == 0b10110000) {
						item.type = REPORT_ITEM_FEATURE;
					}

					if ( uData & (1 << 0)) item.flags |= REPORT_ITEM_CONSTANT;
					if (~uData & (1 << 1)) item.flags |= REPORT_ITEM_ARRAY;
					if ( uData & (1 << 2)) item.flags |= REPORT_ITEM_RELATIVE;
					if ( uData & (1 << 3)) item.flags |= REPORT_ITEM_WRAP;
					if ( uData & (1 << 4)) item.flags |= REPORT_ITEM_NON_LINEAR;
					if (item.logicalMinimum < 0 || item.logicalMaximum < 0) item.flags |= REPORT_ITEM_SIGNED;

					if (local.usageCount) {
						item.usage = local.usages[i >= local.usageCount ? local.usageCount - 1 : i];
					} else {
						item.usage = (i > local.usageMaximum - local.usageMinimum) ? local.usageMaximum : (local.usageMinimum + i);
					}

					if (item.flags & REPORT_ITEM_ARRAY) {
						item.arrayCount = global[gIndex].reportCount;
					}

					if (!reportItems.Add(item)) {
						return false;
					}

					KernelLog(LOG_INFO, "USBHID", "parsed report item", 
							"Parsed report item - group: %d, application: '%z', usage: '%z', range: %i->%i, report: %d, bits: %d, "
							"flags: %z%z%z%z%z%z, type: %z, array count: %d\n",
							group, LookupUsageString(item.application), LookupUsageString(item.usage),
							item.logicalMinimum, item.logicalMaximum, 
							item.reportPrefix, item.bits, 
							(item.flags & REPORT_ITEM_CONSTANT) ? "constant|" : "", 
							(item.flags & REPORT_ITEM_RELATIVE) ? "relative|" : "", 
							(item.flags & REPORT_ITEM_WRAP) ? "wrap|" : "", 
							(item.flags & REPORT_ITEM_NON_LINEAR) ? "non-linear|" : "", 
							(item.flags & REPORT_ITEM_ARRAY) ? "array|" : "", 
							(item.flags & REPORT_ITEM_SIGNED) ? "signed" : "unsigned", 
							item.type == REPORT_ITEM_INPUT ? "input" : item.type == REPORT_ITEM_OUTPUT ? "output" : "feature", item.arrayCount);

					if (item.application == HID_APPLICATION_KEYBOARD) {
						cDebugName = "USB HID keyboard";
					} else if (item.application == HID_APPLICATION_MOUSE) {
						cDebugName = "USB HID mouse";
					} else if (item.application == HID_APPLICATION_JOYSTICK) {
						cDebugName = "USB HID joystick";
					}

					if (item.flags & REPORT_ITEM_ARRAY) {
						break;
					}
				}
			} break;
		}

		if ((type & 0b00001100) == 0) {
			EsMemoryZero(&local, sizeof(ReportLocalState));
		}
	}

	return true;
}

void ReportReceivedCallback(ptrdiff_t bytesNotTransferred, EsGeneric context) {
	HIDDevice *device = (HIDDevice *) context.p;
	size_t bytesTransferred = device->reportEndpoint->GetMaximumPacketSize() - bytesNotTransferred;

	if (bytesNotTransferred == -1) {
		KernelLog(LOG_ERROR, "USBHID", "report transfer failure", "Report transfer failed.\n");
		bytesTransferred = 0;
	}

	BitBuffer buffer = { device->lastReport, bytesTransferred };
	device->ReportReceived(&buffer);
}

void HIDDevice::ReportReceived(BitBuffer *buffer) {
	uint8_t prefix = 0;

	if (usesReportPrefixes) {
		prefix = buffer->ReadUnsigned(8);
	}

#ifdef TRACE_REPORTS
	EsPrint("-- report (%d) --\n", prefix);
#endif

	bool mouseEvent = false;
	KMouseUpdateData mouse = {};
	bool keyboardEvent = false;
	uint16_t keysDown[32];
	size_t keysDownCount = 0;
	bool gameControllerEvent = false;
	EsGameControllerState controllerState = {};
	controllerState.directionalPad = 15;
	controllerState.analogCount = 2;

	for (uintptr_t i = 0; i < gameControllers.Length(); i++) {
		if (gameControllers[i].reportPrefix == prefix) {
			controllerState.id = gameControllers[i].id;
		}
	}

	for (uintptr_t i = 0; i < reportItems.Length(); i++) {
		ReportItem *item = &reportItems[i];

		if (item->type != REPORT_ITEM_INPUT || item->reportPrefix != prefix) {
			continue;
		}

#ifdef TRACE_REPORTS
		uintptr_t startIndex = buffer->index;

		EsPrint("%d/%z: ", item->group, LookupUsageString(item->usage));

		size_t count = (item->flags & REPORT_ITEM_ARRAY) ? item->arrayCount : 1;

		for (uintptr_t i = 0; i < count; i++) {
			if (item->flags & REPORT_ITEM_SIGNED) {
				EsPrint("%i", buffer->ReadSigned(item->bits));
			} else {
				EsPrint("%d", buffer->ReadUnsigned(item->bits));
			}

			if (i != count - 1) {
				EsPrint(", ");
			}
		}

		EsPrint("\n");

		buffer->index = startIndex;
#endif

		if (item->flags & REPORT_ITEM_ARRAY) {
			bool handled = false;

			if (item->application == HID_APPLICATION_KEYBOARD) {
				if (item->usage == HID_USAGE_KEYCODES) {
					for (uintptr_t i = 0; i < item->arrayCount; i++) {
						uint32_t scancode = buffer->ReadUnsigned(item->bits);
						keyboardEvent = true;

						if (scancode > 0 && scancode < 0x200 && keysDownCount != 32) {
							keysDown[keysDownCount++] = scancode;
						}
					}
				}
			}

			if (!handled) {
				buffer->Discard(item->bits * item->arrayCount);
			}
		} else {
			bool handled = false;

			if (item->application == HID_APPLICATION_MOUSE) {
				// TODO Handle absolute, and wrapping movements.

				mouseEvent = true;
				handled = true;

				if (item->usage == HID_USAGE_X_AXIS) {
					if (item->flags & REPORT_ITEM_SIGNED) {
						mouse.xMovement = buffer->ReadSigned(item->bits) * K_CURSOR_MOVEMENT_SCALE;
					} else {
						mouse.xMovement = buffer->ReadUnsigned(item->bits) * K_CURSOR_MOVEMENT_SCALE;
					}

					mouse.xIsAbsolute = !(item->flags & REPORT_ITEM_RELATIVE);
					mouse.xFrom = item->logicalMinimum; 
					mouse.xTo = item->logicalMaximum;
				} else if (item->usage == HID_USAGE_Y_AXIS) {
					if (item->flags & REPORT_ITEM_SIGNED) {
						mouse.yMovement = buffer->ReadSigned(item->bits) * K_CURSOR_MOVEMENT_SCALE;
					} else {
						mouse.yMovement = buffer->ReadUnsigned(item->bits) * K_CURSOR_MOVEMENT_SCALE;
					}

					mouse.yIsAbsolute = !(item->flags & REPORT_ITEM_RELATIVE);
					mouse.yFrom = item->logicalMinimum; 
					mouse.yTo = item->logicalMaximum;
				} else if (item->usage == HID_USAGE_BUTTON_1) {
					if (buffer->ReadUnsigned(item->bits)) mouse.buttons |= 1 << 0;
				} else if (item->usage == HID_USAGE_BUTTON_2) {
					if (buffer->ReadUnsigned(item->bits)) mouse.buttons |= 1 << 2;
				} else if (item->usage == HID_USAGE_BUTTON_3) {
					if (buffer->ReadUnsigned(item->bits)) mouse.buttons |= 1 << 1;
				} else if (item->usage == HID_USAGE_WHEEL) {
					mouse.yScroll = buffer->ReadSigned(item->bits) * K_CURSOR_MOVEMENT_SCALE;
				} else {
					handled = false;
				}
			} else if (item->application == HID_APPLICATION_KEYBOARD) {
				handled = true;

				if (item->usage > HID_USAGE_KEYCODES && item->usage < HID_USAGE_KEYCODES + 0x200) {
					handled = true;
					keyboardEvent = true;

					if (buffer->ReadUnsigned(item->bits) && keysDownCount != 32) {
						keysDown[keysDownCount++] = item->usage - HID_USAGE_KEYCODES;
					}
				}
			} else if (item->application == HID_APPLICATION_JOYSTICK) {
				handled = true;

				if (item->usage >= HID_USAGE_BUTTON_1 && item->usage <= HID_USAGE_BUTTON_16) {
					gameControllerEvent = true;

					if (buffer->ReadUnsigned(item->bits)) {
						controllerState.buttons |= 1 << controllerState.buttonCount;
					}

					controllerState.buttonCount++;
				} else if (item->usage == HID_USAGE_X_AXIS) {
					gameControllerEvent = true;
					controllerState.analog[0].x = buffer->ReadUnsigned(item->bits) * 0xFF / item->logicalMaximum;
				} else if (item->usage == HID_USAGE_Y_AXIS) {
					gameControllerEvent = true;
					controllerState.analog[0].y = buffer->ReadUnsigned(item->bits) * 0xFF / item->logicalMaximum;
				} else if (item->usage == HID_USAGE_Z_AXIS) {
					gameControllerEvent = true;
					controllerState.analog[0].z = buffer->ReadUnsigned(item->bits) * 0xFF / item->logicalMaximum;
				} else if (item->usage == HID_USAGE_X_ROTATION) {
					gameControllerEvent = true;
					controllerState.analog[1].x = buffer->ReadUnsigned(item->bits) * 0xFF / item->logicalMaximum;
				} else if (item->usage == HID_USAGE_Y_ROTATION) {
					gameControllerEvent = true;
					controllerState.analog[1].y = buffer->ReadUnsigned(item->bits) * 0xFF / item->logicalMaximum;
				} else if (item->usage == HID_USAGE_Z_ROTATION) {
					gameControllerEvent = true;
					controllerState.analog[1].z = buffer->ReadUnsigned(item->bits) * 0xFF / item->logicalMaximum;
				} else if (item->usage == HID_USAGE_HAT_SWITCH) {
					gameControllerEvent = true;
					controllerState.directionalPad = buffer->ReadUnsigned(item->bits);
				} else {
					handled = false;
				}
			}

			if (!handled) {
				buffer->Discard(item->bits);
			}
		}
	}

	if (mouseEvent) {
		KMouseUpdate(&mouse);
	}

	if (keyboardEvent) {
		KKeyboardUpdate(keysDown, keysDownCount);
	}

	if (gameControllerEvent) {
		KGameControllerUpdate(&controllerState);
	}

	if (device->flags & K_DEVICE_REMOVED) {
		KDeviceCloseHandle(this);
	} else {
		if (!device->queueTransfer(device, reportEndpoint, ReportReceivedCallback, 
					lastReport, reportEndpoint->GetMaximumPacketSize(), this)) {
			KernelLog(LOG_ERROR, "USBHID", "setup transfer failure", "Could not setup the interrupt input transfer to receive the next report packet.\n");
			KDeviceCloseHandle(this);
		}
	}
}

void HIDDevice::Initialise() {
	// Find the HID descriptor.

	HIDDescriptor *hidDescriptor = (HIDDescriptor *) device->GetCommonDescriptor(0x21, 0);

	if (!hidDescriptor) {
		KernelLog(LOG_ERROR, "USBHID", "missing descriptor", "Could not find the HID descriptor.\n");
		return;
	} else if (hidDescriptor->length < sizeof(HIDDescriptor) || hidDescriptor->linkCount == 0 
			|| (hidDescriptor->linkCount - 1) * sizeof(HIDDescriptorLink) + sizeof(HIDDescriptor) > hidDescriptor->length) {
		KernelLog(LOG_ERROR, "USBHID", "bad descriptor length", "HID descriptor too short (%D) for %d links.\n",
				hidDescriptor->length, hidDescriptor->linkCount);
		return;
	}

	// Get the size of the report descriptor.

	size_t reportBytes = 0;

	for (uintptr_t i = 0; i < hidDescriptor->linkCount; i++) {
		if (hidDescriptor->links[i].type == 0x22) {
			reportBytes = (size_t) hidDescriptor->links[i].length[0] | ((size_t) hidDescriptor->links[i].length[1] << 8);
		}
	}

	if (!reportBytes) {
		KernelLog(LOG_ERROR, "USBHID", "no report descriptor", "Could not find report descriptor link in HID descriptor.\n");
		return;
	}

	// Switch to the report protocol.
	
	if (!device->controlTransfer(device, 0x21, 0x0B /* set protocol */, 1 /* report protocol */, 
				device->interfaceDescriptor.interfaceIndex, nullptr, 0, K_ACCESS_WRITE, nullptr)) {
		KernelLog(LOG_ERROR, "USBHID", "set protocol failure", "Could not switch to the report protocol.\n");
	}

	// Get the report descriptor and parse it.

	uint8_t *report = (uint8_t *) EsHeapAllocate(reportBytes, false, K_FIXED);

	if (!report) {
		KernelLog(LOG_ERROR, "USBHID", "allocation failure", "Could not allocate buffer to store the report descriptor.\n");
		return;
	}

	EsDefer(EsHeapFree(report, reportBytes, K_FIXED));

	uint16_t transferred = 0;

	if (!device->controlTransfer(device, 0x81, 0x06, 0x22 << 8, 0, report, reportBytes, K_ACCESS_READ, &transferred)
			|| transferred != reportBytes) {
		KernelLog(LOG_ERROR, "USBHID", "no report descriptor", "Could not read the report descriptor from the device.\n");
		return;
	}

	if (!ParseReportDescriptor(report, reportBytes)) {
		KernelLog(LOG_ERROR, "USBHID", "invalid report descriptor", "Could not parse the report descriptor.\n");
		return;
	}

	// Set idle.
	
	if (!device->controlTransfer(device, 0x21, 0x0A /* set idle */, 0 /* infinite duration, apply to all report IDs */, 
				device->interfaceDescriptor.interfaceIndex, nullptr, 0, K_ACCESS_WRITE, nullptr)) {
		KernelLog(LOG_ERROR, "USBHID", "enable idle failure", "Could not enable idle mode on the device.\n");
		return;
	}

	// Get the interrupt-in endpoint descriptor.

	KUSBEndpointDescriptor *endpoint = nullptr;

	{
		uintptr_t index = 0;

		while (true) {
			KUSBEndpointDescriptor *e = (KUSBEndpointDescriptor *) device->GetCommonDescriptor(0x05 /* endpoint */, index++);

			if (!e) {
				break;
			} else if (e->IsInterrupt() && e->IsInput()) {
				endpoint = e;
				break;
			}
		}
	}

	lastReport = (uint8_t *) EsHeapAllocate(endpoint->GetMaximumPacketSize(), true, K_FIXED);

	if (!lastReport) {
		KernelLog(LOG_ERROR, "USBHID", "allocation failure", "Could not allocate buffer to store received reports.\n");
		return;
	}

	// Start receiving interrupt packets.

	reportEndpoint = endpoint;
	KDeviceOpenHandle(this);

	if (!device->queueTransfer(device, endpoint, ReportReceivedCallback, lastReport, endpoint->GetMaximumPacketSize(), this)) {
		KernelLog(LOG_ERROR, "USBHID", "setup transfer failure", "Could not setup the interrupt input transfer to receive report packets.\n");
		KDeviceCloseHandle(this);
		return;
	}

	// Work out if this is a keyboard or mouse.

	for (uintptr_t i = 0; i < reportItems.Length(); i++) {
		ReportItem *item = &reportItems[i];

		if (item->application == HID_APPLICATION_KEYBOARD && item->usage == HID_USAGE_KEYCODES) {
			KDeviceSendConnectedMessage(this, ES_DEVICE_KEYBOARD);
			break;
		} else if (item->application == HID_APPLICATION_MOUSE) {
			KDeviceSendConnectedMessage(this, ES_DEVICE_MOUSE);
			break;
		}
	}

	// If this is a game controller, tell the window manager it's been connected.

	{
		uint64_t seen = 0;

		for (uintptr_t i = 0; i < reportItems.Length(); i++) {
			ReportItem *item = &reportItems[i];
			
			if (item->application == HID_APPLICATION_JOYSTICK 
					&& item->reportPrefix < 64 
					&& (~seen & (1 << item->reportPrefix))) {
				seen |= (1 << item->reportPrefix);

				GameController controller = {};
				controller.id = KGameControllerConnect();

				if (controller.id) {
					controller.reportPrefix = item->reportPrefix;
					gameControllers.Add(controller);
				}
			}
		}
	}
}

static void DeviceDestroy(KDevice *_device) {
	HIDDevice *device = (HIDDevice *) _device;

	for (uintptr_t i = 0; i < device->gameControllers.Length(); i++) {
		KGameControllerDisconnect(device->gameControllers[i].id);
	}

	device->reportItems.Free();
	device->gameControllers.Free();
	EsHeapFree(device->lastReport, 0, K_FIXED);
}

static void DeviceAttach(KDevice *parent) {
	HIDDevice *device = (HIDDevice *) KDeviceCreate("USB HID", parent, sizeof(HIDDevice));

	if (!device) {
		KernelLog(LOG_ERROR, "USBHID", "allocation failure", "Could not allocate HIDDevice structure.\n");
		return;
	}

	device->destroy = DeviceDestroy;
	device->device = (KUSBDevice *) parent;
	device->Initialise();
	KRegisterHIDevice(device);
}

KDriver driverUSBHID = {
	.attach = DeviceAttach,
};
