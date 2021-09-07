# Random number generator

## Definitions

```c
uint8_t EsRandomU8(); 
uint64_t EsRandomU64();
void EsRandomAddEntropy(uint64_t x); 
void EsRandomSeed(uint64_t x); 
```

## Description

Used to generate pseudo-random numbers. **Note**: the algorithm used is not suitable for cryptographic or statistical applications. These functions are thread-safe.

`EsRandomU8` generates a single byte of random data, and `EsRandomU64` generates a random `uint64_t`.

`EsRandomSeed` begins a new sequence of random numbers. For a given seed, subsequent calls to `EsRandomU64` will form the same sequence every time. 

`EsRandomAddEntropy` is used to move to a different point in the sequence of numbers, where there is no obvious link between the input value and the new sequence position.

## Example

```c
EsPrint("A random number between 1 and 100 is:\n", 1 + (EsRandomU64() % 100));
```

# Performance timers

## Definitions

```c
void EsPerformanceTimerPush();
double EsPerformanceTimerPop();
```

## Description

Used to accurately time sections of code.

`EsPerformanceTimerPush` pushes the current time onto a stack. `EsPerformanceTimerPop` removes the top item, and returns the elapsed time since that item was added.

The stack must not exceed more than 100 items. 

## Example

```c
EsPerformanceTimerPush();

EsPerformanceTimerPush();
PerformStep1();
double timeStep1 = EsPerformanceTimerPop();

EsPerformanceTimerPush();
PerformStep2();
double timeStep2 = EsPerformanceTimerPop();

double timeTotal = EsPerformanceTimerPop();

EsPrint("Total time: %F seconds.\n", timeTotal);
EsPrint("\tStep 1 took %F seconds.\n", timeStep1);
EsPrint("\tStep 2 took %F seconds.\n", timeStep2);
```

# Threads

## Definitions

```c
struct EsThreadInformation {
	EsHandle handle;
	uint64_t tid;
}

#define ES_CURRENT_THREAD ((EsHandle) (0x10))

typedef void (*EsThreadEntryCallback)(EsGeneric argument);

EsError EsThreadCreate(EsThreadEntryCallback entryFunction, EsThreadInformation *information, EsGeneric argument); 
uint64_t EsThreadGetID(EsHandle thread);
void EsThreadTerminate(EsHandle thread); 
```

## Description

Threads are used to execute code in parallel. Threads can be created and terminated. A process can manipulate threads via handles. A handle to a thread can be closed with `EsHandleClose`. Each thread has a unique ID. A thread ID will not be reused until all handles to the thread that previously used the ID have been closed. Threads may not necessarily execute in parallel; the system may simulate the effect of parallel execution by causing the CPU to switch rapidly between which thread it is executing.

`EsThreadCreate` creates a new thread, starting at the provided `entryFunction`, which will be passed `argument`. After the call, `information` will be filled with a `handle` to the newly created thread, and the thread's unique ID in `tid`. This function returns `ES_SUCCESS` if the thread was successfully created.

`EsThreadGetID` gets the ID of a thread from its handle. 

`EsThreadTerminate` instructs a thread to terminate. If the thread is executing privileged code at the time of the request, it will complete the prviledged code before terminating. If a thread is waiting on a synchronisation object, such as a mutex or event, it will stop waiting and terminate regardless. **Note**: if a thread owns a mutex or spinlock when it is terminated, it will **not** release the object.

A thread can always use the handle `ES_CURRENT_THREAD` to access itself. This handle should not be closed.

## Example

```c
void MyThread(EsGeneric number) {
	while (true) { 
		EsPrint("Thread %d has ID %d!\n", number.i, EsThreadGetID(ES_CURRENT_THREAD));
	}
}

for (uintptr_t number = 1; number <= 5; i++) {
	EsThreadInformation information;
	EsError error = EsThreadCreate(MyThread, &information, number);

	if (error != ES_SUCCESS) {
		EsPrint("Thread %d could not be created.\n", number);
	} else {
		EsPrint("Started thread %d with ID %d.\n", number, information.tid);

		// Close the handle to the thread.
		EsHandleClose(information.handle):
	}
}
```

