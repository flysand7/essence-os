// This file is part of the Essence operating system.
// It is released under the terms of the MIT license -- see LICENSE.md.
// Written by: nakst.

// TODO If the font size is sufficiently large disable subpixel anti-aliasing.
// TODO Variable font support.

#include <shared/bitmap_font.h>

#ifdef USE_FREETYPE_AND_HARFBUZZ
#include <harfbuzz/hb.h>
#include <harfbuzz/hb-ft.h>
#ifndef FT_EXPORT
#define FT_EXPORT(x) extern "C" x
#endif
#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/ftoutln.h>
#endif

#define CHARACTER_SUBPIXEL (1) // 24 bits per pixel; each byte specifies the alpha of each RGB channel.
#define CHARACTER_IMAGE    (2) // 32 bits per pixel, ARGB.
#define CHARACTER_RECOLOR  (3) // 32 bits per pixel, AXXX.

#define FREETYPE_UNIT_SCALE (64)

#define FALLBACK_SCRIPT_LANGUAGE ("en")
#define FALLBACK_SCRIPT (0x4C61746E) // "Latn"

struct Font {
#define FONT_TYPE_BITMAP (1)
#define FONT_TYPE_FREETYPE_AND_HARFBUZZ (2)
	uintptr_t type;

	union {
		const void *bitmapData; // All data has been validated to load the font.

#ifdef USE_FREETYPE_AND_HARFBUZZ
		struct {
			FT_Face ft;
			hb_font_t *hb;
		};
#endif
	};
};

struct GlyphCacheKey {
	uint32_t glyphIndex;
	uint16_t size;
	uint16_t fractionalPosition;
	Font font;
};

struct GlyphCacheEntry {
	uint8_t *data;
	size_t dataBytes;
	int width, height, xoff, yoff;
	int type;

	LinkedItem<GlyphCacheEntry> itemLRU;
	GlyphCacheKey key;
};

struct FontSubstitutionKey {
	EsFontFamily family;
	uint16_t _unused0;
	uint32_t script;
};

struct FontDatabaseEntry : EsFontInformation {
	EsFileStore *files[18];
	char *scripts;
	size_t scriptsBytes;
};

enum TextStyleDifference {
	TEXT_STYLE_NEW_FONT,   // A new font is selected.
	TEXT_STYLE_NEW_SHAPE,  // Shaping parameters have changed.
	TEXT_STYLE_NEW_RENDER, // Render-only properties have changed.
	TEXT_STYLE_IDENTICAL,  // The styles are the same.
};	

struct TextPiece {
	// Shaped glyphs, on the same line, and with constant style and script.
	int32_t ascent, descent, width;
	const EsTextStyle *style;
	uintptr_t glyphOffset;
	size_t glyphCount;
	uintptr_t start, end;
	bool isTabPiece;
};

struct TextLine {
	int32_t ascent, descent, width;
	bool hasEllipsis;
	uintptr_t ellipsisPieceIndex;
	uintptr_t pieceOffset;
	size_t pieceCount;
};

struct TextRun {
	EsTextStyle style;
	uint32_t offset;
	uint32_t script;
};

#ifdef USE_FREETYPE_AND_HARFBUZZ
typedef hb_glyph_info_t TextGlyphInfo;
typedef hb_glyph_position_t TextGlyphPosition;
typedef hb_segment_properties_t TextSegmentProperties;
typedef hb_buffer_t TextShapeBuffer;
typedef hb_feature_t TextFeature;
typedef hb_script_t TextScript;
#else
struct TextGlyphInfo {
	uint32_t codepoint;
	uint32_t cluster;
};

struct TextGlyphPosition {
	int32_t x_advance;
	int32_t y_advance;
	int32_t x_offset;
	int32_t y_offset;
};

struct TextSegmentProperties {
	uint32_t direction;
	uint32_t script;
	uint32_t language;
};

struct TextShapeBuffer {
	uint8_t _unused0;
};

struct TextFeature {
	uint8_t _unused0;
};

typedef uint32_t TextScript;
#endif

struct EsTextPlan {
	TextShapeBuffer *buffer;
	TextSegmentProperties segmentProperties;

	const char *string; 

	Array<TextRun> textRuns; 
	uintptr_t textRunPosition;

	const EsTextStyle *currentTextStyle;
	Font font;

	BreakState breaker;

	Array<TextGlyphInfo> glyphInfos;
	Array<TextGlyphPosition> glyphPositions;

	Array<TextPiece> pieces;
	Array<TextLine> lines;

	int32_t totalHeight, totalWidth;

	bool singleUse;

	EsTextPlanProperties properties;
};

struct {
	// Database.
	HashStore<FontSubstitutionKey, EsFontFamily> substitutions;
	Array<FontDatabaseEntry> database;
	uintptr_t sans, serif, monospaced, fallback;
	char *sansName, *serifName, *monospacedName, *fallbackName;

	// Rendering.
#ifdef USE_FREETYPE_AND_HARFBUZZ
	FT_Library freetypeLibrary;
#endif

	// Caching.
	HashStore<EsFont, Font> loaded; // TODO How many fonts to keep loaded? Reference counting?
#define GLYPH_CACHE_MAX_SIZE (4194304)
	HashStore<GlyphCacheKey, GlyphCacheEntry *> glyphCache;
	LinkedList<GlyphCacheEntry> glyphCacheLRU;
	size_t glyphCacheBytes;
} fontManagement;

struct {
	EsBuffer pack;
	const uint8_t *standardPack;
	size_t standardPackSize;
	char *buffer;
	size_t bufferPosition, bufferAllocated;
} iconManagement;

// --------------------------------- Glyph cache.

void GlyphCacheFreeEntry() {
	GlyphCacheEntry *entry = fontManagement.glyphCacheLRU.lastItem->thisItem;
	fontManagement.glyphCacheLRU.Remove(&entry->itemLRU);
	fontManagement.glyphCache.Delete(&entry->key);
	EsAssert(fontManagement.glyphCacheBytes >= entry->dataBytes);
	fontManagement.glyphCacheBytes -= entry->dataBytes;
	EsHeapFree(entry->data);
	EsHeapFree(entry);
}

void RegisterGlyphCacheEntry(GlyphCacheKey key, GlyphCacheEntry *entry) {
	// Free space in the glyph cache.
	// Do this before adding the new glyph to the cache,
	// in case the new glyph doesn't fit in the cache at all.
	while (fontManagement.glyphCacheBytes > GLYPH_CACHE_MAX_SIZE) {
		GlyphCacheFreeEntry();
	}

	entry->itemLRU.thisItem = entry;
	entry->key = key;
	*fontManagement.glyphCache.Put(&key) = entry;
	fontManagement.glyphCacheLRU.InsertStart(&entry->itemLRU);
	fontManagement.glyphCacheBytes += entry->dataBytes;
}

GlyphCacheEntry *LookupGlyphCacheEntry(GlyphCacheKey key) {
	GlyphCacheEntry *entry = fontManagement.glyphCache.Get1(&key);

	if (!entry) {
		return (GlyphCacheEntry *) EsHeapAllocate(sizeof(GlyphCacheEntry), true);
	} else {
		fontManagement.glyphCacheLRU.Remove(&entry->itemLRU);
		fontManagement.glyphCacheLRU.InsertStart(&entry->itemLRU);
		return entry;
	}
}

// --------------------------------- Font backend abstraction layer.

bool FontLoad(Font *font, const void *data, size_t dataBytes) {
	if (dataBytes > sizeof(BitmapFontHeader)) {
		const BitmapFontHeader *header = (const BitmapFontHeader *) data;

		if (header->signature == BITMAP_FONT_SIGNATURE && (size_t) header->headerBytes >= sizeof(BitmapFontHeader) 
				&& (size_t) header->headerBytes < 0x80 && (size_t) header->glyphBytes < 0x80 && (size_t) header->glyphCount < 0x8000 
				&& dataBytes > (size_t) header->headerBytes + (size_t) header->glyphCount * (size_t) header->glyphBytes
				&& header->glyphCount >= 1 /* index 0 is used as a fallback glyph, which must be present */) {
			for (uintptr_t i = 0; i < header->glyphCount; i++) {
				const BitmapFontGlyph *glyph = ((const BitmapFontGlyph *) ((const uint8_t *) data + header->headerBytes + i * header->glyphBytes));

				size_t bytesPerRow = (glyph->bitsWidth + 7) / 8;
				size_t bitsStorage = (size_t) glyph->bitsHeight * bytesPerRow; 
				size_t kerningStorage = (size_t) glyph->kerningEntryCount * sizeof(BitmapFontKerningEntry);

				if (glyph->bitsWidth > 0x4000 || glyph->bitsHeight > 0x4000 || glyph->kerningEntryCount > 0x4000
						|| glyph->bitsOffset >= dataBytes
						|| bitsStorage > dataBytes - glyph->bitsOffset
						|| kerningStorage > dataBytes - glyph->bitsOffset - bitsStorage) {
					return false;
				}
			}

			font->bitmapData = data;
			font->type = FONT_TYPE_BITMAP;
			return true;
		}
	}

#ifdef USE_FREETYPE_AND_HARFBUZZ
	if (!fontManagement.freetypeLibrary) {
		FT_Init_FreeType(&fontManagement.freetypeLibrary);
	}

	if (!FT_New_Memory_Face(fontManagement.freetypeLibrary, (uint8_t *) data, dataBytes, 0, &font->ft)) {
		font->hb = hb_ft_font_create(font->ft, nullptr);

		if (font->hb) {
			font->type = FONT_TYPE_FREETYPE_AND_HARFBUZZ;
			return true;
		} else {
			FT_Done_Face(font->ft);
		}
	}
#endif

	return false;
}

void FontSetSize(Font *font, uint32_t size) {
	if (font->type == FONT_TYPE_BITMAP) {
		(void) size;
		// Ignored.
	}

#ifdef USE_FREETYPE_AND_HARFBUZZ
	if (font->type == FONT_TYPE_FREETYPE_AND_HARFBUZZ) {
		FT_Set_Char_Size(font->ft, 0, size * FREETYPE_UNIT_SCALE, 100, 100);
		hb_ft_font_changed(font->hb);
	}
#endif
}

int32_t FontGetAscent(Font *font) {
	if (font->type == FONT_TYPE_BITMAP) {
		const BitmapFontHeader *header = (const BitmapFontHeader *) font->bitmapData;
		return header->yAscent * FREETYPE_UNIT_SCALE;
	}

#ifdef USE_FREETYPE_AND_HARFBUZZ
	if (font->type == FONT_TYPE_FREETYPE_AND_HARFBUZZ) {
		return font->ft->size->metrics.ascender;
	}
#endif

	return 0;
}

int32_t FontGetDescent(Font *font) {
	if (font->type == FONT_TYPE_BITMAP) {
		const BitmapFontHeader *header = (const BitmapFontHeader *) font->bitmapData;
		return header->yDescent * -FREETYPE_UNIT_SCALE;
	}

#ifdef USE_FREETYPE_AND_HARFBUZZ
	if (font->type == FONT_TYPE_FREETYPE_AND_HARFBUZZ) {
		return font->ft->size->metrics.descender;
	}
#endif

	return 0;
}

int32_t FontGetEmWidth(Font *font) {
	if (font->type == FONT_TYPE_BITMAP) {
		const BitmapFontHeader *header = (const BitmapFontHeader *) font->bitmapData;
		return header->xEmWidth;
	}

#ifdef USE_FREETYPE_AND_HARFBUZZ
	if (font->type == FONT_TYPE_FREETYPE_AND_HARFBUZZ) {
		return font->ft->size->metrics.x_ppem;
	}
#endif

	return 0;
}

bool FontRenderGlyph(GlyphCacheKey key, GlyphCacheEntry *entry) {
	if (key.font.type == FONT_TYPE_BITMAP) {
		const BitmapFontHeader *header = (const BitmapFontHeader *) key.font.bitmapData;
		const BitmapFontGlyph *glyph = ((const BitmapFontGlyph *) ((const uint8_t *) key.font.bitmapData + header->headerBytes + key.glyphIndex * header->glyphBytes));

		entry->width = glyph->bitsWidth;
		entry->height = glyph->bitsHeight;
		entry->xoff = -glyph->xOrigin;
		entry->yoff = -glyph->yOrigin;
		entry->dataBytes = sizeof(uint8_t) * 4 * glyph->bitsWidth * glyph->bitsHeight;
		entry->data = (uint8_t *) EsHeapAllocate(entry->dataBytes, false);

		if (!entry->data) {
			return false;
		}

		size_t bytesPerRow = (glyph->bitsWidth + 7) / 8;

		for (uintptr_t i = 0; i < glyph->bitsHeight; i++) {
			const uint8_t *row = (const uint8_t *) key.font.bitmapData + glyph->bitsOffset + bytesPerRow * i;

			for (uintptr_t j = 0; j < glyph->bitsWidth; j++) {
				// TODO More efficient storage.
				uint8_t byte = (row[j / 8] & (1 << (j % 8))) ? 0xFF : 0x00;
				uint32_t copy = (byte << 24) | (byte << 16) | (byte << 8) | byte;
				((uint32_t *) entry->data)[i * entry->width + j] = copy;
			}
		}

		return true;
	}

#ifdef USE_FREETYPE_AND_HARFBUZZ
	if (key.font.type == FONT_TYPE_FREETYPE_AND_HARFBUZZ) {
		FT_Load_Glyph(key.font.ft, key.glyphIndex, FT_LOAD_DEFAULT);
		FT_Outline_Translate(&key.font.ft->glyph->outline, key.fractionalPosition, 0);

		int width; 
		int height; 
		int xoff; 
		int yoff; 
		uint8_t *output;

		FT_Render_Glyph(key.font.ft->glyph, FT_RENDER_MODE_LCD);

		FT_Bitmap *bitmap = &key.font.ft->glyph->bitmap;
		width = bitmap->width / 3;
		height = bitmap->rows;
		xoff = key.font.ft->glyph->bitmap_left;
		yoff = -key.font.ft->glyph->bitmap_top;

		entry->dataBytes = 1 /*stupid hack for whitespace*/ + width * height * 4;
		output = (uint8_t *) EsHeapAllocate(entry->dataBytes, false);

		if (!output) {
			return false;
		}

		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				int32_t r = (int32_t) ((uint8_t *) bitmap->buffer)[x * 3 + y * bitmap->pitch + 0];
				int32_t g = (int32_t) ((uint8_t *) bitmap->buffer)[x * 3 + y * bitmap->pitch + 1];
				int32_t b = (int32_t) ((uint8_t *) bitmap->buffer)[x * 3 + y * bitmap->pitch + 2];

				// Reduce how noticible the colour fringes are.
				// TODO Make this adjustable?
				int32_t average = (r + g + b) / 3;
				r -= (r - average) / 3;
				g -= (g - average) / 3;
				b -= (b - average) / 3;

				output[(x + y * width) * 4 + 0] = (uint8_t) r;
				output[(x + y * width) * 4 + 1] = (uint8_t) g;
				output[(x + y * width) * 4 + 2] = (uint8_t) b;
				output[(x + y * width) * 4 + 3] = 0xFF;

				// EsPrint("\tPixel %d, %d: red %X, green %X, blue %X\n", x, y, r, g, b);
			}
		}

		if (output) {
			entry->data = output;
			entry->width = width;
			entry->height = height;
			entry->xoff = xoff;
			entry->yoff = yoff;
			return true;
		}

		return false;
	}
