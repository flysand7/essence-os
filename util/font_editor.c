// TODO Required: final file format, line metrics, horizontally scrolling kerning editor. 
// TODO Extensions: binary search, shifting glyphs in editor, undo/redo.

#define UI_IMPLEMENTATION
#define UI_LINUX
#include "luigi.h"

#include <stdio.h>

#define BITS_WIDTH (50)
#define BITS_HEIGHT (50)
#define ZOOM (18)

typedef struct FileHeader {
	uint16_t glyphCount;
	uint8_t headerBytes, glyphHeaderBytes;
	// Followed by glyphCount copies of FileGlyphHeader, sorted by codepoint.
} FileHeader;

typedef struct FileKerningEntry {
	uint32_t rightCodepoint;
	int16_t xOffset;
	uint16_t _unused0;
} FileKerningEntry;

typedef struct FileGlyphHeader {
	uint32_t bitsOffset; // Stored one row after another; each row is padded to a multiple of 8 bits.
	uint32_t codepoint;
	int16_t xOrigin, yOrigin, xAdvance;
	uint16_t kerningEntryCount; // Stored after the bits. Not necessarily aligned!
	uint16_t bitsWidth, bitsHeight;
} FileGlyphHeader;

typedef struct Kerning {
	int number, xOffset;
} Kerning;

typedef struct Glyph {
	int number;
	int xOrigin, yOrigin, xAdvance;
	int x0, x1, y0, y1; // Tight bounding box.
	uint8_t bits[BITS_HEIGHT][BITS_WIDTH];
	Kerning *kerningArray; // When this glyph is on the left.
	size_t kerningCount;
} Glyph;

UITable *glyphsTable;
UIWindow *window;
UITabPane *tabPane;
UIElement *editor;
UIElement *kerning;
UITextbox *previewText;
Glyph *glyphsArray;
size_t glyphCount;
intptr_t selectedGlyph = -1;
int selectedPixelX, selectedPixelY;
int selectedPairX, selectedPairY, selectedPairI, selectedPairJ;
char *path;

void Save(void *cp) {
	FILE *f = fopen(path, "wb");

	if (f) {
		FileHeader header = { 
			.glyphCount = glyphCount,
			.headerBytes = sizeof(FileHeader),
			.glyphHeaderBytes = sizeof(FileGlyphHeader),
		};

		fwrite(&header, 1, sizeof(header), f);

		uint32_t bitsOffset = sizeof(FileHeader) + glyphCount * sizeof(FileGlyphHeader);

		for (uintptr_t i = 0; i < glyphCount; i++) {
			FileGlyphHeader glyphHeader = { 
				.bitsOffset = bitsOffset,
				.codepoint = glyphsArray[i].number,
				.xOrigin = glyphsArray[i].xOrigin - glyphsArray[i].x0,
				.yOrigin = glyphsArray[i].yOrigin - glyphsArray[i].y0,
				.xAdvance = glyphsArray[i].xAdvance,
				.bitsWidth = glyphsArray[i].x1 - glyphsArray[i].x0 + 1,
				.bitsHeight = glyphsArray[i].y1 - glyphsArray[i].y0 + 1,
				.kerningEntryCount = glyphsArray[i].kerningCount,
			};

			fwrite(&glyphHeader, 1, sizeof(glyphHeader), f);
			bitsOffset += ((glyphHeader.bitsWidth + 7) >> 3) * glyphHeader.bitsHeight + sizeof(FileKerningEntry) * glyphHeader.kerningEntryCount;
		}

		for (uintptr_t i = 0; i < glyphCount; i++) {
			Glyph *g = &glyphsArray[i];

			for (int y = g->y0; y <= g->y1; y++) {
				for (int x = g->x0; x <= g->x1; x += 8) {
					uint8_t b = 0;

					for (int xb = 0; xb < 8; xb++) {
						if (x + xb <= g->x1) {
							b |= (uint8_t) g->bits[y][x + xb] << xb;
						}
					}

					fwrite(&b, 1, 1, f);
				}
			}

			for (uintptr_t i = 0; i < g->kerningCount; i++) {
				FileKerningEntry entry = { 0 };
				entry.rightCodepoint = g->kerningArray[i].number;
				entry.xOffset = g->kerningArray[i].xOffset;
				fwrite(&entry, 1, sizeof(FileKerningEntry), f);
			}
		}

		fclose(f);
	} else {
		UIDialogShow(window, 0, "Could not save the file.\n%f%b", "OK");
	}
}

