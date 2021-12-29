#include <stdint.h>
#include <string.h>
#define EsCRTmemcpy memcpy
#define EsCRTmemset memset
#define EsCRTabs abs
#define STB_IMAGE_IMPLEMENTATION
#include "../shared/stb_image.h"

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <path>\n", argv[0]);
		exit(1);
	}

	int width, height, channels;
	uint32_t *data = (uint32_t *) stbi_load(argv[1], &width, &height, &channels, 4);

	if (!data) {
		fprintf(stderr, "Error: Could not load \"%s\".\n", argv[1]);
		exit(1);
	}

	int rx0 = -1, rx1, ry0, ry1;

	for (int y = 0; y < height; y++) {
		for (int x0 = 0; x0 < width - 3; x0++) {
			if ((data[y * width + x0 + 0] & 0xFF) == 0x12
					&& (data[y * width + x0 + 1] & 0xFF) == 0x34
					&& (data[y * width + x0 + 2] & 0xFF) == 0x56
					&& (data[y * width + x0 + 3] & 0xFF) == 0x78) {
				for (int x1 = x0 + 4; x1 < width - 3; x1++) {
					if ((data[y * width + x1 + 3] & 0xFF) == 0x12
							&& (data[y * width + x1 + 2] & 0xFF) == 0x34
							&& (data[y * width + x1 + 1] & 0xFF) == 0x56
							&& (data[y * width + x1 + 0] & 0xFF) == 0x78) {
						rx0 = x0, rx1 = x1 + 4, ry0 = y, ry1 = height;

						// If a bottom marker is not found, assume the log spans the rest of the height of the image.
						// (Some emulators might round the corners of the display, thus missing off a few pixels.)
						
						for (int y1 = y + 1; y1 < height; y1++) {
							if ((data[y1 * width + x1 + 3] & 0xFF) == 0x12
									&& (data[y1 * width + x1 + 2] & 0xFF) == 0x34
									&& (data[y1 * width + x1 + 1] & 0xFF) == 0x56
									&& (data[y1 * width + x1 + 0] & 0xFF) == 0x78) {
								ry1 = y1 + 1;
								break;
							}
						}

						goto foundMarkers;
					}
				}
			}
		}
	}

	foundMarkers:;

	if (rx0 == -1) {
		fprintf(stderr, "Error: Could not find log markers.\n");
		exit(1);
	}

	uint8_t *output = (uint8_t *) malloc((rx1 - rx0) * (ry1 - ry0) * 2);
	uintptr_t position = 0;

	for (int y = ry0; y < ry1; y++) {
		for (int x = rx0; x < rx1; x++) {
			output[position++] = (data[y * width + x] & 0xFF0000) >> 16;
			output[position++] = (data[y * width + x] & 0xFF00) >> 8;
		}
	}

	printf("%s\n", output);

	free(data);

	return 0;
}
