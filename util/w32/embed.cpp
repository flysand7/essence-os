#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *files[] = {
	"res/Fonts/Inter Thin.otf",
	"res/Fonts/Inter Thin Italic.otf",
	"res/Fonts/Inter Extra Light.otf",
	"res/Fonts/Inter Extra Light Italic.otf",
	"res/Fonts/Inter Light.otf",
	"res/Fonts/Inter Light Italic.otf",
	"res/Fonts/Inter Regular.otf",
	"res/Fonts/Inter Regular Italic.otf",
	"res/Fonts/Inter Medium.otf",
	"res/Fonts/Inter Medium Italic.otf",
	"res/Fonts/Inter Semi Bold.otf",
	"res/Fonts/Inter Semi Bold Italic.otf",
	"res/Fonts/Inter Bold.otf",
	"res/Fonts/Inter Bold Italic.otf",
	"res/Fonts/Inter Extra Bold.otf",
	"res/Fonts/Inter Extra Bold Italic.otf",
	"res/Fonts/Inter Black.otf",
	"res/Fonts/Inter Black Italic.otf",
	"res/Fonts/Hack Regular.ttf",
	"res/Fonts/Hack Regular Italic.ttf",
	"res/Fonts/Hack Bold.ttf",
	"res/Fonts/Hack Bold Italic.ttf",
	"res/Theme.dat",
	"res/Icons/elementary Icons.icon_pack",
	nullptr,
};

int main() {
	FILE *f = fopen("bin/embed.dat", "wb");

	for (uintptr_t i = 0; files[i]; i++) {
		FILE *in = fopen(files[i], "rb");
		fseek(in, 0, SEEK_END);
		size_t size = ftell(in);
		fseek(in, 0, SEEK_SET);
		void *buffer = malloc(size);
		fread(buffer, 1, size, in);
		fwrite(files[i], 1, strlen(files[i]) + 1, f);
		fwrite(&size, 1, 4, f);
		fwrite(buffer, 1, size, f);
		fclose(in);
		free(buffer);
	}

	return 0;
}
