#include <essence.h>

int main(int argc, char **argv, char **envp);
int __libc_start_main(int (*main)(int, char **, char **), int argc, char **argv);

void _start() {
	int argc; 
	char **argv;
	EsPOSIXInitialise(&argc, &argv);
	__libc_start_main(main, argc, argv);
}