#endif

	return false;
}

void FontShapeTextDone(EsTextPlan *plan, uint32_t glyphCount, TextGlyphInfo *_glyphInfos, TextGlyphPosition *_glyphPositions) {
	if (plan->font.type == FONT_TYPE_BITMAP) {
		(void) glyphCount;
		Array<TextGlyphInfo> glyphInfos = { .array = _glyphInfos };
		Array<TextGlyphPosition> glyphPositions = { .array = _glyphPositions };
		glyphInfos.Free();
		glyphPositions.Free();
	}
}

void FontShapeText(EsTextPlan *plan, const char *string, size_t stringBytes, 
		uintptr_t sectionOffsetBytes, size_t sectionCountBytes,
		TextFeature *features, size_t featureCount,
		uint32_t *glyphCount, TextGlyphInfo **_glyphInfos, TextGlyphPosition **_glyphPositions) {
	if (plan->font.type == FONT_TYPE_BITMAP) {
		(void) features;
		(void) featureCount;
		(void) stringBytes;

		Array<TextGlyphInfo> glyphInfos = {};
		Array<TextGlyphPosition> glyphPositions = {};

		Font *font = &plan->font;
		const BitmapFontHeader *header = (const BitmapFontHeader *) font->bitmapData;

		const char *text = string + sectionOffsetBytes;

		while (text < string + sectionOffsetBytes + sectionCountBytes) {
			TextGlyphInfo info = {};
			TextGlyphPosition position = {};
			info.cluster = text - (string + sectionOffsetBytes);
			uint32_t codepoint = utf8_value(text, string + sectionOffsetBytes + sectionCountBytes - text, nullptr);
			if (!codepoint) break;
			text = utf8_advance(text);

			bool glyphFound = false;
			uint32_t glyphIndex = 0;
			ES_MACRO_SEARCH(header->glyphCount, result = codepoint - ((const BitmapFontGlyph *) ((const uint8_t *) 
							font->bitmapData + header->headerBytes + index * header->glyphBytes))->codepoint;, glyphIndex, glyphFound);
			info.codepoint = glyphFound ? glyphIndex : 0;

			const BitmapFontGlyph *glyph = ((const BitmapFontGlyph *) ((const uint8_t *) 
						font->bitmapData + header->headerBytes + info.codepoint * header->glyphBytes));
			size_t bytesPerRow = (glyph->bitsWidth + 7) / 8;
			position.x_advance = glyph->xAdvance * FREETYPE_UNIT_SCALE;

			if (glyph->kerningEntryCount && text < string + sectionOffsetBytes + sectionCountBytes) {
				uint32_t nextCodepoint = utf8_value(text, string + sectionOffsetBytes + sectionCountBytes - text, nullptr);

				for (uintptr_t i = 0; i < glyph->kerningEntryCount; i++) {
					BitmapFontKerningEntry entry;
					EsMemoryCopy(&entry, (const uint8_t *) font->bitmapData + glyph->bitsOffset 
							+ bytesPerRow * glyph->bitsHeight + sizeof(BitmapFontKerningEntry) * i, sizeof(BitmapFontKerningEntry));

					if (entry.rightCodepoint == nextCodepoint) {
						position.x_advance += entry.xOffset * FREETYPE_UNIT_SCALE;
						break;
					}
				}
			}

			glyphInfos.Add(info);
			glyphPositions.Add(position);
		}

		*glyphCount = glyphInfos.Length();
		*_glyphInfos = &glyphInfos[0];
		*_glyphPositions = &glyphPositions[0];

		return;
	}

#ifdef USE_FREETYPE_AND_HARFBUZZ
	if (plan->font.type == FONT_TYPE_FREETYPE_AND_HARFBUZZ) {
		hb_buffer_clear_contents(plan->buffer);
		hb_buffer_set_segment_properties(plan->buffer, &plan->segmentProperties);
		hb_buffer_add_utf8(plan->buffer, string, stringBytes, sectionOffsetBytes, sectionCountBytes);
		hb_shape(plan->font.hb, plan->buffer, features, featureCount);
		*_glyphInfos = hb_buffer_get_glyph_infos(plan->buffer, glyphCount);
		*_glyphPositions = hb_buffer_get_glyph_positions(plan->buffer, glyphCount);
		return;
	}
#endif
}

uint32_t FontGetScriptFromCodepoint(uint32_t codepoint, bool *inheritingScript) {
#ifdef USE_FREETYPE_AND_HARFBUZZ
	static hb_unicode_funcs_t *unicodeFunctions = nullptr;

	if (!unicodeFunctions) {
		// Multiple threads could call this at the same time, but it doesn't matter,
		// since they should always return the same thing anyway...
		unicodeFunctions = hb_unicode_funcs_get_default();
	}

	uint32_t script = hb_unicode_script(unicodeFunctions, codepoint);
	*inheritingScript = script == HB_SCRIPT_COMMON || script == HB_SCRIPT_INHERITED;
	return script;
#else
	(void) codepoint;
	*inheritingScript = false;
	return FALLBACK_SCRIPT;
#endif
}

void FontInitialiseShaping(EsTextPlan *plan) {
#ifdef USE_FREETYPE_AND_HARFBUZZ
	plan->buffer = hb_buffer_create();
	hb_buffer_set_cluster_level(plan->buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);
	plan->segmentProperties.direction = (plan->properties.flags & ES_TEXT_PLAN_RTL) ? HB_DIRECTION_RTL : HB_DIRECTION_LTR;
	plan->segmentProperties.script = (TextScript) FALLBACK_SCRIPT;
	plan->segmentProperties.language = hb_language_from_string(plan->properties.cLanguage ?: FALLBACK_SCRIPT_LANGUAGE, -1);
#else
	(void) plan;
#endif
}

void FontDestroyShaping(EsTextPlan *plan) {
#ifdef USE_FREETYPE_AND_HARFBUZZ
	hb_buffer_destroy(plan->buffer);
	plan->buffer = nullptr;
#else
	(void) plan;
#endif
}

// --------------------------------- Font management.

void FontInitialise() {
	if (fontManagement.database.Length()) {
		return;
	}
	
	fontManagement.sansName       = EsSystemConfigurationReadString(EsLiteral("ui_fonts"), EsLiteral("sans"));
	fontManagement.serifName      = EsSystemConfigurationReadString(EsLiteral("ui_fonts"), EsLiteral("serif"));
	fontManagement.monospacedName = EsSystemConfigurationReadString(EsLiteral("ui_fonts"), EsLiteral("mono"));
	fontManagement.fallbackName   = EsSystemConfigurationReadString(EsLiteral("ui_fonts"), EsLiteral("fallback")); 

	FontDatabaseEntry nullFont = {};
	fontManagement.database.Add(nullFont);

	EsMutexAcquire(&api.systemConfigurationMutex);

	for (uintptr_t i = 0; i < api.systemConfigurationGroups.Length(); i++) {
		EsSystemConfigurationGroup *g = &api.systemConfigurationGroups[i];

		if (0 == EsStringCompareRaw(g->sectionClass, g->sectionClassBytes, EsLiteral("font"))) {
			if (0 == EsStringCompareRaw(g->section, g->sectionBytes, EsLiteral(fontManagement.sansName))) {
				fontManagement.sans = fontManagement.database.Length();
			}

			if (0 == EsStringCompareRaw(g->section, g->sectionBytes, EsLiteral(fontManagement.serifName))) {
				fontManagement.serif = fontManagement.database.Length();
			}

			if (0 == EsStringCompareRaw(g->section, g->sectionBytes, EsLiteral(fontManagement.monospacedName))) {
				fontManagement.monospaced = fontManagement.database.Length();
			}

			if (0 == EsStringCompareRaw(g->section, g->sectionBytes, EsLiteral(fontManagement.fallbackName))) {
				fontManagement.fallback = fontManagement.database.Length();
			}

			FontDatabaseEntry entry = {};

			entry.nameBytes = MinimumInteger(g->sectionBytes, sizeof(entry.name));
			EsMemoryCopy(entry.name, g->section, entry.nameBytes);
			entry.id = fontManagement.database.Length();

			for (uintptr_t i = 0; i < g->itemCount; i++) {
				EsSystemConfigurationItem *item = g->items + i;

				if (0 == EsStringCompareRaw(item->key, item->keyBytes, EsLiteral("category"))) {
					entry.categoryBytes = MinimumInteger(item->valueBytes, sizeof(entry.category));
					EsMemoryCopy(entry.category, item->value, entry.categoryBytes);
				} else if (0 == EsStringCompareRaw(item->key, item->keyBytes, EsLiteral("scripts"))) {
					entry.scripts = item->value;
					entry.scriptsBytes = item->valueBytes;
				} else if ((item->keyBytes == 2 && item->key[0] == '.' && EsCRTisdigit(item->key[1]))
						|| (item->keyBytes == 3 && item->key[0] == '.' && EsCRTisdigit(item->key[1]) && item->key[2] == 'i') ) {
					int weight = item->key[1] - '0';
					bool italic = item->keyBytes == 3;

					if (italic) {
						entry.availableWeightsItalic |= 1 << weight;
					} else {
						entry.availableWeightsNormal |= 1 << weight;
					}

					size_t fileIndex = weight - 1 + italic * 9;

					if (item->valueBytes && item->value[0] == ':') {
						entry.files[fileIndex] = FileStoreCreateFromEmbeddedFile(&bundleDesktop, item->value + 1, item->valueBytes - 1);
					} else {
						entry.files[fileIndex] = FileStoreCreateFromPath(item->value, item->valueBytes);
					}
				}
			}

			fontManagement.database.Add(entry);
		}
	}

	EsMutexRelease(&api.systemConfigurationMutex);
}

EsFontFamily FontGetStandardFamily(EsFontFamily family) {
	FontInitialise();

	if (family == 0 || family == ES_FONT_SANS) {
		return fontManagement.sans ?: fontManagement.fallback;
	} else if (family == ES_FONT_SERIF) {
		return fontManagement.serif ?: fontManagement.fallback;
	} else if (family == ES_FONT_MONOSPACED) {
		return fontManagement.monospaced ?: fontManagement.fallback;
	} else {
		return family;
	}
}

bool EsFontDatabaseLookupByName(const char *name, ptrdiff_t nameBytes, EsFontInformation *information) {
	FontInitialise();
	EsMemoryZero(information, sizeof(EsFontInformation));

	for (uintptr_t i = 0; i < fontManagement.database.Length(); i++) {
		if (0 == EsStringCompare(name, nameBytes, fontManagement.database[i].name, fontManagement.database[i].nameBytes)) {
			EsMemoryCopy(information, &fontManagement.database[i], sizeof(EsFontInformation));
			return true;
		}
	}

	return false;
}

bool EsFontDatabaseLookupByID(EsFontFamily id, EsFontInformation *information) {
	FontInitialise();
	EsMemoryZero(information, sizeof(EsFontInformation));

	id = FontGetStandardFamily(id);

	if (id >= fontManagement.database.Length()) {
		return false;
	}

	EsMemoryCopy(information, &fontManagement.database[id], sizeof(EsFontInformation));
	
	return true;
}

EsFontFamily EsFontDatabaseInsertFile(const EsFontInformation *information, EsFileStore *store) {
	// TODO Locking.

	FontInitialise();
	EsAssert(store->handles);
	FontDatabaseEntry *entry = nullptr;

	if (information->nameBytes) {
		for (uintptr_t i = 1; i < fontManagement.database.Length(); i++) {
			FontDatabaseEntry *entry = &fontManagement.database[i];
			EsAssert(entry->id == i);

			if (0 == EsStringCompareRaw(information->name, information->nameBytes, 
						fontManagement.database[i].name, fontManagement.database[i].nameBytes)) {
				if ((information->availableWeightsItalic & entry->availableWeightsItalic)
						|| (information->availableWeightsNormal & entry->availableWeightsNormal)) {
					// The variant is already in the database.
					return entry->id;
				}

				goto addFileToFamily;
			}
		}
	}

	{
		// The family is not yet in the database; add it.
		FontDatabaseEntry e = {};
		EsMemoryCopy(&e, information, sizeof(EsFontInformation));
		e.id = fontManagement.database.Length();

		if (fontManagement.database.Add(e)) {
			entry = &fontManagement.database.Last();
		} else {
			return 0;
		}
	}

	addFileToFamily:;

	store->handles++;

	entry->availableWeightsNormal |= information->availableWeightsNormal;
	entry->availableWeightsItalic |= information->availableWeightsItalic;

	for (uintptr_t i = 0; i < 18; i++) {
		if ((i < 9 && (information->availableWeightsNormal & (1 << i))) 
				|| (i >= 9 && (information->availableWeightsItalic & (1 << i)))) {
			store->handles++;
			entry->files[i] = store;
			return entry->id;
		}
	}

	EsAssert(false);
	return 0;
}

