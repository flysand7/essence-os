#include <module.h>

#define SETUP_FLAG_D2H       (0x80)

#define DESCRIPTOR_DEVICE        (1)
#define DESCRIPTOR_CONFIGURATION (2)
#define DESCRIPTOR_STRING        (3)
#define DESCRIPTOR_INTERFACE     (4)
#define DESCRIPTOR_ENDPOINT      (5)

KUSBDescriptorHeader *KUSBDevice::GetCommonDescriptor(uint8_t type, uintptr_t index) {
	uintptr_t position = selectedConfigurationOffset;

	while (position < configurationDescriptorsBytes) {
		KUSBDescriptorHeader *header = (KUSBDescriptorHeader *) (configurationDescriptors + position);

		if (header->descriptorType == DESCRIPTOR_INTERFACE || header->descriptorType == DESCRIPTOR_CONFIGURATION) {
			return nullptr;
		} else if (header->descriptorType == type) {
			if (index) {
				index--;
			} else {
				return header;
			}
		}

		position += header->length;
	}

	return nullptr;
}

struct SynchronousTransfer {
	KEvent complete;
	size_t *bytesNotTransferred;
	bool success;
};

void SynchronousTransferCallback(ptrdiff_t bytesNotTransferred, EsGeneric context) {
	SynchronousTransfer *transfer = (SynchronousTransfer *) context.p;

	if (bytesNotTransferred != -1) {
		if (transfer->bytesNotTransferred) {
			transfer->success = true;
			*transfer->bytesNotTransferred = bytesNotTransferred;
		} else if (!bytesNotTransferred) {
			transfer->success = true;
		}
	}

	KEventSet(&transfer->complete);
}

bool KUSBDevice::RunTransfer(KUSBEndpointDescriptor *endpoint, void *buffer, size_t bufferBytes, size_t *bytesNotTransferred) {
	SynchronousTransfer transfer = {};
	transfer.bytesNotTransferred = bytesNotTransferred;
	queueTransfer(this, endpoint, SynchronousTransferCallback, buffer, bufferBytes, &transfer);
	KEventWait(&transfer.complete);
	return transfer.success;
}

bool KUSBDevice::GetString(uint8_t index, char *buffer, size_t bufferBytes) {
	uint16_t wideBuffer[127];

	if (!bufferBytes || !index) {
		return false;
	}

	uint16_t transferred = 0;

	if (!controlTransfer(this, SETUP_FLAG_D2H, 0x06 /* get descriptor */, 
				((uint16_t) DESCRIPTOR_STRING << 8) | (uint16_t) index, 0, 
				wideBuffer, sizeof(wideBuffer), K_ACCESS_READ, &transferred)) {
		return false;
	}

	if (transferred < 4) {
		return false;
	}

	size_t inputCharactersRemaining = (*(uint8_t *) wideBuffer) / 2 - 1;

	if ((size_t) (transferred / 2 - 1) < inputCharactersRemaining) {
		inputCharactersRemaining = transferred / 2 - 1;
	}

	uint16_t *inputPosition = wideBuffer + 1;
	uintptr_t bufferPosition = 0;

	while (inputCharactersRemaining) {
		uint32_t c = *inputPosition;
		inputCharactersRemaining--;
		inputPosition++;

		if (c >= 0xD800 && c < 0xDC00 && inputCharactersRemaining) {
			uint32_t c2 = *inputPosition;

			if (c2 >= 0xDC00 && c2 < 0xE000) {
				inputCharactersRemaining--;
				inputPosition++;

				c = ((c - 0xD800) << 10) + (c2 - 0xDC00) + 0x10000;
			}
		}

		size_t encodedBytes = utf8_encode(c, nullptr);

		if (bufferPosition + encodedBytes < bufferBytes) {
			utf8_encode(c, buffer + bufferPosition);
			bufferPosition += encodedBytes;
		} else {
			break;
		}
	}

	buffer[bufferPosition] = 0;

	return true;
}

