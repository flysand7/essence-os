#ifdef API_TESTS_FOR_RUNNER

#define TEST(_callback) { .cName = #_callback }
typedef struct Test { const char *cName; } Test;

#else

#define ES_PRIVATE_APIS
#include <essence.h>
#include <shared/crc.h>

#define TEST(_callback) { .callback = _callback }
struct Test { bool (*callback)(); };

bool SuccessTest() {
	return true;
}

bool FailureTest() {
	return false;
}

bool TimeoutTest() {
	EsProcessTerminateCurrent();
	return true;
}

#endif

const Test tests[] = {
	TEST(TimeoutTest), TEST(SuccessTest), TEST(FailureTest)
};

#ifndef API_TESTS_FOR_RUNNER

void RunTests() {
	size_t fileSize;
	EsError error;
	void *fileData = EsFileReadAll(EsLiteral("|Settings:/test.dat"), &fileSize, &error); 

	if (error == ES_ERROR_FILE_DOES_NOT_EXIST) {
		return; // Not in test mode.
	} else if (error != ES_SUCCESS) {
		EsPrint("Could not read test.dat (error %d).\n", error);
	} else if (fileSize != sizeof(uint32_t)) {
		EsPrint("test.dat is the wrong size (got %d, expected %d).\n", fileSize, sizeof(uint32_t));
	} else {
		uint32_t index;
		EsMemoryCopy(&index, fileData, sizeof(uint32_t));
		EsHeapFree(fileData);
		
		if (index >= sizeof(tests) / sizeof(tests[0])) {
			EsPrint("Test index out of bounds.\n");
		} else if (tests[index].callback()) {
			EsPrint("[APITests-Success]\n");
		} else {
			EsPrint("[APITests-Failure]\n");
		}
	}

	EsSyscall(ES_SYSCALL_SHUTDOWN, SHUTDOWN_ACTION_POWER_OFF, 0, 0, 0);
	EsProcessTerminateCurrent();
}

void _start() {
	_init();

	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_INSTANCE_CREATE) {
			EsInstance *instance = EsInstanceCreate(message, EsLiteral("API Tests"));
			EsApplicationStartupRequest request = EsInstanceGetStartupRequest(instance);

			if (request.flags & ES_APPLICATION_STARTUP_BACKGROUND_SERVICE) {
				RunTests();
			}
		}
	}
}

#endif
