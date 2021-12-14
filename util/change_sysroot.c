// gcc -o bin/change_sysroot util/change_sysroot.c -Wall -Wextra
// ./change_sysroot "/home/runner/work/build-gcc-x86_64-essence/build-gcc-x86_64-essence/essence/root/" $SYSROOT ...

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <stdbool.h>
#include <unistd.h>
#include <wait.h>
#include <limits.h>
#include <syscall.h>
#include <sys/ptrace.h>
#include <sys/user.h>

const char *needle;
const char *replaceWith;

bool GetReplacement(const char *original, char *buffer) {
	if (0 == memcmp(needle, original, strlen(needle))) {
		strcpy(buffer, replaceWith);
		strcat(buffer, original + strlen(needle));
		// fprintf(stderr, "'%s' -> '%s'\n", original, buffer);
		return true;
	} else {
		return false;
	}
}

char *ReadString(pid_t pid, uintptr_t address) {
	char *buffer = NULL;
	uintptr_t position = 0;
	uintptr_t capacity = 0;

	while (true) {
		char b = ptrace(PTRACE_PEEKDATA, pid, address, 0);

		if (position == capacity) {
			capacity = (capacity + 8) * 2;
			buffer = realloc(buffer, capacity);
		}

		buffer[position] = b;
		position++, address++;

		if (b == 0) {
			break;
		}
	}

	return realloc(buffer, position);
}

uintptr_t WriteString(pid_t pid, uintptr_t rsp, const char *string) {
	uintptr_t address = rsp - 128 /* red zone */ - strlen(string) - sizeof(uintptr_t) /* POKEDATA writes words */;

	for (int i = 0; true; i++) {
		uintptr_t c = string[i];
		ptrace(PTRACE_POKEDATA, pid, (void *) (address + i), (void *) c);
		if (!c) break;
	}

	return address;
}

bool ReplaceString(pid_t pid, uintptr_t rsp, uintptr_t *address) {
	char *original = ReadString(pid, *address);
	char buffer[PATH_MAX];
	bool modified = GetReplacement(original, buffer);
	if (modified) *address = WriteString(pid, rsp, buffer);
	free(original);
	return modified;
}

int main(int argc, char **argv) {
	if (argc < 4) {
		fprintf(stderr, "Usage: %s <path to replace> <what to replace with> <executable> <arguments to executable>\n", argv[0]);
		return 1;
	}

	needle = argv[1];
	replaceWith = argv[2];

	pid_t basePID;

	{
		pid_t pid = vfork();

		if (pid == 0) {
			ptrace(PTRACE_TRACEME, 0, 0, 0);
			execvp(argv[3], &argv[3]);
		}

		waitpid(pid, 0, 0);
		ptrace(PTRACE_SETOPTIONS, pid, 0, PTRACE_O_EXITKILL | PTRACE_O_TRACEVFORK);
		ptrace(PTRACE_SYSCALL, pid, 0, 0);
		basePID = pid;
	}

	while (kill(basePID, 0) == 0 /* still alive */) {
		struct user_regs_struct registers = { 0 };

		pid_t pid = waitpid(-1, 0, 0);
		ptrace(PTRACE_GETREGS, pid, 0, &registers);

		if (registers.orig_rax == SYS_access
				|| registers.orig_rax == SYS_chown
				|| registers.orig_rax == SYS_lstat
				|| registers.orig_rax == SYS_readlink
				|| registers.orig_rax == SYS_stat
				|| registers.orig_rax == SYS_unlink) {
			if (ReplaceString(pid, registers.rsp, (uintptr_t *) &registers.rdi)) {
				ptrace(PTRACE_SETREGS, pid, 0, &registers);
			}
		} else if (registers.orig_rax == SYS_faccessat2
				|| registers.orig_rax == SYS_newfstatat
				|| registers.orig_rax == SYS_openat) {
			if (ReplaceString(pid, registers.rsp, (uintptr_t *) &registers.rsi)) {
				ptrace(PTRACE_SETREGS, pid, 0, &registers);
			}
		} else if (registers.orig_rax == SYS_arch_prctl
				|| registers.orig_rax == SYS_brk
				|| registers.orig_rax == SYS_chmod
				|| registers.orig_rax == SYS_close
				|| registers.orig_rax == SYS_exit_group
				|| registers.orig_rax == SYS_execve
				|| registers.orig_rax == SYS_fcntl
				|| registers.orig_rax == SYS_fstat
				|| registers.orig_rax == SYS_getcwd
				|| registers.orig_rax == SYS_getdents64
				|| registers.orig_rax == SYS_getrandom
				|| registers.orig_rax == SYS_getrusage
				|| registers.orig_rax == SYS_ioctl
				|| registers.orig_rax == SYS_lseek
				|| registers.orig_rax == SYS_madvise
				|| registers.orig_rax == SYS_mmap
				|| registers.orig_rax == SYS_mprotect
				|| registers.orig_rax == SYS_mremap
				|| registers.orig_rax == SYS_munmap
				|| registers.orig_rax == SYS_pipe2
				|| registers.orig_rax == SYS_pread64
				|| registers.orig_rax == SYS_prlimit64
				|| registers.orig_rax == SYS_read
				|| registers.orig_rax == SYS_rename
				|| registers.orig_rax == SYS_rt_sigaction
				|| registers.orig_rax == SYS_rt_sigprocmask
				|| registers.orig_rax == SYS_set_robust_list
				|| registers.orig_rax == SYS_set_tid_address
				|| registers.orig_rax == SYS_sysinfo
				|| registers.orig_rax == SYS_umask
				|| registers.orig_rax == SYS_vfork
				|| registers.orig_rax == SYS_wait4
				|| registers.orig_rax == SYS_write) {
			// Allow through.
		} else {
			printf("unhandled syscall %llu\n", registers.orig_rax);
		}

		ptrace(PTRACE_SYSCALL, pid, 0, 0);
		waitpid(pid, 0, 0);

		if (ptrace(PTRACE_GETREGS, pid, 0, &registers) == -1) {
			// The process has likely exited.
		} else {
			ptrace(PTRACE_SYSCALL, pid, 0, 0);
		}
	}

	return 0;
}
