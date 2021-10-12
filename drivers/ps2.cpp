// TODO Scrolling.

#include <module.h>

struct PS2Update {
	union {
		struct {
			volatile int xMovement, yMovement;
			volatile unsigned buttons;
		};

		struct {
			volatile unsigned scancode;
		};
	};
};

struct PS2 {
	void Initialise(KDevice *parentDevice);
	void EnableDevices(unsigned which);
	void DisableDevices(unsigned which);
	void FlushOutputBuffer();
	void SendCommand(uint8_t command);
	uint8_t ReadByte(KTimeout *timeout);
	void WriteByte(KTimeout *timeout, uint8_t value);
	bool SetupKeyboard(KTimeout *timeout);
	bool SetupMouse(KTimeout *timeout);
	bool PollRead(uint8_t *value, bool forMouse);
	void WaitInputBuffer();

	uint8_t mouseType, scancodeSet;
	size_t channels;
	bool registeredIRQs;
	bool initialised;

	volatile uintptr_t lastUpdatesIndex;
	PS2Update lastUpdates[16];
	KSpinlock lastUpdatesLock;
	KMutex mutex;
};

PS2 ps2;

uint16_t scancodeConversionTable1[] = {
	0, ES_SCANCODE_ESCAPE, ES_SCANCODE_1, ES_SCANCODE_2, ES_SCANCODE_3, ES_SCANCODE_4, ES_SCANCODE_5, ES_SCANCODE_6,
	ES_SCANCODE_7, ES_SCANCODE_8, ES_SCANCODE_9, ES_SCANCODE_0, ES_SCANCODE_HYPHEN, ES_SCANCODE_EQUALS, ES_SCANCODE_BACKSPACE, ES_SCANCODE_TAB,
	ES_SCANCODE_Q, ES_SCANCODE_W, ES_SCANCODE_E, ES_SCANCODE_R, ES_SCANCODE_T, ES_SCANCODE_Y, ES_SCANCODE_U, ES_SCANCODE_I,
	ES_SCANCODE_O, ES_SCANCODE_P, ES_SCANCODE_LEFT_BRACE, ES_SCANCODE_RIGHT_BRACE, ES_SCANCODE_ENTER, ES_SCANCODE_LEFT_CTRL, ES_SCANCODE_A, ES_SCANCODE_S,
	ES_SCANCODE_D, ES_SCANCODE_F, ES_SCANCODE_G, ES_SCANCODE_H, ES_SCANCODE_J, ES_SCANCODE_K, ES_SCANCODE_L, ES_SCANCODE_PUNCTUATION_3,
	ES_SCANCODE_PUNCTUATION_4, ES_SCANCODE_PUNCTUATION_5, ES_SCANCODE_LEFT_SHIFT, ES_SCANCODE_PUNCTUATION_1, ES_SCANCODE_Z, ES_SCANCODE_X, ES_SCANCODE_C, ES_SCANCODE_V,
	ES_SCANCODE_B, ES_SCANCODE_N, ES_SCANCODE_M, ES_SCANCODE_COMMA, ES_SCANCODE_PERIOD, ES_SCANCODE_SLASH, ES_SCANCODE_RIGHT_SHIFT, ES_SCANCODE_NUM_MULTIPLY,
	ES_SCANCODE_LEFT_ALT, ES_SCANCODE_SPACE, ES_SCANCODE_CAPS_LOCK, ES_SCANCODE_F1, ES_SCANCODE_F2, ES_SCANCODE_F3, ES_SCANCODE_F4, ES_SCANCODE_F5,
	ES_SCANCODE_F6, ES_SCANCODE_F7, ES_SCANCODE_F8, ES_SCANCODE_F9, ES_SCANCODE_F10, ES_SCANCODE_NUM_LOCK, ES_SCANCODE_SCROLL_LOCK, ES_SCANCODE_NUM_7,
	ES_SCANCODE_NUM_8, ES_SCANCODE_NUM_9, ES_SCANCODE_NUM_SUBTRACT, ES_SCANCODE_NUM_4, ES_SCANCODE_NUM_5, ES_SCANCODE_NUM_6, ES_SCANCODE_NUM_ADD, ES_SCANCODE_NUM_1,
	ES_SCANCODE_NUM_2, ES_SCANCODE_NUM_3, ES_SCANCODE_NUM_0, ES_SCANCODE_NUM_POINT, 0, 0, 0, ES_SCANCODE_F11,
	ES_SCANCODE_F12, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	ES_SCANCODE_MM_PREVIOUS, 0, 0, 0, 0, 0, 0, 0, 
	0, ES_SCANCODE_MM_NEXT, 0, 0, ES_SCANCODE_NUM_ENTER, ES_SCANCODE_RIGHT_CTRL, 0, 0,
	ES_SCANCODE_MM_MUTE, ES_SCANCODE_MM_CALC, ES_SCANCODE_MM_PAUSE, 0, ES_SCANCODE_MM_STOP, 0, 0, 0, 
	0, 0, ES_SCANCODE_MM_QUIETER, 0, 0, 0, 0, 0,
	ES_SCANCODE_MM_LOUDER, 0, ES_SCANCODE_WWW_HOME, 0, 0, ES_SCANCODE_NUM_DIVIDE, 0, 0,
	ES_SCANCODE_RIGHT_ALT, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, ES_SCANCODE_HOME,
	ES_SCANCODE_UP_ARROW, ES_SCANCODE_PAGE_UP, 0, ES_SCANCODE_LEFT_ARROW, 0, ES_SCANCODE_RIGHT_ARROW, 0, ES_SCANCODE_END,
	ES_SCANCODE_DOWN_ARROW, ES_SCANCODE_PAGE_DOWN, ES_SCANCODE_INSERT, ES_SCANCODE_DELETE, 0, 0, 0, 0, 
	0, 0, 0, ES_SCANCODE_LEFT_FLAG, ES_SCANCODE_RIGHT_FLAG, ES_SCANCODE_CONTEXT_MENU, ES_SCANCODE_ACPI_POWER, ES_SCANCODE_ACPI_SLEEP,
	0, 0, 0, ES_SCANCODE_ACPI_WAKE, 0, ES_SCANCODE_WWW_SEARCH, ES_SCANCODE_WWW_STARRED, ES_SCANCODE_WWW_REFRESH,
	ES_SCANCODE_WWW_STOP, ES_SCANCODE_WWW_FORWARD, ES_SCANCODE_WWW_BACK, ES_SCANCODE_MM_FILES, ES_SCANCODE_MM_EMAIL, ES_SCANCODE_MM_SELECT, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
};