bool USBInterfaceClassCheck(KInstalledDriver *driver, KDevice *device) {
	KUSBInterfaceDescriptor *descriptor = &((KUSBDevice *) device)->interfaceDescriptor;

	int classCode = -1, subclassCode = -1, protocol = -1;

	EsINIState s = {};
	s.buffer = driver->config, s.bytes = driver->configBytes;

	while (EsINIParse(&s)) {
		if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("classCode"))) classCode = EsIntegerParse(s.value, s.valueBytes);
		if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("subclassCode"))) subclassCode = EsIntegerParse(s.value, s.valueBytes);
		if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("protocol"))) protocol = EsIntegerParse(s.value, s.valueBytes);
	}

	if (classCode == -1 && subclassCode == -1 && protocol == -1) {
		return false;
	}

	if (classCode    != -1 && descriptor->interfaceClass    != classCode)    return false;
	if (subclassCode != -1 && descriptor->interfaceSubclass != subclassCode) return false;
	if (protocol     != -1 && descriptor->interfaceProtocol != protocol)     return false;

	return true;
}

bool USBProductIDCheck(KInstalledDriver *driver, KDevice *device) {
	KUSBDeviceDescriptor *descriptor = &((KUSBDevice *) device)->deviceDescriptor;

	int productID = -1, vendorID = -1, version = -1;

	EsINIState s = {};
	s.buffer = driver->config, s.bytes = driver->configBytes;

	while (EsINIParse(&s)) {
		if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("productID"))) productID = EsIntegerParse(s.value, s.valueBytes);
		if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("vendorID"))) vendorID = EsIntegerParse(s.value, s.valueBytes);
		if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("version"))) version = EsIntegerParse(s.value, s.valueBytes);
	}

	if (productID == -1 && vendorID == -1 && version == -1) {
		return false;
	}

	if (productID != -1 && descriptor->productID     != productID) return false;
	if (vendorID  != -1 && descriptor->vendorID      != vendorID)  return false;
	if (version   != -1 && descriptor->deviceVersion != version)   return false;

	return true;
}

