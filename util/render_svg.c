// Expects icons-master/ to contain the contents of https://github.com/elementary/icons.
// You can find the license for the elementary Icon pack in "res/Icons/elementary Icons License.txt".
// The utility produces "res/Icons/elementary Icons.icon_pack". This is a pre-parsed format of the SVG files.

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <limits.h>

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#include "stb_ds.h"

void BlendPixel(uint32_t *a, uint32_t b, bool c) {}
#define EsCRTsqrtf sqrtf
#define EsAssert assert
#define EsHeapAllocate(x, y) ((y) ? calloc(1, (x)) : malloc((x)))
#define EsHeapFree free
#define EsCRTfabsf fabsf
#define EsCRTfmodf fmodf
#define EsCRTisnanf isnan
#define EsCRTfloorf floorf
#define AbsoluteFloat fabsf
#define EsCRTatan2f atan2f
#define EsCRTsinf sinf
#define EsCRTcosf cosf
#define EsCRTacosf acosf
#define EsCRTceilf ceilf
#define ES_INFINITY INFINITY
#define IN_DESIGNER
#include "../desktop/renderer.cpp"

const char *metadataFiles[] = {
	"COPYING",
	"README.md",
};

uint32_t checkShape = 0x12, checkPath = 0x34, zero = 0;

void OutputPaint(NSVGpaint paint, FILE *pack) {
	fwrite(&paint.type, 1, sizeof(char), pack);

	if (paint.type == NSVG_PAINT_COLOR) {
		fwrite(&paint.color, 1, sizeof(unsigned int), pack);
	} else if (paint.type == NSVG_PAINT_LINEAR_GRADIENT || paint.type == NSVG_PAINT_RADIAL_GRADIENT) {
		fwrite(&paint.gradient->xform, 1, 6 * sizeof(float), pack);

		{
			char spread = 0;

			if (paint.gradient->spread == NSVG_SPREAD_PAD) {
				spread = RAST_REPEAT_CLAMP;
			} else if (paint.gradient->spread == NSVG_SPREAD_REFLECT) {
				spread = RAST_REPEAT_MIRROR;
			} else if (paint.gradient->spread == NSVG_SPREAD_REPEAT) {
				spread = RAST_REPEAT_NORMAL;
			}

			fwrite(&spread, 1, sizeof(char), pack);
		}

		fwrite(&paint.gradient->fx, 1, sizeof(float), pack);
		fwrite(&paint.gradient->fy, 1, sizeof(float), pack);
		fwrite(&paint.gradient->nstops, 1, sizeof(int), pack);

		for (int i = 0; i < paint.gradient->nstops; i++) {
			fwrite(&paint.gradient->stops[i].color, 1, sizeof(unsigned int), pack);
			fwrite(&paint.gradient->stops[i].offset, 1, sizeof(float), pack);
		}
	}
}

void OutputPath(NSVGpath *path, FILE *pack) {
	next:;
	fwrite(&checkPath, 1, sizeof(uint8_t), pack);

	fwrite(&path->npts, 1, sizeof(int), pack);
	fwrite(&path->closed, 1, sizeof(char), pack);

	for (int i = 0; i < path->npts; i++) {
		fwrite(&path->pts[i * 2 + 0], 1, sizeof(float), pack);
		fwrite(&path->pts[i * 2 + 1], 1, sizeof(float), pack);
	}

	path = path->next;
	if (path) goto next;
	else fwrite(&zero, 1, sizeof(uint8_t), pack);
}

void OutputShape(NSVGshape *shape, FILE *pack) {
	next:;

	if (!shape) {
		fwrite(&zero, 1, sizeof(uint8_t), pack);
		return;
	}

	if (shape->flags & NSVG_FLAGS_VISIBLE) {
		fwrite(&checkShape, 1, sizeof(uint8_t), pack);

		fwrite(&shape->opacity, 1, sizeof(float), pack);
		fwrite(&shape->strokeWidth, 1, sizeof(float), pack);
		fwrite(&shape->strokeDashOffset, 1, sizeof(float), pack);
		fwrite(&shape->strokeDashArray, 1, 8 * sizeof(float), pack);
		fwrite(&shape->strokeDashCount, 1, sizeof(char), pack);

		{
			char join = 0;

			if (shape->strokeLineJoin == NSVG_JOIN_MITER) {
				join = RAST_LINE_JOIN_MITER;
			} else if (shape->strokeLineJoin == NSVG_JOIN_ROUND) {
				join = RAST_LINE_JOIN_ROUND;
			} else if (shape->strokeLineJoin == NSVG_JOIN_BEVEL) {
				join = RAST_LINE_JOIN_MITER;
				shape->miterLimit = 0;
			}

			fwrite(&join, 1, sizeof(char), pack);
		}

		{
			char cap = 0;

			if (shape->strokeLineCap == NSVG_CAP_BUTT) {
				cap = RAST_LINE_CAP_FLAT;
			} else if (shape->strokeLineCap == NSVG_CAP_ROUND) {
				cap = RAST_LINE_CAP_ROUND;
			} else if (shape->strokeLineCap == NSVG_CAP_SQUARE) {
				cap = RAST_LINE_CAP_SQUARE;
			}

			fwrite(&cap, 1, sizeof(char), pack);
		}

		fwrite(&shape->miterLimit, 1, sizeof(float), pack);
		fwrite(&shape->fillRule, 1, sizeof(char), pack);

		OutputPaint(shape->fill, pack);
		OutputPaint(shape->stroke, pack);
		OutputPath(shape->paths, pack);
	}

	shape = shape->next;
	goto next;
}