uint16_t scancodeConversionTable2[] = {
	0, ES_SCANCODE_F9, 0, ES_SCANCODE_F5, ES_SCANCODE_F3, ES_SCANCODE_F1, ES_SCANCODE_F2, ES_SCANCODE_F12, 
	0, ES_SCANCODE_F10, ES_SCANCODE_F8, ES_SCANCODE_F6, ES_SCANCODE_F4, ES_SCANCODE_TAB, ES_SCANCODE_PUNCTUATION_5, 0, 
	0, ES_SCANCODE_LEFT_ALT, ES_SCANCODE_LEFT_SHIFT, 0, ES_SCANCODE_LEFT_CTRL, ES_SCANCODE_Q, ES_SCANCODE_1, 0, 
	0, 0, ES_SCANCODE_Z, ES_SCANCODE_S, ES_SCANCODE_A, ES_SCANCODE_W, ES_SCANCODE_2, 0, 
	0, ES_SCANCODE_C, ES_SCANCODE_X, ES_SCANCODE_D, ES_SCANCODE_E, ES_SCANCODE_4, ES_SCANCODE_3, 0, 
	0, ES_SCANCODE_SPACE, ES_SCANCODE_V, ES_SCANCODE_F, ES_SCANCODE_T, ES_SCANCODE_R, ES_SCANCODE_5, 0, 
	0, ES_SCANCODE_N, ES_SCANCODE_B, ES_SCANCODE_H, ES_SCANCODE_G, ES_SCANCODE_Y, ES_SCANCODE_6, 0, 
	0, 0, ES_SCANCODE_M, ES_SCANCODE_J, ES_SCANCODE_U, ES_SCANCODE_7, ES_SCANCODE_8, 0, 
	0, ES_SCANCODE_COMMA, ES_SCANCODE_K, ES_SCANCODE_I, ES_SCANCODE_O, ES_SCANCODE_0, ES_SCANCODE_9, 0, 
	0, ES_SCANCODE_PERIOD, ES_SCANCODE_SLASH, ES_SCANCODE_L, ES_SCANCODE_PUNCTUATION_3, ES_SCANCODE_P, ES_SCANCODE_HYPHEN, 0, 
	0, 0, ES_SCANCODE_PUNCTUATION_4, 0, ES_SCANCODE_LEFT_BRACE, ES_SCANCODE_EQUALS, 0, 0, 
	ES_SCANCODE_CAPS_LOCK, ES_SCANCODE_RIGHT_SHIFT, ES_SCANCODE_ENTER, ES_SCANCODE_RIGHT_BRACE, 0, ES_SCANCODE_PUNCTUATION_1, 0, 0, 
	0, ES_SCANCODE_PUNCTUATION_6, 0, 0, 0, 0, ES_SCANCODE_BACKSPACE, 0, 
	0, ES_SCANCODE_NUM_1, 0, ES_SCANCODE_NUM_4, ES_SCANCODE_NUM_7, 0, 0, 0, 
	ES_SCANCODE_NUM_0, ES_SCANCODE_NUM_POINT, ES_SCANCODE_NUM_2, ES_SCANCODE_NUM_5, ES_SCANCODE_NUM_6, ES_SCANCODE_NUM_8, ES_SCANCODE_ESCAPE, ES_SCANCODE_NUM_LOCK, 
	ES_SCANCODE_F11, ES_SCANCODE_NUM_ADD, ES_SCANCODE_NUM_3, ES_SCANCODE_NUM_SUBTRACT, ES_SCANCODE_NUM_MULTIPLY, ES_SCANCODE_NUM_9, ES_SCANCODE_SCROLL_LOCK, 0, 
	0, 0, 0, ES_SCANCODE_F7, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, ES_SCANCODE_PAUSE, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	ES_SCANCODE_WWW_SEARCH, ES_SCANCODE_RIGHT_ALT, ES_SCANCODE_PRINT_SCREEN, 0, ES_SCANCODE_RIGHT_CTRL, ES_SCANCODE_MM_PREVIOUS, 0, 0, 
	ES_SCANCODE_WWW_STARRED, 0, 0, 0, 0, 0, 0, ES_SCANCODE_LEFT_FLAG, 
	ES_SCANCODE_WWW_REFRESH, ES_SCANCODE_MM_QUIETER, 0, ES_SCANCODE_MM_MUTE, 0, 0, 0, ES_SCANCODE_CONTEXT_MENU, 
	ES_SCANCODE_WWW_STOP, 0, 0, ES_SCANCODE_MM_CALC, 0, 0, 0, 0, 
	ES_SCANCODE_WWW_FORWARD, 0, ES_SCANCODE_MM_LOUDER, 0, ES_SCANCODE_MM_PAUSE, 0, 0, ES_SCANCODE_ACPI_POWER, 
	ES_SCANCODE_WWW_BACK, 0, ES_SCANCODE_WWW_HOME, ES_SCANCODE_MM_STOP, 0, 0, 0, ES_SCANCODE_ACPI_SLEEP, 
	ES_SCANCODE_MM_FILES, 0, 0, 0, 0, 0, 0, 0, 
	ES_SCANCODE_MM_EMAIL, 0, ES_SCANCODE_NUM_DIVIDE, 0, 0, ES_SCANCODE_MM_NEXT, 0, 0, 
	ES_SCANCODE_MM_SELECT, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, ES_SCANCODE_NUM_ENTER, 0, 0, 0, ES_SCANCODE_ACPI_WAKE, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, ES_SCANCODE_END, 0, ES_SCANCODE_LEFT_ARROW, ES_SCANCODE_HOME, 0, 0, 0, 
	ES_SCANCODE_INSERT, ES_SCANCODE_DELETE, ES_SCANCODE_DOWN_ARROW, 0, ES_SCANCODE_RIGHT_ARROW, ES_SCANCODE_UP_ARROW, 0, 0, 
	0, 0, ES_SCANCODE_PAGE_DOWN, 0, 0, ES_SCANCODE_PAGE_UP, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
	0, 0, 0, 0, 0, 0, 0, 0, 
};

