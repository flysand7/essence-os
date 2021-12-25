# Contributing Guidelines

*This project is constantly evolving, and as such, this file may be out of date.*

See `help/Source Map.md` for a description of the project structure and what each source file is for.

## Code Style

Functions and structures names use `PascalCase`.
Variables use `camelCase`, while constants and macros use `SCREAMING_SNAKE_CASE`.

Tabs are `\t`, and are 8 characters in size.

Braces are placed at the end of the line: 

    if (a > b) {
        ...
    }
    
Blocks are always surrounded by newlines, and always have braces.

    int x = 5;
    
    if (x < 6) {
        x++; // Postfix operators are preferred.
    }
    
Exception: If there are lot of short, linked blocks, then they may be written like this-

    if (width == DIMENSION_PUSH) { bool a = grid->widths[i] == DIMENSION_PUSH; grid->widths[i] = DIMENSION_PUSH; if (!a) pushH++; }
    else if (grid->widths[i] < width && grid->widths[i] != DIMENSION_PUSH) grid->widths[i] = width;
    if (height == DIMENSION_PUSH) { bool a = grid->heights[j] == DIMENSION_PUSH; grid->heights[j] = DIMENSION_PUSH; if (!a) pushV++; }
    else if (grid->heights[j] < height && grid->heights[j] != DIMENSION_PUSH) grid->heights[j] = height;

Function names are always descriptive, and use prepositions and conjuctions if neccesary. 

    EsFileReadAll // Symbols provided by the API are prefixed with Es-.
    EsDrawSurface
    EsMemoryZero
    
Variable names are usually descriptive, but sometimes shortened names are used for short-lived variables.

    EsMessage m = {};
    m.type = OS_MESSAGE_MEASURE;
	EsMessagePost(&m);

Operators are padded with spaces on either side.

    bounds.left = (grid->bounds.left + grid->bounds.right) / 2 - 4;
    
A space should be placed between a cast and its expression.

    int x = (float) y;

Although the operating system is written in C++, most C++ features are avoided.
However, the kernel uses a lot of member functions.

    struct Window {
        void Update(bool fromUser);
        void SetCursorStyle(OSCursorStyle style);
        void NeedWMTimer(int hz);
        void Destroy();
        bool Move(OSRectangle &newBounds);
        void ClearImage();

        Mutex mutex; // Mutex for drawing to the window. Also needed when moving the window.
        Surface *surface;
        OSPoint position;
        size_t width, height;
        ...
    }
    
Default arguments often provided as functions grow over time.

There is no limit on function size. However, you should avoid regularly exceeding 120 columns.

Pointers are declared like this: `Type *name;`.

Identifiers may be prefixed with `i`, `e` or `c` to indicate inclusive, exclusive or C-style-zero-terminated-string respectively.

## Kernel and Driver Development

See `module.h` for definitions available to driver developers. See `drivers/fat.cpp` and `drivers/ata.cpp` for simple examples of the driver API.

The following subroutines may be of interest:

    void KWaitMicroseconds(uint64_t mcs); // Spin until a given number of microseconds have elapsed.
    void EsPrint(const char *format, ...); // Print a message to serial output. (Ctrl+Alt+3 in Qemu)
    void KernelPanic(const char *format, ...); // Print a message and halt the OS.
    Defer(<statement>); // Defer a statement. Deferred statements will be executed in reverse order when they go out of scope.
    size_t EsCStringLength(const char *string); // Get the length of a zero-terminated string.
    void EsMemoryCopy(void *destination, const void *source, size_t bytes); // Copy memory forwards.
    void EsMemoryZero(void *destination, size_t bytes); // Zero a buffer.
    void EsMemoryMove(void *start, void *end, intptr_t amount, bool zeroEmptySpace); // Move a memory region left (amount < 0) or right (amount > 0).
    int EsMemoryCopy(const void *a, const void *b, size_t bytes); // Compare two memory regions. Returns 0 if equal.
    uint8_t EsSumBytes(uint8_t *source, size_t bytes); // Calculate the 8-bit sum of the bytes in a buffer.
    size_t EsStringFormat(char *buffer, size_t bufferLength, const char *format, ...); // Format a string. Returns the length.
    uint8_t EsGetRandomByte(); // Get a non-secure random byte.
    void EsSort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *, void *), void *callbackArgument); // Sort an array of count items of size size.
    uint32_t CalculateCRC32(void *buffer, size_t length); // Calculate the CRC32 checksum of a buffer.
    void ProcessorEnableInterrupts(); // Enable interrupts.
    void ProcessorDisableInterrupts(); // Disable interrupts. Critical interrupts, such as TLB shootdown IPIs, will remain enabled.
    void ProcessorOut<x>(uint16_t port, uint<x>_t value); // Write to an IO port.
    uint<x>_t ProcessorIn<x>(uint16_t port); // Read from an IO port.
    uint64_t ProcessorReadTimeStamp(); // Read the time stamp in ticks. acpi.timestampTicksPerMs gives the number of ticks per millisecond.
    bool KRegisterIRQ(uintptr_t interruptIndex, IRQHandler handler, void *context, const char *cOwner); // Register an IRQ handler. Returns false if the IRQ could not be registered. The handler should return false if its devices was not responsible for the IRQ.
    void *MMMapPhysical(MMSpace *space /* = kernelMMSpace */, uintptr_t offset, size_t bytes); // Memory mapped IO.
    void *EsHeapAllocate(size_t size, bool zero); // Allocate memory from the heap.
    void EsHeapFree(void *pointer); // Free memory from the heap.
    bool KThreadCreate(const char *cName, void (*startAddress)(uintptr_t), uintptr_t argument = 0); // Create a thread.

Synchronisation:

    void Mutex::Acquire();
    void Mutex::Release();
    void Mutex::AssertLocked();

    void Spinlock::Acquire(); // Disables interrupts.
    void Spinlock::Release();
    void Spinlock::AssertLocked();

    bool Event::Set();
    void Event::Reset();
    bool Event::Pool();
    bool Event::Wait(uintptr_t timeoutMs); // Return false on timeout.
    // event.autoReset determines whether the event will automatically reset after Poll() or Wait() return.

Linked lists:

    LinkedList<T>::InsertStart(LinkedItem<T> *item); // Insert an item at the start of a linked list.
    LinkedList<T>::InsertEnd(LinkedItem<T> *item); // Insert an item at the end of a linked list.
    LinkedList<T>::Remove(LinkedItem<T> *item); // Remove an item from a linked list.
    
    struct LinkedList<T> {
        LinkedItem<T> *firstItem; // The start of the linked list.
        LinkedItem<T> *lastItem; // The end of the linked list.
        size_t count; // The number of items in the linked list.
    }

    struct LinkedItem<T> {
        LinkedItem<T> *previousItem; // The previous item in the linked list.
        LinkedItem<T> *nextItem; // The next item in the linked list.
        LinkedList<T> *list; // The list the item is in.
	T *thisItem; // A pointer to the item itself.
    }

## Contributors

Put your name here!

- nakst
- Brett R. Toomey
- vtlmks
- Aleksander Birkeland

