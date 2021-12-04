// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

#define ES_API
#define ES_FORWARD(x) x
#define ES_EXTERN_FORWARD extern "C"
#define ES_DIRECT_API
#include <essence.h>

#ifdef ENABLE_POSIX_SUBSYSTEM

#define ARRAY_DEFINITIONS_ONLY
#include <shared/array.cpp>

extern "C" void *ProcessorTLSRead(uintptr_t offset);
extern "C" void ProcessorTLSWrite(uintptr_t offset, void *value);
extern ptrdiff_t tlsStorageOffset;
EsMountPoint *NodeFindMountPoint(const char *prefix, size_t prefixBytes);
EsProcessStartupInformation *ProcessGetStartupInformation();

#define _POSIX_SOURCE
#define _GNU_SOURCE
#define __NEED_struct_iovec
#define __NEED_sigset_t
#define __NEED_struct_timespec
#define __NEED_time_t
#include <limits.h>
#include <bits/syscall.h>
#include <bits/alltypes.h>
#include <signal.h>
#include <sys/sysinfo.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <bits/ioctl.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <poll.h>
#include <sys/utsname.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <sched.h>
#include <elf.h>

struct ChildProcess {
	uint64_t id;
	EsHandle handle;
};

char *workingDirectory;
Array<ChildProcess> childProcesses;

#ifdef DEBUG_BUILD
double syscallTimeSpent[1024];
uint64_t syscallCallCount[1024];
#endif

const char *syscallNames[] = {
	"read", "write", "open", "close", "stat", "fstat", "lstat", "poll",
	"lseek", "mmap", "mprotect", "munmap", "brk", "rt_sigaction", "rt_sigprocmask", "rt_sigreturn",
	"ioctl", "pread64", "pwrite64", "readv", "writev", "access", "pipe", "select",
	"sched_yield", "mremap", "msync", "mincore", "madvise", "shmget", "shmat", "shmctl",
	"dup", "dup2", "pause", "nanosleep", "getitimer", "alarm", "setitimer", "getpid",
	"sendfile", "socket", "connect", "accept", "sendto", "recvfrom", "sendmsg", "recvmsg",
	"shutdown", "bind", "listen", "getsockname", "getpeername", "socketpair", "setsockopt", "getsockopt",
	"clone", "fork", "vfork", "execve", "exit", "wait4", "kill", "uname",
	"semget", "semop", "semctl", "shmdt", "msgget", "msgsnd", "msgrcv", "msgctl",
	"fcntl", "flock", "fsync", "fdatasync", "truncate", "ftruncate", "getdents", "getcwd",
	"chdir", "fchdir", "rename", "mkdir", "rmdir", "creat", "link", "unlink",
	"symlink", "readlink", "chmod", "fchmod", "chown", "fchown", "lchown", "umask",
	"gettimeofday", "getrlimit", "getrusage", "sysinfo", "times", "ptrace", "getuid", "syslog",
	"getgid", "setuid", "setgid", "geteuid", "getegid", "setpgid", "getppid", "getpgrp",
	"setsid", "setreuid", "setregid", "getgroups", "setgroups", "setresuid", "getresuid", "setresgid",
	"getresgid", "getpgid", "setfsuid", "setfsgid", "getsid", "capget", "capset", "rt_sigpending",
	"rt_sigtimedwait", "rt_sigqueueinfo", "rt_sigsuspend", "sigaltstack", "utime", "mknod", "uselib", "personality",
	"ustat", "statfs", "fstatfs", "sysfs", "getpriority", "setpriority", "sched_setparam", "sched_getparam",
	"sched_setscheduler", "sched_getscheduler", "sched_get_priority_max", "sched_get_priority_min", "sched_rr_get_interval", "mlock", "munlock", "mlockall",
	"munlockall", "vhangup", "modify_ldt", "pivot_root", "_sysctl", "prctl", "arch_prctl", "adjtimex",
	"setrlimit", "chroot", "sync", "acct", "settimeofday", "mount", "umount2", "swapon",
	"swapoff", "reboot", "sethostname", "setdomainname", "iopl", "ioperm", "create_module", "init_module",
	"delete_module", "get_kernel_syms", "query_module", "quotactl", "nfsservctl", "getpmsg", "putpmsg", "afs_syscall",
	"tuxcall", "security", "gettid", "readahead", "setxattr", "lsetxattr", "fsetxattr", "getxattr",
	"lgetxattr", "fgetxattr", "listxattr", "llistxattr", "flistxattr", "removexattr", "lremovexattr", "fremovexattr",
	"tkill", "time", "futex", "sched_setaffinity", "sched_getaffinity", "set_thread_area", "io_setup", "io_destroy",
	"io_getevents", "io_submit", "io_cancel", "get_thread_area", "lookup_dcookie", "epoll_create", "epoll_ctl_old", "epoll_wait_old",
	"remap_file_pages", "getdents64", "set_tid_address", "restart_syscall", "semtimedop", "fadvise64", "timer_create", "timer_settime",
	"timer_gettime", "timer_getoverrun", "timer_delete", "clock_settime", "clock_gettime", "clock_getres", "clock_nanosleep", "exit_group",
	"epoll_wait", "epoll_ctl", "tgkill", "utimes", "vserver", "mbind", "set_mempolicy", "get_mempolicy",
	"mq_open", "mq_unlink", "mq_timedsend", "mq_timedreceive", "mq_notify", "mq_getsetattr", "kexec_load", "waitid",
	"add_key", "request_key", "keyctl", "ioprio_set", "ioprio_get", "inotify_init", "inotify_add_watch", "inotify_rm_watch",
	"migrate_pages", "openat", "mkdirat", "mknodat", "fchownat", "futimesat", "newfstatat", "unlinkat",
	"renameat", "linkat", "symlinkat", "readlinkat", "fchmodat", "faccessat", "pselect6", "ppoll",
	"unshare", "set_robust_list", "get_robust_list", "splice", "tee", "sync_file_range", "vmsplice", "move_pages",
	"utimensat", "epoll_pwait", "signalfd", "timerfd_create", "eventfd", "fallocate", "timerfd_settime", "timerfd_gettime",
	"accept4", "signalfd4", "eventfd2", "epoll_create1", "dup3", "pipe2", "inotify_init1", "preadv",
	"pwritev", "rt_tgsigqueueinfo", "perf_event_open", "recvmmsg", "fanotify_init", "fanotify_mark", "prlimit64", "name_to_handle_at",
	"open_by_handle_at", "clock_adjtime", "syncfs", "sendmmsg", "setns", "getcpu", "process_vm_readv", "process_vm_writev",
	"kcmp", "finit_module", "sched_setattr", "sched_getattr", "renameat2", "seccomp", "getrandom", "memfd_create",
	"kexec_file_load", "bpf", "execveat", "userfaultfd", "membarrier", "mlock2", "copy_file_range", "preadv2",
	"pwritev2", "pkey_mprotect", "pkey_alloc", "pkey_free", "statx",
};

