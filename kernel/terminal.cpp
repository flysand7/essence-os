// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

// TODO Everything in this file is a hack just so I can debug the kernel.
// 	Replace all of it!!!

#ifdef IMPLEMENTATION

#include <shared/vga_font.cpp>

#define TERMINAL_ADDRESS ((uint16_t *) (LOW_MEMORY_MAP_START + 0xB8000))

#if 1
KSpinlock terminalLock; 
KSpinlock printLock; 
#else
KMutex terminalLock; 
KMutex printLock; 
#endif

void DebugWriteCharacter(uintptr_t character);
int KWaitKey();

#if defined(ES_ARCH_X86_32) || defined(ES_ARCH_X86_64)
bool printToDebugger = false;
uintptr_t terminalPosition = 80;

#define KERNEL_LOG_SIZE (262144)
char kernelLog[KERNEL_LOG_SIZE];
uintptr_t kernelLogPosition;

static void TerminalCallback(int character, void *) {
	if (!character) return;

	KSpinlockAcquire(&terminalLock);
	EsDefer(KSpinlockRelease(&terminalLock));

	if (sizeof(kernelLog)) {
		kernelLog[kernelLogPosition] = character;
		kernelLogPosition++;
		if (kernelLogPosition == sizeof(kernelLog)) kernelLogPosition = 0;
	}

#ifdef VGA_TEXT_MODE
	{
		if (character == '\n') {
			terminalPosition = terminalPosition - (terminalPosition % 80) + 80;
		} else {
			TERMINAL_ADDRESS[terminalPosition] = (uint16_t) character | 0x0700;
			terminalPosition++;
		}

		if (terminalPosition >= 80 * 25) {
			for (int i = 80; i < 80 * 25; i++) {
				TERMINAL_ADDRESS[i - 80] = TERMINAL_ADDRESS[i];
			}

			for (int i = 80 * 24; i < 80 * 25; i++) {
				TERMINAL_ADDRESS[i] = 0x700;
			}

			terminalPosition -= 80;

			// uint64_t start = ProcessorReadTimeStamp();
			// uint64_t end = start + 250 * KGetTimeStampTicksPerMs();
			// while (ProcessorReadTimeStamp() < end);
		}

		{
			ProcessorOut8(0x3D4, 0x0F);
			ProcessorOut8(0x3D5, terminalPosition);
			ProcessorOut8(0x3D4, 0x0E);
			ProcessorOut8(0x3D5, terminalPosition >> 8);
		}
	}
#endif

	{
		ProcessorDebugOutputByte((uint8_t) character);

		if (character == '\n') {
			ProcessorDebugOutputByte((uint8_t) 13);
		}
	}

	if (printToDebugger) {
		DebugWriteCharacter(character);
		if (character == '\t') DebugWriteCharacter(' ');
	}
}
#endif

size_t debugRows, debugColumns, debugCurrentRow, debugCurrentColumn;

void DebugWriteCharacter(uintptr_t character) {
	if (!graphics.target || !graphics.target->debugPutBlock) return;

	if (debugCurrentRow == debugRows) {
#if 0
		debugCurrentRow = 0;

		// uint64_t start = ProcessorReadTimeStamp();
		// uint64_t end = start + 3000 * KGetTimeStampTicksPerMs();
		// while (ProcessorReadTimeStamp() < end);

		graphics.target->debugClearScreen();
#else
		return;
#endif
	}

	uintptr_t row = debugCurrentRow;
	uintptr_t column = debugCurrentColumn;

	if (character == '\n') {
		debugCurrentRow++;
		debugCurrentColumn = 0;
		return;
	}

	if (character > 127) character = ' ';
	if (row >= debugRows) return;
	if (column >= debugColumns) return;

	for (int j = 0; j < VGA_FONT_HEIGHT; j++) {
		uint8_t byte = ((uint8_t *) vgaFont)[character * 16 + j];

		for (int i = 0; i < 8; i++) {
			uint8_t bit = byte & (1 << i);
			if (bit) graphics.target->debugPutBlock((column + 1) * 9 + i, row * 16 + j, false);
		}
	}

	debugCurrentColumn++;

	if (debugCurrentColumn == debugColumns) {
		debugCurrentRow++;
		debugCurrentColumn = 4;
	}
}

