#include <module.h>

// TODO Active cooling.
// TODO Passive cooling.
// TODO Temperature change polling.
// TODO Refresh temperature/thresholds on a separate thread.

struct ACPIThermalZone : KDevice {
	KACPIObject *object;
	uint64_t criticalThreshold;    // Once reached, the system should shutdown as quickly as possible.
	uint64_t hotThreshold;         // Once reached, the system will likely want to enter sleep mode.
	uint64_t passiveThreshold;     // Once reached, the passive cooling algorithm (e.g. processor speed throttling) should be enabled.
	uint64_t activeThresholds[10]; // Once reached, the given active cooling device (e.g. fan) should be enabled.
	uint64_t pollingFrequency;     // Recommended polling frequency of temperature, in tenths of a seconds.
	uint64_t currentTemperature;
	KMutex refreshMutex;
};

static void ACPIThermalRefreshTemperature(EsGeneric context) {
	ACPIThermalZone *device = (ACPIThermalZone *) context.p;
	KACPIObject *object = device->object;
	KMutexAcquire(&device->refreshMutex);
	KernelLog(LOG_INFO, "ACPIThermal", "temperature", "Taking temperature reading...\n");

	EsError error = KACPIObjectEvaluateInteger(object, "_TMP", &device->currentTemperature);
	if (error == ES_SUCCESS) KernelLog(LOG_INFO, "ACPIThermal", "temperature", "Current temperature: %d K.\n", device->currentTemperature / 10);
	else KernelLog(LOG_ERROR, "ACPIThermal", "temperature", "Unable to read current temperature (%d).\n", error);

	// TODO Active cooling.
	
	KMutexRelease(&device->refreshMutex);
}

static void ACPIThermalRefreshThresholds(EsGeneric context) {
	ACPIThermalZone *device = (ACPIThermalZone *) context.p;
	KACPIObject *object = device->object;
	KMutexAcquire(&device->refreshMutex);
	KernelLog(LOG_INFO, "ACPIThermal", "threshold", "Taking threshold readings...\n");
	
	EsError error;

	error = KACPIObjectEvaluateInteger(object, "_CRT", &device->criticalThreshold);
	if (error == ES_SUCCESS) KernelLog(LOG_INFO, "ACPIThermal", "threshold", "Critical temperature threshold: %d K.\n", device->criticalThreshold / 10);

	error = KACPIObjectEvaluateInteger(object, "_HOT", &device->hotThreshold);
	if (error == ES_SUCCESS) KernelLog(LOG_INFO, "ACPIThermal", "threshold", "Hot temperature threshold: %d K.\n", device->hotThreshold / 10);

	error = KACPIObjectEvaluateInteger(object, "_PSV", &device->passiveThreshold);
	if (error == ES_SUCCESS) KernelLog(LOG_INFO, "ACPIThermal", "threshold", "Passive temperature threshold: %d K.\n", device->passiveThreshold / 10);

	error = KACPIObjectEvaluateInteger(object, "_TZP", &device->passiveThreshold);
	if (error == ES_SUCCESS) KernelLog(LOG_INFO, "ACPIThermal", "threshold", "Recommended polling frequency: %d s.\n", device->pollingFrequency / 10);

	char name[5] = "_AC0";

	for (uintptr_t i = 0; i <= 9; i++, name[3]++) {
		EsError error = KACPIObjectEvaluateInteger(object, name, &device->activeThresholds[i]);
		if (error == ES_SUCCESS) KernelLog(LOG_INFO, "ACPIThermal", "threshold", "Active temperature threshold %d: %d K.\n", i, device->activeThresholds[i] / 10);
		else break;
	}

	KMutexRelease(&device->refreshMutex);
	ACPIThermalRefreshTemperature(device);
}

static void ACPIThermalDeviceNotificationHandler(KACPIObject *, uint32_t value, EsGeneric context) {
	ACPIThermalZone *device = (ACPIThermalZone *) context.p;

	if (value == 0x80) {
		KRegisterAsyncTask(ACPIThermalRefreshTemperature, device);
	} else if (value == 0x81) {
		KRegisterAsyncTask(ACPIThermalRefreshThresholds, device);
	}
}

static void ACPIThermalDeviceAttach(KDevice *parent) {
	KACPIObject *object = (KACPIObject *) parent;
	ACPIThermalZone *device = (ACPIThermalZone *) KDeviceCreate("ACPI thermal zone", parent, sizeof(ACPIThermalZone));
	if (!device) return;
	device->object = object;
	KernelLog(LOG_INFO, "ACPIThermal", "device attached", "Found ACPI thermal zone.\n");

	ACPIThermalRefreshThresholds(device);

	EsError error;

	error = KACPIObjectSetDeviceNotificationHandler(object, ACPIThermalDeviceNotificationHandler, device);
	if (error == ES_SUCCESS) KernelLog(LOG_INFO, "ACPIThermal", "notification handler", "Successfully installed notification handler.\n");
	else KernelLog(LOG_ERROR, "ACPIThermal", "notification handler", "Unable to install notification handler (%d).\n", error);

	error = KACPIObjectEvaluateMethodWithInteger(object, "_SCP", 0 /* active cooling policy */);
	if (error == ES_SUCCESS) KernelLog(LOG_INFO, "ACPIThermal", "cooling policy", "Successfully set active cooling policy.\n");
	else KernelLog(LOG_ERROR, "ACPIThermal", "cooling policy", "Unable to set active cooling policy (%d).\n", error);
}

KDriver driverACPIThermal = {
	.attach = ACPIThermalDeviceAttach,
};