# Mutexes

## Definitions

```c
void EsMutexAcquire(EsMutex *mutex); 
void EsMutexDestroy(EsMutex *mutex); 
void EsMutexRelease(EsMutex *mutex); 
```

## Description

A mutex is a synchronisation primitive. Threads can *acquire* and *release* it. Only one thread can have acquired the mutex at a time. Before another thread can acquire it, the original thread must release it. When a thread tries to acquire an already-acquired mutex, it will wait until the mutex is released, and then proceed to acquire it.

The `EsMutex` structure contains a mutex. It should be initialised to zero. When the mutex is no longer needed, it can be destroyed with `EsMutexDestroy`. A mutex must not be acquired when it is destroyed.

To acquire a mutex, call `EsMutexAcquire` with a pointer to the mutex. To release a mutex, call `EsMutexRelease`.  A thread must not attempt to acquire a mutex it already owns, and it must not attempt to release a mutex it does not own.

## Example

In this example, the function `IncrementCount` can safely be called on different threads at the same time.

```c
EsMutex mutex;
#define INITIAL_COUNT (10)

void IncrementCount(int *count) {
	EsMutexAcquire(&mutex);

	if (count == 0) {
		// count is uninitialised, so initialise it now.
		*count = INITIAL_COUNT;
	}

	count++;

	EsMutexRelease(&mutex);
}
```

Without the mutex, unexpected behaviour may occur, as the effective operation of threads may be arbitrarily interleaved. 

Consider two threads, A and B, that both call `IncrementCount`:
- Thread A enters IncrementCount and sees that `count = 0`.
- Thread B enters IncrementCount and sees that `count = 0`.
- Thread A sets `count` to `10`.
- Thread A increments `count` by `1` to `11`.
- Thread B sets `count` to `10`.
- Thread B increments `count` by `1` to `11`.

Despite `IncrementCount` being called twice, the count is only incremented once.

Another possibility is:
- Thread A enters IncrementCount and reads `count` into a register; it has the value `10`.
- Thread A adds `1` to the register, giving `11`.
- Thread B enters IncrementCount and reads `count` into a register; it has the value `10`.
- Thread B adds `1` to the register, giving `11`.
- Thread A stores the value in its register into `count`, `11`.
- Thread B stores the value in its register into `count`, `11`.

Again, despite `IncrementCount` being called twice, the count is only incremented once.

## Deadlock

Mutexes must be acquired in a consistent order. For example, if the following pattern appears in your code:

1. Acquire mutex X.
2. Acquire mutex Y.
3. Release mutex Y.
4. Release mutex X.

Then the following pattern must not occur:

1. Acquire mutex Y.
2. Acquire mutex X.
3. Release mutex X.
4. Release mutex Y.

To explain why, suppose thread A executes the first pattern and thread B executes the second.
- Thread A acquires mutex X.
- Thread B acquires mutex Y.
- Thread A attempts to acquire mutex Y. Mutex Y is owned by thread B, so thread A starts waiting for thread B to release it.
- Thread B attempts to acquire mutex X. Mutex X is owned by thread A, so thread B starts waiting for thread A to release it.
- Both threads will continue to wait indefinitely for the other to perform an operation.

# INI files

## Definitions

```c
struct EsINIState {
	char *buffer, *sectionClass, *section, *key, *value;
	size_t bytes, sectionClassBytes, sectionBytes, keyBytes, valueBytes;
};

bool EsINIParse(EsINIState *s);
bool EsINIPeek(EsINIState *s);
size_t EsINIFormat(EsINIState *s, char *buffer, size_t bytes);
void EsINIZeroTerminate(EsINIState *s);
```

## Description

To parse an INI file, first initialise a blank EsINIState structure. Set `buffer` to point to the INI data, and set `bytes` to the byte count of the data. Then, call `EsINIParse` repeatedly, until it returns false, indicating it has reached the end of the data. After each call to `EsINIParse`, the fields of `EsINIState` are updated to give the information about the last parsed line in the INI file. `EsINIPeek` is the same as `EsINIParse` except it does not advance to the next line in the INI data. Fields in `EsINIState` that are not applicable to the parsed line are set to empty strings. Comment lines set `key` to `;` and `value` contains the comment itself. Aside for empty strings, the fields in `EsINIState` will always point into the provided buffer.