void StartDebugOutput() {
	if (graphics.target && graphics.target->debugClearScreen && graphics.target->debugPutBlock && !printToDebugger) {
		graphics.target->debugClearScreen();

		int widthUsed = 0;

		if (graphics.target->debugPutData) {
			widthUsed = graphics.target->debugPutData((const uint8_t *) kernelLog, KERNEL_LOG_SIZE);
		}

		debugRows = (graphics.height - 1) / VGA_FONT_HEIGHT;
		debugColumns = (graphics.width - 1 - widthUsed) / VGA_FONT_WIDTH - 2;
		debugCurrentRow = debugCurrentColumn = 0;
		printToDebugger = true;
	}
}

bool debugKeyPressed;

void KDebugKeyPressed() {
	if (debugKeyPressed) return;
	debugKeyPressed = true;
	KernelPanic("Debug key pressed.\n");
}

#ifdef POST_PANIC_DEBUGGING
uintptr_t DebugReadNumber() {
	uintptr_t value = 0;

	for (uintptr_t i = 0; i < 2 * sizeof(uintptr_t); i++) {
		value <<= 4;

		while (true) {
			int key = KWaitKey();
			if (key == ES_SCANCODE_0) { EsPrint("0"); value |= 0; }
			else if (key == ES_SCANCODE_1) { EsPrint("1"); value |= 1; }
			else if (key == ES_SCANCODE_2) { EsPrint("2"); value |= 2; }
			else if (key == ES_SCANCODE_3) { EsPrint("3"); value |= 3; }
			else if (key == ES_SCANCODE_4) { EsPrint("4"); value |= 4; }
			else if (key == ES_SCANCODE_5) { EsPrint("5"); value |= 5; }
			else if (key == ES_SCANCODE_6) { EsPrint("6"); value |= 6; }
			else if (key == ES_SCANCODE_7) { EsPrint("7"); value |= 7; }
			else if (key == ES_SCANCODE_8) { EsPrint("8"); value |= 8; }
			else if (key == ES_SCANCODE_9) { EsPrint("9"); value |= 9; }
			else if (key == ES_SCANCODE_A) { EsPrint("A"); value |= 10; }
			else if (key == ES_SCANCODE_B) { EsPrint("B"); value |= 11; }
			else if (key == ES_SCANCODE_C) { EsPrint("C"); value |= 12; }
			else if (key == ES_SCANCODE_D) { EsPrint("D"); value |= 13; }
			else if (key == ES_SCANCODE_E) { EsPrint("E"); value |= 14; }
			else if (key == ES_SCANCODE_F) { EsPrint("F"); value |= 15; }
			else if (key == ES_SCANCODE_ENTER) { value >>= 4; return value; }
			else continue;
			break;
		}
	}

	return value;
}
#endif

