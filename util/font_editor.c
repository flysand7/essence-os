// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

// TODO Extensions: binary search, shifting glyphs in editor, undo/redo.

#define UI_IMPLEMENTATION
#define UI_LINUX
#include "luigi.h"

#include <stdio.h>

#include "../shared/bitmap_font.h"

#define BITS_WIDTH (50)
#define BITS_HEIGHT (50)
#define ZOOM (18)

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
UITextbox *previewText, *yAscentTextbox, *yDescentTextbox, *xEmWidthTextbox;
UIScrollBar *kerningHScroll, *kerningVScroll;
Glyph *glyphsArray;
size_t glyphCount;
intptr_t selectedGlyph = -1;
int selectedPixelX, selectedPixelY;
int selectedPairX, selectedPairY, selectedPairI, selectedPairJ;
int yAscent, yDescent, xEmWidth;
char *path;

void Save(void *cp) {
	FILE *f = fopen(path, "wb");

	if (f) {
		BitmapFontHeader header = { 
			.signature = BITMAP_FONT_SIGNATURE,
			.glyphCount = glyphCount,
			.headerBytes = sizeof(BitmapFontHeader),
			.glyphBytes = sizeof(BitmapFontGlyph),
			.yAscent = yAscent,
			.yDescent = yDescent,
			.xEmWidth = xEmWidth,
		};

		fwrite(&header, 1, sizeof(header), f);

		uint32_t bitsOffset = sizeof(BitmapFontHeader) + glyphCount * sizeof(BitmapFontGlyph);

		for (uintptr_t i = 0; i < glyphCount; i++) {
			BitmapFontGlyph glyphHeader = { 
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
			bitsOffset += ((glyphHeader.bitsWidth + 7) >> 3) * glyphHeader.bitsHeight + sizeof(BitmapFontKerningEntry) * glyphHeader.kerningEntryCount;
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
				BitmapFontKerningEntry entry = { 0 };
				entry.rightCodepoint = g->kerningArray[i].number;
				entry.xOffset = g->kerningArray[i].xOffset;
				fwrite(&entry, 1, sizeof(BitmapFontKerningEntry), f);
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
		BitmapFontHeader header = { 0 };
		fread(&header, 1, 8, f);
		if (ferror(f)) goto end;
		fseek(f, 0, SEEK_SET);
		fread(&header, 1, header.headerBytes > sizeof(BitmapFontHeader) ? sizeof(BitmapFontHeader) : header.headerBytes, f);
		fseek(f, header.headerBytes, SEEK_SET);
		glyphCount = header.glyphCount;
		glyphsArray = (Glyph *) calloc(glyphCount, sizeof(Glyph));
		if (!glyphsArray) goto end;

		for (uintptr_t i = 0; i < glyphCount; i++) {
			BitmapFontGlyph glyphHeader;
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
				BitmapFontKerningEntry entry;
				fread(&entry, 1, sizeof(BitmapFontKerningEntry), f);
				if (ferror(f)) goto end;
				g->kerningArray[i].number = entry.rightCodepoint;
				g->kerningArray[i].xOffset = entry.xOffset;
			}

			if (ferror(f)) { free(bits); goto end; }
			free(bits);
			fseek(f, position, SEEK_SET);
		}

		char buffer[32];
		snprintf(buffer, sizeof(buffer), "%d", header.yAscent);
		UITextboxReplace(yAscentTextbox, buffer, -1, true);
		snprintf(buffer, sizeof(buffer), "%d", header.yDescent);
		UITextboxReplace(yDescentTextbox, buffer, -1, true);
		snprintf(buffer, sizeof(buffer), "%d", header.xEmWidth);
		UITextboxReplace(xEmWidthTextbox, buffer, -1, true);
		
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
	UIDialogShow(window, 0, "Enter the glyph number (base 16):\n%t\n%f%b", &number, "Add");
	Glyph g = { 0 };
	g.number = strtol(number, NULL, 16);
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
			if (glyphsArray[m->index].number < 256) {
				return snprintf(m->buffer, m->bufferBytes, "%c", glyphsArray[m->index].number);
			} else {
				return 0;
			}
		} else if (m->column == 1) {
			return snprintf(m->buffer, m->bufferBytes, "U+%.4X", glyphsArray[m->index].number);
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
	UIDrawBlock(painter, UI_RECT_4(element->bounds.r - 100, element->bounds.r, element->bounds.t, element->bounds.t + 50), 0xFFFFFFFF);
	UIDrawBlock(painter, UI_RECT_4(element->bounds.r - 100, element->bounds.r, element->bounds.t + 25 - yAscent, element->bounds.t + 26 - yAscent), 0xFF88FF88);
	UIDrawBlock(painter, UI_RECT_4(element->bounds.r - 100, element->bounds.r, element->bounds.t + 25, element->bounds.t + 26), 0xFF88FF88);
	UIDrawBlock(painter, UI_RECT_4(element->bounds.r - 100, element->bounds.r, element->bounds.t + 25 + yDescent, element->bounds.t + 26 + yDescent), 0xFF88FF88);

	if (previewText->bytes == 0 && g) {
		DrawGlyph(painter, g, element->bounds.r - 100 + 5, element->bounds.t + 25);
		return;
	}

	int px = 0;
	int previous = -1;

	for (int i = 0; i < previewText->bytes; i++) {
		// TODO Binary search.

		for (uintptr_t j = 0; j < glyphCount; j++) {
			if (glyphsArray[j].number == previewText->string[i]) {
				if (previous != -1) px += GetAdvance(previous, j, NULL);
				DrawGlyph(painter, &glyphsArray[j], element->bounds.r - 100 + 5 + px, element->bounds.t + 25);
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

		int x = element->bounds.l + 20 - kerningHScroll->position, y = element->bounds.t + 20 - kerningVScroll->position;

		selectedPairI = -1, selectedPairJ = -1;

		for (uintptr_t i = 0; i < glyphCount; i++) {
			for (uintptr_t j = 0; j < glyphCount; j++) {
				bool hasKerningEntry = false;

				DrawGlyph(painter, &glyphsArray[j], x, y);
				DrawGlyph(painter, &glyphsArray[i], x + GetAdvance(j, i, &hasKerningEntry), y);

				UIRectangle border = UI_RECT_4(x - 5, x + 20, y - 15, y + 5);

				if (hasKerningEntry) {
					UIDrawBorder(painter, border, 0xFF0099FF, UI_RECT_1(2));
				}

				if (selectedPairX == (x - 20 - element->bounds.l) / 25 && selectedPairY == (y - 20 - element->bounds.t) / 20) {
					UIDrawBorder(painter, border, 0xFF000000, UI_RECT_1(1));
					selectedPairI = i, selectedPairJ = j;
				}

				x += 25;
			}

			x = element->bounds.l + 20 - kerningHScroll->position;
			y += 20;
		}

		DrawPreviewText(painter, element, NULL);
	} else if (message == UI_MSG_LAYOUT) {
		{
			kerningHScroll->maximum = 25 * glyphCount + 40;
			kerningHScroll->page = UI_RECT_WIDTH(element->bounds);
			UIRectangle scrollBarBounds = element->bounds;
			scrollBarBounds.r = scrollBarBounds.r - UI_SIZE_SCROLL_BAR * element->window->scale;
			scrollBarBounds.t = scrollBarBounds.b - UI_SIZE_SCROLL_BAR * element->window->scale;
			UIElementMove(&kerningHScroll->e, scrollBarBounds, true);
		}

		{
			kerningVScroll->maximum = 20 * glyphCount + 40;
			kerningVScroll->page = UI_RECT_HEIGHT(element->bounds);
			UIRectangle scrollBarBounds = element->bounds;
			scrollBarBounds.b = scrollBarBounds.b - UI_SIZE_SCROLL_BAR * element->window->scale;
			scrollBarBounds.l = scrollBarBounds.r - UI_SIZE_SCROLL_BAR * element->window->scale;
			UIElementMove(&kerningVScroll->e, scrollBarBounds, true);
		}
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
	} else if (message == UI_MSG_SCROLLED) {
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_MOUSE_WHEEL) {
		return UIElementMessage(&kerningVScroll->e, message, di, dp);
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

int NumberTextboxMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_VALUE_CHANGED) {
		UITextbox *textbox = (UITextbox *) element;
		char buffer[32];
		snprintf(buffer, sizeof(buffer), "%.*s", (int) textbox->bytes, textbox->string);
		*(int *) element->cp = atoi(buffer);
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
	ui.theme = _uiThemeClassic;
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

	tabPane = UITabPaneCreate(0, UI_ELEMENT_PARENT_PUSH | UI_ELEMENT_V_FILL, "Glyphs\tEdit\tKerning\tGeneral");
	glyphsTable = UITableCreate(0, 0, "ASCII\tNumber");
	glyphsTable->e.messageUser = GlyphsTableMessage;
	editor = UIElementCreate(sizeof(UIElement), 0, 0, GlyphEditorMessage, "Glyph editor");
	kerning = UIElementCreate(sizeof(UIElement), 0, 0, KerningEditorMessage, "Kerning editor");
	kerningHScroll = UIScrollBarCreate(kerning, UI_SCROLL_BAR_HORIZONTAL);
	kerningVScroll = UIScrollBarCreate(kerning, 0);

	UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_GRAY | UI_PANEL_MEDIUM_SPACING | UI_PANEL_SCROLL);
		UIPanelCreate(0, UI_ELEMENT_PARENT_PUSH | UI_PANEL_EXPAND | UI_PANEL_MEDIUM_SPACING);
			UILabelCreate(0, 0, "Y ascent:", -1);
			yAscentTextbox = UITextboxCreate(&UIPanelCreate(0, UI_PANEL_HORIZONTAL)->e, 0); 
			yAscentTextbox->e.cp = &yAscent; 
			yAscentTextbox->e.messageUser = NumberTextboxMessage;
			UILabelCreate(0, 0, "Y descent:", -1);
			yDescentTextbox = UITextboxCreate(&UIPanelCreate(0, UI_PANEL_HORIZONTAL)->e, 0); 
			yDescentTextbox->e.cp = &yDescent; 
			yDescentTextbox->e.messageUser = NumberTextboxMessage;
			UILabelCreate(0, 0, "The sum of the ascent and descent determine the line height.", -1);

			UILabelCreate(0, 0, "X em width:", -1);
			xEmWidthTextbox = UITextboxCreate(&UIPanelCreate(0, UI_PANEL_HORIZONTAL)->e, 0); 
			xEmWidthTextbox->e.cp = &xEmWidth; 
			xEmWidthTextbox->e.messageUser = NumberTextboxMessage;
		UIParentPop();
	UIParentPop();

	UIWindowRegisterShortcut(window, (UIShortcut) { .code = UI_KEYCODE_LETTER('S'), .ctrl = true, .invoke = Save });

	Load();

	return UIMessageLoop();
}
