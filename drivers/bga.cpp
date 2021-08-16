#include <module.h>

struct BGADisplay : KGraphicsTarget {
};

BGADisplay *vboxDisplay;

void BGAUpdateScreen(K_USER_BUFFER const uint8_t *source, uint32_t sourceWidth, uint32_t sourceHeight, uint32_t sourceStride,
		uint32_t destinationX, uint32_t destinationY) {
	GraphicsUpdateScreen32(source, sourceWidth, sourceHeight, sourceStride, destinationX, destinationY, 
			vboxDisplay->screenWidth, vboxDisplay->screenHeight, 
			vboxDisplay->screenWidth * 4, 
			((KPCIDevice *) vboxDisplay->parent)->baseAddressesVirtual[0]);
}

void BGADebugPutBlock(uintptr_t x, uintptr_t y, bool toggle) {
	GraphicsDebugPutBlock32(x, y, toggle, 
			vboxDisplay->screenWidth, vboxDisplay->screenHeight, 
			vboxDisplay->screenWidth * 4, 
			((KPCIDevice *) vboxDisplay->parent)->baseAddressesVirtual[0]);
}

void BGADebugClearScreen() {
	GraphicsDebugClearScreen32(vboxDisplay->screenWidth, vboxDisplay->screenHeight, 
			vboxDisplay->screenWidth * 4, 
			((KPCIDevice *) vboxDisplay->parent)->baseAddressesVirtual[0]);
}

void BGADeviceAttached(KDevice *_parent) {
	KPCIDevice *parent = (KPCIDevice *) _parent;

	BGADisplay *device = (BGADisplay *) KDeviceCreate("BGA", parent, sizeof(BGADisplay), ES_DEVICE_GRAPHICS_TARGET);
	if (!device) return;

	parent->EnableFeatures(K_PCI_FEATURE_IO_PORT_ACCESS | K_PCI_FEATURE_MEMORY_SPACE_ACCESS | K_PCI_FEATURE_BAR_0);

	if (KGraphicsIsTargetRegistered()) {
		return;
	}

	ProcessorOut16(0x01CE, 0 /* version */);
	uint16_t version = ProcessorIn16(0x01CF);
	KernelLog(LOG_INFO, "BGA", "version", "Detected version %X%X.\n", version >> 8, version);

	if (version < 0xB0C0 || version > 0xB0C5) {
		KernelLog(LOG_INFO, "BGA", "unsupported version", "Currently, the only supported versions are B0C0-B0C5.\n");
		return;
	}

	// Set the mode.
	ProcessorOut16(0x01CE, 4 /* enable */);
	ProcessorOut16(0x01CF, 0);
	ProcessorOut16(0x01CE, 1 /* x resolution */);
	ProcessorOut16(0x01CF, BGA_RESOLUTION_WIDTH);
	ProcessorOut16(0x01CE, 2 /* y resolution */);
	ProcessorOut16(0x01CF, BGA_RESOLUTION_HEIGHT);
	ProcessorOut16(0x01CE, 3 /* bpp */);
	ProcessorOut16(0x01CF, 32);
	ProcessorOut16(0x01CE, 4 /* enable */);
	ProcessorOut16(0x01CF, 0x41 /* linear frame-buffer */);

	// Setup the graphics target.
	device->updateScreen = BGAUpdateScreen;
	device->debugPutBlock = BGADebugPutBlock;
	device->debugClearScreen = BGADebugClearScreen;
	ProcessorOut16(0x01CE, 1 /* x resolution */);
	device->screenWidth = ProcessorIn16(0x01CF);
	ProcessorOut16(0x01CE, 2 /* y resolution */);
	device->screenHeight = ProcessorIn16(0x01CF);

	// Register the display.
	KernelLog(LOG_INFO, "BGA", "register target", 
			"Registering graphics target with resolution %d by %d. Linear framebuffer is at virtual address %x.\n", 
			device->screenWidth, device->screenHeight, parent->baseAddressesVirtual[0]);
	vboxDisplay = device;
	KRegisterGraphicsTarget(device);
}

KDriver driverBGA = {
	.attach = BGADeviceAttached,
};
