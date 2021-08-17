#ifndef IMPLEMENTATION

struct DeviceAttachData {
	KDevice *parentDevice;
	KInstalledDriver *installedDriver;
};

KDevice *deviceTreeRoot;
KMutex deviceTreeMutex;

Array<DeviceAttachData, K_FIXED> delayedDevices;
Array<KInstalledDriver, K_FIXED> installedDrivers;

#endif

#ifdef IMPLEMENTATION

void *ResolveKernelSymbol(const char *name, size_t nameBytes);

KDevice *KDeviceCreate(const char *cDebugName, KDevice *parent, size_t bytes) {
	if (bytes < sizeof(KDevice)) {
		KernelPanic("KDeviceCreate - Device structure size is too small (less than KDevice).\n");
	}

	KDevice *device = (KDevice *) EsHeapAllocate(bytes, true, K_FIXED);
	if (!device) return nullptr;

	device->parent = parent;
	device->cDebugName = cDebugName;
	device->handles = 2; // One handle for the creator, and another closed when the device is removed (by the parent).

	static EsObjectID previousObjectID = 0;
	device->objectID = __sync_add_and_fetch(&previousObjectID, 1);

	if (parent) {
		KMutexAcquire(&deviceTreeMutex);

		if (!parent->children.Add(device)) {
			EsHeapFree(device, bytes, K_FIXED);
			device = nullptr;
		}

		KMutexRelease(&deviceTreeMutex);
		return device;
	} else {
		if (deviceTreeRoot) {
			KernelPanic("KDeviceCreate - Root device already created.\n");
		}

		return (deviceTreeRoot = device);
	}
}

void DeviceDestroy(KDevice *device) {
	device->children.Free();
	if (device->destroy) device->destroy(device);
	EsHeapFree(device, 0, K_FIXED);
}

void KDeviceDestroy(KDevice *device) {
	KMutexAcquire(&deviceTreeMutex);

	device->handles = 0;

	if (device->children.Length()) {
		KernelPanic("KDeviceDestroy - Device %x has children.\n", device);
	}

	while (!device->handles && !device->children.Length()) {
		device->parent->children.FindAndDeleteSwap(device, true /* fail if not found */);
		KDevice *parent = device->parent;
		DeviceDestroy(device);
		device = parent;
	}

	KMutexRelease(&deviceTreeMutex);
}

void KDeviceOpenHandle(KDevice *device) {
	KMutexAcquire(&deviceTreeMutex);
	if (!device->handles) KernelPanic("KDeviceOpenHandle - Device %s has no handles.\n", device);
	device->handles++;
	KMutexRelease(&deviceTreeMutex);
}

void KDeviceCloseHandle(KDevice *device) {
	KMutexAcquire(&deviceTreeMutex);

	if (!device->handles) KernelPanic("KDeviceCloseHandle - Device %s has no handles.\n", device);
	device->handles--;

	while (!device->handles && !device->children.Length()) {
		device->parent->children.FindAndDeleteSwap(device, true /* fail if not found */);
		KDevice *parent = device->parent;
		DeviceDestroy(device);
		device = parent;
	}

	KMutexRelease(&deviceTreeMutex);
}

void DeviceRemovedRecurse(KDevice *device) {
	if (device->flags & K_DEVICE_REMOVED) KernelPanic("DeviceRemovedRecurse - Device %x already removed.\n", device);
	device->flags |= K_DEVICE_REMOVED;

	for (uintptr_t i = 0; i < device->children.Length(); i++) {
		KDevice *child = device->children[i];
		DeviceRemovedRecurse(child);
		if (!child->handles) KernelPanic("DeviceRemovedRecurse - Child device %s has no handles.\n", child);
		child->handles--;
		if (child->handles || child->children.Length()) continue;
		device->children.DeleteSwap(i);
		DeviceDestroy(child);
		i--;
	}

	if (device->flags & K_DEVICE_VISIBLE_TO_USER) {
		EsMessage m;
		EsMemoryZero(&m, sizeof(m));
		m.type = ES_MSG_DEVICE_DISCONNECTED;
		m.device.id = device->objectID;
		desktopProcess->messageQueue.SendMessage(nullptr, &m);
	}

	if (device->removed) {
		device->removed(device);
	}
}