// Status register.
#define PS2_OUTPUT_FULL 	(1 << 0)
#define PS2_INPUT_FULL 		(1 << 1)
#define PS2_MOUSE_BYTE		(1 << 5)

// Mouse types.
#define PS2_MOUSE_NORMAL	(0)
#define PS2_MOUSE_SCROLL	(3)
#define PS2_MOUSE_5_BUTTON	(4)

// Controller commands.
#define PS2_DISABLE_FIRST	(0xAD)
#define PS2_ENABLE_FIRST	(0xAE)
#define PS2_TEST_FIRST		(0xAB)
#define PS2_DISABLE_SECOND	(0xA7)
#define PS2_ENABLE_SECOND	(0xA8)
#define PS2_WRITE_SECOND	(0xD4)
#define PS2_TEST_SECOND		(0xA9)
#define PS2_TEST_CONTROLER	(0xAA)
#define PS2_READ_CONFIG		(0x20)
#define PS2_WRITE_CONFIG	(0x60)

// Controller configuration.
#define PS2_FIRST_IRQ_MASK 	(1 << 0)
#define PS2_SECOND_IRQ_MASK	(1 << 1)
#define PS2_SECOND_CLOCK	(1 << 5)
#define PS2_TRANSLATION		(1 << 6)

// IRQs.
#define PS2_FIRST_IRQ 		(1)
#define PS2_SECOND_IRQ		(12)

