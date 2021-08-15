#ifndef IMPLEMENTATION

struct POSIXFile {
	uint64_t posixFlags; // The flags given in SYS_open.
	volatile size_t handles;
	char *path; // Resolved path.
	EsFileOffset offsetIntoFile;
	uint64_t openFlags; // The flags used to open the object.
	KMutex mutex;
	KNode *node;
	Pipe *pipe;
	void *directoryBuffer;
	size_t directoryBufferLength;

#define POSIX_FILE_TERMINAL (1)
#define POSIX_FILE_NORMAL (2)
#define POSIX_FILE_PIPE (3)
#define POSIX_FILE_ZERO (4)
#define POSIX_FILE_NULL (5)
#define POSIX_FILE_DIRECTORY (6)
	int type;
};

namespace POSIX { 
	uintptr_t DoSyscall(_EsPOSIXSyscall syscall, uintptr_t *userStackPointer); 
	KMutex forkMutex;
	KMutex threadPOSIXDataMutex;
}

struct POSIXThread {
	void *forkStack; 
	size_t forkStackSize;
	uintptr_t forkUSP;
	Process *forkProcess;
};

// TODO Use SYSCALL_READ and SYSCALL_WRITE.
#define SYSCALL_BUFFER_POSIX(address, length, index, write) \
	MMRegion *_region ## index = MMFindAndPinRegion(currentVMM, (address), (length)); \
	if (!_region ## index && !fromKernel) { KernelLog(LOG_ERROR, "POSIX", "EFAULT", "POSIX application EFAULT at %x.\n", address); return -EFAULT; } \
	EsDefer(if (_region ## index) MMUnpinRegion(currentVMM, _region ## index)); \
	if (write && (_region ## index->flags & MM_REGION_READ_ONLY) && (~_region ## index->flags & MM_REGION_COPY_ON_WRITE)) \
		{ KernelLog(LOG_ERROR, "POSIX", "EFAULT", "POSIX application EFAULT (2) at %x.\n", address); return -EFAULT; }
#define SYSCALL_BUFFER_ALLOW_NULL_POSIX(address, length, index) \
	MMRegion *_region ## index = MMFindAndPinRegion(currentVMM, (address), (length)); \
	EsDefer(if (_region ## index) MMUnpinRegion(currentVMM, _region ## index));

#define SYSCALL_HANDLE_POSIX(handle, __object, index) \
	KObject _object ## index(handleTable, ConvertStandardInputTo3(handle), KERNEL_OBJECT_POSIX_FD); \
	*((void **) &__object) = _object ## index .object; \
	if (! _object ## index .valid) return -EBADF; else _object ## index .checked = true; \

#endif

#ifdef IMPLEMENTATION

#define _POSIX_SOURCE
#define _GNU_SOURCE

namespace POSIX {

#include <bits/syscall.h>
#include <bits/alltypes.h>
#include <bits/fcntl.h>
#include <bits/ioctl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <fcntl.h>
#include <errno.h>

	EsHandle ConvertStandardInputTo3(EsHandle handle) {
		if (handle) {
			return handle;
		} else {
			return 3;
		}
	}

	intptr_t Read(POSIXFile *file, K_USER_BUFFER void *base, size_t length, bool baseMappedToFile) {
		if (!length) return 0;

		// EsPrint("Read %d bytes from %x into %x\n", length, file, base);

		if (file->type == POSIX_FILE_TERMINAL) {
			return 0;
		} else if (file->type == POSIX_FILE_PIPE) {
			return file->pipe->Access(base, length, false, true);
		} else if (file->type == POSIX_FILE_NORMAL) {
			// EsPrint("READ '%z' %d/%d\n", file->path, file->offsetIntoFile, length);
			KMutexAcquire(&file->mutex);
			EsDefer(KMutexRelease(&file->mutex));
			size_t count = FSFileReadSync(file->node, (void *) base, file->offsetIntoFile, length, baseMappedToFile ? FS_FILE_ACCESS_USER_BUFFER_MAPPED : 0);
			if (ES_CHECK_ERROR(count)) return -EIO;
			else { file->offsetIntoFile += count; return count; }
		} else if (file->type == POSIX_FILE_ZERO) {
			EsMemoryZero(base, length);
			return length;
		} else if (file->type == POSIX_FILE_NULL) {
			return 0;
		} else if (file->type == POSIX_FILE_DIRECTORY) {
			return 0;
		}

		return -EACCES;
	}

	intptr_t Write(POSIXFile *file, K_USER_BUFFER void *base, size_t length, bool baseMappedToFile) {
		if (file->posixFlags & O_APPEND) {
			// TODO Make this atomic.
			file->offsetIntoFile = file->node->directoryEntry->totalSize;
		}

		if (!length) return 0;

		if (file->type == POSIX_FILE_TERMINAL) {
			EsPrint("%s", length, base);
			return length;
		} else if (file->type == POSIX_FILE_PIPE) {
			// EsPrint("Write %d bytes to pipe %x...\n", length, file->pipe);
			return file->pipe->Access(base, length, true, true);
		} else if (file->type == POSIX_FILE_NORMAL) {
			// EsPrint("WRITE '%z' %d/%d\n", file->path, file->offsetIntoFile, length);
			KMutexAcquire(&file->mutex);
			EsDefer(KMutexRelease(&file->mutex));
			size_t count = FSFileWriteSync(file->node, (void *) base, file->offsetIntoFile, length, 
					(baseMappedToFile ? FS_FILE_ACCESS_USER_BUFFER_MAPPED : 0));
			if (ES_CHECK_ERROR(count)) return -EIO;
			else { file->offsetIntoFile += count; return count; }
		} else if (file->type == POSIX_FILE_ZERO || file->type == POSIX_FILE_NULL) {
			return length;
		} else if (file->type == POSIX_FILE_DIRECTORY) {
			return length;
		}

		return -EACCES;
	}

	void Stat(int type, KNode *node, struct stat *buffer) {
		EsMemoryZero(buffer, sizeof(struct stat));

		if (type == POSIX_FILE_NORMAL) {
			buffer->st_ino = node->id;
			buffer->st_mode = S_IFREG;
			buffer->st_nlink = 1;

			if (node->directoryEntry->type == ES_NODE_FILE) {
				buffer->st_size = node->directoryEntry->totalSize;
				buffer->st_blksize = 512;
				buffer->st_blocks = buffer->st_size / 512;
			}
		} else if (type == POSIX_FILE_DIRECTORY) {
			buffer->st_ino = node->id;
			buffer->st_mode = S_IFDIR;
			buffer->st_nlink = 1;
		} else if (type == POSIX_FILE_PIPE) {
			buffer->st_mode = S_IFIFO;
		} else {
			buffer->st_mode = S_IFCHR;
		}
	}

	void CloneFileDescriptorTable(Process *forkProcess, HandleTable *handleTable) {
		HandleTable *source = handleTable,
			    *destination = &forkProcess->handleTable;
		KMutexAcquire(&source->lock);
		EsDefer(KMutexRelease(&source->lock));
		HandleTableL1 *l1 = &source->l1r;

		for (uintptr_t i = 0; i < HANDLE_TABLE_L1_ENTRIES; i++) {
			if (!l1->u[i]) continue;
			HandleTableL2 *l2 = l1->t[i];

			for (uintptr_t k = 0; k < HANDLE_TABLE_L2_ENTRIES; k++) {
				Handle *handle = l2->t + k;
				if (!handle->object) continue;
				if (handle->type != KERNEL_OBJECT_POSIX_FD) continue;
				if (handle->flags & FD_CLOEXEC) continue;

				POSIXFile *file = (POSIXFile *) handle->object;
				OpenHandleToObject(file, KERNEL_OBJECT_POSIX_FD, handle->flags);
				destination->OpenHandle(handle->object, handle->flags, handle->type, k + i * HANDLE_TABLE_L2_ENTRIES);
			}
		}
	}

	uintptr_t DoSyscall(_EsPOSIXSyscall syscall, uintptr_t *userStackPointer) {
		Thread *currentThread = GetCurrentThread();
		Process *currentProcess = currentThread->process;
		MMSpace *currentVMM = currentProcess->vmm;
		HandleTable *handleTable = &currentProcess->handleTable;
		bool fromKernel = false;

		if (!currentThread->posixData) {
			KMutexAcquire(&threadPOSIXDataMutex);
			if (!currentThread->posixData) currentThread->posixData = (POSIXThread *) EsHeapAllocate(sizeof(POSIXThread), true, K_FIXED);
			KMutexRelease(&threadPOSIXDataMutex);
		} 

		if (currentThread->posixData->forkProcess) {
			handleTable = &currentThread->posixData->forkProcess->handleTable;
		}

		// EsPrint("%x, %x, %x, %x, %x, %x, %x, %x\n", syscall.index, syscall.arguments[0], syscall.arguments[1], syscall.arguments[2],
		// 		syscall.arguments[3], syscall.arguments[4], syscall.arguments[5], syscall.arguments[6]);

		switch (syscall.index) {
			case SYS_open: {
				if (syscall.arguments[6] > SYSCALL_BUFFER_LIMIT) return -ENOMEM;
				SYSCALL_BUFFER_POSIX(syscall.arguments[0], syscall.arguments[6], 1, false);
				char *path2 = (char *) syscall.arguments[0];
				size_t pathLength = syscall.arguments[6];
				char *path = (char *) EsHeapAllocate(pathLength, false, K_FIXED);
				if (!path) return -ENOMEM;
				EsMemoryCopy(path, path2, pathLength);
				EsDefer(EsHeapFree(path, 0, K_FIXED));
				int flags = syscall.arguments[1];
				uint64_t openFlags = ES_FLAGS_DEFAULT;

				POSIXFile *file = (POSIXFile *) EsHeapAllocate(sizeof(POSIXFile), true, K_FIXED);
				if (!file) return -ENOMEM;
				file->posixFlags = flags;
				file->handles = 1;

				const char *devZero = "/dev/zero";
				const char *devNull = "/dev/null";

				// EsPrint("Open: %s, %x\n", pathLength, path, flags);

				if ((EsCStringLength(devZero) == pathLength && 0 == EsMemoryCompare(path, devZero, pathLength))) {
					file->type = POSIX_FILE_ZERO;
				} else if (EsCStringLength(devNull) == pathLength && 0 == EsMemoryCompare(path, devNull, pathLength)) {
					file->type = POSIX_FILE_NULL;
				} else {
					if (flags & O_EXCL) openFlags |= ES_NODE_FAIL_IF_FOUND;
					else if (flags & O_CREAT) {}
					else openFlags |= ES_NODE_FAIL_IF_NOT_FOUND;
					if (flags & O_DIRECTORY) openFlags |= ES_NODE_DIRECTORY;
					if (flags & O_APPEND) openFlags |= ES_FILE_WRITE;
					else if (flags & O_RDWR) openFlags |= ES_FILE_WRITE;
					else if (flags & O_WRONLY) openFlags |= ES_FILE_WRITE;
					else if (!(flags & O_PATH)) openFlags |= ES_FILE_READ;

					KNodeInformation information = FSNodeOpen(path, pathLength, openFlags, (KNode *) syscall.arguments[4]);

					if (!information.node) {
						EsHeapFree(file, sizeof(POSIXFile), K_FIXED);
						return (information.error == ES_ERROR_FILE_DOES_NOT_EXIST || information.error == ES_ERROR_PATH_NOT_TRAVERSABLE) ? -ENOENT : -EACCES;
					}

					if (information.node->directoryEntry->type == ES_NODE_DIRECTORY && !(flags & O_DIRECTORY) && !(flags & O_PATH)) {
						EsHeapFree(file, sizeof(POSIXFile), K_FIXED);
						CloseHandleToObject(information.node, KERNEL_OBJECT_NODE, openFlags);
						return -EISDIR;
					}

					file->node = information.node;
					file->openFlags = openFlags;
					file->type = file->node->directoryEntry->type == ES_NODE_DIRECTORY ? POSIX_FILE_DIRECTORY : POSIX_FILE_NORMAL;
				}

				file->path = (char *) EsHeapAllocate(pathLength + 1, true, K_FIXED);
				EsMemoryCopy(file->path, path, pathLength);

				if ((flags & O_TRUNC) && file->type == POSIX_FILE_NORMAL) {
					FSFileResize(file->node, 0);
				}

				// EsPrint("OPEN '%s' %z\n", pathLength, path, (openFlags & ES_NODE_WRITE_ACCESS) ? "Write" : ((openFlags & ES_NODE_READ_ACCESS) ? "Read" : "None"));

				return handleTable->OpenHandle(file, (flags & O_CLOEXEC) ? FD_CLOEXEC : 0, KERNEL_OBJECT_POSIX_FD) ?: -ENFILE;
			} break;

			case ES_POSIX_SYSCALL_GET_POSIX_FD_PATH: {
				if (syscall.arguments[2] > SYSCALL_BUFFER_LIMIT) return -ENOMEM;
				POSIXFile *file;
				SYSCALL_HANDLE_POSIX(syscall.arguments[0], file, 1);
				SYSCALL_BUFFER_POSIX(syscall.arguments[1], syscall.arguments[2], 2, true);
				KMutexAcquire(&file->mutex);
				EsDefer(KMutexRelease(&file->mutex));
				int length = EsCStringLength(file->path);
				if (length > syscall.arguments[2]) length = syscall.arguments[2];
				EsMemoryZero((void *) syscall.arguments[1], syscall.arguments[2]);
				EsMemoryCopy((void *) syscall.arguments[1], file->path, length);
				return length;
			} break;

			case SYS_close: {
				if (!handleTable->CloseHandle(ConvertStandardInputTo3(syscall.arguments[0]))) {
					return -EBADF;
				}

				return 0;
			} break;

			case SYS_fstat: {
				POSIXFile *file;
				SYSCALL_HANDLE_POSIX(syscall.arguments[0], file, 1);
				SYSCALL_BUFFER_POSIX(syscall.arguments[1], sizeof(struct stat), 1, true);
				struct stat temp;
				KMutexAcquire(&file->mutex);
				Stat(file->type, file->node, &temp);
				KMutexRelease(&file->mutex);
				EsMemoryCopy((struct stat *) syscall.arguments[1], &temp, sizeof(struct stat));
				return 0;
			} break;

			case SYS_fcntl: {
				POSIXFile *file;
				SYSCALL_HANDLE_POSIX(syscall.arguments[0], file, 1);

				if (syscall.arguments[1] == F_GETFD) {
					return _object1.flags;
				} else if (syscall.arguments[1] == F_SETFD) {
					KObject object;
					uint32_t newFlags = syscall.arguments[2];
					handleTable->ModifyFlags(syscall.arguments[0], newFlags);
				} else if (syscall.arguments[1] == F_GETFL) {
					return file->posixFlags;
				} else if (syscall.arguments[1] == F_DUPFD) {
					// Duplicate with FD_CLOEXEC clear.
					OpenHandleToObject(file, KERNEL_OBJECT_POSIX_FD, 0);
					return handleTable->OpenHandle(_object1.object, 0, _object1.type) ?: -ENFILE;
				} else if (syscall.arguments[1] == F_DUPFD_CLOEXEC) {
					// Duplicate with FD_CLOEXEC set.
					OpenHandleToObject(file, KERNEL_OBJECT_POSIX_FD, FD_CLOEXEC);
					return handleTable->OpenHandle(_object1.object, FD_CLOEXEC, _object1.type) ?: -ENFILE;
				} else {
					KernelPanic("POSIX::DoSyscall - Unimplemented fcntl %d.\n", syscall.arguments[1]);
				}
			} break;

			case SYS_lseek: {
				POSIXFile *file;
				SYSCALL_HANDLE_POSIX(syscall.arguments[0], file, 1);

				KMutexAcquire(&file->mutex);
				EsDefer(KMutexRelease(&file->mutex));

				if (syscall.arguments[2] == SEEK_SET) {
					file->offsetIntoFile = syscall.arguments[1];
				} else if (syscall.arguments[2] == SEEK_CUR) {
					file->offsetIntoFile += syscall.arguments[1];
				} else if (syscall.arguments[2] == SEEK_END && file->type == POSIX_FILE_NORMAL) {
					file->offsetIntoFile = file->node->directoryEntry->totalSize + syscall.arguments[1];
				} else {
					return -EINVAL;
				}

				return file->offsetIntoFile;
			} break;

			case SYS_ioctl: {
				POSIXFile *file;
				SYSCALL_HANDLE_POSIX(syscall.arguments[0], file, 1);

				KMutexAcquire(&file->mutex);
				EsDefer(KMutexRelease(&file->mutex));

				if (syscall.arguments[1] == TIOCGWINSZ) {
					if (file->type == POSIX_FILE_TERMINAL || file->type == POSIX_FILE_PIPE) {
						SYSCALL_BUFFER_POSIX(syscall.arguments[1], sizeof(struct winsize), 1, true);
						struct winsize *size = (struct winsize *) syscall.arguments[2];
						size->ws_row = 80;
						size->ws_col = 25;
						size->ws_xpixel = 800;
						size->ws_ypixel = 800;
						return 0;
					} else {
						return -EINVAL;
					}
				}
			} break;

			case SYS_read: {
				POSIXFile *file;
				SYSCALL_HANDLE_POSIX(syscall.arguments[0], file, 1);
				SYSCALL_BUFFER_POSIX(syscall.arguments[1], syscall.arguments[2], 3, false);
				return Read(file, (void *) syscall.arguments[1], syscall.arguments[2], _region3->flags & MM_REGION_FILE);
			} break;

			case SYS_readv: {
				POSIXFile *file;
				if (syscall.arguments[2] > 1024) return -EINVAL;
				SYSCALL_HANDLE_POSIX(syscall.arguments[0], file, 1);
				SYSCALL_BUFFER_POSIX(syscall.arguments[1], syscall.arguments[2] * sizeof(struct iovec *), 2, false);

				struct iovec *vectors = (struct iovec *) EsHeapAllocate(syscall.arguments[2] * sizeof(struct iovec), false, K_FIXED);
				if (!vectors) return -ENOMEM;
				EsMemoryCopy(vectors, (void *) syscall.arguments[1], syscall.arguments[2] * sizeof(struct iovec));
				EsDefer(EsHeapFree(vectors, syscall.arguments[2] * sizeof(struct iovec), K_FIXED));

				size_t bytesRead = 0;

				for (uintptr_t i = 0; i < (uintptr_t) syscall.arguments[2]; i++) {
					if (!vectors[i].iov_len) continue;
					SYSCALL_BUFFER_POSIX((uintptr_t) vectors[i].iov_base, vectors[i].iov_len, 3, false);
					intptr_t result = Read(file, vectors[i].iov_base, vectors[i].iov_len, _region3->flags & MM_REGION_FILE);
					if (result < 0) return result; 
					bytesRead += result;
				}

				return bytesRead;
			} break;

			case SYS_write: {
				POSIXFile *file;
				SYSCALL_HANDLE_POSIX(syscall.arguments[0], file, 1);
				SYSCALL_BUFFER_POSIX(syscall.arguments[1], syscall.arguments[2], 3, true);

				if (file->type == POSIX_FILE_NORMAL && !(file->openFlags & (ES_FILE_WRITE | ES_FILE_WRITE_EXCLUSIVE))) {
					return -EACCES;
				}

				return Write(file, (void *) syscall.arguments[1], syscall.arguments[2], _region3->flags & MM_REGION_FILE);
			} break;

			case SYS_writev: {
				POSIXFile *file;
				if (syscall.arguments[2] > 1024) return -EINVAL;
				SYSCALL_HANDLE_POSIX(syscall.arguments[0], file, 1);
				SYSCALL_BUFFER_POSIX(syscall.arguments[1], syscall.arguments[2] * sizeof(struct iovec *), 2, false);

				struct iovec *vectors = (struct iovec *) EsHeapAllocate(syscall.arguments[2] * sizeof(struct iovec), false, K_FIXED);
				if (!vectors) return -ENOMEM;
				EsMemoryCopy(vectors, (void *) syscall.arguments[1], syscall.arguments[2] * sizeof(struct iovec));
				EsDefer(EsHeapFree(vectors, syscall.arguments[2] * sizeof(struct iovec), K_FIXED));

				size_t bytesWritten = 0;

				if (file->type == POSIX_FILE_NORMAL && !(file->openFlags & (ES_FILE_WRITE | ES_FILE_WRITE_EXCLUSIVE))) {
					return -EACCES;
				}

				for (uintptr_t i = 0; i < (uintptr_t) syscall.arguments[2]; i++) {
					if (!vectors[i].iov_len) continue;
					// EsPrint("writev %d: %x/%d\n", i, vectors[i].iov_base, vectors[i].iov_len);
					SYSCALL_BUFFER_POSIX((uintptr_t) vectors[i].iov_base, vectors[i].iov_len, 3, true);
					intptr_t result = Write(file, vectors[i].iov_base, vectors[i].iov_len, _region3->flags & MM_REGION_FILE);
					if (result < 0) return result; 
					bytesWritten += result;
				}

				return bytesWritten;
			} break;

			case SYS_vfork: {
				// To vfork: save the stack and return 0.
				// To exec*: create the new process, restore the state of our stack, then return the new process's ID.

				KMutexAcquire(&forkMutex);
				EsDefer(KMutexRelease(&forkMutex));

				// Did we complete the last vfork?
				if (currentThread->posixData->forkStack) return -ENOMEM;

				// EsPrint("vfork->\n");

				// Allocate the process.
				Process *forkProcess = currentThread->posixData->forkProcess = scheduler.SpawnProcess();
				forkProcess->pgid = currentProcess->pgid;

				// Clone our FDs.
				CloneFileDescriptorTable(forkProcess, handleTable);

				// Save the state of the user's stack.
				currentThread->posixData->forkStackSize = currentThread->userStackCommit;
				currentThread->posixData->forkStack = (void *) EsHeapAllocate(currentThread->posixData->forkStackSize, false, K_PAGED);
#ifdef K_STACK_GROWS_DOWN
				EsMemoryCopy(currentThread->posixData->forkStack, 
						(void *) (currentThread->userStackBase + currentThread->userStackReserve - currentThread->posixData->forkStackSize), 
						currentThread->posixData->forkStackSize);
#else
				EsMemoryCopy(currentThread->posixData->forkStack, (void *) currentThread->userStackBase, currentThread->posixData->forkStackSize);
#endif
				currentThread->posixData->forkUSP = *userStackPointer;

				// Return 0.
				return 0;
			} break;

			case SYS_execve: {
				KMutexAcquire(&forkMutex);
				EsDefer(KMutexRelease(&forkMutex));

				// Are we vforking?
				if (!currentThread->posixData->forkStack) return -ENOMEM;
				if (syscall.arguments[1] > K_MAX_PATH) return -ENOMEM;
				if (syscall.arguments[3] > SYSCALL_BUFFER_LIMIT) return -ENOMEM;

				// EsPrint("<-execve\n");

				SYSCALL_BUFFER_POSIX(syscall.arguments[0], syscall.arguments[1], 1, false);
				SYSCALL_BUFFER_POSIX(syscall.arguments[2], syscall.arguments[3], 2, false);

				// Setup the process object.

				char *path = (char *) EsHeapAllocate(syscall.arguments[1], false, K_FIXED);
				if (!path) return -ENOMEM;
				EsMemoryCopy(path, (void *) syscall.arguments[0], syscall.arguments[1]);

				Process *process = currentThread->posixData->forkProcess;
				process->creationArguments[CREATION_ARGUMENT_ENVIRONMENT] = MakeConstantBuffer((void *) syscall.arguments[2], syscall.arguments[3], process);
				process->posixForking = true;
				process->permissions = currentProcess->permissions;

				EsMountPoint mountPoint = {};
				OpenHandleToObject((void *) syscall.arguments[4], KERNEL_OBJECT_NODE, _ES_NODE_DIRECTORY_WRITE);
				mountPoint.base = process->handleTable.OpenHandle((void *) syscall.arguments[4], _ES_NODE_DIRECTORY_WRITE, KERNEL_OBJECT_NODE);
				mountPoint.prefixBytes = EsStringFormat(mountPoint.prefix, sizeof(mountPoint.prefix), "|POSIX:");
				process->creationArguments[CREATION_ARGUMENT_INITIAL_MOUNT_POINTS] = MakeConstantBuffer(&mountPoint, sizeof(EsMountPoint), process);

				// Start the process.

				if (!process->Start(path, syscall.arguments[1])) {
					EsHeapFree(path, 0, K_FIXED);
					return -ENOMEM;
				}

				KSpinlockAcquire(&scheduler.lock);

				process->posixForking = false;

				if (process->allThreadsTerminated) {
					KEventSet(&process->killedEvent, true);
				}

				KSpinlockRelease(&scheduler.lock);

				EsHeapFree(path, 0, K_FIXED);
				CloseHandleToObject(process->executableMainThread, KERNEL_OBJECT_THREAD);

				// Restore the state of our stack.
#ifdef K_STACK_GROWS_DOWN
				EsMemoryCopy((void *) (currentThread->userStackBase + currentThread->userStackReserve - currentThread->posixData->forkStackSize),
						currentThread->posixData->forkStack, currentThread->posixData->forkStackSize);
#else
				EsMemoryCopy((void *) currentThread->userStackBase, currentThread->posixData->forkStack, currentThread->posixData->forkStackSize);
#endif
				EsHeapFree(currentThread->posixData->forkStack, currentThread->posixData->forkStackSize, K_PAGED);
				*userStackPointer = currentThread->posixData->forkUSP;

				// Fork complete.
				currentThread->posixData->forkProcess = nullptr;
				currentThread->posixData->forkStack = nullptr;
				currentThread->posixData->forkUSP = 0;

				if (!process) return -ENOMEM;

				return currentProcess->handleTable.OpenHandle(process, 0, KERNEL_OBJECT_PROCESS);
			} break;

			case SYS_exit_group: {
				EsHandle processHandle = 0;

				KMutexAcquire(&forkMutex);

				// Are we vforking?
				if (currentThread->posixData->forkStack) {
					// Restore the state of our stack.
#ifdef K_STACK_GROWS_DOWN
					EsMemoryCopy((void *) (currentThread->userStackBase + currentThread->userStackReserve - currentThread->posixData->forkStackSize),
							currentThread->posixData->forkStack, currentThread->posixData->forkStackSize);
#else
					EsMemoryCopy((void *) currentThread->userStackBase, currentThread->posixData->forkStack, currentThread->posixData->forkStackSize);
#endif
					EsHeapFree(currentThread->posixData->forkStack, currentThread->posixData->forkStackSize, K_PAGED);
					*userStackPointer = currentThread->posixData->forkUSP;

					// Set the exit status.
					Process *process = currentThread->posixData->forkProcess;
					process->exitStatus = syscall.arguments[0];
					process->allThreadsTerminated = true; // Set in case execve() was not attempted.
					KEventSet(&process->killedEvent);

					processHandle = currentProcess->handleTable.OpenHandle(currentThread->posixData->forkProcess, 0, KERNEL_OBJECT_PROCESS);

					// Close any handles the process owned.
					process->handleTable.Destroy();

					// Cancel the vfork.
					currentThread->posixData->forkProcess = nullptr;
					currentThread->posixData->forkStack = nullptr;
					currentThread->posixData->forkUSP = 0;
				}

				KMutexRelease(&forkMutex);

				if (processHandle) {
					return processHandle;
				} else {
					scheduler.TerminateProcess(currentProcess, syscall.arguments[0]);
				}
			} break;

			case SYS_sysinfo: {
				SYSCALL_BUFFER_POSIX(syscall.arguments[0], sizeof(struct sysinfo), 1, true);
				struct sysinfo *buffer = (struct sysinfo *) syscall.arguments[0];
				EsMemoryZero(buffer, sizeof(struct sysinfo));

				// TODO Incomplete.
				buffer->totalram = K_PAGE_SIZE * pmm.commitFixedLimit;
				buffer->freeram = K_PAGE_SIZE * (pmm.countZeroedPages + pmm.countFreePages);
				buffer->procs = scheduler.allProcesses.count;
				buffer->mem_unit = 1;

				return 0;
			} break;

			case SYS_dup2: {
				POSIXFile *file;
				SYSCALL_HANDLE_POSIX(syscall.arguments[0], file, 1);

				// Try to close the newfd.

				handleTable->CloseHandle(ConvertStandardInputTo3(syscall.arguments[1]));

				// Clone the oldfd as newfd.

				OpenHandleToObject(file, KERNEL_OBJECT_POSIX_FD, _object1.flags);
				return handleTable->OpenHandle(_object1.object, _object1.flags, _object1.type, 
						ConvertStandardInputTo3(syscall.arguments[1])) ? 0 : -EBUSY;
			} break;

			case SYS_pipe2: {
				if (syscall.arguments[1] & ~O_CLOEXEC) {
					return -EINVAL;
				}

				SYSCALL_BUFFER_POSIX(syscall.arguments[0], sizeof(int) * 2, 1, true);
				int *fildes = (int *) syscall.arguments[0];

				Pipe *pipe = (Pipe *) EsHeapAllocate(sizeof(Pipe), true, K_PAGED);
				POSIXFile *reader = (POSIXFile *) EsHeapAllocate(sizeof(POSIXFile), true, K_FIXED);
				POSIXFile *writer = (POSIXFile *) EsHeapAllocate(sizeof(POSIXFile), true, K_FIXED);

				if (!reader || !writer || !pipe) {
					EsHeapFree(pipe, 0, K_PAGED);
					EsHeapFree(reader, 0, K_FIXED);
					EsHeapFree(writer, 0, K_FIXED);
					return -ENOMEM;
				}

				KEventSet(&pipe->canWrite);

				reader->type = POSIX_FILE_PIPE;
				reader->openFlags = PIPE_READER;
				reader->handles = 1;
				reader->pipe = pipe;

				writer->type = POSIX_FILE_PIPE;
				writer->openFlags = PIPE_WRITER;
				writer->handles = 1;
				writer->pipe = pipe;

				pipe->readers = 1;
				pipe->writers = 1;

				fildes[0] = handleTable->OpenHandle(reader, (syscall.arguments[1] & O_CLOEXEC) ? FD_CLOEXEC : 0, KERNEL_OBJECT_POSIX_FD);
				fildes[1] = handleTable->OpenHandle(writer, (syscall.arguments[1] & O_CLOEXEC) ? FD_CLOEXEC : 0, KERNEL_OBJECT_POSIX_FD);

				return 0;
			} break;

			case SYS_getdents64: {
				// TODO This is a bit of a hack.
				// 	Especially allocating the entire directory list in the kernel's memory space.

				if (syscall.arguments[2] > SYSCALL_BUFFER_LIMIT) return -ENOMEM;

				POSIXFile *file;
				SYSCALL_HANDLE_POSIX(syscall.arguments[0], file, 1);
				SYSCALL_BUFFER_POSIX(syscall.arguments[1], syscall.arguments[2], 3, true);

				KMutexAcquire(&file->mutex);
				EsDefer(KMutexRelease(&file->mutex));

				if (file->type != POSIX_FILE_DIRECTORY) {
					return -EACCES;
				}

				if (!file->offsetIntoFile || !file->directoryBuffer) {
					EsHeapFree(file->directoryBuffer, file->directoryBufferLength, K_PAGED);

					file->directoryBuffer = nullptr;
					file->offsetIntoFile = 0;
					file->directoryBufferLength = 0;

					uintptr_t bufferSize = file->node->directoryEntry->directoryChildren;
					EsDirectoryChild *buffer = (EsDirectoryChild *) EsHeapAllocate(sizeof(EsDirectoryChild) * bufferSize, false, K_FIXED);

					if (!buffer) {
						return -ENOMEM;
					}

					size_t count = FSDirectoryEnumerateChildren(file->node, buffer, bufferSize);

					if (ES_CHECK_ERROR(count)) {
						EsHeapFree(buffer, sizeof(EsDirectoryChild) * bufferSize, K_FIXED);
						return -EIO;
					}

					size_t neededSize = 0;

					for (uintptr_t i = 0; i < count; i++) {
						neededSize += RoundUp(19 + buffer[i].nameBytes + 1, (size_t) 8);
					}

					file->directoryBuffer = EsHeapAllocate(neededSize, true, K_PAGED);

					if (!file->directoryBuffer) {
						EsHeapFree(buffer, bufferSize * sizeof(EsDirectoryChild), K_FIXED);
						return -ENOMEM;
					}

					file->directoryBufferLength = neededSize;

					uint8_t *position = (uint8_t *) file->directoryBuffer;

					for (uintptr_t i = 0; i < count; i++) {
						// EsPrint("%d - %d\n", i, position - (uint8_t *) file->directoryBuffer);
						size_t size = RoundUp(19 + buffer[i].nameBytes + 1, (size_t) 8);
						EsDirectoryChild *entry = buffer + i;
						((uint64_t *) position)[0] = EsRandomU64(); // We don't have a concept of inodes.
						((uint64_t *) position)[1] = position - (uint8_t *) file->directoryBuffer + size; 
						((uint16_t *) position)[8] = size;
						((uint8_t *) position)[18] = entry->type == ES_NODE_DIRECTORY ? 4 /* DT_DIR */
							: entry->type == ES_NODE_FILE ? 8 /* DT_REG */ : 0 /* DT_UNKNOWN */;
						EsMemoryCopy(position + 19, entry->name, entry->nameBytes);
						*(position + 19 + entry->nameBytes) = 0;
						position += size;
					}

					EsHeapFree(buffer, bufferSize * sizeof(EsDirectoryChild), K_FIXED);
				}

				uint64_t offset = file->offsetIntoFile;
				size_t bufferSize = syscall.arguments[2];

				while (offset + 19 < file->directoryBufferLength) {
					uint8_t *position = (uint8_t *) file->directoryBuffer + offset;
					uint64_t nextOffset = ((uint64_t *) position)[1]; 

					if (nextOffset > file->directoryBufferLength || nextOffset < offset + 19
							|| nextOffset - file->offsetIntoFile >= bufferSize) {
						break;
					}

					offset = nextOffset;
				}

				size_t bytesToReturn = offset - file->offsetIntoFile;

				if (bytesToReturn > bufferSize) {
					bytesToReturn = bufferSize;
				}

				EsMemoryCopy((void *) syscall.arguments[1], (uint8_t *) file->directoryBuffer + file->offsetIntoFile, bytesToReturn);
				file->offsetIntoFile += bytesToReturn;
				return bytesToReturn;
			} break;

			case SYS_setpgid: {
				Process *process = (Process *) syscall.arguments[0];
				process->pgid = syscall.arguments[1];
				return 0;
			} break;
		}

		return -1;
	}
};

#endif