void KDeviceSendConnectedMessage(KDevice *device, EsDeviceType type) {
	KMutexAcquire(&deviceTreeMutex);

	if (device->flags & K_DEVICE_VISIBLE_TO_USER) {
		KernelPanic("KDeviceSendConnectedMessage - Connected message already sent for device %x.\n", device);
	}

	device->flags |= K_DEVICE_VISIBLE_TO_USER;

	KMutexRelease(&deviceTreeMutex);

	KDeviceOpenHandle(device);

	EsMessage m;
	EsMemoryZero(&m, sizeof(m));
	m.type = ES_MSG_DEVICE_CONNECTED;
	m.device.id = device->objectID;
	m.device.type = type;
	m.device.handle = desktopProcess->handleTable.OpenHandle(device, 0, KERNEL_OBJECT_DEVICE);

	if (m.device.handle) {
		if (!desktopProcess->messageQueue.SendMessage(nullptr, &m)) {
			desktopProcess->handleTable.CloseHandle(m.device.handle); // This will check that the handle is still valid.
		}
	}
}

void KDeviceRemoved(KDevice *device) {
	KMutexAcquire(&deviceTreeMutex);
	DeviceRemovedRecurse(device);
	KMutexRelease(&deviceTreeMutex);
	KDeviceCloseHandle(device);
}

const KDriver *DriverLoad(KInstalledDriver *installedDriver) {
	static KMutex mutex = {};

	KMutexAcquire(&mutex);
	EsDefer(KMutexRelease(&mutex));

	if (installedDriver->loadedDriver) {
		return installedDriver->loadedDriver;
	}

	KDriver *driver = nullptr;

	char *driverVariable = nullptr;
	size_t driverVariableBytes = 0;

	EsINIState s = {};
	s.buffer = installedDriver->config;
	s.bytes = installedDriver->configBytes;

	while (EsINIParse(&s)) {
		if (0 == EsStringCompareRaw(s.key, s.keyBytes, EsLiteral("driver"))) {
			driverVariable = s.value;
			driverVariableBytes = s.valueBytes;
		}
	}

	char *buffer = (char *) EsHeapAllocate(K_MAX_PATH, true, K_FIXED);
	if (!buffer) return nullptr;
	EsDefer(EsHeapFree(buffer, K_MAX_PATH, K_FIXED));

	KModule *module = (KModule *) EsHeapAllocate(sizeof(KModule), true, K_FIXED);
	if (!module) return nullptr;

	module->path = buffer;
	module->pathBytes = EsStringFormat(buffer, K_MAX_PATH, K_OS_FOLDER "/Modules/%s.ekm", 
			installedDriver->nameBytes, installedDriver->name);
	module->resolveSymbol = ResolveKernelSymbol;

	EsError error = KLoadELFModule(module);

	if (error == ES_SUCCESS) {
		KernelLog(LOG_INFO, "Modules", "module loaded", "Successfully loaded module '%s'.\n", 
				installedDriver->nameBytes, installedDriver->name);

		driver = (KDriver *) KFindSymbol(module, driverVariable, driverVariableBytes);

		if (!driver) {
			KernelLog(LOG_ERROR, "Modules", "bad module", "DriverLoad - Could not find driver symbol in module '%s'.\n", 
					installedDriver->nameBytes, installedDriver->name);
		}
	} else {
		KernelLog(LOG_ERROR, "Modules", "module load failure", "Could not load module '%s' (error = %d).\n", 
				installedDriver->nameBytes, installedDriver->name, error);
		EsHeapFree(module, sizeof(KModule), K_FIXED);
	}

	return (installedDriver->loadedDriver = driver);
}

void DeviceAttach(DeviceAttachData attach) {
	TS("DeviceAttach to %s\n", attach.installedDriver->nameBytes, attach.installedDriver->name);

	if (attach.parentDevice) {
		KDeviceOpenHandle(attach.parentDevice);
	}

	KMutexAcquire(&deviceTreeMutex);

	if (!attach.installedDriver->builtin && !fs.bootFileSystem) {
		KernelLog(LOG_INFO, "Modules", "delayed device", "Delaying attach device to driver '%s' until boot file system mounted.\n", 
				attach.installedDriver->nameBytes, attach.installedDriver->name);
		delayedDevices.Add(attach);
		KMutexRelease(&deviceTreeMutex);
		return;
	}

	KernelLog(LOG_INFO, "Modules", "device attach", "Attaching device to driver '%s'.\n", 
			attach.installedDriver->nameBytes, attach.installedDriver->name);

	const KDriver *driver = DriverLoad(attach.installedDriver);
	KMutexRelease(&deviceTreeMutex);

	if (driver && driver->attach) { 
		driver->attach(attach.parentDevice);
	}

	if (attach.parentDevice) {
		KDeviceCloseHandle(attach.parentDevice);
	}
}

