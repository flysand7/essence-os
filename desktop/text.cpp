#if defined(TEXT_RENDERER)

// TODO Fallback VGA font.
// TODO If the font size is sufficiently large disable subpixel anti-aliasing.
// TODO Variable font support.

#ifdef USE_HARFBUZZ
#include <harfbuzz/hb.h>
#include <harfbuzz/hb-ft.h>
#define HB_SHAPE(plan, features, featureCount) hb_shape(plan->font.hb, plan->buffer, features, featureCount)
#endif

#ifdef USE_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/ftoutln.h>
#endif

#define FREETYPE_UNIT_SCALE (64)

#define FALLBACK_SCRIPT_LANGUAGE ("en")
#define FALLBACK_SCRIPT (0x4C61746E) // "Latn"

struct Font {
#ifdef USE_FREETYPE
	FT_Face ft;
#else
	float scale;
	const BasicFontHeader *header;
#endif

#ifdef USE_HARFBUZZ
	hb_font_t *hb;
#endif
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

struct {
	// Database.
	HashStore<FontSubstitutionKey, EsFontFamily> substitutions;
	Array<FontDatabaseEntry> database;
	uintptr_t sans, serif, monospaced, fallback;
	char *sansName, *serifName, *monospacedName, *fallbackName;

	// Rendering.
#ifdef USE_FREETYPE
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

Font FontGet(EsFont key);

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

// --------------------------------- Font renderer.

bool FontLoad(Font *font, const void *data, size_t dataBytes) {
#ifdef USE_FREETYPE
	if (!fontManagement.freetypeLibrary) {
		FT_Init_FreeType(&fontManagement.freetypeLibrary);
	}

	if (FT_New_Memory_Face(fontManagement.freetypeLibrary, (uint8_t *) data, dataBytes, 0, &font->ft)) {
		return false;
	}
#else
	if (dataBytes < sizeof(BasicFontHeader)) {
		return false;
	}

	const BasicFontHeader *header = (const BasicFontHeader *) data;

	if (header->signature != BASIC_FONT_SIGNATURE
			|| (dataBytes < sizeof(BasicFontHeader) 
				+ header->glyphCount * sizeof(BasicFontGlyph) 
				+ header->kerningEntries * sizeof(BasicFontKerningEntry))) {
		return false;
	}

	const BasicFontGlyph *glyphs = (const BasicFontGlyph *) (header + 1);

	for (uintptr_t i = 0; i < header->glyphCount; i++) {
		if (dataBytes <= glyphs[i].offsetToPoints
				|| dataBytes < glyphs[i].offsetToPoints + glyphs[i].pointCount * 24) {
			return false;
		}
	}

	font->header = header;
#endif

#ifdef USE_HARFBUZZ
	font->hb = hb_ft_font_create(font->ft, nullptr);
#endif

	return true;
}

void FontSetSize(Font *font, uint32_t size) {
#ifdef USE_FREETYPE
	FT_Set_Char_Size(font->ft, 0, size * FREETYPE_UNIT_SCALE, 100, 100);
#else
	font->scale = 1.75f * (float) size / (font->header->ascender - font->header->descender);
#endif

#ifdef USE_HARFBUZZ
	hb_ft_font_changed(font->hb);
#endif
}

uint32_t FontCodepointToGlyphIndex(Font *font, uint32_t codepoint) {
#ifdef USE_FREETYPE
	return FT_Get_Char_Index(font->ft, codepoint);
#else
	const BasicFontGlyph *glyphs = (const BasicFontGlyph *) (font->header + 1);

	for (uintptr_t i = 0; i < font->header->glyphCount; i++) {
		if (glyphs[i].codepoint == codepoint) {
			return i;
		}
	}

	return 0;
#endif
}

void FontGetGlyphMetrics(Font *font, uint32_t glyphIndex, uint32_t *xAdvance, uint32_t *yAdvance, uint32_t *xOffset, uint32_t *yOffset) {
#ifdef USE_FREETYPE
	FT_Load_Glyph(font->ft, glyphIndex, 0);
	*xAdvance = font->ft->glyph->advance.x;
	*yAdvance = font->ft->glyph->advance.y;
	// *xOffset = font->ft->glyph->bitmap_left;
	// *yOffset = font->ft->glyph->bitmap_top;
	*xOffset = *yOffset = 0;
#else
	const BasicFontGlyph *glyph = ((const BasicFontGlyph *) (font->header + 1)) + glyphIndex;
	*xOffset = *yOffset = *yAdvance = 0;
	*xAdvance = glyph->xAdvance * font->scale * FREETYPE_UNIT_SCALE;
#endif
}

int32_t FontGetKerning(Font *font, uint32_t previous, uint32_t next) {
#ifdef USE_FREETYPE
	FT_Vector kerning = {};
	if (previous) FT_Get_Kerning(font->ft, previous, next, 0, &kerning);
	return kerning.x;
#else
	const BasicFontKerningEntry *entries = (const BasicFontKerningEntry *) (((const BasicFontGlyph *) (font->header + 1)) + font->header->glyphCount);

	uintptr_t currentIndex = 0;
	bool startFound = false;
	ES_MACRO_SEARCH(font->header->kerningEntries, result = previous - entries[index].leftGlyphIndex;, currentIndex, startFound);
	int32_t xAdvance = 0;

	if (startFound) {
		if (entries[currentIndex].rightGlyphIndex == next) {
			xAdvance = entries[currentIndex].xAdvance;
		} else if (entries[currentIndex].rightGlyphIndex < next) {
			while (currentIndex != font->header->kerningEntries && entries[currentIndex].leftGlyphIndex == previous) {
				if (entries[currentIndex].rightGlyphIndex == next) {
					xAdvance = entries[currentIndex].xAdvance;
					break;
				} else {
					currentIndex++;
				}
			}
		} else {
			while (entries[currentIndex].leftGlyphIndex == previous) {
				if (entries[currentIndex].rightGlyphIndex == next) {
					xAdvance = entries[currentIndex].xAdvance;
					break;
				} else if (!currentIndex) {
					break;
				} else {
					currentIndex--;
				}
			}
		}
	}

	return xAdvance * FREETYPE_UNIT_SCALE * font->scale;
#endif
}

int32_t FontGetAscent(Font *font) {
#ifdef USE_FREETYPE
	return font->ft->size->metrics.ascender;
#else
	return font->header->ascender * font->scale * FREETYPE_UNIT_SCALE;
#endif
}

int32_t FontGetDescent(Font *font) {
#ifdef USE_FREETYPE
	return font->ft->size->metrics.descender;
#else
	return font->header->descender * font->scale * FREETYPE_UNIT_SCALE;
#endif
}

int32_t FontGetEmWidth(Font *font) {
#ifdef USE_FREETYPE
	return font->ft->size->metrics.x_ppem;
#else
	return font->header->ascender * font->scale; // TODO.
#endif
}

int TextGetLineHeight(EsElement *element, const EsTextStyle *textStyle) {
	EsAssert(element);
	EsMessageMutexCheck();
	Font font = FontGet(textStyle->font);
	FontSetSize(&font, textStyle->size * theming.scale);
	return (FontGetAscent(&font) - FontGetDescent(&font) + FREETYPE_UNIT_SCALE / 2) / FREETYPE_UNIT_SCALE;
}

bool FontRenderGlyph(bool mono, GlyphCacheKey key, GlyphCacheEntry *entry) {
#ifdef USE_FREETYPE
	FT_Load_Glyph(key.font.ft, key.glyphIndex, FT_LOAD_DEFAULT);
	FT_Outline_Translate(&key.font.ft->glyph->outline, key.fractionalPosition, 0);

	int width; 
	int height; 
	int xoff; 
	int yoff; 
	uint8_t *output;

	if (mono) {
		FT_Render_Glyph(key.font.ft->glyph, FT_RENDER_MODE_MONO);

		FT_Bitmap *bitmap = &key.font.ft->glyph->bitmap;
		width = bitmap->width;
		height = bitmap->rows;
		xoff = key.font.ft->glyph->bitmap_left;
		yoff = -key.font.ft->glyph->bitmap_top;

		if ((uint64_t) width * (uint64_t) height * 4 > 100000000) {
			// Refuse to output glyphs more than 100MB.
			return false;
		}

		entry->dataBytes = 1 + (width * height + 7) / 8;
		output = (uint8_t *) EsHeapAllocate(entry->dataBytes, true);

		if (!output) {
			return false;
		}

		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				uintptr_t s = bitmap->pitch * 8 * y + x;
				uintptr_t d = width * y + x;

				if (bitmap->buffer[s / 8] & (1 << (7 - (s & 7)))) {
					output[d / 8] |= (1 << (d & 7));
				}
			}
		}
	} else {
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
#else
	EsAssert(!mono);

	const BasicFontGlyph *glyph = ((const BasicFontGlyph *) (key.font.header + 1)) + key.glyphIndex;
	uint32_t width = glyph->width * key.font.scale + 2;
       	uint32_t height = glyph->height * key.font.scale + 2;

	if (width > 4096 || height > 4096) {
		return false;
	}

	RastSurface surface = {};
	RastPath path = {};
	RastPaint paint = {};
	paint.type = RAST_PAINT_SOLID;
	paint.solid.alpha = 1.0f;

	float vertexScale = key.font.scale * (key.font.header->ascender - key.font.header->descender) * 0.01f;

	entry->data = (uint8_t *) EsHeapAllocate(width * height * 4, true);

	if (!entry->data) {
		return false;
	}

	if (glyph->pointCount) {
		float *points = (float *) ((const uint8_t *) key.font.header + glyph->offsetToPoints);

		for (uintptr_t i = 0, j = 0; i < glyph->pointCount * 3; i += 3) {
			if ((int) i == glyph->pointCount * 3 - 3 || points[i * 2 + 2] == -1e6) {
				RastPathAppendBezier(&path, (RastVertex *) points + j, i - j + 1, { vertexScale, vertexScale });
				RastPathCloseSegment(&path);
				j = i + 3;
			}
		}

		RastPathTranslate(&path, (float) key.fractionalPosition / FREETYPE_UNIT_SCALE - glyph->xOffset * key.font.scale, -glyph->yOffset * key.font.scale);
		RastShape shape = RastShapeCreateSolid(&path);

		if (RastSurfaceInitialise(&surface, width, height, false)) {
			RastSurfaceFill(surface, shape, paint, false);
			RastPathDestroy(&path);

			uint32_t *in = surface.buffer;
			uint8_t *out = entry->data;

			for (uintptr_t i = 0; i < height; i++) {
				for (uintptr_t j = 0; j < width; j++) {
					int32_t a = in[(height - i - 1) * width + j] >> 24;
					*out++ = (uint8_t) a;
					*out++ = (uint8_t) a;
					*out++ = (uint8_t) a;
					*out++ = 0xFF;
				}
			}
		}

		RastSurfaceDestroy(&surface);
	}

	entry->width = width;
	entry->height = height;
	entry->xoff = glyph->xOffset * key.font.scale + 0.25f;
	entry->yoff = -glyph->yOffset * key.font.scale - height + 0.25f;

	return true;
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

void EsFontDatabaseEnumerate(EsFontEnumerationCallback callback, EsGeneric context) {
	FontInitialise();

	for (uintptr_t i = 1; i < fontManagement.database.Length(); i++) {
		EsFontInformation information;
		EsFontDatabaseLookupByID(i, &information);
		callback(&information, context);
	}
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
			int distance = ((italic != key.italic) ? 10 : 0) + AbsoluteInteger(weight - key.weight);

			if (distance < matchDistance) {
				matchDistance = distance;
				file = entry->files[i];
			}
		}
	}

	if (!file) {
		EsPrint("Could not load font (f%d/w%d/i%d).\n", key.family, key.weight, key.italic);
		return {};
	}

	// EsPrint("Loading font from '%z' (f%d/w%d/i%d).\n", file, key.family, key.weight, key.italic);

	size_t size;
	void *data = EsFileStoreMap(file, &size, ES_MAP_OBJECT_READ_ONLY);

	if (!data) {
		EsPrint("Could not load font (f%d/w%d/i%d).\n", key.family, key.weight, key.italic);
		return {};
	}

	Font font = {};