EsFontInformation *EsFontDatabaseEnumerate(size_t *count) {
	// TODO Locking.
	FontInitialise();
	*count = 0;
	EsFontInformation *result = (EsFontInformation *) EsHeapAllocate(sizeof(EsFontInformation) * (fontManagement.database.Length() - 1), true);
	if (!result) return nullptr;
	*count = fontManagement.database.Length() - 1;
	for (uintptr_t i = 1; i <= *count; i++) EsFontDatabaseLookupByID(i, &result[i - 1]);
	return result;
}

bool FontSupportsScript(FontDatabaseEntry *entry, uint32_t _script, bool first) {
	if (!entry->scriptsBytes) {
		return first;
	}

	char script[4];
	script[0] = (char) (_script >> 24);
	script[1] = (char) (_script >> 16);
	script[2] = (char) (_script >>  8);
	script[3] = (char) (_script >>  0);

	for (uintptr_t i = 0; i <= entry->scriptsBytes - 4; i += 5) {
		if (script[0] == entry->scripts[i + 0] 
				&& script[1] == entry->scripts[i + 1] 
				&& script[2] == entry->scripts[i + 2] 
				&& script[3] == entry->scripts[i + 3]) {
			return true;
		}
	}

	return false;
}

EsFontFamily FontApplySubstitution(EsTextPlanProperties *properties, EsFontFamily family, uint32_t script) {
	FontInitialise();

	if (properties->flags & ES_TEXT_PLAN_NO_FONT_SUBSTITUTION) {
		return family;
	}

	FontSubstitutionKey key = {};
	key.family = FontGetStandardFamily(family);
	key.script = script;
	EsFontFamily result = fontManagement.substitutions.Get1(&key);
	if (result) return result;

	EsAssert(key.family < fontManagement.database.Length());
	FontDatabaseEntry *entry = &fontManagement.database[key.family];

	if (FontSupportsScript(entry, script, true)) {
		*fontManagement.substitutions.Put(&key) = key.family;
		return key.family;
	}

	EsFontFamily firstMatch = (EsFontFamily) -1;

	for (uintptr_t i = 1; i < fontManagement.database.Length(); i++) {
		if (&fontManagement.database[i] == entry) continue;
		if (!FontSupportsScript(&fontManagement.database[i], script, false)) continue;

		if (firstMatch == (EsFontFamily) -1) {
			firstMatch = i;
		}

		if (0 == EsStringCompareRaw(fontManagement.database[i].category, fontManagement.database[i].categoryBytes,
					entry->category, entry->categoryBytes)) {
			*fontManagement.substitutions.Put(&key) = i;
			return i;
		}
	}

	if (firstMatch != (EsFontFamily) -1) {
		*fontManagement.substitutions.Put(&key) = firstMatch;
		return firstMatch;
	} else {
		// No installed font supports the script.
		*fontManagement.substitutions.Put(&key) = key.family;
		return result;
	}
}

Font FontGet(EsFont key) {
	FontInitialise();

	if (key.weight == 0) {
		key.weight = ES_FONT_REGULAR;
	}

	key.family = FontGetStandardFamily(key.family);

	Font *_font = fontManagement.loaded.Get(&key);
	if (_font) return *_font;

	EsFileStore *file = nullptr;
	int matchDistance = 1000;

	EsAssert(key.family < fontManagement.database.Length());
	FontDatabaseEntry *entry = &fontManagement.database[key.family];

	for (uintptr_t i = 0; i < 18; i++) {
		if (entry->files[i]) {
			int weight = (i % 9) + 1;
			bool italic = i >= 9;
			int distance = ((italic != ((key.flags & ES_FONT_ITALIC) ? true : false)) ? 10 : 0) + AbsoluteInteger(weight - key.weight);

			if (distance < matchDistance) {
				matchDistance = distance;
				file = entry->files[i];
			}
		}
	}

	if (!file) {
		EsPrint("Could not load font (f%d/w%d/%X).\n", key.family, key.weight, key.flags);
		key.family = fontManagement.fallback;
		return FontGet(key);
	}

	// EsPrint("Loading font from '%z' (f%d/w%d/i%d).\n", file, key.family, key.weight, key.italic);

	size_t size;
	void *data = EsFileStoreMap(file, &size, ES_MEMORY_MAP_OBJECT_READ_ONLY);

	if (!data) {
		EsPrint("Could not load font (f%d/w%d/%X).\n", key.family, key.weight, key.flags);
		key.family = fontManagement.fallback;
		return FontGet(key);
	}

	Font font = {};

	if (!FontLoad(&font, data, size)) {
		EsPrint("Could not load font (f%d/w%d/%X).\n", key.family, key.weight, key.flags);
		key.family = fontManagement.fallback;
		return FontGet(key);
	}

	*fontManagement.loaded.Put(&key) = font;
	return font;
}

void FontDatabaseFree() {
	while (fontManagement.glyphCacheLRU.count) {
		GlyphCacheFreeEntry();
	}

	for (uintptr_t i = 0; i < fontManagement.loaded.Count(); i++) {
		// TODO Unmap file store data.
		Font font = fontManagement.loaded[i];

		if (font.type == FONT_TYPE_BITMAP) {
			// Nothing to be done.
		}

#ifdef USE_FREETYPE_AND_HARFBUZZ
		if (font.type == FONT_TYPE_FREETYPE_AND_HARFBUZZ) {
			hb_font_destroy(font.hb);
			FT_Done_Face(font.ft);
		}
#endif
	}

	for (uintptr_t i = 0; i < fontManagement.database.Length(); i++) {
		FontDatabaseEntry *entry = &fontManagement.database[i];

		for (uintptr_t j = 0; j < sizeof(entry->files) / sizeof(entry->files[0]); j++) {
			if (entry->files[j]) {
				FileStoreCloseHandle(entry->files[j]);
			}
		}
	}

	EsAssert(fontManagement.glyphCache.Count() == 0);
	EsAssert(fontManagement.glyphCacheBytes == 0);

	EsHeapFree(fontManagement.sansName);
	EsHeapFree(fontManagement.serifName);
	EsHeapFree(fontManagement.monospacedName);
	EsHeapFree(fontManagement.fallbackName);

	fontManagement.glyphCache.Free();
	fontManagement.substitutions.Free();
	fontManagement.database.Free();
	fontManagement.loaded.Free();

#ifdef USE_FREETYPE_AND_HARFBUZZ
	FT_Done_FreeType(fontManagement.freetypeLibrary);
#endif
}

// --------------------------------- Blitting rendered glyphs.

__attribute__((no_instrument_function))
inline static void DrawStringPixel(int oX, int oY, void *bitmap, size_t stride, uint32_t textColor, 
		uint32_t selectionColor, int32_t backgroundColor, uint32_t pixel, bool selected, bool fullAlpha) {
	uint32_t *destination = (uint32_t *) ((uint8_t *) bitmap + (oX) * 4 + (oY) * stride);
	uint8_t alpha = (textColor & 0xFF000000) >> 24;

	if (pixel == 0xFFFFFF && alpha == 0xFF) {
		*destination = 0xFF000000 | textColor;
	} else if (pixel && fullAlpha) {
		uint32_t original;

		if (selected) {
			original = selectionColor;
		} else if (backgroundColor < 0) {
			original = *destination;
		} else {
			original = backgroundColor;
		}

		uint32_t ga = (((pixel & 0x0000FF00) >> 8) * alpha) >> 8;
		uint32_t alphaD2 = (255 - ga) * ((original & 0xFF000000) >> 24);
		uint32_t alphaOut = ga + (alphaD2 >> 8);

		if (alphaOut) {
			uint32_t m2 = alphaD2 / alphaOut;
			uint32_t m1 = (ga << 8) / alphaOut;
			if (m2 == 0x100) m2--;
			if (m1 == 0x100) m1--;

			uint32_t r2 = m2 * ((original & 0x000000FF) >> 0);
			uint32_t g2 = m2 * ((original & 0x0000FF00) >> 8);
			uint32_t b2 = m2 * ((original & 0x00FF0000) >> 16);
			uint32_t r1 = m1 * ((textColor & 0x000000FF) >> 0);
			uint32_t g1 = m1 * ((textColor & 0x0000FF00) >> 8);
			uint32_t b1 = m1 * ((textColor & 0x00FF0000) >> 16);

			uint32_t result = 
				(0x00FF0000 & ((b1 + b2) << 8)) 
				| (0x0000FF00 & ((g1 + g2) << 0)) 
				| (0x000000FF & ((r1 + r2) >> 8))
				| (alphaOut << 24);

			*destination = result;
		}
	} else if (pixel) {
		uint32_t original;

		if (selected) {
			original = selectionColor;
		} else if (backgroundColor < 0) {
			original = *destination;
		} else {
			original = backgroundColor;
		}

		uint32_t ra = (((pixel & 0x000000FF) >> 0) * alpha) >> 8;
		uint32_t ga = (((pixel & 0x0000FF00) >> 8) * alpha) >> 8;
		uint32_t ba = (((pixel & 0x00FF0000) >> 16) * alpha) >> 8;
		uint32_t r2 = (255 - ra) * ((original & 0x000000FF) >> 0);
		uint32_t g2 = (255 - ga) * ((original & 0x0000FF00) >> 8);
		uint32_t b2 = (255 - ba) * ((original & 0x00FF0000) >> 16);
		uint32_t r1 = ra * ((textColor & 0x000000FF) >> 0);
		uint32_t g1 = ga * ((textColor & 0x0000FF00) >> 8);
		uint32_t b1 = ba * ((textColor & 0x00FF0000) >> 16);

		uint32_t result = 0xFF000000 | (0x00FF0000 & ((b1 + b2) << 8)) 
			| (0x0000FF00 & ((g1 + g2) << 0)) 
			| (0x000000FF & ((r1 + r2) >> 8));

		*destination = result;
	}
}

void DrawSingleCharacter(int width, int height, int xoff, int yoff, 
		EsPoint outputPosition, EsRectangle region, EsPaintTarget *target,  
		int blur, int type, bool selected, uint8_t *output,
		uint32_t color, uint32_t selectionColor, int32_t backgroundColor, bool fullAlpha) {
	// TODO Rewrite.

	if (type != CHARACTER_SUBPIXEL) {
		blur = 0;
	}

	uint8_t alpha = color >> 24;

	int xOut = outputPosition.x + xoff;
	int yOut = outputPosition.y + yoff;
	int xFrom = xOut, xTo = xOut + width;
	int yFrom = yOut, yTo = yOut + height;

	if (blur) {
		xFrom -= blur;
		yFrom -= blur;
		xTo   += blur;
		yTo   += blur;
	}

	if (xFrom < region.l) xFrom = region.l; else if (xFrom >= region.r) xFrom = region.r;
	if (xFrom < 0) xFrom = 0; else if (xFrom >= (int) target->width) xFrom = target->width;
	if (xTo < region.l) xTo = region.l; else if (xTo >= region.r) xTo = region.r;
	if (xTo < 0) xTo = 0; else if (xTo >= (int) target->width) xTo = target->width;

	if (yFrom < region.t) yFrom = region.t; else if (yFrom >= region.b) yFrom = region.b;
	if (yFrom < 0) yFrom = 0; else if (yFrom >= (int) target->height) yFrom = target->height;
	if (yTo < region.t) yTo = region.t; else if (yTo >= region.b) yTo = region.b;
	if (yTo < 0) yTo = 0; else if (yTo >= (int) target->height) yTo = target->height;

	float blurExponentDenominator = -1.0f / (2.0f * (blur / 3.0f) * (blur / 3.0f));

	for (int oY = yFrom; oY < yTo; oY++) {
		int y = oY - yOut;

		for (int oX = xFrom; oX < xTo; oX++) {
			int x = oX - xOut;

			if (blur) {
				float c = 0, d = 0;

				for (int i = y - blur; i <= y + blur; i++) {
					for (int j = x - blur; j <= x + blur; j++) {
						float weight = EsCRTexpf(blurExponentDenominator * ((i - y) * (i - y) + (j - x) * (j - x)));
						d += weight;

						if (i >= 0 && j >= 0 && i < height && j < width) {
							uint32_t pixel = *((uint32_t *) (output + (j * 4 + i * width * 4)));
							c += (pixel & 0xFF00) * weight;
						}
					}
				}

				uint32_t a = c / (d * 256.0f);
				DrawStringPixel(oX, oY, target->bits, target->stride, color, selectionColor, backgroundColor, 
						a | (a << 8) | (a << 16), selected, fullAlpha);
			} else if (type == CHARACTER_IMAGE || type == CHARACTER_RECOLOR) {
				uint32_t pixel = *((uint32_t *) (output + (x * 4 + y * width * 4)));
				uint32_t *destination = (uint32_t *) ((uint8_t *) target->bits + (oX) * 4 + (oY) * target->stride);

				if (type == CHARACTER_RECOLOR) {
					pixel = (pixel & 0xFF000000) | (color & 0x00FFFFFF);
				}

				if ((pixel >> 24) == 0xFF && alpha == 0xFF) {
					*destination = pixel;
				} else if (pixel && fullAlpha) {
					uint32_t original = *destination;
					uint32_t alphaSource = ((pixel >> 24) * alpha) >> 8;
					uint32_t alphaDestination = ((original & 0xFF000000) >> 24) * (255 - alphaSource);
					uint32_t alphaOut = alphaSource + (alphaDestination >> 8);

					if (alphaOut) {
						uint32_t m2 = alphaDestination / alphaOut;
						uint32_t m1 = (alphaSource << 8) / alphaOut;
						if (m2 == 0x100) m2--;
						if (m1 == 0x100) m1--;
						uint32_t r2 = m2 * ((original & 0x000000FF) >> 0);
						uint32_t g2 = m2 * ((original & 0x0000FF00) >> 8);
						uint32_t b2 = m2 * ((original & 0x00FF0000) >> 16);
						uint32_t r1 = m1 * ((pixel & 0x000000FF) >> 0);
						uint32_t g1 = m1 * ((pixel & 0x0000FF00) >> 8);
						uint32_t b1 = m1 * ((pixel & 0x00FF0000) >> 16);
						uint32_t result = (alphaOut << 24) | (0x00FF0000 & ((b1 + b2) << 8)) 
							| (0x0000FF00 & ((g1 + g2) << 0)) 
							| (0x000000FF & ((r1 + r2) >> 8));
						*destination = result;
					}
				} else if (pixel) {
					uint32_t original = *destination;
					uint32_t a = ((pixel >> 24) * alpha) >> 8;
					uint32_t r2 = (255 - a) * ((original & 0x000000FF) >> 0);
					uint32_t g2 = (255 - a) * ((original & 0x0000FF00) >> 8);
					uint32_t b2 = (255 - a) * ((original & 0x00FF0000) >> 16);
					uint32_t r1 = a * ((pixel & 0x000000FF) >> 0);
					uint32_t g1 = a * ((pixel & 0x0000FF00) >> 8);
					uint32_t b1 = a * ((pixel & 0x00FF0000) >> 16);
					uint32_t result = 0xFF000000 
						| (0x00FF0000 & ((b1 + b2) << 8)) 
						| (0x0000FF00 & ((g1 + g2) << 0)) 
						| (0x000000FF & ((r1 + r2) >> 8));
					*destination = result;
				}
			} else if (type == CHARACTER_SUBPIXEL) {
				uint32_t pixel = *((uint32_t *) (output + (x * 4 + y * width * 4)));
				DrawStringPixel(oX, oY, target->bits, target->stride, color, selectionColor, backgroundColor, pixel, selected, fullAlpha);
			}
		}
	}
}