For example, the line `[@hello world]` will set `sectionClass` to `"hello"`, `section` to `"world"`, and `key` and `value` to empty strings. When followed by the line `k=v`, `sectionClass` and `section` will retain their previous values, and `key` will be set to `"k"` and `value` will be set to `"v"`.

`EsINIFormat` formats the contents of `EsINIState` into a buffer. `bytes` gives the size of `buffer`. The return value gives the number of bytes written to `buffer`; this is clipped to the end of the buffer.

`EsINIZeroTerminate` zero-terminates `sectionClass`, `section`, `key` and `value` in the `EsINIState`. It cannot be used after calling `EsINIPeek`. 

## Example

```c
size_t fileBytes;
void *file = EsFileReadAll(EsLiteral("|Settings:/Default.ini"), &fileBytes);

EsINIState s = {};
s.buffer = file;
s.bytes = fileBytes;

while (EsINIParse(&s)) {
	EsINIZeroTerminate(&s);
	EsPrint("section = %z, key = %z, value = %z\n", s.section, s.key, s.value);
}

EsHeapFree(file);
```

# Heap allocator

## Definitions

```c
void *EsHeapAllocate(size_t size, bool zeroMemory, EsHeap *heap = ES_NULL);
void EsHeapFree(void *address, size_t expectedSize = 0, EsHeap *heap = ES_NULL);
void *EsHeapReallocate(void *oldAddress, size_t newAllocationSize, bool zeroNewSpace, EsHeap *heap = ES_NULL);
void EsHeapValidate(); 
```

## Description

The heap allocator is a general-purpose allocator. It is thread-safe. It is designed to handle regions of memory of arbitrary size, and arbitrary lifetime.

To allocate memory, call `EsHeapAllocate`. Set `size` to be the size of the region in bytes. Set `zeroMemory` to `true` for the contents of the region to be automatically zeroed, otherwise `false`. Set `heap` to `NULL`; this parameter is reserved. The return value is the address of the start of allocated region. It will be suitably aligned, given the specified region size and current processor architecture. If `size` is `0`, then `NULL` is returned.

To free memory, call `EsHeapFree`. Set `address` to be the start of the previously allocated region. Optionally, set `expectedSize` to be the size of the allocated region; if non-zero, the system will assert that this value is correct. Set `heap` to `NULL`; this parameter is reserved. If `address` is `NULL`, this function will do nothing.

To grow or shrink an existing region, call `EsHeapReallocate`. Set `oldAddress` to be the start of the previously allocated region. Set `newAllocationSize` to be new the size of the region. Set `heap` to `NULL`; this parameter is reserved. If `oldAddress` is `NULL`, this call is equivalent to `EsHeapAllocate`. If `newAllocationSize` is `0`, this call is equivalent to `EsHeapFree`. The return value gives the new start address of the region. This may be the same as `oldAddress`, if the region was able to change size in place. If the region was not able to change size in place, the old contents will be copied to the new region. If `zeroNewSpace` is set, and `newAllocationSize` is greater than the previous size of the region, then the newly accessible bytes at the end of the region will be cleared to zero.

`EsHeapValidate` will check the current process's heap for errors. If it finds an error, it will crash the process. Do not rely on any behaviour of this function. It is only intended to aid debugging memory errors.

## Example

```c
int *array = (int *) EsHeapAllocate(sizeof(int) * 10, true);

for (int i = 0; i < 5; i++) {
	array[i] = i + 1;
}

for (int i = 0; i < 10; i++) {
	EsPrint("%d ", array[i]); // Prints 1 2 3 4 5 0 0 0 0 0.
}

EsPrint("\n");

array = (int *) EsHeapReallocate(array, sizeof(int) * 15, true);

for (int i = 0; i < 15; i++) {
	EsPrint("%d ", array[i]); // Prints 1 2 3 4 5 0 0 0 0 0 0 0 0 0 0.
}

EsHeapFree(array);
```