extern "C" void ProcessorCheckStackAlignment();

char *EsPOSIXConvertPath(const char *path, size_t *outNameLength, bool addPOSIXMountPointPrefix) {
	const char *posixNames[2] = { path[0] != '/' ? workingDirectory : nullptr, path };
	size_t posixNameLengths[2] = { path[0] != '/' ? EsCStringLength(workingDirectory) : 0, EsCStringLength(path) };

	char *name = (char *) EsHeapAllocate(posixNameLengths[0] + posixNameLengths[1] + (addPOSIXMountPointPrefix ? 7 : 0) + 2 /* space for / and NUL; see chdir */, true);
	if (!name) return nullptr;
	size_t nameLength = 0;
	if (addPOSIXMountPointPrefix) name += 7;

	for (uintptr_t i = 0; i < 2; i++) {
		while (posixNameLengths[i]) {
			const char *entry = posixNames[i];
			size_t entryLength = 0;

			while (posixNameLengths[i]) {
				posixNameLengths[i]--;
				posixNames[i]++;
				if (entry[entryLength] == '/') break;
				entryLength++;
			}

			if (!entryLength || (entryLength == 1 && entry[0] == '.')) {
				// Ignore.
			} else if (entryLength == 2 && entry[0] == '.' && entry[1] == '.' && nameLength) {
				while (name[--nameLength] != '/');
			} else {
				name[nameLength++] = '/';
				EsMemoryCopy(name + nameLength, entry, entryLength);
				nameLength += entryLength;
			}
		}
	}

	if (!nameLength) {
		nameLength++;
		name[0] = '/';
	}

	if (addPOSIXMountPointPrefix) {
		name -= 7;
		nameLength += 7;
		EsMemoryCopy(name, "|POSIX:", 7);
	}

	if (outNameLength) *outNameLength = nameLength;
	name[nameLength] = 0;
	return name;
}

