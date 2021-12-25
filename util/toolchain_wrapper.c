#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#define _STRING(x) #x
#define STRING(x) _STRING(x)

int main(int argc, char **argv) {
	char buffer[PATH_MAX] = {}, change[PATH_MAX] = {}, sysroot[PATH_MAX] = {}, tool[PATH_MAX] = {};
	readlink("/proc/self/exe", buffer, sizeof(buffer));
	int directoryPosition = 0;
	for (int i = 0; buffer[i]; i++) if (buffer[i] == '/') directoryPosition = i;
	buffer[directoryPosition] = 0;
	for (int i = 0; buffer[i]; i++) if (buffer[i] == '/') directoryPosition = i;
	buffer[directoryPosition] = 0;
	for (int i = 0; buffer[i]; i++) if (buffer[i] == '/') directoryPosition = i;
	buffer[directoryPosition] = 0;
	strcpy(change, buffer);
	strcpy(sysroot, buffer);
	strcpy(tool, buffer);
	strcat(change, "/bin/change_sysroot");
	strcat(sysroot, "/root/");
	strcat(tool, "/cross/bin/" STRING(TOOL));
	// printf("'%s'\n", change);
	// printf("'%s'\n", sysroot);
	// printf("'%s'\n", tool);
	char **newArgv = (char **) calloc(sizeof(char *), (argc + 4));
	newArgv[0] = change;
	newArgv[1] = STRING(CONFIGURE_SYSROOT);
	newArgv[2] = sysroot;
	newArgv[3] = tool;
	memcpy(newArgv + 4, argv + 1, (argc - 1) * sizeof(char *));
	execv(newArgv[0], newArgv);
	return 0;
}