# Rectangles

## Definitions

```c
struct EsRectangle {
	int32_t l;
	int32_t r;
	int32_t t;
	int32_t b;
};

#define ES_RECT_1(x) ((EsRectangle) { (int32_t) (x), (int32_t) (x), (int32_t) (x), (int32_t) (x) })
#define ES_RECT_1I(x) ((EsRectangle) { (int32_t) (x), (int32_t) -(x), (int32_t) (x), (int32_t) -(x) })
#define ES_RECT_2(x, y) ((EsRectangle) { (int32_t) (x), (int32_t) (x), (int32_t) (y), (int32_t) (y) })
#define ES_RECT_2I(x, y) ((EsRectangle) { (int32_t) (x), (int32_t) -(x), (int32_t) (y), (int32_t) -(y) })
#define ES_RECT_2S(x, y) ((EsRectangle) { 0, (int32_t) (x), 0, (int32_t) (y) })
#define ES_RECT_4(x, y, z, w) ((EsRectangle) { (int32_t) (x), (int32_t) (y), (int32_t) (z), (int32_t) (w) })
#define ES_RECT_4PD(x, y, w, h) ((EsRectangle) { (int32_t) (x), (int32_t) ((x) + (w)), (int32_t) (y), (int32_t) ((y) + (h)) })
#define ES_RECT_WIDTH(_r) ((_r).r - (_r).l)
#define ES_RECT_HEIGHT(_r) ((_r).b - (_r).t)
#define ES_RECT_TOTAL_H(_r) ((_r).r + (_r).l)
#define ES_RECT_TOTAL_V(_r) ((_r).b + (_r).t)
#define ES_RECT_ALL(_r) (_r).l, (_r).r, (_r).t, (_r).b
#define ES_RECT_VALID(_r) (ES_RECT_WIDTH(_r) > 0 && ES_RECT_HEIGHT(_r) > 0)

EsRectangle EsRectangleAdd(EsRectangle a, EsRectangle b);
EsRectangle EsRectangleAddBorder(EsRectangle rectangle, EsRectangle border);
EsRectangle EsRectangleBounding(EsRectangle a, EsRectangle b);
EsRectangle EsRectangleCenter(EsRectangle parent, EsRectangle child);
EsRectangle EsRectangleCut(EsRectangle a, int32_t amount, char side);
EsRectangle EsRectangleFit(EsRectangle parent, EsRectangle child, bool allowScalingUp);
EsRectangle EsRectangleIntersection(EsRectangle a, EsRectangle b);
EsRectangle EsRectangleSplit(EsRectangle *a, int32_t amount, char side, int32_t gap = 0);
EsRectangle EsRectangleSubtract(EsRectangle a, EsRectangle b);
EsRectangle EsRectangleTranslate(EsRectangle a, EsRectangle b);
bool EsRectangleEquals(EsRectangle a, EsRectangle b);
bool EsRectangleContains(EsRectangle a, int32_t x, int32_t y);
```

## Description

`EsRectangle` is used to store an integral rectangular region. `l` gives the offset of the left edge, `r` the right edge, `t` the top edge, and `b` the bottom edge. Note that this means the rectangle does not contain the pixels with x coordinate `r` and y coordinate `b`. The edges are stored are 32-bit signed integers. TODO Diagram.

`EsRectangleAdd` performs a component-wise sum of two rectangles. 

`EsRectangleAddBorder` is similar to `EsRectangleAdd`, but it negates the `r` and `b` fields of `border` before the addition. TODO Diagram.

`EsRectangleSubtract` is similar to `EsRectangleAdd`, but it negates all fields of the second rectangle before the addition. TODO Diagram.

`EsRectangleTranslate` is similar to `EsRectangleAdd`, but it sets `r` to `l` and `b` to `t` in the second rectangle before the addition. TODO Diagram.

`EsRectangleBounding` computes the smallest possible rectangle that contains both parameters. TODO Diagram.

`EsRectangleCenter` centers the `child` rectangle within the `parent` rectangle. The origin of `child` is ignored; only its dimensions matter. TODO Diagram.