// Ports.
#define PS2_PORT_DATA		(0x60)
#define PS2_PORT_STATUS		(0x64)
#define PS2_PORT_COMMAND	(0x64)

// Keyboard commands.
#define PS2_KEYBOARD_RESET	(0xFF)
#define PS2_KEYBOARD_ENABLE	(0xF4)
#define PS2_KEYBOARD_DISABLE	(0xF5)
#define PS2_KEYBOARD_REPEAT	(0xF3)
#define PS2_KEYBOARD_SET_LEDS	(0xED)
#define PS2_KEYBOARD_SCANCODE_SET (0xF0)

// Mouse commands.
#define PS2_MOUSE_RESET		(0xFF)
#define PS2_MOUSE_ENABLE	(0xF4)
#define PS2_MOUSE_DISABLE	(0xF5)
#define PS2_MOUSE_SAMPLE_RATE	(0xF3)
#define PS2_MOUSE_READ		(0xEB)
#define PS2_MOUSE_RESOLUTION	(0xE8)

void PS2MouseUpdated(EsGeneric _update) {
	PS2Update *update = (PS2Update *) _update.p;

	KMouseUpdateData data = { 
		.xMovement = update->xMovement * K_CURSOR_MOVEMENT_SCALE, 
		.yMovement = update->yMovement * K_CURSOR_MOVEMENT_SCALE,
		.buttons = update->buttons,
	};

	KMouseUpdate(&data);
}

void PS2KeyboardUpdated(EsGeneric _update) {
	PS2Update *update = (PS2Update *) _update.p;
	KernelLog(LOG_VERBOSE, "PS/2", "keyboard update", "Received scancode %x.\n", update->scancode);
	KKeyPress(update->scancode);
}

void PS2::WaitInputBuffer() {
	while (ProcessorIn8(PS2_PORT_STATUS) & PS2_INPUT_FULL);
}

bool PS2::PollRead(uint8_t *value, bool forMouse) {
	uint8_t status = ProcessorIn8(PS2_PORT_STATUS);
	if (status & PS2_MOUSE_BYTE && !forMouse) return false;
	if (!(status & PS2_MOUSE_BYTE) && forMouse) return false;

	if (status & PS2_OUTPUT_FULL) {
		*value = ProcessorIn8(PS2_PORT_DATA);

		if (*value == 0xE1 && !forMouse) {
			KDebugKeyPressed();
		}
		
		EsRandomAddEntropy(*value);
		return true;
	} else {
		return false;
	}
}