void OutputImage(NSVGimage *image, FILE *pack) {
	fwrite(&image->width, 1, sizeof(float), pack);
	fwrite(&image->height, 1, sizeof(float), pack);
	OutputShape(image->shapes, pack);
}

char buffer[65536];

typedef struct Icon {
	char group[128], name[128];
	bool metadata;
} Icon;

void GenerateMainIconPack() {
	Icon *icons = NULL;

	{
		FILE *f = fopen("desktop/icons.header", "wb");
		fprintf(f, "%s", "inttype EsStandardIcon enum none { // Taken from the elementary icon pack, see res/Icons for license.\n\tES_ICON_NONE\n");
		fclose(f);
	}

	{
		system("find bin/icons-master/ -name *.svg -type f | grep -v cursors | awk -F '/' 'BEGIN { OFS=\" \" } { print $3, $5 }' | awk -F '.' '{ print $1 }'"
				" | tr '-' '_' | grep -v '_rtl' | tr '_' '-' | sort -u > icons.txt");
		system("awk -F ' ' 'BEGIN { OFS=\"\" } { print \"\tES_ICON_\", $2 }' icons.txt | tr '[:lower:]' '[:upper:]'"
				" | tr '+-' '__' >> desktop/icons.header");
		system("echo '}' >> desktop/icons.header");

		FILE *iconList = fopen("icons.txt", "rb");
		Icon icon = {};

		while (fscanf(iconList, "%s %s\n", icon.group, icon.name) == 2) {
			arrput(icons, icon);
		}

		fclose(iconList);
		system("rm icons.txt");

		for (uintptr_t i = 0; i < sizeof(metadataFiles) / sizeof(metadataFiles[0]); i++) {
			icon.metadata = true;
			strcpy(icon.name, metadataFiles[i]);
			arrput(icons, icon);
		}
	}

	FILE *pack = fopen("res/Themes/elementary Icons.dat", "wb");
	uint32_t zero = 0;

	uint32_t count = arrlen(icons);
	fwrite(&count, 1, sizeof(uint32_t), pack);
	for (int i = 0; i < arrlen(icons); i++) fwrite(&zero, 1, sizeof(uint32_t), pack);

	int total = 0;

	for (int i = 0; i < arrlen(icons); i++) {
		Icon *icon = icons + i;
		uint32_t offset = ftell(pack);
		fseek(pack, (i + 1) * sizeof(uint32_t), SEEK_SET);
		fwrite(&offset, 1, sizeof(uint32_t), pack);
		fseek(pack, offset, SEEK_SET);

		if (icon->metadata) {
			sprintf(buffer, "bin/icons-master/%s", icon->name);
			FILE *f = fopen(buffer, "rb");
			fwrite(buffer, 1, fread(buffer, 1, 65536, f), pack);
			fclose(f);
		} else {
			printf("%s %s\n", icon->group, icon->name);

			uint32_t variants[] = { 1 /* Symbolic */, 16, 21, 24, 32, 48, 64, 128, 
				1 | 0x8000, 16 | 0x8000, 21 | 0x8000, 24 | 0x8000, 32 | 0x8000, 48 | 0x8000, 64 | 0x8000, 128 | 0x8000 };

			for (uint32_t i = 0; i < sizeof(variants) / sizeof(variants[0]); i++) {
				if ((variants[i] & 0x7FFF) != 1) {
					sprintf(buffer, "bin/icons-master/%s/%d/%s%s.svg", icon->group, variants[i] & 0x7FFF, icon->name, (variants[i] & 0x8000) ? "-rtl" : "");
				} else {
					sprintf(buffer, "bin/icons-master/%s/symbolic/%s%s.svg", icon->group, icon->name, (variants[i] & 0x8000) ? "-rtl" : "");
				}

				NSVGimage *image = nsvgParseFromFile(buffer, "px", 96.0f);

				if (image) {
					printf("\t%s\n", buffer);
					total++;

					fwrite(&variants[i], 1, sizeof(uint32_t), pack);
					uint32_t skip = ftell(pack);
					fwrite(&zero, 1, sizeof(uint32_t), pack);
					OutputImage(image, pack);
					nsvgDelete(image);
					uint32_t restore = ftell(pack);
					fseek(pack, skip, SEEK_SET);
					fwrite(&restore, 1, sizeof(uint32_t), pack);
					fseek(pack, restore, SEEK_SET);
				} else {
					// printf("\tcould not find %s\n", buffer);
				}
			}

			fwrite(&zero, 1, sizeof(uint32_t), pack);
		}
	}

	fclose(pack);
	printf("total = %d\n", total);
}

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <command> <options...>\n", argv[0]);
		return 1;
	}

	if (0 == strcmp(argv[1], "generate-main-icon-pack")) {
		GenerateMainIconPack();
	} else if (0 == strcmp(argv[1], "convert")) {
		if (argc != 4) {
			fprintf(stderr, "Usage: %s convert <input> <output>\n", argv[0]);
			return 1;
		}

		NSVGimage *image = nsvgParseFromFile(argv[2], "px", 96.0f);
		FILE *f = fopen(argv[3], "wb");

		if (!image) { fprintf(stderr, "Error: Could not access/parse file '%s'.\n", argv[2]); return 1; }
		if (!f) { fprintf(stderr, "Error: Could not access/parse file '%s'.\n", argv[3]); return 1; }

		OutputImage(image, f);
	}

	return 0;
}
