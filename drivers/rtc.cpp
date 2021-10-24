#include <module.h>
#include <kernel/x86_64.h>

struct RTCDevice : KClockDevice {};

KMutex mutex;

uint8_t RTCRead(uint8_t index, bool convertFromBCD, bool convertFrom12Hour) {
	KMutexAcquire(&mutex);
	EsDefer(KMutexRelease(&mutex));

	for (uint8_t i = 0; i < 10; i++) {
		// Write the index a few times to delay before reading.
		ProcessorOut8(IO_RTC_INDEX, index);
	}

	uint8_t value = ProcessorIn8(IO_RTC_DATA);

	if (convertFromBCD) {
		value = (value >> 4) * 10 + (value & 0xF);
		if (convertFrom12Hour) value -= 0x30;
	}

	if (convertFrom12Hour) {
		if (value > 0x80) value -= 0x45;
		else value -= 0x01;
	}

	return value;
}

void RTCReadAll(EsDateComponents *reading) {
	uint8_t centuryRegisterIndex = ACPIGetCenturyRegisterIndex();
	uint8_t status   = RTCRead(0x0B, false, false);
	reading->second  = RTCRead(0x00, ~status & 4, false);
	reading->minute  = RTCRead(0x02, ~status & 4, ~status & 2);
	reading->hour    = RTCRead(0x04, ~status & 4, false);
	reading->day     = RTCRead(0x07, ~status & 4, false);
	reading->month   = RTCRead(0x08, ~status & 4, false);
	reading->year    = RTCRead(0x09, ~status & 4, false);
	reading->year   += 100 * (centuryRegisterIndex ? RTCRead(centuryRegisterIndex, ~status & 4, false) : 20);
}

EsError RTCMakeReading(KClockDevice *, EsDateComponents *reading, uint64_t *linear) {
	*linear = 0; // The RTC is a componented clock.
	EsMemoryZero(reading, sizeof(EsDateComponents));

	// Try up to 10 times to get a consistent reading.
	for (uintptr_t i = 0; i < 10; i++) {
		EsDateComponents now;
		RTCReadAll(&now);
		bool match = 0 == EsMemoryCompare(reading, &now, sizeof(EsDateComponents));
		EsMemoryCopy(reading, &now, sizeof(EsDateComponents));
		if (match) break;
	}

	KernelLog(LOG_INFO, "RTC", "read time", "Read current time from RTC as %d/%d/%d %d:%d:%d. Status byte is 0x%X. Century register index is 0x%X.\n", 
			reading->year, reading->month, reading->day, reading->hour, reading->minute, reading->second, 
			RTCRead(0x0B, false, false), ACPIGetCenturyRegisterIndex());

	// Use the time as a source of entropy.
	EsRandomAddEntropy(reading->second);
	EsRandomAddEntropy(reading->minute);
	EsRandomAddEntropy(reading->hour);
	EsRandomAddEntropy(reading->day);
	EsRandomAddEntropy(reading->month);
	EsRandomAddEntropy(reading->year);

	return ES_SUCCESS;
}

void RTCDeviceAttached(KDevice *_parent) {
	RTCDevice *device = (RTCDevice *) KDeviceCreate("RTC", _parent, sizeof(RTCDevice));
	if (!device) return;
	device->read = RTCMakeReading;
	KDeviceSendConnectedMessage(device, ES_DEVICE_CLOCK);

#if 0
	{
		RTCReading start = RTCMakeReading();
		KEvent e = {};
		KEventWait(&e, 300000);
		RTCReading end = RTCMakeReading();
		KernelLog(LOG_INFO, "RTC", "delay test", "5 minute delay: %d/%d/%d %d:%d:%d; %d/%d/%d %d:%d:%d.\n", 
				start.year, start.month, start.day, start.hour, start.minute, start.second,
				end.year, end.month, end.day, end.hour, end.minute, end.second);
	}
#endif
}

KDriver driverRTC = {
	.attach = RTCDeviceAttached,
};