int PS2ReadKey() {
	static uint8_t firstByte = 0, secondByte = 0, thirdByte = 0;
	static size_t bytesFound = 0;

	if (bytesFound == 0) {
		if (!ps2.PollRead(&firstByte, false)) return 0;
		bytesFound++;
		if (firstByte != 0xE0 && firstByte != 0xF0) {
			goto sequenceFinished;
		} else return 0;
	} else if (bytesFound == 1) {
		if (!ps2.PollRead(&secondByte, false)) return 0;
		bytesFound++;
		if (secondByte != 0xF0) {
			goto sequenceFinished;
		} else return 0;
	} else if (bytesFound == 2) {
		if (!ps2.PollRead(&thirdByte, false)) return 0;
		bytesFound++;
		goto sequenceFinished;
	}

	sequenceFinished:
	KernelLog(LOG_VERBOSE, "PS/2", "keyboard data", "Keyboard data: %X%X%X\n", firstByte, secondByte, thirdByte);

	int scancode = 0;

	if (ps2.scancodeSet == 1) {
		if (firstByte == 0xE0) {
			if (secondByte & 0x80) {
				scancode = secondByte | K_SCANCODE_KEY_RELEASED;
			} else {
				scancode = secondByte | 0x80;
			}
		} else {
			if (firstByte & 0x80) {
				scancode = (firstByte & ~0x80) | K_SCANCODE_KEY_RELEASED;
			} else {
				scancode = firstByte;
			}
		}
	} else if (ps2.scancodeSet == 2) {
		if (firstByte == 0xE0) {
			if (secondByte == 0xF0) {
				scancode = K_SCANCODE_KEY_RELEASED | (1 << 8) | thirdByte;
			} else {
				scancode = K_SCANCODE_KEY_PRESSED | (1 << 8) | secondByte;
			}
		} else {
			if (firstByte == 0xF0) {
				scancode = K_SCANCODE_KEY_RELEASED | (0 << 8) | secondByte;
			} else {
				scancode = K_SCANCODE_KEY_PRESSED | (0 << 8) | firstByte;
			}
		}
	} 

	uint16_t *table = ps2.scancodeSet == 2 ? scancodeConversionTable2 : scancodeConversionTable1;

	firstByte = 0;
	secondByte = 0;
	thirdByte = 0;
	bytesFound = 0;

	return (scancode & (1 << 15)) | table[scancode & ~(1 << 15)];
}

int KWaitKey() {
	if (!ps2.channels) ProcessorHalt();
	int scancode;
	while (!(scancode = PS2ReadKey()));
	return scancode;
}

bool PS2IRQHandler(uintptr_t interruptIndex, void *) {
	if (!ps2.channels) return false;
	if (ps2.channels == 1 && interruptIndex == 12) return false;

	if (interruptIndex == 12) {
		static uint8_t firstByte = 0, secondByte = 0, thirdByte = 0;
		static size_t bytesFound = 0;

		if (bytesFound == 0) {
			if (!ps2.PollRead(&firstByte, true)) return false;
			if (!(firstByte & 8)) return false;
			bytesFound++;
			return true;
		} else if (bytesFound == 1) {
			if (!ps2.PollRead(&secondByte, true)) return false;
			bytesFound++;
			return true;
		} else if (bytesFound == 2) {
			if (!ps2.PollRead(&thirdByte, true)) return false;
			bytesFound++;
		}

		KernelLog(LOG_VERBOSE, "PS/2", "mouse data", "Mouse data: %X%X%X\n", firstByte, secondByte, thirdByte);

		KSpinlockAcquire(&ps2.lastUpdatesLock);
		PS2Update *update = ps2.lastUpdates + ps2.lastUpdatesIndex;
		ps2.lastUpdatesIndex = (ps2.lastUpdatesIndex + 1) % 16;
		KSpinlockRelease(&ps2.lastUpdatesLock);

		update->xMovement = secondByte - ((firstByte << 4) & 0x100);
		update->yMovement = -(thirdByte - ((firstByte << 3) & 0x100));
		update->buttons = ((firstByte & (1 << 0)) ? K_LEFT_BUTTON : 0)
			      	| ((firstByte & (1 << 1)) ? K_RIGHT_BUTTON : 0)
				| ((firstByte & (1 << 2)) ? K_MIDDLE_BUTTON : 0);

		KRegisterAsyncTask(PS2MouseUpdated, update, false);

		firstByte = 0;
		secondByte = 0;
		thirdByte = 0;
		bytesFound = 0;
	} else if (interruptIndex == 1) {
		KernelLog(LOG_VERBOSE, "PS/2", "keyboard IRQ", "Received keyboard IRQ.\n");

		int scancode = PS2ReadKey();

		if (scancode) {
			KSpinlockAcquire(&ps2.lastUpdatesLock);
			PS2Update *update = ps2.lastUpdates + ps2.lastUpdatesIndex;
			ps2.lastUpdatesIndex = (ps2.lastUpdatesIndex + 1) % 16;
			KSpinlockRelease(&ps2.lastUpdatesLock);
			update->scancode = scancode;
			KRegisterAsyncTask(PS2KeyboardUpdated, update, false);
		}
	} else {
		KernelPanic("PS2IRQHandler - Incorrect interrupt index.\n", interruptIndex);
	}

	return true;
}