// --------------------------------- Icons.

#define ICON_PACK_PAINT_SOLID (1)
#define ICON_PACK_PAINT_LINEAR_GRADIENT (2)
#define ICON_PACK_PAINT_RADIAL_GRADIENT (3)

struct IconPackGradientStop {
	uint32_t color;
	float offset;
};

struct IconPackGradient {
	float transform[6];
	uint8_t repeatMode, stopCount;
	float fx, fy;
	IconPackGradientStop stops[1];
};

struct IconPackPaint {
	uint8_t type;

	union {
		uint32_t color;
		IconPackGradient *gradient;
	};
};

struct IconPackPath {
	IconPackPath *next;
	float *points;
	int pointCount;
	bool closed;
};

struct IconPackShape {
	IconPackShape *next;
	IconPackPath *paths;
	IconPackPaint fill, stroke;
	bool evenOddRule;
	float opacity;
	float strokeWidth, strokeDashOffset, strokeDashArray[8], miterLimit;
	uint8_t strokeLineJoin, strokeLineCap, strokeDashCount;
};

struct IconPackImage {
	IconPackShape *shapes;
	float width, height;
};

void *IconBufferAllocate(size_t size) {
	// Must allocate adjacent to the previous allocation.
	void *memory = iconManagement.buffer + iconManagement.bufferPosition;
	iconManagement.bufferPosition += size;
	EsAssert(iconManagement.bufferAllocated > iconManagement.bufferPosition); // Icon required more space than is available in iconBuffer.
	EsMemoryZero(memory, size);
	return memory;
}

void IconPackReadPaint(IconPackPaint *paint) {
	paint->type = EsBufferReadByte(&iconManagement.pack);

	if (paint->type == ICON_PACK_PAINT_SOLID) {
		paint->color = EsBufferReadInt(&iconManagement.pack);
	} else if (paint->type == ICON_PACK_PAINT_LINEAR_GRADIENT || paint->type == ICON_PACK_PAINT_RADIAL_GRADIENT) {
		paint->gradient = (IconPackGradient *) IconBufferAllocate(sizeof(IconPackGradient));
		for (int i = 0; i < 6; i++) paint->gradient->transform[i] = EsBufferReadFloat(&iconManagement.pack);
		paint->gradient->repeatMode = EsBufferReadByte(&iconManagement.pack);
		paint->gradient->fx = EsBufferReadFloat(&iconManagement.pack);
		paint->gradient->fy = EsBufferReadFloat(&iconManagement.pack);
		paint->gradient->stopCount = EsBufferReadInt(&iconManagement.pack);
		IconBufferAllocate(8 * paint->gradient->stopCount);

		for (int i = 0; i < paint->gradient->stopCount; i++) {
			paint->gradient->stops[i].color = EsBufferReadInt(&iconManagement.pack);
			paint->gradient->stops[i].offset = EsBufferReadFloat(&iconManagement.pack);
		}
	}
}

void IconPackReadPaint(IconPackPath **link) {
	if (EsBufferReadByte(&iconManagement.pack) != 0x34) return;
	next:;

	IconPackPath *path = *link = (IconPackPath *) IconBufferAllocate(sizeof(IconPackPath));

	path->pointCount = EsBufferReadInt(&iconManagement.pack);
	path->closed = EsBufferReadByte(&iconManagement.pack);
	path->points = (float *) IconBufferAllocate(sizeof(float) * 2 * path->pointCount);
	link = &path->next;

	for (int i = 0; i < path->pointCount; i++) {
		path->points[i * 2 + 0] = EsBufferReadFloat(&iconManagement.pack);
		path->points[i * 2 + 1] = EsBufferReadFloat(&iconManagement.pack);
	}

	if (EsBufferReadByte(&iconManagement.pack)) goto next;
}

void IconPackReadShape(IconPackShape **link) {
	if (EsBufferReadByte(&iconManagement.pack) != 0x12) return;
	next:;

	IconPackShape *shape = *link = (IconPackShape *) IconBufferAllocate(sizeof(IconPackShape));

	shape->opacity = EsBufferReadFloat(&iconManagement.pack);
	shape->strokeWidth = EsBufferReadFloat(&iconManagement.pack);
	shape->strokeDashOffset = EsBufferReadFloat(&iconManagement.pack);
	for (int i = 0; i < 8; i++) shape->strokeDashArray[i] = EsBufferReadFloat(&iconManagement.pack);
	shape->strokeDashCount = EsBufferReadByte(&iconManagement.pack);
	shape->strokeLineJoin = EsBufferReadByte(&iconManagement.pack);
	shape->strokeLineCap = EsBufferReadByte(&iconManagement.pack);
	shape->miterLimit = EsBufferReadFloat(&iconManagement.pack);
	shape->evenOddRule = EsBufferReadByte(&iconManagement.pack);

	IconPackReadPaint(&shape->fill);
	IconPackReadPaint(&shape->stroke);
	IconPackReadPaint(&shape->paths);
	link = &shape->next;

	if (EsBufferReadByte(&iconManagement.pack)) goto next;
}

IconPackImage *IconPackReadImage(uint32_t id, uint32_t size, int *type) {
	iconManagement.bufferPosition = 0;
	iconManagement.pack.position = 0;

	uint32_t count = EsBufferReadInt(&iconManagement.pack);
	if (id >= count) return nullptr;
	iconManagement.pack.position = (id + 1) * 4;
	uint32_t start = iconManagement.pack.position = EsBufferReadInt(&iconManagement.pack);
	*type = (EsBufferReadInt(&iconManagement.pack) == 1) ? CHARACTER_RECOLOR : CHARACTER_IMAGE;
	iconManagement.pack.position = start;

	// bool rtl = api.systemConstants[ES_SYSTEM_CONSTANT_RIGHT_TO_LEFT];
	bool rtl = false;
	bool found = false;
	uint32_t variant = 0;

	// TODO Clean this up!

	while (true) {
		// Look for a perfect match of size and direction.
		variant = EsBufferReadInt(&iconManagement.pack);
		if (!variant) break;
		if ((variant == size || variant == 1) && !rtl) { found = true; break; }
		iconManagement.pack.position = EsBufferReadInt(&iconManagement.pack);
	}

	if (!found) {
		// Look for the smallest bigger size.
		iconManagement.pack.position = start;

		while (true) {
			variant = EsBufferReadInt(&iconManagement.pack);
			if (!variant) break;
			if ((variant & 0x7FFF) > size && !rtl) { found = true; break; }
			iconManagement.pack.position = EsBufferReadInt(&iconManagement.pack);
		}
	}

	if (!found && rtl) {
		iconManagement.pack.position = start;

		while (true) {
			variant = EsBufferReadInt(&iconManagement.pack);
			if (!variant) break;
			if ((variant == (size | 0x8000)) || variant == 0x8001) { found = true; break; }
			iconManagement.pack.position = EsBufferReadInt(&iconManagement.pack);
		}
	}

	if (!found && rtl) {
		iconManagement.pack.position = start;

		while (true) {
			variant = EsBufferReadInt(&iconManagement.pack);
			if (!variant) break;
			if ((variant & 0x7FFF) > size && (variant & 0x8000)) { found = true; break; }
			iconManagement.pack.position = EsBufferReadInt(&iconManagement.pack);
		}
	}

	if (!found) {
		// Look for the biggest size.
		iconManagement.pack.position = start;
		uintptr_t previous = 0;

		while (true) {
			variant = EsBufferReadInt(&iconManagement.pack);

			if (!variant) { 
				iconManagement.pack.position = previous; 
				found = previous != 0; 
				break; 
			}

			if ((~variant & 0x8000) || rtl) {
				previous = iconManagement.pack.position;
			}

			iconManagement.pack.position = EsBufferReadInt(&iconManagement.pack);
		}
	}

	EsBufferReadInt(&iconManagement.pack);

	IconPackImage *image = (IconPackImage *) IconBufferAllocate(sizeof(IconPackImage));
	image->width = EsBufferReadFloat(&iconManagement.pack);
	image->height = EsBufferReadFloat(&iconManagement.pack);
	IconPackReadShape(&image->shapes);
	return image;
}

void DrawIcon(int width, int height, uint8_t *destination, IconPackImage *icon, int stride, float translateX, float translateY, float scaleX, float scaleY) {
	if (width <= 0 || height <= 0) return;

	RastVertex scale2 = { scaleX, scaleY };

	RastSurface surface = {};

	surface.buffer = (uint32_t *) destination;
	surface.stride = stride;

	if (!RastSurfaceInitialise(&surface, width, height, true)) {
		RastSurfaceDestroy(&surface);
		return;
	}

	// TODO strokeDashOffset, strokeDashArray, strokeDashCount.

	IconPackShape *shape = icon->shapes;
	int shapeIndex = 0;

	while (shape) {
		RastPaint paintFill = {}, paintStroke = {};

		RastContourStyle contour = {};
		contour.internalWidth = shape->strokeWidth * scaleX * 0.5f;
		contour.externalWidth = shape->strokeWidth * scaleX * 0.5f;
		contour.joinMode = (RastLineJoinMode) shape->strokeLineJoin;
		contour.capMode = (RastLineCapMode) shape->strokeLineCap;
		contour.miterLimit = scaleX * shape->strokeWidth * shape->miterLimit; 

		if (shape->opacity == 0) {
			goto nextShape;
		}

		for (uintptr_t i = 0; i < 2; i++) {
			IconPackPaint *p1 = i ? &shape->stroke : &shape->fill;
			RastPaint *p2 = i ? &paintStroke : &paintFill;

			if (p1->type == ICON_PACK_PAINT_SOLID) {
				p2->type = RAST_PAINT_SOLID;
				uint32_t color = p1->color;
				color = (color & 0xFF00FF00) | ((color & 0xFF) << 16) | ((color & 0xFF0000) >> 16);
				p2->solid.color = 0xFFFFFF & color;
				p2->solid.alpha = (float) ((0xFF000000 & color) >> 24) / 255.0f * shape->opacity;
			} else if (p1->type == ICON_PACK_PAINT_LINEAR_GRADIENT || p1->type == ICON_PACK_PAINT_RADIAL_GRADIENT) {
				IconPackGradient *gradient = p1->gradient;
				p2->type = p1->type == ICON_PACK_PAINT_RADIAL_GRADIENT ? RAST_PAINT_RADIAL_GRADIENT : RAST_PAINT_LINEAR_GRADIENT;

				if (p1->type == ICON_PACK_PAINT_RADIAL_GRADIENT) {
					p2->gradient.transform[0] = gradient->transform[0] / scale2.x;
					p2->gradient.transform[1] = gradient->transform[2] / scale2.y;
					p2->gradient.transform[2] = gradient->transform[4] - (p2->gradient.transform[0] * translateX + p2->gradient.transform[1] * translateY);
					p2->gradient.transform[3] = gradient->transform[1] / scale2.x;
					p2->gradient.transform[4] = gradient->transform[3] / scale2.y;
					p2->gradient.transform[5] = gradient->transform[5] - (p2->gradient.transform[3] * translateX + p2->gradient.transform[4] * translateY);
				} else {
					p2->gradient.transform[0] = gradient->transform[1] / scale2.x;
					p2->gradient.transform[1] = gradient->transform[3] / scale2.y;
					p2->gradient.transform[2] = gradient->transform[5] - (p2->gradient.transform[0] * translateX + p2->gradient.transform[1] * translateY);
				}

				RastGradientStop stops[16];
				size_t stopCount = gradient->stopCount;
				if (stopCount > 16) stopCount = 16;

				for (uintptr_t i = 0; i < stopCount; i++) {
					uint32_t color = gradient->stops[i].color;
					color = (color & 0xFF00FF00) | ((color & 0xFF) << 16) | ((color & 0xFF0000) >> 16);
					stops[i].color = (0xFFFFFF & color)
							| ((uint32_t) ((float) ((0xFF000000 & color) >> 24) * shape->opacity) << 24);
					stops[i].position = gradient->stops[i].offset;
				}

				RastGradientInitialise(p2, stops, stopCount, false);
				p2->gradient.repeatMode = (RastRepeatMode) gradient->repeatMode;
			}
		}

		if (paintFill.type) {
			RastPath p = {};
			IconPackPath *path = shape->paths;

			while (path) {
				RastPathAppendBezier(&p, (RastVertex *) path->points, path->pointCount, scale2);
				if (path->closed || path->next) RastPathCloseSegment(&p);
				path = path->next;
			}

			RastPathTranslate(&p, translateX, translateY);
			RastShape s = RastShapeCreateSolid(&p);
			RastSurfaceFill(surface, s, paintFill, shape->evenOddRule);

			RastPathDestroy(&p);
		}

		if (shape->strokeWidth && paintStroke.type) {
			IconPackPath *path = shape->paths;

			int pathCount = 0;

			while (path) {
				RastPath p = {};
				RastPathAppendBezier(&p, (RastVertex *) path->points, path->pointCount, scale2);
				RastPathTranslate(&p, translateX, translateY);
				RastShape s = RastShapeCreateContour(&p, contour, !path->closed);
				RastSurfaceFill(surface, s, paintStroke, false);
				path = path->next;
				RastPathDestroy(&p);
				pathCount++;
			}
		}

		nextShape:;

		RastGradientDestroy(&paintFill);
		RastGradientDestroy(&paintStroke);

		shapeIndex++;
		shape = shape->next;
	}

	RastSurfaceDestroy(&surface);
}

