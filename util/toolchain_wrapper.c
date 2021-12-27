#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#define _STRING(x) #x
#define STRING(x) _STRING(x)

int main(int argc, char **argv) {
	char buffer[PATH_MAX] = {}, sysroot[PATH_MAX] = {}, tool[PATH_MAX] = {};
	readlink("/proc/self/exe", buffer, sizeof(buffer));

	int directoryPosition = 0;
	for (int i = 0; buffer[i]; i++) if (buffer[i] == '/') directoryPosition = i;
	buffer[directoryPosition] = 0;
	for (int i = 0; buffer[i]; i++) if (buffer[i] == '/') directoryPosition = i;
	buffer[directoryPosition] = 0;
	for (int i = 0; buffer[i]; i++) if (buffer[i] == '/') directoryPosition = i;
	buffer[directoryPosition] = 0;

	snprintf(sysroot, sizeof(sysroot), "%s/root/", buffer);
	snprintf(tool, sizeof(tool), "%s/cross/bin/%s", buffer, STRING(TOOL));
	char *toolEnd = tool + strlen(tool);

	char **newArgv = (char **) calloc(sizeof(char *), (argc + 16));
	int index = 0;
	newArgv[index++] = tool;

	if (0 == strcmp(toolEnd - 3, "g++") || 0 == strcmp(toolEnd - 3, "gcc") || 0 == strcmp(toolEnd - 2, "ld")) {
		newArgv[index++] = "--sysroot";
		newArgv[index++] = sysroot;
	}

	memcpy(newArgv + index, argv + 1, (argc - 1) * sizeof(char *));
	return execv(newArgv[0], newArgv);
}