void PS2::DisableDevices(unsigned which) {
	WaitInputBuffer();
	// EsPrint("ps2 first write...\n");
	if (which & 1) ProcessorOut8(PS2_PORT_COMMAND, PS2_DISABLE_FIRST);
	// EsPrint("ps2 first write end\n");
	WaitInputBuffer();
	if (which & 2) ProcessorOut8(PS2_PORT_COMMAND, PS2_DISABLE_SECOND);
}

void PS2::EnableDevices(unsigned which) {
	WaitInputBuffer();
	if (which & 1) ProcessorOut8(PS2_PORT_COMMAND, PS2_ENABLE_FIRST);
	WaitInputBuffer();
	if (which & 2) ProcessorOut8(PS2_PORT_COMMAND, PS2_ENABLE_SECOND);
}

void PS2::FlushOutputBuffer() {
	while (ProcessorIn8(PS2_PORT_STATUS) & PS2_OUTPUT_FULL) {
		ProcessorIn8(PS2_PORT_DATA);
	}
}

void PS2::SendCommand(uint8_t command) {
	WaitInputBuffer();
	ProcessorOut8(PS2_PORT_COMMAND, command);
}

uint8_t PS2::ReadByte(KTimeout *timeout) {
	while (!(ProcessorIn8(PS2_PORT_STATUS) & PS2_OUTPUT_FULL) && !timeout->Hit());
	return ProcessorIn8(PS2_PORT_DATA);
}

void PS2::WriteByte(KTimeout *timeout, uint8_t value) {
	while ((ProcessorIn8(PS2_PORT_STATUS) & PS2_INPUT_FULL) && !timeout->Hit());
	if (timeout->Hit()) return;
	ProcessorOut8(PS2_PORT_DATA, value);
}

bool PS2::SetupKeyboard(KTimeout *timeout) {
	WaitInputBuffer();
	ProcessorOut8(PS2_PORT_DATA, PS2_KEYBOARD_ENABLE);
	if (ReadByte(timeout) != 0xFA) return false;

	WaitInputBuffer();
	ProcessorOut8(PS2_PORT_DATA, PS2_KEYBOARD_SCANCODE_SET);
	if (ReadByte(timeout) != 0xFA) return false;
	WaitInputBuffer();
	ProcessorOut8(PS2_PORT_DATA, 0);
	if (ReadByte(timeout) != 0xFA) return false;
	scancodeSet = ReadByte(timeout) & 3;
	KernelLog(LOG_INFO, "PS/2", "scancode set", "Keyboard reports it is using scancode set %d.\n", scancodeSet);

	WaitInputBuffer();
	ProcessorOut8(PS2_PORT_DATA, PS2_KEYBOARD_REPEAT);
	if (ReadByte(timeout) != 0xFA) return false;
	WaitInputBuffer();
	ProcessorOut8(PS2_PORT_DATA, 0);
	if (ReadByte(timeout) != 0xFA) return false;

	return true;
}

bool PS2::SetupMouse(KTimeout *timeout) {
	// TODO Mouse with scroll wheel detection.

	WaitInputBuffer();
	ProcessorOut8(PS2_PORT_COMMAND, PS2_WRITE_SECOND);
	WaitInputBuffer();
	ProcessorOut8(PS2_PORT_DATA, PS2_MOUSE_RESET);
	if (ReadByte(timeout) != 0xFA) return false;
	if (ReadByte(timeout) != 0xAA) return false;
	if (ReadByte(timeout) != 0x00) return false;
	WaitInputBuffer();
	ProcessorOut8(PS2_PORT_COMMAND, PS2_WRITE_SECOND);
	WaitInputBuffer();
	ProcessorOut8(PS2_PORT_DATA, PS2_MOUSE_SAMPLE_RATE);
	if (ReadByte(timeout) != 0xFA) return false;
	WaitInputBuffer();
	ProcessorOut8(PS2_PORT_COMMAND, PS2_WRITE_SECOND);
	WaitInputBuffer();
	ProcessorOut8(PS2_PORT_DATA, 100);
	if (ReadByte(timeout) != 0xFA) return false;
	WaitInputBuffer();
	ProcessorOut8(PS2_PORT_COMMAND, PS2_WRITE_SECOND);
	WaitInputBuffer();
	ProcessorOut8(PS2_PORT_DATA, PS2_MOUSE_RESOLUTION);
	if (ReadByte(timeout) != 0xFA) return false;
	WaitInputBuffer();
	ProcessorOut8(PS2_PORT_COMMAND, PS2_WRITE_SECOND);
	WaitInputBuffer();
	ProcessorOut8(PS2_PORT_DATA, 3);
	if (ReadByte(timeout) != 0xFA) return false;
	WaitInputBuffer();
	ProcessorOut8(PS2_PORT_COMMAND, PS2_WRITE_SECOND);
	WaitInputBuffer();
	ProcessorOut8(PS2_PORT_DATA, PS2_MOUSE_ENABLE);
	if (ReadByte(timeout) != 0xFA) return false;

	return true;
}