bool EsDrawStandardIcon(EsPainter *painter, uint32_t id, int size, EsRectangle region, uint32_t color) {
	if (!id) return false;
	id--;
	
	{
		// Center the icon.

		if (region.r - region.l > size) {
			int d = region.r - region.l - size;
			region.l += d / 2, region.r -= d / 2;
		}

		if (region.b - region.t > size) {
			int d = region.b - region.t - size;
			region.t += d / 2, region.b -= d / 2;
		}
	}

	int left = region.l, top = region.t;
	EsRectangleClip(region, painter->clip, &region);

	GlyphCacheKey key = {};
	key.glyphIndex = id;
	key.size = size;

	GlyphCacheEntry *cacheEntry = LookupGlyphCacheEntry(key);

	if (!cacheEntry) {
		return false;
	}

	if (!cacheEntry->data) {
		if (!iconManagement.standardPack) {
			iconManagement.standardPack = (const uint8_t *) EsBundleFind(&bundleDesktop, EsLiteral("Icons.dat"), &iconManagement.standardPackSize);
		}

		iconManagement.buffer = (char *) EsHeapAllocate((iconManagement.bufferAllocated = 131072), false);
		if (!iconManagement.buffer) return false;
		iconManagement.pack = { .in = iconManagement.standardPack, .bytes = iconManagement.standardPackSize };
		cacheEntry->width = size, cacheEntry->height = size;
		cacheEntry->dataBytes = size * size * 4;
		cacheEntry->data = (uint8_t *) EsHeapAllocate(cacheEntry->dataBytes, true);

		if (cacheEntry->data) {
			RegisterGlyphCacheEntry(key, cacheEntry);
			IconPackImage *image = IconPackReadImage(id, size, &cacheEntry->type);

			if (image) {
				DrawIcon(size, size, cacheEntry->data, image, size * 4, 0, 0, (float) size / image->width, (float) size / image->height);
			}
		} else {
			EsHeapFree(cacheEntry);
		}

		EsHeapFree(iconManagement.buffer);
	}

	DrawSingleCharacter(cacheEntry->width, cacheEntry->height, 0, 0, 
			ES_POINT(left, top), region, painter->target,  
			0, cacheEntry->type, false, cacheEntry->data,
			color, 0, 0, painter->target->fullAlpha);

	return true;
}

void EsDrawVectorFile(EsPainter *painter, EsRectangle bounds, const void *data, size_t dataBytes) {
	iconManagement.bufferPosition = 0;
	iconManagement.buffer = (char *) EsHeapAllocate((iconManagement.bufferAllocated = 131072), false);
	iconManagement.pack = { .in = (const uint8_t *) data, .bytes = dataBytes };

	if (!iconManagement.buffer) {
		return;
	}

	IconPackImage *image = (IconPackImage *) IconBufferAllocate(sizeof(IconPackImage));
	image->width = EsBufferReadFloat(&iconManagement.pack);
	image->height = EsBufferReadFloat(&iconManagement.pack);
	IconPackReadShape(&image->shapes);

	EsRectangle destination = EsRectangleIntersection(bounds, painter->clip);
	EsPaintTarget *target = painter->target;
	DrawIcon(Width(destination), Height(destination), (uint8_t *) target->bits + destination.l * 4 + destination.t * target->stride, image, target->stride, 
			bounds.l - destination.l, bounds.t - destination.t, (float) Width(bounds) / image->width, (float) Height(bounds) / image->height);

	EsHeapFree(iconManagement.buffer);
}

// --------------------------------- Text shaping.

TextStyleDifference CompareTextStyles(const EsTextStyle *style1, const EsTextStyle *style2) {
	if (!style1) return TEXT_STYLE_NEW_FONT;
	if (style1->font.family 	!= style2->font.family) 	return TEXT_STYLE_NEW_FONT;
	if (style1->font.weight 	!= style2->font.weight) 	return TEXT_STYLE_NEW_FONT;
	if (style1->font.flags 		!= style2->font.flags) 		return TEXT_STYLE_NEW_FONT;
	if (style1->size 		!= style2->size) 		return TEXT_STYLE_NEW_FONT;
	if (style1->baselineOffset 	!= style2->baselineOffset) 	return TEXT_STYLE_NEW_SHAPE;
	if (style1->tracking 		!= style2->tracking) 		return TEXT_STYLE_NEW_SHAPE;
	if (style1->figures 		!= style2->figures) 		return TEXT_STYLE_NEW_SHAPE;
	if (style1->alternateDirection	!= style2->alternateDirection)	return TEXT_STYLE_NEW_SHAPE;
	if (style1->color		!= style2->color)		return TEXT_STYLE_NEW_RENDER;
	if (style1->blur		!= style2->blur)		return TEXT_STYLE_NEW_RENDER;
	if (style1->decorations		!= style2->decorations)		return TEXT_STYLE_NEW_RENDER;
	if (style1->decorationsColor	!= style2->decorationsColor)	return TEXT_STYLE_NEW_RENDER;
	return TEXT_STYLE_IDENTICAL;
}

ptrdiff_t TextGetCharacterAtPoint(EsElement *element, const EsTextStyle *textStyle, const char *string, size_t stringBytes, int *_pointX, uint32_t flags) {
	// TODO Better integration with the EsTextPlan API.

	EsTextPlanProperties properties = {};
	EsTextRun textRuns[2] = {};
	textRuns[0].style = *textStyle;
	textRuns[1].offset = stringBytes;
	EsTextPlan *plan = EsTextPlanCreate(element, &properties, {}, string, textRuns, 1); 
	if (!plan) return 0;

	EsAssert(plan->lines.Length() == 1);
	bool useMiddle = flags & ES_TEXT_GET_CHARACTER_AT_POINT_MIDDLE;
	int pointX = *_pointX;
	pointX *= FREETYPE_UNIT_SCALE;
	int currentX = 0, priorMiddle = 0;
	ptrdiff_t result = -1;

	for (uintptr_t j = 0; j < plan->lines[0].pieceCount; j++) {
		TextPiece *piece = &plan->pieces[plan->lines[0].pieceOffset + j];
		TextGlyphInfo *glyphs = &plan->glyphInfos[piece->glyphOffset];
		TextGlyphPosition *glyphPositions = &plan->glyphPositions[piece->glyphOffset];

		for (uintptr_t i = 0; i < piece->glyphCount; i++) {
			int left = useMiddle ? priorMiddle : currentX;
			int right = currentX + glyphPositions[i].x_advance / (useMiddle ? 2 : 1);

			priorMiddle = right;

			if (pointX >= left && pointX < right) {
				result = glyphs[i].cluster;
				goto done;
			}

			currentX += glyphPositions[i].x_advance;
		}
	}

	done:;
	*_pointX = currentX / FREETYPE_UNIT_SCALE;
	EsTextPlanDestroy(plan);
	return result;
}

int TextGetPartialStringWidth(EsElement *element, const EsTextStyle *textStyle, const char *fullString, size_t fullStringBytes, size_t measureBytes) {
	// TODO Better integration with the EsTextPlan API.

	EsTextPlanProperties properties = {};
	EsTextRun textRuns[3] = {};
	textRuns[0].style = *textStyle;
	textRuns[1].style = *textStyle;
	textRuns[1].offset = measureBytes;
	textRuns[2].offset = fullStringBytes;
	EsTextPlan *plan = EsTextPlanCreate(element, &properties, {}, fullString, textRuns, 2); 
	if (!plan) return 0;

	int width = 0;
	EsAssert(plan->lines.Length() == 1);

	for (uintptr_t i = 0; i < plan->lines[0].pieceCount; i++) {
		TextPiece *piece = &plan->pieces[plan->lines[0].pieceOffset + i];

		if (piece->start < measureBytes) {
			width += piece->width;
		}
	}

	EsTextPlanDestroy(plan);
	return width / FREETYPE_UNIT_SCALE;
}

int TextGetStringWidth(EsElement *element, const EsTextStyle *textStyle, const char *string, size_t stringBytes) {
	return TextGetPartialStringWidth(element, textStyle, string, stringBytes, stringBytes);
}

void TextTrimSpaces(EsTextPlan *plan) {
	if (~plan->properties.flags & ES_TEXT_PLAN_TRIM_SPACES) {
		return;
	}

	for (uintptr_t i = 0; i < plan->lines.Length(); i++) {
		TextLine *line = &plan->lines[i];

		if (!line->pieceCount) {
			continue;
		}

		TextPiece *firstPiece = &plan->pieces[line->pieceOffset];
		TextPiece *lastPiece = &plan->pieces[line->pieceOffset + line->pieceCount - 1];

		while (firstPiece->glyphCount && firstPiece->start != firstPiece->end
				&& plan->glyphInfos[firstPiece->glyphOffset].cluster == firstPiece->start 
				&& plan->string[firstPiece->start] == ' ') {
			line->width -= plan->glyphPositions[firstPiece->glyphOffset].x_advance;
			firstPiece->width -= plan->glyphPositions[firstPiece->glyphOffset].x_advance;
			firstPiece->glyphOffset++;
			firstPiece->glyphCount--;
			firstPiece->start++;
		}

		while (lastPiece->glyphCount && lastPiece->start != lastPiece->end
				&& plan->glyphInfos[lastPiece->glyphOffset + lastPiece->glyphCount - 1].cluster == lastPiece->end - 1
				&& plan->string[lastPiece->end - 1] == ' ') {
			line->width -= plan->glyphPositions[lastPiece->glyphOffset + lastPiece->glyphCount - 1].x_advance;
			lastPiece->width -= plan->glyphPositions[lastPiece->glyphOffset + lastPiece->glyphCount - 1].x_advance;
			lastPiece->glyphCount--;
			lastPiece->end--;
		}
	}
}

void TextPlaceEmergencyBreaks(EsTextPlan *plan, int32_t maximumLineWidth) {
	if ((plan->properties.flags & ES_TEXT_PLAN_CLIP_UNBREAKABLE_LINES) || maximumLineWidth == -1) {
		return;
	}

	repeat:;
	TextLine *line = &plan->lines.Last();
	if (line->width <= maximumLineWidth) return;
	EsAssert(line->pieceCount >= 1);

	int32_t x = 0, x0 = 0;
	uintptr_t j, piece;

	for (piece = 0; piece < line->pieceCount; piece++) {
		TextPiece *p = &plan->pieces[line->pieceOffset + piece];
		x0 = x;

		for (j = 0; j < p->glyphCount; j++) {
			int32_t width = plan->glyphPositions[p->glyphOffset + j].x_advance;

			if (x + width > maximumLineWidth && (j || piece)) {
				goto foundBreakPoint;
			}

			x += width;
		}
	}

	return; // One glyph on the line; we can't do anything.
	foundBreakPoint:;

	// Split the line.

	TextPiece *piece0 = &plan->pieces[line->pieceOffset + piece];
	TextPiece  piece1 = *piece0;
	piece1.width = piece0->width - (x - x0);
	piece1.glyphOffset += j;
	piece1.glyphCount = piece0->glyphCount - j;
	piece1.start = plan->glyphInfos[piece0->glyphOffset + j].cluster;
	piece0->end = piece1.start;
	piece0->width = x - x0;
	piece0->glyphCount = j;
	plan->pieces.Insert(piece1, line->pieceOffset + piece + 1);

	TextLine *line0 = line;
	TextLine  line1 = *line;
	line1.width -= x;
	line0->width = x;
	line1.pieceOffset += piece + 1;
	line1.pieceCount = line0->pieceCount - piece;
	line0->pieceCount = piece + 1;
	plan->lines.Add(line1);

	goto repeat;
}