void Load() {
	FILE *f = fopen(path, "rb");

	if (f) {
		FileHeader header;
		fread(&header, 1, sizeof(header), f);
		if (ferror(f)) goto end;
		glyphCount = header.glyphCount;
		glyphsArray = (Glyph *) calloc(glyphCount, sizeof(Glyph));
		if (!glyphsArray) goto end;

		for (uintptr_t i = 0; i < glyphCount; i++) {
			FileGlyphHeader glyphHeader;
			fread(&glyphHeader, 1, sizeof(glyphHeader), f);
			if (ferror(f)) goto end;
			if (glyphHeader.bitsWidth >= BITS_WIDTH || glyphHeader.bitsHeight >= BITS_HEIGHT) goto end;
			Glyph *g = &glyphsArray[i];
			g->number = glyphHeader.codepoint;
			g->x0 = BITS_WIDTH / 2 - glyphHeader.bitsWidth / 2;
			g->x1 = g->x0 + glyphHeader.bitsWidth - 1;
			g->y0 = BITS_HEIGHT / 2 - glyphHeader.bitsHeight / 2;
			g->y1 = g->y0 + glyphHeader.bitsHeight - 1;
			g->xOrigin = glyphHeader.xOrigin + g->x0;
			g->yOrigin = glyphHeader.yOrigin + g->y0;
			g->xAdvance = glyphHeader.xAdvance;
			g->kerningCount = glyphHeader.kerningEntryCount;
			g->kerningArray = (Kerning *) malloc(sizeof(Kerning) * glyphHeader.kerningEntryCount);
			if (!g->kerningArray) goto end;
			off_t position = ftell(f);
			fseek(f, glyphHeader.bitsOffset, SEEK_SET);
			if (ferror(f)) goto end;
			size_t bytesPerLine = (glyphHeader.bitsWidth + 7) >> 3;
			uint8_t *bits = (uint8_t *) malloc(bytesPerLine * glyphHeader.bitsHeight);
			if (!bits) goto end;
			fread(bits, 1, bytesPerLine * glyphHeader.bitsHeight, f);

			for (int i = 0; i < glyphHeader.bitsHeight; i++) {
				for (int j = 0; j < glyphHeader.bitsWidth; j++) {
					g->bits[i + g->y0][j + g->x0] = (bits[i * bytesPerLine + j / 8] & (1 << (j % 8))) ? 1 : 0;
				}
			}

			for (uintptr_t i = 0; i < g->kerningCount; i++) {
				FileKerningEntry entry;
				fread(&entry, 1, sizeof(FileKerningEntry), f);
				if (ferror(f)) goto end;
				g->kerningArray[i].number = entry.rightCodepoint;
				g->kerningArray[i].xOffset = entry.xOffset;
			}

			if (ferror(f)) { free(bits); goto end; }
			free(bits);
			fseek(f, position, SEEK_SET);
		}
		
		end:;

		if (ferror(f)) {
			UIDialogShow(window, 0, "Could not load the file.\n%f%b", "OK");

			for (uintptr_t i = 0; i < glyphCount; i++) {
				free(glyphsArray[i].kerningArray);
			}

			glyphCount = 0;
			free(glyphsArray);
			glyphsArray = NULL;
		}

		fclose(f);
	}

	glyphsTable->itemCount = glyphCount;
	UITableResizeColumns(glyphsTable);
	UIElementRefresh(&glyphsTable->e);
}

int CompareGlyphs(const void *_a, const void *_b) {
	Glyph *a = (Glyph *) _a, *b = (Glyph *) _b;
	return a->number < b->number ? -1 : a->number != b->number;
}

int CompareKernings(const void *_a, const void *_b) {
	Kerning *a = (Kerning *) _a, *b = (Kerning *) _b;
	return a->number < b->number ? -1 : a->number != b->number;
}

void AddGlyph(void *cp) {
	char *number = NULL;
	UIDialogShow(window, 0, "Enter the glyph number:\n%t\n%f%b", &number, "Add");
	Glyph g = { 0 };
	g.number = atoi(number);
	free(number);
	glyphsTable->itemCount = ++glyphCount;
	glyphsArray = realloc(glyphsArray, sizeof(Glyph) * glyphCount);
	glyphsArray[glyphCount - 1] = g;
	qsort(glyphsArray, glyphCount, sizeof(Glyph), CompareGlyphs);
	selectedGlyph = -1;
	UITableResizeColumns(glyphsTable);
	UIElementRefresh(&glyphsTable->e);
	UIElementRefresh(editor);
	UIElementRefresh(kerning);
}