`EsRectangleCut` cuts a slice of a rectangle, by moving one of the edges of the input rectangle. The returned rectangle is the slice that was cut off. `side` determines the edge; it is a single character, set to be the same as the name of the field that will be modified. If `amount` is positive, then the edge will be moved inwards by that amount. If `amount` is negative, then the edge will be moved outwards by the absolute value. TODO Diagram.

`EsRectangleSplit` is similar to `EsRectangleCut`, except it modifies the input rectangle so that it contains the modified rectangle after a side is moved. `gap` may be optionally specified to change the distance between the two returned rectangles. TODO Diagram.

`EsRectangleFit` resizes and moves the `child` rectangle, while preserving its aspect ratio, so that it fits in, and is centered in, `parent`. If `allowScalingUp` is set to `false`, the function will never resize `child` to a larger size. TODO Diagram.

`EsRectangleIntersection` computes the intersection of the two parameters. TODO Diagram.

`EsRectangleEquals` returns `true` if the rectangles are identical, otherwise `false`.

`EsRectangleContains` returns `true` if the rectangle contains the specified point, otherwise `false`. Recall, as noted above, if `x` is `r` or larger, the point is considered to not be inside the rectangle, and similarly if `y` is `b` or larger.

`ES_RECT_1` creates a rectangle where all fields are the same value. `ES_RECT_1I` creates a rectangle similarly, except `r` and `b` are negated.

`ES_RECT_2` creates a rectangle where `l` and `r` are the same value, and `t` and `b` are the same value. `ES_RECT_2I` creates a rectangle similarly, except `r` and `b` are negated. `ES_RECT_2S` creates a rectangle with its top-left corner at `(0, 0)`, with the specified width and height.

`ES_RECT_4` creates a rectangle with the specified values of `l`, `r`, `t` and `b`. `ES_RECT_4PD` creates a rectangle with its top-left corner at `(x, y)` and a width of `w` and height of `h`.

`ES_RECT_WIDTH` returns the width of a rectangle. `ES_RECT_HEIGHT` returns the height. `ES_RECT_TOTAL_H` returns the sum of the left and right components. `ES_RECT_TOTAL_V` returns the sum of the top and bottom coponents.

`ES_RECT_ALL` splits a rectangle into its four components, separated by commas.

`ES_RECT_VALID` returns `true` if the rectangle has positive width and height, otherwise `false`.

# Text plans

## Definitions

```c
struct EsTextRun {
	EsTextStyle style;
	uint32_t offset;
};

struct EsTextPlanProperties {
	EsCString cLanguage;
	uint32_t flags; 
	int maxLines;
};

#define ES_TEXT_H_LEFT 	                    (1 << 0)
#define ES_TEXT_H_CENTER                    (1 << 1)
#define ES_TEXT_H_RIGHT 	            (1 << 2)
#define ES_TEXT_V_TOP 	                    (1 << 3)
#define ES_TEXT_V_CENTER                    (1 << 4)
#define ES_TEXT_V_BOTTOM                    (1 << 5)
#define ES_TEXT_ELLIPSIS	            (1 << 6)
#define ES_TEXT_WRAP 	                    (1 << 7)
#define ES_TEXT_PLAN_SINGLE_USE             (1 << 8)
#define ES_TEXT_PLAN_TRIM_SPACES            (1 << 9)
#define ES_TEXT_PLAN_RTL	            (1 << 10)
#define ES_TEXT_PLAN_CLIP_UNBREAKABLE_LINES (1 << 11)

EsTextPlan *EsTextPlanCreate(EsElement *element, EsTextPlanProperties *properties, EsRectangle bounds, const char *string, const EsTextRun *textRuns, size_t textRunCount);

int EsTextPlanGetWidth(EsTextPlan *plan);
int EsTextPlanGetHeight(EsTextPlan *plan);
size_t EsTextPlanGetLineCount(EsTextPlan *plan);

void EsTextPlanDestroy(EsTextPlan *plan);

void EsTextPlanReplaceStyleRenderProperties(EsTextPlan *plan, EsTextStyle *style);
```

## Description