long EsPOSIXSystemCall(long n, long a1, long a2, long a3, long a4, long a5, long a6) {
#ifdef DEBUG_BUILD
	ProcessorCheckStackAlignment();
#endif

	long returnValue = 0;
	_EsPOSIXSyscall syscall = { n, a1, a2, a3, a4, a5, a6 };

#ifdef DEBUG_BUILD
	double startTime = EsTimeStampMs();
	static double processStartTime = 0;

	if (!processStartTime) {
		processStartTime = startTime;
	}

	if (n == SYS_exit_group) {
		double processExecutionTime = startTime - processStartTime;

		EsPrint("=== System call performance ===\n");

		int array[sizeof(syscallNames) / sizeof(syscallNames[0])];

		for (uintptr_t i = 0; i < sizeof(array) / sizeof(array[0]); i++) {
			array[i] = i;
		}

		EsCRTqsort(array, sizeof(array) / sizeof(array[0]), sizeof(array[0]), [] (const void *_left, const void *_right) {
			int left = *(int *) _left, right = *(int *) _right;
			if (syscallTimeSpent[left] > syscallTimeSpent[right]) return -1;
			if (syscallTimeSpent[left] < syscallTimeSpent[right]) return 1;
			return 0;
		});

		double total = 0;

		for (uintptr_t i = 0; i < sizeof(array) / sizeof(array[0]); i++) {
			if (!syscallTimeSpent[array[i]]) break;
			EsPrint("%z - %Fms - %d calls\n", syscallNames[array[i]], syscallTimeSpent[array[i]], syscallCallCount[array[i]]);
			total += syscallTimeSpent[array[i]];
		}

		EsPrint("Total time in system calls: %Fms\n", total);
		EsPrint("Total run time of process: %Fms\n", processExecutionTime);
	}
#endif

	switch (n) {
		case SYS_open: {
			size_t pathBytes;
			char *path = EsPOSIXConvertPath((const char *) a1, &pathBytes, false);
			syscall.arguments[0] = (long) path;
			syscall.arguments[4] = (long) NodeFindMountPoint(EsLiteral("|POSIX:"))->base;
			syscall.arguments[6] = (long) pathBytes;
			returnValue = EsSyscall(ES_SYSCALL_POSIX, (uintptr_t) &syscall, 0, 0, 0);
			// EsPrint("SYS_open '%s' with handle %d\n", pathBytes, path, returnValue);
			EsHeapFree(path);
		} break;

		case SYS_vfork: {
			long result = EsSyscall(ES_SYSCALL_POSIX, (uintptr_t) &syscall, 0, 0, 0);

			if (result > 0) {
				EsHandle handle = result;
				ChildProcess pid = { EsProcessGetID(handle), handle };
				childProcesses.Add(pid);
				returnValue = pid.id;
			}
		} break;

		case SYS_pipe: {
			syscall.index = SYS_pipe2;
			syscall.arguments[1] = 0;
			returnValue = EsSyscall(ES_SYSCALL_POSIX, (uintptr_t) &syscall, 0, 0, 0);
		} break;

		case SYS_close: {
			// EsPrint("SYS_close handle %d\n", a1);
			returnValue = EsSyscall(ES_SYSCALL_POSIX, (uintptr_t) &syscall, 0, 0, 0);
		} break;

		case SYS_pipe2:
		case SYS_writev:
		case SYS_fcntl:
		case SYS_dup2:
		case SYS_write:
		case SYS_readv:
		case SYS_lseek:
		case SYS_read:
		case SYS_fstat:
		case SYS_sysinfo:
		case SYS_getdents64:
		case SYS_exit_group:
		case SYS_ioctl: {
			returnValue = EsSyscall(ES_SYSCALL_POSIX, (uintptr_t) &syscall, 0, 0, 0);
		} break;

		case SYS_chdir: {
			char *simplified = EsPOSIXConvertPath((const char *) a1, nullptr, false);
			EsHeapFree(workingDirectory);
			size_t oldLength = EsCStringLength(simplified);
			simplified[oldLength] = '/';
			simplified[oldLength + 1] = 0;
			workingDirectory = simplified;
		} break;

		case SYS_getpid: {
			// Run the system call directly, so that the kernel can handle the vfork()'d case.
			EsObjectID id;
			EsSyscall(ES_SYSCALL_THREAD_GET_ID, ES_CURRENT_PROCESS, (uintptr_t) &id, 0, 0);
			returnValue = id;
		} break;

		case SYS_gettid: {
			returnValue = EsThreadGetID(ES_CURRENT_THREAD);
		} break;

		case SYS_getcwd: {
			size_t bytes = EsCStringLength(workingDirectory) + 1;
			char *destination = (char *) a1;

			if (bytes > (size_t) a2) {
				returnValue = -ERANGE;
			} else { 
				EsMemoryCopy(destination, workingDirectory, bytes);
				if (workingDirectory[bytes - 2] == '/' && bytes > 2) destination[bytes - 2] = 0;
				returnValue = a1;
			}
		} break;

		case SYS_getppid:
		case SYS_getuid:
		case SYS_getgid:
		case SYS_getegid:
		case SYS_geteuid: {
			// TODO.
		} break;

		case SYS_getrusage: {
			// TODO.
			struct rusage *buffer = (struct rusage *) a2;
			EsMemoryZero(buffer, sizeof(struct rusage));
		} break;

		case SYS_unlink: {
			_EsNodeInformation node;
			node.handle = NodeFindMountPoint(EsLiteral("|POSIX:"))->base;
			size_t pathBytes;
			char *path = EsPOSIXConvertPath((const char *) a1, &pathBytes, false);
			EsError error = EsSyscall(ES_SYSCALL_NODE_OPEN, (uintptr_t) path, pathBytes, ES_NODE_FAIL_IF_NOT_FOUND | ES_FILE_WRITE, (uintptr_t) &node);
			EsHeapFree(path);
			if (error == ES_ERROR_FILE_DOES_NOT_EXIST) returnValue = -ENOENT;
			else if (error == ES_ERROR_PATH_NOT_TRAVERSABLE) returnValue = -ENOTDIR;
			else if (error == ES_ERROR_FILE_IN_EXCLUSIVE_USE) returnValue = -EBUSY;
			else if (error == ES_ERROR_DRIVE_CONTROLLER_REPORTED || error == ES_ERROR_CORRUPT_DATA) returnValue = -EIO;
			else if (error != ES_SUCCESS) returnValue = -EACCES;
			else {
				error = EsSyscall(ES_SYSCALL_NODE_DELETE, node.handle, 0, 0, 0);
				EsHandleClose(node.handle);
				if (error == ES_ERROR_DRIVE_CONTROLLER_REPORTED || error == ES_ERROR_CORRUPT_DATA) returnValue = -EIO;
				else if (error != ES_SUCCESS) returnValue = -EACCES;
			}
		} break;

		case SYS_truncate: {
			_EsNodeInformation node;
			node.handle = NodeFindMountPoint(EsLiteral("|POSIX:"))->base;
			size_t pathBytes;
			char *path = EsPOSIXConvertPath((const char *) a1, &pathBytes, false);
			EsError error = EsSyscall(ES_SYSCALL_NODE_OPEN, (uintptr_t) path, pathBytes, ES_NODE_FAIL_IF_NOT_FOUND | ES_FILE_WRITE, (uintptr_t) &node);
			EsHeapFree(path);
			if (error == ES_ERROR_FILE_DOES_NOT_EXIST) returnValue = -ENOENT;
			else if (error == ES_ERROR_PATH_NOT_TRAVERSABLE) returnValue = -ENOTDIR;
			else if (error == ES_ERROR_FILE_IN_EXCLUSIVE_USE) returnValue = -EBUSY;
			else if (error == ES_ERROR_DRIVE_CONTROLLER_REPORTED || error == ES_ERROR_CORRUPT_DATA) returnValue = -EIO;
			else if (error != ES_SUCCESS) returnValue = -EACCES;
			else if (node.type == ES_NODE_DIRECTORY) { returnValue = -EISDIR; EsHandleClose(node.handle); }
			else {
				EsError error = EsFileResize(node.handle, a2);
				EsHandleClose(node.handle);
				if (error == ES_ERROR_DRIVE_CONTROLLER_REPORTED || error == ES_ERROR_CORRUPT_DATA) returnValue = -EIO;
				else if (error != ES_SUCCESS) returnValue = -EACCES;
			}
		} break;

		case SYS_execve: {
			// NOTE We can't use EsHeapAllocate since the system call never returns.

			size_t pathBytes;
			char *_path = EsPOSIXConvertPath((const char *) a1, &pathBytes, false);
			char *path = (char *) __builtin_alloca(pathBytes);
			EsMemoryCopy(path, _path, pathBytes);
			EsHeapFree(_path);

			char **argv = (char **) a2;
			char **envp = (char **) a3;
		
			size_t environmentSize = 2;

			for (uintptr_t i = 0; argv[i]; i++) environmentSize += EsCStringLength(argv[i]) + 1;
			for (uintptr_t i = 0; envp[i]; i++) environmentSize += EsCStringLength(envp[i]) + 1;

			bool environmentContainsWorkingDirectory = false;

			for (uintptr_t i = 0; envp[i]; i++) {
				if (0 == EsMemoryCompare("PWD=", envp[i], 4)) {
					environmentContainsWorkingDirectory = true;
					break;
				}
			}

			if (!environmentContainsWorkingDirectory) {
				environmentSize += 4 + EsCStringLength(workingDirectory) + 1;
			}

			char newEnvironment[environmentSize]; 
			char *position = newEnvironment;
			EsMemoryZero(newEnvironment, environmentSize);

			for (uintptr_t i = 0; argv[i]; i++) {
				size_t length = EsCStringLength(argv[i]) + 1;
				EsMemoryCopy(position, argv[i], length);
				position += length;
			}

			position++;

			for (uintptr_t i = 0; envp[i]; i++) {
				size_t length = EsCStringLength(envp[i]) + 1;
				EsMemoryCopy(position, envp[i], length);
				position += length;
			}

			if (!environmentContainsWorkingDirectory) {
				size_t length = 4 + EsCStringLength(workingDirectory) + 1;
				EsMemoryCopy(position, "PWD=", 4);
				EsMemoryCopy(position + 4, workingDirectory, EsCStringLength(workingDirectory) + 1);
				position += length;
			}

			syscall.arguments[0] = (long) path;
			syscall.arguments[1] = (long) pathBytes;
			syscall.arguments[2] = (long) newEnvironment;
			syscall.arguments[3] = (long) environmentSize;
			syscall.arguments[4] = (long) NodeFindMountPoint(EsLiteral("|POSIX:"))->base;

			returnValue = EsSyscall(ES_SYSCALL_POSIX, (uintptr_t) &syscall, 0, 0, 0);
		} break;

		case SYS_access: {
			// We don't support file permissions yet, so just check the file exists.
			int fd = EsPOSIXSystemCall(SYS_open, a1, O_PATH, 0, 0, 0, 0);
			if (fd < 0) returnValue = fd;
			else {
				returnValue = 0;
				EsPOSIXSystemCall(SYS_close, fd, 0, 0, 0, 0, 0);
			}
		} break;

		case SYS_lstat:
		case SYS_stat: {
			int fd = EsPOSIXSystemCall(SYS_open, a1, O_PATH, 0, 0, 0, 0);
			if (fd < 0) returnValue = fd;
			else {
				returnValue = EsPOSIXSystemCall(SYS_fstat, fd, a2, 0, 0, 0, 0);
				EsPOSIXSystemCall(SYS_close, fd, 0, 0, 0, 0, 0);
			}
		} break;

		case SYS_readlink: {
			if (0 == EsMemoryCompare((void *) a1, EsLiteral("/proc/self/fd/"))) {
				// The process is trying to get the path of a file descriptor.
				syscall.index = ES_POSIX_SYSCALL_GET_POSIX_FD_PATH;
				syscall.arguments[0] = EsCRTatoi((char *) a1 + EsCStringLength("/proc/self/fd/"));
				returnValue = EsSyscall(ES_SYSCALL_POSIX, (uintptr_t) &syscall, 0, 0, 0);
			} else {
				// We don't support symbolic links, so the output is the same as the input.
				int length = EsCStringLength((char *) a1);
				EsMemoryZero((void *) a2, a3);
				EsMemoryCopy((void *) a2, (void *) a1, length > a3 ? a3 : length);
				returnValue = length > a3 ? a3 : length;
			}
		} break;

		case SYS_set_tid_address: {
			// TODO Support set_child_tid and clear_child_tid addresses.
			returnValue = EsThreadGetID(ES_CURRENT_THREAD);
		} break;

		case SYS_brk: {
			returnValue = -1;
		} break;

		case SYS_mremap: {
			returnValue = -ENOMEM;
		} break;

		case SYS_mmap: {
			bool read = a3 & PROT_READ, write = a3 & PROT_WRITE, none = a3 == PROT_NONE;

			if (a4 & MAP_FIXED) {
				returnValue = -ENOMEM;
			} else if ((a4 == (MAP_ANON | MAP_PRIVATE)) && (a5 == -1) && (a6 == 0) && ((read && write) || none)) {
				returnValue = (long) EsMemoryReserve(a2, ES_MEMORY_PROTECTION_READ_WRITE, none ? 0 : ES_MEMORY_RESERVE_COMMIT_ALL);
			} else {
				EsPanic("Unsupported mmap [%x, %x, %x, %x, %x, %x]\n", a1, a2, a3, a4, a5, a6);
			}

		} break;

		case SYS_munmap: {
			void *address = (void *) a1;
			size_t length = (size_t) a2;

			if (length == 0 || ((uintptr_t) address & (ES_PAGE_SIZE - 1))) {
				returnValue = -EINVAL;
			} else {
				EsMemoryUnreserve(address, length); 
			}
		} break;

		case SYS_mprotect: {
			void *address = (void *) a1;
			size_t length = (size_t) a2;
			int protection = (int) a3;

			if (protection == (PROT_READ | PROT_WRITE)) {
				returnValue = EsMemoryCommit(address, length) ? 0 : -ENOMEM;
			} else if (protection == 0) {
				returnValue = EsMemoryDecommit(address, length) ? 0 : -ENOMEM; 
			} else {
				EsPanic("Unsupported mprotect [%x, %x, %x, %x, %x, %x]\n", a1, a2, a3, a4, a5, a6);
			}
		} break;

		case SYS_prlimit64: {
			// You can't access other process's resources.
			if (a1 && a1 != (long) EsProcessGetID(ES_CURRENT_PROCESS)) {
				returnValue = -EPERM;
				break;
			}

			struct rlimit *newLimit = (struct rlimit *) a3;

			if (newLimit && a2 != RLIMIT_STACK) {
				returnValue = -EPERM;
				break;
			}

			struct rlimit *limit = (struct rlimit *) a4;

			if (a2 == RLIMIT_STACK) {
				size_t current, maximum;
				EsError error = EsSyscall(ES_SYSCALL_THREAD_STACK_SIZE, ES_CURRENT_THREAD, 
						(uintptr_t) &current, (uintptr_t) &maximum, newLimit ? newLimit->rlim_cur : 0);

				if (limit) {
					limit->rlim_cur = current;
					limit->rlim_max = maximum;
				}

				if (error != ES_SUCCESS) returnValue = -EINVAL;
			} else if (a2 == RLIMIT_AS) {
				if (limit) limit->rlim_cur = limit->rlim_max = RLIM_INFINITY;
			} else if (a2 == RLIMIT_RSS) {
				if (limit) limit->rlim_cur = limit->rlim_max = 0x10000000; // 256MB. This value is fake. TODO
			} else if (a2 == RLIMIT_NOFILE) {
				if (limit) limit->rlim_cur = limit->rlim_max = 1048576;
			} else {
				EsPanic("Unsupported prlimit64 [%x]\n", a2);
			}
		} break;

		case SYS_setitimer:
		case SYS_madvise:
		case SYS_umask:
		case SYS_chmod:
		case SYS_rt_sigaction:
		case SYS_rt_sigprocmask: {
			// TODO Support signals.
			// Ignore.
		} break;

		case SYS_clock_gettime: {
			// We'll ignore the clockid_t in a1, since we don't have proper timekeeping yet.
			struct timespec *tp = (struct timespec *) a2;
			double timeStampMs = EsTimeStampMs();
			uint64_t ns = timeStampMs * 1e6;
			tp->tv_sec = ns / 1000000000;
			tp->tv_nsec = ns % 1000000000;
		} break;

		case SYS_wait4: {
			if ((a3 & ~3) || a4 || a1 < -1 || !a1) {
				EsPanic("Unsupported wait4 [%x/%x/%x/%x]\n", a1, a2, a3, a4);
			}

			int *wstatus = (int *) a2;
			int options = a3;

			bool foundChild = false;
			uintptr_t childIndex = 0;

			if (a1 > 0) {
				for (uintptr_t i = 0; i < childProcesses.Length(); i++) {
					if (childProcesses[i].id == (uint64_t) a1) {
						foundChild = true;
						childIndex = i;
						break;
					}
				}
			} else if (a1 == -1) {
				foundChild = childProcesses.Length();
			}

			if (!foundChild) {
				returnValue = -ECHILD;
			} else {
				returnValue = 0;

				if (~options & 1 /* WNOHANG */) {
					if (a1 == -1) {
						EsHandle *handles = (EsHandle *) __builtin_alloca(childProcesses.Length() * sizeof(EsHandle));

						for (uintptr_t i = 0; i < childProcesses.Length(); i++) {
							handles[i] = childProcesses[i].handle;
						}

						EsWait(handles, childProcesses.Length(), ES_WAIT_NO_TIMEOUT);
					} else {
						EsWaitSingle(childProcesses[childIndex].handle);
					}
				}

				for (uintptr_t i = 0; i < childProcesses.Length(); i++) {
					if (a1 > 0 && childProcesses[i].id != (uint64_t) a1) {
						continue;
					}

					EsHandle handle = childProcesses[i].handle;
					EsProcessState state;
					EsProcessGetState(handle, &state);

					if (state.flags & ES_PROCESS_STATE_ALL_THREADS_TERMINATED) {
						returnValue = childProcesses[i].id;
						*wstatus = (EsProcessGetExitStatus(handle) & 0xFF) << 8;
						EsHandleClose(handle);
						childProcesses.Delete(i);
						break;
					}
				}
			}
		} break;

		case SYS_sched_getaffinity: {
			// TODO Getting the correct number of CPUs.
			// TODO Getting the affinity for other processes.
			cpu_set_t *set = (cpu_set_t *) a3;
			EsCRTmemset(set, 0, a2);
			CPU_SET(0, set); 
		} break;

		case SYS_mkdir: {
			size_t pathBytes;
			char *path = EsPOSIXConvertPath((const char *) a1, &pathBytes, true);
			EsError error = EsPathCreate(path, pathBytes, ES_NODE_DIRECTORY, false);
			if (error == ES_ERROR_INSUFFICIENT_RESOURCES) returnValue = -ENOMEM;
			else if (error == ES_ERROR_FILE_ALREADY_EXISTS) returnValue = -EEXIST;
			else if (error == ES_ERROR_PATH_NOT_TRAVERSABLE) returnValue = -ENOENT;
			else if (error == ES_ERROR_PATH_NOT_WITHIN_MOUNTED_VOLUME) returnValue = -ENOENT;
			else if (error == ES_ERROR_FILE_ON_READ_ONLY_VOLUME) returnValue = -EPERM;
			EsHeapFree(path);
		} break;

		case SYS_uname: {
			struct utsname *buffer = (struct utsname *) a1;
			EsCRTstrcpy(buffer->sysname, "Essence");
			EsCRTstrcpy(buffer->release, "0.0.0");
			EsCRTstrcpy(buffer->version, "0.0.0");
			EsCRTstrcpy(buffer->machine, "Unknown");
		} break;

		case SYS_setpgid: {
			if (a1 < 0) {
				returnValue = -EINVAL;
			} else {
				EsHandle process = EsProcessOpen(a1);

				if (process != ES_INVALID_HANDLE) {
					syscall.arguments[0] = process;
					returnValue = EsSyscall(ES_SYSCALL_POSIX, (uintptr_t) &syscall, 0, 0, 0);
					EsHandleClose(process);
				} else {
					returnValue = -ESRCH;
				}
			}
		} break;

		case SYS_rename: {
			size_t oldPathBytes;
			char *oldPath = EsPOSIXConvertPath((const char *) a1, &oldPathBytes, true);
			size_t newPathBytes;
			char *newPath = EsPOSIXConvertPath((const char *) a2, &newPathBytes, true);
			EsError error = EsPathMove(oldPath, oldPathBytes, newPath, newPathBytes);
			EsHeapFree(oldPath);
			EsHeapFree(newPath);
			// TODO More return values.
			if (error == ES_ERROR_FILE_DOES_NOT_EXIST) returnValue = -ENOENT;
			else if (error == ES_ERROR_PATH_NOT_TRAVERSABLE) returnValue = -ENOTDIR;
			else if (error == ES_ERROR_FILE_IN_EXCLUSIVE_USE) returnValue = -EBUSY;
			else if (error == ES_ERROR_DRIVE_CONTROLLER_REPORTED || error == ES_ERROR_CORRUPT_DATA) returnValue = -EIO;
			else if (error != ES_SUCCESS) returnValue = -EACCES;
		} break;

		case -1000: {
			// Update thread local storage:
			void *apiTLS = ProcessorTLSRead(tlsStorageOffset);
			EsSyscall(ES_SYSCALL_THREAD_SET_TLS, a1, 0, 0, 0);
			tlsStorageOffset = -a2;
			ProcessorTLSWrite(tlsStorageOffset, apiTLS);
		} break;

		default: {
			EsPanic("Unknown linux syscall %d = %z.\nArguments: %x, %x, %x, %x, %x, %x\n", 
					n, syscallNames[n], a1, a2, a3, a4, a5, a6);
		} break;
	}

#ifdef DEBUG_BUILD
	double endTime = EsTimeStampMs();
	syscallTimeSpent[n] += endTime - startTime;
	syscallCallCount[n]++;
#endif

	// EsPrint(":: %z %x %x %x -> %x; %Fms\n", syscallNames[n], a1, a2, a3, returnValue, endTime - startTime);

	return returnValue;
}