void DeleteGlyph(void *cp) {
	if (selectedGlyph == -1) return;
	memmove(glyphsArray + selectedGlyph, glyphsArray + selectedGlyph + 1, sizeof(Glyph) * (glyphCount - selectedGlyph - 1));
	selectedGlyph = -1;
	glyphsTable->itemCount = --glyphCount;
	glyphsArray = realloc(glyphsArray, sizeof(Glyph) * glyphCount);
	UITableResizeColumns(glyphsTable);
	UIElementRefresh(&glyphsTable->e);
	UIElementRefresh(editor);
	UIElementRefresh(kerning);
}

int GlyphsTableMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_TABLE_GET_ITEM) {
		UITableGetItem *m = (UITableGetItem *) dp;
		m->isSelected = selectedGlyph == m->index;

		if (m->column == 0) {
			return snprintf(m->buffer, m->bufferBytes, "%c", glyphsArray[m->index].number);
		} else if (m->column == 1) {
			return snprintf(m->buffer, m->bufferBytes, "%d", glyphsArray[m->index].number);
		}
	} else if (message == UI_MSG_LEFT_DOWN || message == UI_MSG_MOUSE_DRAG) {
		int index = UITableHitTest((UITable *) element, element->window->cursorX, element->window->cursorY);

		if (index != -1) {
			selectedGlyph = index;
			tabPane->active = 1;
			UIElementMessage(&tabPane->e, UI_MSG_LAYOUT, 0, 0);
			UIElementRepaint(&tabPane->e, NULL);
		}
	}

	return 0;
}

void DrawGlyph(UIPainter *painter, Glyph *g, int _x, int _y) {
	for (int y = g->y0; y <= g->y1; y++) {
		for (int x = g->x0; x <= g->x1; x++) {
			if (g->bits[y][x]) {
				UIDrawBlock(painter, UI_RECT_4(_x + x - g->xOrigin, _x + x + 1 - g->xOrigin, _y + y - g->yOrigin, _y + y + 1 - g->yOrigin), 0xFF000000);
			}
		}
	}
}

void SetOrigin(void *cp) {
	Glyph *g = &glyphsArray[selectedGlyph];
	g->xOrigin = selectedPixelX;
	g->yOrigin = selectedPixelY;
	UIElementRepaint(editor, NULL);
}

void SetXAdvance(void *cp) {
	Glyph *g = &glyphsArray[selectedGlyph];
	g->xAdvance = selectedPixelX - g->xOrigin;
	UIElementRepaint(editor, NULL);
}

int GetAdvance(int leftGlyph, int rightGlyph, bool *hasKerningEntry) {
	if (hasKerningEntry) *hasKerningEntry = false;
	int p = glyphsArray[leftGlyph].xAdvance;

	if (rightGlyph != -1) {
		// TODO Binary search.

		for (uintptr_t i = 0; i < glyphsArray[leftGlyph].kerningCount; i++) {
			if (glyphsArray[leftGlyph].kerningArray[i].number == glyphsArray[rightGlyph].number) {
				p += glyphsArray[leftGlyph].kerningArray[i].xOffset;
				if (hasKerningEntry) *hasKerningEntry = true;
				break;
			}
		}
	}

	return p;
}

void DrawPreviewText(UIPainter *painter, UIElement *element, Glyph *g) {
	UIDrawBlock(painter, UI_RECT_4(element->clip.r - 100, element->clip.r, element->clip.t, element->clip.t + 50), 0xFFFFFFFF);

	if (previewText->bytes == 0 && g) {
		DrawGlyph(painter, g, element->clip.r - 100 + 5, element->clip.t + 25);
		return;
	}

	int px = 0;
	int previous = -1;

	for (int i = 0; i < previewText->bytes; i++) {
		// TODO Binary search.

		for (uintptr_t j = 0; j < glyphCount; j++) {
			if (glyphsArray[j].number == previewText->string[i]) {
				if (previous != -1) px += GetAdvance(previous, j, NULL);
				DrawGlyph(painter, &glyphsArray[j], element->clip.r - 100 + 5 + px, element->clip.t + 25);
				previous = j;
				break;
			}
		}
	}
}

int GlyphEditorMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_PAINT) {
		UIPainter *painter = (UIPainter *) dp;
		UIDrawBlock(painter, element->bounds, 0xD0D1D4);

		if (selectedGlyph >= 0 && selectedGlyph < (intptr_t) glyphCount) {
			Glyph *g = &glyphsArray[selectedGlyph];

			for (int y = 0; y < BITS_HEIGHT; y++) {
				for (int x = 0; x < BITS_WIDTH; x++) {
					UIRectangle rectangle = UIRectangleAdd(element->bounds, UI_RECT_2(x * ZOOM, y * ZOOM));
					rectangle.r = rectangle.l + ZOOM, rectangle.b = rectangle.t + ZOOM;

					if (g->bits[y][x]) {
						UIDrawBlock(painter, rectangle, 0xFF000000);
					} else {
						UIDrawBorder(painter, rectangle, 0xFF000000, UI_RECT_4(1, 0, 1, 0));
					}

					if (g->xOrigin == x && g->yOrigin == y) {
						UIDrawBorder(painter, rectangle, 0xFFFF0000, UI_RECT_1(2));
					}

					if (g->xOrigin + g->xAdvance == x && g->yOrigin == y) {
						UIDrawBorder(painter, rectangle, 0xFF0099FF, UI_RECT_1(2));
					}
				}
			}

			DrawPreviewText(painter, element, g);
		}
	} else if (message == UI_MSG_MIDDLE_UP) {
		if (selectedGlyph >= 0 && selectedGlyph < (intptr_t) glyphCount) {
			int mx = (window->cursorX - element->bounds.l) / ZOOM;
			int my = (window->cursorY - element->bounds.t) / ZOOM;

			if (mx >= 0 && my >= 0 && mx < BITS_WIDTH && my < BITS_HEIGHT) {
				selectedPixelX = mx;
				selectedPixelY = my;
				UIMenu *menu = UIMenuCreate(&window->e, UI_MENU_NO_SCROLL);
				UIMenuAddItem(menu, 0, "Set origin", -1, SetOrigin, element);
				UIMenuAddItem(menu, 0, "Set X advance", -1, SetXAdvance, element);
				UIMenuShow(menu);
			}
		}
	} else if (message == UI_MSG_MOUSE_DRAG || message == UI_MSG_LEFT_DOWN || message == UI_MSG_RIGHT_DOWN) {
		if (selectedGlyph >= 0 && selectedGlyph < (intptr_t) glyphCount && (window->pressedButton == 1 || window->pressedButton == 3)) {
			Glyph *g = &glyphsArray[selectedGlyph];
			int mx = (window->cursorX - element->bounds.l) / ZOOM;
			int my = (window->cursorY - element->bounds.t) / ZOOM;

			if (mx >= 0 && my >= 0 && mx < BITS_WIDTH && my < BITS_HEIGHT) {
				g->bits[my][mx] = window->pressedButton == 1;

				g->x0 = 0;
				g->x1 = 0;
				g->y0 = 0;
				g->y1 = 0;

				bool first = true;

				for (int y = 0; y < BITS_HEIGHT; y++) {
					for (int x = 0; x < BITS_WIDTH; x++) {
						if (g->bits[y][x]) {
							if (first) {
								g->x0 = x;
								g->x1 = x;
								g->y0 = y;
								g->y1 = y;
								first = false;
							} else {
								if (x < g->x0) g->x0 = x;
								if (x > g->x1) g->x1 = x;
								if (y < g->y0) g->y0 = y;
								if (y > g->y1) g->y1 = y;
							}
						}
					}
				}

				UIElementRepaint(element, NULL);
			}
		}
	}

	return 0;
}

int KerningEditorMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_PAINT) {
		UIPainter *painter = (UIPainter *) dp;
		UIDrawBlock(painter, element->bounds, 0xD0D1D4);

		int x = element->bounds.l + 20, y = element->bounds.t + 20;

		selectedPairI = -1, selectedPairJ = -1;

		for (uintptr_t i = 0; i < glyphCount; i++) {
			for (uintptr_t j = 0; j < glyphCount; j++) {
				bool hasKerningEntry = false;

				DrawGlyph(painter, &glyphsArray[j], x, y);
				DrawGlyph(painter, &glyphsArray[i], x + GetAdvance(j, i, &hasKerningEntry), y);

				UIRectangle border = UI_RECT_4(x - 5, x + 20, y - 15, y + 5);

				if (hasKerningEntry) {
					UIDrawBorder(painter, border, 0xFF0099FF, UI_RECT_1(1));
				}

				if (selectedPairX == (x - 20 - element->bounds.l) / 25 && selectedPairY == (y - 20 - element->bounds.t) / 20) {
					UIDrawBorder(painter, border, 0xFF000000, UI_RECT_1(1));
					selectedPairI = i, selectedPairJ = j;
				}

				x += 25;
			}

			x = element->bounds.l + 20;
			y += 20;
		}

		DrawPreviewText(painter, element, NULL);
	} else if (message == UI_MSG_GET_HEIGHT) {
		return 20 * glyphCount + 40;
	} else if (message == UI_MSG_LEFT_DOWN || message == UI_MSG_RIGHT_DOWN) {
		int delta = message == UI_MSG_LEFT_DOWN ? 1 : -1;

		if (selectedPairI >= 0 && selectedPairI < (int) glyphCount && selectedPairJ >= 0 && selectedPairJ < (int) glyphCount) {
			int j = selectedPairJ;
			bool found = false;

			for (uintptr_t i = 0; i < glyphsArray[j].kerningCount; i++) {
				if (glyphsArray[j].kerningArray[i].number == glyphsArray[selectedPairI].number) {
					glyphsArray[j].kerningArray[i].xOffset += delta;

					if (!glyphsArray[j].kerningArray[i].xOffset) {
						memmove(glyphsArray[j].kerningArray + i, glyphsArray[j].kerningArray + i + 1, 
								sizeof(Kerning) * (glyphsArray[j].kerningCount - i - 1));
						glyphsArray[j].kerningCount--;
					}

					found = true;
					break;
				}
			}

			if (!found) {
				glyphsArray[j].kerningCount++;
				glyphsArray[j].kerningArray = (Kerning *) realloc(glyphsArray[j].kerningArray, sizeof(Kerning) * glyphsArray[j].kerningCount);
				glyphsArray[j].kerningArray[glyphsArray[j].kerningCount - 1].number = glyphsArray[selectedPairI].number;
				glyphsArray[j].kerningArray[glyphsArray[j].kerningCount - 1].xOffset = delta; 
				qsort(glyphsArray[j].kerningArray, glyphsArray[j].kerningCount, sizeof(Kerning), CompareKernings);
			}

			UIElementRepaint(element, NULL);
		}
	} else if (message == UI_MSG_MOUSE_MOVE) {
		int pairX = (window->cursorX - element->bounds.l - 20 + 5) / 25;
		int pairY = (window->cursorY - element->bounds.t - 20 + 15) / 20;

		if (pairX != selectedPairX || pairY != selectedPairY) {
			selectedPairX = pairX;
			selectedPairY = pairY;
			UIElementRepaint(element, NULL);
		}
	}

	return 0;
}

int PreviewTextMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_VALUE_CHANGED) {
		UIElementRepaint(editor, NULL);
		UIElementRepaint(kerning, NULL);
	}

	return 0;
}

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <path to font file>\n", argv[0]);
		exit(1);
	}

	path = argv[1];

	UIInitialise();
	window = UIWindowCreate(0, UI_ELEMENT_PARENT_PUSH, "Font Editor", 1024, 768);
	UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_EXPAND);

	UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_GRAY | UI_PANEL_HORIZONTAL | UI_PANEL_MEDIUM_SPACING);
		UIButtonCreate(0, 0, "Save", -1)->invoke = Save;
		UIButtonCreate(0, 0, "Add glyph", -1)->invoke = AddGlyph;
		UIButtonCreate(0, 0, "Delete glyph", -1)->invoke = DeleteGlyph;
		UISpacerCreate(0, UI_ELEMENT_H_FILL, 0, 0);
		UILabelCreate(0, 0, "Preview text:", -1);
		previewText = UITextboxCreate(0, 0);
		previewText->e.messageUser = PreviewTextMessage;
	UIParentPop();

	tabPane = UITabPaneCreate(0, UI_ELEMENT_PARENT_PUSH | UI_ELEMENT_V_FILL, "Select glyph\tEdit\tKerning");
	glyphsTable = UITableCreate(0, 0, "ASCII\tNumber");
	glyphsTable->e.messageUser = GlyphsTableMessage;
	editor = UIElementCreate(sizeof(UIElement), 0, 0, GlyphEditorMessage, "Glyph editor");
	kerning = UIElementCreate(sizeof(UIElement), &UIPanelCreate(0, UI_PANEL_SCROLL)->e, UI_ELEMENT_H_FILL, KerningEditorMessage, "Kerning editor");

	UIWindowRegisterShortcut(window, (UIShortcut) { .code = UI_KEYCODE_LETTER('S'), .ctrl = true, .invoke = Save });

	Load();

	return UIMessageLoop();
}