void KRegisterUSBDevice(KUSBDevice *device) {
	EsDefer(KDeviceCloseHandle(device));

	bool foundInterfaceDescriptor = false;

	{
		// Get the device descriptor.

		uint16_t transferred;

		if (!device->controlTransfer(device, SETUP_FLAG_D2H, 0x06 /* get descriptor */, 
					(DESCRIPTOR_DEVICE << 8) | 0x00, 0, &device->deviceDescriptor, 
					sizeof(KUSBDeviceDescriptor), K_ACCESS_READ, &transferred) 
				|| transferred != sizeof(KUSBDeviceDescriptor)) {
			return;
		}

		if (device->deviceDescriptor.length < sizeof(KUSBDeviceDescriptor) 
				|| device->deviceDescriptor.descriptorType != DESCRIPTOR_DEVICE
				|| device->deviceDescriptor.configurationCount == 0) {
			KernelLog(LOG_ERROR, "USB", "invalid device descriptor", "Device descriptor is invalid or unsupported.\n");
			return;
		}
	}

	KernelLog(LOG_INFO, "USB", "device identification", "Device has identification %W/%W/%W.\n", 
			device->deviceDescriptor.vendorID, device->deviceDescriptor.productID, device->deviceDescriptor.deviceVersion);

	{
		// Get strings.

		char buffer[256];

		if (device->GetString(device->deviceDescriptor.manufacturerString, buffer, sizeof(buffer))) {
			KernelLog(LOG_INFO, "USB", "device manufacturer", "Device manufacturer string: '%z'.\n", buffer);
		} else {
			goto skipStrings;
		}

		if (device->GetString(device->deviceDescriptor.productString, buffer, sizeof(buffer))) {
			KernelLog(LOG_INFO, "USB", "device product", "Device product string: '%z'.\n", buffer);
		} else {
			goto skipStrings;
		}

		if (device->GetString(device->deviceDescriptor.serialNumberString, buffer, sizeof(buffer))) {
			KernelLog(LOG_INFO, "USB", "device serial number", "Device serial number string: '%z'.\n", buffer);
		} else {
			goto skipStrings;
		}

		skipStrings:;
	}

	{
		// Get the configuration descriptor.
		
		uint16_t transferred;

		if (!device->controlTransfer(device, SETUP_FLAG_D2H, 0x06 /* get descriptor */, 
					(DESCRIPTOR_CONFIGURATION << 8) | 0x00, 0, &device->configurationDescriptor, 
					sizeof(KUSBConfigurationDescriptor), K_ACCESS_READ, &transferred) 
				|| transferred != sizeof(KUSBConfigurationDescriptor)) {
			return;
		}

		if (device->configurationDescriptor.totalLength < sizeof(KUSBConfigurationDescriptor) 
				|| device->configurationDescriptor.descriptorType != DESCRIPTOR_CONFIGURATION
				|| !device->configurationDescriptor.interfaceCount
				|| !device->configurationDescriptor.configurationIndex) {
			KernelLog(LOG_ERROR, "USB", "invalid configuration descriptor", "Invalid field in configuration descriptor.\n");
			return;
		}
	}

	{
		// Read the rest of the configuration descriptors.

		uint8_t *buffer = (uint8_t *) EsHeapAllocate(device->configurationDescriptor.totalLength, false, K_FIXED);

		if (!buffer) {
			KernelLog(LOG_ERROR, "USB", "allocation failure", "Could not allocate buffer to read all configuration descriptors.\n");
			return;
		}

		uint16_t transferred;

		if (!device->controlTransfer(device, SETUP_FLAG_D2H, 0x06 /* get descriptor */, 
					(DESCRIPTOR_CONFIGURATION << 8) | 0x00, 0, buffer, 
					device->configurationDescriptor.totalLength, K_ACCESS_READ, &transferred) 
				|| transferred != device->configurationDescriptor.totalLength) {
			return;
		}

		device->configurationDescriptors = buffer;
		device->configurationDescriptorsBytes = device->configurationDescriptor.totalLength;
	}

	{
		// Check the configuration descriptors are valid.

		uintptr_t position = 0;
		uintptr_t configurationsSeen = 0;

		while (position < device->configurationDescriptorsBytes) {
			if (position >= device->configurationDescriptorsBytes - sizeof(KUSBDescriptorHeader)) {
				KernelLog(LOG_ERROR, "USB", "descriptor invalid length", "Remaining %D, too small for descriptor.\n",
						device->configurationDescriptorsBytes - position);
				return;
			}

			KUSBDescriptorHeader *header = (KUSBDescriptorHeader *) (device->configurationDescriptors + position);

			if (header->length < sizeof(KUSBDescriptorHeader) || header->length > device->configurationDescriptorsBytes - position) {
				KernelLog(LOG_ERROR, "USB", "descriptor invalid length", "Given length %D, remaining %D.\n",
						header->length, device->configurationDescriptorsBytes - position);
				return;
			}

			if (header->descriptorType == DESCRIPTOR_CONFIGURATION
					&& header->length >= sizeof(KUSBConfigurationDescriptor)) {
				configurationsSeen++;
			}

			if (header->descriptorType == DESCRIPTOR_INTERFACE 
					&& header->length >= sizeof(KUSBInterfaceDescriptor) 
					&& !foundInterfaceDescriptor
					&& configurationsSeen == 1) {
				device->interfaceDescriptor = *(KUSBInterfaceDescriptor *) header;
				device->selectedConfigurationOffset = position + header->length;
				foundInterfaceDescriptor = true;
			}

			position += header->length;
		}
	}

	{
		// Look for a driver that matches the vendor/product.

		if (KDeviceAttach(device, "USB", USBProductIDCheck)) {
			return;
		}

		// Otherwise, pick the default configuration and interface.

		if (!foundInterfaceDescriptor) {
			KernelLog(LOG_ERROR, "USB", "no interface descriptor", 
					"The device does not have any interface descriptors for the default configuration.");
		}

		KernelLog(LOG_INFO, "USB", "device interface", "Device has interface %d with identification %X/%X/%X.\n", 
				device->interfaceDescriptor.interfaceIndex, device->interfaceDescriptor.interfaceClass, 
				device->interfaceDescriptor.interfaceSubclass, device->interfaceDescriptor.interfaceProtocol);

		if (!device->selectConfigurationAndInterface(device)) {
			KernelLog(LOG_ERROR, "USB", "select interface failure", "Could not select configuration and interface for device.\n");
			return;
		}

		if (KDeviceAttach(device, "USB", USBInterfaceClassCheck)) {
			return;
		}

		KernelLog(LOG_ERROR, "USB", "no driver", "No driver could be found for the device.\n");
		// TODO Show an error message to the user.
	}
}

KDriver driverUSB;