void TextAddEllipsis(EsTextPlan *plan, int32_t maximumLineWidth, bool needFinalEllipsis, int32_t boundsWidth) {
	if (!boundsWidth || (~plan->properties.flags & ES_TEXT_ELLIPSIS)) {
		return;
	}

	bool needEllipsis = false;

	if (maximumLineWidth == -1) {
		for (uintptr_t i = 0; i < plan->lines.Length(); i++) {
			if (plan->lines[i].width > boundsWidth * FREETYPE_UNIT_SCALE) {
				maximumLineWidth = boundsWidth * FREETYPE_UNIT_SCALE;
				needEllipsis = true;
			}
		}
	} else {
		// Word-wrapping was enabled so lines won't exceed the boundary width.
	}

	if (!needEllipsis && !needFinalEllipsis) {
		return;
	}

	uint8_t ellipsisUTF8[3] = { 0xE2, 0x80, 0xA6 };

	// Shape and measure the ellipsis character.

	unsigned int glyphCount;
	TextGlyphInfo *glyphInfos;
	TextGlyphPosition *glyphPositions;
	FontShapeText(plan, (const char *) ellipsisUTF8, sizeof(ellipsisUTF8), 0, sizeof(ellipsisUTF8), nullptr, 0, &glyphCount, &glyphInfos, &glyphPositions);
	
	int32_t ellipsisWidth = 0;

	for (uintptr_t i = 0; i < glyphCount; i++) {
		ellipsisWidth += glyphPositions[i].x_advance;
	}

	for (uintptr_t i = needEllipsis ? 0 : plan->lines.Length() - 1; i < plan->lines.Length(); i++) {
		TextLine *line = &plan->lines[i];

		if (i == plan->lines.Length() - 1 && needFinalEllipsis) {
			// The maximum number of lines was exceeded, and this is the last permitted line, so add an ellipsis.
		} else if (line->width > boundsWidth * FREETYPE_UNIT_SCALE) {
			// This line exceeds the width boundary (and hence word-wrapping was disabled), so add an ellipsis.
		} else {
			continue;
		}
			
		// Make space for the ellipsis.

		int32_t spaceNeeded = ellipsisWidth - (maximumLineWidth - line->width);

		while (line->pieceCount && spaceNeeded > 0) {
			TextPiece *piece = &plan->pieces[line->pieceOffset + line->pieceCount - 1];

			if (piece->isTabPiece) {
				spaceNeeded -= piece->width;
				line->pieceCount--;
			} else if (piece->start == piece->end || !piece->glyphCount) {
				line->pieceCount--;
			} else {
				piece->end = plan->glyphInfos[piece->glyphOffset + piece->glyphCount - 1].cluster;
				int32_t width = plan->glyphPositions[piece->glyphOffset + piece->glyphCount - 1].x_advance;
				spaceNeeded -= width, line->width -= width, piece->width -= width;
				piece->glyphCount--;

				while (piece->glyphCount) {
					if (plan->glyphInfos[piece->glyphOffset + piece->glyphCount - 1].cluster == piece->end) {
						// TODO Test this branch!
						int32_t width = plan->glyphPositions[piece->glyphOffset + piece->glyphCount - 1].x_advance;
						spaceNeeded -= width, line->width -= width, piece->width -= width;
						piece->glyphCount--;
					} else {
						break;
					}
				}
			}
		}

		// Add the ellipsis.

		TextPiece piece = {};
		piece.style = plan->currentTextStyle;
		piece.glyphOffset = plan->glyphInfos.Length();
		piece.ascent  =  FontGetAscent (&plan->font) + plan->currentTextStyle->baselineOffset, 
		piece.descent = -FontGetDescent(&plan->font) - plan->currentTextStyle->baselineOffset;

		for (uintptr_t i = 0; i < glyphCount; i++) {
			if (!plan->glyphInfos.Add(glyphInfos[i])) break;
			if (!plan->glyphPositions.Add(glyphPositions[i])) break;
			piece.glyphCount++;
			int32_t width = glyphPositions[i].x_advance;
			piece.width += width, line->width += width;
		}

		line->hasEllipsis = true;
		line->ellipsisPieceIndex = plan->pieces.Length();
		plan->pieces.Add(piece);
	}

	FontShapeTextDone(plan, glyphCount, glyphInfos, glyphPositions);
}

void TextItemizeByScript(EsTextPlan *plan, const EsTextRun *runs, size_t runCount, float sizeScaleFactor) {
	uint32_t lastAssignedScript = FALLBACK_SCRIPT;

	for (uintptr_t i = 0; i < runCount; i++) {
		uintptr_t offset = runs[i].offset;

		for (uintptr_t j = offset; j < runs[i + 1].offset;) {
			uint32_t codepoint = utf8_value(plan->string + j);
			uint32_t script;
			bool inheritingScript = false;

			if (codepoint == '\t') {
				// Tab characters should go in their own section.
				script = '\t';
			} else {
				script = FontGetScriptFromCodepoint(codepoint, &inheritingScript);
			}

			if (inheritingScript) {
				// TODO If this is a closing character, restore the last assigned script before the most recent opening character.
				script = lastAssignedScript == '\t' ? FALLBACK_SCRIPT : lastAssignedScript;
			}

			if (lastAssignedScript != script && j != runs[i].offset) {
				TextRun run = {};
				run.style = runs[i].style;
				run.offset = offset;
				run.script = lastAssignedScript;
				run.style.font.family = FontApplySubstitution(&plan->properties, run.style.font.family, run.script);
				run.style.size *= sizeScaleFactor;
				plan->textRuns.Add(run);
				offset = j;
			}

			lastAssignedScript = script;
			j = utf8_advance(plan->string + j) - plan->string;
		}

		TextRun run = {};
		run.style = runs[i].style;
		run.offset = offset;
		run.script = lastAssignedScript;
		run.style.font.family = FontApplySubstitution(&plan->properties, run.style.font.family, run.script);
		run.style.size *= sizeScaleFactor;
		plan->textRuns.Add(run);
	}

	TextRun run = {};
	run.offset = runs[runCount].offset;
	plan->textRuns.Add(run);
}

void TextUpdateFont(EsTextPlan *plan, const EsTextStyle *style) {
	if (TEXT_STYLE_NEW_FONT == CompareTextStyles(plan->currentTextStyle, style)) {
		plan->font = FontGet(style->font);
		FontSetSize(&plan->font, style->size);
	}

	plan->currentTextStyle = style;
}

int32_t TextExpandTabs(EsTextPlan *plan, uintptr_t pieceOffset, int32_t width) {
	int32_t addedWidth = 0;

	for (uintptr_t i = pieceOffset; i < plan->pieces.Length(); i++) {
		TextPiece *piece = &plan->pieces[i];

		if (piece->isTabPiece) {
			TextUpdateFont(plan, piece->style);
			int32_t emWidth = FontGetEmWidth(&plan->font) * FREETYPE_UNIT_SCALE;
			int32_t tabWidth = emWidth * 4; // TODO Make spaces-per-tab customizable.
			int32_t firstWidth = emWidth + tabWidth - (width + emWidth) % tabWidth;
			piece->width = firstWidth + tabWidth * (piece->end - piece->start - 1);
			addedWidth += piece->width;
			piece->glyphOffset = plan->glyphInfos.Length();
			piece->glyphCount = 0;
			piece->ascent = FontGetAscent(&plan->font);
			piece->descent = -FontGetDescent(&plan->font);

			for (uintptr_t i = 0; i < piece->end - piece->start; i++) {
				TextGlyphInfo info = {};
				info.cluster = piece->start + i;
				info.codepoint = 0xFFFFFFFF;
				TextGlyphPosition position = {};
				position.x_advance = i ? tabWidth : firstWidth;
				if (!plan->glyphInfos.Add(info)) break;
				if (!plan->glyphPositions.Add(position)) break;
				piece->glyphCount++;
			}
		}

		width += piece->width;
	}

	return addedWidth;
}

int32_t TextBuildTextPieces(EsTextPlan *plan, uintptr_t sectionStart, uintptr_t sectionEnd) {
	// Find the first run that contains the section.

	for (; plan->textRunPosition < plan->textRuns.Length() - 1; plan->textRunPosition++) {
		if (plan->textRuns[plan->textRunPosition].offset <= sectionStart && plan->textRuns[plan->textRunPosition + 1].offset > sectionStart) {
			break;
		}
	}

	EsAssert(plan->textRunPosition != plan->textRuns.Length() - 1);

	// Iterate through each run in the section.

	int32_t width = 0;

	while (plan->textRunPosition != plan->textRuns.Length() - 1) {
		TextRun *run = &plan->textRuns[plan->textRunPosition];

		uintptr_t start = sectionStart > run[0].offset ? sectionStart : run[0].offset;
		uintptr_t end   = sectionEnd   < run[1].offset ? sectionEnd   : run[1].offset;

		if (end <= start) {
			break;
		}

		// Update the font to match the run.

		TextUpdateFont(plan, &run->style);

		// Don't shape newline characters.

		while (start < end && plan->string[start] == '\n') start++;
		while (end - 1 > start && plan->string[end - 1] == '\n') end--;

		if (end == start) {
			plan->textRunPosition++;
			continue;
		}

		EsAssert(end > start);

		// Handle tab characters specially.

		if (plan->string[start] == '\t') {
			TextPiece _piece = {};

			if (plan->pieces.Add(_piece)) {
				TextPiece *piece = &plan->pieces.Last();
				piece->style = plan->currentTextStyle;
				piece->glyphOffset = 0;
				piece->glyphCount = 0;
				piece->start = start;
				piece->end = end;
				piece->isTabPiece = true;
			}

			plan->textRunPosition++;
			continue;
		}

		// Shape the run.

		TextFeature features[4] = {};
		size_t featureCount = 0;

#ifdef USE_FREETYPE_AND_HARFBUZZ
		if (plan->currentTextStyle->figures == ES_TEXT_FIGURE_OLD) hb_feature_from_string("onum", -1, features + (featureCount++));
		if (plan->currentTextStyle->figures == ES_TEXT_FIGURE_TABULAR) hb_feature_from_string("tnum", -1, features + (featureCount++));
#endif
		plan->segmentProperties.script = (TextScript) run->script;

		unsigned int glyphCount;
		TextGlyphInfo *glyphInfos;
		TextGlyphPosition *glyphPositions;
		FontShapeText(plan, plan->string, plan->breaker.bytes, start, end - start, features, featureCount, &glyphCount, &glyphInfos, &glyphPositions);

		// Create the text piece.

		TextPiece _piece = {};

		if (plan->pieces.Add(_piece)) {
			TextPiece *piece = &plan->pieces.Last();
			piece->style = plan->currentTextStyle;
			piece->glyphOffset = plan->glyphInfos.Length();
			piece->glyphCount = 0;
			piece->ascent  =  FontGetAscent (&plan->font) + plan->currentTextStyle->baselineOffset;
			piece->descent = -FontGetDescent(&plan->font) - plan->currentTextStyle->baselineOffset;
			piece->start = start;
			piece->end = end;

			for (uintptr_t i = 0; i < glyphCount; i++) {
				if (!plan->glyphInfos.Add(glyphInfos[i])) break;
				if (!plan->glyphPositions.Add(glyphPositions[i])) break;
				piece->glyphCount++;

				piece->width += glyphPositions[i].x_advance;

				if (i == glyphCount - 1 || glyphInfos[i].cluster != glyphInfos[i + 1].cluster) {
					piece->width += plan->currentTextStyle->tracking * FREETYPE_UNIT_SCALE;
				}

				// EsPrint("\t%d\n", glyphInfos[i].codepoint);
			}

			width += piece->width;
		}

		// Go to the next run.

		plan->textRunPosition++;

		FontShapeTextDone(plan, glyphCount, glyphInfos, glyphPositions);
	}

	plan->textRunPosition--;

	return width;
}

void TextPlanDestroy(EsTextPlan *plan) {
	plan->glyphInfos.Free();
	plan->glyphPositions.Free();
	plan->pieces.Free();
	plan->lines.Free();
	plan->textRuns.Free();
}