void PS2::Initialise(KDevice *parentDevice) {
	KMutexAcquire(&mutex);
	EsDefer(KMutexRelease(&mutex));

	if (initialised) {
		return;
	}

	initialised = true;
	channels = 0;

	KTimeout timeout(10000);

	FlushOutputBuffer();

	// TODO PS/2 detection with ACPI.

	DisableDevices(1 | 2);
	FlushOutputBuffer();

	WaitInputBuffer();
	ProcessorOut8(PS2_PORT_COMMAND, PS2_READ_CONFIG);
	uint8_t configurationByte = ReadByte(&timeout);
	WaitInputBuffer();
	ProcessorOut8(PS2_PORT_COMMAND, PS2_WRITE_CONFIG);
	WriteByte(&timeout, configurationByte & ~(PS2_FIRST_IRQ_MASK | PS2_SECOND_IRQ_MASK | PS2_TRANSLATION));
	if (timeout.Hit()) return;

	SendCommand(PS2_TEST_CONTROLER);
	if (ReadByte(&timeout) != 0x55) return;

	bool hasMouse = false;
	if (configurationByte & PS2_SECOND_CLOCK) {
		EnableDevices(2);
		WaitInputBuffer();
		ProcessorOut8(PS2_PORT_COMMAND, PS2_READ_CONFIG);
		configurationByte = ReadByte(&timeout);
		if (!(configurationByte & PS2_SECOND_CLOCK)) {
			hasMouse = true;
			DisableDevices(2);
		}
	}

	{
		WaitInputBuffer();
		ProcessorOut8(PS2_PORT_COMMAND, PS2_TEST_FIRST);
		uint8_t b = ReadByte(&timeout);
		if (b) return;
		if (timeout.Hit()) return;
		channels = 1;
	}

	if (hasMouse) {
		WaitInputBuffer();
		ProcessorOut8(PS2_PORT_COMMAND, PS2_TEST_SECOND);
		if (!ReadByte(&timeout) && !timeout.Hit()) channels = 2;
	}

	EnableDevices(1 | 2);

	if (!SetupKeyboard(&timeout) || timeout.Hit()) {
		channels = 0;
		return;
	}

	if (!SetupMouse(&timeout) || timeout.Hit()) {
		channels = 1;
	}

	{
		WaitInputBuffer();
		ProcessorOut8(PS2_PORT_COMMAND, PS2_READ_CONFIG);
		uint8_t configurationByte = ReadByte(&timeout);
		WaitInputBuffer();
		ProcessorOut8(PS2_PORT_COMMAND, PS2_WRITE_CONFIG);
		WriteByte(&timeout, configurationByte | PS2_FIRST_IRQ_MASK | PS2_SECOND_IRQ_MASK);
	}

	if (!registeredIRQs) {
		KRegisterIRQ(PS2_FIRST_IRQ, PS2IRQHandler, nullptr, "PS2");
		KRegisterIRQ(PS2_SECOND_IRQ, PS2IRQHandler, nullptr, "PS2");
		registeredIRQs = true;
	}

	KDevice *controller = KDeviceCreate("PS/2 controller", parentDevice, sizeof(KDevice));
	KRegisterHIDevice((KHIDevice *) KDeviceCreate("PS/2 keyboard", controller, sizeof(KHIDevice)));
	if (channels == 2) KRegisterHIDevice((KHIDevice *) KDeviceCreate("PS/2 mouse", controller, sizeof(KHIDevice)));

	KernelLog(LOG_INFO, "PS/2", "controller initialised", "Setup PS/2 controller%z.\n", channels == 2 ? ", with a mouse" : "");
}

static void DeviceAttach(KDevice *parentDevice) {
	ps2.Initialise(parentDevice);
}

KDriver driverPS2 = {
	.attach = DeviceAttach,
};