void KernelPanic(const char *format, ...) {
	ProcessorDisableInterrupts();
	ProcessorSendIPI(KERNEL_PANIC_IPI, true);

	// Disable synchronisation objects. The panic IPI must be sent before this, 
	// so other processors don't start getting "mutex not correctly acquired" panics.
	scheduler.panic = true; 

	if (debugKeyPressed) {
		DriversDumpState();
	}

	StartDebugOutput();

	EsPrint("\n--- System Error ---\n* If you are using an emulator, please capture a screenshot of the entire window and report the error. *\n>> ");

	va_list arguments;
	va_start(arguments, format);
	_StringFormat(TerminalCallback, (void *) 0x4F00, format, arguments);
	va_end(arguments);

	EsPrint("Current thread = %x\n", GetCurrentThread());
	EsPrint("Trace: %x\n", __builtin_return_address(0));
#ifdef ES_ARCH_X86_64
	EsPrint("RSP: %x; RBP: %x\n", ProcessorGetRSP(), ProcessorGetRBP());
#endif
	// EsPrint("Memory: %x/%x\n", pmm.pagesAllocated, pmm.startPageCount);

	{
		EsPrint("Threads:\n");

		LinkedItem<Thread> *item = scheduler.allThreads.firstItem;

		while (item) {
			Thread *thread = item->thisItem;

#ifdef ES_ARCH_X86_64
			EsPrint("%z %d %x @%x:%x ", (GetCurrentThread() == thread) ? "=>" : "  ", 
					thread->id, thread, thread->interruptContext ? thread->interruptContext->rip : 0, 
					thread->interruptContext ? thread->interruptContext->rbp : 0);
#endif

			if (thread->state == THREAD_WAITING_EVENT) {
				EsPrint("WaitEvent(Count:%d, %x) ", thread->blocking.eventCount, thread->blocking.events[0]);
			} else if (thread->state == THREAD_WAITING_MUTEX) {
				EsPrint("WaitMutex(%x, Owner:%d) ", thread->blocking.mutex, thread->blocking.mutex->owner->id);
			} else if (thread->state == THREAD_WAITING_WRITER_LOCK) {
				EsPrint("WaitWriterLock(%x, %d) ", thread->blocking.writerLock, thread->blocking.writerLockType);
			}

			Process *process = thread->process;
			EsPrint("%z:%z\n", process->cExecutableName, thread->cName);

			item = item->nextItem;
		}
	}

	for (uintptr_t i = 0; i < KGetCPUCount(); i++) {
		CPULocalStorage *local = KGetCPULocal(i);

		if (local && local->panicContext) {
#ifdef ES_ARCH_X86_64
			EsPrint("CPU %d LS %x RIP/RBP %x:%x TID %d\n", local->processorID, local,
					local->panicContext->rip, local->panicContext->rbp,
					local->currentThread ? local->currentThread->id : 0);
#endif
		}
	}

#ifdef POST_PANIC_DEBUGGING
	uintptr_t kernelLogEnd = kernelLogPosition;
	EsPrint("Press 'D' to enter debugger.\n");

	while (true) {
		int key = KWaitKey();
		if (key == ES_SCANCODE_D) break;
		if (key == -1) ProcessorHalt();
	}

	graphics.debuggerActive = true;

	while (true) {
#ifdef VGA_TEXT_MODE
		for (uintptr_t i = 0; i < 80 * 25; i++) {
			TERMINAL_ADDRESS[i] = 0x0700;
		}

		terminalPosition = 80;
#else
		graphics.target->debugClearScreen();

		debugCurrentRow = debugCurrentColumn = 0;
#endif


		EsPrint("0 - view log\n1 - reset\n2 - view pmem\n3 - view vmem\n4 - stack trace\n");

		int key = KWaitKey();

		if (key == ES_SCANCODE_0) {
			uintptr_t position = 0, nextPosition = 0;
			uintptr_t x = 0, y = 0;

#ifdef VGA_TEXT_MODE
			for (uintptr_t i = 0; i < 80 * 25; i++) {
				TERMINAL_ADDRESS[i] = 0x0700;
			}
#else
			graphics.target->debugClearScreen();
#endif

			while (position < kernelLogEnd) {
				char c = kernelLog[position];

				if (c != '\n') {
#ifdef VGA_TEXT_MODE
					TERMINAL_ADDRESS[x + y * 80] = c | 0x0700;
#else
					debugCurrentRow = y, debugCurrentColumn = x;
					DebugWriteCharacter(c);
#endif
				}

				x++;

				if (x == 
#ifdef VGA_TEXT_MODE
						80 
#else
						debugColumns
#endif
						|| c == '\n') {
					x = 0;
					y++;

					if (y == 1) {
						nextPosition = position;
					}
				}

				if (y == 
#ifdef VGA_TEXT_MODE
						25
#else
						debugRows
#endif
						) {
					while (true) {
						int key = KWaitKey();

						if (key == ES_SCANCODE_SPACE || key == ES_SCANCODE_DOWN_ARROW) {
							position = nextPosition;
							break;
						} else if (key == ES_SCANCODE_UP_ARROW) {
							position = nextPosition;
							if (position < 240) position = 0;
							else position -= 240;
							break;
						}
					}

#ifdef VGA_TEXT_MODE
					for (uintptr_t i = 0; i < 80 * 25; i++) {
						TERMINAL_ADDRESS[i] = 0x0700;
					}
#else
					graphics.target->debugClearScreen();
#endif

					y = 0;
				}

				position++;
			}

			KWaitKey();
		} else if (key == ES_SCANCODE_1) {
			ProcessorReset();
		} else if (key == ES_SCANCODE_2) {
			EsPrint("Enter address: ");
			uintptr_t address = DebugReadNumber();
			uintptr_t offset = address & (K_PAGE_SIZE - 1);
			MMRemapPhysical(kernelMMSpace, pmm.pmManipulationRegion, address - offset);
			uintptr_t *data = (uintptr_t *) ((uint8_t *) pmm.pmManipulationRegion + offset);

			for (uintptr_t i = 0; i < 8 && (offset + 8 * sizeof(uintptr_t) < K_PAGE_SIZE); i++) {
				EsPrint("\n%x - %x\n", address + 8 * sizeof(uintptr_t), data[i]);
			}

			while (KWaitKey() != ES_SCANCODE_ENTER);
		} else if (key == ES_SCANCODE_3) {
			EsPrint("Enter address: ");
			uintptr_t address = DebugReadNumber();
			uintptr_t offset = address & (K_PAGE_SIZE - 1);
			uintptr_t *data = (uintptr_t *) address;

			for (uintptr_t i = 0; i < 8 && (offset + i * sizeof(uintptr_t) < K_PAGE_SIZE); i++) {
				EsPrint("\n%x - %x", address + i * sizeof(uintptr_t), data[i]);
			}

			while (KWaitKey() != ES_SCANCODE_ENTER);
		} else if (key == ES_SCANCODE_4) {
			EsPrint("Enter RBP: ");
			uintptr_t address = DebugReadNumber();

			while (address) {
				EsPrint("\n%x", ((uintptr_t *) address)[1]);
				address = ((uintptr_t *) address)[0];
			}

			while (KWaitKey() != ES_SCANCODE_ENTER);
		}
	}
#else
	EsPrint("End of report.\n");

	if (graphics.target->debugPutData) {
		// We put the log data on the screen before and after the panic report in case it gets stuck.
		graphics.target->debugPutData((const uint8_t *) kernelLog, KERNEL_LOG_SIZE);
	}
#endif

	ProcessorHalt();
}

void EsPrint(const char *format, ...) {
	KSpinlockAcquire(&printLock);
	EsDefer(KSpinlockRelease(&printLock));

	va_list arguments;
	va_start(arguments, format);
	_StringFormat(TerminalCallback, (void *) 0x0700, format, arguments);
	va_end(arguments);
}

void __KernelLog(const char *format, ...) {
	va_list arguments;
	va_start(arguments, format);
	_StringFormat(TerminalCallback, nullptr, format, arguments);
	va_end(arguments);
}

void KernelLog(KLogLevel level, const char *subsystem, const char *event, const char *format, ...) {
	if (level == LOG_VERBOSE) return;
	(void) event;

	KSpinlockAcquire(&printLock);
	EsDefer(KSpinlockRelease(&printLock));

	__KernelLog("[%z:%z] ", level == LOG_INFO ? "Info" : level == LOG_ERROR ? "**Error**" : level == LOG_VERBOSE ? "Verbose" : "", subsystem);

	va_list arguments;
	va_start(arguments, format);
	_StringFormat(TerminalCallback, nullptr, format, arguments);
	va_end(arguments);
}

#endif