EsTextPlan *EsTextPlanCreate(EsElement *element, EsTextPlanProperties *properties, EsRectangle bounds, const char *string, const EsTextRun *formatRuns, size_t formatRunCount) {
	// TODO Bidirectional text (UAX9). 
	// TODO Vertical text layout (UAX50).
	// TODO Supporting arbitrary OpenType features.
	// TODO Reshaping lines once word wrapping is applied.
	// TODO Don't break lines in the middle of emails/URLs.

	// EsPrint("EsTextPlanCreate... width %d\n", Width(bounds) * FREETYPE_UNIT_SCALE);

	EsMessageMutexCheck();
	EsAssert(element);
	float scale = theming.scale; // TODO Get the scale factor from the element's window.

	EsTextPlan plan = {};

	// Initialise the line breaker.

	plan.breaker.string = string;
	plan.breaker.bytes = formatRuns[formatRunCount].offset;
	EsAssert(plan.breaker.bytes < 0x80000000);

	if (!plan.breaker.bytes) {
		return nullptr; // Empty input.
	}

	// Initialise the plan.

	plan.string = string;
	plan.singleUse = properties->flags & ES_TEXT_PLAN_SINGLE_USE;
	plan.properties = *properties;

	TextLine blankLine = {};

	if (!plan.lines.Add(blankLine)) {
		return nullptr;
	}

	// Setup the HarfBuzz buffer.

	FontInitialiseShaping(&plan);

	// Subdivide the runs by character script.
	// This is also responsible for scaling the text sizes.
	
	TextItemizeByScript(&plan, formatRuns, formatRunCount, scale);

	// Layout the paragraph.

	int32_t maximumLineWidth = Width(bounds) && (properties->flags & ES_TEXT_WRAP) ? Width(bounds) * FREETYPE_UNIT_SCALE : -1;
	Break previousBreak = {};
	bool needEllipsis = false;

	while (previousBreak.position != plan.breaker.bytes) {
		// Find the next break opportunity.

		Break nextBreak = plan.breaker.Next();

		while (!plan.breaker.error && !nextBreak.forced && (~properties->flags & ES_TEXT_WRAP)) {
			nextBreak = plan.breaker.Next();
		}

		if (plan.breaker.error) {
			break;
		}

		// Build the text pieces for this section.

		uintptr_t pieceOffset = plan.pieces.Length();
		int32_t width = TextBuildTextPieces(&plan, previousBreak.position, nextBreak.position);
		width += TextExpandTabs(&plan, pieceOffset, plan.lines.Last().width);

		// Should we start a new line?

		if (previousBreak.forced || (maximumLineWidth != -1 && plan.lines.Last().width + width > maximumLineWidth)) {
			if (properties->maxLines == (int32_t) plan.lines.Length()) {
				needEllipsis = true;
				break;
			}

			if (plan.lines.Add(blankLine)) {
				plan.lines.Last().pieceOffset = pieceOffset;
			}
		}

#if 0
		EsPrint("\tadded section '%s' to line %d (%d pieces) at x=%d\n", 
				nextBreak.position - previousBreak.position, string + previousBreak.position, 
				ArrayLength(plan.lines) - 1, ArrayLength(plan.pieces) - pieceOffset, 
				plan.lines.Last().width);
#endif

		// Add the pieces to the line.

		TextLine *line = &plan.lines.Last();
		TextExpandTabs(&plan, pieceOffset, line->width);

		for (uintptr_t i = pieceOffset; i < plan.pieces.Length(); i++) {
			line->width += plan.pieces[i].width;
			line->pieceCount++;
		}

		TextPlaceEmergencyBreaks(&plan, maximumLineWidth);

		// Go to the next section.

		previousBreak = nextBreak;
	}

	// Calculate the ascent/descent of each line.

	for (uintptr_t i = 0; i < plan.lines.Length(); i++) {
		TextLine *line = &plan.lines[i];

		if (!line->pieceCount && i) {
			// If the line doesn't have any pieces, it must be from a double newline.
			// Inherit the ascent/descent of the previous line.

			line->ascent = line[-1].ascent;
			line->descent = line[-1].descent;
		}

		for (uintptr_t i = line->pieceOffset; i < line->pieceOffset + line->pieceCount; i++) {
			if (line->ascent < plan.pieces[i].ascent) line->ascent = plan.pieces[i].ascent;
			if (line->descent < plan.pieces[i].descent) line->descent = plan.pieces[i].descent;
		}
	}

	// Trim leading and trailing spaces.
	
	TextTrimSpaces(&plan);

	// Add a terminating ellipsis.

	TextAddEllipsis(&plan, maximumLineWidth, needEllipsis, Width(bounds));

	// Calculate the total width and height.

	for (uintptr_t i = 0; i < plan.lines.Length(); i++) {
		plan.totalHeight += plan.lines[i].ascent + plan.lines[i].descent;

		if (plan.lines[i].width > plan.totalWidth) {
			plan.totalWidth = plan.lines[i].width;
		}
	}

	// Destroy the HarfBuzz buffer.

	FontDestroyShaping(&plan);
	
	// Return the plan.

	EsTextPlan *copy = (EsTextPlan *) EsHeapAllocate(sizeof(EsTextPlan), true);
	
	if (copy) {
		*copy = plan;
		return copy;
	} else {
		TextPlanDestroy(&plan);
		return nullptr;
	}
}

void EsTextPlanDestroy(EsTextPlan *plan) {
	EsMessageMutexCheck();
	EsAssert(!plan->singleUse);
	TextPlanDestroy(plan);
	EsHeapFree(plan);
}

void EsTextPlanReplaceStyleRenderProperties(EsTextPlan *plan, const EsTextStyle *style) {
	for (uintptr_t i = 0; i < plan->textRuns.Length() - 1; i++) {
		plan->textRuns[i].style.color = style->color;
		plan->textRuns[i].style.blur = style->blur;
		plan->textRuns[i].style.decorations = style->decorations;
		plan->textRuns[i].style.decorationsColor = style->decorationsColor;
	}
}

int EsTextPlanGetWidth(EsTextPlan *plan) {
	return (plan->totalWidth + FREETYPE_UNIT_SCALE - 1) / FREETYPE_UNIT_SCALE;
}

int EsTextPlanGetHeight(EsTextPlan *plan) {
	return (plan->totalHeight + FREETYPE_UNIT_SCALE - 1) / FREETYPE_UNIT_SCALE;
}

size_t EsTextPlanGetLineCount(EsTextPlan *plan) {
	return plan->lines.Length();
}

EsTextStyle TextPlanGetPrimaryStyle(EsTextPlan *plan) {
	return plan->textRuns[0].style;
}

void DrawTextPiece(EsPainter *painter, EsTextPlan *plan, TextPiece *piece, TextLine *line,
		int32_t cursorX, int32_t cursorY, 
		const EsTextSelection *selection, uintptr_t caret, int32_t selectionBackgroundBottom) {
	if (cursorX / FREETYPE_UNIT_SCALE > painter->clip.r 
			|| (cursorX + piece->width) / FREETYPE_UNIT_SCALE < painter->clip.l
			|| cursorY > painter->clip.b
			|| (cursorY + (piece->ascent + piece->descent) / FREETYPE_UNIT_SCALE) < painter->clip.t) {
		return;
	}

#if 0
	EsPrint("\tdrawing piece; '%s' on line %d glyphOffset %d and glyphCount %d at %i, %i with caret %d\n", 
			piece->end - piece->start, plan->string + piece->start,
			line - plan->lines, piece->glyphOffset, piece->glyphCount, 
			cursorX / FREETYPE_UNIT_SCALE, cursorY, caret);
#endif

	// Prevent issues with negative numbers getting rounded differently...
	int32_t cursorXIntegerOffset = -(0x40000000 / FREETYPE_UNIT_SCALE);
	cursorX += 0x40000000;
	int32_t cursorXStart = cursorX;

	TextGlyphInfo *glyphs = &plan->glyphInfos[piece->glyphOffset];
	TextGlyphPosition *glyphPositions = &plan->glyphPositions[piece->glyphOffset];

	// Update the font to match the piece.

	TextUpdateFont(plan, piece->style);

	// Draw the selection background.

	if (selection->caret0 != selection->caret1 && !selection->hideCaret) {
		int sCursorX = cursorX, selectionStartX = -1, selectionEndX = -1;

		for (uintptr_t i = 0; i < piece->glyphCount; i++) {
			if (selectionStartX == -1 && (int32_t) glyphs[i].cluster >= selection->caret0) {
				selectionStartX = sCursorX;
			}

			if (selectionEndX == -1 && (int32_t) glyphs[i].cluster >= selection->caret1) {
				selectionEndX = sCursorX;
			}

			sCursorX += glyphPositions[i].x_advance;

			if (i == piece->glyphCount - 1 || glyphs[i].cluster != glyphs[i + 1].cluster) {
				sCursorX += plan->currentTextStyle->tracking;
			}
		}

		if (selectionStartX == -1 && selection->caret0 >= 0) {
			selectionStartX = sCursorX;
		}

		if (selectionEndX == -1) {
			selectionEndX = sCursorX;
		}

		EsRectangle s;
		s.l = (selectionStartX + FREETYPE_UNIT_SCALE / 2) / FREETYPE_UNIT_SCALE + cursorXIntegerOffset;
		s.t = cursorY;
		s.r = (selectionEndX + FREETYPE_UNIT_SCALE / 2) / FREETYPE_UNIT_SCALE + cursorXIntegerOffset;
		s.b = selectionBackgroundBottom;
		EsDrawBlock(painter, s, selection->background);
	}

	// Draw each glyph in the piece.

	int32_t caretX = -1, caretY = cursorY;

	for (uintptr_t i = 0; i < piece->glyphCount; i++) {
		uint32_t codepoint = glyphs[i].codepoint;

		int positionX = (glyphPositions[i].x_offset + cursorX) / FREETYPE_UNIT_SCALE + cursorXIntegerOffset, 
		    positionY = ((glyphPositions[i].y_offset + FREETYPE_UNIT_SCALE / 2) / FREETYPE_UNIT_SCALE + cursorY);
		uint32_t color = plan->currentTextStyle->color;

		GlyphCacheKey key = {};
		key.glyphIndex = codepoint;
		key.size = plan->currentTextStyle->size;
		key.font = plan->font;
		GlyphCacheEntry *entry = nullptr;

		if (codepoint == 0xFFFFFFFF || key.size > 2000) {
			goto nextCharacter;
		}

		if (key.size > 25) {
			key.fractionalPosition = 0;
		} else if (key.size > 15) {
			key.fractionalPosition = ((glyphPositions[i].x_offset + cursorX) & 0x3F) & 0x20;
		} else {
			key.fractionalPosition = ((glyphPositions[i].x_offset + cursorX) & 0x3F) & 0x30;
		}

		entry = LookupGlyphCacheEntry(key);

		if (!entry) {
			goto nextCharacter;
		}

		if (!entry->data) {
			if (!FontRenderGlyph(key, entry)) {
				EsHeapFree(entry);
				goto nextCharacter;
			} else {
				RegisterGlyphCacheEntry(key, entry);
			}
		}

		if (selection->caret0 != selection->caret1 && !selection->hideCaret 
				&& (int32_t) glyphs[i].cluster >= selection->caret0 && (int32_t) glyphs[i].cluster < selection->caret1
				&& selection->foreground) {
			color = selection->foreground;
		}

		// EsPrint("\t%c at %i.%i\n", plan->string[glyphs[i].cluster], positionX, (glyphPositions[i].x_offset + cursorX) & 0x3F);

		DrawSingleCharacter(entry->width, entry->height, entry->xoff, entry->yoff, 
				ES_POINT(positionX, positionY + line->ascent / FREETYPE_UNIT_SCALE), 
				painter->clip, painter->target, 
				plan->currentTextStyle->blur, 
				CHARACTER_SUBPIXEL, 
				false, entry->data, 
				color, 0, -1, painter->target->fullAlpha);

		nextCharacter:;

		if (caretX == -1 && glyphs[i].cluster >= caret) {
			caretX = (cursorX + FREETYPE_UNIT_SCALE / 2) / FREETYPE_UNIT_SCALE;
		}

		cursorX += glyphPositions[i].x_advance;
		cursorY += (glyphPositions[i].y_advance + FREETYPE_UNIT_SCALE / 2) / FREETYPE_UNIT_SCALE;

		if (i == piece->glyphCount - 1 || glyphs[i].cluster != glyphs[i + 1].cluster) {
			cursorX += plan->currentTextStyle->tracking * FREETYPE_UNIT_SCALE;
		}
	}

	if (caretX == -1) {
		caretX = (cursorX + FREETYPE_UNIT_SCALE / 2) / FREETYPE_UNIT_SCALE;
	}

	// Draw the caret.

	if (!selection->hideCaret && caret >= piece->start 
			&& (caret < piece->end || (caret == piece->end && piece == &plan->pieces[line->pieceOffset + line->pieceCount - 1]))) {
		caretX += cursorXIntegerOffset;

		if (selection->snapCaretToInsets && selection->caret0 == selection->caret1) {
			EsRectangle insets = EsPainterBoundsInset(painter); 
			// EsPrint("%d, %d, %d\n", caretX + bounds.l, insets.l, insets.r);

			if (caretX >= insets.l - 1 && caretX <= insets.l + 1) {
				caretX = insets.l;
			} else if (caretX >= insets.r - 2 && caretX <= insets.r) {
				caretX = insets.r - 1;
			}
		}

		int caretWidth = theming.scale; // TODO Make this a system constant.
		EsDrawInvert(painter, ES_RECT_4(caretX, caretX + caretWidth, caretY, selectionBackgroundBottom));
	}

	// Draw decorations.

	{
		int32_t thickness = piece->style->size / 15 + 1;
		uint32_t color = piece->style->decorationsColor ?: piece->style->color;

		EsRectangle bounds;
		bounds.l = cursorXStart / FREETYPE_UNIT_SCALE + cursorXIntegerOffset;
		bounds.r = cursorX / FREETYPE_UNIT_SCALE + cursorXIntegerOffset;

		if (piece->style->decorations & ES_TEXT_DECORATION_STRIKE_THROUGH) {
			int32_t center = cursorY + (line->ascent + line->descent) / FREETYPE_UNIT_SCALE / 2;
			bounds.t = center - thickness / 2 + 1;
			bounds.b = center + (thickness + 1) / 2 + 1;
			EsDrawBlock(painter, bounds, color);
		}

		if (piece->style->decorations & ES_TEXT_DECORATION_UNDERLINE) {
			int32_t baseline = cursorY + line->ascent / FREETYPE_UNIT_SCALE;
			bounds.t = baseline + thickness;
			bounds.b = baseline + thickness * 2;
			EsDrawBlock(painter, bounds, color);
		}
	}
}