Before you can draw text, you first need to create a *text plan*, which contains all the necessary information to draw the text. The advantage of using text plans is that it enables you to draw the same block of text multiple times without needing the text shaping and layout to be recalculated.

To create a text plan, use `EsTextPlanCreate`. 
- `element`: The user interface element which will use the text plan. The text plan will inherit appropriate display settings from the element, such as the UI scaling factor.
- `properties`: Contains properties to apply while laying out the text. `cLanguage` is the BCP 47 language tag as a string; if `NULL`, the default language is used. `maxLines` contains the maximum number of lines allowed in the layout, after which lines will be clipped; if `0`, the number of lines will be unlimited. `flags` contains any combination of the following constants:
  - `ES_TEXT_H/V_...`: Sets the alignment of the text in the bounds.
  - `ES_TEXT_WRAP`: The text is allowed to wrap when it reaches the end of a line.
  - `ES_TEXT_ELLIPSIS`: If the text is to be truncated, an ellipsis will be inserted.
  - `ES_TEXT_PLAN_SINGLE_USE`: Set to automatically destroy the text plan after the first time it is drawn.
  - `ES_TEXT_PLAN_TRIM_SPACES`: Removes any leading and trailing spaces from each line.
  - `ES_TEXT_PLAN_RTL`: Sets the default text direction to right-to-left.
  - `ES_TEXT_PLAN_CLIP_UNBREAKABLE_LINES`: If a word is to long to be word-wrapped, prevent it being from being wrapped regardless, and clip it instead.
  - `ES_TEXT_PLAN_NO_FONT_SUBSTITUTION`: Prevents font substitution. Otherwise, glyphs missing from the selected font will be drawn in an automatically-selected font that does support them.
- `bounds`: Gives the width and height of the bounds in which the text will be drawn. Only the dimensions of this rectangle matters. It is unused if word wrapping is disabled, and no text alignment flags are specified.
- `string`: Gives the text string, in UTF-8. This string should remain accessible until the text plan is destroyed.
- `textRuns`: An array of text runs, describing the offset and style of each run in the text. The length of each run is automatically calculated as the difference between its offset and the offset of the next run in the array. The first run should have an offset of `0`. The last item in the array should have its `offset` field set to be the total length of the input string, i.e. the end of the last run. The `style` field of the last item is ignored.
- `textRunCount`: The number of text runs in the array. **Note** that the last item in the array should not be included in this count, because it itself does not describe a run, it is only used to indicate the end position of the actual last run.

Once a text plan has been created, it can be drawn with `EsDrawText`. Information about the calculated text layout can be accessed with `EsTextPlanGetWidth` (returns the horizontal size of the layout), `EsTextPlanGetHeight` (returns the vertical size), and `EsTextPlanGetLineCount` (returns the number of lines in the layout, including lines containing wrapped text). The text plan can be destroyed with `EsTextPlanDestroy`.

After a text plan is created, some of the style properties can be replaced. These are called the *render properties*, because they only are used when the text is rendered, and do not affect the layout. These are the `color`, `blur`, `decorations` and `decorationsColor` fields in `EsTextStyle`. They can be replaced using `EsTextPlanReplaceStyleRenderProperties`. Note that this replaces the properties for all text runs in the text plan.

## Example

```c
void DrawElementText(EsElement *element, EsRectangle bounds, const char *string, size_t stringBytes) {
	EsTextRun textRun[2] = {};
	EsElementGetTextStyle(element, &textRun[0].style);
	textRun[0].offset = 0;
	textRun[1].offset = stringBytes;
	EsTextPlanProperties properties = {};
	EsTextPlan *plan = EsTextPlanCreate(element, &properties, bounds, string, textRun, 1);
	EsDrawText(painter, plan, bounds, nullptr, nullptr);
	EsTextPlanDestroy(plan);
}
```

# CRT functions

## Description

The Essence API provides a number of functions from the C standard library which may be used without requiring the POSIX subsystem. See the API header file for an authoritative list of which functions are available. Please note that the functions may not be fully compliant with standards where it would cause unwanted complexity. The functions are prefixed with `EsCRT`; to use the functions without needing this prefix, define `ES_CRT_WITHOUT_PREFIX` before including the API header.