void EsPOSIXInitialise(int *argc, char ***argv) {
	EsProcessStartupInformation *startupInformation = ProcessGetStartupInformation();

	// Get the arguments and environment.

	EsHandle environmentHandle = startupInformation->data.subsystemData;
	char *environmentBuffer = (char *) "./application\0\0LANG=en_US.UTF-8\0PWD=/\0HOME=/\0PATH=/Applications/POSIX/bin\0TMPDIR=/Applications/POSIX/tmp\0\0";

	if (environmentHandle) {
		EsAssert(startupInformation->data.subsystemID == ES_SUBSYSTEM_ID_POSIX);
		environmentBuffer = (char *) EsHeapAllocate(ARG_MAX, false);
		EsConstantBufferRead((EsHandle) environmentHandle, environmentBuffer);
		EsHandleClose((EsHandle) environmentHandle);
	}

	// Extract the arguments and environment variables.

	uintptr_t position = 0;
	char *start = environmentBuffer;
	Array<void *> _argv = {};
	*argc = 0;

	for (int i = 0; i < 2; i++) {
		while (position < ARG_MAX) {
			if (!environmentBuffer[position]) {
				_argv.Add(start);
				start = environmentBuffer + position + 1;

				if (i == 0) {
					*argc = *argc + 1;
				}

				if (!environmentBuffer[position + 1]) {
					start = environmentBuffer + position + 2;
					_argv.Add(nullptr);
					break;
				}
			}

			position++;
		}

		position += 2;
	}

	// Copy the working directory string.

	for (uintptr_t i = *argc + 1; i < _argv.Length(); i++) {
		if (_argv[i] && 0 == EsMemoryCompare("PWD=", _argv[i], 4)) {
			size_t length = EsCStringLength((char *) _argv[i]) - 4;
			workingDirectory = (char *) EsHeapAllocate(length + 2, false);
			workingDirectory[length] = 0, workingDirectory[length + 1] = 0;
			EsMemoryCopy(workingDirectory, (char *) _argv[i] + 4, length);
			if (workingDirectory[length - 1] != '/') workingDirectory[length] = '/';
		}
	}

	// Add the auxillary vectors.

#ifdef ES_ARCH_X86_64
	Elf64_Phdr *tlsHeader = (Elf64_Phdr *) EsHeapAllocate(sizeof(Elf64_Phdr), true);
	tlsHeader->p_type = PT_TLS;
	tlsHeader->p_flags = 4 /* read */;
	tlsHeader->p_vaddr = startupInformation->tlsImageStart;
	tlsHeader->p_filesz = startupInformation->tlsImageBytes;
	tlsHeader->p_memsz = startupInformation->tlsBytes;
	tlsHeader->p_align = 8;

	_argv.Add((void *) AT_PHNUM);
	_argv.Add((void *) 1);
	_argv.Add((void *) AT_PHENT);
	_argv.Add((void *) sizeof(Elf64_Phdr));
	_argv.Add((void *) AT_PHDR);
	_argv.Add((void *) tlsHeader);
#else
#error "no architecture TLS support"
#endif

	_argv.Add((void *) AT_PAGESZ);
	_argv.Add((void *) ES_PAGE_SIZE);

	_argv.Add(nullptr);

	// Return argv.
	
	*argv = (char **) _argv.array;
}

#endif