void EsDrawText(EsPainter *painter, EsTextPlan *plan, EsRectangle bounds, const EsRectangle *_clip, const EsTextSelection *_selection) {
	EsMessageMutexCheck();

	if (!plan) return;

	// EsPrint("EsDrawText... '%s' in %R\n", plan->textRuns[plan->textRunCount].offset, plan->string, bounds);

	// TODO Underlined text.
	// TODO Inline images and icons.

	// Work out the selection we should display.

	EsTextSelection selection = {};
	if (_selection) selection = *_selection;
	uintptr_t caret = selection.caret1;

	if (selection.caret0 > selection.caret1) {
		int swap = selection.caret1;
		selection.caret1 = selection.caret0;
		selection.caret0 = swap;
	} else if (!_selection) {
		selection.hideCaret = true;
	}

	// Calculate the area we're drawing into.

	int32_t maximumLineWidth = Width(bounds), maximumHeight = Height(bounds);
	EsRectangle oldClip = painter->clip;
	if (_clip) EsRectangleClip(*_clip, painter->clip, &painter->clip);
	int32_t cursorY = (plan->properties.flags & ES_TEXT_V_CENTER) ? (maximumHeight - EsTextPlanGetHeight(plan)) / 2 
		: (plan->properties.flags & ES_TEXT_V_BOTTOM) ? maximumHeight - EsTextPlanGetHeight(plan) : 0;

	// Iterate through each line.

	for (uintptr_t i = 0; i < plan->lines.Length(); i++) {
		TextLine *line = &plan->lines[i];

		int32_t cursorX = (plan->properties.flags & ES_TEXT_H_CENTER) ? ((maximumLineWidth * FREETYPE_UNIT_SCALE - line->width) / 2)
			: (plan->properties.flags & ES_TEXT_H_RIGHT) ? (maximumLineWidth * FREETYPE_UNIT_SCALE - line->width) : 0;

		int32_t selectionBackgroundBottom;
		
		if (plan->lines.Length() == 1 && (plan->properties.flags & ES_TEXT_V_CENTER)) {
			// If this is a single, centered line, make sure that the selection background bottom edge
			// is the same distance from the destination bounds as it is for the top edge.
			selectionBackgroundBottom = bounds.b - cursorY;
		} else {
			selectionBackgroundBottom = cursorY + bounds.t + (line->ascent + line->descent + FREETYPE_UNIT_SCALE / 2) / FREETYPE_UNIT_SCALE;
		}

		// Draw each text piece on the line.

		for (uintptr_t j = 0; j < line->pieceCount; j++) {
			TextPiece *piece = &plan->pieces[line->pieceOffset + j];
			DrawTextPiece(painter, plan, piece, line, cursorX + bounds.l * FREETYPE_UNIT_SCALE, cursorY + bounds.t, &selection, caret, selectionBackgroundBottom);
			cursorX += piece->width;
		}

		if (line->hasEllipsis) {
			TextPiece *piece = &plan->pieces[line->ellipsisPieceIndex];
			DrawTextPiece(painter, plan, piece, line, cursorX + bounds.l * FREETYPE_UNIT_SCALE, cursorY + bounds.t, &selection, caret, selectionBackgroundBottom);
			cursorX += piece->width;
		}

		cursorY += (line->ascent + line->descent + FREETYPE_UNIT_SCALE / 2) / FREETYPE_UNIT_SCALE;
	}

	// Destroy the plan if it is single use.

	if (plan->singleUse) {
		plan->singleUse = false;
		EsTextPlanDestroy(plan);
	}

	painter->clip = oldClip;
}

void EsDrawTextSimple(EsPainter *painter, EsElement *element, EsRectangle bounds, const char *string, ptrdiff_t stringBytes, EsTextStyle style, uint32_t flags) {
	EsTextPlanProperties properties = {};
	properties.flags = ES_TEXT_PLAN_SINGLE_USE | flags;
	EsTextRun textRuns[2] = {};
	textRuns[0].style = style;
	textRuns[1].offset = stringBytes == -1 ? EsCStringLength(string) : stringBytes;
	if (!textRuns[1].offset) return;
	EsDrawText(painter, EsTextPlanCreate(element, &properties, bounds, string, textRuns, 1), bounds); 
}

void EsDrawTextThemed(EsPainter *painter, EsElement *element, EsRectangle bounds, const char *string, ptrdiff_t stringBytes, EsStyleID style, uint32_t flags) {
	EsTextStyle textStyle;
	GetStyle(MakeStyleKey(style, 0), true)->GetTextStyle(&textStyle);
	EsDrawTextSimple(painter, element, bounds, string, stringBytes, textStyle, flags); 
}

// --------------------------------- Markup parsing.

void EsRichTextParse(const char *inString, ptrdiff_t inStringBytes, 
		char **outString, EsTextRun **outTextRuns, size_t *outTextRunCount,
		EsTextStyle *baseStyle) {
	if (inStringBytes == -1) {
		inStringBytes = EsCStringLength(inString);
	}

	// Step 1: Count the number of runs, and the number of bytes in the actual string.

	size_t textRunCount = 1;
	size_t stringBytes = 0;

	for (ptrdiff_t i = 0; i < inStringBytes; i++) {
		if (inString[i] == '\a') {
			for (; i < inStringBytes; i++) {
				if (inString[i] == ']') {
					break;
				}
			}

			textRunCount++;
		} else {
			stringBytes++;
		}
	}

	// Step 2: Allocate the string and text runs array.

	char *string = (char *) EsHeapAllocate(stringBytes, false);
	EsTextRun *textRuns = (EsTextRun *) EsHeapAllocate((textRunCount + 1) * sizeof(EsTextRun), false);

	textRuns[0].style = *baseStyle;
	textRuns[0].offset = 0;
	textRuns[textRunCount].offset = stringBytes;

	// Step 3: Copy the information.

	uintptr_t textRunIndex = 1;
	uintptr_t stringIndex = 0;

	for (ptrdiff_t i = 0; i < inStringBytes; i++) {
		if (inString[i] == '\a') {
			EsTextRun *textRun = textRuns + textRunIndex;
			textRun->offset = stringIndex;
			textRun->style = *baseStyle;

			for (; i < inStringBytes; i++) {
				char c = inString[i];

				if (c == ']') {
					break;
				} else if (c == 'w' /* weight */) {
					i++; if (i >= inStringBytes || inString[i] == ']') goto parsedFormat;
					textRun->style.font.weight = inString[i] - '0';
				} else if (c == 'i' /* italic */) {
					textRun->style.font.flags |= ES_FONT_ITALIC;
				} else if (c == 's' /* size */) {
					textRun->style.size = 0;

					while (true) {
						i++; if (i >= inStringBytes || inString[i] == ']') goto parsedFormat;
						if (inString[i] < '0' || inString[i] > '9') { i--; break; }
						textRun->style.size *= 10;
						textRun->style.size += inString[i] - '0';
					}
				} else if (c == 'm' /* monospaced */) {
					textRun->style.font.family = ES_FONT_MONOSPACED;
				} else if (c == '-' /* strike-through */) {
					textRun->style.decorations |= ES_TEXT_DECORATION_STRIKE_THROUGH;
				} else if (c == '_' /* underline */) {
					textRun->style.decorations |= ES_TEXT_DECORATION_UNDERLINE;
				} else if (c == '2' /* secondary color */) {
					textRun->style.color = GetConstantNumber("textSecondary");
				} else if (c == '#' /* custom color */) {
					char string[9];
					size_t bytes = 0;

					while (bytes < sizeof(string)) {
						if (i >= inStringBytes || inString[i] == ']') break;
						string[bytes++] = inString[i++];
					}

					textRun->style.color = EsColorParse(string, bytes);
					goto parsedFormat;
				}
			}

			parsedFormat:;
			textRunIndex++;
		} else {
			string[stringIndex++] = inString[i];
		}
	}

	EsAssert(textRunIndex == textRunCount && stringIndex == stringBytes);

	// Step 4: Return the parsed information.

	*outString = string;
	*outTextRuns = textRuns;
	*outTextRunCount = textRunCount;
}

// --------------------------------- Syntax highlighting.

const char *const keywords_a[] = { "auto", nullptr };
const char *const keywords_b[] = { "bool", "break", nullptr };
const char *const keywords_c[] = { "case", "char", "const", "continue", nullptr };
const char *const keywords_d[] = { "default", "do", "double", nullptr };
const char *const keywords_e[] = { "else", "enum", "extern", nullptr };
const char *const keywords_f[] = { "float", "for", nullptr };
const char *const keywords_g[] = { "goto", nullptr };
const char *const keywords_h[] = { nullptr };
const char *const keywords_i[] = { "if", "inline", "int", "int16_t", "int32_t", "int64_t", "int8_t", "intptr_t", nullptr };
const char *const keywords_j[] = { nullptr };
const char *const keywords_k[] = { nullptr };
const char *const keywords_l[] = { "long", nullptr };
const char *const keywords_m[] = { nullptr };
const char *const keywords_n[] = { nullptr };
const char *const keywords_o[] = { nullptr };
const char *const keywords_p[] = { nullptr };
const char *const keywords_q[] = { nullptr };
const char *const keywords_r[] = { "register", "restrict", "return", nullptr };
const char *const keywords_s[] = { "short", "signed", "sizeof", "static", "struct", "switch", nullptr };
const char *const keywords_t[] = { "typedef", nullptr };
const char *const keywords_u[] = { "uint16_t", "uint32_t", "uint64_t", "uint8_t", "uintptr_t", "union", "unsigned", nullptr };
const char *const keywords_v[] = { "void", "volatile", nullptr };
const char *const keywords_w[] = { "while", nullptr };
const char *const keywords_x[] = { nullptr };
const char *const keywords_y[] = { nullptr };
const char *const keywords_z[] = { nullptr };

const char *const *const keywords[] = {
	keywords_a, keywords_b, keywords_c, keywords_d,
	keywords_e, keywords_f, keywords_g, keywords_h,
	keywords_i, keywords_j, keywords_k, keywords_l,
	keywords_m, keywords_n, keywords_o, keywords_p,
	keywords_q, keywords_r, keywords_s, keywords_t,
	keywords_u, keywords_v, keywords_w, keywords_x,
	keywords_y, keywords_z,
};

bool CharIsAlphaOrDigitOrUnderscore(char c) {
	return EsCRTisalpha(c) || EsCRTisdigit(c) || c == '_';
}

Array<EsTextRun> TextApplySyntaxHighlighting(const EsTextStyle *baseStyle, int language, uint32_t *colors, Array<EsTextRun> runs, const char *string, size_t bytes) {
	// TODO Make these colors customizable!
	// TODO Highlight keywords.

	int lexState = 0;
	bool inComment = false, inIdentifier = false, inChar = false, startedString = false;
	bool seenEquals = false;
	uint32_t last = 0;

	for (uintptr_t index = 0; index < bytes; index++) {
		char c = index >= bytes ? 0 : string[index];
		char c1 = index >= bytes - 1 ? 0 : string[index + 1];
		last <<= 8;
		last |= c;

		if (c == '\n') {
			lexState = 0;
			inComment = false, inIdentifier = false, inChar = false, startedString = false;
			seenEquals = false;
			last = 0;
		}

		if (language == ES_SYNTAX_HIGHLIGHTING_LANGUAGE_C) {
			if (lexState == 4) {
				lexState = 0;
			} else if (lexState == 1) {
				if ((last & 0xFF0000) == ('*' << 16) && (last & 0xFF00) == ('/' << 8) && inComment) {
					lexState = 0, inComment = false;
				}
			} else if (lexState == 3 || lexState == 6) {
				if (!CharIsAlphaOrDigitOrUnderscore(c)) {
					lexState = 0;
				}
			} else if (lexState == 2) {
				if (!startedString) {
					if (!inChar && ((last >> 8) & 0xFF) == '"' && ((last >> 16) & 0xFF) != '\\') {
						lexState = 0;
					} else if (inChar && ((last >> 8) & 0xFF) == '\'' && ((last >> 16) & 0xFF) != '\\') {
						lexState = 0;
					}
				}

				startedString = false;
			}

			if (lexState == 0) {
				if (c == '#') {
					lexState = 5;
				} else if (c == '/' && c1 == '/') {
					lexState = 1;
				} else if (c == '/' && c1 == '*') {
					lexState = 1, inComment = true;
				} else if (c == '"') {
					lexState = 2;
					inChar = false;
					startedString = true;
				} else if (c == '\'') {
					lexState = 2;
					inChar = true;
					startedString = true;
				} else if (EsCRTisdigit(c) && !inIdentifier) {
					lexState = 3;
				} else if (!CharIsAlphaOrDigitOrUnderscore(c)) {
					lexState = 4;
					inIdentifier = false;
				} else {
					inIdentifier = true;

					if (c >= 'a' && c <= 'z' && (!index || !CharIsAlphaOrDigitOrUnderscore(string[index - 1]))) {
						const char *const *k = keywords[c - 'a'];

						for (int i = 0; k[i]; i++) {
							int j = 0;

							for (; k[i][j]; j++) {
								if (index + j >= bytes || string[index + j] != k[i][j]) {
									goto next;
								}
							}

							if (index + j == bytes || !CharIsAlphaOrDigitOrUnderscore(string[index + j])) {
								lexState = 6;
							}

							next:;
						}
					}
				}
			}
		} else if (language == ES_SYNTAX_HIGHLIGHTING_LANGUAGE_INI) {
			if (c == ';' && !index) {
				lexState = 1;
			} else if (c == '[' && !index) {
				lexState = 6;
			} else if ((c == ' ' || c == ']') && lexState == 5) {
				lexState = 6;
			} else if (c == '=' && !seenEquals) {
				seenEquals = true;
				lexState = 4;
			} else if (c == '@' && lexState == 6) {
				lexState = 5;
			} else if (seenEquals && lexState == 4 && EsCRTisdigit(c)) {
				lexState = 3;
			} else if (seenEquals && lexState == 4) {
				lexState = 2;
			}
		}

		if (!runs.Length() || runs.Last().style.color != colors[lexState + 1]) {
			EsTextRun run = {};
			run.offset = index;
			run.style = *baseStyle;
			run.style.color = colors[lexState + 1];
			runs.Add(run);
		}
	}

	EsTextRun run = {};
	run.offset = bytes;
	runs.Add(run);

	return runs;
}