	if (!FontLoad(&font, data, size)) {
		EsPrint("Could not load font (f%d/w%d/i%d).\n", key.family, key.weight, key.italic);
		return {};
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
#ifdef USE_HARFBUZZ
		hb_font_destroy(font.hb);
#endif
#ifdef USE_FREETYPE
		FT_Done_Face(font.ft);
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

#ifdef USE_FREETYPE
	FT_Done_FreeType(fontManagement.freetypeLibrary);
#endif
}

// --------------------------------- Blitting rendered glyphs.

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
			} else if (type == CHARACTER_MONO) {
				uintptr_t n = y * width + x;

				if (output[n / 8] & (1 << (n & 7))) {
					uint32_t *destination = (uint32_t *) ((uint8_t *) target->bits + oX * 4 + oY * target->stride);
					*destination = 0xFF000000 | color;
				} 
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

// --------------------------------- Basic shaping engine.

#ifndef USE_HARFBUZZ

#define HB_SCRIPT_COMMON (1)
#define HB_SCRIPT_INHERITED (1)

#define HB_SHAPE(plan, features, featureCount) hb_shape(plan->font, plan->buffer, features, featureCount)

struct hb_segment_properties_t {
	uint32_t script;
};

struct hb_glyph_info_t {
	uint32_t cluster;
	uint32_t codepoint;
};

struct hb_glyph_position_t {
	uint32_t x_advance;
	uint32_t y_advance;
	uint32_t x_offset;
	uint32_t y_offset;
};

struct hb_feature_t {
};

struct hb_buffer_t {
	const char *text;
	size_t textBytes;
	uintptr_t shapeOffset;
	size_t shapeBytes;

	Array<hb_glyph_info_t> glyphInfos;
	Array<hb_glyph_position_t> glyphPositions;
};

void hb_buffer_clear_contents(hb_buffer_t *buffer) {
	buffer->glyphInfos.Free();
	buffer->glyphPositions.Free();
}

void hb_buffer_set_segment_properties(hb_buffer_t *, hb_segment_properties_t *) {
}

void hb_buffer_add_utf8(hb_buffer_t *buffer, const char *text, size_t textBytes, uintptr_t shapeOffset, size_t shapeBytes) {
	buffer->text = text;
	buffer->textBytes = textBytes;
	buffer->shapeOffset = shapeOffset;
	buffer->shapeBytes = shapeBytes;
}

hb_glyph_info_t *hb_buffer_get_glyph_infos(hb_buffer_t *buffer, uint32_t *glyphCount) {
	*glyphCount = buffer->glyphInfos.Length();
	return buffer->glyphInfos.array;
}

hb_glyph_position_t *hb_buffer_get_glyph_positions(hb_buffer_t *buffer, uint32_t *glyphCount) {
	*glyphCount = buffer->glyphPositions.Length();
	return buffer->glyphPositions.array;
}

uint32_t hb_unicode_script(struct hb_unicode_funcs_t *, uint32_t) { 
	return FALLBACK_SCRIPT; 
}

struct hb_unicode_funcs_t *hb_unicode_funcs_get_default() { 
	return nullptr; 
}

hb_buffer_t *hb_buffer_create() {
	return (hb_buffer_t *) EsHeapAllocate(sizeof(hb_buffer_t), true);
}

void hb_buffer_destroy(hb_buffer_t *buffer) {
	hb_buffer_clear_contents(buffer);
	EsHeapFree(buffer);
}

void hb_shape(Font font, hb_buffer_t *buffer, const hb_feature_t *, uint32_t) {
	// TODO Cache glyph metrics.

	const char *text = buffer->text + buffer->shapeOffset;
	uint32_t previous = 0;

	while (true) {
		hb_glyph_info_t info = {};
		hb_glyph_position_t position = {};
		info.cluster = text - buffer->text;
		uint32_t codepoint = utf8_value(text, buffer->text + buffer->shapeOffset + buffer->shapeBytes - text, nullptr);
		if (!codepoint) break;
		text = utf8_advance(text);
		info.codepoint = FontCodepointToGlyphIndex(&font, codepoint);
		FontGetGlyphMetrics(&font, info.codepoint, &position.x_advance, &position.y_advance, &position.x_offset, &position.y_offset);
		position.x_advance += FontGetKerning(&font, previous, info.codepoint);
		previous = info.codepoint;
		buffer->glyphInfos.Add(info);
		buffer->glyphPositions.Add(position);
	}
}

#endif

// --------------------------------- Text shaping.

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

struct EsTextPlan {
	hb_buffer_t *buffer;
	hb_segment_properties_t segmentProperties;

	const char *string; 

	Array<TextRun> textRuns; 
	uintptr_t textRunPosition;

	const EsTextStyle *currentTextStyle;
	Font font;

	BreakState breaker;

	Array<hb_glyph_info_t> glyphInfos;
	Array<hb_glyph_position_t> glyphPositions;

	Array<TextPiece> pieces;
	Array<TextLine> lines;

	int32_t totalHeight, totalWidth;

	bool singleUse;

	EsTextPlanProperties properties;
};

TextStyleDifference CompareTextStyles(const EsTextStyle *style1, const EsTextStyle *style2) {
	if (!style1) return TEXT_STYLE_NEW_FONT;
	if (style1->font.family 	!= style2->font.family) 	return TEXT_STYLE_NEW_FONT;
	if (style1->font.weight 	!= style2->font.weight) 	return TEXT_STYLE_NEW_FONT;
	if (style1->font.italic 	!= style2->font.italic) 	return TEXT_STYLE_NEW_FONT;
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
		hb_glyph_info_t *glyphs = &plan->glyphInfos[piece->glyphOffset];
		hb_glyph_position_t *glyphPositions = &plan->glyphPositions[piece->glyphOffset];

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

	hb_buffer_clear_contents(plan->buffer);
	hb_buffer_set_segment_properties(plan->buffer, &plan->segmentProperties);
	hb_buffer_add_utf8(plan->buffer, (const char *) ellipsisUTF8, sizeof(ellipsisUTF8), 0, sizeof(ellipsisUTF8));
	HB_SHAPE(plan, nullptr, 0);

	int32_t ellipsisWidth = 0;

	uint32_t glyphCount, glyphCount2;
	hb_glyph_info_t *glyphInfos = hb_buffer_get_glyph_infos(plan->buffer, &glyphCount);
	hb_glyph_position_t *glyphPositions = hb_buffer_get_glyph_positions(plan->buffer, &glyphCount2);
	EsAssert(glyphCount == glyphCount2);

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
}

void TextItemizeByScript(EsTextPlan *plan, const EsTextRun *runs, size_t runCount, float sizeScaleFactor) {
	hb_unicode_funcs_t *unicodeFunctions = hb_unicode_funcs_get_default();
	uint32_t lastAssignedScript = FALLBACK_SCRIPT;

	for (uintptr_t i = 0; i < runCount; i++) {
		uintptr_t offset = runs[i].offset;

		for (uintptr_t j = offset; j < runs[i + 1].offset;) {
			uint32_t codepoint = utf8_value(plan->string + j);
			uint32_t script;

			if (codepoint == '\t') {
				// Tab characters should go in their own section.
				script = '\t';
			} else {
				script = hb_unicode_script(unicodeFunctions, codepoint);
			}

			if (script == HB_SCRIPT_COMMON || script == HB_SCRIPT_INHERITED) {
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
				hb_glyph_info_t info = {};
				info.cluster = piece->start + i;
				info.codepoint = 0xFFFFFFFF;
				hb_glyph_position_t position = {};
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

		hb_feature_t features[4] = {};
		size_t featureCount = 0;

#ifdef USE_HARFBUZZ
		if (plan->currentTextStyle->figures == ES_TEXT_FIGURE_OLD) hb_feature_from_string("onum", -1, features + (featureCount++));
		if (plan->currentTextStyle->figures == ES_TEXT_FIGURE_TABULAR) hb_feature_from_string("tnum", -1, features + (featureCount++));
		plan->segmentProperties.script = (hb_script_t) run->script;
#endif

		hb_buffer_clear_contents(plan->buffer);
		hb_buffer_set_segment_properties(plan->buffer, &plan->segmentProperties);
		hb_buffer_add_utf8(plan->buffer, plan->string, plan->breaker.bytes, start, end - start);

		HB_SHAPE(plan, features, featureCount);

		uint32_t glyphCount, glyphCount2;
		hb_glyph_info_t *glyphInfos = hb_buffer_get_glyph_infos(plan->buffer, &glyphCount);
		hb_glyph_position_t *glyphPositions = hb_buffer_get_glyph_positions(plan->buffer, &glyphCount2);
		EsAssert(glyphCount == glyphCount2);

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

	plan.buffer = hb_buffer_create();
#ifdef USE_HARFBUZZ
	hb_buffer_set_cluster_level(plan.buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);

	plan.segmentProperties.direction = (properties->flags & ES_TEXT_PLAN_RTL) ? HB_DIRECTION_RTL : HB_DIRECTION_LTR;
	plan.segmentProperties.script = (hb_script_t) FALLBACK_SCRIPT;
	plan.segmentProperties.language = hb_language_from_string(properties->cLanguage ?: FALLBACK_SCRIPT_LANGUAGE, -1);
#endif

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

	hb_buffer_destroy(plan.buffer);
	plan.buffer = nullptr;
	
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

void EsTextPlanReplaceStyleRenderProperties(EsTextPlan *plan, EsTextStyle *style) {
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
		EsTextSelection *selection, uintptr_t caret, int32_t selectionBackgroundBottom) {
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

	hb_glyph_info_t *glyphs = &plan->glyphInfos[piece->glyphOffset];
	hb_glyph_position_t *glyphPositions = &plan->glyphPositions[piece->glyphOffset];

	// Update the font to match the piece.

	TextUpdateFont(plan, piece->style);

	// Draw the selection background.

	if (selection->caret0 != selection->caret1 && !selection->hideCaret) {
		int sCursorX = cursorX, selectionStartX = -1, selectionEndX = -1;

		for (uintptr_t i = 0; i < piece->glyphCount; i++) {
			if (selectionStartX == -1 && glyphs[i].cluster >= selection->caret0) {
				selectionStartX = sCursorX;
			}

			if (selectionEndX == -1 && glyphs[i].cluster >= selection->caret1) {
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

		// bool mono = api.systemConstants[ES_SYSTEM_CONSTANT_NO_FANCY_GRAPHICS];
		bool mono = false;

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
			if (!FontRenderGlyph(mono, key, entry)) {
				EsHeapFree(entry);
				goto nextCharacter;
			} else {
				RegisterGlyphCacheEntry(key, entry);
			}
		}

		if (selection->caret0 != selection->caret1 && !selection->hideCaret 
				&& glyphs[i].cluster >= selection->caret0 && glyphs[i].cluster < selection->caret1
				&& selection->foreground) {
			color = selection->foreground;
		}

		// EsPrint("\t%c at %i.%i\n", plan->string[glyphs[i].cluster], positionX, (glyphPositions[i].x_offset + cursorX) & 0x3F);

		DrawSingleCharacter(entry->width, entry->height, entry->xoff, entry->yoff, 
				ES_POINT(positionX, positionY + line->ascent / FREETYPE_UNIT_SCALE), 
				painter->clip, painter->target, 
				plan->currentTextStyle->blur, 
				mono ? CHARACTER_MONO : CHARACTER_SUBPIXEL, 
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

void EsDrawText(EsPainter *painter, EsTextPlan *plan, EsRectangle bounds, EsRectangle *_clip, EsTextSelection *_selection) {
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
	EsDrawText(painter, EsTextPlanCreate(element, &properties, bounds, string, textRuns, 1), bounds); 
}

#elif defined(TEXT_ELEMENTS)

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
					textRun->style.font.italic = true;
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

// --------------------------------- Textboxes.

// TODO Caret blinking.
// TODO Wrapped lines.
// TODO Unicode grapheme/word boundaries.
// TODO Selecting lines with the margin.

struct DocumentLine {
	char *GetBuffer(EsTextbox *textbox);

	int32_t lengthBytes,
		lengthWidth, 
		height,
		yPosition,
		offset;
};

struct TextboxVisibleLine {
	int32_t yPosition;
};

struct TextboxCaret {
	int32_t byte, // Relative to the start of the line.
		line;
};

struct EsTextbox : EsElement {
	ScrollPane scroll;

	char *data; // Call TextboxSetActiveLine(textbox, -1) to access this.
	uintptr_t dataAllocated;
	int32_t dataBytes;

	bool editing;
	char *editStartContent;
	int32_t editStartContentBytes;

	EsUICallback overlayCallback;
	EsGeneric overlayData;

	char *activeLine;
	uintptr_t activeLineAllocated;
	int32_t activeLineIndex, activeLineStart, activeLineOldBytes, activeLineBytes;

	int32_t longestLine, longestLineWidth; // To set the horizontal scrollbar's size.

	TextboxCaret carets[2]; // carets[1] is the actual caret; carets[0] is the selection anchor.
	TextboxCaret wordSelectionAnchor, wordSelectionAnchor2;

	Array<DocumentLine> lines;
	Array<TextboxVisibleLine> visibleLines;
	int32_t firstVisibleLine;

	int verticalMotionHorizontalDepth;
	int oldHorizontalScroll;

	EsUndoManager *undo;
	EsUndoManager localUndo;

	EsElement *margin;
	
	EsRectangle borders, insets;
	EsTextStyle textStyle;
	EsFont overrideFont;
	uint16_t overrideTextSize;

	uint32_t syntaxHighlightingLanguage;
	uint32_t syntaxHighlightingColors[8];

	bool smartQuotes;

	bool inRightClickDrag;

	// For smart context menus:
	bool colorUppercase;
};

#define MOVE_CARET_SINGLE (2)
#define MOVE_CARET_WORD (3)
#define MOVE_CARET_LINE (4)
#define MOVE_CARET_VERTICAL (5)
#define MOVE_CARET_ALL (6)

#define MOVE_CARET_BACKWARDS (false)
#define MOVE_CARET_FORWARDS (true)

void TextboxBufferResize(void **array, uintptr_t *allocated, uintptr_t needed, uintptr_t itemSize) {
	if (*allocated >= needed) {
		return;
	}

	uintptr_t oldAllocated = *allocated;
	void *oldArray = *array;

	uintptr_t newAllocated = oldAllocated * 2;
	if (newAllocated < needed) newAllocated = needed + 16;
	void *newArray = EsHeapAllocate(newAllocated * itemSize, false);

	EsMemoryCopy(newArray, oldArray, oldAllocated * itemSize);
	EsHeapFree(oldArray);

	*allocated = newAllocated;
	*array = newArray;
}

bool IsScancodeNonTypeable(unsigned scancode) {
	switch (scancode) {
		case ES_SCANCODE_CAPS_LOCK:
		case ES_SCANCODE_SCROLL_LOCK:
		case ES_SCANCODE_NUM_LOCK:
		case ES_SCANCODE_LEFT_SHIFT:
		case ES_SCANCODE_LEFT_CTRL:
		case ES_SCANCODE_LEFT_ALT:
		case ES_SCANCODE_LEFT_FLAG:
		case ES_SCANCODE_RIGHT_SHIFT:
		case ES_SCANCODE_RIGHT_CTRL:
		case ES_SCANCODE_RIGHT_ALT:
		case ES_SCANCODE_PAUSE:
		case ES_SCANCODE_CONTEXT_MENU:
		case ES_SCANCODE_PRINT_SCREEN:
		case ES_SCANCODE_F1:
		case ES_SCANCODE_F2:
		case ES_SCANCODE_F3:
		case ES_SCANCODE_F4:
		case ES_SCANCODE_F5:
		case ES_SCANCODE_F6:
		case ES_SCANCODE_F7:
		case ES_SCANCODE_F8:
		case ES_SCANCODE_F9:
		case ES_SCANCODE_F10:
		case ES_SCANCODE_F11:
		case ES_SCANCODE_F12:
		case ES_SCANCODE_ACPI_POWER:
		case ES_SCANCODE_ACPI_SLEEP:
		case ES_SCANCODE_ACPI_WAKE:
		case ES_SCANCODE_MM_NEXT:
		case ES_SCANCODE_MM_PREVIOUS:
		case ES_SCANCODE_MM_STOP:
		case ES_SCANCODE_MM_PAUSE:
		case ES_SCANCODE_MM_MUTE:
		case ES_SCANCODE_MM_QUIETER:
		case ES_SCANCODE_MM_LOUDER:
		case ES_SCANCODE_MM_SELECT:
		case ES_SCANCODE_MM_EMAIL:
		case ES_SCANCODE_MM_CALC:
		case ES_SCANCODE_MM_FILES:
		case ES_SCANCODE_WWW_SEARCH:
		case ES_SCANCODE_WWW_HOME:
		case ES_SCANCODE_WWW_BACK:
		case ES_SCANCODE_WWW_FORWARD:
		case ES_SCANCODE_WWW_STOP:
		case ES_SCANCODE_WWW_REFRESH:
		case ES_SCANCODE_WWW_STARRED:
			return true;

		default:
			return false;
	}
}

void ConvertScancodeToCharacter(unsigned scancode, int *_ic, int *_isc, bool enableTabs, bool enableNewline) {
	int ic = -1, isc = -1;

	switch (scancode) {
		case ES_SCANCODE_A: ic = 'a'; isc = 'A'; break;
		case ES_SCANCODE_B: ic = 'b'; isc = 'B'; break;
		case ES_SCANCODE_C: ic = 'c'; isc = 'C'; break;
		case ES_SCANCODE_D: ic = 'd'; isc = 'D'; break;
		case ES_SCANCODE_E: ic = 'e'; isc = 'E'; break;
		case ES_SCANCODE_F: ic = 'f'; isc = 'F'; break;
		case ES_SCANCODE_G: ic = 'g'; isc = 'G'; break;
		case ES_SCANCODE_H: ic = 'h'; isc = 'H'; break;
		case ES_SCANCODE_I: ic = 'i'; isc = 'I'; break;
		case ES_SCANCODE_J: ic = 'j'; isc = 'J'; break;
		case ES_SCANCODE_K: ic = 'k'; isc = 'K'; break;
		case ES_SCANCODE_L: ic = 'l'; isc = 'L'; break;
		case ES_SCANCODE_M: ic = 'm'; isc = 'M'; break;
		case ES_SCANCODE_N: ic = 'n'; isc = 'N'; break;
		case ES_SCANCODE_O: ic = 'o'; isc = 'O'; break;
		case ES_SCANCODE_P: ic = 'p'; isc = 'P'; break;
		case ES_SCANCODE_Q: ic = 'q'; isc = 'Q'; break;
		case ES_SCANCODE_R: ic = 'r'; isc = 'R'; break;
		case ES_SCANCODE_S: ic = 's'; isc = 'S'; break;
		case ES_SCANCODE_T: ic = 't'; isc = 'T'; break;
		case ES_SCANCODE_U: ic = 'u'; isc = 'U'; break;
		case ES_SCANCODE_V: ic = 'v'; isc = 'V'; break;
		case ES_SCANCODE_W: ic = 'w'; isc = 'W'; break;
		case ES_SCANCODE_X: ic = 'x'; isc = 'X'; break;
		case ES_SCANCODE_Y: ic = 'y'; isc = 'Y'; break;
		case ES_SCANCODE_Z: ic = 'z'; isc = 'Z'; break;
		case ES_SCANCODE_0: ic = '0'; isc = ')'; break;
		case ES_SCANCODE_1: ic = '1'; isc = '!'; break;
		case ES_SCANCODE_2: ic = '2'; isc = '@'; break;
		case ES_SCANCODE_3: ic = '3'; isc = '#'; break;
		case ES_SCANCODE_4: ic = '4'; isc = '$'; break;
		case ES_SCANCODE_5: ic = '5'; isc = '%'; break;
		case ES_SCANCODE_6: ic = '6'; isc = '^'; break;
		case ES_SCANCODE_7: ic = '7'; isc = '&'; break;
		case ES_SCANCODE_8: ic = '8'; isc = '*'; break;
		case ES_SCANCODE_9: ic = '9'; isc = '('; break;
		case ES_SCANCODE_SLASH: 	ic = '/';  isc = '?'; break;
		case ES_SCANCODE_PUNCTUATION_1: ic = '\\'; isc = '|'; break;
		case ES_SCANCODE_LEFT_BRACE: 	ic = '[';  isc = '{'; break;
		case ES_SCANCODE_RIGHT_BRACE: 	ic = ']';  isc = '}'; break;
		case ES_SCANCODE_EQUALS: 	ic = '=';  isc = '+'; break;
		case ES_SCANCODE_PUNCTUATION_5: ic = '`';  isc = '~'; break;
		case ES_SCANCODE_HYPHEN: 	ic = '-';  isc = '_'; break;
		case ES_SCANCODE_PUNCTUATION_3: ic = ';';  isc = ':'; break;
		case ES_SCANCODE_PUNCTUATION_4: ic = '\''; isc = '"'; break;
		case ES_SCANCODE_COMMA: 	ic = ',';  isc = '<'; break;
		case ES_SCANCODE_PERIOD: 	ic = '.';  isc = '>'; break;
		case ES_SCANCODE_SPACE: 	ic = ' ';  isc = ' '; break;
		case ES_SCANCODE_ENTER:		if (enableNewline) { ic = '\n'; isc = '\n'; } break;
		case ES_SCANCODE_TAB:		if (enableTabs) { ic = '\t'; isc = '\t'; } break;
	}

	*_ic = ic, *_isc = isc;
}

size_t EsMessageGetInputText(EsMessage *message, char *buffer) {
	int ic, isc;
	ConvertScancodeToCharacter(message->keyboard.scancode, &ic, &isc, true, true);

	if (message->keyboard.modifiers & ES_MODIFIER_SHIFT) ic = isc;
	if (ic == -1) return 0;

	return utf8_encode(ic, buffer);
}

enum CharacterType {
	CHARACTER_INVALID,
	CHARACTER_IDENTIFIER, // A-Z, a-z, 0-9, _, >= 0x7F
	CHARACTER_WHITESPACE, // space, tab, newline
	CHARACTER_OTHER,
};

static CharacterType GetCharacterType(int character) {
	if ((character >= '0' && character <= '9') 
			|| (character >= 'a' && character <= 'z')
			|| (character >= 'A' && character <= 'Z')
			|| (character == '_')
			|| (character >= 0x80)) {
		return CHARACTER_IDENTIFIER;
	}

	if (character == '\n' || character == '\t' || character == ' ') {
		return CHARACTER_WHITESPACE;
	}

	return CHARACTER_OTHER;
}

int TextboxCompareCarets(const TextboxCaret *left, const TextboxCaret *right) {
	if (left->line < right->line) return -1;
	if (left->line > right->line) return  1;
	if (left->byte < right->byte) return -1;
	if (left->byte > right->byte) return  1;
	return 0;
}

void TextboxSetActiveLine(EsTextbox *textbox, int lineIndex) {
	if (textbox->activeLineIndex == lineIndex) {
		return;
	}

	if (lineIndex == -1) {
		int32_t lineBytesDelta = textbox->activeLineBytes - textbox->activeLineOldBytes;

		// Step 1: Resize the data buffer to fit the new contents of the line.

		TextboxBufferResize((void **) &textbox->data, &textbox->dataAllocated, textbox->dataBytes + lineBytesDelta, 1);

		// Step 2: Move everything after the old end of the active line to its new position.

		EsMemoryMove(textbox->data + textbox->activeLineStart + textbox->activeLineOldBytes,
				textbox->data + textbox->dataBytes,
				lineBytesDelta,
				false);
		textbox->dataBytes += lineBytesDelta;

		// Step 3: Copy the active line back into the data buffer.

		EsMemoryCopy(textbox->data + textbox->activeLineStart,
				textbox->activeLine,
				textbox->activeLineBytes);

		// Step 4: Update the line byte offsets.

		for (uintptr_t i = textbox->activeLineIndex + 1; i < textbox->lines.Length(); i++) {
			textbox->lines[i].offset += lineBytesDelta;
		}
	} else {
		TextboxSetActiveLine(textbox, -1);

		DocumentLine *line = &textbox->lines[lineIndex];

		TextboxBufferResize((void **) &textbox->activeLine, &textbox->activeLineAllocated, (textbox->activeLineBytes = line->lengthBytes), 1);
		EsMemoryCopy(textbox->activeLine, textbox->data + line->offset, textbox->activeLineBytes);

		textbox->activeLineStart = line->offset;
		textbox->activeLineOldBytes = textbox->activeLineBytes;
	}

	textbox->activeLineIndex = lineIndex;
}

void EsTextboxStartEdit(EsTextbox *textbox) {
	textbox->state &= ~UI_STATE_LOST_STRONG_FOCUS;

	if ((textbox->flags & ES_TEXTBOX_EDIT_BASED) && !textbox->editing) {
		EsMessage m = { ES_MSG_TEXTBOX_EDIT_START };

		if (0 == EsMessageSend(textbox, &m)) {
			EsTextboxSelectAll(textbox);
		}

		if (textbox->state & UI_STATE_DESTROYING) {
			return;
		}

		textbox->editing = true; // Update this after sending the message so overlays can receive it.
		TextboxSetActiveLine(textbox, -1);
		textbox->editStartContent = (char *) EsHeapAllocate(textbox->dataBytes, false);
		textbox->editStartContentBytes = textbox->dataBytes;
		EsMemoryCopy(textbox->editStartContent, textbox->data, textbox->editStartContentBytes);
		textbox->Repaint(true);
	}
}

void TextboxEndEdit(EsTextbox *textbox, bool reject) {
	if ((textbox->flags & ES_TEXTBOX_EDIT_BASED) && textbox->editing) {
		TextboxSetActiveLine(textbox, -1);
		textbox->editing = false;
		EsMessage m = { ES_MSG_TEXTBOX_EDIT_END };
		m.endEdit.rejected = reject;
		m.endEdit.unchanged = textbox->dataBytes == textbox->editStartContentBytes 
			&& 0 == EsMemoryCompare(textbox->data, textbox->editStartContent, textbox->dataBytes);

		if (reject || ES_REJECTED == EsMessageSend(textbox, &m)) {
			EsTextboxSelectAll(textbox);
			EsTextboxInsert(textbox, textbox->editStartContent, textbox->editStartContentBytes);
			TextboxSetActiveLine(textbox, -1);
			if (reject) EsMessageSend(textbox, &m);
		}

		if (textbox->state & UI_STATE_DESTROYING) {
			return;
		}

		EsTextboxSetSelection(textbox, 0, 0, 0, 0);
		EsHeapFree(textbox->editStartContent);
		textbox->editStartContent = nullptr;
		textbox->scroll.SetX(0);
		textbox->Repaint(true);
	}
}

void TextboxUpdateCommands(EsTextbox *textbox, bool noClipboard) {
	if (~textbox->state & UI_STATE_FOCUSED) {
		return;
	}

	EsCommand *command;

	bool selectionEmpty = !TextboxCompareCarets(textbox->carets + 0, textbox->carets + 1) && textbox->editing;

	command = EsCommandByID(textbox->instance, ES_COMMAND_DELETE);
	command->data = textbox;
	EsCommandSetDisabled(command, selectionEmpty);

	EsCommandSetCallback(command, [] (EsInstance *, EsElement *, EsCommand *command) {
		EsTextbox *textbox = (EsTextbox *) command->data.p;
		EsTextboxInsert(textbox, "", 0, true);
	});

	command = EsCommandByID(textbox->instance, ES_COMMAND_COPY);
	command->data = textbox;
	EsCommandSetDisabled(command, selectionEmpty);

	EsCommandSetCallback(command, [] (EsInstance *, EsElement *, EsCommand *command) {
		EsTextbox *textbox = (EsTextbox *) command->data.p;
		size_t textBytes;
		char *text = EsTextboxGetContents(textbox, &textBytes, textbox->editing ? ES_TEXTBOX_GET_CONTENTS_SELECTED_ONLY : ES_FLAGS_DEFAULT);
		EsError error = EsClipboardAddText(ES_CLIPBOARD_PRIMARY, text, textBytes);
		EsHeapFree(text);

		EsRectangle bounds = EsElementGetWindowBounds(textbox);
		int32_t x = (bounds.l + bounds.r) / 2;
		int32_t y = (bounds.t + bounds.b) / 2; // TODO Position this in the middle of the selection.

		if (error == ES_SUCCESS) {
			EsAnnouncementShow(textbox->window, ES_FLAGS_DEFAULT, x, y, INTERFACE_STRING(CommonAnnouncementTextCopied));
		} else if (error == ES_ERROR_INSUFFICIENT_RESOURCES || error == ES_ERROR_DRIVE_FULL) {
			EsAnnouncementShow(textbox->window, ES_FLAGS_DEFAULT, x, y, INTERFACE_STRING(CommonAnnouncementCopyErrorResources));
		} else {
			EsAnnouncementShow(textbox->window, ES_FLAGS_DEFAULT, x, y, INTERFACE_STRING(CommonAnnouncementCopyErrorOther));
		}
	});

	command = EsCommandByID(textbox->instance, ES_COMMAND_CUT);
	command->data = textbox;
	EsCommandSetDisabled(command, selectionEmpty);

	EsCommandSetCallback(command, [] (EsInstance *, EsElement *, EsCommand *command) {
		EsTextbox *textbox = (EsTextbox *) command->data.p;
		size_t textBytes;
		char *text = EsTextboxGetContents(textbox, &textBytes, textbox->editing ? ES_TEXTBOX_GET_CONTENTS_SELECTED_ONLY : ES_FLAGS_DEFAULT);
		EsClipboardAddText(ES_CLIPBOARD_PRIMARY, text, textBytes);
		EsHeapFree(text);
		EsTextboxStartEdit(textbox);
		EsTextboxInsert(textbox, "", 0, true);
	});

	EsInstanceSetActiveUndoManager(textbox->instance, textbox->undo);

	command = EsCommandByID(textbox->instance, ES_COMMAND_SELECT_ALL);
	command->data = textbox;
	EsCommandSetDisabled(command, !(textbox->lines.Length() > 1 || textbox->lines[0].lengthBytes));

	EsCommandSetCallback(command, [] (EsInstance *, EsElement *, EsCommand *command) {
		EsTextboxSelectAll((EsTextbox *) command->data.p);
	});

	if (!noClipboard) {
		command = EsCommandByID(textbox->instance, ES_COMMAND_PASTE);
		command->data = textbox;
		EsCommandSetDisabled(command, !EsClipboardHasText(ES_CLIPBOARD_PRIMARY));

		EsCommandSetCallback(command, [] (EsInstance *, EsElement *, EsCommand *command) {
			EsTextbox *textbox = (EsTextbox *) command->data.p;

			size_t textBytes = 0;
			char *text = EsClipboardReadText(ES_CLIPBOARD_PRIMARY, &textBytes);
			EsTextboxInsert(textbox, text, textBytes, true);
			EsTextboxEnsureCaretVisible(textbox);
			EsHeapFree(text);
		});
	}
}

char *DocumentLine::GetBuffer(EsTextbox *textbox) {
	if (textbox->activeLineIndex == this - textbox->lines.array) {
		return textbox->activeLine;
	} else {
		return textbox->data + offset;
	}
}

void TextboxFindLongestLine(EsTextbox *textbox) {
	if (textbox->longestLine == -1) {
		textbox->longestLine = 0;
		textbox->longestLineWidth = textbox->lines[0].lengthWidth;

		for (uintptr_t i = 1; i < textbox->lines.Length(); i++) {
			int32_t width = textbox->lines[i].lengthWidth;

			if (width > textbox->longestLineWidth) {
				textbox->longestLine = i, textbox->longestLineWidth = width;
			}
		}
	}
}

TextboxVisibleLine *TextboxGetVisibleLine(EsTextbox *textbox, int32_t documentLineIndex) {
	return textbox->firstVisibleLine > documentLineIndex 
		|| textbox->firstVisibleLine + (int32_t) textbox->visibleLines.Length() <= documentLineIndex 
		? nullptr : &textbox->visibleLines[documentLineIndex - textbox->firstVisibleLine];
}

void EsTextboxEnsureCaretVisible(EsTextbox *textbox, bool verticallyCenter) {
	TextboxCaret caret = textbox->carets[1];
	EsRectangle bounds = textbox->GetBounds();

	{
		DocumentLine *line = &textbox->lines[caret.line];
		int caretY = line->yPosition + textbox->insets.t;

		int scrollY = textbox->scroll.position[1];
		int viewportHeight = bounds.b;
		caretY -= scrollY;

		if (viewportHeight > 0) {
			if (verticallyCenter) {
				scrollY += caretY - viewportHeight / 2;
			} else {
				if (caretY < textbox->insets.t) {
					scrollY += caretY - textbox->insets.t;
				} else if (caretY + line->height > viewportHeight - textbox->insets.b) {
					scrollY += caretY + line->height - viewportHeight + textbox->insets.b;
				}
			}

			textbox->scroll.SetY(scrollY);
		}
	}

	TextboxVisibleLine *visibleLine = TextboxGetVisibleLine(textbox, caret.line);

	if (visibleLine) {
		DocumentLine *line = &textbox->lines[caret.line];
		int scrollX = textbox->scroll.position[0];
		int viewportWidth = bounds.r;
		int caretX = TextGetPartialStringWidth(textbox, &textbox->textStyle,
				line->GetBuffer(textbox), line->lengthBytes, caret.byte) - scrollX + textbox->insets.l;

		if (caretX < textbox->insets.l) {
			scrollX += caretX - textbox->insets.l;
		} else if (caretX + 1 > viewportWidth - textbox->insets.r) {
			scrollX += caretX + 1 - viewportWidth + textbox->insets.r;
		}

		textbox->scroll.SetX(scrollX);
	}
}

bool TextboxMoveCaret(EsTextbox *textbox, TextboxCaret *caret, bool right, int moveType, bool strongWhitespace = false) {
	TextboxCaret old = *caret;
	EsDefer(TextboxUpdateCommands(textbox, true));

	if (moveType == MOVE_CARET_LINE) {
		caret->byte = right ? textbox->lines[caret->line].lengthBytes : 0;
	} else if (moveType == MOVE_CARET_ALL) {
		caret->line = right ? textbox->lines.Length() - 1 : 0;
		caret->byte = right ? textbox->lines[caret->line].lengthBytes : 0;
	} else if (moveType == MOVE_CARET_VERTICAL) {
		if ((right && caret->line + 1 == (int32_t) textbox->lines.Length()) || (!right && !caret->line)) {
			return false;
		}

		if (textbox->verticalMotionHorizontalDepth == -1) {
			textbox->verticalMotionHorizontalDepth = TextGetPartialStringWidth(textbox, &textbox->textStyle,
					textbox->lines[caret->line].GetBuffer(textbox), textbox->lines[caret->line].lengthBytes, caret->byte);
		}

		if (right) caret->line++; else caret->line--;
		caret->byte = 0;

		DocumentLine *line = &textbox->lines[caret->line];
		int pointX = textbox->verticalMotionHorizontalDepth ? textbox->verticalMotionHorizontalDepth - 1 : 0;
		ptrdiff_t result = TextGetCharacterAtPoint(textbox, &textbox->textStyle,
				line->GetBuffer(textbox), line->lengthBytes, &pointX, ES_TEXT_GET_CHARACTER_AT_POINT_MIDDLE);
		caret->byte = result == -1 ? line->lengthBytes : result;
	} else {
		CharacterType type = CHARACTER_INVALID;
		char *currentLineBuffer = textbox->lines[caret->line].GetBuffer(textbox);
		if (moveType == MOVE_CARET_WORD && right) goto checkCharacterType;

		while (true) {
			if (!right) {
				if (caret->byte || caret->line) {
					if (caret->byte) {
						caret->byte = utf8_retreat(currentLineBuffer + caret->byte) - currentLineBuffer;
					} else {
						caret->byte = textbox->lines[--caret->line].lengthBytes;
						currentLineBuffer = textbox->lines[caret->line].GetBuffer(textbox);
					}
				} else {
					break; // We cannot move any further left.
				}
			} else {
				if (caret->line < (int32_t) textbox->lines.Length() - 1 || caret->byte < textbox->lines[caret->line].lengthBytes) {
					if (caret->byte < textbox->lines[caret->line].lengthBytes) {
						caret->byte = utf8_advance(currentLineBuffer + caret->byte) - currentLineBuffer;
					} else {
						caret->line++;
						caret->byte = 0;
						currentLineBuffer = textbox->lines[caret->line].GetBuffer(textbox);
					}
				} else {
					break; // We cannot move any further right.
				}
			}

			if (moveType == MOVE_CARET_SINGLE) {
				break;
			}

			checkCharacterType:;

			int character;

			if (caret->byte == textbox->lines[caret->line].lengthBytes) {
				character = '\n';
			} else {
				character = utf8_value(currentLineBuffer + caret->byte);
			}

			CharacterType newType = GetCharacterType(character);

			if (type == CHARACTER_INVALID) {
				if (newType != CHARACTER_WHITESPACE || strongWhitespace) {
					type = newType;
				}
			} else {
				if (newType != type) {
					if (!right) {
						// We've gone too far.
						TextboxMoveCaret(textbox, caret, true, MOVE_CARET_SINGLE);
					}

					break;
				}
			}
		}
	}

	return caret->line != old.line;
}

void EsTextboxMoveCaretRelative(EsTextbox *textbox, uint32_t flags) {
	if (~flags & ES_TEXTBOX_MOVE_CARET_SECOND_ONLY) {
		TextboxMoveCaret(textbox, &textbox->carets[0], ~flags & ES_TEXTBOX_MOVE_CARET_BACKWARDS, 
				flags & 0xFF, flags & ES_TEXTBOX_MOVE_CARET_STRONG_WHITESPACE);
	}

	if (~flags & ES_TEXTBOX_MOVE_CARET_FIRST_ONLY) {
		TextboxMoveCaret(textbox, &textbox->carets[1], ~flags & ES_TEXTBOX_MOVE_CARET_BACKWARDS, 
				flags & 0xFF, flags & ES_TEXTBOX_MOVE_CARET_STRONG_WHITESPACE);
	}
}

void TextboxRepaintLine(EsTextbox *textbox, int line) {
	if (line == -1 || (~textbox->flags & ES_TEXTBOX_MULTILINE)) {
		textbox->Repaint(true);
	} else {
		EsRectangle borders = textbox->borders;
		int topInset = textbox->insets.t;

		TextboxVisibleLine *visibleLine = TextboxGetVisibleLine(textbox, line);

		if (visibleLine) {
			EsRectangle bounds = textbox->GetBounds();
			EsRectangle lineBounds = ES_RECT_4(bounds.l + borders.l, bounds.r - borders.r, 
					visibleLine->yPosition + topInset - 1 - textbox->scroll.position[1], 
					visibleLine->yPosition + topInset + textbox->lines[line].height - textbox->scroll.position[1]);
			// EsPrint("textbox bounds %R; line bounds %R\n", bounds);
			textbox->Repaint(false, lineBounds);
		}
	}
}

void TextboxSetHorizontalScroll(EsTextbox *textbox, int scroll) {
	textbox->Repaint(true);
	textbox->oldHorizontalScroll = scroll;
}

void TextboxRefreshVisibleLines(EsTextbox *textbox, bool repaint = true) {
	if (textbox->visibleLines.Length()) {
		textbox->visibleLines.SetLength(0);
	}

	int scrollX = textbox->scroll.position[0], scrollY = textbox->scroll.position[1];
	EsRectangle bounds = textbox->GetBounds();

	int32_t low = 0, high = textbox->lines.Length() - 1, target = scrollY - textbox->insets.t;

	while (low != high) {
		int32_t middle = (low + high) / 2;
		int32_t position = textbox->lines[middle].yPosition;

		if (position < target && low != middle) low = middle;
		else if (position > target && high != middle) high = middle;
		else break;
	}

	textbox->firstVisibleLine = (low + high) / 2;
	if (textbox->firstVisibleLine) textbox->firstVisibleLine--;

	for (int32_t i = textbox->firstVisibleLine; i < (int32_t) textbox->lines.Length(); i++) {
		TextboxVisibleLine line = {};
		line.yPosition = textbox->lines[i].yPosition;
		textbox->visibleLines.Add(line);

		if (line.yPosition - scrollY > bounds.b) {
			break;
		}
	}

	bool refreshXLimit = false;

	for (uintptr_t i = 0; i < textbox->visibleLines.Length(); i++) {
		DocumentLine *line = &textbox->lines[textbox->firstVisibleLine + i];

		if (line->lengthWidth != -1) {
			continue;
		}

		line->lengthWidth = TextGetStringWidth(textbox, &textbox->textStyle,
				line->GetBuffer(textbox), line->lengthBytes);

		if (textbox->longestLine != -1 && line->lengthWidth > textbox->longestLineWidth) {
			textbox->longestLine = textbox->firstVisibleLine + i;
			textbox->longestLineWidth = line->lengthWidth;
			refreshXLimit = true;
		}
	}

	if (refreshXLimit) {
		textbox->scroll.Refresh();
		EsElementRelayout(textbox);
	}

	textbox->scroll.SetX(scrollX);
	if (repaint) textbox->Repaint(true);
}

void TextboxLineCountChangeCleanup(EsTextbox *textbox, int32_t offsetDelta, int32_t startLine) {
	for (int32_t i = startLine; i < (int32_t) textbox->lines.Length(); i++) {
		DocumentLine *line = &textbox->lines[i], *previous = &textbox->lines[i - 1];
		line->yPosition = previous->yPosition + previous->height;
		line->offset += offsetDelta;
	}

	TextboxRefreshVisibleLines(textbox);
}

void EsTextboxMoveCaret(EsTextbox *textbox, int32_t line, int32_t byte) {
	EsMessageMutexCheck();

	textbox->carets[0].line = line;
	textbox->carets[0].byte = byte;
	textbox->carets[1].line = line;
	textbox->carets[1].byte = byte;
	textbox->Repaint(true);
	TextboxUpdateCommands(textbox, true);
}

void EsTextboxGetSelection(EsTextbox *textbox, int32_t *fromLine, int32_t *fromByte, int32_t *toLine, int32_t *toByte) {
	EsMessageMutexCheck();

	*fromLine = textbox->carets[0].line;
	*fromByte = textbox->carets[0].byte;
	*toLine   = textbox->carets[1].line;
	*toByte   = textbox->carets[1].byte;
}

void EsTextboxSetSelection(EsTextbox *textbox, int32_t fromLine, int32_t fromByte, int32_t toLine, int32_t toByte) {
	EsMessageMutexCheck();

	if (fromByte == -1) fromByte = textbox->lines[fromLine].lengthBytes;
	if (toByte == -1) toByte = textbox->lines[toLine].lengthBytes;
	if (fromByte < 0 || toByte < 0 || fromByte > textbox->lines[fromLine].lengthBytes || toByte > textbox->lines[toLine].lengthBytes) return;
	textbox->carets[0].line = fromLine;
	textbox->carets[0].byte = fromByte;
	textbox->carets[1].line = toLine;
	textbox->carets[1].byte = toByte;
	textbox->Repaint(true);
	TextboxUpdateCommands(textbox, true);
	EsTextboxEnsureCaretVisible(textbox);
}

void EsTextboxSelectAll(EsTextbox *textbox) {
	EsMessageMutexCheck();

	TextboxMoveCaret(textbox, &textbox->carets[0], false, MOVE_CARET_ALL);
	TextboxMoveCaret(textbox, &textbox->carets[1], true, MOVE_CARET_ALL);
	EsTextboxEnsureCaretVisible(textbox);
	textbox->Repaint(true);
}

void EsTextboxClear(EsTextbox *textbox, bool sendUpdatedMessage) {
	EsMessageMutexCheck();

	EsTextboxSelectAll(textbox);
	EsTextboxInsert(textbox, "", 0, sendUpdatedMessage);
}

size_t EsTextboxGetLineLength(EsTextbox *textbox, uintptr_t line) {
	EsMessageMutexCheck();

	return textbox->lines[line].lengthBytes;
}

struct TextboxUndoItemHeader {
	EsTextbox *textbox;
	TextboxCaret caretsBefore[2];
	size_t insertBytes;
	double timeStampMs;
	// Followed by insert string.
};

void TextboxUndoItemCallback(const void *item, EsUndoManager *manager, EsMessage *message) {
	if (message->type == ES_MSG_UNDO_INVOKE) {
		TextboxUndoItemHeader *header = (TextboxUndoItemHeader *) item;
		EsTextbox *textbox = header->textbox;
		EsAssert(textbox->undo == manager);
		TextboxRepaintLine(textbox, textbox->carets[0].line);
		TextboxRepaintLine(textbox, textbox->carets[0].line);
		textbox->carets[0] = header->caretsBefore[0];
		textbox->carets[1] = header->caretsBefore[1];
		EsTextboxInsert(textbox, (const char *) (header + 1), header->insertBytes, true);
	} else if (message->type == ES_MSG_UNDO_CANCEL) {
		// Nothing to do.
	}
}

void EsTextboxInsert(EsTextbox *textbox, const char *string, ptrdiff_t stringBytes, bool sendUpdatedMessage) {
	EsMessageMutexCheck();

	// EsPerformanceTimerPush();
	// double measureLineTime = 0;

	if (stringBytes == -1) {
		stringBytes = EsCStringLength(string);
	}

	TextboxUndoItemHeader *undoItem = nullptr;
	size_t undoItemBytes = 0;

	textbox->wordSelectionAnchor  = textbox->carets[0];
	textbox->wordSelectionAnchor2 = textbox->carets[1];

	textbox->verticalMotionHorizontalDepth = -1;

	// ::: Delete the selected text.

	// Step 1: Get the range of text we're deleting.

	TextboxCaret deleteFrom, deleteTo;
	int comparison = TextboxCompareCarets(textbox->carets + 0, textbox->carets + 1);

	if      (comparison < 0) deleteFrom = textbox->carets[0], deleteTo = textbox->carets[1];
	else if (comparison > 0) deleteFrom = textbox->carets[1], deleteTo = textbox->carets[0];

	if (comparison) {
		textbox->carets[0] = textbox->carets[1] = deleteFrom;

		// Step 2: Calculate the number of bytes we are deleting.

		int32_t deltaBytes;

		if (deleteFrom.line == deleteTo.line) {
			deltaBytes = deleteFrom.byte - deleteTo.byte;
		} else {
			TextboxSetActiveLine(textbox, -1);

			deltaBytes = deleteFrom.byte - deleteTo.byte;

			for (int32_t i = deleteFrom.line; i < deleteTo.line; i++) {
				deltaBytes -= textbox->lines[i].lengthBytes;
			}
		}

		if (textbox->undo) {
			// Step 3: Allocate space for an undo item.

			undoItemBytes = sizeof(TextboxUndoItemHeader) - deltaBytes + deleteTo.line - deleteFrom.line;
			undoItem = (TextboxUndoItemHeader *) EsHeapAllocate(undoItemBytes, false);
			EsMemoryZero(undoItem, sizeof(TextboxUndoItemHeader));
			undoItem->insertBytes = undoItemBytes - sizeof(TextboxUndoItemHeader);
		}

		if (deleteFrom.line == deleteTo.line) {
			EsAssert(deltaBytes < 0); // Expected deleteTo > deleteFrom.
			DocumentLine *line = &textbox->lines[deleteFrom.line];
			TextboxSetActiveLine(textbox, deleteFrom.line);

			// Step 4: Update the width of the line and repaint it.

			line->lengthWidth = TextGetStringWidth(textbox, &textbox->textStyle, textbox->activeLine, textbox->activeLineBytes);
			TextboxRepaintLine(textbox, deleteFrom.line);

			// Step 5: Update the active line buffer.

			if (undoItem) EsMemoryCopy(undoItem + 1, textbox->activeLine + deleteFrom.byte, -deltaBytes);
			EsMemoryMove(textbox->activeLine + deleteTo.byte, textbox->activeLine + line->lengthBytes, deltaBytes, false);
			textbox->activeLineBytes += deltaBytes;
			line->lengthBytes += deltaBytes;

			// Step 6: Update the longest line.

			if (textbox->longestLine == deleteFrom.line && line->lengthWidth < textbox->longestLineWidth) {
				textbox->longestLine = -1;
			}
		} else {
			if (undoItem) {
				// Step 4: Copy into the undo item.

				char *position = (char *) (undoItem + 1);
				
				for (int32_t i = deleteFrom.line; i <= deleteTo.line; i++) {
					char *from = textbox->data + textbox->lines[i].offset;
					char *to = textbox->data + textbox->lines[i].offset + textbox->lines[i].lengthBytes;	
					if (i == deleteFrom.line) from += deleteFrom.byte;
					if (i == deleteTo.line) to += deleteTo.byte - textbox->lines[i].lengthBytes;
					EsMemoryCopy(position, from, to - from);
					position += to - from;
					if (i != deleteTo.line) *position++ = '\n';
				}
			}

			// Step 5: Remove the text from the buffer.

			EsMemoryMove(textbox->data + deleteTo.byte + textbox->lines[deleteTo.line].offset, textbox->data + textbox->dataBytes, deltaBytes, false);
			textbox->dataBytes += deltaBytes;

			// Step 6: Merged the joined lines.

			DocumentLine *firstLine = &textbox->lines[deleteFrom.line];
			firstLine->lengthBytes = textbox->lines[deleteTo.line].lengthBytes - deleteTo.byte + deleteFrom.byte;
			firstLine->lengthWidth = TextGetStringWidth(textbox, &textbox->textStyle, textbox->data + firstLine->offset, firstLine->lengthBytes);

			// Step 7: Remove the deleted lines and update the textbox.

			textbox->lines.DeleteMany(deleteFrom.line + 1, deleteTo.line - deleteFrom.line);
			textbox->longestLine = -1;
			TextboxLineCountChangeCleanup(textbox, deltaBytes, deleteFrom.line + 1);
		}
	} else {
		if (textbox->undo) {
			undoItemBytes = sizeof(TextboxUndoItemHeader);
			undoItem = (TextboxUndoItemHeader *) EsHeapAllocate(undoItemBytes, false);
			EsMemoryZero(undoItem, sizeof(TextboxUndoItemHeader));
		}
	}

	if (undoItem) {
		undoItem->caretsBefore[0] = undoItem->caretsBefore[1] = textbox->carets[0];
	}

	// ::: Insert the new text.

	if (!stringBytes) goto done;

	{
		TextboxCaret insertionPoint = textbox->carets[0];

		DocumentLine *line = &textbox->lines[insertionPoint.line];
		int32_t lineByteOffset = line->offset,
			offsetIntoLine = insertionPoint.byte,
			byteOffset = offsetIntoLine + lineByteOffset;

		// Step 1: Count the number of newlines in the input string.

		uintptr_t position = 0,
			  newlines = 0,
			  carriageReturns = 0;

		while (position < (size_t) stringBytes) {
			int length;
			UTF8_LENGTH_CHAR(string + position, length);
			if (length == 0) length = 1;

			if (position + length > (size_t) stringBytes) {
				break;
			} else if (string[position] == '\n') {
				newlines++;
			} else if (string[position] == '\r' && position != (size_t) stringBytes - 1 && string[position + 1] == '\n') {
				carriageReturns++;
			}

			position += length;
		}

		size_t bytesToInsert = stringBytes - newlines - carriageReturns;

		if (!newlines || (~textbox->flags & ES_TEXTBOX_MULTILINE)) {
			// Step 2: Update the active line buffer.

			TextboxSetActiveLine(textbox, insertionPoint.line);
			TextboxBufferResize((void **) &textbox->activeLine, &textbox->activeLineAllocated, (textbox->activeLineBytes += bytesToInsert), 1);
			EsMemoryMove(textbox->activeLine + offsetIntoLine, textbox->activeLine + line->lengthBytes, bytesToInsert, false);

			const char *dataToInsert = string;
			size_t added = 0;

			for (uintptr_t i = 0; i < newlines + 1; i++) {
				const char *end = (const char *) EsCRTmemchr(dataToInsert, '\n', stringBytes - (dataToInsert - string)) ?: string + stringBytes;
				bool carriageReturn = end != string && end[-1] == '\r';
				if (carriageReturn) end--;
				EsMemoryCopy(textbox->activeLine + offsetIntoLine + added, dataToInsert, end - dataToInsert);
				added += end - dataToInsert;
				dataToInsert = end + (carriageReturn ? 2 : 1);
			}

			EsAssert(added == bytesToInsert); // Added incorrect number of bytes in EsTextboxInsert.

			line->lengthBytes += bytesToInsert;

			// Step 3: Update the carets, line width, and repaint it.

			textbox->carets[0].byte += bytesToInsert;
			textbox->carets[1].byte += bytesToInsert;
			line->lengthWidth = TextGetStringWidth(textbox, &textbox->textStyle, textbox->activeLine, line->lengthBytes);
			TextboxRepaintLine(textbox, insertionPoint.line);

			// Step 4: Update the longest line.

			if (textbox->longestLine != -1 && line->lengthWidth > textbox->longestLineWidth) {
				textbox->longestLine = insertionPoint.line;
				textbox->longestLineWidth = line->lengthWidth;
			}
		} else {
			// Step 2: Make room in the buffer for the contents of the string.

			TextboxSetActiveLine(textbox, -1);
			TextboxBufferResize((void **) &textbox->data, &textbox->dataAllocated, textbox->dataBytes + bytesToInsert, 1);
			EsMemoryMove(textbox->data + byteOffset, textbox->data + textbox->dataBytes, bytesToInsert, false);
			textbox->dataBytes += bytesToInsert;

			// Step 3: Truncate the insertion line.

			int32_t truncation = line->lengthBytes - insertionPoint.byte;
			line->lengthBytes = insertionPoint.byte;

			// Step 4: Add the new lines.

			textbox->lines.InsertMany(insertionPoint.line + 1, newlines);
			const char *dataToInsert = string;
			uintptr_t insertedBytes = 0;

			for (uintptr_t i = 0; i < newlines + 1; i++) {
				DocumentLine *line = &textbox->lines[insertionPoint.line + i], *previous = line - 1;

				// Step 4a: Initialise the line.

				if (i) {
					EsMemoryZero(line, sizeof(*line));
					line->height = TextGetLineHeight(textbox, &textbox->textStyle);
					line->yPosition = previous->yPosition + previous->height;
					line->offset = lineByteOffset + insertedBytes;
				}

				// Step 4b: Copy the string data into the line.

				const char *end = (const char *) EsCRTmemchr(dataToInsert, '\n', stringBytes - (dataToInsert - string)) ?: string + stringBytes;
				bool carriageReturn = end != string && end[-1] == '\r';
				if (carriageReturn) end--;
				EsMemoryCopy(textbox->data + line->offset + line->lengthBytes, dataToInsert, end - dataToInsert);
				line->lengthBytes += end - dataToInsert;
				insertedBytes += line->lengthBytes;
				dataToInsert = end + (carriageReturn ? 2 : 1);

				if (i == newlines) {
					line->lengthBytes += truncation;
				}

				// Step 4c: Update the line's width.

				// EsPerformanceTimerPush(); 
#if 0
				line->lengthWidth = EsTextGetPartialStringWidth(&textbox->textStyle, textbox->data + line->offset, line->lengthBytes, 0, line->lengthBytes);
#else
				line->lengthWidth = -1;
#endif
				// double time = EsPerformanceTimerPop();
				// measureLineTime += time;
				// EsPrint("Measured the length of line %d in %Fms.\n", insertionPoint.line + i, time * 1000);
			}

			// Step 5: Update the carets.

			textbox->carets[0].line = insertionPoint.line + newlines;
			textbox->carets[1].line = insertionPoint.line + newlines;
			textbox->carets[0].byte = textbox->lines[insertionPoint.line + newlines].lengthBytes - truncation;
			textbox->carets[1].byte = textbox->lines[insertionPoint.line + newlines].lengthBytes - truncation;

			// Step 6: Update the textbox.

			textbox->longestLine = -1;
			TextboxLineCountChangeCleanup(textbox, bytesToInsert, insertionPoint.line + 1 + newlines);
		}

		if (undoItem) undoItem->caretsBefore[1] = textbox->carets[0];
	}

	done:;

	if (sendUpdatedMessage) {
		EsMessage m = { ES_MSG_TEXTBOX_UPDATED };
		EsMessageSend(textbox, &m);
	} else if (textbox->overlayCallback) {
		EsMessage m = { ES_MSG_TEXTBOX_UPDATED };
		textbox->overlayCallback(textbox, &m);
	}

	if (textbox->state & UI_STATE_DESTROYING) {
		return;
	}

	TextboxFindLongestLine(textbox);
	InspectorNotifyElementContentChanged(textbox);

	if (undoItem && (undoItem->insertBytes || TextboxCompareCarets(undoItem->caretsBefore + 0, undoItem->caretsBefore + 1))) {
		undoItem->timeStampMs = EsTimeStampMs();

		EsUndoCallback previousCallback;
		const void *previousItem;

		if (!EsUndoInUndo(textbox->undo) 
				&& EsUndoPeek(textbox->undo, &previousCallback, &previousItem) 
				&& previousCallback == TextboxUndoItemCallback) {
			TextboxUndoItemHeader *header = (TextboxUndoItemHeader *) previousItem;

#define TEXTBOX_UNDO_TIMEOUT (500) // TODO Make this configurable.
			if (undoItem->timeStampMs - header->timeStampMs < TEXTBOX_UNDO_TIMEOUT) {
				if (!undoItem->insertBytes && !header->insertBytes 
						&& undoItem->caretsBefore[0].line == header->caretsBefore[1].line
						&& undoItem->caretsBefore[0].byte == header->caretsBefore[1].byte) {
					// Merge the items.
					undoItem->caretsBefore[0] = header->caretsBefore[0];
					EsUndoPop(textbox->undo);
				} else {
					// Add the new item to the same group as the previous.
					EsUndoContinueGroup(textbox->undo); 
				}
			}
		}

		undoItem->textbox = textbox;
		EsUndoPush(textbox->undo, TextboxUndoItemCallback, undoItem, undoItemBytes, false /* do not set instance's undo manager */);
	}

	EsHeapFree(undoItem);

	// double time = EsPerformanceTimerPop();
	// EsPrint("EsTextboxInsert in %Fms (%Fms measuring new lines).\n", time * 1000, measureLineTime * 1000);

	textbox->scroll.Refresh();
	TextboxUpdateCommands(textbox, true);
}

char *EsTextboxGetContents(EsTextbox *textbox, size_t *_bytes, uint32_t flags) {
	EsMessageMutexCheck();

	TextboxSetActiveLine(textbox, -1);

	bool includeNewline = textbox->flags & ES_TEXTBOX_MULTILINE;
	size_t bytes = textbox->dataBytes + (includeNewline ? textbox->lines.Length() : 0);
	char *buffer = (char *) EsHeapAllocate(bytes + 1, false);
	buffer[bytes] = 0;

	uintptr_t position = 0;
	uintptr_t lineFrom = 0, lineTo = textbox->lines.Length() - 1;

	if (flags & ES_TEXTBOX_GET_CONTENTS_SELECTED_ONLY) {
		lineFrom = textbox->carets[0].line;
		lineTo = textbox->carets[1].line;

		if (lineFrom > lineTo) { 
			uintptr_t swap = lineFrom; 
			lineFrom = lineTo, lineTo = swap; 
		}
	}

	for (uintptr_t i = lineFrom; i <= lineTo; i++) {
		DocumentLine *line = &textbox->lines[i];

		uintptr_t offsetFrom = 0;
		uintptr_t offsetTo = line->lengthBytes;

		if (flags & ES_TEXTBOX_GET_CONTENTS_SELECTED_ONLY) {
			if (i == lineFrom) {
				offsetFrom = TextboxCompareCarets(textbox->carets + 0, textbox->carets + 1) < 0 ? textbox->carets[0].byte : textbox->carets[1].byte; 
			}

			if (i == lineTo) {
				offsetTo = TextboxCompareCarets(textbox->carets + 0, textbox->carets + 1) > 0 ? textbox->carets[0].byte : textbox->carets[1].byte; 
			}
		}

		EsMemoryCopy(buffer + position, line->GetBuffer(textbox) + offsetFrom, offsetTo - offsetFrom);
		position += offsetTo - offsetFrom;

		if (includeNewline && i != lineTo) {
			buffer[position++] = '\n';
		}
	}

	buffer[position] = 0;
	EsAssert(position <= bytes); 
	if (_bytes) *_bytes = position;

	char *result = (char *) EsHeapReallocate(buffer, position + 1, false);

	if (!result) {
		EsHeapFree(buffer);
	}

	return result;
}

double EsTextboxGetContentsAsDouble(EsTextbox *textbox, uint32_t flags) {
	size_t bytes;
	char *text = EsTextboxGetContents(textbox, &bytes, flags);
	double result = EsDoubleParse(text, bytes, nullptr); 
	EsHeapFree(text);
	return result;
}

bool EsTextboxFind(EsTextbox *textbox, const char *needle, intptr_t _needleBytes, int32_t *_line, int32_t *_byte, uint32_t flags) {
	EsMessageMutexCheck();

	if (_needleBytes == 0) {
		return false;
	}

	uintptr_t needleBytes = _needleBytes == -1 ? EsCStringLength(needle) : _needleBytes;
	uint32_t lineIndex = *_line, byteIndex = *_byte;
	bool firstLoop = true;

	while (true) {
		DocumentLine *line = &textbox->lines[lineIndex];
		const char *buffer = line->GetBuffer(textbox);
		size_t bufferBytes = line->lengthBytes;
		EsAssert(byteIndex <= bufferBytes); // Invalid find byte offset.

		// TODO Case-insensitive search.
		// TODO Ignore quotation mark type.

		if (flags & ES_TEXTBOX_FIND_BACKWARDS) {
			if (bufferBytes >= needleBytes) {
				for (uintptr_t i = byteIndex; i >= needleBytes; i--) {
					for (uintptr_t j = 0; j < needleBytes; j++) {
						if (buffer[i - needleBytes + j] != needle[j]) {
							goto previousPosition;
						}
					}

					*_line = lineIndex;
					*_byte = i - needleBytes;
					return true;

					previousPosition:;
				}
			}

			if ((int32_t) lineIndex <= *_line && !firstLoop) {
				return false;
			}

			if (lineIndex == 0) {
				firstLoop = false;
				lineIndex = textbox->lines.Length() - 1;
			} else {
				lineIndex--;
			}

			byteIndex = textbox->lines[lineIndex].lengthBytes;
		} else {
			if (bufferBytes >= needleBytes) {
				for (uintptr_t i = byteIndex; i <= bufferBytes - needleBytes; i++) {
					for (uintptr_t j = 0; j < needleBytes; j++) {
						if (buffer[i + j] != needle[j]) {
							goto nextPosition;
						}
					}

					*_line = lineIndex;
					*_byte = i;
					return true;

					nextPosition:;
				}
			}

			lineIndex++;

			if ((int32_t) lineIndex > *_line && !firstLoop) {
				return false;
			}

			if (lineIndex == textbox->lines.Length()) {
				firstLoop = false;
				lineIndex = 0;
			}

			byteIndex = 0;
		}
	}

	return false;
}

bool TextboxFindCaret(EsTextbox *textbox, int positionX, int positionY, bool secondCaret, int clickChainCount) {
	int startLine0 = textbox->carets[0].line, startLine1 = textbox->carets[1].line;
	EsRectangle bounds = textbox->GetBounds();

	if (positionX < 0) {
		positionX = 0;
	} else if (positionX >= bounds.r) {
		positionX = bounds.r - 1;
	}

	if (positionY < 0) {
		positionY = 0;
	} else if (positionY >= bounds.b) {
		positionY = bounds.b - 1;
	}

	if (clickChainCount >= 4) {
		textbox->carets[0].line = 0;
		textbox->carets[0].byte = 0;
		textbox->carets[1].line = textbox->lines.Length() - 1;
		textbox->carets[1].byte = textbox->lines[textbox->lines.Length() - 1].lengthBytes;
	} else {
		for (uintptr_t i = 0; i < textbox->visibleLines.Length(); i++) {
			TextboxVisibleLine *visibleLine = &textbox->visibleLines[i];
			DocumentLine *line = &textbox->lines[textbox->firstVisibleLine + i];

			EsRectangle lineBounds = ES_RECT_4(textbox->insets.l, bounds.r, 
					textbox->insets.t + visibleLine->yPosition, 
					textbox->insets.t + visibleLine->yPosition + line->height);
			lineBounds.l -= textbox->scroll.position[0];
			lineBounds.t -= textbox->scroll.position[1];
			lineBounds.b -= textbox->scroll.position[1];

			if (!((positionY >= lineBounds.t || i + textbox->firstVisibleLine == 0) && (positionY < lineBounds.b 
							|| i + textbox->firstVisibleLine == textbox->lines.Length() - 1))) {
				continue;
			}

			if (!line->lengthBytes) {
				textbox->carets[1].byte = 0;
			} else {
				DocumentLine *line = &textbox->lines[i + textbox->firstVisibleLine];
				int pointX = positionX + textbox->scroll.position[0] - textbox->insets.l;
				if (pointX < 0) pointX = 0;
				ptrdiff_t result = TextGetCharacterAtPoint(textbox, &textbox->textStyle,
						line->GetBuffer(textbox), line->lengthBytes, 
						&pointX, ES_TEXT_GET_CHARACTER_AT_POINT_MIDDLE);
				textbox->carets[1].byte = result == -1 ? line->lengthBytes : result;
			}

			textbox->carets[1].line = i + textbox->firstVisibleLine;

			break;
		}

		if (!secondCaret) {
			textbox->carets[0] = textbox->carets[1];

			if (clickChainCount == 2) {
				TextboxMoveCaret(textbox, textbox->carets + 0, MOVE_CARET_BACKWARDS, MOVE_CARET_WORD, true);
				TextboxMoveCaret(textbox, textbox->carets + 1, MOVE_CARET_FORWARDS,  MOVE_CARET_WORD, true);
				textbox->wordSelectionAnchor  = textbox->carets[0];
				textbox->wordSelectionAnchor2 = textbox->carets[1];
			} else if (clickChainCount == 3) {
				TextboxMoveCaret(textbox, textbox->carets + 0, MOVE_CARET_BACKWARDS, MOVE_CARET_LINE, true);
				TextboxMoveCaret(textbox, textbox->carets + 1, MOVE_CARET_FORWARDS,  MOVE_CARET_LINE, true);
				textbox->wordSelectionAnchor  = textbox->carets[0];
				textbox->wordSelectionAnchor2 = textbox->carets[1];
			}
		} else {
			if (clickChainCount == 2) {
				if (TextboxCompareCarets(textbox->carets + 1, textbox->carets + 0) < 0) {
					TextboxMoveCaret(textbox, textbox->carets + 1, MOVE_CARET_BACKWARDS, MOVE_CARET_WORD);
					textbox->carets[0] = textbox->wordSelectionAnchor2;
				} else {
					TextboxMoveCaret(textbox, textbox->carets + 1, MOVE_CARET_FORWARDS, MOVE_CARET_WORD);
					textbox->carets[0] = textbox->wordSelectionAnchor;
				}
			} else if (clickChainCount == 3) {
				if (TextboxCompareCarets(textbox->carets + 1, textbox->carets + 0) < 0) {
					TextboxMoveCaret(textbox, textbox->carets + 1, MOVE_CARET_BACKWARDS, MOVE_CARET_LINE);
					textbox->carets[0] = textbox->wordSelectionAnchor2;
				} else {
					TextboxMoveCaret(textbox, textbox->carets + 1, MOVE_CARET_FORWARDS, MOVE_CARET_LINE);
					textbox->carets[0] = textbox->wordSelectionAnchor;
				}
			}
		}
	}

	TextboxUpdateCommands(textbox, true);
	return textbox->carets[0].line != startLine0 || textbox->carets[1].line != startLine1;
}

void TextboxMoveCaretToCursor(EsTextbox *textbox, int x, int y, bool doNotMoveIfNoSelection) {
	int oldCompare = TextboxCompareCarets(textbox->carets + 0, textbox->carets + 1);
	bool hasSelection = oldCompare != 0;
	TextboxCaret old[2] = { textbox->carets[0], textbox->carets[1] };
	bool lineChanged = TextboxFindCaret(textbox, x, y, gui.clickChainCount == 1, gui.clickChainCount);

	if (doNotMoveIfNoSelection && TextboxCompareCarets(&old[0], &old[1]) != 0) {
		textbox->carets[0] = old[0];
		textbox->carets[1] = old[1];
	} else if (gui.clickChainCount == 1 && !EsKeyboardIsShiftHeld()) {
		textbox->carets[0] = textbox->carets[1];
	}

	TextboxUpdateCommands(textbox, true);
	textbox->verticalMotionHorizontalDepth = -1;
	TextboxRepaintLine(textbox, lineChanged || hasSelection ? -1 : textbox->carets[0].line);
	EsTextboxEnsureCaretVisible(textbox);
}

int ProcessTextboxMarginMessage(EsElement *element, EsMessage *message) {
	EsTextbox *textbox = (EsTextbox *) element->parent;

	if (message->type == ES_MSG_PAINT) {
		EsPainter *painter = message->painter;

		for (int32_t i = 0; i < (int32_t) textbox->visibleLines.Length(); i++) {
			TextboxVisibleLine *visibleLine = &textbox->visibleLines[i];
			DocumentLine *line = &textbox->lines[i + textbox->firstVisibleLine];

			EsRectangle bounds;
			bounds.l = painter->offsetX + element->currentStyle->insets.l;
			bounds.r = painter->offsetX + painter->width - element->currentStyle->insets.r;
			bounds.t = painter->offsetY + textbox->insets.t + visibleLine->yPosition - textbox->scroll.position[1];
			bounds.b = bounds.t + line->height;

			char label[64];
			EsTextRun textRun[2] = {};
			element->currentStyle->GetTextStyle(&textRun[0].style);
			textRun[0].style.figures = ES_TEXT_FIGURE_TABULAR;
			textRun[1].offset = EsStringFormat(label, sizeof(label), "%d", i + textbox->firstVisibleLine + 1);
			EsTextPlanProperties properties = {};
			properties.flags = ES_TEXT_V_CENTER | ES_TEXT_H_RIGHT | ES_TEXT_ELLIPSIS | ES_TEXT_PLAN_SINGLE_USE;
			EsTextPlan *plan = EsTextPlanCreate(element, &properties, bounds, label, textRun, 1);
			if (plan) EsDrawText(painter, plan, bounds, nullptr, nullptr);
		}
	}

	return 0;
}

void TextboxStyleChanged(EsTextbox *textbox) {
	textbox->borders = textbox->currentStyle->borders;
	textbox->insets = textbox->currentStyle->insets;

	if (textbox->flags & ES_TEXTBOX_MARGIN) {
		int marginWidth = textbox->margin->currentStyle->preferredWidth;
		textbox->borders.l += marginWidth;
		textbox->insets.l += marginWidth + textbox->margin->currentStyle->gapMajor;
	}

	int lineHeight = TextGetLineHeight(textbox, &textbox->textStyle);

	for (int32_t i = 0; i < (int32_t) textbox->lines.Length(); i++) {
		DocumentLine *line = &textbox->lines[i];
		DocumentLine *previous = i ? (&textbox->lines[i - 1]) : nullptr;
		line->height = lineHeight;
		line->yPosition = previous ? (previous->yPosition + previous->height) : 0;
		line->lengthWidth = -1;
		textbox->longestLine = -1;
	}

	TextboxRefreshVisibleLines(textbox);
	TextboxFindLongestLine(textbox);
	textbox->scroll.Refresh();
	EsElementRepaint(textbox);
}

int ProcessTextboxMessage(EsElement *element, EsMessage *message) {
	EsTextbox *textbox = (EsTextbox *) element;

	if (!textbox->editing && textbox->overlayCallback) {
		int response = textbox->overlayCallback(element, message);
		if (response != 0 && message->type != ES_MSG_DESTROY) return response;
	}

	textbox->scroll.ReceivedMessage(message);

	int response = ES_HANDLED;

	if (message->type == ES_MSG_PAINT) {
		EsPainter *painter = message->painter;

		EsTextSelection selectionProperties = {};
		selectionProperties.hideCaret = (~textbox->state & UI_STATE_FOCUSED) || (textbox->flags & ES_ELEMENT_DISABLED) || !textbox->editing;
		selectionProperties.snapCaretToInsets = true;
		selectionProperties.background = textbox->currentStyle->metrics->selectedBackground;
		selectionProperties.foreground = textbox->currentStyle->metrics->selectedText;

		EsRectangle clip;
		EsRectangleClip(painter->clip, ES_RECT_4(painter->offsetX + textbox->borders.l, 
					painter->offsetX + painter->width - textbox->borders.r, 
					painter->offsetY + textbox->borders.t, 
					painter->offsetY + painter->height - textbox->borders.b), &clip);

		Array<EsTextRun> textRuns = {};

		for (int32_t i = 0; i < (int32_t) textbox->visibleLines.Length(); i++) {
			TextboxVisibleLine *visibleLine = &textbox->visibleLines[i];
			DocumentLine *line = &textbox->lines[i + textbox->firstVisibleLine];

			EsRectangle lineBounds = ES_RECT_4(painter->offsetX + textbox->insets.l, 
					painter->offsetX + painter->width, 
					painter->offsetY + textbox->insets.t + visibleLine->yPosition, 
					painter->offsetY + textbox->insets.t + visibleLine->yPosition + line->height);
			lineBounds.l -= textbox->scroll.position[0];
			lineBounds.t -= textbox->scroll.position[1];
			lineBounds.b -= textbox->scroll.position[1];

			if (~textbox->flags & ES_TEXTBOX_MULTILINE) {
				lineBounds.b = painter->offsetY + painter->height - textbox->insets.b;
			}

			int32_t caret0 = textbox->carets[0].byte, caret1 = textbox->carets[1].byte;
			if (textbox->carets[0].line < i + textbox->firstVisibleLine) caret0 = -2;
			if (textbox->carets[0].line > i + textbox->firstVisibleLine) caret0 = line->lengthBytes + 2;
			if (textbox->carets[1].line < i + textbox->firstVisibleLine) caret1 = -2;
			if (textbox->carets[1].line > i + textbox->firstVisibleLine) caret1 = line->lengthBytes + 2;

			if (textbox->carets[1].line == i + textbox->firstVisibleLine && textbox->syntaxHighlightingLanguage) {
				EsRectangle line = ES_RECT_4(painter->offsetX, painter->offsetX + painter->width, lineBounds.t, lineBounds.b);
				EsDrawBlock(painter, line, textbox->syntaxHighlightingColors[0]);
			}

			if (textbox->syntaxHighlightingLanguage && line->lengthBytes) {
				if (textRuns.Length()) textRuns.SetLength(0);
				textRuns = TextApplySyntaxHighlighting(&textbox->textStyle, textbox->syntaxHighlightingLanguage, 
						textbox->syntaxHighlightingColors, textRuns, line->GetBuffer(textbox), line->lengthBytes);
			} else {
				textRuns.SetLength(2);
				textRuns[0].style = textbox->textStyle;
				textRuns[0].offset = 0;
				textRuns[1].offset = line->lengthBytes;
			}

			EsTextPlanProperties properties = {};
			properties.flags = ES_TEXT_V_CENTER | ES_TEXT_H_LEFT | ES_TEXT_PLAN_SINGLE_USE;
			selectionProperties.caret0 = caret0; 
			selectionProperties.caret1 = caret1;
			EsTextPlan *plan;

			if (!textRuns.Length()) {
				plan = nullptr;
			} else if (textRuns[1].offset) {
				plan = EsTextPlanCreate(element, &properties, lineBounds, line->GetBuffer(textbox), textRuns.array, textRuns.Length() - 1);
			} else {
				textRuns[1].offset = 1; // Make sure that the caret and selection is draw correctly, even on empty lines.
				plan = EsTextPlanCreate(element, &properties, lineBounds, " ", textRuns.array, textRuns.Length() - 1);
			}

			if (plan) {
				EsDrawText(painter, plan, lineBounds, &clip, &selectionProperties);
			}
		}

		textRuns.Free();
	} else if (message->type == ES_MSG_LAYOUT) {
		EsRectangle bounds = textbox->GetBounds();

		if (textbox->margin) {
			int marginWidth = textbox->margin->currentStyle->preferredWidth;
			textbox->margin->InternalMove(marginWidth, Height(bounds), bounds.l, bounds.t);
		}

		TextboxRefreshVisibleLines(textbox);

		if (textbox->editing && (~textbox->flags & ES_TEXTBOX_MULTILINE)) {
			EsTextboxEnsureCaretVisible(textbox);
		}
	} else if (message->type == ES_MSG_DESTROY) {
		textbox->visibleLines.Free();
		textbox->lines.Free();
		UndoManagerDestroy(&textbox->localUndo);
		EsHeapFree(textbox->activeLine);
		EsHeapFree(textbox->data);
		EsHeapFree(textbox->editStartContent);
	} else if (message->type == ES_MSG_KEY_TYPED && !IsScancodeNonTypeable(message->keyboard.scancode)) {
		bool verticalMotion = false;
		bool ctrl = message->keyboard.modifiers & ES_MODIFIER_CTRL;

		if (message->keyboard.modifiers & ~(ES_MODIFIER_CTRL | ES_MODIFIER_ALT | ES_MODIFIER_SHIFT)) {
			// Unused modifier.
			return 0;
		}

		if (message->keyboard.scancode == ES_SCANCODE_LEFT_ARROW || message->keyboard.scancode == ES_SCANCODE_RIGHT_ARROW
				|| message->keyboard.scancode == ES_SCANCODE_HOME || message->keyboard.scancode == ES_SCANCODE_END
				|| message->keyboard.scancode == ES_SCANCODE_UP_ARROW || message->keyboard.scancode == ES_SCANCODE_DOWN_ARROW) {
			bool direction = (message->keyboard.scancode == ES_SCANCODE_LEFT_ARROW || message->keyboard.scancode == ES_SCANCODE_HOME 
					|| message->keyboard.scancode == ES_SCANCODE_UP_ARROW)
				? MOVE_CARET_BACKWARDS : MOVE_CARET_FORWARDS;
			int moveType = (message->keyboard.scancode == ES_SCANCODE_HOME || message->keyboard.scancode == ES_SCANCODE_END) 
				? (ctrl ? MOVE_CARET_ALL : MOVE_CARET_LINE)
				: ((message->keyboard.scancode == ES_SCANCODE_UP_ARROW || message->keyboard.scancode == ES_SCANCODE_DOWN_ARROW) 
						? MOVE_CARET_VERTICAL : (ctrl ? MOVE_CARET_WORD : MOVE_CARET_SINGLE));
			if (moveType == MOVE_CARET_VERTICAL) verticalMotion = true;

			int32_t lineFrom = textbox->carets[1].line;

			if (message->keyboard.modifiers & ES_MODIFIER_SHIFT) {
				TextboxMoveCaret(textbox, &textbox->carets[1], direction, moveType);
			} else {
				int caretCompare = TextboxCompareCarets(textbox->carets + 1, textbox->carets + 0);

				if ((caretCompare < 0 && direction == MOVE_CARET_BACKWARDS) || (caretCompare > 0 && direction == MOVE_CARET_FORWARDS)) {
					textbox->carets[0] = textbox->carets[1];
					TextboxUpdateCommands(textbox, true);
				} else if ((caretCompare > 0 && direction == MOVE_CARET_BACKWARDS) || (caretCompare < 0 && direction == MOVE_CARET_FORWARDS)) {
					textbox->carets[1] = textbox->carets[0];
					TextboxUpdateCommands(textbox, true);
				} else {
					TextboxMoveCaret(textbox, &textbox->carets[1], direction, moveType);
					textbox->carets[0] = textbox->carets[1];
					TextboxUpdateCommands(textbox, true);
				}
			}

			int32_t lineTo = textbox->carets[1].line;
			if (lineFrom > lineTo) { int32_t t = lineTo; lineTo = lineFrom; lineFrom = t; }
			for (int32_t i = lineFrom; i <= lineTo; i++) TextboxRepaintLine(textbox, i);
		} else if (message->keyboard.scancode == ES_SCANCODE_PAGE_UP || message->keyboard.scancode == ES_SCANCODE_PAGE_DOWN) {
			for (uintptr_t i = 0; i < 10; i++) {
				TextboxMoveCaret(textbox, textbox->carets + 1, 
						message->keyboard.scancode == ES_SCANCODE_PAGE_UP ? MOVE_CARET_BACKWARDS : MOVE_CARET_FORWARDS, 
						MOVE_CARET_VERTICAL);
			}

			if (~message->keyboard.modifiers & ES_MODIFIER_SHIFT) {
				textbox->carets[0] = textbox->carets[1];
				TextboxUpdateCommands(textbox, true);
			}
			
			textbox->Repaint(true);
			verticalMotion = true;
		} else if (message->keyboard.scancode == ES_SCANCODE_BACKSPACE || message->keyboard.scancode == ES_SCANCODE_DELETE) {
			if (!TextboxCompareCarets(textbox->carets + 0, textbox->carets + 1)) {
				TextboxMoveCaret(textbox, textbox->carets + 1, message->keyboard.scancode == ES_SCANCODE_BACKSPACE ? MOVE_CARET_BACKWARDS : MOVE_CARET_FORWARDS, 
						ctrl ? MOVE_CARET_WORD : MOVE_CARET_SINGLE);
			}

			EsTextboxInsert(textbox, EsLiteral(""));
		} else if (message->keyboard.scancode == ES_SCANCODE_ENTER && (textbox->flags & ES_TEXTBOX_EDIT_BASED)) {
			if (textbox->editing) {
				TextboxEndEdit(textbox, false);
			} else {
				EsTextboxStartEdit(textbox);
			}
		} else if (message->keyboard.scancode == ES_SCANCODE_ESCAPE && (textbox->flags & ES_TEXTBOX_EDIT_BASED)) {
			TextboxEndEdit(textbox, true);
		} else if (message->keyboard.scancode == ES_SCANCODE_TAB && (~textbox->flags & ES_TEXTBOX_ALLOW_TABS)) {
			response = 0;
		} else {
			if (!textbox->editing) {
				EsTextboxStartEdit(textbox);
			}

			int ic, isc;
			ConvertScancodeToCharacter(message->keyboard.scancode, &ic, &isc, true, textbox->flags & ES_TEXTBOX_MULTILINE); 
			int character = (message->keyboard.modifiers & ES_MODIFIER_SHIFT) ? isc : ic;

			if (ic != -1 && (message->keyboard.modifiers & ~ES_MODIFIER_SHIFT) == 0) {
				if (textbox->smartQuotes && api.global->useSmartQuotes) {
					DocumentLine *currentLine = &textbox->lines[textbox->carets[0].line];
					const char *buffer = currentLine->GetBuffer(textbox);
					bool left = !textbox->carets[0].byte || buffer[textbox->carets[0].byte - 1] == ' ';

					if (character == '"') {
						character = left ? 0x201C : 0x201D;
					} else if (character == '\'') {
						character = left ? 0x2018 : 0x2019;
					}
				}

				char buffer[4];
				EsTextboxInsert(textbox, buffer, utf8_encode(character, buffer));

				if (buffer[0] == '\n' && textbox->carets[0].line) {
					// Copy the indentation from the previous line.

					DocumentLine *previousLine = &textbox->lines[textbox->carets[0].line - 1];
					const char *buffer = previousLine->GetBuffer(textbox);
					int32_t i = 0;
					
					for (; i < previousLine->lengthBytes; i++) {
						if (buffer[i] != '\t') {
							break;
						}
					}

					EsTextboxInsert(textbox, buffer, i);
				}
			} else {
				response = 0;
			}
		}

		if (!verticalMotion) {
			textbox->verticalMotionHorizontalDepth = -1;
		}

		if (response != 0 && (~textbox->state & UI_STATE_DESTROYING)) {
			TextboxFindLongestLine(textbox);
			textbox->scroll.Refresh();
			EsTextboxEnsureCaretVisible(textbox);
			textbox->window->ensureVisible = textbox;
		}
	} else if (message->type == ES_MSG_MOUSE_LEFT_DOWN || message->type == ES_MSG_MOUSE_RIGHT_DOWN) {
		TextboxMoveCaretToCursor(textbox, message->mouseDown.positionX, message->mouseDown.positionY, message->type == ES_MSG_MOUSE_RIGHT_DOWN);
	} else if (message->type == ES_MSG_MOUSE_LEFT_CLICK) {
		EsTextboxStartEdit(textbox);
	} else if (message->type == ES_MSG_FOCUSED_START || message->type == ES_MSG_PRIMARY_CLIPBOARD_UPDATED) {
		TextboxUpdateCommands(textbox, false);
		EsInstanceSetActiveUndoManager(textbox->instance, textbox->undo);
		textbox->Repaint(true);
	} else if (message->type == ES_MSG_FOCUSED_END) {
		EsCommandSetCallback(EsCommandByID(textbox->instance, ES_COMMAND_SELECT_ALL), nullptr);
		EsCommandSetCallback(EsCommandByID(textbox->instance, ES_COMMAND_DELETE), nullptr);
		EsCommandSetCallback(EsCommandByID(textbox->instance, ES_COMMAND_COPY), nullptr);
		EsCommandSetCallback(EsCommandByID(textbox->instance, ES_COMMAND_CUT), nullptr);
		EsCommandSetCallback(EsCommandByID(textbox->instance, ES_COMMAND_PASTE), nullptr);
		EsInstanceSetActiveUndoManager(textbox->instance, textbox->instance->undoManager);
		textbox->Repaint(true);
	} else if (message->type == ES_MSG_STRONG_FOCUS_END) {
		TextboxEndEdit(textbox, textbox->flags & ES_TEXTBOX_REJECT_EDIT_IF_LOST_FOCUS);
	} else if (message->type == ES_MSG_MOUSE_LEFT_DRAG || message->type == ES_MSG_MOUSE_RIGHT_DRAG || (message->type == ES_MSG_ANIMATE && textbox->scroll.dragScrolling)) {
		int32_t lineFrom = textbox->carets[1].line;

		if (gui.lastClickButton == ES_MSG_MOUSE_RIGHT_DOWN && !textbox->inRightClickDrag) {
			TextboxMoveCaretToCursor(textbox, message->mouseDragged.originalPositionX, message->mouseDragged.originalPositionY, false);
			textbox->inRightClickDrag = true;
		}

		EsPoint position = EsMouseGetPosition(textbox); 
		TextboxFindCaret(textbox, position.x, position.y, true, gui.clickChainCount);

		int32_t lineTo = textbox->carets[1].line;
		if (lineFrom > lineTo) { int32_t t = lineTo; lineTo = lineFrom; lineFrom = t; }
		for (int32_t i = lineFrom; i <= lineTo; i++) TextboxRepaintLine(textbox, i);
	} else if (message->type == ES_MSG_GET_CURSOR) {
		if (!textbox->editing || (textbox->flags & ES_ELEMENT_DISABLED)) {
			message->cursorStyle = ES_CURSOR_NORMAL;
		} else {
			return 0;
		}
	} else if (message->type == ES_MSG_MOUSE_RIGHT_UP) {
		textbox->inRightClickDrag = false;
		EsMenu *menu = EsMenuCreate(textbox, ES_MENU_AT_CURSOR);
		if (!menu) return ES_HANDLED;

		// TODO User customisation of menus.

		if (textbox->editing) {
			EsMenuAddCommand(menu, 0, INTERFACE_STRING(CommonUndo), EsCommandByID(textbox->instance, ES_COMMAND_UNDO));
			EsMenuAddSeparator(menu);
		}

		EsMenuAddCommand(menu, 0, INTERFACE_STRING(CommonClipboardCut), EsCommandByID(textbox->instance, ES_COMMAND_CUT));
		EsMenuAddCommand(menu, 0, INTERFACE_STRING(CommonClipboardCopy), EsCommandByID(textbox->instance, ES_COMMAND_COPY));
		EsMenuAddCommand(menu, 0, INTERFACE_STRING(CommonClipboardPaste), EsCommandByID(textbox->instance, ES_COMMAND_PASTE));

		if (textbox->editing) {
			EsMenuAddSeparator(menu);
			EsMenuAddCommand(menu, 0, INTERFACE_STRING(CommonSelectionSelectAll), EsCommandByID(textbox->instance, ES_COMMAND_SELECT_ALL));
			EsMenuAddCommand(menu, 0, INTERFACE_STRING(CommonSelectionDelete), EsCommandByID(textbox->instance, ES_COMMAND_DELETE));

			// Add the smart context menu, if necessary.

			if ((~textbox->flags & ES_TEXTBOX_NO_SMART_CONTEXT_MENUS) && textbox->carets[0].line == textbox->carets[1].line) {
				int32_t selectionFrom = textbox->carets[0].byte, selectionTo = textbox->carets[1].byte;

				if (selectionTo < selectionFrom) {
					int32_t temporary = selectionFrom;
					selectionFrom = selectionTo;
					selectionTo = temporary;
				}

				if (selectionTo - selectionFrom == 7) {
					char buffer[7];
					EsMemoryCopy(buffer, textbox->lines[textbox->carets[0].line].GetBuffer(textbox) + selectionFrom, 7);

					if (buffer[0] == '#' && EsCRTisxdigit(buffer[1]) && EsCRTisxdigit(buffer[2]) && EsCRTisxdigit(buffer[3])
							&& EsCRTisxdigit(buffer[4]) && EsCRTisxdigit(buffer[5]) && EsCRTisxdigit(buffer[6])) {
						// It's a color hex-code!
						// TODO Versions with alpha.
						EsMenuNextColumn(menu);
						ColorPickerCreate(menu, { textbox }, EsColorParse(buffer, 7), false);

						textbox->colorUppercase = true;

						for (uintptr_t i = 1; i <= 6; i++) {
							if (buffer[i] >= 'a' && buffer[i] <= 'f') {
								textbox->colorUppercase = false;
								break;
							}
						}
					}
				}
			}
		}

		EsMenuShow(menu);
	} else if (message->type == ES_MSG_COLOR_CHANGED) {
		EsAssert(~textbox->flags & ES_TEXTBOX_NO_SMART_CONTEXT_MENUS); // Textbox sent color changed message, but it cannot have smart context menus?
		uint32_t color = message->colorChanged.newColor;

		if (message->colorChanged.pickerClosed) {
			int32_t selectionFrom = textbox->carets[0].byte, selectionTo = textbox->carets[1].byte;

			if (textbox->carets[0].line == textbox->carets[1].line && AbsoluteInteger(selectionFrom - selectionTo) == 7) {
				char buffer[7];
				const char *hexChars = textbox->colorUppercase ? "0123456789ABCDEF" : "0123456789abcedf";
				size_t length = EsStringFormat(buffer, 7, "#%c%c%c%c%c%c", 
						hexChars[(color >> 20) & 0xF], hexChars[(color >> 16) & 0xF], hexChars[(color >> 12) & 0xF], 
						hexChars[(color >> 8) & 0xF], hexChars[(color >> 4) & 0xF], hexChars[(color >> 0) & 0xF]);
				EsTextboxInsert(textbox, buffer, length, true);
				EsTextboxSetSelection(textbox, textbox->carets[1].line, textbox->carets[1].byte - 7, 
						textbox->carets[1].line, textbox->carets[1].byte);
			}
		}
	} else if (message->type == ES_MSG_GET_WIDTH) {
		message->measure.width = textbox->longestLineWidth + textbox->insets.l + textbox->insets.r;
	} else if (message->type == ES_MSG_GET_HEIGHT) {
		DocumentLine *lastLine = &textbox->lines.Last();
		message->measure.height = lastLine->yPosition + lastLine->height + textbox->insets.t + textbox->insets.b;
	} else if (message->type == ES_MSG_SCROLL_X) {
		TextboxSetHorizontalScroll(textbox, message->scrollbarMoved.scroll);
	} else if (message->type == ES_MSG_SCROLL_Y) {
		TextboxRefreshVisibleLines(textbox, false);
		EsElementRepaintForScroll(textbox, message, EsRectangleAdd(element->GetInternalOffset(), element->currentStyle->borders));
	} else if (message->type == ES_MSG_GET_INSPECTOR_INFORMATION) {
		DocumentLine *firstLine = &textbox->lines.First();
		EsBufferFormat(message->getContent.buffer, "'%s'", firstLine->lengthBytes, firstLine->GetBuffer(textbox));
	} else if (message->type == ES_MSG_UI_SCALE_CHANGED) {
		if (textbox->margin) {
			// Force the margin to update its style now, so that its width can be read correctly by TextboxStyleChanged.
			textbox->margin->RefreshStyle(nullptr, false, true);
		}

		textbox->currentStyle->GetTextStyle(&textbox->textStyle);

		if (textbox->overrideTextSize) {
			textbox->textStyle.size = textbox->overrideTextSize;
		}

		if (textbox->overrideFont.family) {
			textbox->textStyle.font = textbox->overrideFont;
		}

		TextboxStyleChanged(textbox);
	} else {
		response = 0;
	}

	return response;
}

EsTextbox *EsTextboxCreate(EsElement *parent, uint64_t flags, const EsStyle *style) {
	EsTextbox *textbox = (EsTextbox *) EsHeapAllocate(sizeof(EsTextbox), true);
	if (!textbox) return nullptr;

	if (!style) {
		if (flags & ES_TEXTBOX_MULTILINE) {
			style = ES_STYLE_TEXTBOX_BORDERED_MULTILINE;
		} else {
			style = ES_STYLE_TEXTBOX_BORDERED_SINGLE;
		}
	}

	textbox->Initialise(parent, ES_ELEMENT_FOCUSABLE | flags, ProcessTextboxMessage, style);
	textbox->cName = "textbox";

	textbox->scroll.Setup(textbox, 
			(flags & ES_TEXTBOX_MULTILINE) ? SCROLL_MODE_AUTO : SCROLL_MODE_HIDDEN, 
			(flags & ES_TEXTBOX_MULTILINE) ? SCROLL_MODE_AUTO : SCROLL_MODE_NONE,
			SCROLL_X_DRAG | SCROLL_Y_DRAG);

	textbox->undo = &textbox->localUndo;
	textbox->undo->instance = textbox->instance;

	textbox->borders = textbox->currentStyle->borders;
	textbox->insets = textbox->currentStyle->insets;

	textbox->currentStyle->GetTextStyle(&textbox->textStyle);

	textbox->smartQuotes = true;

	DocumentLine firstLine = {};
	firstLine.height = TextGetLineHeight(textbox, &textbox->textStyle);
	textbox->lines.Add(firstLine);

	TextboxVisibleLine firstVisibleLine = {};
	textbox->visibleLines.Add(firstVisibleLine);

	textbox->activeLineIndex = textbox->verticalMotionHorizontalDepth = textbox->longestLine = -1;

	if (~flags & ES_TEXTBOX_EDIT_BASED) {
		textbox->editing = true;
	}

	if (textbox->flags & ES_TEXTBOX_MARGIN) {
		textbox->margin = EsCustomElementCreate(textbox, ES_CELL_FILL, ES_STYLE_TEXTBOX_MARGIN);
		textbox->margin->cName = "margin";
		textbox->margin->messageUser = ProcessTextboxMarginMessage;

		int marginWidth = textbox->margin->currentStyle->preferredWidth;
		textbox->borders.l += marginWidth;
		textbox->insets.l += marginWidth + textbox->margin->currentStyle->gapMajor;
	}

	return textbox;
}

void EsTextboxUseNumberOverlay(EsTextbox *textbox, bool defaultBehaviour) {
	EsMessageMutexCheck();

	EsAssert(textbox->flags & ES_TEXTBOX_EDIT_BASED); // Using textbox overlay without edit based mode.
	EsAssert(~textbox->flags & ES_TEXTBOX_MULTILINE); // Using number overlay with multiline mode.

	textbox->overlayData = defaultBehaviour;

	textbox->overlayCallback = [] (EsElement *element, EsMessage *message) {
		EsTextbox *textbox = (EsTextbox *) element;
		bool defaultBehaviour = textbox->overlayData.u;

		if (message->type == ES_MSG_MOUSE_LEFT_DRAG) {
			if (!gui.draggingStarted) {
				EsMessage m = { ES_MSG_TEXTBOX_NUMBER_DRAG_START };
				EsMessageSend(textbox, &m);
			}

			TextboxFindCaret(textbox, message->mouseDragged.originalPositionX, message->mouseDragged.originalPositionY, false, 1);
			
			EsMessage m = { ES_MSG_TEXTBOX_NUMBER_DRAG_DELTA };
			m.numberDragDelta.delta = message->mouseDragged.originalPositionY - message->mouseDragged.newPositionY;
			m.numberDragDelta.fast = EsKeyboardIsShiftHeld();
			m.numberDragDelta.hoverCharacter = textbox->carets[1].byte;
			EsMessageSend(textbox, &m);

			EsMouseSetPosition(textbox->window, gui.lastClickX, gui.lastClickY);
		} else if (message->type == ES_MSG_KEY_TYPED && message->keyboard.scancode == ES_SCANCODE_UP_ARROW) {
			EsMessage m = { ES_MSG_TEXTBOX_NUMBER_DRAG_DELTA };
			m.numberDragDelta.delta = 1;
			m.numberDragDelta.fast = EsKeyboardIsShiftHeld();
			m.numberDragDelta.hoverCharacter = 0;
			EsMessageSend(textbox, &m);
		} else if (message->type == ES_MSG_KEY_TYPED && message->keyboard.scancode == ES_SCANCODE_DOWN_ARROW) {
			EsMessage m = { ES_MSG_TEXTBOX_NUMBER_DRAG_DELTA };
			m.numberDragDelta.delta = -1;
			m.numberDragDelta.fast = EsKeyboardIsShiftHeld();
			m.numberDragDelta.hoverCharacter = 0;
			EsMessageSend(textbox, &m);
		} else if (message->type == ES_MSG_MOUSE_LEFT_UP) {
			if (gui.draggingStarted) {
				EsMessage m = { ES_MSG_TEXTBOX_NUMBER_DRAG_END };
				EsMessageSend(textbox, &m);
			}
		} else if (message->type == ES_MSG_GET_CURSOR) {
			if (gui.draggingStarted) {
				message->cursorStyle = ES_CURSOR_BLANK;
			} else if (~textbox->flags & ES_ELEMENT_DISABLED) {
				message->cursorStyle = ES_CURSOR_RESIZE_VERTICAL;
			} else {
				message->cursorStyle = ES_CURSOR_NORMAL;
			}
		} else if (message->type == ES_MSG_TEXTBOX_EDIT_END && defaultBehaviour) {
			double oldValue = EsDoubleParse(textbox->editStartContent, textbox->editStartContentBytes, nullptr);

			char *expression = EsTextboxGetContents(textbox);
			EsCalculationValue value = EsCalculateFromUserExpression(expression); 
			EsHeapFree(expression);

			if (value.error) {
				return ES_REJECTED;
			} else {
				EsMessage m = { ES_MSG_TEXTBOX_NUMBER_UPDATED };
				m.numberUpdated.delta = value.number - oldValue;
				m.numberUpdated.newValue = value.number;
				EsMessageSend(textbox, &m);

				char result[64];
				size_t resultBytes = EsStringFormat(result, sizeof(result), "%F", (double) m.numberUpdated.newValue);
				EsTextboxSelectAll(textbox);
				EsTextboxInsert(textbox, result, resultBytes);
			}
		} else if (message->type == ES_MSG_TEXTBOX_NUMBER_DRAG_DELTA && defaultBehaviour) {
			TextboxSetActiveLine(textbox, -1);
			double oldValue = EsDoubleParse(textbox->data, textbox->lines[0].lengthBytes, nullptr);
			double newValue = oldValue + message->numberDragDelta.delta * (message->numberDragDelta.fast ? 10 : 1);

			EsMessage m = { ES_MSG_TEXTBOX_NUMBER_UPDATED };
			m.numberUpdated.delta = newValue - oldValue;
			m.numberUpdated.newValue = newValue;
			EsMessageSend(textbox, &m);

			char result[64];
			size_t resultBytes = EsStringFormat(result, sizeof(result), "%F", m.numberUpdated.newValue);
			EsTextboxSelectAll(textbox);
			EsTextboxInsert(textbox, result, resultBytes);
		} else {
			return 0;
		}

		return ES_HANDLED;
	};
}

void TextboxBreadcrumbOverlayRecreate(EsTextbox *textbox) {
	if (textbox->overlayData.p) {
		// Remove the old breadcrumb panel.
		((EsElement *) textbox->overlayData.p)->Destroy();
	}

	EsPanel *panel = EsPanelCreate(textbox, ES_PANEL_HORIZONTAL | ES_CELL_FILL | ES_ELEMENT_NO_HOVER, ES_STYLE_BREADCRUMB_BAR_PANEL);
	textbox->overlayData = panel;

	if (!panel) {
		return;
	}

	uint8_t _buffer[256];
	EsBuffer buffer = { .out = _buffer, .bytes = sizeof(_buffer) };
	EsMessage m = { ES_MSG_TEXTBOX_GET_BREADCRUMB };
	m.getBreadcrumb.buffer = &buffer;

	while (true) {
		buffer.position = 0;
		int response = EsMessageSend(textbox, &m);
		EsAssert(response != 0); // Must handle ES_MSG_TEXTBOX_GET_BREADCRUMB message for breadcrumb overlay.
		if (response == ES_REJECTED) break;

		EsButton *crumb = EsButtonCreate(panel, ES_BUTTON_NOT_FOCUSABLE | ES_BUTTON_COMPACT | ES_CELL_V_FILL, 
				ES_STYLE_BREADCRUMB_BAR_CRUMB, (char *) buffer.out, buffer.position);

		if (crumb) {
			crumb->userData = m.getBreadcrumb.index;

			crumb->messageUser = [] (EsElement *element, EsMessage *message) {
				if (message->type == ES_MSG_MOUSE_LEFT_CLICK) {
					EsMessage m = { ES_MSG_TEXTBOX_ACTIVATE_BREADCRUMB };
					m.activateBreadcrumb = element->userData.u;
					EsMessageSend(element->parent->parent, &m);
				} else {
					return 0;
				}

				return ES_HANDLED;
			};
		}
		
		m.getBreadcrumb.index++;
	}
}

void EsTextboxUseBreadcrumbOverlay(EsTextbox *textbox) {
	EsMessageMutexCheck();

	EsAssert(textbox->flags & ES_TEXTBOX_EDIT_BASED); // Using textbox overlay without edit based mode.

	// Use this to store the panel containing the breadcrumb buttons.
	textbox->overlayData = nullptr;

	textbox->overlayCallback = [] (EsElement *element, EsMessage *message) {
		EsTextbox *textbox = (EsTextbox *) element;

		if (message->type == ES_MSG_TEXTBOX_UPDATED) {
			TextboxBreadcrumbOverlayRecreate(textbox);
		} else if (message->type == ES_MSG_TEXTBOX_EDIT_START) {
			((EsElement *) textbox->overlayData.p)->Destroy();
			textbox->overlayData.p = nullptr;
		} else if (message->type == ES_MSG_TEXTBOX_EDIT_END) {
			TextboxBreadcrumbOverlayRecreate(textbox);
		} else if (message->type == ES_MSG_LAYOUT) {
			EsRectangle bounds = textbox->GetBounds();
			((EsElement *) textbox->overlayData.p)->InternalMove(bounds.r, bounds.b, 0, 0);
		} else if (message->type == ES_MSG_PAINT) {
			return ES_HANDLED;
		}

		return 0;
	};

	TextboxBreadcrumbOverlayRecreate(textbox);
}

void EsTextboxSetUndoManager(EsTextbox *textbox, EsUndoManager *undoManager) {
	EsMessageMutexCheck();
	EsAssert(~textbox->state & UI_STATE_FOCUSED); // Can't change undo manager if the textbox is focused.
	EsAssert(textbox->undo == &textbox->localUndo); // This can only be set once.
	textbox->undo = undoManager;
}

void EsTextboxSetTextSize(EsTextbox *textbox, uint16_t size) {
	textbox->overrideTextSize = size;
	textbox->textStyle.size = size;
	TextboxStyleChanged(textbox);
}

void EsTextboxSetFont(EsTextbox *textbox, EsFont font) {
	textbox->overrideFont = font;
	textbox->textStyle.font = font;
	TextboxStyleChanged(textbox);
}

void EsTextboxSetupSyntaxHighlighting(EsTextbox *textbox, uint32_t language, uint32_t *customColors, size_t customColorCount) {
	textbox->syntaxHighlightingLanguage = language;

	// TODO Load these from the theme file.
	textbox->syntaxHighlightingColors[0] = 0x04000000; // Highlighted line.
	textbox->syntaxHighlightingColors[1] = 0xFF000000; // Default.
	textbox->syntaxHighlightingColors[2] = 0xFFA11F20; // Comment.
	textbox->syntaxHighlightingColors[3] = 0xFF037E01; // String.
	textbox->syntaxHighlightingColors[4] = 0xFF213EF1; // Number.
	textbox->syntaxHighlightingColors[5] = 0xFF7F0480; // Operator.
	textbox->syntaxHighlightingColors[6] = 0xFF545D70; // Preprocessor.
	textbox->syntaxHighlightingColors[7] = 0xFF17546D; // Keyword.

	if (customColorCount > sizeof(textbox->syntaxHighlightingColors) / sizeof(uint32_t)) {
		customColorCount = sizeof(textbox->syntaxHighlightingColors) / sizeof(uint32_t);
	}

	EsMemoryCopy(textbox->syntaxHighlightingColors, customColors, customColorCount * sizeof(uint32_t));

	textbox->Repaint(true);
}

void EsTextboxEnableSmartQuotes(EsTextbox *textbox, bool enabled) {
	textbox->smartQuotes = enabled;
}

// --------------------------------- Text displays.

// TODO Inline images and icons.
// TODO Links.
// TODO Inline backgrounds.

void TextDisplayFreeRuns(EsTextDisplay *display) {
	if (display->usingSyntaxHighlighting) {
		Array<EsTextRun> textRuns = { display->textRuns };
		textRuns.Free();
	} else {
		EsHeapFree(display->textRuns);
	}
}

int ProcessTextDisplayMessage(EsElement *element, EsMessage *message) {
	EsTextDisplay *display = (EsTextDisplay *) element;

	if (message->type == ES_MSG_PAINT) {
		EsRectangle textBounds = EsPainterBoundsInset(message->painter);

		if (!display->plan || display->planWidth != textBounds.r - textBounds.l || display->planHeight != textBounds.b - textBounds.t) {
			if (display->plan) EsTextPlanDestroy(display->plan);
			display->properties.flags = display->currentStyle->textAlign;
			if (~display->flags & ES_TEXT_DISPLAY_PREFORMATTED) display->properties.flags |= ES_TEXT_PLAN_TRIM_SPACES;
			if (display->flags & ES_TEXT_DISPLAY_NO_FONT_SUBSTITUTION) display->properties.flags |= ES_TEXT_PLAN_NO_FONT_SUBSTITUTION;
			display->plan = EsTextPlanCreate(element, &display->properties, textBounds, display->contents, display->textRuns, display->textRunCount);
			display->planWidth = textBounds.r - textBounds.l;
			display->planHeight = textBounds.b - textBounds.t;
		}

		if (display->plan) {
			EsDrawTextLayers(message->painter, display->plan, EsPainterBoundsInset(message->painter)); 
		}
	} else if (message->type == ES_MSG_GET_WIDTH || message->type == ES_MSG_GET_HEIGHT) {
		if (!display->measurementCache.Get(message, &display->state)) {
			if (display->plan) EsTextPlanDestroy(display->plan);
			display->properties.flags = display->currentStyle->textAlign | ((display->flags & ES_TEXT_DISPLAY_PREFORMATTED) ? 0 : ES_TEXT_PLAN_TRIM_SPACES);
			EsRectangle insets = EsElementGetInsets(element);
			display->planWidth = message->type == ES_MSG_GET_HEIGHT && message->measure.width 
				? (message->measure.width - insets.l - insets.r) : 0;
			display->planHeight = 0;
			display->plan = EsTextPlanCreate(element, &display->properties, 
					ES_RECT_4(0, display->planWidth, 0, 0), 
					display->contents, display->textRuns, display->textRunCount);

			if (!display->plan) {
				message->measure.width = message->measure.height = 0;
			} else {
				if (message->type == ES_MSG_GET_WIDTH) {
					message->measure.width = EsTextPlanGetWidth(display->plan) + insets.l + insets.r;
				} else {
					message->measure.height = EsTextPlanGetHeight(display->plan) + insets.t + insets.b;
				}
			}

			display->measurementCache.Store(message);
		}
	} else if (message->type == ES_MSG_DESTROY) {
		if (display->plan) {
			EsTextPlanDestroy(display->plan);
		}

		TextDisplayFreeRuns(display);
		EsHeapFree(display->contents);
	} else if (message->type == ES_MSG_GET_INSPECTOR_INFORMATION) {
		EsBufferFormat(message->getContent.buffer, "'%s'", display->textRuns[display->textRunCount].offset, display->contents);
	} else if (message->type == ES_MSG_UI_SCALE_CHANGED) {
		if (display->plan) {
			EsTextPlanDestroy(display->plan);
			display->plan = nullptr;
		}
	} else {
		return 0;
	}

	return ES_HANDLED;
}

void EsTextDisplaySetStyledContents(EsTextDisplay *display, const char *string, EsTextRun *runs, size_t runCount) {
	TextDisplayFreeRuns(display);

	display->textRuns = (EsTextRun *) EsHeapAllocate(sizeof(EsTextRun) * (runCount + 1), true);
	display->textRunCount = runCount;

	size_t outBytes;
	HeapDuplicate((void **) &display->contents, &outBytes, string, runs[runCount].offset);

	if (outBytes != runs[runCount].offset) {
		// TODO Handle allocation failure.
	}

	EsMemoryCopy(display->textRuns, runs, sizeof(EsTextRun) * (runCount + 1));

	display->usingSyntaxHighlighting = false;
	EsElementUpdateContentSize(display);
	InspectorNotifyElementContentChanged(display);
}

void EsTextDisplaySetContents(EsTextDisplay *display, const char *string, ptrdiff_t stringBytes) {
	if (stringBytes == -1) stringBytes = EsCStringLength(string);

	TextDisplayFreeRuns(display);

	if (display->flags & ES_TEXT_DISPLAY_RICH_TEXT) {
		EsHeapFree(display->contents);
		EsTextStyle baseStyle = {};
		display->currentStyle->GetTextStyle(&baseStyle);
		EsRichTextParse(string, stringBytes, &display->contents, &display->textRuns, &display->textRunCount, &baseStyle);
	} else {
		HeapDuplicate((void **) &display->contents, (size_t *) &stringBytes, string, stringBytes);
		display->textRuns = (EsTextRun *) EsHeapAllocate(sizeof(EsTextRun) * 2, true);
		display->currentStyle->GetTextStyle(&display->textRuns[0].style);
		display->textRuns[1].offset = stringBytes;
		display->textRunCount = 1;
	}

	display->usingSyntaxHighlighting = false;
	EsElementUpdateContentSize(display);
	InspectorNotifyElementContentChanged(display);
}

EsTextDisplay *EsTextDisplayCreate(EsElement *parent, uint64_t flags, const EsStyle *style, const char *label, ptrdiff_t labelBytes) {
	EsTextDisplay *display = (EsTextDisplay *) EsHeapAllocate(sizeof(EsTextDisplay), true);
	if (!display) return nullptr;
	display->Initialise(parent, flags, ProcessTextDisplayMessage, style ?: UIGetDefaultStyleVariant(ES_STYLE_TEXT_LABEL, parent));
	display->cName = "text display";
	if (labelBytes == -1) labelBytes = EsCStringLength(label);
	EsTextDisplaySetContents(display, label, labelBytes);
	return display;
}

void EsTextDisplaySetupSyntaxHighlighting(EsTextDisplay *display, uint32_t language, uint32_t *customColors, size_t customColorCount) {
	// Copied from EsTextboxSetupSyntaxHighlighting.
	uint32_t colors[8];
	colors[0] = 0x04000000; // Highlighted line.
	colors[1] = 0xFF000000; // Default.
	colors[2] = 0xFFA11F20; // Comment.
	colors[3] = 0xFF037E01; // String.
	colors[4] = 0xFF213EF1; // Number.
	colors[5] = 0xFF7F0480; // Operator.
	colors[6] = 0xFF545D70; // Preprocessor.
	colors[7] = 0xFF17546D; // Keyword.

	if (customColorCount > sizeof(colors) / sizeof(uint32_t)) customColorCount = sizeof(colors) / sizeof(uint32_t);
	EsMemoryCopy(colors, customColors, customColorCount * sizeof(uint32_t));

	EsTextStyle textStyle = {};
	display->currentStyle->GetTextStyle(&textStyle);

	EsTextRun *newRuns = TextApplySyntaxHighlighting(&textStyle, language, colors, {}, 
			display->contents, display->textRuns[display->textRunCount].offset).array;
	TextDisplayFreeRuns(display);
	display->textRuns = newRuns;
	display->textRunCount = ArrayLength(display->textRuns) - 1;
	display->usingSyntaxHighlighting = true;
	display->Repaint(true);
}

// --------------------------------- List displays.

struct EsListDisplay : EsElement {
	uintptr_t itemCount, startIndex;
	EsListDisplay *previous;
};

int ProcessListDisplayMessage(EsElement *element, EsMessage *message) {
	EsListDisplay *display = (EsListDisplay *) element;

	if (message->type == ES_MSG_GET_HEIGHT) {
		int32_t height = 0;
		int32_t margin = element->currentStyle->insets.l + element->currentStyle->insets.r + element->currentStyle->gapMinor;
		uintptr_t itemCount = 0;

		for (uintptr_t i = 0; i < element->GetChildCount(); i++) {
			EsElement *child = element->GetChild(i);
			if (child->flags & ES_ELEMENT_NON_CLIENT) continue;
			height += child->GetHeight(message->measure.width - margin);
			itemCount++;
		}

		if (itemCount) {
			height += (itemCount - 1) * element->currentStyle->gapMajor;
		}

		message->measure.height = height + element->currentStyle->insets.t + element->currentStyle->insets.b;
	} else if (message->type == ES_MSG_LAYOUT) {
		int32_t position = element->currentStyle->insets.t;
		int32_t margin = element->currentStyle->insets.l + element->currentStyle->gapMinor;
		int32_t width = element->width - margin - element->currentStyle->insets.r;

		for (uintptr_t i = 0; i < element->GetChildCount(); i++) {
			EsElement *child = element->GetChild(i);
			if (child->flags & ES_ELEMENT_NON_CLIENT) continue;
			int height = child->GetHeight(width);
			EsElementMove(child, margin, position, width, height);
			position += height + element->currentStyle->gapMajor;
		}
	} else if (message->type == ES_MSG_PAINT) {
		char buffer[64];
		EsTextPlanProperties properties = {};
		properties.flags = ES_TEXT_H_RIGHT | ES_TEXT_V_TOP | ES_TEXT_PLAN_SINGLE_USE;
		EsTextRun textRun[2] = {};

		EsRectangle bounds = EsPainterBoundsClient(message->painter); 
		bounds.r = bounds.l + element->currentStyle->insets.l;

		uintptr_t counter = display->previous ? display->previous->itemCount : display->startIndex;
		uint8_t markerType = element->flags & ES_LIST_DISPLAY_MARKER_TYPE_MASK;

		EsMessage m = {};
		m.type = ES_MSG_LIST_DISPLAY_GET_MARKER;
		EsBuffer buffer2 = { .out = (uint8_t *) buffer, .bytes = sizeof(buffer) };
		m.getContent.buffer = &buffer2;

		for (uintptr_t i = 0; i < element->GetChildCount(); i++) {
			EsElement *child = element->GetChild(i);
			if (child->flags & ES_ELEMENT_NON_CLIENT) continue;

			if (markerType == ES_LIST_DISPLAY_BULLETED) {
				EsMemoryCopy(buffer, "\xE2\x80\xA2", (textRun[1].offset = 3));
			} else if (markerType == ES_LIST_DISPLAY_NUMBERED) {
				textRun[1].offset = EsStringFormat(buffer, sizeof(buffer), "%d.", counter + 1);
			} else if (markerType == ES_LIST_DISPLAY_LOWER_ALPHA) {
				textRun[1].offset = EsStringFormat(buffer, sizeof(buffer), "(%c)", counter + 'a');
			} else if (markerType == ES_LIST_DISPLAY_CUSTOM_MARKER) {
				m.getContent.index = counter;
				EsMessageSend(element, &m);
				textRun[1].offset = buffer2.position;
			} else {
				EsAssert(false);
			}

			child->currentStyle->GetTextStyle(&textRun[0].style);
			textRun[0].style.figures = ES_TEXT_FIGURE_TABULAR;
			bounds.t += child->offsetY;
			bounds.b = bounds.t + child->height;
			EsTextPlan *plan = EsTextPlanCreate(element, &properties, bounds, buffer, textRun, 1);
			if (plan) EsDrawText(message->painter, plan, bounds); 
			bounds.t -= child->offsetY;
			counter++;
		}
	} else if (message->type == ES_MSG_ADD_CHILD) {
		display->itemCount++;
	} else if (message->type == ES_MSG_REMOVE_CHILD) {
		display->itemCount--;
	}

	return 0;
}

EsListDisplay *EsListDisplayCreate(EsElement *parent, uint64_t flags, const EsStyle *style) {
	EsListDisplay *display = (EsListDisplay *) EsHeapAllocate(sizeof(EsListDisplay), true);
	if (!display) return nullptr;
	display->Initialise(parent, flags, ProcessListDisplayMessage, style ?: ES_STYLE_LIST_DISPLAY_DEFAULT);
	display->cName = "list display";
	return display;
}

void EsListDisplaySetCounterContinuation(EsListDisplay *display, EsListDisplay *previous) {
	display->previous = previous;
	EsElementRepaint(display);
}

void EsListDisplaySetCounterStart(EsListDisplay *display, uintptr_t index) {
	display->startIndex = index;
	display->previous = nullptr;
	EsElementRepaint(display);
}

#endif