bool KDeviceAttach(KDevice *parentDevice, const char *cName, KDriverIsImplementorCallback callback) {
	size_t nameBytes = EsCStringLength(cName);

	for (uintptr_t i = installedDrivers.Length(); i > 0; i--) {
		if (0 == EsStringCompareRaw(cName, nameBytes, installedDrivers[i - 1].parent, installedDrivers[i - 1].parentBytes) 
				&& callback(&installedDrivers[i - 1], parentDevice)) {
			DeviceAttach({ .parentDevice = parentDevice, .installedDriver = &installedDrivers[i - 1] });
			return true;
		}
	}

	return false;
}

void KDeviceAttachAll(KDevice *parentDevice, const char *cName) {
	size_t nameBytes = EsCStringLength(cName);

	for (uintptr_t i = 0; i < installedDrivers.Length(); i++) {
		if (0 == EsStringCompareRaw(cName, nameBytes, installedDrivers[i].parent, installedDrivers[i].parentBytes)) {
			DeviceAttach({ .parentDevice = parentDevice, .installedDriver = &installedDrivers[i] });
		}
	}
}

bool KDeviceAttachByName(KDevice *parentDevice, const char *cName) {
	size_t nameBytes = EsCStringLength(cName);

	for (uintptr_t i = 0; i < installedDrivers.Length(); i++) {
		if (0 == EsStringCompareRaw(cName, nameBytes, installedDrivers[i].name, installedDrivers[i].nameBytes)) {
			DeviceAttach({ .parentDevice = parentDevice, .installedDriver = &installedDrivers[i] });
			return true;
		}
	}

	return false;
}

void DeviceRootAttach(KDevice *parentDevice) {
	// Load all the root drivers and create their devices.

	KDeviceAttachAll(KDeviceCreate("root", parentDevice, sizeof(KDevice)), "Root");

	// Check we have found the drive from which we booted.
	// TODO Decide the timeout.

	if (!KEventWait(&fs.foundBootFileSystemEvent, 10000)) {
		KernelPanic("DeviceRootAttach - Could not find the boot file system.\n");
	}

	// Load any devices that were waiting for the boot file system to be loaded.

	for (uintptr_t i = 0; i < delayedDevices.Length(); i++) {
		DeviceAttach(delayedDevices[i]);
		KDeviceCloseHandle(delayedDevices[i].parentDevice);
	}

	delayedDevices.Free();
}

KDriver driverRoot = { 
	.attach = DeviceRootAttach,
};

void DriversInitialise() {
	// Add the builtin drivers to the database.

	for (uintptr_t i = 0; i < sizeof(builtinDrivers) / sizeof(builtinDrivers[0]); i++) {
		installedDrivers.Add(builtinDrivers[i]);
	}

	// Attach to the root device.

	DeviceAttach({ .parentDevice = nullptr, .installedDriver = &installedDrivers[0] });
}

void DriversDumpStateRecurse(KDevice *device) {
	if (device->dumpState) {
		device->dumpState(device);
	}

	for (uintptr_t i = 0; i < device->children.Length(); i++) {
		DriversDumpStateRecurse(device->children[i]);
	}
}

void DriversDumpState() {
	KMutexAcquire(&deviceTreeMutex);
	DriversDumpStateRecurse(deviceTreeRoot);
	KMutexRelease(&deviceTreeMutex);
}

void DriversShutdownRecurse(KDevice *device) {
	for (uintptr_t i = 0; i < device->children.Length(); i++) {
		DriversShutdownRecurse(device->children[i]);
	}

	if (device->shutdown) {
		device->shutdown(device);
	}
}

void DriversShutdown() {
	KMutexAcquire(&deviceTreeMutex);
	DriversShutdownRecurse(deviceTreeRoot);
	KMutexRelease(&deviceTreeMutex);
}

#endif
